#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>
#include <stdbool.h>
#include "../frontend/ast.h"

/**
 * @brief Backend code generator interface
 */
typedef struct backend_codegen {
    // Generate code from AST
    bool (*generate)(struct backend_codegen *self, 
                     tess_program *program,
                     char *output_buffer,
                     size_t buffer_size);
    
    // Get backend name
    const char* (*get_name)(struct backend_codegen *self);
    
    // Get file extension
    const char* (*get_extension)(struct backend_codegen *self);
    
    // Free resources
    void (*destroy)(struct backend_codegen *self);
} backend_codegen;

// Create backends
backend_codegen* backend_create_c99(const void *config);
backend_codegen* backend_create_llvm(const void *config);
backend_codegen* backend_create_wasm(const void *config);

// Get available backends
size_t backend_get_available(backend_codegen *out_backends[], size_t capacity);

#endif // BACKEND_H
