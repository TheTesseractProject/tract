#include "engine/frontend/ast.h"
#include "engine/frontend/lexer.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static tess_program *parse_ok(const char *name, const char *src) {
    tess_program *program = tess_parse(src, strlen(src), name);
    if (program == NULL) {
        fprintf(stderr, "FAIL: could not parse %s\n", name);
        failures++;
    }
    return program;
}

static tess_stmt *stmt_at(tess_program *program, size_t index,
                          const char *what) {
    if (program == NULL || index >= program->count) {
        fprintf(stderr, "FAIL: %s: statement %zu is missing\n", what, index);
        failures++;
        return NULL;
    }
    return program->items[index];
}

static void test_lexer(void) {
    const char *src = "@gtk.new_window(); // tail\n";
    tess_lexer lx;
    tess_lexer_init(&lx, src, strlen(src));
    tess_token_kind want[] = {
        TESS_TOK_AT,     TESS_TOK_IDENT,  TESS_TOK_DOT,    TESS_TOK_IDENT,
        TESS_TOK_LPAREN, TESS_TOK_RPAREN, TESS_TOK_SEMI,   TESS_TOK_EOF,
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        tess_token tok;
        bool ok = tess_lexer_next(&lx, &tok);
        check(ok, "lexer keeps producing tokens");
        check(tok.kind == want[i], "lexer token kind");
    }
}

static void test_hello(void) {
    tess_program *program =
        parse_ok("hello.t", "put(\"Hello, tesseract!\");\n");
    check(stmt_at(program, 0, "hello") != NULL, "hello statement");
    if (program != NULL && program->count == 1) {
        tess_stmt *stmt = program->items[0];
        check(stmt->kind == TESS_STMT_EXPR, "hello is an expression statement");
        tess_expr *call = stmt->as.expr;
        check(call->kind == TESS_EXPR_CALL, "hello is a call");
        check(call->as.call.callee->kind == TESS_EXPR_IDENT, "callee is a name");
        check(strcmp(call->as.call.callee->as.ident, "put") == 0,
              "callee is put");
        check(call->as.call.arg_count == 1, "put takes one argument");
        tess_expr *arg = call->as.call.args[0];
        check(arg->kind == TESS_EXPR_STRING, "argument is a string");
        check(arg->as.string.len == 17, "hello string length");
        check(memcmp(arg->as.string.data, "Hello, tesseract!", 17) == 0,
              "hello string bytes");
    }
    tess_program_free(program);
}

static void test_even_odd(void) {
    tess_program *program = parse_ok(
        "even_odd.t", "num = 2;\nput(!(num%2) ? \"Even\" : \"Odd\");\n");
    check(program != NULL && program->count == 2, "even_odd statement count");
    tess_stmt *assign = stmt_at(program, 0, "even_odd");
    if (assign != NULL) {
        check(assign->kind == TESS_STMT_ASSIGN, "num is assigned");
        check(assign->as.assign.target->kind == TESS_EXPR_IDENT,
              "num target is a name");
        check(assign->as.assign.value->kind == TESS_EXPR_INT,
              "num value is an integer");
        check(assign->as.assign.value->as.int_value == 2, "num value is 2");
    }
    tess_stmt *expr = stmt_at(program, 1, "even_odd");
    if (expr != NULL && expr->kind == TESS_STMT_EXPR) {
        tess_expr *call = expr->as.expr;
        check(call->kind == TESS_EXPR_CALL, "even_odd calls put");
        check(call->as.call.arg_count == 1, "even_odd one argument");
        tess_expr *ternary = call->as.call.args[0];
        check(ternary->kind == TESS_EXPR_TERNARY, "argument is a ternary");
        tess_expr *cond = ternary->as.ternary.cond;
        check(cond->kind == TESS_EXPR_UNARY, "condition is a unary");
        check(cond->as.unary.op == TESS_UN_NOT, "condition is a negation");
        tess_expr *mod = cond->as.unary.operand;
        check(mod->kind == TESS_EXPR_BINARY, "negation wraps a binary");
        check(mod->as.binary.op == TESS_BIN_MOD, "operator is modulo");
        check(ternary->as.ternary.then_expr->kind == TESS_EXPR_STRING,
              "then branch is a string");
        check(ternary->as.ternary.else_expr->kind == TESS_EXPR_STRING,
              "else branch is a string");
    }
    tess_program_free(program);
}

