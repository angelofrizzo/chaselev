# chaselev
A work-stealing task scheduler in C++23, benchmarked against oneTBB, OpenMP, and Taskflow.

**What:** per-worker Chase–Lev deques, randomized stealing, task graphs with dependencies, priorities.

**Why:** scheduling overhead dominates when tasks are small or irregular. This project measures where each design wins or loses, and why.

**How it is measured:** standard workloads (`fib`, UTS, N-queens, synthetic DAGs with heavy-tailed durations). Threads pinned, repeated runs, p99/p99.9 with bootstrap confidence intervals, `perf` counters to explain every result.

## Status

- [x] `Ring<T>` — power-of-two circular buffer over unbounded logical indices (`include/chaselev/ring.hpp`)
- [x] `Deque<T>` — Chase–Lev deque with the weak-memory orderings of Lê et al., PPoPP 2013; growable (`include/chaselev/deque.hpp`)
- [ ] Scheduler: per-worker deques, randomized stealing, `spawn`/`sync`
- [ ] Benchmarks vs oneTBB, OpenMP, Taskflow (`bench/`)
- [ ] Task graphs with dependencies, priorities

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
