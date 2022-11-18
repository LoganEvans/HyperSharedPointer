#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <new>
#include <thread>

namespace distcount {

//#define CACHELINE_SIZE std::hardware_destructive_interference_size
#define CACHELINE_SIZE 64

class Slab {
public:
  static constexpr size_t kCounters =
      std::max(64UL, CACHELINE_SIZE / sizeof(int));

  Slab();
  Slab(const Slab& other);

  void increment(int slabIndex);
  bool decrement(int slabIndex);

private:
  std::array<std::atomic<int>, kCounters> counters_;
};

class Arena;

class Counter {
public:
  Counter(Arena *arena, int originalCpu, int slabIndex);

  void increment();

  // Returns false if the Counter cannot be decremented further.
  bool decrement();

//private:
  uintptr_t reference_;

  Arena* arena() const;

  int originalCpu() const;

  int slabIndex() const;
};

// Contains space for up to 64 counters.
class alignas(64 * 64) Arena {
public:
  Arena();

  Counter getCounter();

  void increment(int cpu, int slabIndex);

  bool decrement(int originalCpu, int slabIndex);

 private:
  const int numCpus_;
  std::atomic<uint64_t> availableMask_;
  std::vector<Slab> slabs_;

  int getCPU();
};

} // namespace distcount
