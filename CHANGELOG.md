# Changelog

All notable changes to protoCpp are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
