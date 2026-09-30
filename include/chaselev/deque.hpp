#pragma once

// Chase–Lev work-stealing deque, with the memory orderings from
// Lê, Pop, Cohen, Zappa Nardelli, "Correct and Efficient Work-Stealing for
// Weak Memory Models" (PPoPP 2013), Fig. 1.
//
// One owner thread calls push/pop at the bottom; any thread may call steal
// at the top. Slots are atomics because a thief may read a slot that the
// owner is overwriting; the thief's CAS on top then fails and the value is
// discarded.

#include <chaselev/ring.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace chaselev {

// 128, not 64: Apple M-series cores prefetch pairs of 64-byte lines.
inline constexpr std::size_t cache_line = 128;

template <typename T>
  requires std::is_trivially_copyable_v<T>
class Deque {
 public:
  explicit Deque(std::int64_t capacity = 64) {
    rings_.push_back(std::make_unique<Ring<T>>(capacity));
    ring_.store(rings_.back().get(), std::memory_order_relaxed);
  }

  Deque(const Deque&) = delete;
  Deque& operator=(const Deque&) = delete;

  // Owner only.
  void push(T x) {
    std::int64_t b = bottom_.load(std::memory_order_relaxed);
    std::int64_t t = top_.load(std::memory_order_acquire);
    Ring<T>* r = ring_.load(std::memory_order_relaxed);
    if (b - t > r->capacity() - 1) r = grow(r, t, b);
    r->put(b, x);
    std::atomic_thread_fence(std::memory_order_release);
    bottom_.store(b + 1, std::memory_order_relaxed);
  }

  // Owner only. LIFO end.
  std::optional<T> pop() {
    std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
    Ring<T>* r = ring_.load(std::memory_order_relaxed);
    bottom_.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::int64_t t = top_.load(std::memory_order_relaxed);

    if (t > b) {  // empty
      bottom_.store(b + 1, std::memory_order_relaxed);
      return std::nullopt;
    }
    T x = r->get(b);
    if (t == b) {  // last element: race thieves for it
      bool won = top_.compare_exchange_strong(
          t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
      bottom_.store(b + 1, std::memory_order_relaxed);
      if (!won) return std::nullopt;
    }
    return x;
  }

  // Any thread. FIFO end. Returns nullopt if empty or if it lost a race.
  std::optional<T> steal() {
    std::int64_t t = top_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::int64_t b = bottom_.load(std::memory_order_acquire);
    if (t >= b) return std::nullopt;

    // The paper uses consume; acquire is what compilers emit for it anyway.
    Ring<T>* r = ring_.load(std::memory_order_acquire);
    T x = r->get(t);
    if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                      std::memory_order_relaxed))
      return std::nullopt;
    return x;
  }

  // Approximate unless called by the owner with no concurrent thieves.
  std::int64_t size() const {
    std::int64_t b = bottom_.load(std::memory_order_relaxed);
    std::int64_t t = top_.load(std::memory_order_relaxed);
    return b > t ? b - t : 0;
  }

 private:
  // Old rings stay alive until the deque dies: a thief may still be reading
  // one. Total retained memory is bounded by 2x the largest ring.
  Ring<T>* grow(Ring<T>* old, std::int64_t t, std::int64_t b) {
    rings_.push_back(old->grow(t, b));
    Ring<T>* r = rings_.back().get();
    ring_.store(r, std::memory_order_release);
    return r;
  }

  alignas(cache_line) std::atomic<std::int64_t> top_{0};
  alignas(cache_line) std::atomic<std::int64_t> bottom_{0};
  alignas(cache_line) std::atomic<Ring<T>*> ring_{nullptr};
  std::vector<std::unique_ptr<Ring<T>>> rings_;  // owner only
};

}  // namespace chaselev
