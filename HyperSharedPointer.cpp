#include "HyperSharedPointer.h"

#include <sched.h>

namespace hsp {

int getCpu() {
  thread_local int remainingUses = 0;
  thread_local unsigned int cpu;

  if (remainingUses) {
    remainingUses--;
    return cpu;
  }

  remainingUses = 31;
  getcpu(&cpu, nullptr);
  return cpu;
}

Slab::Slab() : usedCpus_(0) {}
Slab::Slab(const Slab &other) {}

bool Slab::increment(int slabSlot) {
  if (0 == counters_[slabSlot].fetch_add(1, std::memory_order_relaxed)) {
    return true;
  }
  return false;
}

bool Slab::tryIncrement(int slabSlot) {
  int counter = counters_[slabSlot].load(std::memory_order_relaxed);
  while (true) {
    if (counter == 0) {
      return false;
    }
    int desired = counter + 1;
    if (counters_[slabSlot].compare_exchange_weak(counter, desired,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
      return true;
    }
  }
}

bool Slab::decrement(int slabSlot) {
  if (1 == counters_[slabSlot].load(std::memory_order_relaxed)) {
    // This assumes that the counter will be destroyed now.
    return false;
  }

  return 1 != counters_[slabSlot].fetch_sub(1, std::memory_order_relaxed);
}

Counter::Counter(Arena *arena, int orignalCpu, int slabSlot)
    : reference_(reinterpret_cast<uintptr_t>(arena) + (orignalCpu << 6) +
                 slabSlot) {}

Counter::Counter(const Counter &other)
    : Counter(other.arena(), getCpu(), other.slabSlot()) {}

Counter::Counter(Counter &&other) : reference_(other.reference_) {
  other.reference_ = 0;
}

Counter &Counter::operator=(const Counter &other) {
  Counter c{other.arena(), getCpu(), other.slabSlot()};
  std::swap(reference_, c.reference_);
  return *this;
}

Counter &Counter::operator=(Counter &&other) {
  *this = other;
  other.reference_ = 0;
  return *this;
}

Counter::~Counter() {}

Counter::operator bool() const { return reference_ != 0; }

void Counter::increment() {
  arena()->increment(originalCpu(), slabSlot(), /*weak=*/false);
}

bool Counter::decrement() {
  return arena()->decrement(originalCpu(), slabSlot(), /*weak=*/false);
}

Arena *Counter::arena() const {
  return reinterpret_cast<Arena *>(reference_ & ~4091);
}

int Counter::originalCpu() const {
  return static_cast<int>((reference_ >> 6) & 63);
}

int Counter::slabSlot() const { return static_cast<int>(reference_ & 63); }

Arena::Arena() : availableSlotsMask_((1UL << (Slab::kCounters - 1)) - 1) {
  int numCpus = std::thread::hardware_concurrency();
  usedCpusPerSlab_.resize(numCpus);
  slabs_.resize(numCpus);
}

Counter Arena::getCounter() {
  // TODO(lpe): With rseq, this could be faster. Instead of needing to increment
  // the slab counter in all cases, we could instead only increment it if the
  // Cpu has already produced a counter.
  uint64_t expected = availableSlotsMask_.load(std::memory_order_relaxed);
  while (true) {
    // assert(expected);
    int slabSlot = __builtin_ctz(expected);
    uint64_t desired = expected & ~(1 << slabSlot);
    if (availableSlotsMask_.compare_exchange_weak(expected, desired,
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
      return Counter(this, getCpu(), slabSlot);
    }
  }
}

void Arena::increment(int cpu, int slabSlot, bool weak) {
  if (weak) {
    weakSlabs_[cpu].increment(slabSlot);
  }
  slabs_[cpu].increment(slabSlot);
}

bool Arena::tryIncrement(int cpu, int slabSlot) {
  if (slabs_[cpu].tryIncrement(slabSlot)) {
    return true;
  }

  while (true) {
    uint64_t usedCpus =
        usedCpusPerSlab_[slabSlot].value.load(std::memory_order_acquire);
    if (!usedCpus) {
      return false;
    }

    int nextCpu = (cpu + 1) % slabs_.size();
    while (0 == (usedCpus & (1UL << nextCpu))) {
      nextCpu = (nextCpu + 1) % slabs_.size();
    }

    if (slabs_[nextCpu].tryIncrement(slabSlot)) {
      // Success. However, we incremented the count for the wrong CPU. Now that
      // we have guaranteed that the object won't be deleted, we can correct
      // that problem.

      if (slabs_[cpu].increment(slabSlot)) {
        markCpu(cpu, slabSlot);
      }

      if (slabs_[nextCpu].decrement(slabSlot)) {
        unmarkCpu(nextCpu, slabSlot);
      }

      return true;
    }
  }
}

bool Arena::decrement(int originalCpu, int slabSlot, bool weak) {
  if (weak) {
    return weakSlabs_[originalCpu].decrement(slabSlot);
  }
  return slabs_[originalCpu].decrement(slabSlot);
}

void Arena::markCpu(int cpu, int slabSlot) {
  uint64_t expected =
      usedCpusPerSlab_[slabSlot].value.load(std::memory_order_relaxed);
  uint64_t desired;
  do {
    desired = expected | (1UL << cpu);
  } while (!usedCpusPerSlab_[slabSlot].value.compare_exchange_weak(
      expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed));
}

void Arena::unmarkCpu(int cpu, int slabSlot) {
  uint64_t expected =
      usedCpusPerSlab_[slabSlot].value.load(std::memory_order_relaxed);
  uint64_t desired;
  do {
    desired = expected & (~(1UL << cpu));
  } while (!usedCpusPerSlab_[slabSlot].value.compare_exchange_weak(
      expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed));
}

/*static*/ ArenaManager& ArenaManager::getInstance() {
  static ArenaManager instance;
  return instance;
}

} // namespace hsp
