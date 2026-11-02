#include "backend.h"
#include "c99_codegen.h"
#include <stdlib.h>
#include <string.h>

// C99 Backend Implementation
typedef struct {
    backend_codegen base;
} c99_backend_impl;

static bool c99_generate(backend_codegen *self, 
                         tess_program *program,
                         char *output_buffer,
                         size_t buffer_size) {
    (void)self;
    return c99_codegen_program(program, output_buffer, buffer_size) > 0;
}
static const char* c99_get_name(backend_codegen *self) {
    (void)self;
    return "c99";
}

static const char* c99_get_extension(backend_codegen *self) {
    (void)self;
    return ".c";
}

static void c99_destroy(backend_codegen *self) {
    free(self);
}

backend_codegen* backend_create_c99(const void *config) {
    (void)config;
    c99_backend_impl *impl = malloc(sizeof(c99_backend_impl));
    if (!impl) return NULL;
    
    impl->base.generate = c99_generate;
    impl->base.get_name = c99_get_name;
    impl->base.get_extension = c99_get_extension;
    impl->base.destroy = c99_destroy;
    
    return &impl->base;
}

// LLVM Backend Placeholder
typedef struct {
    backend_codegen base;
} llvm_backend_impl;

static bool llvm_generate(backend_codegen *self,
                          tess_program *program,
                          char *output_buffer,
                          size_t buffer_size) {
    (void)self; (void)program; (void)output_buffer; (void)buffer_size;
    // TODO: Implement LLVM IR generation
    return false;
}

static const char* llvm_get_name(backend_codegen *self) {
    (void)self;
    return "llvm";
}

static const char* llvm_get_extension(backend_codegen *self) {
    (void)self;
    return ".ll";
}

static void llvm_destroy(backend_codegen *self) {
    free(self);
}

backend_codegen* backend_create_llvm(const void *config) {
    (void)config;
    llvm_backend_impl *impl = malloc(sizeof(llvm_backend_impl));
    if (!impl) return NULL;
    
    impl->base.generate = llvm_generate;
    impl->base.get_name = llvm_get_name;
    impl->base.get_extension = llvm_get_extension;
    impl->base.destroy = llvm_destroy;
    
    return &impl->base;
}

// WASM Backend Placeholder
typedef struct {
    backend_codegen base;
} wasm_backend_impl;

static bool wasm_generate(backend_codegen *self,
                          tess_program *program,
                          char *output_buffer,
                          size_t buffer_size) {
    (void)self; (void)program; (void)output_buffer; (void)buffer_size;
    // TODO: Implement WebAssembly generation
    return false;
}

static const char* wasm_get_name(backend_codegen *self) {
    (void)self;
    return "wasm";
}

static const char* wasm_get_extension(backend_codegen *self) {
    (void)self;
    return ".wasm";
}

static void wasm_destroy(backend_codegen *self) {
    free(self);
}

backend_codegen* backend_create_wasm(const void *config) {
    (void)config;
    wasm_backend_impl *impl = malloc(sizeof(wasm_backend_impl));
    if (!impl) return NULL;
    
    impl->base.generate = wasm_generate;
    impl->base.get_name = wasm_get_name;
    impl->base.get_extension = wasm_get_extension;
    impl->base.destroy = wasm_destroy;
    
    return &impl->base;
}

size_t backend_get_available(backend_codegen *out_backends[], size_t capacity) {
    static backend_codegen *backends[3];
    static int initialized = 0;
    
    if (!initialized) {
        backends[0] = backend_create_c99(NULL);
        backends[1] = backend_create_llvm(NULL);
        backends[2] = backend_create_wasm(NULL);
        initialized = 1;
    }
    
    size_t count = 0;
    for (int i = 0; i < 3 && capacity > 0; i++) {
        if (backends[i]) {
            out_backends[count++] = backends[i];
            capacity--;
        }
    }
    
    return count;
}
