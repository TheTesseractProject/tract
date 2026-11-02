#include "engine/compiler/c99_codegen.h"
#include "engine/frontend/ast.h"
#include "engine/sema/sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static tess_program *parse(const char *name, const char *src) {
    tess_program *program = tess_parse(src, strlen(src), name);
    if (program == NULL) {
        fprintf(stderr, "FAIL: could not parse %s\n", name);
        failures++;
    }
    return program;
}

static void test_basic_generation(void) {
    const char *src = "put(\"Hello!\");\n";
    tess_program *program = parse("basic.t", src);
    check(program != NULL, "basic program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "basic.t"), "basic program passes sema");

    char *code = c99_codegen_generate(program);
    check(code != NULL, "code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "int main") != NULL, "generated code has main");
        check(strstr(code, "put_str") != NULL, "put(string) maps to put_str");
        check(code[0] != '\0', "generated code is not empty");
        free(code);
    }

    tess_program_free(program);
}

static void test_large_generation(void) {
    /* Enough top-level assignments to push the generated C past the old
     * fixed 64 KiB output buffer. Parsing only: the point is buffer growth. */
    const size_t lines = 5000;
    size_t cap = lines * 32 + 64;
    char *src = malloc(cap);
    check(src != NULL, "large source buffer allocated");
    if (src == NULL) return;

    size_t used = 0;
    for (size_t i = 0; i < lines; i++) {
        used += (size_t)snprintf(src + used, cap - used, "v%zu = %zu;\n", i, i);
    }
    src[used] = '\0';

    tess_program *program = parse("large.t", src);
    check(program != NULL, "large program parses");
    if (program != NULL) {
        char *code = c99_codegen_generate(program);
        check(code != NULL, "large code generation succeeds");
        if (code != NULL) {
            check(strlen(code) > 65536,
                  "large generated code exceeds the old 64 KiB buffer");
            size_t n = strlen(code);
            check(n > 0 && code[n - 1] == '\n',
                  "large generated code is NUL-terminated and complete");
            free(code);
        }
        tess_program_free(program);
    }

    free(src);
}

static void test_builtins(void) {
    const char *src =
        "x = 1.5;\n"
        "put(x);\n"
        "put(len(\"hello\"));\n"
        "put(str(42));\n"
        "put(int(3.9));\n"
        "put(float(7));\n"
        "assert(1 == 1);\n";
    tess_program *program = parse("builtins.t", src);
    check(program != NULL, "builtins program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "builtins.t"),
          "builtins program passes sema");

    char *code = c99_codegen_generate(program);
    check(code != NULL, "builtins code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "put_float") != NULL, "put(float) maps to put_float");
        check(strstr(code, "static void put_float") != NULL,
              "put_float is defined in the runtime");
        check(strstr(code, "terra_len") != NULL, "len maps to terra_len");
        check(strstr(code, "terra_str_int") != NULL, "str(int) maps to terra_str_int");
        check(strstr(code, "terra_int_from_float") != NULL,
              "int(float) maps to terra_int_from_float");
        check(strstr(code, "terra_float_from_int") != NULL,
              "float(int) maps to terra_float_from_int");
        check(strstr(code, "terra_assert") != NULL, "assert maps to terra_assert");
        free(code);
    }
    tess_program_free(program);
}

static void test_string_escaping(void) {
    const char *src = "put(\"a\\\"b\\\\c\\n\\t\");\n";
    tess_program *program = parse("escape.t", src);
    check(program != NULL, "escaping program parses");
    if (program == NULL) return;

    char *code = c99_codegen_generate(program);
    check(code != NULL, "escaping code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "\\\"") != NULL, "quote is escaped");
        check(strstr(code, "\\\\") != NULL, "backslash is escaped");
        check(strstr(code, "\\n") != NULL, "newline is escaped");
        check(strstr(code, "\\t") != NULL, "tab is escaped");
        free(code);
    }
    tess_program_free(program);
}

static void test_init_class_tracking(void) {
    const char *src =
        "Point {\n"
        "    x: int;\n"
        "    init(v: int) { self.x = v; }\n"
        "    show { put(self.x); }\n"
        "}\n"
        "p = Point.init(7);\n"
        "p.show;\n";
    tess_program *program = parse("init.t", src);
    check(program != NULL, "init program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "init.t"),
          "init program passes sema");

    char *code = c99_codegen_generate(program);
    check(code != NULL, "init code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "Point_show(p)") != NULL,
              "variables built with init() dispatch methods correctly");
        check(strstr(code, "UNSUPPORTED") == NULL,
              "init program has no unsupported constructs");
        free(code);
    }
    tess_program_free(program);
}

static void test_small_buffer_rejected(void) {
    const char *src = "put(\"Hello!\");\n";
    tess_program *program = parse("small.t", src);
    check(program != NULL, "small-buffer program parses");
    if (program != NULL) {
        char small[16];
        memset(small, 'x', sizeof small);
        size_t written = c99_codegen_program(program, small, sizeof small);
        check(written == 0, "undersized output buffer is rejected");
        check(small[0] == '\0', "undersized buffer is reset");
        tess_program_free(program);
    }
}

int main(void) {
    test_basic_generation();
    test_large_generation();
    test_builtins();
    test_string_escaping();
    test_init_class_tracking();
    test_small_buffer_rejected();

    if (failures != 0) {
        fprintf(stderr, "%d codegen test(s) failed\n", failures);
        return 1;
    }
    printf("codegen tests passed\n");
    return 0;
}
