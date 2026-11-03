#include "engine/frontend/ast.h"
#include "engine/sema/sema.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Parse and run sema; return true on success. Suppress stderr for error cases. */
static bool sema_ok(const char *src) {
    tess_program *p = tess_parse(src, strlen(src), "<test>");
    if (p == NULL) return false;
    bool ok = tess_sema_check(p, src, "<test>");
    tess_program_free(p);
    return ok;
}

static bool sema_err(const char *src) {
    /* Redirect stderr to /dev/null while checking expected-error cases so the
     * test output stays clean. */
    FILE *orig = stderr;
    FILE *null = fopen("/dev/null", "w");
    if (null) stderr = null;

    tess_program *p = tess_parse(src, strlen(src), "<test>");
    bool ok = true;
    if (p != NULL) {
        ok = !tess_sema_check(p, src, "<test>");
        tess_program_free(p);
    }

    if (null) { fclose(null); stderr = orig; }
    return ok;
}

/* ---- passing cases ---- */

static void test_hello(void) {
    check(sema_ok("put(\"hello\");"), "hello world passes sema");
}

static void test_assign_then_use(void) {
    check(sema_ok("x = 42;\nput(x);"), "assign-to-declare then use");
}

static void test_even_odd(void) {
    const char *src = "num = 2;\nput(!(num%2) ? \"Even\" : \"Odd\");\n";
    check(sema_ok(src), "even_odd passes sema");
}

static void test_class(void) {
    const char *src =
        "Counter {\n"
        "    n: int;\n"
        "    inc { n = n + 1; }\n"
        "    val: int { return n; }\n"
        "}\n"
        "c = Counter.new;\n"
        "c.inc;\n"
        "put(c.val);\n";
    check(sema_ok(src), "class with method passes sema");
}

static void test_method_params(void) {
    const char *src =
        "Greeter {\n"
        "    greet(name: str) { put(name); }\n"
        "}\n"
        "g = Greeter.new;\n"
        "g.greet(\"world\");\n";
    check(sema_ok(src), "method parameters visible in body");
}

static void test_forward_ref(void) {
    /* Class used before its definition in the source order */
    const char *src =
        "x = Animal.new;\n"
        "Animal { name: str; }\n";
    check(sema_ok(src), "forward class reference passes sema");
}

static void test_nested_scope(void) {
    const char *src =
        "i = 0;\n"
        "while (i < 10) {\n"
        "    i = i + 1;\n"
        "}\n"
        "put(i);\n";
    check(sema_ok(src), "while loop with outer variable passes");
}

static void test_if_else(void) {
    const char *src =
        "x = 5;\n"
        "if (x > 0) { put(\"pos\"); } else { put(\"non-pos\"); }\n";
    check(sema_ok(src), "if-else passes sema");
}

static void test_builtins(void) {
    check(sema_ok("x = str(42);"),    "str() builtin passes");
    check(sema_ok("x = int(\"3\");"), "int() builtin passes");
    check(sema_ok("x = len(\"hi\");"),"len() builtin passes");
    check(sema_ok("assert(1);"),      "assert() builtin passes");
    check(sema_ok("x = input();"),    "input() builtin passes");
}

static void test_module(void) {
    check(sema_ok("w = @math.sqrt;\n"), "@module expr passes sema");
}

/* ---- error cases ---- */

static void test_undefined_var(void) {
    check(sema_err("put(x);"), "undefined variable is rejected");
}

static void test_bad_lvalue(void) {
    check(sema_err("1 + 2 = 5;\n"), "non-lvalue assignment is rejected");
    check(sema_err("put(\"x\") = 5;\n"), "call result assignment is rejected");
}

static void test_arity_mismatch(void) {
    check(sema_err("put(\"a\", \"b\");"), "put with 2 args is rejected");
    check(sema_err("put();"),            "put with 0 args is rejected");
    check(sema_err("assert(1, 2);"),     "assert with 2 args is rejected");
}

static void test_return_outside_method(void) {
    check(sema_err("return 1;\n"), "top-level return is rejected");
}

