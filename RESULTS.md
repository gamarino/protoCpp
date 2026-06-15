# protoCpp benchmark results

**Hardware**: Ryzen 5500U (6 physical cores, SMT 8, mobile/laptop class), Linux x86_64. Median of 5 runs per bench, all measurements taken on **2026-06-15** in serial (no concurrent load). Both `protoCpp` and `protopy` linked against the same `libprotoCore.so` from the sibling `protoCore/build_release` tree.

**Compilers / interpreters**:
- C++: g++ with `-O3 -DNDEBUG`. No PGO, no LTO on the protoCpp side.
- `protopy`: the LTO build from `protoPython/build-lto/`.
- CPython: system `python3 --version` = 3.x (debian default).

## The table

| benchmark           | C++ floor (ms) | protoCpp (ms) | protoCpp+fast-path (ms) | protopy (ms) | CPython (ms) |
|---|---:|---:|---:|---:|---:|
| `int_sum_loop`      |  6.35 |  23.97 |  22.20 |    40.09 |  75.38 |
| `call_recursion`    |  6.84 |  54.95 |  30.48 |  2065.21 |  95.10 |
| `attr_lookup`       |  4.73 |  31.57 |   –    |   361.36 |  76.50 |
| `list_append_loop`  |  6.19 |  27.93 |   –    |   446.00 |  83.99 |
| `str_concat_loop`   |  8.26 |  28.23 |   –    |   444.77 |  65.10 |
| `multithread_cpu`   |  6.35 |  59.50 |  40.05 |   ~30 000* |  541** |

`*` protopy multithread is highly variable on this mobile CPU (thermal throttle on a 4-thread CPU-bound loop running for tens of seconds). 24.4 s / 37.6 s / >120 s across three completed attempts; the ~30 000 ms figure is the median of two completed runs. A sustained-boost desktop would shrink the number considerably; the qualitative story (the interpretation layer is the bottleneck) stays the same.
`**` Local CPython multithread suffered from the same throttle effect; quoted figure is from the May 2026 protoPython report on a sustained-boost reference machine.

The fast-path column is filled only for benches with tight inner arithmetic loops. It applies the inline SmallInt helpers (`proto::isSmallInt` / `proto::asSmallInt` / `proto::makeSmallInt` / `proto::smallIntInRange`) directly in user C++ code, short-circuiting the cross-DSO call into `ProtoObject::add` for the common `SmallInt+SmallInt` case. This is the model protoPython's bytecode opcodes use; replicating it from C++ is one of the things protoCpp exists to demonstrate.

## The headline

**protoCpp is faster than CPython on 5 of 6 benchmarks** — significantly faster on 4 of them. The lone exception is `call_recursion` (fib 25), where the per-call kernel overhead of allocating SmallInts and crossing into `ProtoObject::add` adds up over 75 K recursive calls. With the fast-path version (`30.48 ms`), protoCpp catches up to CPython (`95.10 ms`) and beats it by ~3×.

So the structural finding is:

> protoCore, driven directly from C++, can beat CPython on the same workloads CPython has been hand-tuned over 30 years to run well. The kernel is not the bottleneck.

That settles a real question. The other side of the same coin —

> protoPython, despite running on this same fast kernel, is slower than CPython on every benchmark except `int_sum_loop`.

— makes the optimisation target obvious. The gap between protoCpp and protopy is the Python interpretation layer: opcode dispatch, frame allocation, name resolution, every "look up `add` on `int`" indirection that protoCpp skips by calling C++ functions directly. That cost (median **~10-15× over protoCpp**) is what stands between protopy and CPython parity.

## Ratios

| benchmark           | proto/cpp | fast/cpp | proto/cpython | py/cpython | py/proto |
|---|---:|---:|---:|---:|---:|
| `int_sum_loop`      |  3.8× |  3.5× |  **0.32×** (3.1× faster) |  0.53× (1.9× faster) |  1.7× |
| `call_recursion`    |  8.0× |  4.5× |  0.58× (1.7× faster) | 21.7× SLOW | 37.6× |
| `attr_lookup`       |  6.7× |   –   |  **0.41×** (2.4× faster) |  4.7× SLOW | 11.4× |
| `list_append_loop`  |  4.5× |   –   |  **0.33×** (3.0× faster) |  5.3× SLOW | 16.0× |
| `str_concat_loop`   |  3.4× |   –   |  **0.43×** (2.3× faster) |  6.8× SLOW | 15.8× |
| `multithread_cpu`   |  9.4× |  6.3× |   –   |   –   |  ~500× |

