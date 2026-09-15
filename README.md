# protoCpp

**The protoCore ceiling, in C++.**

protoCpp uses [protoCore](https://github.com/numaes/protoCore) directly: plain C++20 drives the kernel through its public header. There is no interpreter, no bytecode loop and no symbol-table dispatch. The language runtimes built on protoCore ([protoJS](https://github.com/gamarino/protoJS), [protoPython](https://github.com/gamarino/protoPython), [protoST](https://github.com/gamarino/protoST), [protoClojure](https://github.com/gamarino/protoClojure)) each add a language layer on top of the kernel. protoCpp removes that layer, so its benchmark numbers exclude any language layer.

It serves two purposes:

1. **A reference for embedders.** Six short examples (30 to 98 lines each) cover the primitives a C++ application uses to drive protoCore:
   - building and reading a list
   - SmallInt arithmetic with the inline fast-path helpers
   - atomic compare-and-swap on a mutable attribute
   - persistent collections with structural sharing
   - OS threads managed by protoCore
   - a hand-built one-actor system
2. **A protoCore performance ceiling.** Each benchmark has two versions:
   - a pure C++ version that does not link protoCore (the language floor)
   - a protoCpp version that does the same work through the kernel

   `int_sum_loop`, `call_recursion` and `multithread_cpu` also have a fast-path variant that uses the inline SmallInt helpers. The ratio between the C++ and protoCpp versions is intended to show the cost of going through the kernel, and the gap between protoCpp and a language runtime the cost of the language layer. [`RESULTS.md`](RESULTS.md) describes what the current measurements can and cannot show.

## What "no overhead" does not mean

protoCpp removes the language overhead only. It **still pays for**:

- The concurrent garbage collector.
- The 1024-entry per-thread `AttributeCache` and the 256-shard `mutableRoot` table.
- Allocation and structural sharing in the AVL-backed `ProtoList` / `ProtoTuple` / `ProtoSparseList`.
- SmallInt encoding and decoding on every numeric operation, unless the inline fast-path helpers are used (see `examples/02_smallint_fast_path.cpp`).
- Atomic (compare-and-swap) updates of mutable objects.

protoCpp's numbers therefore show what an application can achieve through protoCore's public C++ API without a language runtime. They do not show the performance of hand-written C++ that does not use protoCore at all. The `bench_cpp_*` binaries measure that, for reference.

## Build

protoCpp expects [protoCore](https://github.com/numaes/protoCore) at `../protoCore`, or at the prefix given with `-DPROTO_CORE_PREFIX=<path>`. Build the kernel first, then this repository:

```bash
cd ../protoCore
cmake -B build_release -S . && cmake --build build_release --target protoCore

cd ../protoCpp
cmake -B build_release -S . && cmake --build build_release
```

## Run

Examples:

```bash
./build_release/example_01_hello
./build_release/example_02_smallint_fast_path
./build_release/example_03_atom_cas
./build_release/example_04_structural_sharing
./build_release/example_05_threads
./build_release/example_06_actor_manual
```

Benchmarks:

```bash
./benchmarks/bench.sh          # median of 5 runs per binary
```

Each row pairs `bench_cpp_<name>` (pure C++ floor) with `bench_proto_<name>` (protoCpp), plus `bench_proto_fast_<name>` where a fast-path variant exists. The script prints the median times and the protoCpp / C++ ratios.

## Results

[`RESULTS.md`](RESULTS.md) has the full table, measured on 2026-06-15 on an AMD Ryzen 5 5500U laptop. Its columns are C++ floor, protoCpp, protoCpp with fast path, protopy, protopyc and CPython. protoCpp, protopy and protopyc link against the same `libprotoCore.so`. All values are whole-process wall times. In that measurement:

- **In whole-process wall time, protoCpp finishes before CPython** on the five benchmarks with a CPython measurement at the same problem size:
  - `call_recursion`: 1.04× faster
  - `attr_lookup`: 1.96× faster
  - `list_append_loop`: 1.99× faster
  - `str_concat_loop`: 2.39× faster
  - `multithread_cpu`: 14.33× faster, on four native threads

  `int_sum_loop` has no CPython value at the same size. CPython's `startup_empty` median in the same report (64.75 ms) is longer than its `call_recursion`, `list_append_loop` and `str_concat_loop` times, so on those three rows the CPython value is dominated by interpreter start-up rather than by the workload. The C++ and Python columns also come from different harnesses and separate runs.
- **Going through the kernel** costs 3.87× to 32.60× the time of plain C++ doing the same work (whole-process wall time, including ProtoSpace start-up).
- **The inline SmallInt helpers** cut protoCpp wall time by 58.9% on `call_recursion` (51.45 → 21.16 ms), 42.0% on `int_sum_loop` and 18.4% on `multithread_cpu`.
- **The Python runtimes on the same kernel are slower than CPython** on these benchmarks, excluding `int_sum_loop`, whose Python values are dominated by a module import added to the script just before the source report:
  - protopy: 1.19× to ≈5.66× slower
  - protopyc: 1.09× to 6.05× slower, on the rows where its value is valid and measured at the same size

  At these sizes the Python timings include interpreter start-up and, for scripts that import `os`, module loading, so this table does not show where the Python runtimes' remaining cost lies.

## Layout

```
protoCpp/
├── CMakeLists.txt
├── LICENSE
├── README.md       # this file
├── RESULTS.md      # the published benchmark table
├── examples/       # 6 short demos, one feature each
│   ├── 01_hello.cpp
│   ├── 02_smallint_fast_path.cpp
│   ├── 03_atom_cas.cpp
│   ├── 04_structural_sharing.cpp
│   ├── 05_threads.cpp
│   ├── 06_actor_manual.cpp
│   └── README.md
└── benchmarks/
    ├── bench.sh
    ├── cpp/        # pure C++ floor, no protoCore link
    ├── proto/      # protoCpp, same work through the kernel API
    └── proto_fast/ # protoCpp with the embedder-side SmallInt fast path
                    #   (int_sum_loop, call_recursion, multithread_cpu)
```

## Related projects

Four language runtimes (protoJS, protoPython, protoST, protoClojure) and protoCpp's C++ examples are built on protoCore.

| Project | Role | Repository |
|---|---|---|
| protoCore | C++20 object model and runtime kernel: immutable structures, concurrent GC, GIL-free threads | https://github.com/numaes/protoCore |
| protoJS | JavaScript runtime on protoCore | https://github.com/gamarino/protoJS |
| protoPython | Python 3 runtime (protopy) and ahead-of-time compiler (protopyc) on protoCore | https://github.com/gamarino/protoPython |
| protoST | Smalltalk-inspired actor language on protoCore | https://github.com/gamarino/protoST |
| protoClojure | Clojure dialect on protoCore (early stage) | https://github.com/gamarino/protoClojure |
| protoCpp | Examples and benchmarks using protoCore directly from C++ | https://github.com/gamarino/protoCpp |

## License

Copyright (c) 2026 Gustavo Marino. Released under the MIT License; see [LICENSE](LICENSE).
