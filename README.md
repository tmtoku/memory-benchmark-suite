# Micro-benchmark Suite

A suite of microbenchmarks for measuring the empirical peak performance of processors and memory subsystems.

## Overview

Each benchmark demonstrates the empirical peak performance of a specific hardware resource.

## Requirements

- x86-64 CPU
- Linux kernel with `perf_event_open` support
- C++17-capable compiler (GCC or Clang)
- CMake >= 3.10
- OpenMP
- [`perf-counter`](https://github.com/tmtoku/perf-counter) (included as a submodule)
- [`libpfm4`](https://sourceforge.net/p/perfmon2/libpfm4/ci/master/tree/) for named PMU event support

## Build

```sh
git clone --recursive https://github.com/tmtoku/micro-benchmark-suite
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To target a specific microarchitecture (defaults to `native`):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMICRO_BENCHMARK_SUITE_ARCH=znver2
```

## Benchmarks

- **memory_latency** — load latency at each cache level, measured via random pointer chasing to minimize hardware prefetcher effects
- **memory_throughput** — load throughput at each cache level, measured across multiple thread counts
- **integer_division** — `idivq` latency with different dividend or quotient bit lengths

## License

See [LICENSE](LICENSE).
