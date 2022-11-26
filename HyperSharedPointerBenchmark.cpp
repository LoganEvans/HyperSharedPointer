#include "HyperSharedPointer.h"

#include "benchmark/benchmark.h"

#include <memory>
#include <random>
#include <thread>

static void BM_CreateStdSharedPtr(benchmark::State &state) {
  for (auto _ : state) {
    std::shared_ptr<int> p{new int};
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_CreateStdSharedPtr)->Threads(1);
BENCHMARK(BM_CreateStdSharedPtr)->Threads(12);

static void BM_CreateHyperSharedPointer(benchmark::State &state) {
  for (auto _ : state) {
    hsp::HyperSharedPointer p{new int};
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_CreateHyperSharedPointer)->Threads(1);
BENCHMARK(BM_CreateHyperSharedPointer)->Threads(12);

static void BM_SharingStdSharedPtr(benchmark::State &state) {
  static std::shared_ptr<int> staticPtr{new int};
  std::array<std::shared_ptr<int>, 100> buf;
  std::default_random_engine gen;
  std::uniform_int_distribution<> unif(0, buf.size() - 1);

  for (auto _ : state) {
    int idx = unif(gen);
    buf[idx].reset();
    buf[idx] = staticPtr;
  }
}
BENCHMARK(BM_SharingStdSharedPtr)->Threads(1);
BENCHMARK(BM_SharingStdSharedPtr)->Threads(12);

static void BM_SharingHyperSharedPointer(benchmark::State &state) {
  static hsp::HyperSharedPointer staticPtr{new int};
  std::array<hsp::HyperSharedPointer<int>, 100> buf;
  std::default_random_engine gen;
  std::uniform_int_distribution<> unif(0, buf.size() - 1);

  for (auto _ : state) {
    int idx = unif(gen);
    buf[idx].reset();
    buf[idx] = staticPtr;
  }
}
BENCHMARK(BM_SharingHyperSharedPointer)->Threads(1);
BENCHMARK(BM_SharingHyperSharedPointer)->Threads(12);

BENCHMARK_MAIN();
