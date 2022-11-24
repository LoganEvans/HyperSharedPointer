# HyperSharedPointer

This was an experiment to test out a different way of managing the control
block for a shared\_ptr. The concept is to cache the counter on per-cpu
counters. In heavily shared situations, almost all of the atomic counter
operations will be uncontested.

While this code could be more heavily optimized, it's a long way from being on
parity with `std::shared_ptr`.

```
$ bazel run -c opt :hyper_shared_pointer_benchmark -- --benchmark_min_time=1
...
  Run on (12 X 4500 MHz CPU s)
  CPU Caches:
    L1 Data 32 KiB (x6)
      L1 Instruction 32 KiB (x6)
        L2 Unified 256 KiB (x6)
          L3 Unified 12288 KiB (x1)
          Load Average: 1.18, 1.04, 1.10
          ***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will incur extra overhead.
          ---------------------------------------------------------------------------------
          Benchmark                                       Time             CPU   Iterations
          ---------------------------------------------------------------------------------
          BM_CreateSharedPtr/threads:1                 24.4 ns         24.4 ns     59111205
          BM_CreateSharedPtr/threads:10                8.70 ns         87.0 ns     12253140
          BM_CreateHyperSharedPointer/threads:1        51.7 ns         51.7 ns     27796853
          BM_CreateHyperSharedPointer/threads:10        169 ns         1692 ns       784070
          BM_SharingHyperSharedPointer/threads:1       44.4 ns         44.4 ns     31457713
          BM_SharingHyperSharedPointer/threads:9       8.80 ns         79.2 ns     19449045
          BM_SharingSharedPointer/threads:1            7.37 ns         7.37 ns    204555296
          BM_SharingSharedPointer/threads:9            1.41 ns         12.7 ns    110009268

```

The two `threads:9` benchmarks are the key ones here. The `HyperSharedPointer`
code is about an order of magnitude slower.

It was an interesting experiment, but it's not likely that this idea is going
anywhere.
