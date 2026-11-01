cmake_minimum_required(VERSION 3.16)
project(terra VERSION 0.1.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings and maximum performance flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration -O3 -march=native -flto -funroll-loops -fomit-frame-pointer -DNDEBUG")

# Linker flags for performance
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s -Wl,--gc-sections -flto")

# Output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# Collect all source files recursively
file(GLOB_RECURSE SOURCES "src/*.c")

# Add executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Common core (lives under src/core)
add_subdirectory(src/core)
include_directories("src" "src/core/include")

# Link math library
target_link_libraries(${PROJECT_NAME} core m)

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
endif()

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
