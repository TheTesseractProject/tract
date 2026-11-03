#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "engine/frontend/ast.h"

typedef struct {
    bool check_only; /* parse and type-check, do not generate C */
    bool to_stdout;  /* write generated C to stdout instead of a file */
    bool quiet;      /* suppress progress messages */
    bool compile;    /* compile generated C into an executable */
    const char *c_compiler; /* compiler executable path; defaults to `cc` */
    const char *target_triple; /* optional cross-compilation target */
    bool freestanding;
} terra_build_options;

bool engine_build_file(FILE *input_file, const char *input_path,
                       const char *output_path,
                       const terra_build_options *options);
char* generate_c99_code_from_program(tess_program *program, const char *path,
                                     const terra_build_options *options);
void free_generated_code(char *code);