static void test_class(void) {
    const char *src =
        "TestClass {\n"
        "    a: int;\n"
        "    b: str;\n"
        "    c {\n"
        "        put(\"Hello!\");\n"
        "    }\n"
        "    show(prefix: str) {\n"
        "        put(prefix);\n"
        "        put(b);\n"
        "    }\n"
        "}\n"
        "t = TestClass.new;\n"
        "t.b = \"world\";\n"
        "t.c;\n"
        "t.show(\"> \");\n";
    tess_program *program = parse_ok("class.t", src);
    check(program != NULL && program->count == 5, "class statement count");
    tess_stmt *decl = stmt_at(program, 0, "class");
    if (decl != NULL) {
        check(decl->kind == TESS_STMT_CLASS, "first statement is a class");
        tess_class *cls = decl->as.class_decl;
        check(strcmp(cls->name, "TestClass") == 0, "class name");
        check(cls->field_count == 2, "class field count");
        check(strcmp(cls->fields[0].name, "a") == 0, "first field name");
        check(strcmp(cls->fields[0].type, "int") == 0, "first field type");
        check(strcmp(cls->fields[1].name, "b") == 0, "second field name");
        check(strcmp(cls->fields[1].type, "str") == 0, "second field type");
        check(cls->method_count == 2, "class method count");
        check(strcmp(cls->methods[0].name, "c") == 0, "first method name");
        check(cls->methods[0].param_count == 0, "first method params");
        check(cls->methods[0].body->kind == TESS_STMT_BLOCK,
              "first method body");
        check(cls->methods[0].body->as.block.count == 1,
              "first method body size");
        check(strcmp(cls->methods[1].name, "show") == 0, "second method name");
        check(cls->methods[1].param_count == 1, "second method params");
        check(strcmp(cls->methods[1].params[0].name, "prefix") == 0,
              "method parameter name");
        check(strcmp(cls->methods[1].params[0].type, "str") == 0,
              "method parameter type");
    }
    tess_stmt *assign = stmt_at(program, 1, "class");
    if (assign != NULL) {
        check(assign->kind == TESS_STMT_ASSIGN, "t is assigned");
        check(assign->as.assign.value->kind == TESS_EXPR_MEMBER,
              "value is a member access");
        tess_expr *member = assign->as.assign.value;
        check(member->as.member.object->kind == TESS_EXPR_IDENT,
              "new is read from a class name");
        check(strcmp(member->as.member.name, "new") == 0, "member is new");
    }
    tess_stmt *field = stmt_at(program, 2, "class");
    if (field != NULL) {
        check(field->kind == TESS_STMT_ASSIGN, "field is assigned");
        check(field->as.assign.target->kind == TESS_EXPR_MEMBER,
              "field target is a member");
        check(strcmp(field->as.assign.target->as.member.name, "b") == 0,
              "field name is b");
    }
    tess_stmt *call = stmt_at(program, 4, "class");
    if (call != NULL && call->kind == TESS_STMT_EXPR) {
        tess_expr *expr = call->as.expr;
        check(expr->kind == TESS_EXPR_CALL, "show is called");
        check(expr->as.call.callee->kind == TESS_EXPR_MEMBER,
              "show is a member");
        check(expr->as.call.arg_count == 1, "show takes one argument");
    }
    tess_program_free(program);
}

static void test_gtk(void) {
    const char *src =
        "window = @gtk.new_window;\n"
        "window.set_title(\"sosal\");\n"
        "window.set_size(480, 360);\n"
        "window.show;\n";
    tess_program *program = parse_ok("gtk.t", src);
    check(program != NULL && program->count == 4, "gtk statement count");
    tess_stmt *assign = stmt_at(program, 0, "gtk");
    if (assign != NULL) {
        tess_expr *member = assign->as.assign.value;
        check(member->kind == TESS_EXPR_MEMBER, "gtk value is a member");
        check(member->as.member.object->kind == TESS_EXPR_MODULE,
              "gtk receiver is a module");
        check(strcmp(member->as.member.object->as.module, "gtk") == 0,
              "module name is gtk");
        check(strcmp(member->as.member.name, "new_window") == 0,
              "member name is new_window");
    }
    tess_stmt *size = stmt_at(program, 2, "gtk");
    if (size != NULL) {
        tess_expr *call = size->as.expr;
        check(call->kind == TESS_EXPR_CALL, "set_size is called");
        check(call->as.call.arg_count == 2, "set_size takes two arguments");
    }
    tess_program_free(program);
}

