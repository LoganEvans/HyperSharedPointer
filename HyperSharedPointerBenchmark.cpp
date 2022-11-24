#include "HyperSharedPointer.h"

#include "benchmark/benchmark.h"

#include <memory>
#include <random>
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

static void BM_SharingHyperSharedPointer(benchmark::State &state) {
  static hsp::HyperSharedPointer staticPtr{new int};
  std::array<hsp::HyperSharedPointer<int>, 100> buf;

  std::default_random_engine gen;
  std::uniform_int_distribution<> unif(0, buf.size() - 1);

  for (auto _ : state) {
    buf[unif(gen)] = staticPtr;
  }
}
BENCHMARK(BM_SharingHyperSharedPointer)->Threads(1);
BENCHMARK(BM_SharingHyperSharedPointer)->Threads(10);

static void BM_SharingSharedPointer(benchmark::State &state) {
  static std::shared_ptr<int> staticPtr{new int};
  std::array<std::shared_ptr<int>, 100> buf;

  std::default_random_engine gen;
  std::uniform_int_distribution<> unif(0, buf.size() - 1);

  for (auto _ : state) {
    buf[unif(gen)] = staticPtr;
  }
}
BENCHMARK(BM_SharingSharedPointer)->Threads(1);
BENCHMARK(BM_SharingSharedPointer)->Threads(10);

BENCHMARK_MAIN();
