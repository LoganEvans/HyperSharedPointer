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

class Debug {
 public:
  static int curFuncIndent_;
  static int curCtorIndent_;

  Debug(const char* fmt, ...) {
    char buf[1000];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 1000, fmt, args);
    va_end(args);
    msg_ = buf;

    if (isFuncMsg()) {
      myIndent_ = curFuncIndent_++;
    } else {
      myIndent_ = curCtorIndent_++;
    }

    for (int i = 0; i < myIndent_; i++) {
      fprintf(stderr, " ");
    }
    fprintf(stderr, "> %s\n", msg_.c_str());
  }

  ~Debug() {
    for (int i = 0; i < myIndent_; i++) {
      fprintf(stderr, " ");
    }
    fprintf(stderr, "< %s%s\n", msg_.c_str(), note_.c_str());

    if (isFuncMsg()) {
      curFuncIndent_--;
    } else {
      curCtorIndent_--;
    }
  }

  void note(const char* fmt, ...) {
    char buf[1000];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 1000, fmt, args);
    va_end(args);
    note_ = std::string{" // "} + buf;
  }

  void print(const char* fmt, ...) {
    char buf[1000];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 1000, fmt, args);
    va_end(args);

    for (int i = 0; i < myIndent_; i++) {
      fprintf(stderr, " ");
    }
    fprintf(stderr, "- %s\n", buf);
  }

 private:
  int myIndent_;
  std::string msg_;
  std::string note_;

  bool isFuncMsg() const { return msg_[msg_.size() - 1] == ')'; }
};

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

  // Returns true if the Counter cannot be decremented further. A false Counter
  // will always return false;
  bool destroy();

  operator bool() const;

  std::string debugStr() const {
    char buf[1000];
    sprintf(buf,
            "Counter{reference_: 0x%zx, arena: 0x%zx, originalCpu: %d, "
            "slabSlot: %d}",
            reinterpret_cast<size_t>(reference_),
            reinterpret_cast<size_t>(arena()), originalCpu(), slabSlot());
    return buf;
  }

private:
  uintptr_t reference_{0};

  Arena* arena() const;

  int originalCpu() const;

  int slabSlot() const;

  void increment();

  // Returns true if the Counter cannot be decremented further. A false Counter
  // will always return false.
  bool decrement();
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

  // Returns true if the WeakCounter cannot be decremented further.
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
  uint64_t unmarkCpu(int cpu, int slabSlot);
  uint64_t unmarkSlabSlot(int slabSlot);
};

class ArenaManager {
public:
  static ArenaManager& getInstance();

  ArenaManager(ArenaManager const &) = delete;
  void operator=(ArenaManager const &) = delete;
  ~ArenaManager();

  Counter getCounter();
  void notifyNewAvailability(Arena* arena);

private:
  ArenaManager() = default;
  std::atomic<Arena *> currentArena_;
  std::mutex mutex_;            // Protects changing the arenas_ list.
  std::vector<Arena *> arenas_; // This is not all arenas, but just ones that
                                // have available capacity.
};

template <typename T>
class HyperSharedPointer {
 public:
  HyperSharedPointer() : counter_(), ptr_(nullptr) {}

  HyperSharedPointer(T *ptr)
      : counter_(ArenaManager::getInstance().getCounter()), ptr_(ptr) {}

  HyperSharedPointer(const HyperSharedPointer &other)
      : counter_(other.counter_), ptr_(other.ptr_) {}

  HyperSharedPointer(HyperSharedPointer &&other)
      : counter_(std::move(other.counter_)), ptr_(other.ptr_) {
    other.ptr_ = nullptr;
  }

  ~HyperSharedPointer() {
    if (counter_.destroy()) {
      delete ptr_;
    }
  }

  HyperSharedPointer<T> &operator=(const HyperSharedPointer<T> &other) {
    HyperSharedPointer p{other};
    swap(p);
    return *this;
  }

  void reset(T *ptr = nullptr) {
    if (ptr) {
      HyperSharedPointer p{ptr};
      swap(p);
    } else {
      HyperSharedPointer p;
      swap(p);
    }
  }

  void swap(HyperSharedPointer &other) {
    if (!counter_) {
      if (!other.counter_) {
        return;
      }
      ptr_ = other.ptr_;
      counter_ = other.counter_;

      other.ptr_ = nullptr;
      other.counter_.destroy();
      other.counter_ = Counter();
      return;
    }

    if (!other.counter_) {
      other.ptr_ = ptr_;
      other.counter_ = counter_;

      ptr_ = nullptr;
      counter_.destroy();
      counter_ = Counter();
      return;
    }

    T *tmpPtr{other.ptr_};
    Counter tmpCounter{other.counter_};
    other.counter_.destroy();

    other.ptr_ = ptr_;
    other.counter_ = counter_;
    counter_.destroy();

    ptr_ = tmpPtr;
    counter_ = tmpCounter;
    tmpCounter.destroy();
  }

  T *get() const { return ptr_; }

  T &operator*() const { return *ptr_; }

  T *operator->() const { return ptr_; }

  explicit operator bool() const { return ptr_ != nullptr; }

private:
  Counter counter_;
  T* ptr_;
};

template <class T, class U>
bool operator==(const HyperSharedPointer<T> &lhs,
                const HyperSharedPointer<U> &rhs) {
  return lhs.get() == rhs.get();
}

} // namespace hsp
