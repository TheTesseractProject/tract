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
        check(strstr(code, "put_len(\"Hello!\", 6)") != NULL,
              "put(string literal) maps to length-aware put_len");
        check(strstr(code, "void put_len(const char* s, size_t n)") != NULL,
              "put_len runtime helper is emitted");
        check(code[0] != '\0', "generated code is not empty");
        free(code);
    }

    tess_program_free(program);
}

static void test_put_string_variable(void) {
    const char *src = "let s = \"Hi\";\nput(s);\n";
    tess_program *program = parse("putvar.t", src);
    check(program != NULL, "string-variable program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "putvar.t"), "string-variable program passes sema");

    char *code = c99_codegen_generate(program);
    check(code != NULL, "code generation succeeds for string variable");
    if (code != NULL) {
        check(strstr(code, "put_str") != NULL,
              "put(string variable) maps to put_str");
        free(code);
    }

    tess_program_free(program);
}

static void test_string_methods(void) {
    const char *src =
        "let s = \"  Hi  \";\n"
        "put(s.trim());\n"
        "put(s.trim().upper());\n"
        "put(s.contains(\"H\"));\n"
        "put(s.find(\"H\"));\n";
    tess_program *program = parse("strmeth.t", src);
    check(program != NULL, "string-method program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "strmeth.t"),
          "string-method program passes sema");

    char *code = c99_codegen_generate(program);
    check(code != NULL, "string-method code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "terra_str_trim(") != NULL,
              "trim dispatches to terra_str_trim");
        check(strstr(code, "terra_str_upper(") != NULL,
              "upper dispatches to terra_str_upper");
        check(strstr(code, "terra_str_contains(") != NULL,
              "contains dispatches to terra_str_contains");
        check(strstr(code, "char* terra_str_upper(const char* s)") != NULL,
              "string method helpers are emitted");
        free(code);
    }

    tess_program_free(program);
}

static void test_safe_mode(void) {
    const char *src = "let a = 2;\nput(a + 3);\nlet nums = [1, 2];\nput(nums[0]);\n";
    tess_program *program = parse("safe.t", src);
    check(program != NULL, "safe-mode program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "safe.t"), "safe-mode program passes sema");

    char *code = c99_codegen_generate_mode(program, false, true);
    check(code != NULL, "safe-mode code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "terra_checked_add(a, 3)") != NULL,
              "safe mode wraps integer addition in a checked helper");
        check(strstr(code, "terra_check_index") != NULL,
              "safe mode bounds-checks array indexing");
        check(strstr(code, "void terra_panic(const char* msg)") != NULL,
              "safe mode emits the panic helper");
        free(code);
    }

    tess_program_free(program);
}

static void test_hex_binary_shift_codegen(void) {
    const char *src =
        "put(0xFF);\n"
        "put(1 << 10);\n"
        "let m = 0b1100 & 0x0F;\n"
        "put(m);\n";
    tess_program *program = parse("bits.t", src);
    check(program != NULL, "bit-ops program parses");
    if (program == NULL) return;

    check(tess_sema_check(program, src, "bits.t"),
          "bit-ops program passes sema");

    char *code = c99_codegen_generate(program);
    check(code != NULL, "bit-ops code generation succeeds");
    if (code != NULL) {
        check(strstr(code, "<< 10") != NULL, "shift left reaches generated C");
        check(strstr(code, "255") != NULL, "hex literal 0xFF reaches generated C");
        check(strstr(code, "& 15") != NULL,
              "hex literal 0x0F reaches generated C");
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
        check(strstr(code, "static inline void put_float") != NULL,
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

static void test_functions_and_c99_scopes(void) {
    const char *src =
        "fn add(a: int, b: int): int { return a + b; }\n"
        "x = add(2, 3);\n"
        "if (true) { y = 8; put(y); }\n"
        "y = 9;\n"
        "put(y);\n";
    tess_program *program = parse("functions.t", src);
    check(program != NULL, "function program parses");
    if (program == NULL) return;
    check(tess_sema_check(program, src, "functions.t"),
          "function program passes sema");
    char *code = c99_codegen_generate(program);
    check(code != NULL, "function program generates");
    if (code != NULL) {
        check(strstr(code, "__auto_type") == NULL,
              "generator emits standard C declarations");
        check(strstr(code, "static int add(int a, int b);") != NULL,
              "function prototype is emitted before use");
        check(strstr(code, "long long x = add(2, 3);") != NULL,
              "function result is assigned with an explicit C type");
        free(code);
    }
    tess_program_free(program);
}

int main(void) {
    test_basic_generation();
    test_put_string_variable();
    test_string_methods();
    test_hex_binary_shift_codegen();
    test_safe_mode();
    test_large_generation();
    test_builtins();
    test_string_escaping();
    test_init_class_tracking();
    test_small_buffer_rejected();
    test_functions_and_c99_scopes();

    if (failures != 0) {
        fprintf(stderr, "%d codegen test(s) failed\n", failures);
        return 1;
    }
    printf("codegen tests passed\n");
    return 0;
}
