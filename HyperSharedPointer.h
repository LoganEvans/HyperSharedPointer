#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <list>
#include <mutex>
#include <new>
#include <thread>

namespace hsp {

#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

int getCpu();

class alignas(hardware_destructive_interference_size) Slab {
 public:
  // Returns true if the increment was successful.
  // The parameter cpuConfirmed indicates whether the caller has verified that
  // the CPU has been marked. If the CPU has not been confirmed then this method
  // will refuse to increment from 0->1.
  bool increment();

  void incrementCpuMarked();

  // Returns true iff this is can be further decremented.
  bool decrement();

  size_t use_count() const;

 private:
  // If the result of a fetch_add is negative, this indicates that this slab
  // has not been registered with the Arena.
  std::atomic<int> counter_{std::numeric_limits<int>::min()};
};

class Arena;

class Counter {
  template <typename T>
  friend class KeepAlive;

 public:
  Counter() = default;

  Counter(Arena *arena, int originalCpu);

  Counter(const Counter &other);

  Counter(Counter &&other);

  Counter &operator=(const Counter &other);

  Counter &operator=(Counter &&other);

  ~Counter();

  // Returns true if the Counter cannot be decremented further. A false Counter
  // will always return false;
  bool destroy();

  operator bool() const;

  size_t use_count() const;

  int originalCpu() const;

 private:
  static constexpr uint64_t kFieldBits = 7;  // From alignas(128) for Arena.
  static constexpr uint64_t kFieldMask = (1UL << kFieldBits) - 1;
  // This comes from using alignas(4096) for Arena.
  static constexpr uint64_t kArenaMask = ~kFieldMask;
  static constexpr uint64_t kCpuOffset = 0;

  uintptr_t reference_{0};

  Arena *arena() const;

  void increment();

  // Returns true iff the Counter can be decremented further. A false Counter
  // will always return true.
  bool decrement();
};

class WeakCounter {
 public:
  WeakCounter() = delete;

  WeakCounter(Arena *arena, int originalCpu);

  WeakCounter(const WeakCounter &other);

  WeakCounter(WeakCounter &&other);

  WeakCounter &operator=(const Counter &other);

  WeakCounter &operator=(WeakCounter &&other);

  ~WeakCounter() = default;

  Counter lock() const;

 private:
  uintptr_t reference_;

  void increment();

  // Returns true if the WeakCounter cannot be decremented further.
  bool decrement();

  Arena *arena() const;

  int originalCpu() const;
};

// Contains space for up to 64 counters.
class alignas(128) Arena {
  template <typename T>
  friend class KeepAlive;

 public:
  static Arena *create();
  static void destroy(Arena *arena);

  void increment(int cpu);

  bool decrement(int originalCpu);

  size_t use_count() const;

 private:
  // This cannot be constructed because slabs_ will be a properly sized array,
  // which avoid an extra pointer lookup in std::vector.
  Arena() = delete;

  struct alignas(128) {
    std::atomic<uint64_t> usedCpus;
    size_t sizeofArena;
    int numCpus;
  } info_;

  // This will end up having a Slab for each CPU. malloc is used instead of a
  // constructor to make this abomination happen.
  Slab slabs_[1];

  bool markCpu(int cpu);
  uint64_t unmarkCpu(int cpu);

  uint64_t unmarkSlabSlot();
};

template <typename T>
class KeepAlive;

template <typename T>
class HyperSharedPointer {
  template <typename U>
  friend class KeepAlive;

 public:
  HyperSharedPointer() : counter_(), ptr_(nullptr) {}

  HyperSharedPointer(T *ptr) : counter_(Arena::create(), getCpu()), ptr_(ptr) {}

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
    if (*this == other) {
      return *this;
    }

    if (counter_.destroy()) {
      delete ptr_;
    }

    ptr_ = other.ptr_;
    counter_ = other.counter_;
    return *this;
  }

  void reset(T *ptr = nullptr) {
    if (!ptr) {
      HyperSharedPointer p;
      swap(p);
    } else {
      HyperSharedPointer p{ptr};
      swap(p);
    }
  }

  void swap(HyperSharedPointer &other) {
    if (!counter_) {
      if (!other.counter_) {
        return;
      }
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
      counter_ = std::move(other.counter_);
      return;
    }

    if (!other.counter_) {
      other.ptr_ = ptr_;
      ptr_ = nullptr;
      other.counter_ = std::move(counter_);
      return;
    }

    T *tmpPtr{other.ptr_};
    Counter tmpCounter{std::move(other.counter_)};

    other.ptr_ = ptr_;
    other.counter_ = std::move(counter_);

    ptr_ = tmpPtr;
    counter_ = std::move(tmpCounter);
  }

  T *get() const { return ptr_; }

  T &operator*() const { return *ptr_; }

  T *operator->() const { return ptr_; }

  explicit operator bool() const { return ptr_ != nullptr; }

  size_t use_count() const { return counter_.use_count(); }

  int originalCpu() const { return counter_.originalCpu(); }

 private:
  Counter counter_;
  T *ptr_;
};

template <class T, class U>
bool operator==(const HyperSharedPointer<T> &lhs,
                const HyperSharedPointer<U> &rhs) {
  return lhs.get() == rhs.get();
}

template <typename T>
class KeepAlive {
 public:
  KeepAlive(T *ptr) {
    std::lock_guard lock{mutex_};
    reset(ptr, lock);
  }

  HyperSharedPointer<T> reset(T *ptr) {
    std::lock_guard lock{mutex_};
    reset(ptr, lock);
    return ptr_;
  }

  HyperSharedPointer<T> get() const { return ptr_; }

 private:
  std::mutex mutex_;
  HyperSharedPointer<T> ptr_;

  void reset(T *ptr, const std::lock_guard<std::mutex> &) {
    int numCpus = std::thread::hardware_concurrency();

    if (ptr_) {
      for (int i = 0; i < numCpus; i++) {
        ptr_.counter_.arena()->slabs_[i].decrement();
      }
    }

    ptr_ = HyperSharedPointer(ptr);
    ptr_.counter_.arena()->info_.usedCpus.store((1ULL << numCpus) - 1,
                                                 std::memory_order_release);
    for (int i = 0; i < numCpus; i++) {
      ptr_.counter_.arena()->slabs_[i].incrementCpuMarked();
    }
  }
};

}  // namespace hsp
