# chaselev
A work-stealing task scheduler in C++23, benchmarked against oneTBB, OpenMP, and Taskflow.

**What:** per-worker Chase–Lev deques, randomized stealing, task graphs with dependencies, priorities.

**Why:** scheduling overhead dominates when tasks are small or irregular. This project measures where each design wins or loses, and why.

**How it is measured:** standard workloads (`fib`, UTS, N-queens, synthetic DAGs with heavy-tailed durations). Threads pinned, repeated runs, p99/p99.9 with bootstrap confidence intervals, `perf` counters to explain every result.

## Status

- [x] `Ring<T>` — power-of-two circular buffer over unbounded logical indices (`include/chaselev/ring.hpp`)
- [x] `Deque<T>` — Chase–Lev deque with the weak-memory orderings of Lê et al., PPoPP 2013; growable (`include/chaselev/deque.hpp`)
- [x] `Scheduler` / `TaskGroup` — per-worker deques, randomized stealing, Cilk-style `spawn`/`sync`; the waiting worker keeps stealing instead of blocking (`include/chaselev/scheduler.hpp`)
- [ ] Benchmarks vs oneTBB, OpenMP, Taskflow (`bench/`)
- [ ] Task graphs with dependencies, priorities

## Usage

```cpp
#include <chaselev/chaselev.hpp>

std::int64_t fib(std::int64_t n) {
  if (n < 2) return n;
  std::int64_t a;
  chaselev::TaskGroup g;
  g.spawn([&] { a = fib(n - 1); });
  std::int64_t b = fib(n - 2);
  g.sync();
  return a + b;
}

chaselev::Scheduler s(8);           // the caller of run() is worker 0
std::int64_t r;
s.run([&] { r = fib(30); });
```

## Results

*TBD* — no number appears here unless it is reproducible from `bench/`.

## Build

Header-only, C++23, CMake ≥ 3.24. Tests use Catch2 (fetched automatically).

```bash
cmake --preset release && cmake --build --preset release -j
ctest --preset release
```

Presets: `release` (`build/`), `debug` (`build-debug/`), `tsan` (`build-tsan/`, ThreadSanitizer).

**macOS:** the ThreadSanitizer runtime in Command Line Tools 16.x crashes on macOS 26, so the `tsan` preset builds but its tests don't run. Use Homebrew LLVM (`brew install llvm`) or newer Command Line Tools.
