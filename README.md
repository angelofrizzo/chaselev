# chaselev
A work-stealing task scheduler in C++23, benchmarked against oneTBB, OpenMP, and Taskflow.

**What:** per-worker Chase–Lev deques, randomized stealing, task graphs with dependencies, priorities.

**Why:** scheduling overhead dominates when tasks are small or irregular. This project measures where each design wins or loses, and why.

**How it is measured:** standard workloads (`fib`, UTS, N-queens, synthetic DAGs with heavy-tailed durations). Threads pinned, repeated runs, p99/p99.9 with bootstrap confidence intervals, `perf` counters to explain every result.

## Results

*TBD* — no number appears here unless it is reproducible from `bench/`.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build
./build/bench/run_all.sh
```
