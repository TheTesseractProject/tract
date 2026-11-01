#include "code_parser.h"

#include "engine/frontend/ast.h"
#include "engine/sema/sema.h"

bool tesseract_parse(const char *buffer, size_t buffer_size,
                     const char *path) {
    tess_program *program = tess_parse(buffer, buffer_size, path);
    if (program == NULL) {
        return false;
    }

    bool ok = tess_sema_check(program, buffer, path);
    tess_program_free(program);
    return ok;
}
