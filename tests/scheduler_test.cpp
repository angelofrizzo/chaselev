#include <chaselev/scheduler.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using chaselev::Scheduler;
using chaselev::TaskGroup;

namespace {

std::int64_t fib(std::int64_t n) {
  if (n < 2) return n;
  std::int64_t a = 0;
  TaskGroup g;
  g.spawn([&] { a = fib(n - 1); });
  std::int64_t b = fib(n - 2);
  g.sync();
  return a + b;
}

const unsigned all_cores = std::max(2u, std::thread::hardware_concurrency());

}  // namespace

TEST_CASE("fib with 1, 2 and all workers") {
  const unsigned threads = GENERATE(1u, 2u, all_cores);
  Scheduler s(threads);
  REQUIRE(s.size() == threads);
  std::int64_t r = 0;
  s.run([&] { r = fib(25); });
  CHECK(r == 75025);
}

TEST_CASE("every spawned task runs exactly once") {
  Scheduler s(all_cores);
  constexpr int n = 100'000;
  std::vector<std::atomic<int>> hits(n);
  s.run([&] {
    TaskGroup g;
    for (int i = 0; i < n; ++i)
      g.spawn([&hits, i] { hits[i].fetch_add(1, std::memory_order_relaxed); });
  });
  CHECK(std::ranges::all_of(hits, [](auto& h) { return h.load() == 1; }));
}

TEST_CASE("TaskGroup destructor joins its children") {
  Scheduler s(all_cores);
  std::atomic<int> count{0};
  int seen = -1;
  s.run([&] {
    {
      TaskGroup g;
      for (int i = 0; i < 1000; ++i)
        g.spawn([&] { count.fetch_add(1, std::memory_order_relaxed); });
    }
    seen = count.load(std::memory_order_relaxed);
  });
  CHECK(seen == 1000);
}

TEST_CASE("run can be called repeatedly") {
  Scheduler s(all_cores);
  for (int round = 0; round < 200; ++round) {
    std::int64_t r = 0;
    s.run([&] { r = fib(12); });
    REQUIRE(r == 144);
  }
}

TEST_CASE("worker_index") {
  CHECK(Scheduler::worker_index() == -1);
  Scheduler s(2);
  int inside = -2;
  s.run([&] { inside = Scheduler::worker_index(); });
  CHECK(inside == 0);
  CHECK(Scheduler::worker_index() == -1);
}

TEST_CASE("work is stolen by other workers") {
  const unsigned threads = std::min(all_cores, 4u);
  Scheduler s(threads);
  std::vector<std::atomic<bool>> ran_on(threads);
  s.run([&] {
    TaskGroup g;
    for (int i = 0; i < 1000; ++i)
      g.spawn([&] {
        ran_on[Scheduler::worker_index()].store(true, std::memory_order_relaxed);
        auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(20);
        while (std::chrono::steady_clock::now() < until) {
        }
      });
  });
  auto workers = std::ranges::count_if(ran_on, [](auto& b) { return b.load(); });
  INFO("tasks ran on " << workers << " of " << threads << " workers");
  CHECK(workers > 1);
}
