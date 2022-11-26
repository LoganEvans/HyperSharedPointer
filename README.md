# HyperSharedPointer

A `HyperSharedPointer` behaves like `std::shared_ptr`, but when the pointer is
shared many times across lots of threads, it is faster by as much as 9x.

Beware! Since `HyperSharedPointer` is slow to create and consumes a large
amount of memory, it is not a drop-in replacement for `std::shared_ptr`. Also,
support for a `std::weak_ptr` corollary is not yet complete.

The basic concept behind `HyperSharedPointer` is to shard the control-block
counter for a shared pointer across each CPU. When the pointer is heavily shared,
nearly all of the atomic counter operations will be uncontested.

```
$ bazel run -c opt :hyper_shared_pointer_benchmark -- --benchmark_min_time=1
2022-11-25T23:07:30-08:00
Running bazel-bin/hyper_shared_pointer_benchmark
Run on (12 X 4500 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x6)
    L1 Instruction 32 KiB (x6)
      L2 Unified 256 KiB (x6)
        L3 Unified 12288 KiB (x1)
        Load Average: 0.69, 0.70, 0.64
        ***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will incur extra overhead.
        ----------------------------------------------------------------------------------
        Benchmark                                        Time             CPU   Iterations
        ----------------------------------------------------------------------------------
        BM_CreateStdSharedPtr/threads:1               24.9 ns         24.9 ns     56765746
        BM_CreateStdSharedPtr/threads:12              5.19 ns         62.2 ns     24550152
        BM_CreateHyperSharedPointer/threads:1         4509 ns         4506 ns       306655
        BM_CreateHyperSharedPointer/threads:12         854 ns         9962 ns       149592
        BM_SharingStdSharedPtr/threads:1              15.6 ns         15.6 ns     87822375
        BM_SharingStdSharedPtr/threads:12             38.0 ns          456 ns      3195936
        BM_SharingHyperSharedPointer/threads:1        26.9 ns         26.9 ns     51484835
        BM_SharingHyperSharedPointer/threads:12       4.07 ns         48.9 ns     29400012
```

The `BM_SharingStdSharedPtr/threads:12` and
`BM_SharingHyperSharedPointer/threads:12` benchmarks model the hyper sharing use
case.
