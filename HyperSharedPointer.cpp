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

class RseqRegistrar {
public:
  RseqRegistrar() {
    CHECK(rseq_available(RSEQ_AVAILABLE_QUERY_KERNEL));
    CHECK(!rseq_register_current_thread());
  }

  ~RseqRegistrar() {
    CHECK(!rseq_unregister_current_thread());
  }
};
thread_local RseqRegistrar rseqRegistrar{};

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
  // This can't be memory_order_relaxed because the initialization (when the
  // counter is set to a non-negative value) needs to be communicated with
  // other CPUs.
  int count = counter_.fetch_add(1, std::memory_order_acquire);
  // The value count == 0 is a special case. A second thread could have
  // decremented this counter to 0, after which it will attempt to set the
  // counter to std::numeric_limits<int>::min(). However, since the code here
  // in Slab::increment() finished first, that process will fail and the Slab
  // will remain registered.
  if (count < 0) [[unlikely]] {
    return false;
  }

  return true;
}

void Slab::incrementCpuMarked() {
  int count = std::numeric_limits<int>::min() + 1;
  int wantCount;
  do {
    wantCount = std::max(count + 1, 1);
  } while (!counter_.compare_exchange_weak(
      count, wantCount, std::memory_order_acq_rel, std::memory_order_relaxed));
}

bool Slab::decrement() {
  int count = counter_.fetch_sub(1, std::memory_order_acq_rel);

  if (count == 1) [[unlikely]] {
    count = 0;
    int disabledCount = std::numeric_limits<int>::min();
    if (counter_.compare_exchange_strong(count, disabledCount,
                                         std::memory_order_acq_rel,
                                         std::memory_order_relaxed)) {
      return false;
    }
  }

  return true;
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
  bool alive = decrement();
  if (!alive) {
    Arena::destroy(arena());
  }
  reference_ = 0;
  return !alive;
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
  while (true) {
    if (slabs_[cpu].increment()) [[likely]] {
      break;
    }

    if (markCpu(cpu)) [[likely]] {
      slabs_[cpu].incrementCpuMarked();
      break;
    }

    // We failed to mark the CPU because of one of two race conditions:
    //
    // 1) This thread was interrupted and a second thread attempted to
    // increment the slab counter (which also failed) and then marked the
    // CPU. If we keep trying, eventually that thread will initialize the
    // slab counter in incrementCpuMarked and our attempt to increment will
    // succeed.
    //
    // 2) A second thread has disabled the counter by setting it to
    // std::numeric_limits<int>::min(), but it has not yet unmarked the CPU.
    // We need to wait for that thread to eventually unmark the CPU so that
    // we don't leave the CPU unmarked while we are using the slab counter.

    std::this_thread::yield();
  }
}

bool Arena::decrement(int originalCpu) {
  if (!slabs_[originalCpu].decrement()) {
    return unmarkCpu(originalCpu) != 0UL;
  }

  return true;
}

size_t Arena::use_count() const {
  size_t count = 0;

  for (int i = 0; i < info_.numCpus; i++) {
    count += slabs_[i].use_count();
  }

  return count;
}

bool Arena::markCpu(int cpu) {
  uint64_t expected = info_.usedCpus.load(std::memory_order_relaxed);
  uint64_t usedCpus;
  do {
    if (expected & (1UL << cpu)) {
      return false;
    }
    usedCpus = expected | (1UL << cpu);
  } while (!info_.usedCpus.compare_exchange_weak(expected, usedCpus,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed));
  return true;
}

uint64_t Arena::unmarkCpu(int cpu) {
  uint64_t usedCpus = info_.usedCpus.load(std::memory_order_relaxed);
  uint64_t expected = usedCpus;
  do {
    if (!(expected & (1UL << cpu))) {
      return usedCpus;
    }
    usedCpus = expected & (~(1UL << cpu));
  } while (!info_.usedCpus.compare_exchange_weak(expected, usedCpus,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed));

  return usedCpus;
}

} // namespace hsp
