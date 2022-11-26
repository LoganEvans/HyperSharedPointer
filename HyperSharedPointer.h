#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <list>
#include <mutex>
#include <new>
#include <thread>

namespace hsp {

class Debug {
 public:
  static int curFuncIndent_;
  static int curCtorIndent_;
  static bool enabled_;

  static void enable() { enabled_ = true; }

  static void disable() { enabled_ = false; }

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

    if (enabled_) {
      for (int i = 0; i < myIndent_; i++) {
        fprintf(stderr, " ");
      }
    fprintf(stderr, "> %s\n", msg_.c_str());
    }
  }

  ~Debug() {
    if (enabled_) {
      for (int i = 0; i < myIndent_; i++) {
        fprintf(stderr, " ");
      }
    fprintf(stderr, "< %s%s\n", msg_.c_str(), note_.c_str());
    }

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
    if (enabled_) {
      vsnprintf(buf, 1000, fmt, args);
    }
    va_end(args);
    note_ = std::string{" // "} + buf;
  }

  void print(const char* fmt, ...) {
    char buf[1000];
    va_list args;
    va_start(args, fmt);
    if (enabled_) {
      vsnprintf(buf, 1000, fmt, args);
    }
    va_end(args);

    if (enabled_) {
      for (int i = 0; i < myIndent_; i++) {
        fprintf(stderr, " ");
      }
      fprintf(stderr, "- %s\n", buf);
    }
  }

 private:
  int myIndent_;
  std::string msg_;
  std::string note_;

  bool isFuncMsg() const { return msg_[msg_.size() - 1] == ')'; }
};

int getCpu();

// TODO(lpe): Instead of 128, this should use
// std::hardware_destructive_interferrence_size.
class alignas(128) Slab {
public:
  // Returns true iff this is the first increment.
  bool increment();

  // Returns true iff this is the last decrement.
  bool decrement();

  size_t use_count() const;

private:
  std::atomic<int> counter_{0};
};

class Arena;

class Counter {
public:
  Counter() = default;

  Counter(Arena *arena, int originalCpu);

  Counter(const Counter& other);

  Counter(Counter&& other);

  Counter& operator=(const Counter& other);

  Counter& operator=(Counter&& other);

  ~Counter();

  // Returns true if the Counter cannot be decremented further. A false Counter
  // will always return false;
  bool destroy();

  operator bool() const;

  size_t use_count() const;

private:
  static constexpr uint64_t kFieldBits = 7;  // From alignas(128) for Arena.
  static constexpr uint64_t kFieldMask = (1UL << kFieldBits) - 1;
  // This comes from using alignas(4096) for Arena.
  static constexpr uint64_t kArenaMask = ~kFieldMask;
  static constexpr uint64_t kCpuOffset = 0;

  uintptr_t reference_{0};

  Arena* arena() const;

  int originalCpu() const;

  void increment();

  // Returns true if the Counter cannot be decremented further. A false Counter
  // will always return false.
  bool decrement();
};

class WeakCounter {
public:
  WeakCounter() = delete;

  WeakCounter(Arena *arena, int originalCpu);

  WeakCounter(const WeakCounter& other);

  WeakCounter(WeakCounter&& other);

  WeakCounter& operator=(const Counter& other);

  WeakCounter& operator=(WeakCounter&& other);

  ~WeakCounter() = default;

  Counter lock() const;

private:
  uintptr_t reference_;

  void increment();

  // Returns true if the WeakCounter cannot be decremented further.
  bool decrement();

  Arena* arena() const;

  int originalCpu() const;
};

// Contains space for up to 64 counters.
class alignas(128) Arena {
public:
  static Arena* create();
  static void destroy(Arena *arena);

  // Reserves a slot for a new counter. If this fails, then the resulting
  // Counter will evaluate to false.
  Counter getCounter();

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

  void markCpu(int cpu);
  uint64_t unmarkCpu(int cpu);
  uint64_t unmarkSlabSlot();
};

template <typename T>
class HyperSharedPointer {
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
