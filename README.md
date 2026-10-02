# protoCpp

**The protoCore ceiling, in C++.**

protoCpp uses [protoCore](https://github.com/numaes/protoCore) directly: plain C++20 drives the kernel through its public header. There is no interpreter, no bytecode loop and no symbol-table dispatch. The language runtimes built on protoCore ([protoJS](https://github.com/gamarino/protoJS), [protoPython](https://github.com/gamarino/protoPython), [protoST](https://github.com/gamarino/protoST), [protoClojure](https://github.com/gamarino/protoClojure), [protoScala](https://github.com/gamarino/protoScala)) each add a language layer on top of the kernel. protoCpp removes that layer, so its benchmark numbers exclude any language layer.

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

protoCpp expects [protoCore](https://github.com/numaes/protoCore) at `../protoCore`, or at the prefix given with `-DPROTO_CORE_PREFIX=<path>`. An installed protoCore CMake package (`lib/cmake/protoCore`) is used when its prefix is named with `-DCMAKE_PREFIX_PATH=<prefix>`; system prefixes are not searched, so a configure that names no prefix keeps using `../protoCore`. Build the kernel first, then this repository:

```bash
cd ../protoCore
cmake -B build_release -S . && cmake --build build_release --target protoCore

cd ../protoCpp
cmake -B build_release -S . && cmake --build build_release
```

### Windows (MSVC)

protoCpp builds and runs natively on Windows with Visual Studio 2022 (MSVC
19.44 verified, Windows 11), using the CMake and Ninja that ship with it. Build
protoCore first (its `docs/INSTALLATION.md`, "Windows (MSVC)") and install it
into a prefix. From an "x64 Native Tools Command Prompt":

```bat
set PREFIX=%LOCALAPPDATA%\Programs\proto
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%PREFIX%
cmake --build build
build\bin\example_01_hello
cmake --install build --prefix C:\path\to\protocpp
```

`-DPROTO_CORE_PREFIX=%PREFIX%` works too; on Windows that prefix is read as a
CMake package. Windows always needs the installed package: a bare
`..\protoCore` build tree is not used there, because only the package's
imported target knows the DLL's file name. The programs go to `build\bin\`
together with a copy of protoCore's DLL, so they run in place; `cmake
--install` puts them and the DLL in `<prefix>\bin`, which runs with nothing
else on `PATH`. The DLL is whatever file the imported target
`protoCore::protoCore` names (`$<TARGET_FILE>`), never a fixed name: from
protoCore 2.9.0 it is `protoCore-3.dll`, the ABI (SOVERSION 3) in the file
name, where earlier versions built `protoCore.dll`.
`benchmarks/bench.sh build` runs the benchmark table from Git Bash (it uses
`build/bin` when present).

How Windows differs, by design:

- **Same output bytes everywhere.** Every program is built with
  `windows/stdio_setup.cpp`, which puts the standard streams in binary mode
  (`\n`, not `\r\n`) and switches the console to UTF-8, and with
  `windows/utf8.manifest`, which makes UTF-8 the process code page. The
  example and benchmark sources need no I/O setup of their own. Sizes
  returned by protoCore (`proto::proto_ulong`, `unsigned long long` on Windows)
  are printed with `PROTO_FMT_U`.
- **Compiler flags.** protoCpp's own programs get `/W3 /utf-8` from MSVC
  and, outside Debug, `/O2 /DNDEBUG`, in place of `-Wall -Wextra -Wpedantic
  -O3 -DNDEBUG`. Warning level 3 includes the truncation warnings (C4244,
  C4267); the build has none, and CI builds with
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON` so a new one fails. An unset
  `CMAKE_BUILD_TYPE` means Release, as on Linux.
- **The pure C++ floors.** `bench_cpp_attr_lookup` and `bench_cpp_int_sum_loop`
  stop the optimiser from folding their loops with `PROTOCPP_OPAQUE`
  (`benchmarks/cpp/opaque.h`). With GCC and Clang it is the original empty
  `asm volatile` barrier; MSVC x64 has no inline assembly, so there it is a
  volatile store and load per use, which makes those two floors somewhat
  slower. Compare Windows numbers with Windows numbers only; `RESULTS.md`
  was measured on Linux.

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

Each row pairs `bench_cpp_<name>` (pure C++ floor) with `bench_proto_<name>` (protoCpp), plus `bench_proto_fast_<name>` where a fast-path variant exists. The script prints the median times and the protoCpp / C++ ratios. Before timing a row it runs each binary once and compares what it prints with `tests/expected/<binary>.out`; a binary that prints a wrong result is reported as `** wrong result, not timed **` and the script exits 1.

## Tests

Every example and benchmark prints the result of the work it did: the list it built, a sum, `fib(25)`, an element count. `ctest` runs each program once and compares its standard output byte-for-byte with `tests/expected/<program>.out` (`tests/check_output.cmake`), so a program that exits 0 without doing its work, or computes a wrong value, fails. The expected values are closed-form results, identical on Linux, macOS and Windows. A new program needs its `tests/expected/<program>.out`, or configure fails.

```bash
ctest --test-dir build_release --output-on-failure
```

## Continuous integration

- `ci.yml` (Linux, GCC) builds protoCore from source next to protoCpp, checks with `ldd` that the programs load that build, and runs `ctest`.
- `cross-platform.yml` (macOS with Apple clang, Windows with MSVC) installs protoCore into a prefix, builds protoCpp against the package and runs `ctest` without the prefix on `PATH`. On Windows it also runs the installed `bin\` from a clean directory with only system directories on `PATH`.

Both build protoCpp with warnings as errors and use the same pinned protoCore commit, `PROTOCORE_REF` in each workflow: currently `21889c91`, protoCore 2.9.0 on master (2.9.0 has no tag). Bump it in both files together, to a tag's commit when one exists, and re-run both workflows.

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
├── tests/
│   ├── check_output.cmake  # runs one program, compares its output
│   ├── expected/           # <program>.out: the exact result each prints
│   └── selftest/           # a wrong expectation the checker must reject
└── benchmarks/
    ├── bench.sh
    ├── cpp/        # pure C++ floor, no protoCore link
    ├── proto/      # protoCpp, same work through the kernel API
    └── proto_fast/ # protoCpp with the embedder-side SmallInt fast path
                    #   (int_sum_loop, call_recursion, multithread_cpu)
```

## Related projects

Five language runtimes (protoJS, protoPython, protoST, protoClojure, protoScala) and protoCpp's C++ examples are built on protoCore.

| Project | Role | Repository |
|---|---|---|
| protoCore | C++20 object model and runtime kernel: immutable structures, concurrent GC, GIL-free threads | https://github.com/numaes/protoCore |
| protoJS | JavaScript runtime on protoCore | https://github.com/gamarino/protoJS |
| protoPython | Python 3 runtime (protopy) and ahead-of-time compiler (protopyc) on protoCore | https://github.com/gamarino/protoPython |
| protoST | Smalltalk-inspired actor language on protoCore | https://github.com/gamarino/protoST |
| protoClojure | Clojure dialect on protoCore (early stage) | https://github.com/gamarino/protoClojure |
| protoScala | Dynamic Scala 3 dialect on protoCore (early stage) | https://github.com/gamarino/protoScala |
| protoCpp | Examples and benchmarks using protoCore directly from C++ | https://github.com/gamarino/protoCpp |
| protoIO | Shared input and output for the runtimes: files, processes, TCP, UDP, TLS and HTTP/1.1 (used by protoST, protoScala and protoClojure) | https://github.com/gamarino/protoIO |

## License

Copyright (c) 2026 Gustavo Marino. Released under the MIT License; see [LICENSE](LICENSE).
