#include "engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "file_check/file_check.h"
#include "buffer_manager/buffer_manager.h"
#include "compiler/c99_codegen.h"
#include "sema/sema.h"
#include "constants.h"

/* Progress messages go to stdout by default, but they must never mix into the
 * generated C when the caller asked for --stdout output. */
static void progress(bool to_stdout, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (to_stdout) {
        char msg[512];
        vsnprintf(msg, sizeof msg, fmt, ap);
        terra_err(msg);
        fflush(stderr);
    } else {
        char msg[512];
        vsnprintf(msg, sizeof msg, fmt, ap);
        terra_out(msg);
        fflush(stdout);
    }
    va_end(ap);
}

static void progress_line(bool to_stdout, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char msg[512];
    vsnprintf(msg, sizeof msg, fmt, ap);
    if (to_stdout) {
        terra_err(msg);
        fputc('\n', stderr);
        fflush(stderr);
    } else {
        terra_out(msg);
        fputc('\n', stdout);
        fflush(stdout);
    }
    va_end(ap);
}

char* generate_c99_code_from_program(tess_program *program, const char *path,
                                     const terra_build_options *opts) {
    if (!program) return NULL;

    bool to_stdout = opts != NULL && opts->to_stdout;
    bool quiet = opts != NULL && opts->quiet;

    if (!quiet) {
        progress(to_stdout, GENERATING_MSG, 1, 1, path);
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
        progress_line(to_stdout, GENERATION_DONE_MSG, elapsed_time);
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
        progress(opts.to_stdout, BUILDING_MSG, 1, 1, input_path);
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
        if (opts.to_stdout) {
            ERRFLN(WARNING_EMPTY_SOURCE, input_path);
        } else {
            MSGFLN(WARNING_EMPTY_SOURCE, input_path);
        }
    }

    tess_program *program = tess_parse(buffer, file_size, input_path);
    if (!program) {
        cleanup_buffer(buffer);
        return false;
    }

    if (!tess_sema_check(program, buffer, input_path)) {
        tess_program_free(program);
        cleanup_buffer(buffer);
        return false;
    }

    if (opts.check_only) {
        tess_program_free(program);
        cleanup_buffer(buffer);
        if (!opts.quiet) {
            progress_line(opts.to_stdout, ERR_CHECK_ONLY_MSG);
            progress_line(opts.to_stdout, OK);
        }
        return true;
    }

    // Generate C99 code from AST
    char* c99_code = generate_c99_code_from_program(program, input_path, &opts);
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
            progress_line(true, ERR_OUTPUT_STDOUT_MSG);
        }
        return true;
    }

    // Determine output file path
    const char *out_path = output_path ? output_path : "output";

    // Refuse to clobber the source file with generated output. Comparing the
    // open input and existing destination also catches symlink and hard-link
    // aliases, where the two path strings differ.
    struct stat input_stat;
    struct stat output_stat;
    bool same_file = strcmp(out_path, input_path) == 0;
    if (fstat(fileno(input_file), &input_stat) == 0 &&
        stat(out_path, &output_stat) == 0 &&
        input_stat.st_dev == output_stat.st_dev &&
        input_stat.st_ino == output_stat.st_ino) {
        same_file = true;
    }
    if (same_file) {
        ERRFLN(ERR_OUTPUT_IS_INPUT, input_path);
        free_generated_code(c99_code);
        tess_program_free(program);
        cleanup_buffer(buffer);
        return false;
    }

    // Write C99 code to output file
    FILE *output_file = fopen(out_path, "wb");
    if (output_file) {
        size_t code_len = strlen(c99_code);
        bool write_failed = fwrite(c99_code, 1, code_len, output_file) != code_len;
        if (ferror(output_file)) write_failed = true;
        // Always close the stream, even if writing already failed.
        if (fclose(output_file) != 0) write_failed = true;
        if (write_failed) {
            ERRFLN(ERR_OUTPUT_FILE_CREATE, out_path);
            free_generated_code(c99_code);
            tess_program_free(program);
            cleanup_buffer(buffer);
            return false;
        }
        if (!opts.quiet) {
            progress_line(opts.to_stdout, OUTPUT_SAVED_MSG, out_path);
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
        progress_line(opts.to_stdout, OK);
        if (output_path != NULL && strcmp(output_path, "output") != 0) {
            progress_line(opts.to_stdout, BUILDING_BUILT_MSG, output_path);
        } else {
            progress_line(opts.to_stdout, TERRA_BUILD_FINISHED);
        }
    }

    return true;
}
