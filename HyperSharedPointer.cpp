#include "HyperSharedPointer.h"

#include <sched.h>

#include <cstddef>
#include <cstring>

#include <glog/logging.h>
#include <rseq/rseq.h>

namespace hsp {

int Debug::curFuncIndent_ = 0;
int Debug::curCtorIndent_ = 40;
bool Debug::enabled_ = true;

int getCpu() {
  thread_local int remainingUses = 0;
  thread_local unsigned int cpu = -1;

  if (remainingUses) {
    remainingUses--;
    return cpu;
  }

  remainingUses = 31;
  cpu = rseq_current_cpu();
  return cpu;
}

bool Slab::increment() {
  if (!counter_.fetch_add(1, std::memory_order_relaxed)) {
    return true;
  }
  return false;
}

bool Slab::decrement() {
  return 1 == counter_.fetch_sub(1, std::memory_order_acq_rel);
}

size_t Slab::use_count() const {
  return counter_.load(std::memory_order_acq_rel);
}

Counter::Counter(Arena *arena_, int originalCpu_)
    : reference_(reinterpret_cast<uintptr_t>(arena_) + originalCpu_) {
  if (arena_ == nullptr) {
    reference_ = 0;
    return;
  }
  increment();
}

Counter::Counter(const Counter &other)
    : reference_((other.reference_ & kArenaMask) + getCpu()) {
  increment();
}

Counter::Counter(Counter &&other) : reference_(other.reference_) {
  other.reference_ = 0;
}

Counter &Counter::operator=(const Counter &other) {
  Counter c{other};
  std::swap(reference_, c.reference_);
  return *this;
}

Counter &Counter::operator=(Counter &&other) {
  Counter c{std::move(other)};
  std::swap(reference_, c.reference_);
  return *this;
}

Counter::~Counter() { DCHECK(!(*this)); }

bool Counter::destroy() {
  if (!(*this)) {
    return false;
  }
  bool ret = decrement();
  if (ret) {
    Arena::destroy(arena());
  }
  reference_ = 0;
  return ret;
}

Counter::operator bool() const { return reference_ != 0; }

size_t Counter::use_count() const {
  if (!arena()) {
    return 0;
  }
  return arena()->use_count();
}

Arena *Counter::arena() const {
  return reinterpret_cast<Arena *>(reference_ & kArenaMask);
}

int Counter::originalCpu() const {
  return static_cast<int>((reference_ >> kCpuOffset) & kFieldMask);
}

void Counter::increment() { arena()->increment(originalCpu()); }

bool Counter::decrement() { return arena()->decrement(originalCpu()); }

Arena *Arena::create() {
  int numCpus = std::thread::hardware_concurrency();

  size_t infoSize = offsetof(Arena, slabs_);
  size_t slabsSize = numCpus * sizeof(Slab);

  Arena *arena = reinterpret_cast<Arena *>(
      aligned_alloc(alignof(Arena), infoSize + slabsSize));
  arena->info_.usedCpus.store(0, std::memory_order_release);
  arena->info_.sizeofArena = infoSize + slabsSize;
  arena->info_.numCpus = numCpus;
  for (int i = 0; i < numCpus; i++) {
    new (&arena->slabs_[i]) Slab();
  }

  return arena;
}

void Arena::destroy(Arena *arena) { free(arena); }

Counter Arena::getCounter() { return Counter(this, getCpu()); }

void Arena::increment(int cpu) {
  if (slabs_[cpu].increment()) {
    markCpu(cpu);
  }
}

bool Arena::decrement(int originalCpu) {
  if (slabs_[originalCpu].decrement()) {
    uint64_t usedCpus = unmarkCpu(originalCpu);
    if (!usedCpus) {
      return true;
    }
  }

  return false;
}

size_t Arena::use_count() const {
  size_t count = 0;

  for (int i = 0; i < info_.numCpus; i++) {
    count += slabs_[i].use_count();
  }

  return count;
}

void Arena::markCpu(int cpu) {
  uint64_t expected = info_.usedCpus.load(std::memory_order_relaxed);
  uint64_t usedCpus;
  do {
    usedCpus = expected | (1UL << cpu);
  } while (!info_.usedCpus.compare_exchange_weak(expected, usedCpus,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed));
}

uint64_t Arena::unmarkCpu(int cpu) {
  uint64_t expected = info_.usedCpus.load(std::memory_order_relaxed);
  uint64_t usedCpus;
  do {
    usedCpus = expected & (~(1UL << cpu));
  } while (!info_.usedCpus.compare_exchange_weak(expected, usedCpus,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed));
  return usedCpus;
}

} // namespace hsp
