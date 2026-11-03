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

# --- Additional coverage -----------------------------------------------------

get_filename_component(SOURCE_DIR "${SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cli_work")
set(WORK "${CMAKE_CURRENT_BINARY_DIR}/cli_work")

# The accepted extensions (.t, .tes, .tt) all pass --check.
foreach(ext t tes tt)
    string(REPLACE "." "_" ext_var "${ext}")
    set(alt_source "${WORK}/ext_${ext_var}.${ext}")
    configure_file("${SOURCE}" "${alt_source}" COPYONLY)
    execute_process(COMMAND "${TERRA}" --check "${alt_source}"
        RESULT_VARIABLE ext_result
        OUTPUT_VARIABLE ext_out
        ERROR_VARIABLE ext_err)
    if(NOT ext_result EQUAL 0)
        message(FATAL_ERROR "extension .${ext} was rejected:\n${ext_out}\n${ext_err}")
    endif()
endforeach()

# An unknown extension must be rejected.
set(bad_source "${WORK}/bad.xxx")
configure_file("${SOURCE}" "${bad_source}" COPYONLY)
execute_process(COMMAND "${TERRA}" --check "${bad_source}"
    RESULT_VARIABLE bad_ext_result)
if(bad_ext_result EQUAL 0)
    message(FATAL_ERROR "extension .xxx was accepted")
endif()

# A missing file must be rejected.
execute_process(COMMAND "${TERRA}" --check "${WORK}/no_such_file.t"
    RESULT_VARIABLE missing_file_result)
if(missing_file_result EQUAL 0)
    message(FATAL_ERROR "missing file was accepted")
endif()

# --stdout without --quiet must still keep the C clean: progress messages
# belong on stderr, never interleaved with generated code.
execute_process(COMMAND "${TERRA}" "${SOURCE}" --stdout
    RESULT_VARIABLE noisy_result
    OUTPUT_VARIABLE noisy_out
    ERROR_VARIABLE noisy_err)
if(NOT noisy_result EQUAL 0)
    message(FATAL_ERROR "plain --stdout failed:\n${noisy_out}\n${noisy_err}")
endif()
string(FIND "${noisy_out}" "/* Terra Runtime Library */" runtime_prelude_pos)
if(NOT runtime_prelude_pos EQUAL 0)
    message(FATAL_ERROR "--stdout output starts with progress text, not C:\n${noisy_out}")
endif()
if(noisy_err STREQUAL "")
    message(FATAL_ERROR "--stdout progress messages missing on stderr")
endif()

# -o - writes the generated C to stdout.
execute_process(COMMAND "${TERRA}" "${SOURCE}" -o - --quiet
    RESULT_VARIABLE dash_result
    OUTPUT_VARIABLE dash_out
    ERROR_VARIABLE dash_err)
if(NOT dash_result EQUAL 0)
    message(FATAL_ERROR "-o - failed:\n${dash_out}\n${dash_err}")
endif()
if(NOT dash_out MATCHES "int main")
    message(FATAL_ERROR "-o - did not emit generated C:\n${dash_out}")
endif()

# --output=<path> writes the file.
set(out_file "${WORK}/out_opt.c")
execute_process(COMMAND "${TERRA}" "${SOURCE}" --output=${out_file} --quiet
    RESULT_VARIABLE outeq_result)
if(NOT outeq_result EQUAL 0)
    message(FATAL_ERROR "--output=<path> failed")
endif()
file(READ "${out_file}" out_content)
if(NOT out_content MATCHES "int main")
    message(FATAL_ERROR "--output=<path> produced wrong content")
endif()

# --build=<path> is an alternative spelling of the input file.
execute_process(COMMAND "${TERRA}" --build=${SOURCE} --stdout --quiet
    RESULT_VARIABLE buildeq_result
    OUTPUT_VARIABLE buildeq_out)
if(NOT buildeq_result EQUAL 0)
    message(FATAL_ERROR "--build=<path> failed")
endif()
if(NOT buildeq_out MATCHES "int main")
    message(FATAL_ERROR "--build=<path> did not emit generated C")
endif()

# The -- separator: options after it are treated as file names.
execute_process(COMMAND "${TERRA}" -- --quiet "${SOURCE}"
    RESULT_VARIABLE sep_result)
if(sep_result EQUAL 0)
    message(FATAL_ERROR "-- separator was ignored: --quiet treated as an option")
endif()
