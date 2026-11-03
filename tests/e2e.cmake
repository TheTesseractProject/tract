# End-to-end test driver.
#
# Required variables:
#   TERRA     - path to the terra compiler binary
#   COMPILER  - C compiler used to build the generated code
#   SOURCE    - Tesseract source file to compile
#   EXPECT    - substring expected in the program output
#   WORK      - scratch directory for generated artifacts

foreach(var TERRA COMPILER SOURCE EXPECT WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "e2e.cmake: missing required variable ${var}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK}")
set(generated "${WORK}/program.c")
set(binary "${WORK}/program")

execute_process(
    COMMAND "${TERRA}" "${SOURCE}" -o "${generated}"
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

execute_process(
    COMMAND "${binary}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "generated program failed (${run_result})\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()

string(FIND "${run_stdout}" "${EXPECT}" found)
if(found EQUAL -1)
    message(FATAL_ERROR
        "program output did not contain '${EXPECT}'\nactual output:\n${run_stdout}")
endif()
