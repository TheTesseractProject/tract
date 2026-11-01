add_library(compiler STATIC
    codegen.c
)

target_include_directories(compiler PRIVATE
    .
    ../frontend
    ../sema
    # Add other necessary includes here
)

# Link dependencies (e.g., core, frontend, sema) here
# target_link_libraries(compiler PRIVATE core_lib frontend_lib sema_lib)
