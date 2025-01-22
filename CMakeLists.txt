cmake_minimum_required(VERSION 3.10)
project(tract)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s -Wl,--gc-sections")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

file(GLOB_RECURSE SOURCES "src/*.c")

add_executable(${PROJECT_NAME} ${SOURCES})

include_directories("src")

target_link_libraries(${PROJECT_NAME})