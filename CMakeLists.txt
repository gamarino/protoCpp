cmake_minimum_required(VERSION 3.20)
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

# Locate protoCore. Either point at an installed prefix via PROTO_CORE_PREFIX,
# or rely on the sibling source tree at ../protoCore with one of its build_*
# directories populated. Same pattern protoClojure / protoST use.
if(DEFINED PROTO_CORE_PREFIX)
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
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()

include_directories(${PROTOCORE_INCLUDE_DIRS})

# -O3 + no asserts for the bench numbers. -Wall to catch obvious mistakes.
add_compile_options(-Wall -Wextra -Wpedantic -O3 -DNDEBUG)

# --- Examples ----------------------------------------------------------------
# Each example is a single .cpp file that produces one executable. Adding a
# new file under examples/ automatically picks it up — see the glob.
file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
foreach(src ${EXAMPLE_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(example_${name} ${src})
    target_link_libraries(example_${name} PRIVATE ${PROTOCORE_LIBRARY})
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
    add_executable(bench_cpp_${name} ${src})
    # Pure C++ benches don't link protoCore (that's the whole point —
    # they're the floor).
endforeach()

file(GLOB PROTO_BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/proto/*.cpp")
foreach(src ${PROTO_BENCH_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(bench_proto_${name} ${src})
    target_link_libraries(bench_proto_${name} PRIVATE ${PROTOCORE_LIBRARY})
endforeach()

message(STATUS "protoCpp ${PROJECT_VERSION}")
message(STATUS "  protoCore lib: ${PROTOCORE_LIBRARY}")
message(STATUS "  protoCore inc: ${PROTOCORE_INCLUDE_DIRS}")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
