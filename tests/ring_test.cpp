#include <chaselev/ring.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using chaselev::Ring;

TEST_CASE("capacity rounds up to a power of two") {
  CHECK(Ring<int>(0).capacity() == 1);
  CHECK(Ring<int>(1).capacity() == 1);
  CHECK(Ring<int>(3).capacity() == 4);
  CHECK(Ring<int>(64).capacity() == 64);
  CHECK(Ring<int>(65).capacity() == 128);
}

TEST_CASE("logical indices wrap onto slots") {
  Ring<int> r(4);
  r.put(1, 10);
  CHECK(r.get(1) == 10);
  CHECK(r.get(1 + 4) == 10);
  CHECK(r.get(1 + 4 * 1000) == 10);
  r.put(6, 60);  // 6 & 3 == 2
  CHECK(r.get(2) == 60);
  CHECK(r.get(1) == 10);  // neighbour untouched
}

TEST_CASE("a full window of capacity indices never collides") {
  Ring<std::int64_t> r(8);
  const std::int64_t t = 1'000'003;  // arbitrary, not a multiple of 8
  for (std::int64_t i = t; i < t + 8; ++i) r.put(i, i);
  for (std::int64_t i = t; i < t + 8; ++i) REQUIRE(r.get(i) == i);
}

TEST_CASE("grow keeps [t, b) at the same logical indices") {
  Ring<std::int64_t> r(4);
  // Live range 6..9 wraps in the old ring: slots 2,3,0,1.
  const std::int64_t t = 6, b = 10;
  for (std::int64_t i = t; i < b; ++i) r.put(i, i * 10);

  auto g = r.grow(t, b);
  CHECK(g->capacity() == 8);
  for (std::int64_t i = t; i < b; ++i) REQUIRE(g->get(i) == i * 10);

  // The new ring has room for capacity() more without touching live slots.
  for (std::int64_t i = b; i < t + 8; ++i) g->put(i, -1);
  for (std::int64_t i = t; i < b; ++i) REQUIRE(g->get(i) == i * 10);

  // The old ring is untouched: a late thief may still be reading it.
  for (std::int64_t i = t; i < b; ++i) REQUIRE(r.get(i) == i * 10);
}

TEST_CASE("grow of an empty range") {
  Ring<int> r(2);
  auto g = r.grow(5, 5);
  CHECK(g->capacity() == 4);
}