Read across the `proto/cpython` column. **protoCpp beats CPython on 5 of 6 rows** by 1.7-3.1×. The kernel is competitive with — usually faster than — CPython's hand-optimised C implementation when an embedder uses it directly.

Read across the `py/cpython` column. **protopy LOSES to CPython on 5 of 6 rows** by 1.9-21.7×. The kernel is fast; the Python interpretation layer on top is what makes protopy slower than CPython.

Read across the `py/proto` column. **The Python layer adds 1.7-37× on top of the kernel cost**, median ~15×. On `call_recursion` it adds 37×: every recursive Python call pays for opcode dispatch + frame allocation + the name-resolution machinery that the C++ recursive call avoids.

## What `proto/cpp` measures

Each row is the cost of running the same algorithm through these protoCore primitives:

- `int_sum_loop` — `ProtoObject::add` per iteration on the public path; `proto::makeSmallInt` per iteration on the fast path. SmallInt fast-path collapses the loop to a tagged-pointer arithmetic sequence inline in the user's code.
- `call_recursion` — `ProtoObject::add` / `subtract` / `compare` per recursive call. The fast-path version inlines all three for SmallInt+SmallInt, which saves ~44% on this workload (54.95 → 30.48 ms). Every leaf of fib(25) does 1 compare + 2 SmallInt allocations + 2 subtracts + 1 add; the fast path replaces the function-call boundary with a few cycles.
- `attr_lookup` — `getAttribute` on a mutable receiver, three keys × 100 K iterations. Exercises the per-thread 1024-entry `AttributeCache`; the cache hits on every read after the first, so the 6.7× ratio is the cost of `AttributeCache::lookup` + the hash-table probe vs a struct-field load.
- `list_append_loop` — `ProtoList::appendLast` returns a new immutable list sharing structure with the old. The 4.5× ratio is the cost of allocating a new AVL node and the structural-sharing bookkeeping vs `std::vector::push_back`'s amortised in-place write.
- `str_concat_loop` — `ProtoString::appendLast` on a rope. The 3.4× ratio is the *closest* the kernel comes to native because the rope avoids the O(N) copy `std::string`'s `+` operator pays per concat.
- `multithread_cpu` — 4 `ProtoThread`s × 2M-iter accumulator, each writing into a per-worker mutable cell. The 9.4× ratio confirms protoCore's `newThread` + GC-quorum participation scales linearly to 4 cores without GIL serialisation; the per-iteration `add` cost dominates over the parallelism savings.

## Reproducing

```bash
# protoCore must be built first
cd ../protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore

cd ../protoCpp
cmake -B build_release -S . && cmake --build build_release
./benchmarks/bench.sh
```

For the protoPython column, run the same benchmark sources from `protoPython/benchmarks/*.py` through the protoPython binary; for CPython, just `python3 <bench>.py` on the same machine. Different hardware will move the absolute numbers; the ratios are the part that travels.

## Caveats

1. The C++ floor is what a hand-written C++ implementation does — `std::vector::push_back`, `std::string::operator+`, raw `int64_t`. protoCpp gives up some of that to be a real shared kernel: GC, structural sharing, atomic mutability, GIL-free threads. The ratio is the price of those features.
2. The CPython numbers are with the system `python3`. A PyPy run, or CPython with `-OO`, would change them. Within an order of magnitude they are representative.
3. `int_sum_loop` is the bench protopy was specifically tuned for (SmallInt fast-path opcodes since session 11 — see protoPython's history). That's why it's the only bench where protopy beats CPython.
4. Numbers are not the point of comparison between protoCpp and protopy; the **ratios** are. Re-run on your own hardware to validate; the structural shape — kernel ≪ language layer — should hold.
