#include "CPUDistributedCounter.h"

#include <sched.h>

namespace distcount {

Slab::Slab() {}
Slab::Slab(const Slab &other) {}

void Slab::increment(int slabIndex) {
  counters_[slabIndex].fetch_add(1, std::memory_order_relaxed);
}

bool Slab::decrement(int slabIndex) {
  if (1 == counters_[slabIndex].load(std::memory_order_relaxed)) {
    // This assumes that the counter will be destroyed now.
    return false;
  }

  return 1 != counters_[slabIndex].fetch_sub(1, std::memory_order_relaxed);
}

Counter::Counter(Arena *arena, int orignalCpu, int slabIndex)
    : reference_(reinterpret_cast<uintptr_t>(arena) + (orignalCpu << 6) +
                 slabIndex) {}

void Counter::increment() {
  printf("> Counter::increment() -- %d, %d\n", originalCpu(), slabIndex());
  arena()->increment(originalCpu(), slabIndex());
}

bool Counter::decrement() {
  printf("> Counter::decrement() -- %d, %d\n", originalCpu(), slabIndex());
  return arena()->decrement(originalCpu(), slabIndex());
}

Arena *Counter::arena() const {
  return reinterpret_cast<Arena *>(reference_ & ~4091);
}

int Counter::originalCpu() const {
  return static_cast<int>((reference_ >> 6) & 63);
}

int Counter::slabIndex() const { return static_cast<int>(reference_ & 63); }

Arena::Arena()
    : numCpus_(std::thread::hardware_concurrency()),
      availableMask_((1UL << (Slab::kCounters - 1)) - 1) {
  printf("numCpus_: %d, this: 0x%zx\n", numCpus_,
         reinterpret_cast<size_t>(this));
  slabs_.resize(numCpus_);
}

Counter Arena::getCounter() {
  // TODO(lpe): With rseq, this could be faster. Instead of needing to increment
  // the slab counter in all cases, we could instead only increment it if the
  // CPU has already produced a counter.
  uint64_t expected = availableMask_.load(std::memory_order_relaxed);
  while (true) {
    // assert(expected);
    int slabIndex = __builtin_ctz(expected);
    uint64_t desired = expected & ~(1 << slabIndex);
    if (availableMask_.compare_exchange_weak(expected, desired,
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
      return Counter(this, getCPU(), slabIndex);
    }
  }
}

void Arena::increment(int cpu, int slabIndex) {
  printf("numCpus_: %d, numslabs: %zu, cpu: %d, slabIndex: %d\n", numCpus_,
         slabs_.size(), getCPU(), slabIndex);
  slabs_[cpu].increment(slabIndex);
}

bool Arena::decrement(int originalCpu, int slabIndex) {
  return slabs_[originalCpu].decrement(slabIndex);
}

int Arena::getCPU() {
  thread_local int remainingUses = 0;
  thread_local unsigned int cpu;

  if (remainingUses) {
    remainingUses--;
    return cpu;
  }

  remainingUses = 32;
  getcpu(&cpu, nullptr);
  return cpu;
}

} // namespace distcount
