#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <new>
#include <thread>

namespace hsp {

//#define CACHELINE_SIZE std::hardware_destructive_interference_size
#define CACHELINE_SIZE 64

class Slab {
public:
  static constexpr size_t kCounters =
      std::max(64UL, CACHELINE_SIZE / sizeof(int));

  Slab();
  Slab(const Slab& other);

  // Returns true iff this is the first increment.
  bool increment(int slabSlot);

  // Returns true iff this was not the first increment.
  bool tryIncrement(int slabSlot);

  // Returns true iff this is the last decrement.
  bool decrement(int slabSlot);

private:
  std::array<std::atomic<int>, kCounters> counters_;
};

class Arena;

class Counter {
public:
  Counter() = default;

  Counter(Arena *arena, int originalCpu, int slabSlot);

  Counter(const Counter& other);

  Counter(Counter&& other);

  Counter& operator=(const Counter& other);

  Counter& operator=(Counter&& other);

  ~Counter();

  operator bool() const;

  void increment();

  // Returns false if the Counter cannot be decremented further.
  bool decrement();

private:
  uintptr_t reference_{0};

  Arena* arena() const;

  int originalCpu() const;

  int slabSlot() const;
};

class WeakCounter {
public:
  WeakCounter() = delete;

  WeakCounter(Arena *arena, int originalCpu, int slabSlot);

  WeakCounter(const WeakCounter& other);

  WeakCounter(WeakCounter&& other);

  WeakCounter& operator=(const Counter& other);

  WeakCounter& operator=(WeakCounter&& other);

  ~WeakCounter() = default;

  Counter lock() const;

//private:
  uintptr_t reference_;

  void increment();

  // Returns false if the WeakCounter cannot be decremented further.
  bool decrement();

  Arena* arena() const;

  int originalCpu() const;

  int slabSlot() const;
};

// Contains space for up to 64 counters.
class alignas(64 * 64) Arena {
public:
  Arena();

  // Reserves a slot for a new counter. If this fails, then the resulting
  // Counter will evaluate to false.
  Counter getCounter();

  void increment(int cpu, int slabSlot, bool weak);
  bool tryIncrement(int cpu, int slabSlot);

  bool decrement(int originalCpu, int slabSlot, bool weak);

 private:
  std::atomic<uint64_t> availableSlotsMask_;

  struct AtomicWrapper {
    AtomicWrapper() : value(0) {}
    AtomicWrapper(const AtomicWrapper &) : value(0) {}

    std::atomic<uint64_t> value;
  };

  std::vector<AtomicWrapper> usedCpusPerSlab_;
  std::vector<AtomicWrapper> usedCpusPerWeakSlab_;

  // One Slab and one weak Slab per Cpu.
  std::vector<Slab> slabs_;
  std::vector<Slab> weakSlabs_;

  void markCpu(int cpu, int slabSlot);
  void unmarkCpu(int cpu, int slabSlot);
};

class ArenaManager {
public:
  static ArenaManager& getInstance();

  ArenaManager(ArenaManager const &) = delete;
  void operator=(ArenaManager const &) = delete;

  Counter getCounter();
  void notifyNewAvailability(Arena* arena);

private:
  ArenaManager() = default;
  std::atomic<Arena *> currentArena_;
  std::mutex mutex_;            // Protects changing the arenas_ list.
  std::vector<Arena *> arenas_; // This is not all arenas, but just ones that
                                // have available capacity.
};

} // namespace hsp
