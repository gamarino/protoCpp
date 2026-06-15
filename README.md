# protoCpp

**The protoCore ceiling, in C++.**

protoCpp is the most direct way to use [protoCore](https://github.com/numaes/protoCore): plain C++20 driving the kernel through its public header, with no interpreter, no bytecode loop, no symbol-table dispatch. The other family members — [protoJS](https://github.com/numaes/protoJS), [protoPython](https://github.com/numaes/protoPython), [protoST](https://github.com/numaes/protoST), [protoClojure](https://github.com/gamarino/protoClojure) — each add a language runtime on top of the kernel. protoCpp removes that layer so the numbers reflect protoCore itself.

It exists for two reasons:

1. **A reference for embedders.** Six short examples (≤ 60 lines each) walk through every primitive a C++ application needs to drive protoCore: SmallInt arithmetic with the inline fast-path helpers, atomic CAS on a mutable attribute, persistent collections with structural sharing, OS threads that participate in the concurrent GC, and a hand-built one-actor system.

2. **A protoCore performance ceiling.** Each benchmark has two variants — a pure C++ version that does not link protoCore at all (the language floor), and a protoCpp version that performs the same work through the kernel (the protoCore ceiling). The ratio between them is the cost of going through the kernel; the gap between protoCpp and the language runtimes is how much the language layer costs on top.

## What "no overhead" does not mean

It is the language overhead that is removed. protoCpp **still pays for**:

- The concurrent garbage collector (mark, sweep, survivor re-chain, per-thread thresholds).
- The 1024-entry per-thread `AttributeCache` and the 256-shard `mutableRoot` table.
- AVL-backed `ProtoList` / `ProtoTuple` / `ProtoSparseList` allocation and structural sharing.
- The SmallInt encoding / decoding on every numeric operation (unless you use the inline fast-path helpers — see `examples/02_smallint_fast_path.cpp`).
- Atomic `setAttributeIfEqual` on every mutable write.

The number protoCpp publishes is the **best achievable performance for an application that drives protoCore through its public API in C++**. It is not the best achievable performance for a hand-written C++ application that does not use protoCore at all — that is what the `bench_cpp_*` column measures, for reference.

## Build

protoCpp depends on [protoCore](https://github.com/numaes/protoCore) sitting at `../protoCore` (or pointed at by `-DPROTO_CORE_PREFIX=<path>`). Build the kernel first, then this repo:

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
./benchmarks/bench.sh          # median of 5 runs, prints the table
```

Each row pairs `bench_cpp_<name>` (pure C++ floor) with `bench_proto_<name>` (protoCpp).

## Results

See [`RESULTS.md`](RESULTS.md) for the full 5-column table (C++ floor / protoCpp / protoCpp with fast-path / protopy / CPython). The headline on a Ryzen 5500U laptop:

> **protoCpp beats CPython on 5 of 6 benchmarks** — by 1.7-3.1×. The kernel is competitive with CPython's hand-tuned C implementation when an embedder uses it directly.

> **protopy LOSES to CPython on 5 of 6 benchmarks** — by 1.9-22×. The kernel is fast; the Python interpretation layer sitting on top is what makes protopy slower than CPython.

The split answers the question "how much of the gap is the kernel, and how much is the language runtime?" — and the practical answer is **the runtime dominates everywhere except the trivial integer-loop case**. protopy's optimisation target is the interpreter; protoCore itself is not the bottleneck.

Embedder-side fast-path numbers (using the inline SmallInt helpers from `protoCore.h`) shrink the kernel cost by ~44% on `call_recursion` (fib 25), matching what protoPython's INPLACE_ADD opcodes already do.

## Layout

```
protoCpp/
├── CMakeLists.txt
├── README.md       # this file
├── RESULTS.md      # the published bench table
├── examples/       # 6 short demos, each one feature
│   ├── 01_hello.cpp
│   ├── 02_smallint_fast_path.cpp
│   ├── 03_atom_cas.cpp
│   ├── 04_structural_sharing.cpp
│   ├── 05_threads.cpp
│   ├── 06_actor_manual.cpp
│   └── README.md
└── benchmarks/
    ├── bench.sh
    ├── cpp/        # pure C++ floor — no protoCore link
    ├── proto/      # protoCpp — same semantics through the kernel API
    └── proto_fast/ # protoCpp with embedder-side SmallInt fast path
                   #   (where it applies — int / fib / multithread)
```

## License

MIT. See `LICENSE`.

## Related

- [protoCore](https://github.com/numaes/protoCore) — the prototype-based kernel.
- [protoJS](https://github.com/numaes/protoJS) / [protoPython](https://github.com/numaes/protoPython) / [protoST](https://github.com/numaes/protoST) / [protoClojure](https://github.com/gamarino/protoClojure) — language runtimes built on the same kernel.
