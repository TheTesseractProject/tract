cmake_minimum_required(VERSION 3.16)
project(terra VERSION 0.2.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings. These are portable across GCC and Clang.
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror
                        -Werror=implicit-function-declaration)
endif()

# Aggressive optimization is opt-in for local release builds and is guarded so
# it does not break macOS/Windows or cross-compilation.
option(TERRA_NATIVE_OPT "Use -march=native and link-time optimization" ON)
if(TERRA_NATIVE_OPT AND NOT MSVC AND NOT APPLE)
    add_compile_options(-O3 -march=native -flto -funroll-loops
                        -fomit-frame-pointer -DNDEBUG)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s -Wl,--gc-sections -flto")
elseif(NOT MSVC)
    add_compile_options(-O2 -DNDEBUG)
endif()

# Output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# Collect all source files recursively, excluding the separate core library
# (it is built by add_subdirectory(src/core) and linked as a target).
file(GLOB_RECURSE SOURCES "src/*.c")
list(FILTER SOURCES EXCLUDE REGEX "/src/core/")
message("Detected sources: ${SOURCES}")

# Add executable target
add_executable(${PROJECT_NAME} ${SOURCES})
# Common core (lives under src/core)
add_subdirectory(src/core)
include_directories("src" "src/core/include")

# Link math library
target_link_libraries(${PROJECT_NAME} core m)

# Add codegen dependencies
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/engine/compiler")

# Tests for the shared core
include(CTest)
if(BUILD_TESTING)
    add_executable(core_common_tests tests/core_common_test.c)
    target_link_libraries(core_common_tests core m)
    add_test(NAME core_common COMMAND core_common_tests)

    add_executable(sema_tests
        tests/sema_test.c
        src/engine/frontend/lexer.c
        src/engine/frontend/ast.c
        src/engine/frontend/parser.c
        src/engine/sema/sema.c
    )
    target_link_libraries(sema_tests core m)
    add_test(NAME sema COMMAND sema_tests)

    add_executable(frontend_tests
        tests/frontend_test.c
        src/engine/frontend/lexer.c
        src/engine/frontend/ast.c
        src/engine/frontend/parser.c
    )
    target_link_libraries(frontend_tests core m)
    add_test(NAME frontend COMMAND frontend_tests)

    add_executable(codegen_tests
        tests/codegen_test.c
        src/engine/frontend/lexer.c
        src/engine/frontend/ast.c
        src/engine/frontend/parser.c
        src/engine/sema/sema.c
        src/engine/compiler/c99_codegen.c
    )
    target_link_libraries(codegen_tests core m)
    add_test(NAME codegen COMMAND codegen_tests)

    # End-to-end: compile a Tesseract example, build the generated C, run it.
    foreach(example class hello_world even_odd matrix_mult linked_classes)
        if(example STREQUAL "class")
            set(example_expect "world")
        elseif(example STREQUAL "hello_world")
            set(example_expect "Hello, Tesseract!")
        elseif(example STREQUAL "even_odd")
            set(example_expect "Even")
        elseif(example STREQUAL "linked_classes")
            set(example_expect "Doubled:")
        else()
            set(example_expect "Product")
        endif()
        add_test(
            NAME e2e_${example}
            COMMAND ${CMAKE_COMMAND}
                -DTERRA=$<TARGET_FILE:${PROJECT_NAME}>
                -DCOMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_SOURCE_DIR}/examples/${example}.t
                -DEXPECT=${example_expect}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_${example}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.cmake
        )
    endforeach()

    add_test(NAME cli
        COMMAND ${CMAKE_COMMAND}
            -DTERRA=$<TARGET_FILE:${PROJECT_NAME}>
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/class.t
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake
    )
endif()

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
