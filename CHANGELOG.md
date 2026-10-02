# Changelog

All notable changes to protoCpp are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed — protoCore 2.9.4 in CI, 2026-10-02

- **protoCore pinned to 2.9.4.** Both workflows build protoCore tag `v2.9.4`
  (`9cb0ef54`) instead of the untagged 2.9.0 commit `21889c91`. protoCpp
  declares no minimum protoCore version, so there is no floor job.

### Changed — verified results, protoCore 2.9.0, 2026-10-02

- **Every program's result is verified.** `ctest` runs each example and
  benchmark and compares its output byte-for-byte with
  `tests/expected/<program>.out` (closed-form values); before, CI only
  checked that each program exited 0, and skipped the multithread
  benchmarks. A self-test requires the checker to reject a wrong value.
  `benchmarks/bench.sh` checks each binary's result before timing it and
  exits 1 on a wrong one.
- **protoCore pinned to 2.9.0.** Both workflows build protoCore commit
  `21889c91` (2.9.0 on master; no tag yet); `ci.yml` used unpinned master
  before. On Windows, protoCore 2.9.0's DLL is `protoCore-3.dll`; protoCpp
  copies and installs the file the imported target names. Windows now
  requires the installed protoCore package: the bare `../protoCore`
  build-tree fallback, which looked for a fixed `protoCore.dll`, is not used
  there.
- Compile options are set on protoCpp's own programs instead of the whole
  directory (MSVC `/W3`, no warnings), and CI builds with warnings as errors.

### Added — Windows (MSVC), 2026-10-01

- **Native Windows build.** Every example and benchmark builds with Visual
  Studio 2022 (MSVC 19.44) against an installed protoCore and prints the same
  bytes as on Linux; `cmake --install` gives a `bin\` with the programs and
  `protoCore.dll`. Every Windows difference is behind `WIN32` / `MSVC` /
  `_MSC_VER`; on Linux the compile and link commands are unchanged. See
  README.md, "Windows (MSVC)".
- **Installed protoCore package.** protoCore is also found as a CMake package
  (`protoCore::protoCore`) when its prefix is named with
  `-DCMAKE_PREFIX_PATH`, `-DprotoCore_DIR` or the `CMAKE_PREFIX_PATH`
  environment variable. `-DPROTO_CORE_PREFIX` and the sibling `../protoCore`
  keep working as before on Linux and macOS.
- `.gitattributes` keeps `*.sh` LF, so `benchmarks/bench.sh` runs from a
  `core.autocrlf=true` checkout; `bench.sh` uses `<build>/bin` when present.

### Changed

- Sizes from protoCore's `getSize()` print with `PROTO_FMT_U` and `size_t`
  with `%zu` (the same `%lu` on Linux and macOS); the C++ floors' optimisation
  barrier is the `PROTOCPP_OPAQUE` macro (`benchmarks/cpp/opaque.h`), which is
  the same `asm volatile` statement under GCC and Clang.