static void test_constant_divmod_zero(void) {
    check(sema_err("put(1/0);\n"),   "integer division by zero is rejected");
    check(sema_err("put(1%0);\n"),   "integer modulo by zero is rejected");
    check(sema_err("put(1/-0);\n"),  "negated zero divisor is rejected");
    check(sema_ok("put(1/2);\n"),    "nonzero integer division passes");
    check(sema_ok("put(1.0/0.0);\n"),"float division by zero passes (IEEE)");
    check(sema_ok("d = 0;\nput(1/d);\n"),
          "division by a non-constant zero variable passes");
}

static void test_duplicate_class(void) {
    check(sema_err("A { x: int; }\nA { y: str; }\n"),
          "duplicate class name is rejected");
}

static void test_unknown_member(void) {
    const char *src =
        "Dog { name: str; }\n"
        "d = Dog.new;\n"
        "put(d.bark);\n";    /* 'bark' not declared */
    check(sema_err(src), "unknown member on instance is rejected");

    const char *src2 =
        "Cat { name: str; }\n"
        "Cat.bark;\n";   /* 'bark' not declared, accessed on the class symbol */
    check(sema_err(src2), "unknown member on class name is rejected");

    const char *src3 =
        "Bird { name: str; }\n"
        "b = Bird.new;\n"
        "b.bark();\n";
    check(sema_err(src3), "unknown method call is rejected");
}

/* ---- regressions ---- */

static void test_many_variables(void) {
    /* The old fixed-size (64 entry) scope table rejected the 65th name. */
    char src[4096];
    size_t n = 0;
    for (int i = 0; i < 120; i++) {
        n += (size_t)snprintf(src + n, sizeof src - n, "v%d = %d;\n", i, i);
    }
    check(sema_ok(src), "a scope with more than 64 variables works");
}

static void test_field_assignment_types(void) {
    const char *src =
        "Box { s: str; n: int; }\n"
        "b = Box.new;\n"
        "b.s = \"ok\";\n"
        "b.n = 42;\n";
    check(sema_ok(src), "correct field assignments pass");

    const char *bad =
        "Box { s: str; }\n"
        "b = Box.new;\n"
        "b.s = 12345;\n";
    check(sema_err(bad), "assigning int to a str field is rejected");
}

static void test_method_signatures(void) {
    const char *src =
        "Calc { add(a: int, b: int) { put(a + b); } }\n"
        "c = Calc.new;\n"
        "c.add(1, 2);\n";
    check(sema_ok(src), "method call with matching arguments passes");

    check(sema_err(
        "Calc { add(a: int, b: int) { put(a + b); } }\n"
        "c = Calc.new;\n"
        "c.add(1);\n"), "method call with too few arguments is rejected");

    check(sema_err(
        "Calc { add(a: int) { put(a); } }\n"
        "c = Calc.new;\n"
        "c.add(\"x\");\n"), "method call with wrong argument type is rejected");

    check(sema_err(
        "Calc { add(a: int) { put(a); } }\n"
        "c = Calc.new;\n"
        "c.add;\n"), "parameterised method used without a call is rejected");
}

static void test_duplicate_parameter(void) {
    check(sema_err("C { m(a: int, a: int) { put(a); } }\n"),
          "duplicate parameter is rejected");
}

static void test_function_semantics(void) {
    check(sema_ok("fn add(a: int, b: int): int { return a + b; }\n"
                  "x = add(2, 3);\n"),
          "declared function call passes");
    check(sema_err("fn add(a: int): int { return a; }\n"
                   "x = add();\n"),
          "function arity mismatch is rejected");
    check(sema_err("if (true) { scoped = 1; }\nput(scoped);\n"),
          "branch-local variable does not escape its scope");
}

static void test_duplicate_field(void) {
    check(sema_err("Box { x: int; x: str; }\n"),
          "duplicate field is rejected");
}

int main(void) {
    test_hello();
    test_assign_then_use();
    test_even_odd();
    test_class();
    test_method_params();
    test_forward_ref();
    test_nested_scope();
    test_if_else();
    test_builtins();
    test_module();
    test_undefined_var();
    test_bad_lvalue();
    test_arity_mismatch();
    test_return_outside_method();
    test_constant_divmod_zero();
    test_duplicate_class();
    test_unknown_member();
    test_duplicate_field();
    test_many_variables();
    test_field_assignment_types();
    test_method_signatures();
    test_duplicate_parameter();
    test_function_semantics();

    if (failures != 0) {
        fprintf(stderr, "%d sema test(s) failed\n", failures);
        return 1;
    }
    puts("sema: all tests passed");
    return 0;
}
