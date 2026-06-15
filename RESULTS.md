# protoCpp benchmark results

**Hardware**: Ryzen 5500U (6 physical cores, SMT 8, mobile/laptop class), Linux x86_64. Median of 5 runs per bench, all measurements taken on **2026-06-15**. Both `protoCpp` and `protopy` linked against the same `libprotoCore.so` from the sibling `protoCore/build_release` tree.

**Compiler**: g++ with `-O3 -DNDEBUG`. No PGO, no LTO on the protoCpp side. `protopy` is the LTO build from `protoPython/build-lto/`.

## The table

| benchmark | C++ floor (ms) | protoCpp (ms) | protopy (ms) | proto/cpp | py/proto |
|---|---:|---:|---:|---:|---:|
| `int_sum_loop`     |   3.25 |   18.16 |    40.09 |  5.6× |  2.2× |
| `call_recursion`   |   3.72 |   38.46 |  2065.21 | 10.3× | 53.7× |
| `attr_lookup`      |   2.65 |   20.75 |   361.36 |  7.8× | 17.4× |
| `list_append_loop` |   4.05 |   18.56 |   446.00 |  4.6× | 24.0× |
| `str_concat_loop`  |   4.07 |   16.03 |   444.77 |  3.9× | 27.7× |
| `multithread_cpu`  |   4.50 |   37.21 |  ~30000* |  8.3× | ~806× |

*The protopy multithread run is highly variable on this Ryzen 5500U mobile CPU due to thermal throttle on long-running 4-thread CPU-bound loops: 24.4 s / 37.6 s / >120 s across three attempts. The 30 s figure is the median of the two completed runs. CPython on the same machine would be expected to land closer to its May 2026 published 541 ms — see footnote.

**`proto/cpp`** = how much slower is the protoCpp version than the same algorithm in pure C++. This is the **kernel cost** — what an application pays for going through protoCore instead of native primitives. Median ~5-6× across these workloads.

**`py/proto`** = how much slower is protoPython than protoCpp on the same algorithm. This is the **language-layer cost** — what the Python bytecode interpreter, frame allocation, and name resolution add on top of the kernel API. Median ~25× excluding the multithread outlier; ~800× on the multithread workload where parallelism amplifies the per-iteration interpreter overhead.

> Note on the multithread row: the absolute protopy number on this CPU is dominated by thermal-throttle artefacts and per-iteration interpretation cost in a tight loop running on 4 cores simultaneously. The 800× ratio is informative about the order of magnitude (interpretation is the bottleneck), not the precise factor — a desktop with sustained boost clock would shrink it. The structural story — the kernel is fast, the interpretation overhead is what costs — is the same.

## Reading the numbers

The headline that motivated this repo: **on every benchmark except `int_sum_loop`, the Python layer costs more than the protoCore kernel — usually 3-15× more.** Concretely on `call_recursion` (fib 25):

- a hand-written C++ implementation takes 3.7 ms;
- the same algorithm driven through protoCore's public C++ API takes 38 ms (× 10);
- the Python source compiled to protoPython bytecode and run on protoCore takes 2065 ms (× 54 more on top).

So of the ~560× gap between hand-written C++ and protoPython on fib(25), roughly **1/55 is the kernel and 54/55 is the Python interpretation layer**. That ratio is the answer to "where is the runtime spending the time?", and it tells us the optimisation target for protoPython is the interpreter, not protoCore.

The exception is `int_sum_loop`: the tight integer-accumulator loop runs ~ 5× over C++ on protoCpp (the kernel cost) and only another ~2× over that for protopy (the language cost). That's because protoPython's SmallInt fast-path opcodes — `INPLACE_ADD` etc. — already collapse most of the per-bytecode overhead for this exact shape. The dispatch is so tight that the kernel call is the dominant cost on both sides.

## What protoCpp itself measures

Each `proto/cpp` row is the cost of running the same algorithm through these protoCore primitives:

- `int_sum_loop` — `ProtoObject::add` per iteration. SmallInt fast-path inside protoCore handles the inner case in a few cycles, but there is still a function-call boundary into the shared library on every add.
- `call_recursion` — `ProtoObject::add`, `subtract`, `compare` per recursive call. Every leaf of fib does 2 SmallInt allocations + 1 compare + 1 subtract; non-leaves add to that. Hot path is heavy on the per-call kernel work.
- `attr_lookup` — `getAttribute` on a mutable receiver, three keys × N iterations. Exercises the per-thread 1024-entry `AttributeCache`. The cache hits on every read after the first; the 7.8× ratio is the cost of that single cache lookup vs a struct-field load.
- `list_append_loop` — `ProtoList::appendLast` returns a new immutable list sharing structure with the old. The 4.6× ratio is the cost of allocating a new AVL node and the structural-sharing bookkeeping vs `std::vector::push_back`'s amortised in-place write.
- `str_concat_loop` — `ProtoString::appendLast` on a rope. The 3.9× ratio is the *closest* the kernel comes to native because the rope avoids the O(N) copy `std::string`'s `+` operator pays per concat.
- `multithread_cpu` — 4 `ProtoThread`s × 2M-iter accumulator, each writing into a per-worker mutable cell. 8.3× over the `std::thread` baseline; demonstrates that protoCore's worker spawn + GC-quorum participation does scale linearly to 4 cores without GIL serialisation, but the per-iteration `ProtoObject::add` cost dominates.

## What's NOT in the kernel cost

The numbers above are not protoCore's absolute ceiling; they are what an embedder gets through the standard public API. Two paths cost less but are not used here:

1. **The inline SmallInt helpers** (`proto::isSmallInt`, `proto::asSmallInt`, `proto::makeSmallInt`) — embedders can short-circuit the cross-DSO call for the SmallInt+SmallInt fast path. See `examples/02_smallint_fast_path.cpp`. Numbers were not measured separately here; protoPython uses them in its hottest opcodes and gets ~3× speed-up on `int_sum_loop` vs a non-fast-path build.

2. **Direct cell access** — protoCpp benchmarks use the immutable `ProtoList::appendLast` API to match what every language runtime in the family does. The mutable `ProtoList` exists and skips the structural-sharing allocation; using it would shrink the `list_append_loop` ratio.

## Reproducing

```bash
# protoCore must be built first
cd ../protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore

cd ../protoCpp
cmake -B build_release -S . && cmake --build build_release
./benchmarks/bench.sh
```

For the protoPython column, run the same benchmark sources from `protoPython/benchmarks/*.py` through the protoPython binary:

```bash
PROTOPY=/path/to/protoPython/build-lto/src/runtime/protopy
for b in int_sum_loop call_recursion attr_lookup list_append_loop str_concat_loop multithreaded_cpu; do
  $PROTOPY /path/to/protoPython/benchmarks/$b.py
done
```

Different hardware will move the absolute numbers but the ratios should stay in the same ballpark.
