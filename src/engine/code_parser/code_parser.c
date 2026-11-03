#include "code_parser.h"

#include <stdio.h>
#include <string.h>

#include "engine/frontend/ast.h"
#include "engine/sema/sema.h"
#include "core_alloc.h"

tess_program* tesseract_parse(const char *buffer, size_t buffer_size, const char *path) {
    (void)buffer_size;
    (void)path;
    if (!buffer) {
        return NULL;
    }
    
    // Allow empty source files (generates minimal valid C)
    // Note: The caller is responsible for printing the warning with --quiet support
    
    tess_program *program = core_alloc(sizeof *program);
    if (program == NULL) {
        return NULL;
    }
    memset(program, 0, sizeof *program);
    return program;
}
