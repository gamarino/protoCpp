cmake_minimum_required(VERSION 3.20)

# On Windows, CMake's MSVC platform files default an unset CMAKE_BUILD_TYPE
# to Debug during project(), which would hide it from the Release default
# below: give it before project() there.
if(CMAKE_HOST_WIN32 AND NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

project(protoCpp VERSION 0.1.0 LANGUAGES CXX)

# Default to Release so casual builds are -O3. Override with
# -DCMAKE_BUILD_TYPE=Debug for diagnosis runs.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    # Windows has no rpath: a program finds its DLLs next to itself, so every
    # executable of the build goes to one directory, together with a copy of
    # protoCore's DLL (see protocpp_runtime_dlls below).
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
endif()

# Locate protoCore, in this order:
#   1. An installed prefix named with -DPROTO_CORE_PREFIX=<prefix> (the
#      library and protoCore.h are looked up directly; on Windows the prefix
#      is read as a CMake package, see 2).
#   2. An installed protoCore CMake package (lib/cmake/protoCore), found
#      through a prefix the caller names explicitly: -DCMAKE_PREFIX_PATH,
#      -DprotoCore_DIR or the CMAKE_PREFIX_PATH environment variable. System
#      prefixes are not searched, so a configure that names no prefix keeps
#      using 3.
#   3. The sibling source tree at ../protoCore with one of its build_*
#      directories populated. Same pattern protoClojure / protoST use.
if(WIN32 AND DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()
if(DEFINED PROTO_CORE_PREFIX AND NOT WIN32)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES lib lib64)
    find_path(PROTOCORE_INCLUDE_DIR NAMES protoCore.h
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES include headers)
    if(NOT (PROTOCORE_LIBRARY AND PROTOCORE_INCLUDE_DIR))
        message(FATAL_ERROR
            "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but protoCore not found.")
    endif()
    set(PROTOCORE_INCLUDE_DIRS "${PROTOCORE_INCLUDE_DIR}")
else()
    find_package(protoCore CONFIG QUIET
        NO_CMAKE_PACKAGE_REGISTRY NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
        NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    if(protoCore_FOUND)
        # The imported target carries protoCore's include directory, its
        # compile definitions and its thread library.
        set(PROTOCORE_LIBRARY protoCore::protoCore)
        set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    elseif(DEFINED PROTO_CORE_PREFIX)
        message(FATAL_ERROR
            "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but no protoCore CMake "
            "package (lib/cmake/protoCore) was found there.")
    elseif(WIN32)
        # The DLL's file name carries protoCore's ABI version (protoCore-3.dll
        # since 2.9.0) and only the package's imported target knows it, so
        # Windows does not use a bare build tree.
        message(FATAL_ERROR
            "protoCore CMake package not found. On Windows, install protoCore "
            "(cmake --install <protoCore build> --prefix <prefix>) and configure "
            "with -DCMAKE_PREFIX_PATH=<prefix>.")
    else()
        set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
        find_library(PROTOCORE_LIBRARY NAMES protoCore
            PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
            NO_DEFAULT_PATH)
        if(NOT PROTOCORE_LIBRARY)
            message(FATAL_ERROR
                "protoCore shared library not found. Build it first:\n"
                "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore\n"
                "or point at an installed protoCore with -DCMAKE_PREFIX_PATH=<prefix>.")
        endif()
        set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
        get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
        set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
        set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
    endif()
endif()

include_directories(${PROTOCORE_INCLUDE_DIRS})

# -O3 + no asserts for the bench numbers. -Wall to catch obvious mistakes.
# These options go on protoCpp's own programs only (protocpp_add_program).
if(MSVC)
    # Same intent under MSVC: optimise, no asserts, warning level 3 (which
    # includes the truncation warnings C4244 and C4267). Sources are UTF-8. A
    # Debug build keeps MSVC's /Od /RTC1 (/O2 and /RTC1 cannot be combined).
    set(PROTOCPP_COMPILE_OPTIONS /W3 /utf-8 "$<$<NOT:$<CONFIG:Debug>>:/O2;/DNDEBUG>")
else()
    set(PROTOCPP_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -O3 -DNDEBUG)
endif()

# Windows only: every executable gets the UTF-8 process code page (manifest)
# and binary stdio with a UTF-8 console, so its output is byte-for-byte what
# it prints on Linux and macOS ("\n", not "\r\n"). Nothing to add elsewhere.
set(PROTOCPP_PLATFORM_SOURCES "")
if(WIN32)
    set(PROTOCPP_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/utf8.manifest"
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/stdio_setup.cpp")
endif()

# Windows finds a DLL next to the program that loads it: copy protoCore's DLL
# into the build's bin/ with a target every protoCore program depends on. The
# file is the imported target's location ($<TARGET_FILE>), never a hard-coded
# name: protoCore 2.9.0 names it protoCore-<SOVERSION>.dll (protoCore-3.dll).
set(PROTOCPP_RUNTIME_DLLS "")
if(WIN32)
    set(PROTOCPP_RUNTIME_DLLS "$<TARGET_FILE:protoCore::protoCore>")
    add_custom_target(protocpp_runtime_dlls
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROTOCPP_RUNTIME_DLLS}
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        VERBATIM)
endif()

set(PROTOCPP_PROGRAMS "")

# Add one of protoCpp's programs: a single source file plus the platform
# sources, built with protoCpp's compile options.
function(protocpp_add_program target src)
    add_executable(${target} ${src} ${PROTOCPP_PLATFORM_SOURCES})
    target_compile_options(${target} PRIVATE ${PROTOCPP_COMPILE_OPTIONS})
    set(PROTOCPP_PROGRAMS ${PROTOCPP_PROGRAMS} ${target} PARENT_SCOPE)
endfunction()

# Link a program against protoCore (and, on Windows, have protoCore's DLL
# copied next to it before it is built).
function(protocpp_link_protocore target)
    target_link_libraries(${target} PRIVATE ${PROTOCORE_LIBRARY})
    if(TARGET protocpp_runtime_dlls)
        add_dependencies(${target} protocpp_runtime_dlls)
    endif()
endfunction()

# --- Examples ----------------------------------------------------------------
# Each example is a single .cpp file that produces one executable. Adding a
# new file under examples/ automatically picks it up — see the glob.
file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
foreach(src ${EXAMPLE_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    protocpp_add_program(example_${name} ${src})
    protocpp_link_protocore(example_${name})
endforeach()

# --- Benchmarks --------------------------------------------------------------
# Two parallel families:
#   benchmarks/cpp/<name>.cpp   — pure C++ baseline, no protoCore.
#   benchmarks/proto/<name>.cpp — protoCpp version, uses protoCore types.
# Each pair shares a name; the runner pairs them by stem.
file(GLOB CPP_BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/cpp/*.cpp")
foreach(src ${CPP_BENCH_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    protocpp_add_program(bench_cpp_${name} ${src})
    # Pure C++ benches don't link protoCore (that's the whole point —
    # they're the floor).
endforeach()

file(GLOB PROTO_BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/proto/*.cpp")
foreach(src ${PROTO_BENCH_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    protocpp_add_program(bench_proto_${name} ${src})
    protocpp_link_protocore(bench_proto_${name})
endforeach()

# proto_fast — SmallInt fast-path variants. They use the inline
# `proto::isSmallInt` / `proto::asSmallInt` / `proto::makeSmallInt` /
# `proto::smallIntInRange` helpers from protoCore.h to short-circuit
# the cross-DSO call into `ProtoObject::add` etc. Only benches with
# tight numeric inner loops have a fast-path version — list / str /
# attr would not benefit.
file(GLOB PROTO_FAST_BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/proto_fast/*.cpp")
foreach(src ${PROTO_FAST_BENCH_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    protocpp_add_program(bench_proto_fast_${name} ${src})
    protocpp_link_protocore(bench_proto_fast_${name})
endforeach()

# --- Install (Windows) ---------------------------------------------------------
# `cmake --install <build> --prefix <dir>` gives a self-contained bin/: the
# programs and the protoCore DLL they load. Elsewhere the build tree is the
# deliverable, as before (the programs find libprotoCore through the build
# rpath, which an install would strip).
if(WIN32)
    install(TARGETS ${PROTOCPP_PROGRAMS}
        RUNTIME DESTINATION bin COMPONENT protoCpp)
    install(FILES ${PROTOCPP_RUNTIME_DLLS}
        DESTINATION bin COMPONENT protoCpp)
endif()

# --- Smoke tests (ctest) -------------------------------------------------------
# Every example and benchmark prints the result of its work; each test runs one
# program and compares its standard output byte-for-byte with
# tests/expected/<program>.out (tests/check_output.cmake), so a program that
# exits 0 without doing its work, or prints a wrong result, fails. A program
# without an expected-output file is a configure error: nothing is built that
# the smoke test does not verify. The protoCore programs find protoCore through
# the build rpath (Linux, macOS) or the DLL copied next to them (Windows).
enable_testing()
foreach(program ${PROTOCPP_PROGRAMS})
    set(expected "${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${program}.out")
    if(NOT EXISTS "${expected}")
        message(FATAL_ERROR
            "${program} has no expected output: add tests/expected/${program}.out "
            "with the exact result it must print.")
    endif()
    add_test(NAME smoke_${program}
        COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:${program}>"
                "-DEXPECTED=${expected}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_output.cmake")
endforeach()
# The checker itself must reject a wrong result: run it against a deliberately
# wrong expectation (fib(25) is 75025, the file says 75026) and require failure.
add_test(NAME smoke_selftest_rejects_wrong_result
    COMMAND ${CMAKE_COMMAND} "-DPROGRAM=$<TARGET_FILE:bench_cpp_call_recursion>"
            "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/selftest/wrong_result.out"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_output.cmake")
set_tests_properties(smoke_selftest_rejects_wrong_result PROPERTIES WILL_FAIL TRUE)

message(STATUS "protoCpp ${PROJECT_VERSION}")
message(STATUS "  protoCore lib: ${PROTOCORE_LIBRARY}")
message(STATUS "  protoCore inc: ${PROTOCORE_INCLUDE_DIRS}")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
