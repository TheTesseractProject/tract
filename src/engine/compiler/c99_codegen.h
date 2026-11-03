#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "../frontend/ast.h"

/**
 * @brief C99 Code Generator Context
 * 
 * Manages indentation, buffer management, and emitted symbols during code generation.
 */
typedef struct {
    char *buffer;              // Output C99 code buffer
    size_t capacity;           // Total buffer capacity
    size_t size;               // Current output size
    int indent_level;          // Current indentation level
    bool has_main;             // Whether main() was already generated
    bool failed;               // An unsupported construct was encountered
    tess_program *program;     // Source program (for class lookup)
    const char *current_class; // Current class being generated
    char **declared;           // Declared variable names in current function
    size_t declared_count;
    size_t declared_cap;
    char **var_names;          // Variable -> class name map
    char **var_classes;
    size_t var_count;
    size_t var_cap;
    char **module_aliases;     // Imported module alias -> module key
    char **module_targets;
    size_t module_alias_count;
    size_t module_alias_cap;
    const char *module;        // Module key when generating an imported module
} c99_codegen;

/**
 * @brief Initialize the C99 code generator
 * @param gen Pointer to codegen context
 * @param initial_capacity Initial buffer capacity
 * @return true on success, false otherwise
 */
bool c99_codegen_init(c99_codegen *gen, size_t initial_capacity);

/**
 * @brief Destroy the codegen and free resources
 * @param gen Pointer to codegen context
 */
void c99_codegen_destroy(c99_codegen *gen);

/**
 * @brief Reset the code generator state (clear buffer but keep capacity)
 * @param gen Pointer to codegen context
 */
void c99_codegen_reset(c99_codegen *gen);

/**
 * @brief Generate C99 code from a Tesseract program into a new allocation
 * @param program Parsed Tesseract program (AST)
 * @return Heap-allocated, NUL-terminated C99 source owned by the caller, or
 *         NULL on error. Free it with free().
 */
char *c99_codegen_generate(tess_program *program);

/**
 * @brief Generate C99 code for an imported module (module-scoped functions).
 * @param program Parsed Tesseract program (AST) of the module
 * @param module Module key used to namespace generated functions
 * @return Heap-allocated C99 source owned by the caller, or NULL on error
 */
char *c99_codegen_generate_module(tess_program *program, const char *module);

/**
 * @brief Generate C99 code from a Tesseract program into a caller buffer
 * @param program Parsed Tesseract program (AST)
 * @param out_buffer Output buffer for C99 code
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, or 0 on error (including when the buffer
 *         is too small; out_buffer is left empty in that case)
 */
size_t c99_codegen_program(tess_program *program, char *out_buffer, size_t buffer_size);

/**
 * @brief Print runtime library header information
 * @param gen Pointer to codegen context
 */
void c99_codegen_print_runtime_info(c99_codegen *gen);