static void test_control_flow(void) {
    const char *src =
        "if (true && !false) { put(\"a\"); } else { put(\"b\"); }\n"
        "while (1 < 2) { return null; }\n";
    tess_program *program = parse_ok("flow.t", src);
    check(program != NULL && program->count == 2, "flow statement count");
    tess_stmt *cond = stmt_at(program, 0, "flow");
    if (cond != NULL) {
        check(cond->kind == TESS_STMT_IF, "first statement is if");
        check(cond->as.if_stmt.cond->kind == TESS_EXPR_BINARY,
              "if condition is a binary");
        check(cond->as.if_stmt.cond->as.binary.op == TESS_BIN_AND,
              "if condition is a conjunction");
        check(cond->as.if_stmt.then_branch->kind == TESS_STMT_BLOCK,
              "then branch is a block");
        check(cond->as.if_stmt.else_branch->kind == TESS_STMT_BLOCK,
              "else branch is a block");
    }
    tess_stmt *loop = stmt_at(program, 1, "flow");
    if (loop != NULL) {
        check(loop->kind == TESS_STMT_WHILE, "second statement is while");
        check(loop->as.while_stmt.body->kind == TESS_STMT_BLOCK,
              "loop body is a block");
        check(loop->as.while_stmt.body->as.block.count == 1,
              "loop body has one statement");
        check(loop->as.while_stmt.body->as.block.items[0]->kind ==
                  TESS_STMT_RETURN,
              "loop body returns");
    }
    tess_program_free(program);
}

static void test_escapes(void) {
    tess_program *program =
        parse_ok("escapes.t", "put(\"a\\n\\t\\\"\\\\\\x41\");");
    if (program != NULL && program->count == 1) {
        tess_expr *call = program->items[0]->as.expr;
        tess_expr *arg = call->as.call.args[0];
        check(arg->as.string.len == 6, "escaped string length");
        check(memcmp(arg->as.string.data, "a\n\t\"\\A", 6) == 0,
              "escaped string bytes");
    }
    tess_program_free(program);
}

static void test_deep_nesting(void) {
    /* Deeply nested expressions must be rejected instead of exhausting the
     * parser stack. */
    char src[6000];
    size_t n = 0;
    for (int i = 0; i < 2000; i++) {
        n += (size_t)snprintf(src + n, sizeof src - n, "%s", "(");
    }
    n += (size_t)snprintf(src + n, sizeof src - n, "%s", "1");
    for (int i = 0; i < 2000; i++) {
        n += (size_t)snprintf(src + n, sizeof src - n, "%s", ")");
    }
    src[n] = '\0';
    check(tess_parse(src, n, "deep.t") == NULL, "deep nesting is rejected");
}

static void test_errors(void) {
    check(tess_parse("put(\"x\")", strlen("put(\"x\")"), "missing_semi.t") ==
              NULL,
          "missing semicolon is rejected");
    check(tess_parse("TestClass { a: int;", strlen("TestClass { a: int;"),
                     "missing_brace.t") == NULL,
          "missing brace is rejected");
    check(tess_parse("put(\"x);", strlen("put(\"x);"), "unterminated.t") ==
              NULL,
          "unterminated string is rejected");
    check(tess_parse("put(\"\\q\");", strlen("put(\"\\q\");"), "bad_escape.t") ==
              NULL,
          "unknown escape is rejected");
    check(tess_parse("put(#);", strlen("put(#);"), "bad_char.t") == NULL,
          "unexpected character is rejected");
}

int main(void) {
    test_lexer();
    test_hello();
    test_even_odd();
    test_class();
    test_gtk();
    test_control_flow();
    test_escapes();
    test_errors();
    test_deep_nesting();
    if (failures != 0) {
        fprintf(stderr, "%d frontend failure(s)\n", failures);
        return 1;
    }
    puts("frontend ok");
    return 0;
}
