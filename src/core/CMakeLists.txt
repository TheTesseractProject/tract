cmake_minimum_required(VERSION 3.16)
project(core LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(core STATIC
    src/core_alloc.c
    src/core_crc.c
    src/core_diagnostics.c
    src/core_float.c
    src/core_json.c
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Default to FULL profile unless overridden.
if(NOT DEFINED CORE_PROFILE)
    set(CORE_PROFILE "FULL")
endif()

if(CORE_PROFILE STREQUAL "FULL")
    target_compile_definitions(core PUBLIC CORE_PROFILE_FULL)
elseif(CORE_PROFILE STREQUAL "EMBEDDED")
    target_compile_definitions(core PUBLIC CORE_PROFILE_EMBEDDED)
else()
    message(FATAL_ERROR "Unknown CORE_PROFILE: ${CORE_PROFILE}")
endif()
