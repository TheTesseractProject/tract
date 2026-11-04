# End-to-end test driver.
#
# Required variables:
#   TERRA     - path to the terra compiler binary
#   COMPILER  - C compiler used to build the generated code
#   SOURCE    - Tesseract source file to compile
#   EXPECT    - substring expected in the program output (ignored when
#               EXPECT_PANIC is set)
#   WORK      - scratch directory for generated artifacts
#
# Optional variables:
#   SAFE_MODE    - pass --safe to terra
#   EXPECT_PANIC - require the program to fail with a "runtime error" message

foreach(var TERRA COMPILER SOURCE WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "e2e.cmake: missing required variable ${var}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK}")
set(generated "${WORK}/program.c")
set(binary "${WORK}/program")

set(terra_args "${SOURCE}" -o "${generated}")
if(SAFE_MODE)
    list(APPEND terra_args --safe)
endif()

execute_process(
    COMMAND "${TERRA}" ${terra_args}
    RESULT_VARIABLE terra_result
    OUTPUT_VARIABLE terra_stdout
    ERROR_VARIABLE terra_stderr
)
if(NOT terra_result EQUAL 0)
    message(FATAL_ERROR
        "terra failed (${terra_result})\nstdout:\n${terra_stdout}\nstderr:\n${terra_stderr}")
endif()
if(NOT EXISTS "${generated}")
    message(FATAL_ERROR "terra did not produce ${generated}")
endif()

# Generated code must be warning-free, not merely compilable: invalid or
# sloppy output (unused helpers, implicit conversions, ...) fails the suite.
execute_process(
    COMMAND "${COMPILER}" -std=c99 -Wall -Wextra -Werror
            "${generated}" -o "${binary}"
    RESULT_VARIABLE cc_result
    OUTPUT_VARIABLE cc_stdout
    ERROR_VARIABLE cc_stderr
)
if(NOT cc_result EQUAL 0)
    message(FATAL_ERROR
        "generated C failed to compile (${cc_result})\nstdout:\n${cc_stdout}\nstderr:\n${cc_stderr}")
endif()

if(DEFINED EXPECT_HEX)
    set(stdout_file "${WORK}/stdout.bin")
    execute_process(
        COMMAND "${binary}"
        RESULT_VARIABLE run_result
        OUTPUT_FILE "${stdout_file}"
        ERROR_VARIABLE run_stderr
    )
    file(READ "${stdout_file}" actual_hex HEX)
    if(NOT actual_hex STREQUAL EXPECT_HEX)
        message(FATAL_ERROR
            "program output bytes mismatch: expected ${EXPECT_HEX}, got ${actual_hex}\nstderr:\n${run_stderr}")
    endif()
else()
    execute_process(
        COMMAND "${binary}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
endif()

if(EXPECT_PANIC)
    if(run_result EQUAL 0)
        message(FATAL_ERROR
            "program was expected to panic but succeeded\nstdout:\n${run_stdout}")
    endif()
    string(FIND "${run_stderr}" "runtime error" panic_pos)
    if(panic_pos EQUAL -1)
        message(FATAL_ERROR
            "program failed without a runtime error message\nstderr:\n${run_stderr}")
    endif()
    return()
endif()

if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "generated program failed (${run_result})\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()

if(NOT DEFINED EXPECT_HEX)
    string(FIND "${run_stdout}" "${EXPECT}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "program output did not contain '${EXPECT}'\nactual output:\n${run_stdout}")
    endif()
endif()

# Optional exact byte count, for outputs that embed NUL bytes which the
# substring check above cannot represent.
if(DEFINED EXPECT_LEN)
    string(LENGTH "${run_stdout}" actual_len)
    if(NOT actual_len EQUAL EXPECT_LEN)
        message(FATAL_ERROR
            "program output length mismatch: expected ${EXPECT_LEN}, got ${actual_len}\nactual output:\n${run_stdout}")
    endif()
endif()
