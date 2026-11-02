#include "code_parser.h"

#include <stdio.h>
#include <string.h>

#include "engine/frontend/ast.h"
#include "engine/sema/sema.h"
#include "core_alloc.h"

tess_program* tesseract_parse(const char *buffer, size_t buffer_size, const char *path) {
    if (!buffer) {
        return NULL;
    }
    
    // Allow empty source files (generates minimal valid C)
    if (buffer_size == 0) {
        fprintf(stderr, "Warning: Empty source file '%s'\n", path);
        tess_program *program = core_alloc(sizeof *program);
        if (program == NULL) {
            return NULL;
        }
        memset(program, 0, sizeof *program);
        return program;
    }
    
    // Parse source into AST
    tess_program *program = tess_parse(buffer, buffer_size, path);
    if (program == NULL) {
        return NULL;
    }
    
    // Run semantic analysis
    bool ok = tess_sema_check(program, buffer, path);
    if (!ok) {
        tess_program_free(program);
        return NULL;
    }
    
    return program;
}
