#pragma once

// Fixed-capacity circular buffer addressed by unbounded logical indices:
// index i lives in slot i & (capacity - 1). Callers never wrap indices
// themselves, so a pair of ever-increasing counters [t, b) describes the
// live range and b - t is its size.
//
// Slots are atomics accessed relaxed, so concurrent get/put on the same slot
// is not a data race; ordering is the caller's job.

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace chaselev {

template <typename T>
  requires std::is_trivially_copyable_v<T>
class Ring {
 public:
  // Rounds up to a power of two so wrapping is a mask, not a modulo.
  explicit Ring(std::int64_t min_capacity)
      : mask_(static_cast<std::int64_t>(std::bit_ceil(
                  static_cast<std::uint64_t>(std::max<std::int64_t>(min_capacity, 1)))) -
              1),
        slots_(std::make_unique<std::atomic<T>[]>(mask_ + 1)) {}

  std::int64_t capacity() const { return mask_ + 1; }

  T get(std::int64_t i) const {
    return slots_[i & mask_].load(std::memory_order_relaxed);
  }
  void put(std::int64_t i, T x) {
    slots_[i & mask_].store(x, std::memory_order_relaxed);
  }

  // A ring of twice the capacity holding [t, b) at the same logical indices.
  std::unique_ptr<Ring> grow(std::int64_t t, std::int64_t b) const {
    auto bigger = std::make_unique<Ring>(capacity() * 2);
    for (std::int64_t i = t; i < b; ++i) bigger->put(i, get(i));
    return bigger;
  }

 private:
  std::int64_t mask_;
  std::unique_ptr<std::atomic<T>[]> slots_;
};

}  // namespace chaselev
