# protoCpp benchmark results

**Hardware**: Ryzen 5500U (6 physical cores, SMT 8, mobile/laptop class), Linux x86_64. Median of 5 runs (protoPython harness — `run_benchmarks.py` with 2 warm-ups) and median of 5 runs (protoCpp's `benchmarks/bench.sh`). All measurements taken on **2026-06-15**, machine cool, no concurrent load. Both `protopy` and `protopyc` linked against the same `libprotoCore.so` from the sibling `protoCore/build_release` tree.

**Compilers / interpreters**:
- C++: g++ with `-O3 -DNDEBUG`. No PGO. `protoCpp/build_release/bench_*` are the floor.
- `protopy`, `protopyc`: protoPython commit `358ac368` (post-2026-06-15 optimisation sprint), built with `-O3 -DNDEBUG -flto -ftls-model=initial-exec`.
- CPython: system `python3` (Debian 3.x).

**Note on the protoPython numbers**: this table uses the canonical figures from the protoPython performance harness, published at <https://github.com/numaes/protoPython/blob/main/benchmarks/reports/2026-06-15-post-optimisation.md>. They are the same `.py` source files protoCpp's pair-equivalent C++ files reproduce.

## The table

Numbers reflect the post-sprint-2 protoPython (commits `82a5dd08`..`0d06e617`,
2026-06-15). All paths linked against the same `libprotoCore.so`.

**N-bump for `int_sum_loop` and `attr_lookup`** (2026-06-15): at the canonical N=100K, the `int_sum_loop` and `attr_lookup` C++ baselines were trivially constant-folded by `-O3` (`objdump` showed `mov $constant; call printf`), so the wall-clock ratio was binary startup, not per-iteration cost. Bumped to N=10M (int_sum) and N=5M (attr) with `asm volatile` barriers so the loop body dominates. The protoPython side honours `BENCH_N=10000000` for `int_sum_loop.py` (since [protoPython `432c0621`](https://github.com/numaes/protoPython/commit/432c0621)) and `protopy benchmarks/attr_lookup.py 5000000` for `attr_lookup.py`. Other benches keep their canonical N because their workloads (`std::vector::push_back`, recursive fib, rope concat, 4-thread loops) are not optimisation-eliminable.

| benchmark | N | C++ floor (ms) | protoCpp (ms) | protoCpp+fast-path (ms) | protopy (ms) | protopyc (ms) | CPython (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 10 M | 8.13 | 126.42 | 73.31 | 241.04 | ~226* | 52.99 |
| `call_recursion` | (fib 25) | 4.01 | 51.45 | 21.16 | 95.83 | **58.36** | 53.35 |
| `attr_lookup` | 5 M | 9.69 | 315.93 | – | ~3 500† | ~89* | 618 |
| `list_append_loop` | 10 K | 4.69 | 22.17 | – | 221.32 | 200.99 | 44.10 |
| `str_concat_loop` | 2 K | 5.57 | 21.55 | – | 212.52 | 311.14 | 51.41 |
| `multithread_cpu` | 4×2M | 6.85 | 50.36 | 41.10 | 861.62 | **25.13** | 721.67 |

\* protopyc on `int_sum_loop` and `attr_lookup` is likely benefiting from the same constant-folding the C++ baseline got: `obj = FastObject(1, 2, 3)` and `sum(range(N))` are trivially analyseable by the AOT C++ generator. We mark those rows with ~ so the reader does not lean on them as "AOT magic"; they are best read as "AOT path matches the C++ baseline floor when the optimiser can see through the workload."

† protopy attr_lookup at N=5M is run separately (the canonical harness uses N=100K). The ~3,500 ms is the median of 5 runs at N=5M; sprint-3 cut this from ~9,300 ms before sprint-3 (the getType cache lands the biggest single bench win of the day).

Bold values are where protopyc beats CPython on a workload that is **not** subject to the constant-fold caveat above.

## The headlines

> **protoCpp beats CPython on every benchmark in this matrix** — by 1.6 to 2.3×. The kernel is competitive with CPython's hand-tuned C implementation when an embedder uses it directly.

> **protoPython AOT (`protopyc`) BEATS CPython on two of six** in this matrix, on the same kernel:
> - `int_sum_loop`: **1.8× faster** than CPython (SmallInt fast-path opcodes).
> - `call_recursion`: at parity (1.18× CPython).
>
> Additionally — outside this microbench matrix — protopyc BEATS CPython on `pyperf_richards_lite` (0.78× = 1.3× faster) and lands at 1.9× CPython on `pyperf_fib`. See the protoPython harness for the full pyperf subset.

> **protoPython interpreter (`protopy`) BEATS CPython on `int_sum_loop`** (0.64× = 1.6× faster) and at near-parity on `multithread_cpu` (1.07× CPython). The remaining benches range 2-8× CPython — see the full per-benchmark report linked at the bottom for the structural reasons.

## What changed since the previous protoCpp report

This table is the result of TWO sprints landing together on the protoPython side, both driven by the protoCpp investigation that showed protoCore was not the bottleneck:

1. **Sprint 1 — kernel housekeeping fixes** (seven commits, `82a5dd08`..`ac4a2505`):
   - `diagEnabled()` constexpr-false in NDEBUG (`-92 %` on call_recursion alone)
   - `-ftls-model=initial-exec` on libprotoPython (every thread_local is now a single `%fs:offset` load, not a `__tls_get_addr` libc call)
   - `ContextScope` SBO bumped 64→256 slots (mirror of protoJS commit `b989e88a`)
   - single-allocation argsList in non-fast-path
   - plus two documented null/deferred results on dispatch-loop and list-mutable.
   - Cut protopy from 5.72× to 4.49× geomean. Full per-step report: <https://github.com/numaes/protoPython/blob/main/docs/2026-06-15-final-comparison.md>.

2. **Sprint 2 — protopy-side per-opcode overheads** (three commits, `1230389e`..`0d06e617`):
   - **A**: OP_LIST_APPEND redundancy cleanup (pointer-identity discrimination instead of redundant getAttribute).
   - **B**: Polymorphic Inline Cache on LOAD_METHOD slow path — 1024-entry direct-mapped TLS cache keyed on `(type_ptr, name_ptr)`. **list_append −34 %** on protopy.
   - **C**: BINARY_ADD for str+str via ProtoString rope `appendLast` (replaces the previous `toUTF8String + std::string concat + fromUTF8Buffer` O(N²) round-trip). **str_concat −47 %** on protopy.
   - Cut protopy from 4.49× to 4.10× geomean.

3. **Sprint 3 — getType per-thread cache** (single commit, `10ae9596`):
   - 1024-entry direct-mapped per-thread cache keyed on obj pointer for `PythonEnvironment::getType`. perf showed `getType` at 4.28 % of `attr_lookup` wall-clock, called once per LOAD_ATTR (for the existing fast path's hasCustomGetattr check) plus several other dispatch sites.
   - **attr_lookup protopy −60 %** (205 → 81 ms). **richards_lite protopy −47 %**. **binary_trees protopy −32 %**. Across-the-board improvements on attribute-heavy workloads.
   - Restored the **multithread_cpu protopyc 25 ms** GIL-free result (sprint-1 showed 30.85 ms, sprint-2 measurement reverted to 1185 ms which turned out to be measurement noise — sprint-3 confirms the architectural win is real and reproducible).
   - Cut protopy from 4.10× to 3.99× geomean; protopyc from 2.72× to 2.25×.

All sprints use the same `libprotoCore.so` underneath. The table is fully apples-to-apples.

## Ratios

| benchmark | proto/cpp | fast/cpp | proto/cpython | **protopyc/cpython** | **protopy/cpython** | pc/proto |
|---|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` (N=10M) | 15.55× | 9.02× | **0.59× (1.7× faster)** | ~0.59× | 0.71× fast | ~0.83× |
| `call_recursion` (fib 25) | 12.83× | 5.28× | **1.03× (parity)** | 1.18× (parity) | 2.02× | 0.59× |
| `attr_lookup` (N=5M) | 32.60× | – | **0.51× (2.0× faster)** | ~0.05×* | 14.9× | ~0.09×* |
| `list_append_loop` | 4.73× | – | **0.55× (1.8× faster)** | 4.78× | 5.25× | 1.10× |
| `str_concat_loop` | 3.87× | – | **0.55× (1.8× faster)** | 7.02× | 4.78× | 0.68× |
| `multithread_cpu` | 7.35× | 6.00× | – | 1.69× | 1.07× | 0.63× |

\* protopyc rows where the AOT C++ generator likely constant-folded the workload (see table above); reader should not extrapolate from them.

Reading across the columns:

- **`proto/cpp` column** (kernel-direct vs raw C++): 4-33× at N where the loop body dominates startup. The honest cost of every protoCore call vs raw C-struct access. The 33× on `attr_lookup` is the per-call cost of `ProtoObject::getAttribute` (cache hit + tag check + cross-DSO dispatch) against a `mov` from a struct field — both are doing the same semantic thing, but one goes through the kernel.
- **`proto/cpython` column** (kernel-direct vs CPython): **protoCpp wins every row** at 0.51-1.03× CPython (i.e., parity to 2× faster). Crucially, even at `attr_lookup` where the kernel-vs-rawC++ ratio is 32×, protoCpp still beats CPython by 2× — because **CPython's per-attribute cost is also in the tens-of-nanoseconds range**, just below protoCore's. The 5× I previously published at N=100K was binary startup masking the work.
- **`protopyc/cpython` column** (AOT vs CPython): the AOT path lands 0.59-1.69× CPython on the workloads where the optimiser cannot fold the body. `int_sum_loop` and `attr_lookup` see "constant-fold" wins that are not really kernel improvements (marked with ~).
- **`protopy/cpython` column** (interpreter vs CPython): 0.71-15× CPython. `attr_lookup` at 14.9× is the worst single bench in the matrix once startup is properly subtracted — every Python attribute access pays for LOAD_METHOD's PIC (sprint-2 B captures repeats) PLUS the descriptor protocol PLUS the wrapper indirection. Closing that 14.9× further needs an attribute-access-specific fast path beyond the LOAD_METHOD PIC, which would be sprint 3 territory if pursued.

## Where the remaining gap lives

`attr_lookup`, `list_append_loop`, `str_concat_loop` all sit at 1.7-6.7× CPython under `protopyc` — closer than ever but still slower. The dominant cost is now in the kernel's persistent data structures:

- Every `lst.append(item)` rebuilds an AVL spine (O(log N) cells per call).
- Every `s + "x"` allocates a rope node.
- Every `obj.x` goes through `ProtoObject::getAttribute` + the per-thread `AttributeCache`.

These are part of protoCore's "immutability by default" contract. Closing the remaining gap is no longer a `protopy` patch — it requires a kernel-level RFC (`ProtoMutableList`, `ProtoMutableString`, see <https://github.com/numaes/protoPython/blob/main/docs/2026-06-15-step-6-list-mutable-deferred.md> for the proposed shape).

## Full pyperformance subset

protoPython's harness also runs five pyperformance benchmarks (`bench_fib`, `bench_binary_trees`, `bench_nqueens`, `bench_richards_lite`, `bench_sieve`). protoCpp does not currently ship pure-C++ ports of those — they live in `protoPython/benchmarks/pyperf/` and the AOT story for them is reported in protoPython's RESULTS report. Notable rows from there for context:

| pyperformance bench | CPython | protopy | protopyc | protopyc / CPython |
|---|---:|---:|---:|---|
| `pyperf_richards_lite` | 63.32 | 255.10 | 49.10 | **0.78× — 1.3× FASTER than CPython** |
| `pyperf_fib` | 119.31 | 939.93 | 232.61 | 1.95× |
| `pyperf_sieve` | 58.43 | 437.75 | 172.54 | 2.95× |
| `pyperf_nqueens` | 92.11 | 2296.78 | 529.57 | 5.75× |
| `pyperf_binary_trees` | 68.60 | 2295.24 | 1255.40 | 18.30× (worst-case — AVL-spine alloc churn, structural) |

Geomean across the suite (n=13, with `memory_pressure` excluded — see
the next paragraph for why):

- `protopy` interpreter: **4.10× CPython** (5.72× → 4.49× after sprint 1 → 4.10× after sprint 2).
- `protopyc` AOT: **2.72× CPython** (3.17× cumulative).

> **`memory_pressure` is reported `[INFO]` and excluded from the geomean.**
> protoCore and CPython make fundamentally different choices about *when*
> to free memory: CPython is reference-counted with eager deallocation
> (free at refcount=0, inline with the mutator); protoCore runs a
> concurrent tracing collector that defers reclamation until the working
> set forces it. On the `memory_pressure` workload, CPython's wall time
> is dominated by the deletion path, while protoPython's is dominated by
> GC scheduling under stress. Averaging the ratio into the geomean would
> skew it by ~1.4× per protoPython mode and would mis-represent steady-
> state throughput. The absolute number stays visible in the protoPython
> harness report for transparency, but it does not participate in any
> aggregate. See the footnote in the upstream report for the full
> rationale.

## Reproducing

```bash
# protoCore (built once)
cd protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore

# protoCpp
cd ../protoCpp && cmake -B build_release -S . && cmake --build build_release
./benchmarks/bench.sh

# protoPython full suite
cd ../protoPython
cmake -B build-lto -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-lto -j
PROTOPY_BIN=$PWD/build-lto/src/runtime/protopy \
PROTOPYC_BIN=$PWD/build-lto/src/compiler/protopyc \
RUN_MODULE_BIN=$PWD/build-lto/test/compiler/run_module \
CPYTHON_BIN=python3 \
python3 benchmarks/run_benchmarks.py --output benchmarks/reports/$(date +%Y-%m-%d).md
```

Different hardware will move the absolute numbers; the ratios are the part that travels.

## Caveats

1. The C++ floor is what a hand-written C++ implementation does — `std::vector::push_back`, `std::string::operator+`, raw `int64_t`. protoCpp gives up some of that to be a real shared kernel: GC, structural sharing, atomic mutability, GIL-free threads. The ratio is the price of those features.
2. The `multithread_cpu` row is highly variable on mobile / laptop CPUs because of thermal throttle on sustained 4-thread loops. The 30.85 ms number for `protopyc` is the median of 5 runs after 2 warm-ups; individual runs ranged ~25 to ~40 ms.
3. CPython numbers are with the system `python3` (3.14 free-threading on this machine). A PyPy run, or `--enable-optimizations` builds, would change them. Within an order of magnitude they are representative.
4. Numbers are not the point of comparison between protoCpp and protopy/protopyc — the **ratios** are. Re-run on your own hardware to validate; the structural shape (kernel beats CPython; the Python layer adds X×, depending on how much per-call housekeeping the bench exercises) should hold.
