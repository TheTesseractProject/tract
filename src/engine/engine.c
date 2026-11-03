#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "file_check/file_check.h"
#include "code_parser/code_parser.h"
#include "buffer_manager/buffer_manager.h"
#include "compiler/c99_codegen.h"
#include "sema/sema.h"
#include "constants.h"

// Old deprecated API removed - use generate_c99_code_from_program instead
// char* generate_c99_code(const char *source, size_t len, const char *path) { ... }

char* generate_c99_code_from_program(tess_program *program, const char *path,
                                     bool quiet) {
    if (!program) return NULL;
    
    if (!quiet) {
        MSGF(GENERATING_MSG, 1, 1, path);
        fflush(stdout);
    }
    
    clock_t start = clock();
    
    // Generate C99 code from AST into a freshly allocated buffer.
    char *buffer = c99_codegen_generate(program);
    if (!buffer) {
        ERRLN(GEN_FAIL_MSG);
        return NULL;
    }
    
    // Calculate time spent
    clock_t end = clock();
    double elapsed_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    if (!quiet) {
        MSGFLN(GENERATION_DONE_MSG, elapsed_time);
    }
    
    // Return ownership of buffer to caller
    return buffer;
}

void free_generated_code(char *code) {
    if (code) {
        free(code);
    }
}

bool engine_build_file(FILE *input_file, const char *input_path,
                       const char *output_path,
                       const terra_build_options *options) {
    terra_build_options opts = {0};
    if (options != NULL) {
        opts = *options;
    }

    if (!input_file) {
        ERRLN(TERRA_ACCESS_ERROR);
        return false;
    }

    // Validate input path
    if (input_path == NULL || input_path[0] == '\0') {
        ERRLN(ERR_BUILD_FILE_NOT_SPECIFIED);
        return false;
    }

    if (!opts.quiet) {
        MSGF(BUILDING_MSG, 1, 1, input_path);
        fflush(stdout);
    }

    // Initialize file buffer
    size_t file_size;
    char* buffer = init_file_buffer(input_file, &file_size);
    if (buffer == NULL) {
        return false;
    }

    // Safety limit for source size
    if (file_size > TERRA_MAX_SOURCE_SIZE) {
        ERRLN(ERR_SOURCE_TOO_LARGE);
        cleanup_buffer(buffer);
        return false;
    }

    // Note empty source files (they still produce a valid program).
    if (file_size == 0 && !opts.quiet) {
        MSGLN(WARNING_EMPTY_SOURCE);
    }

    // Parse source code into AST with semantic analysis
    tess_program *program = tesseract_parse(buffer, file_size, input_path);
    if (!program) {
        cleanup_buffer(buffer);
        return false;
    }
    
    // Re-run semantic analysis for non-empty source (tesseract_parse now just creates empty program)
    if (file_size > 0) {
        program = tess_parse(buffer, file_size, input_path);
        if (!program) {
            cleanup_buffer(buffer);
            return false;
        }
        
        if (!tess_sema_check(program, buffer, input_path)) {
            tess_program_free(program);
            cleanup_buffer(buffer);
            return false;
        }
    }

    if (opts.check_only) {
        tess_program_free(program);
        cleanup_buffer(buffer);
        if (!opts.quiet) {
            MSGLN(ERR_CHECK_ONLY_MSG);
            MSGLN(OK);
        }
        return true;
    }

    // Generate C99 code from AST
    char* c99_code = generate_c99_code_from_program(program, input_path,
                                                    opts.quiet);
    if (!c99_code) {
        tess_program_free(program);
        cleanup_buffer(buffer);
        return false;
    }

    if (opts.to_stdout) {
        fputs(c99_code, stdout);
        fflush(stdout);
        free_generated_code(c99_code);
        tess_program_free(program);
        cleanup_buffer(buffer);
        if (!opts.quiet) {
            MSGLN(ERR_OUTPUT_STDOUT_MSG);
        }
        return true;
    }

    // Determine output file path
    const char *out_path = output_path ? output_path : "output";

    // Write C99 code to output file
    FILE *output_file = fopen(out_path, "wb");
    if (output_file) {
        fprintf(output_file, "%s", c99_code);
        if (ferror(output_file) || fclose(output_file) != 0) {
            ERRFLN(ERR_OUTPUT_FILE_CREATE, out_path);
            free_generated_code(c99_code);
            tess_program_free(program);
            cleanup_buffer(buffer);
            return false;
        }
        if (!opts.quiet) {
            MSGFLN(OUTPUT_SAVED_MSG, out_path);
        }
    } else {
        ERRFLN(ERR_OUTPUT_FILE_CREATE, out_path);
        free_generated_code(c99_code);
        tess_program_free(program);
        cleanup_buffer(buffer);
        return false;
    }

    // Cleanup
    free_generated_code(c99_code);
    tess_program_free(program);
    cleanup_buffer(buffer);

    if (!opts.quiet) {
        MSGLN(OK);
        if (output_path != NULL && strcmp(output_path, "output") != 0) {
            MSGFLN(BUILDING_BUILT_MSG, output_path);
        } else {
            MSGLN(TERRA_BUILD_FINISHED);
        }
    }

    return true;
}
