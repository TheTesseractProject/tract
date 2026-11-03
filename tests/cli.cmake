# CLI behaviour tests.
#
# Required variables: TERRA (compiler binary), SOURCE (a valid .t example)

foreach(var TERRA TERRA_VERSION SOURCE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "cli.cmake: missing required variable ${var}")
    endif()
endforeach()

function(run_expect_ok)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "command failed (${result}): ${ARGN}\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    set(LAST_OUT "${out}" PARENT_SCOPE)
endfunction()

# --version / -v
run_expect_ok("${TERRA}" -v)
string(REPLACE "." "[.]" TERRA_VERSION_REGEX "${TERRA_VERSION}")
if(NOT LAST_OUT MATCHES "${TERRA_VERSION_REGEX}")
    message(FATAL_ERROR "version output missing version number:\n${LAST_OUT}")
endif()
if(NOT LAST_OUT MATCHES ",--\\.")
    message(FATAL_ERROR "version output missing Tesseract ending:\n${LAST_OUT}")
endif()

run_expect_ok("${TERRA}" --version)
if(NOT LAST_OUT MATCHES "Tesseract compiler")
    message(FATAL_ERROR "version output missing description:\n${LAST_OUT}")
endif()

# --check succeeds on a valid program
execute_process(COMMAND "${TERRA}" --check "${SOURCE}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_out
    ERROR_VARIABLE check_err)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "--check failed:\n${check_out}\n${check_err}")
endif()

# --stdout emits C on stdout
execute_process(COMMAND "${TERRA}" "${SOURCE}" --stdout --quiet
    RESULT_VARIABLE stdout_result
    OUTPUT_VARIABLE stdout_out
    ERROR_VARIABLE stdout_err)
if(NOT stdout_result EQUAL 0)
    message(FATAL_ERROR "--stdout failed:\n${stdout_out}\n${stdout_err}")
endif()
if(NOT stdout_out MATCHES "int main")
    message(FATAL_ERROR "--stdout did not emit generated C:\n${stdout_out}")
endif()

# Unknown options must fail
execute_process(COMMAND "${TERRA}" --definitely-not-an-option
    RESULT_VARIABLE bad_result)
if(bad_result EQUAL 0)
    message(FATAL_ERROR "unknown option was accepted")
endif()

# Missing option values must fail
execute_process(COMMAND "${TERRA}" "${SOURCE}" -o
    RESULT_VARIABLE missing_result)
if(missing_result EQUAL 0)
    message(FATAL_ERROR "missing -o value was accepted")
endif()

# --check cannot be combined with --stdout
execute_process(COMMAND "${TERRA}" --check "${SOURCE}" --stdout
    RESULT_VARIABLE conflict_result)
if(conflict_result EQUAL 0)
    message(FATAL_ERROR "--check + --stdout conflict was accepted")
endif()
