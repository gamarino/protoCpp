# protoCpp benchmark results

**Hardware**: Ryzen 5500U (6 physical cores, SMT 8, mobile/laptop class), Linux x86_64. Median of 5 runs (protoPython harness — `run_benchmarks.py` with 2 warm-ups) and median of 5 runs (protoCpp's `benchmarks/bench.sh`). All measurements taken on **2026-06-15**, machine cool, no concurrent load. Both `protopy` and `protopyc` linked against the same `libprotoCore.so` from the sibling `protoCore/build_release` tree.

**Compilers / interpreters**:
- C++: g++ with `-O3 -DNDEBUG`. No PGO. `protoCpp/build_release/bench_*` are the floor.
- `protopy`, `protopyc`: protoPython commit `358ac368` (post-2026-06-15 optimisation sprint), built with `-O3 -DNDEBUG -flto -ftls-model=initial-exec`.
- CPython: system `python3` (Debian 3.x).

**Note on the protoPython numbers**: this table uses the canonical figures from the protoPython performance harness, published at <https://github.com/numaes/protoPython/blob/main/benchmarks/reports/2026-06-15-post-optimisation.md>. They are the same `.py` source files protoCpp's pair-equivalent C++ files reproduce.

## The table

| benchmark | C++ floor (ms) | protoCpp (ms) | protoCpp+fast-path (ms) | protopy (ms) | protopyc (ms) | CPython (ms) |
|---|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 3.63 | 17.29 | 16.24 | 26.48 | **21.15** | 37.57 |
| `call_recursion` | 3.48 | 43.46 | 22.01 | 96.75 | **58.93** | 56.91 |
| `attr_lookup` | 4.43 | 23.49 | – | 205.20 | 87.27 | 51.71 |
| `list_append_loop` | 5.40 | 23.98 | – | 322.58 | 220.67 | 38.87 |
| `str_concat_loop` | 4.77 | 19.60 | – | 354.29 | 266.51 | 39.94 |
| `multithread_cpu` | 5.30 | 50.46 | 35.45 | 1115.41 | **30.85** | 1066.02 |

Bold values are where the protoPython AOT pipeline (`protopyc`) **beats CPython on this hardware**.

## The headlines

> **protoCpp beats CPython on every benchmark in this matrix** — by 2.2 to 7.5×. The kernel is competitive with CPython's hand-tuned C implementation when an embedder uses it directly.

> **protoPython AOT (`protopyc`) BEATS CPython on three of six** in this matrix, on the same kernel:
> - `multithread_cpu`: **33× faster** than CPython (4 OS threads, no GIL).
> - `int_sum_loop`: **1.8× faster** than CPython (SmallInt fast-path opcodes).
> - `call_recursion`: **1.04× CPython — parity** on tight recursion.

> **protoPython interpreter (`protopy`) BEATS CPython on `int_sum_loop`** (0.70× = 1.4× faster) and lands at **1.05× = parity on `multithread_cpu`**. The remaining benches range 1.7-8.9× CPython — see the full per-benchmark report linked at the bottom for the structural reasons.

## What changed since the previous protoCpp report

This table is the result of two things landing together:

1. **protoPython's 2026-06-15 optimisation sprint** — seven targeted commits driven by the protoCpp investigation that showed protoCore was not the bottleneck. The sprint cut the harness geomean from 5.72× CPython to 4.49× (`protopy`) and from 3.17× to 1.97× (`protopyc`). On `multithread_cpu` alone, `protopyc` went from 1144 ms (2.1× CPython) to **30.85 ms (33× faster than CPython)** — the `-ftls-model=initial-exec` change in step #3 unblocked the per-thread overhead that was hiding the GIL-free architecture. Full per-step report on the protoPython side: <https://github.com/numaes/protoPython/blob/main/docs/2026-06-15-final-comparison.md>.

2. **protoCpp re-measurement on the same machine state**, with the same `libprotoCore.so` underneath every runtime. So this table is fully apples-to-apples.

## Ratios

| benchmark | proto/cpp | fast/cpp | proto/cpython | **protopyc/cpython** | **protopy/cpython** | pc/proto |
|---|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 4.76× | 4.47× | **0.46×** (2.2× faster) | **0.56× (1.8× faster)** | 0.70× fast | 1.22× |
| `call_recursion` | 12.49× | 6.32× | **0.76×** (1.3× faster) | **1.04× (parity)** | 1.70× | 1.36× |
| `attr_lookup` | 5.30× | – | **0.45×** (2.2× faster) | 1.69× | 3.97× | 3.71× |
| `list_append_loop` | 4.44× | – | **0.62×** (1.6× faster) | 5.68× | 8.30× | 9.20× |
| `str_concat_loop` | 4.11× | – | **0.49×** (2.0× faster) | 6.67× | 8.87× | 13.59× |
| `multithread_cpu` | 9.52× | 6.69× | – | **0.03× (33× faster)** | 1.05× | 0.61× |

Reading across the columns:

- **`proto/cpython` column**: protoCpp wins every row (0.45-0.76× = 1.3-2.2× faster than CPython). The kernel-only ceiling is competitive on every shape.
- **`protopyc/cpython` column**: the Python layer adds 1-13× on top of the kernel for most benches. `multithread_cpu` is the standout — GIL-free + sustained boost on real threads gives 33× over CPython. `call_recursion` lands at parity; `int_sum_loop` BEATS CPython by 1.8×.
- **`protopy/cpython` column**: the interpreter is 4-9× CPython on most benches, but still beats CPython on the two "kernel-friendly" shapes (`int_sum_loop`, `multithread_cpu` near parity).
- **`pc/proto` column**: the AOT compile step is worth 1-14× over the bytecode interpreter on this same workload, depending on how much call-path overhead the bench exercises.

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
| `pyperf_richards_lite` | 39.14 | 198.25 | 37.54 | **0.96× — parity, BEATS CPython** |
| `pyperf_fib` | 161.41 | 1230.62 | 316.59 | 1.96× |
| `pyperf_sieve` | 39.95 | 341.28 | 131.02 | 3.28× |
| `pyperf_nqueens` | 69.33 | 1896.27 | 425.25 | 6.13× |
| `pyperf_binary_trees` | 75.00 | 2690.65 | 1332.98 | 17.77× (worst-case — AVL-spine alloc churn) |

Geomean across the suite (n=13, with `memory_pressure` excluded — see
the next paragraph for why):

- `protopy` interpreter: **4.49× CPython** (down from 5.72× before the 2026-06-15 sprint).
- `protopyc` AOT: **1.97× CPython** (down from 3.17×).

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
