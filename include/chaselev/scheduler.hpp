#pragma once

// Work-stealing scheduler: one Chase–Lev deque per worker, randomized
// stealing, Cilk-style spawn/sync through TaskGroup.
//
//   chaselev::Scheduler s(8);
//   s.run([] {
//     chaselev::TaskGroup g;
//     g.spawn([] { ... });
//     g.spawn([] { ... });
//     g.sync();
//   });
//
// The thread calling run() is worker 0 for the duration of the call; the
// other workers are threads owned by the scheduler. A worker waiting in
// sync() never blocks: it pops its own deque or steals until the group's
// children are done, so any thread count >= 1 makes progress.
//
// Tasks must not throw: an escaping exception calls std::terminate.

#include <chaselev/deque.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace chaselev {

class Scheduler;
class TaskGroup;

namespace detail {

struct Task {
  void (*invoke)(Task*) noexcept;  // runs the task, then deletes it
  TaskGroup* group;
};

template <typename F>
struct FnTask final : Task {
  template <typename G>
  FnTask(G&& g, TaskGroup* grp) : Task{&FnTask::invoke, grp}, f(std::forward<G>(g)) {}

  static void invoke(Task* t) noexcept {
    auto* self = static_cast<FnTask*>(t);
    self->f();
    delete self;
  }

  F f;
};

struct Worker {
  explicit Worker(Scheduler* s, unsigned i)
      : sched(s), index(i), rng(0x9E3779B97F4A7C15ull * (i + 1)) {}

  Deque<Task*> deque;
  Scheduler* sched;
  unsigned index;
  std::uint64_t rng;  // xorshift64 state, never zero
};

inline thread_local Worker* this_worker = nullptr;

inline std::uint64_t next_random(std::uint64_t& s) noexcept {
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return s;
}

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  // isb, not yield: yield is a no-op on most ARM cores, isb actually stalls.
  __asm__ __volatile__("isb" ::: "memory");
#endif
}

// Spin with exponentially more pauses, then fall back to yielding the core.
class Backoff {
 public:
  void pause() noexcept {
    if (step_ < spin_limit) {
      for (unsigned i = 0; i < (1u << step_); ++i) cpu_relax();
      ++step_;
    } else {
      std::this_thread::yield();
    }
  }
  void reset() noexcept { step_ = 0; }

 private:
  static constexpr unsigned spin_limit = 6;
  unsigned step_ = 0;
};

void execute(Task* t) noexcept;

}  // namespace detail

// Children spawned through a group are joined by sync() or, implicitly, by
// the destructor. Only the worker that created the group may spawn into it
// or sync it.
class TaskGroup {
 public:
  TaskGroup() = default;
  TaskGroup(const TaskGroup&) = delete;
  TaskGroup& operator=(const TaskGroup&) = delete;
  ~TaskGroup() { sync(); }

  // Must be called from inside Scheduler::run.
  template <typename F>
  void spawn(F&& f) {
    detail::Worker* w = detail::this_worker;
    assert(w && "TaskGroup::spawn called outside Scheduler::run");
    // Relaxed is enough: the thief that decrements has acquired the task
    // through the deque, which orders it after this increment.
    pending_.fetch_add(1, std::memory_order_relaxed);
    w->deque.push(new detail::FnTask<std::decay_t<F>>(std::forward<F>(f), this));
  }

  // Returns once every child spawned so far has finished; their writes are
  // visible to the caller.
  void sync();

 private:
  friend void detail::execute(detail::Task*) noexcept;

  std::atomic<std::int64_t> pending_{0};
};

namespace detail {

inline void execute(Task* t) noexcept {
  TaskGroup* g = t->group;
  t->invoke(t);
  g->pending_.fetch_sub(1, std::memory_order_release);
}

}  // namespace detail

class Scheduler {
 public:
  explicit Scheduler(unsigned threads = std::thread::hardware_concurrency()) {
    threads = std::max(threads, 1u);
    workers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i)
      workers_.push_back(std::make_unique<detail::Worker>(this, i));
    threads_.reserve(threads - 1);
    for (unsigned i = 1; i < threads; ++i)
      threads_.emplace_back([this, i] { worker_loop(*workers_[i]); });
  }

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  ~Scheduler() {
    state_.store(stopping, std::memory_order_release);
    state_.notify_all();
    for (auto& t : threads_) t.join();
  }

  unsigned size() const { return static_cast<unsigned>(workers_.size()); }

  // Runs f on the calling thread as worker 0 and returns when it does.
  // Every task f spawns is joined by then, since TaskGroups sync on
  // destruction. Calls from different threads are serialized; calls from
  // inside a task are not allowed.
  template <typename F>
  void run(F&& f) {
    assert(!detail::this_worker && "Scheduler::run called from inside a task");
    std::lock_guard lock(run_mutex_);
    struct Guard {
      Scheduler* s;
      ~Guard() {
        s->state_.store(idle, std::memory_order_relaxed);
        detail::this_worker = nullptr;
      }
    } guard{this};
    detail::this_worker = workers_[0].get();
    state_.store(running, std::memory_order_release);
    state_.notify_all();
    std::forward<F>(f)();
  }

  // Index of the calling worker in [0, size()), or -1 outside any worker.
  static int worker_index() {
    return detail::this_worker ? static_cast<int>(detail::this_worker->index) : -1;
  }

 private:
  friend class TaskGroup;

  enum State : int { idle, running, stopping };

  // Own deque first (LIFO, cache-warm), else one steal attempt from a
  // uniformly random other worker. Callers loop with backoff.
  detail::Task* find_work(detail::Worker& w) {
    if (auto t = w.deque.pop()) return *t;
    const auto n = static_cast<std::uint64_t>(workers_.size());
    if (n == 1) return nullptr;
    const auto v = (w.index + 1 + detail::next_random(w.rng) % (n - 1)) % n;
    if (auto t = workers_[v]->deque.steal()) return *t;
    return nullptr;
  }

  void worker_loop(detail::Worker& w) {
    detail::this_worker = &w;
    detail::Backoff backoff;
    for (;;) {
      const int s = state_.load(std::memory_order_acquire);
      if (s == stopping) return;
      if (s == idle) {
        state_.wait(idle, std::memory_order_acquire);
        continue;
      }
      if (detail::Task* t = find_work(w)) {
        detail::execute(t);
        backoff.reset();
      } else {
        backoff.pause();
      }
    }
  }

  std::vector<std::unique_ptr<detail::Worker>> workers_;
  std::vector<std::thread> threads_;
  alignas(cache_line) std::atomic<int> state_{idle};
  std::mutex run_mutex_;
};

inline void TaskGroup::sync() {
  if (pending_.load(std::memory_order_acquire) == 0) return;
  detail::Worker* w = detail::this_worker;
  detail::Backoff backoff;
  while (pending_.load(std::memory_order_acquire) != 0) {
    if (detail::Task* t = w->sched->find_work(*w)) {
      detail::execute(t);
      backoff.reset();
    } else {
      backoff.pause();
    }
  }
}

}  // namespace chaselev
