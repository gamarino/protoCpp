# check_output.cmake — run one program and verify what it printed.
#
#   cmake -DPROGRAM=<exe> -DEXPECTED=<file> -P check_output.cmake
#
# Every example and benchmark prints the result of the work it did (a list,
# a sum, a count). A program that crashes after main() returned, skips its
# loop or prints a wrong value must not pass as "exited 0", so this script
# requires both exit code 0 and standard output byte-for-byte equal to
# <file> (tests/expected/<program>.out). The expected values are closed-form
# results (fib(25) = 75025, sum of 0..N-1, ...), not recordings of a
# particular run, and they are the same on every platform: on Windows the
# programs write "\n", not "\r\n" (windows/stdio_setup.cpp).

if(NOT DEFINED PROGRAM OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "usage: cmake -DPROGRAM=<exe> -DEXPECTED=<file> -P check_output.cmake")
endif()
if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "expected-output file not found: ${EXPECTED}")
endif()

execute_process(
    COMMAND "${PROGRAM}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE stderr_text
    TIMEOUT 120)
file(READ "${EXPECTED}" expected)

if(NOT rc STREQUAL "0")
    message(FATAL_ERROR
        "${PROGRAM} failed (exit status: ${rc})\n"
        "--- stdout ---\n${actual}--- stderr ---\n${stderr_text}")
endif()
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "${PROGRAM} printed an unexpected result\n"
        "--- expected (${EXPECTED}) ---\n${expected}"
        "--- actual ---\n${actual}"
        "--- stderr ---\n${stderr_text}")
endif()
message(STATUS "OK: ${actual}")
