// stdio_setup.cpp — Windows only (added to every program by CMakeLists.txt).
//
// Makes a program's output byte-for-byte what it prints on Linux and macOS:
// stdin/stdout/stderr in binary mode (no "\n" -> "\r\n" translation) and a
// UTF-8 console. It runs as a static initializer, before main(), so the
// examples and benchmarks themselves stay free of platform code.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>

namespace {

struct StdioSetup {
    StdioSetup() {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
};

const StdioSetup stdioSetup;

}  // namespace

#endif  // _WIN32
