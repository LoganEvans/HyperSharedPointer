#include "HyperSharedPointer.h"

#include "benchmark/benchmark.h"

#include <memory>
#include <thread>

static void BM_CreateSharedPtr(benchmark::State &state) {
  for (auto _ : state) {
    std::shared_ptr<int> p{new int};
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_CreateSharedPtr)->Threads(1);
BENCHMARK(BM_CreateSharedPtr)->Threads(10);

static void BM_CreateHyperSharedPointer(benchmark::State &state) {
  for (auto _ : state) {
    hsp::HyperSharedPointer p{new int};
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_CreateHyperSharedPointer)->Threads(1);
BENCHMARK(BM_CreateHyperSharedPointer)->Threads(10);

BENCHMARK_MAIN();
