cmake_minimum_required(VERSION 3.16)
project(tract VERSION 0.1.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
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

# Include directories
include_directories("src")

# Link math library
target_link_libraries(${PROJECT_NAME} m)

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)