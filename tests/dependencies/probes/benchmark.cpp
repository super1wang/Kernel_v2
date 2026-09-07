#include <benchmark/benchmark.h>
static void dependency_smoke(benchmark::State& state) { for (auto _ : state) { int value = 7; benchmark::DoNotOptimize(value); } }
BENCHMARK(dependency_smoke)->Iterations(1000);
BENCHMARK_MAIN();
