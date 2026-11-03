#include "engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

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

static bool compile_generated_program(const char *code, const char *output,
                                     const char *compiler,
                                     const char *target_triple,
                                     bool freestanding,
                                     const tess_program *program) {
#ifdef _WIN32
    (void)code;
    (void)output;
    (void)compiler;
    (void)target_triple;
    (void)freestanding;
    (void)program;
    ERRLN("--compile is currently supported on POSIX systems only");
    return false;
#else
    char temp_path[] = "/tmp/terra-source-XXXXXX";
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        ERRLN("Unable to create temporary C source file");
        return false;
    }
    FILE *temp = fdopen(fd, "wb");
    if (temp == NULL) {
        close(fd);
        unlink(temp_path);
        ERRLN("Unable to open temporary C source file");
        return false;
    }
    size_t code_len = strlen(code);
    bool write_failed = fwrite(code, 1, code_len, temp) != code_len;
    if (fclose(temp) != 0) write_failed = true;
    if (write_failed) {
        unlink(temp_path);
        ERRLN("Unable to write temporary C source file");
        return false;
    }

    size_t output_len = strlen(output);
    if (output_len > SIZE_MAX - sizeof ".tmp.XXXXXX") {
        unlink(temp_path);
        ERRLN("Executable output path is too long");
        return false;
    }
    char *temp_output = malloc(output_len + sizeof ".tmp.XXXXXX");
    if (temp_output == NULL) {
        unlink(temp_path);
        ERRLN("Memory allocation failed while preparing executable output");
        return false;
    }
    memcpy(temp_output, output, output_len);
    memcpy(temp_output + output_len, ".tmp.XXXXXX", sizeof ".tmp.XXXXXX");
    int output_fd = mkstemp(temp_output);
    if (output_fd < 0 || close(output_fd) != 0) {
        if (output_fd >= 0) unlink(temp_output);
        free(temp_output);
        unlink(temp_path);
        ERRLN("Unable to create temporary executable output");
        return false;
    }

    size_t link_count = 0;
    size_t link_i = 0;
    for (size_t i = 0; i < program->count; i++) {
        if (program->items[i] != NULL &&
            program->items[i]->kind == TESS_STMT_LINK) link_count++;
    }
    size_t extra_options = (target_triple != NULL ? 2u : 0u) +
                           (freestanding ? 3u : 0u);
    if (link_count > SIZE_MAX - 11 - extra_options) {
        unlink(temp_output);
        free(temp_output);
        unlink(temp_path);
        ERRLN("Too many linked libraries");
        return false;
    }
    char **args = calloc(link_count + 11 + extra_options, sizeof *args);
    char **link_args = link_count ? calloc(link_count, sizeof *link_args) : NULL;
    if (args == NULL || (link_count != 0 && link_args == NULL)) {
        free(args);
        free(link_args);
        unlink(temp_output);
        free(temp_output);
        unlink(temp_path);
        ERRLN("Memory allocation failed while preparing C compiler");
        return false;
    }
    size_t n = 0;
    args[n++] = (char *)(compiler != NULL ? compiler : "cc");
    args[n++] = "-std=gnu11";
    args[n++] = "-x";
    args[n++] = "c";
    args[n++] = temp_path;
    if (!freestanding) {
        args[n++] = "-x";
        args[n++] = "none";
    }
    if (target_triple != NULL) {
        args[n++] = "-target";
        args[n++] = (char *)target_triple;
    }
    if (freestanding) {
        args[n++] = "-c";
        args[n++] = "-ffreestanding";
        args[n++] = "-fno-builtin";
    }
    args[n++] = "-o";
    args[n++] = temp_output;
    for (size_t i = 0; !freestanding && i < program->count; i++) {
        tess_stmt *st = program->items[i];
        if (st == NULL || st->kind != TESS_STMT_LINK) continue;
        const char *library = st->as.link.library;
        size_t len = strlen(library);
        char *arg = malloc(len + 3);
        if (arg == NULL) {
            for (size_t j = 0; j < link_count; j++) free(link_args[j]);
            free(link_args);
            free(args);
            unlink(temp_output);
            free(temp_output);
            unlink(temp_path);
            ERRLN("Memory allocation failed while preparing library flags");
            return false;
        }
        memcpy(arg, "-l", 2);
        memcpy(arg + 2, library, len + 1);
        link_args[link_i++] = arg;
        args[n++] = arg;
    }
    if (!freestanding) args[n++] = "-lm";
    args[n] = NULL;

    pid_t child;
    int spawn_error = posix_spawnp(&child, args[0], NULL, NULL, args, environ);
    int status = -1;
    if (spawn_error == 0) {
        while (waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) {
                status = -1;
                break;
            }
        }
    }
    for (size_t i = 0; i < link_count; i++) free(link_args[i]);
    free(link_args);
    free(args);
    unlink(temp_path);
    if (spawn_error != 0 || status == -1 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        unlink(temp_output);
        free(temp_output);
        ERRFLN("C compiler failed%s%s",
               spawn_error != 0 ? ": " : "",
               spawn_error != 0 ? strerror(spawn_error) : "");
        return false;
    }
    if (chmod(temp_output, freestanding ? 0644 : 0700) != 0 ||
        rename(temp_output, output) != 0) {
        unlink(temp_output);
        free(temp_output);
        ERRFLN("Unable to publish executable '%s'", output);
        return false;
    }
    free(temp_output);
    return true;
#endif
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
    char *buffer = c99_codegen_generate_mode(program,
        opts != NULL && opts->freestanding);
    if (!buffer) {
        if (opts != NULL && opts->freestanding)
            ERRLN("Freestanding output cannot contain classes, hosted runtime helpers, or top-level executable statements");
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
    const char *out_path = output_path ? output_path
                           : opts.compile ? "a.out" : "output";

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

    if (opts.compile) {
        bool compiled = compile_generated_program(c99_code, out_path,
                                                   opts.c_compiler,
                                                   opts.target_triple,
                                                   opts.freestanding, program);
        free_generated_code(c99_code);
        tess_program_free(program);
        cleanup_buffer(buffer);
        if (!compiled) return false;
        if (!opts.quiet) {
            progress_line(false, opts.freestanding ? "Freestanding object built to '%s'" :
                                                     "Executable built to '%s'", out_path);
        }
        return true;
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
