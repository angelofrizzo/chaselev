#include <chaselev/deque.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

using chaselev::Deque;

TEST_CASE("empty deque") {
  Deque<int> d;
  CHECK_FALSE(d.pop());
  CHECK_FALSE(d.steal());
  CHECK(d.size() == 0);
}

TEST_CASE("pop is LIFO, steal is FIFO") {
  Deque<int> d;
  for (int i = 0; i < 4; ++i) d.push(i);
  CHECK(d.steal() == 0);
  CHECK(d.pop() == 3);
  CHECK(d.steal() == 1);
  CHECK(d.pop() == 2);
  CHECK_FALSE(d.pop());
  CHECK_FALSE(d.steal());
}

TEST_CASE("grows past initial capacity, including after wraparound") {
  Deque<int> d(2);
  // Advance top so live indices don't start at 0.
  for (int i = 0; i < 5; ++i) d.push(-1);
  for (int i = 0; i < 5; ++i) REQUIRE(d.steal() == -1);

  constexpr int n = 1000;
  for (int i = 0; i < n; ++i) d.push(i);
  CHECK(d.size() == n);
  for (int i = n - 1; i >= 0; --i) REQUIRE(d.pop() == i);
  CHECK_FALSE(d.pop());
}

// Owner pushes 0..n-1 interleaved with pops; thieves steal concurrently.
// Every value must come out exactly once.
TEST_CASE("concurrent: every item delivered exactly once") {
  constexpr std::int64_t n = 1'000'000;
  const unsigned thieves = std::max(2u, std::thread::hardware_concurrency() - 1);

  Deque<std::int64_t> d(2);  // small, to exercise grow under contention
  std::atomic<bool> done{false};
  std::vector<std::vector<std::int64_t>> got(thieves + 1);

  std::vector<std::thread> ts;
  for (unsigned k = 0; k < thieves; ++k) {
    ts.emplace_back([&, k] {
      while (!done.load(std::memory_order_acquire))
        if (auto x = d.steal()) got[k].push_back(*x);
    });
  }

  auto& mine = got[thieves];
  for (std::int64_t i = 0; i < n; ++i) {
    d.push(i);
    if (i % 3 == 0)  // keep the deque short so pop and steal often collide
      if (auto x = d.pop()) mine.push_back(*x);
  }
  while (auto x = d.pop()) mine.push_back(*x);
  done.store(true, std::memory_order_release);
  for (auto& t : ts) t.join();

  std::vector<std::int64_t> all;
  for (auto& g : got) all.insert(all.end(), g.begin(), g.end());
  std::ranges::sort(all);
  REQUIRE(all.size() == static_cast<std::size_t>(n));
  std::vector<std::int64_t> expected(n);
  std::iota(expected.begin(), expected.end(), 0);
  CHECK(all == expected);

  std::size_t stolen = all.size() - mine.size();
  INFO("stolen " << stolen << " of " << n);
  CHECK(stolen > 0);
}
