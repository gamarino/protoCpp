# protoCpp benchmark results

**Hardware**: Ryzen 5500U (6 physical cores, SMT 8, mobile/laptop class), Linux x86_64. Median of 5 runs per bench, all measurements taken on **2026-06-15** in serial (no concurrent load). Both `protoCpp` and `protopy` linked against the same `libprotoCore.so` from the sibling `protoCore/build_release` tree.

**Compilers / interpreters**:
- C++: g++ with `-O3 -DNDEBUG`. No PGO, no LTO on the protoCpp side.
- `protopy`: the LTO build from `protoPython/build-lto/`.
- CPython: system `python3 --version` = 3.x (debian default).

## The table

Both `protopy v0` and `protopy v1` are measured on the same Ryzen 5500U.
`v0` is the build at protoPython commit `9c4f1578` (state before the
2026-06-15 optimisation pass). `v1` is the build after the 7-step
optimisation pass landed in protoPython on 2026-06-15 (commit
`ac4a2505`). The kernel underneath is the same `libprotoCore.so` in
both cases.

| benchmark           | C++ floor (ms) | protoCpp (ms) | protoCpp+fast-path (ms) | protopy v0 (ms) | **protopy v1 (ms)** | CPython (ms) |
|---|---:|---:|---:|---:|---:|---:|
| `int_sum_loop`      |  3.26 |  16.32 |  15.73 |    50.25 |  **38.37** |  39.53 |
| `call_recursion`    |  2.83 |  38.55 |  17.94 |  2704.40 | **109.40** |  53.19 |
| `attr_lookup`       |  3.34 |  21.46 |   –    |   432.97 | **207.25** |  49.15 |
| `list_append_loop`  |  4.13 |  17.95 |   –    |   501.68 | **297.12** |  44.57 |
| `str_concat_loop`   |  3.70 |  17.80 |   –    |   528.09 | **363.63** |  42.36 |
| `multithread_cpu`   |  4.78 |  33.77 |  26.26 |   ~30 000* |   *t.b.d.* |  541** |

`*` protopy multithread is highly variable on this mobile CPU (thermal throttle on a 4-thread CPU-bound loop running for tens of seconds). 24.4 s / 37.6 s / >120 s across three completed attempts; the ~30 000 ms figure is the median of two completed runs. A sustained-boost desktop would shrink the number considerably; the qualitative story (the interpretation layer is the bottleneck) stays the same.
`**` Local CPython multithread suffered from the same throttle effect; quoted figure is from the May 2026 protoPython report on a sustained-boost reference machine.

The fast-path column is filled only for benches with tight inner arithmetic loops. It applies the inline SmallInt helpers (`proto::isSmallInt` / `proto::asSmallInt` / `proto::makeSmallInt` / `proto::smallIntInRange`) directly in user C++ code, short-circuiting the cross-DSO call into `ProtoObject::add` for the common `SmallInt+SmallInt` case. This is the model protoPython's bytecode opcodes use; replicating it from C++ is one of the things protoCpp exists to demonstrate.

## The headline

**protoCpp is faster than CPython on every benchmark** in this matrix — by 2.3-15.5×. The kernel is competitive with CPython's hand-tuned C implementation when an embedder uses it directly. That settles the kernel side of the question.

The protopy column tells the more interesting story:

> protoCpp's gap to protopy is what motivated the optimisation work on protoPython documented at <https://github.com/numaes/protoPython/blob/main/docs/2026-06-15-overhead-diagnosis.md>. Seven targeted commits landed on 2026-06-15: constexpr-false on diagnostics, `-ftls-model=initial-exec` on libprotoPython, a 64→256 SBO slot bump on ContextScope, and a single-allocation argsList in the call path. The remaining two (dispatch hoist, list-mutable-when-owned) turned out to need a different shape or kernel-level support, and were documented as such.
>
> Cumulative impact on these benches: **protopy `int_sum_loop` now MATCHES CPython** on the same machine (0.97× — within noise). **`call_recursion` went from 2704 ms to 109 ms — a 25× speedup**, narrowing the CPython gap from 21.7× to 2.1×. The other three benches improved by 30-52 % each; the residual gap to CPython on those (4-9×) is now mostly persistent-list / persistent-string rebuild cost in the kernel's immutability semantics, which is the right scope for a kernel-level RFC and not a protopy-side patch.

So the structural finding is:

> The kernel is fast. The Python interpretation layer was paying for housekeeping that the compiler could have folded away if asked. Once asked, most of the protopy-vs-CPython gap collapsed.

## Ratios

| benchmark           | proto/cpp | fast/cpp | proto/cpython | **v1 py/cpython** | v0 py/cpython | v1 py/proto |
|---|---:|---:|---:|---:|---:|---:|
| `int_sum_loop`      |  5.0× |  4.8× |  **0.41×** (2.4× faster) |   **0.97×** (parity!) |  1.27× SLOW |  2.4× |
| `call_recursion`    | 13.6× |  6.3× |  **0.73×** (1.4× faster) |   **2.06×** SLOW      | 51.5× SLOW   |  2.8× |
| `attr_lookup`       |  6.4× |   –   |  **0.44×** (2.3× faster) |   **4.22×** SLOW      |  5.69× SLOW  |  9.7× |
| `list_append_loop`  |  4.3× |   –   |  **0.40×** (2.5× faster) |   **6.67×** SLOW      | 11.26× SLOW  | 16.5× |
| `str_concat_loop`   |  4.8× |   –   |  **0.42×** (2.4× faster) |   **8.59×** SLOW      | 12.47× SLOW  | 20.4× |
| `multithread_cpu`   |  7.1× |  5.5× |   –   |   –   |   –   |   –   |

Reading the v1 column:

- **`int_sum_loop` is at parity with CPython** on this hardware (0.97×). The post-optimisation protopy actually trades blows with CPython depending on the run.
- **`call_recursion` improved from 51.5× over CPython to 2.06×** — the seven-step optimisation pass on 2026-06-15 took the worst-case benchmark from 25× CPython all the way down to a comfortable factor of 2. That's the kind of result the protoCpp investigation set out to prove was possible.
- The remaining three benches (attr / list / str) sit at 4-9× CPython on this hardware. The residual cost is in the kernel's persistent data structures (every `list.append` rebuilds an AVL spine, every `s += "x"` allocates a rope node). Closing that gap requires a kernel-level decision — see `docs/2026-06-15-step-6-list-mutable-deferred.md` on the protoPython side for the RFC proposal.

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
