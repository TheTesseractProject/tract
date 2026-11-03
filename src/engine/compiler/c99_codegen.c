#include "c99_codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include "../../constants.h"

/* Runtime helpers referenced by the generated program. Bits are set at
 * emission time, when the argument types are already known, and the runtime
 * block is built from this mask after the program body is generated. */
enum {
    RT_PUT_INT    = 1u << 0,
    RT_PUT_FLOAT  = 1u << 1,
    RT_PUT_STR    = 1u << 2,
    RT_CONCAT     = 1u << 3,
    RT_LEN        = 1u << 4,
    RT_STR_INT    = 1u << 5,
    RT_STR_FLOAT  = 1u << 6,
    RT_INT_FLOAT  = 1u << 7,
    RT_INT_STR    = 1u << 8,
    RT_FLOAT_INT  = 1u << 9,
    RT_FLOAT_STR  = 1u << 10,
    RT_ASSERT     = 1u << 11,
    RT_INPUT      = 1u << 12
};

/**
 * @brief Ensure buffer has enough space
 */
static bool ensure_capacity(c99_codegen *gen, size_t extra) {
    if (!gen || extra == 0) return true;
    /* Include one byte for the trailing NUL and reject arithmetic wraparound
     * before computing the new allocation size. */
    if (extra > SIZE_MAX - gen->size - 1) return false;
    size_t needed = gen->size + extra + 1;
    if (needed > gen->capacity) {
        size_t new_capacity = gen->capacity ? gen->capacity : 64;
        while (new_capacity < needed) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = needed;
                break;
            }
            new_capacity *= 2;
        }
        char *nb = realloc(gen->buffer, new_capacity);
        if (!nb) return false;
        gen->buffer = nb;
        gen->capacity = new_capacity;
    }
    return true;
}

/**
 * @brief Internal helper: append string to buffer
 */
static bool append_str(c99_codegen *gen, const char *str) {
    if (!gen || !str) { if (gen) gen->failed = true; return false; }
    
    size_t len = strlen(str);
    if (!ensure_capacity(gen, len)) { gen->failed = true; return false; }
    
    memcpy(gen->buffer + gen->size, str, len);
    gen->size += len;
    return true;
}

/**
 * @brief Internal helper: append formatted string
 */
static bool append_fprintf(c99_codegen *gen, const char *format, ...) {
    if (!gen || !format) return false;
    
    va_list args;
    va_start(args, format);
    
    // First pass: determine required size
    int written = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (written < 0) {
        if (gen) gen->failed = true;
        return false;
    }
    
    size_t needed = (size_t)written + 1;
    if (!ensure_capacity(gen, needed)) {
        gen->failed = true;
        return false;
    }
    
    va_start(args, format);
    int actual_written = vsnprintf(gen->buffer + gen->size, gen->capacity - gen->size, format, args);
    va_end(args);
    
    if (actual_written < 0) { gen->failed = true; return false; }
    gen->size += (size_t)actual_written;
    return true;
}

/**
 * @brief Internal helper: add indentation
 */
/* Emit a Tesseract string literal as a valid C string literal, escaping every
 * byte that is not safely printable ASCII. */
static void append_c_string_literal(c99_codegen *gen, const char *data,
                                    size_t len) {
    if (data == NULL && len != 0) {
        gen->failed = true;
        return;
    }
    append_str(gen, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        switch (c) {
            case '"': append_str(gen, "\\\""); break;
            case '\\': append_str(gen, "\\\\"); break;
            case '\n': append_str(gen, "\\n"); break;
            case '\t': append_str(gen, "\\t"); break;
            case '\r': append_str(gen, "\\r"); break;
            default:
                if (c == 0) {
                    append_str(gen, "\\000");
                } else if (c < 0x20 || c >= 0x7f) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\%03o", c);
                    append_str(gen, buf);
                } else {
                    char buf[2] = { (char)c, '\0' };
                    append_str(gen, buf);
                }
                break;
        }
    }
    append_str(gen, "\"");
}

static void append_indent(c99_codegen *gen) {
    int level = gen->indent_level > 256 ? 256 : gen->indent_level;
    for (int i = 0; i < level; i++) {
        append_str(gen, "    ");  // 4-space indent
    }
}

static void reset_function_state(c99_codegen *gen) {
    /* Name/type tracking is local to one generated C function. */
    gen->declared_count = 0;
    gen->var_count = 0;
    gen->in_function = false;
    gen->fn_returns_void = false;
}

/* C reserves a set of keywords the Tesseract lexer does not, so a Tesseract
 * identifier may collide with one. Emit such names with a trailing underscore
 * to keep the generated C valid. */
static bool is_c_keyword(const char *name) {
    static const char *const keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
        "union", "unsigned", "void", "volatile", "while", "_Bool",
    };
    if (name == NULL) return false;
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (strcmp(keywords[i], name) == 0) return true;
    }
    return false;
}

static void append_ident(c99_codegen *gen, const char *name) {
    append_str(gen, name);
    if (is_c_keyword(name)) append_str(gen, "_");
}

static bool is_declared_var(c99_codegen *gen, const char *name) {
    for (size_t i = 0; i < gen->declared_count; i++) {
        if (strcmp(gen->declared[i], name) == 0) return true;
    }
    return false;
}

static void mark_declared_var(c99_codegen *gen, const char *name) {
    if (is_declared_var(gen, name)) return;
    if (gen->declared_count == gen->declared_cap) {
        size_t cap = gen->declared_cap == 0 ? 8 : gen->declared_cap * 2;
        char **arr = realloc(gen->declared, cap * sizeof(char*));
        if (!arr) { gen->failed = true; return; }
        gen->declared = arr;
        gen->declared_cap = cap;
    }
    gen->declared[gen->declared_count++] = (char*)name;
}

static const char* get_var_class(c99_codegen *gen, const char *name) {
    if (!gen || !name) return NULL;
    for (size_t i = 0; i < gen->var_count; i++) {
        if (strcmp(gen->var_names[i], name) == 0) return gen->var_classes[i];
    }
    return NULL;
}

static bool expr_is_string_like(c99_codegen *gen, tess_expr *e);
static bool expr_is_float_like(c99_codegen *gen, tess_expr *e);
static bool is_class_name(c99_codegen *gen, const char *name);
static tess_field *find_field(tess_class *cls, const char *name);
static tess_class *find_class(c99_codegen *gen, const char *name);
static const char *expr_class(c99_codegen *gen, tess_expr *e);
static const char *fn_ret_type_name(c99_codegen *gen, const char *name);
static const char *method_ret_type_name(c99_codegen *gen, tess_expr *obj_expr,
                                        const char *method_name);
static const char *expr_type_name(c99_codegen *gen, tess_expr *expr) {
    if (!expr) return "int";
    switch (expr->kind) {
        case TESS_EXPR_FLOAT: return "double";
        case TESS_EXPR_STRING: return "const char*";
        case TESS_EXPR_BOOL: return "bool";
        case TESS_EXPR_NULL: return "void*";
        case TESS_EXPR_IDENT: {
            /* This is intentionally lightweight inference for declaration
             * spelling; semantic analysis remains the source of type errors. */
            const char *cls = get_var_class(gen, expr->as.ident);
            if (cls && is_class_name(gen, cls)) return cls;
            if (cls && (strcmp(cls, "str") == 0 || strcmp(cls, "string") == 0))
                return "const char*";
            if (cls && (strcmp(cls, "float") == 0 || strcmp(cls, "f64") == 0))
                return "double";
            if (gen->current_class != NULL) {
                tess_field *field = find_field(
                    find_class(gen, gen->current_class), expr->as.ident);
                if (field != NULL) {
                    if (strcmp(field->type, "str") == 0 ||
                        strcmp(field->type, "string") == 0) return "const char*";
                    if (strcmp(field->type, "float") == 0 ||
                        strcmp(field->type, "f64") == 0) return "double";
                    if (is_class_name(gen, field->type)) return "void*";
                }
            }
            return "long long";
        }
        case TESS_EXPR_CALL:
            if (expr->as.call.callee &&
                expr->as.call.callee->kind == TESS_EXPR_IDENT) {
                const char *name = expr->as.call.callee->as.ident;
                if (strcmp(name, "str") == 0 || strcmp(name, "input") == 0)
                    return "const char*";
                if (strcmp(name, "float") == 0) return "double";
                const char *ret = fn_ret_type_name(gen, name);
                if (ret != NULL) {
                    if (strcmp(ret, "str") == 0 || strcmp(ret, "string") == 0)
                        return "const char*";
                    if (strcmp(ret, "float") == 0 || strcmp(ret, "f64") == 0)
                        return "double";
                    if (is_class_name(gen, ret)) return ret;
                }
            } else if (expr->as.call.callee &&
                       expr->as.call.callee->kind == TESS_EXPR_MEMBER) {
                const char *ret = method_ret_type_name(
                    gen, expr->as.call.callee->as.member.object,
                    expr->as.call.callee->as.member.name);
                if (ret != NULL) {
                    if (strcmp(ret, "str") == 0 || strcmp(ret, "string") == 0)
                        return "const char*";
                    if (strcmp(ret, "float") == 0 || strcmp(ret, "f64") == 0)
                        return "double";
                    if (is_class_name(gen, ret)) return ret;
                }
            }
            return "long long";
        case TESS_EXPR_MEMBER: {
            /* Field copy: `v = q.p` has the declared type of the field. */
            if (expr->as.member.object != NULL &&
                expr->as.member.name != NULL) {
                const char *cls = expr_class(gen, expr->as.member.object);
                tess_field *field = cls != NULL
                    ? find_field(find_class(gen, cls), expr->as.member.name)
                    : NULL;
                if (field != NULL && field->type != NULL) {
                    if (strcmp(field->type, "str") == 0 ||
                        strcmp(field->type, "string") == 0)
                        return "const char*";
                    if (strcmp(field->type, "float") == 0 ||
                        strcmp(field->type, "f64") == 0)
                        return "double";
                    if (is_class_name(gen, field->type)) return field->type;
                } else if (field == NULL) {
                    /* A bare method reference (`n = a.get;`) calls it. */
                    const char *ret = method_ret_type_name(
                        gen, expr->as.member.object, expr->as.member.name);
                    if (ret != NULL) {
                        if (strcmp(ret, "str") == 0 ||
                            strcmp(ret, "string") == 0)
                            return "const char*";
                        if (strcmp(ret, "float") == 0 ||
                            strcmp(ret, "f64") == 0)
                            return "double";
                        if (is_class_name(gen, ret)) return ret;
                    }
                }
            }
            return "long long";
        }
        case TESS_EXPR_BINARY:
            if (expr_is_string_like(gen, expr)) return "const char*";
            if (expr_is_float_like(gen, expr)) return "double";
            return "long long";
        case TESS_EXPR_UNARY:
            return expr_type_name(gen, expr->as.unary.operand);
        case TESS_EXPR_TERNARY:
            return expr_type_name(gen, expr->as.ternary.then_expr);
        default: return "long long";
    }
}

static void record_var_class(c99_codegen *gen, const char *name, const char *cls) {
    if (!gen || !name || !cls) return;
    for (size_t i = 0; i < gen->var_count; i++) {
        if (strcmp(gen->var_names[i], name) == 0) {
            gen->var_classes[i] = (char*)cls;
            return;
        }
    }
    if (gen->var_count == gen->var_cap) {
        size_t cap = gen->var_cap == 0 ? 8 : gen->var_cap * 2;
        char **names = realloc(gen->var_names, cap * sizeof(char*));
        if (!names) { gen->failed = true; return; }
        gen->var_names = names;
        char **classes = realloc(gen->var_classes, cap * sizeof(char*));
        if (!classes) { gen->failed = true; return; }
        gen->var_classes = classes;
        gen->var_cap = cap;
    }
    gen->var_names[gen->var_count] = (char*)name;
    gen->var_classes[gen->var_count] = (char*)cls;
    gen->var_count++;
}

static void forget_var_class(c99_codegen *gen, const char *name) {
    if (!gen || !name) return;
    for (size_t i = 0; i < gen->var_count; i++) {
        if (strcmp(gen->var_names[i], name) == 0) {
            /* Swap-remove keeps updates O(1) after the lookup. */
            gen->var_names[i] = gen->var_names[gen->var_count - 1];
            gen->var_classes[i] = gen->var_classes[gen->var_count - 1];
            gen->var_count--;
            return;
        }
    }
}

static bool is_class_name(c99_codegen *gen, const char *name) {
    if (!gen || !gen->program || !name) return false;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st && st->kind == TESS_STMT_CLASS && st->as.class_decl &&
            strcmp(st->as.class_decl->name, name) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Convert Tesseract type name to C99 type
 */
static const char* tess_type_to_c(const char *tess_type) {
    if (!tess_type) return "void";
    
    if (strcmp(tess_type, "int") == 0 || strcmp(tess_type, "i32") == 0) {
        return "int";
    } else if (strcmp(tess_type, "float") == 0 || strcmp(tess_type, "f64") == 0) {
        return "double";
    } else if (strcmp(tess_type, "bool") == 0 || strcmp(tess_type, "boolean") == 0) {
        return "bool";
    } else if (strcmp(tess_type, "str") == 0 || strcmp(tess_type, "string") == 0) {
        return "char*";
    } else if (strcmp(tess_type, "null") == 0 || strcmp(tess_type, "None") == 0) {
        return "void*";
    }
    
    // Assume it's a class type (user-defined)
    return tess_type;
}

/**
 * @brief True when the type name is a built-in scalar/string type.
 */
static bool is_builtin_type_name(const char *tess_type) {
    if (!tess_type) return false;
    return strcmp(tess_type, "int") == 0 || strcmp(tess_type, "i32") == 0 ||
           strcmp(tess_type, "float") == 0 || strcmp(tess_type, "f64") == 0 ||
           strcmp(tess_type, "bool") == 0 || strcmp(tess_type, "boolean") == 0 ||
           strcmp(tess_type, "str") == 0 || strcmp(tess_type, "string") == 0 ||
           strcmp(tess_type, "null") == 0 || strcmp(tess_type, "None") == 0;
}

/**
 * @brief Emit a C declarator: "<type><* if class> <name>"
 *
 * Instances of user-defined classes are heap objects, so fields and
 * parameters of class type are pointers.
 */
static void append_declarator(c99_codegen *gen, const char *tess_type,
                              const char *name) {
    const char *c_type = tess_type_to_c(tess_type);
    append_fprintf(gen, "%s%s ", c_type,
                   is_builtin_type_name(tess_type) ? "" : "*");
    append_ident(gen, name);
}

static bool expr_is_string_like(c99_codegen *gen, tess_expr *e);
static bool expr_is_float_like(c99_codegen *gen, tess_expr *e);
static tess_field *find_field(tess_class *cls, const char *name);
static tess_class *find_class(c99_codegen *gen, const char *name);
static const char *expr_class(c99_codegen *gen, tess_expr *e);
static bool class_has_method(c99_codegen *gen, const char *cls,
                             const char *method);
static const char *fn_ret_type_name(c99_codegen *gen, const char *name);
static const char *method_ret_type_name(c99_codegen *gen, tess_expr *obj_expr,
                                        const char *method_name);
static void c99_codegen_generate_class_init_helpers(c99_codegen *gen,
                                                     tess_program *program);

/**
 * @brief Check if an expression is a float literal or has float type
 */
static bool expr_is_float_like(c99_codegen *gen, tess_expr *e) {
    if (!e) return false;
    switch (e->kind) {
        case TESS_EXPR_FLOAT: return true;
        case TESS_EXPR_IDENT: {
            const char *cls = get_var_class(gen, e->as.ident);
            /* Bare field name inside a method (maps to self->field) */
            if (cls == NULL && gen->current_class) {
                tess_field *field = find_field(find_class(gen, gen->current_class),
                                               e->as.ident);
                if (field != NULL) cls = field->type;
            }
            return cls != NULL && (strcmp(cls, "float") == 0 ||
                                   strcmp(cls, "f64") == 0);
        }
        case TESS_EXPR_MEMBER: {
            /* Float-typed field access (self->b, t->value) */
            if (!e->as.member.object || !e->as.member.name) return false;
            const char *cls = expr_class(gen, e->as.member.object);
            if (cls == NULL) return false;
            tess_field *field = find_field(find_class(gen, cls), e->as.member.name);
            return field != NULL && field->type != NULL &&
                   (strcmp(field->type, "float") == 0 ||
                    strcmp(field->type, "f64") == 0);
        }
        case TESS_EXPR_CALL: {
            if (e->as.call.callee && e->as.call.callee->kind == TESS_EXPR_IDENT) {
                if (strcmp(e->as.call.callee->as.ident, "float") == 0) {
                    return true;
                }
                const char *ret = fn_ret_type_name(
                    gen, e->as.call.callee->as.ident);
                if (ret != NULL && (strcmp(ret, "float") == 0 ||
                                    strcmp(ret, "f64") == 0)) {
                    return true;
                }
            } else if (e->as.call.callee &&
                       e->as.call.callee->kind == TESS_EXPR_MEMBER) {
                const char *ret = method_ret_type_name(
                    gen, e->as.call.callee->as.member.object,
                    e->as.call.callee->as.member.name);
                if (ret != NULL && (strcmp(ret, "float") == 0 ||
                                    strcmp(ret, "f64") == 0)) {
                    return true;
                }
            }
            return false;
        }
        case TESS_EXPR_UNARY:
            return expr_is_float_like(gen, e->as.unary.operand);
        case TESS_EXPR_BINARY: {
            /* Arithmetic expressions with at least one float operand */
            bool lhs_float = expr_is_float_like(gen, e->as.binary.lhs);
            bool rhs_float = expr_is_float_like(gen, e->as.binary.rhs);
            return (lhs_float || rhs_float) &&
                   (e->as.binary.op == TESS_BIN_ADD || e->as.binary.op == TESS_BIN_SUB ||
                    e->as.binary.op == TESS_BIN_MUL || e->as.binary.op == TESS_BIN_DIV ||
                    e->as.binary.op == TESS_BIN_MOD);
        }
        default: return false;
    }
}

static bool expr_is_string_like(c99_codegen *gen, tess_expr *e) {
    if (!e) return false;
    switch (e->kind) {
        case TESS_EXPR_STRING: return true;
        case TESS_EXPR_IDENT: {
            const char *cls = get_var_class(gen, e->as.ident);
            /* Bare field name inside a method (maps to self->field) */
            if (cls == NULL && gen->current_class) {
                tess_field *field = find_field(find_class(gen, gen->current_class),
                                               e->as.ident);
                if (field != NULL) cls = field->type;
            }
            return cls != NULL && strcmp(cls, "str") == 0;
        }
        case TESS_EXPR_MEMBER: {
            /* String-typed field access (self->b, t->name), or a bare method
             * reference whose declared return type is str. */
            if (!e->as.member.object || !e->as.member.name) return false;
            const char *cls = expr_class(gen, e->as.member.object);
            if (cls == NULL) return false;
            tess_field *field = find_field(find_class(gen, cls), e->as.member.name);
            if (field != NULL) {
                return field->type != NULL &&
                       (strcmp(field->type, "str") == 0 ||
                        strcmp(field->type, "string") == 0);
            }
            const char *ret = method_ret_type_name(
                gen, e->as.member.object, e->as.member.name);
            return ret != NULL && (strcmp(ret, "str") == 0 ||
                                   strcmp(ret, "string") == 0);
        }
        case TESS_EXPR_BINARY:
            if (e->as.binary.op != TESS_BIN_ADD) return false;
            return expr_is_string_like(gen, e->as.binary.lhs) &&
                   expr_is_string_like(gen, e->as.binary.rhs);
        case TESS_EXPR_CALL: {
            if (e->as.call.callee && e->as.call.callee->kind == TESS_EXPR_IDENT) {
                const char *name = e->as.call.callee->as.ident;
                if (strcmp(name, "str") == 0 || strcmp(name, "input") == 0) {
                    return true;
                }
                const char *ret = fn_ret_type_name(gen, name);
                if (ret != NULL && (strcmp(ret, "str") == 0 ||
                                    strcmp(ret, "string") == 0)) {
                    return true;
                }
            } else if (e->as.call.callee &&
                       e->as.call.callee->kind == TESS_EXPR_MEMBER) {
                const char *ret = method_ret_type_name(
                    gen, e->as.call.callee->as.member.object,
                    e->as.call.callee->as.member.name);
                if (ret != NULL && (strcmp(ret, "str") == 0 ||
                                    strcmp(ret, "string") == 0)) {
                    return true;
                }
            }
            return false;
        }
        case TESS_EXPR_TERNARY:
            return expr_is_string_like(gen, e->as.ternary.then_expr) &&
                   expr_is_string_like(gen, e->as.ternary.else_expr);
        default: return false;
    }
}

/* If `value` constructs an instance of a known class, return its name. */
static const char *construction_class(c99_codegen *gen, tess_expr *value) {
    if (value == NULL) return NULL;
    tess_expr *member = NULL;
    if (value->kind == TESS_EXPR_CALL &&
        value->as.call.callee != NULL &&
        value->as.call.callee->kind == TESS_EXPR_MEMBER) {
        member = value->as.call.callee;
    } else if (value->kind == TESS_EXPR_MEMBER) {
        member = value;
    }
    if (member == NULL || member->as.member.name == NULL) return NULL;
    if (strcmp(member->as.member.name, "new") != 0 &&
        strcmp(member->as.member.name, "init") != 0) {
        return NULL;
    }
    tess_expr *obj = member->as.member.object;
    if (obj == NULL || obj->kind != TESS_EXPR_IDENT) return NULL;
    return is_class_name(gen, obj->as.ident) ? obj->as.ident : NULL;
}

static bool expr_is_null(c99_codegen *gen, tess_expr *e) {
    (void)gen;
    return e != NULL && e->kind == TESS_EXPR_NULL;
}

/* Find a declared field named `name` on class `cls`. */
static tess_field *find_field(tess_class *cls, const char *name) {
    if (cls == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < cls->field_count; i++) {
        if (strcmp(cls->fields[i].name, name) == 0) return &cls->fields[i];
    }
    return NULL;
}

/* Find a declared class by name. */
static tess_class *find_class(c99_codegen *gen, const char *name) {
    if (gen == NULL || gen->program == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st && st->kind == TESS_STMT_CLASS && st->as.class_decl &&
            strcmp(st->as.class_decl->name, name) == 0) {
            return st->as.class_decl;
        }
    }
    return NULL;
}

/* True when `cls` declares a method named `method`. */
static bool class_has_method(c99_codegen *gen, const char *cls,
                             const char *method) {
    if (cls == NULL || method == NULL) return false;
    tess_class *c = find_class(gen, cls);
    if (c == NULL) return false;
    for (size_t i = 0; i < c->method_count; i++) {
        if (strcmp(c->methods[i].name, method) == 0) return true;
    }
    return false;
}

/* Declared return type (Tesseract-level) of the top-level fn `name`, or NULL
 * when no such fn exists or it returns nothing. */
static const char *fn_ret_type_name(c99_codegen *gen, const char *name) {
    if (gen == NULL || gen->program == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st && st->kind == TESS_STMT_FN &&
            strcmp(st->as.fn.name, name) == 0) {
            return st->as.fn.ret_type;
        }
    }
    return NULL;
}

/* Declared return type of the method `method_name` called on `obj_expr`, or
 * NULL when the receiver or method is unknown / returns nothing. */
static const char *method_ret_type_name(c99_codegen *gen, tess_expr *obj_expr,
                                        const char *method_name) {
    if (obj_expr == NULL || method_name == NULL) return NULL;
    const char *cls = expr_class(gen, obj_expr);
    if (cls == NULL) return NULL;
    tess_class *c = find_class(gen, cls);
    if (c == NULL) return NULL;
    for (size_t i = 0; i < c->method_count; i++) {
        if (strcmp(c->methods[i].name, method_name) == 0) {
            return c->methods[i].ret_type;
        }
    }
    return NULL;
}

/* Static class of an object expression, or NULL when it is not a known class
 * instance. Handles `self`, tracked variables (including `__auto_type`
 * temporaries holding a member of a class-typed field) and `A.new` values. */
static const char *expr_class(c99_codegen *gen, tess_expr *e) {
    if (e == NULL) return NULL;
    if (e->kind == TESS_EXPR_IDENT) {
        if (strcmp(e->as.ident, "self") == 0) return gen->current_class;
        const char *cls = get_var_class(gen, e->as.ident);
        if (cls != NULL) return cls;
        if (is_class_name(gen, e->as.ident)) return e->as.ident;
        /* Bare field of the current class (`other.setx(...)` in a method). */
        if (gen->current_class != NULL) {
            tess_field *field = find_field(find_class(gen, gen->current_class),
                                           e->as.ident);
            if (field != NULL) return field->type;
        }
        return NULL;
    }
    if (e->kind == TESS_EXPR_MEMBER) {
        const char *owner = expr_class(gen, e->as.member.object);
        if (owner == NULL) return NULL;
        tess_field *field = find_field(find_class(gen, owner), e->as.member.name);
        return field != NULL ? field->type : NULL;
    }
    return construction_class(gen, e);
}

static bool is_field_of_current_class(c99_codegen *gen, const char *name) {
    if (!gen || !gen->current_class || !name || !gen->program) return false;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st && st->kind == TESS_STMT_CLASS && st->as.class_decl &&
            strcmp(st->as.class_decl->name, gen->current_class) == 0) {
            tess_class *cls = st->as.class_decl;
            for (size_t j = 0; j < cls->field_count; j++) {
                if (strcmp(cls->fields[j].name, name) == 0) return true;
            }
            return false;
        }
    }
    return false;
}

/* Imported module alias -> module key resolution. */
static bool module_add(c99_codegen *gen, const char *alias, const char *target) {
    if (!gen || !alias || !target) return true;
    for (size_t i = 0; i < gen->module_alias_count; i++) {
        if (strcmp(gen->module_aliases[i], alias) == 0) {
            char *replacement = strdup(target);
            if (!replacement) { gen->failed = true; return false; }
            free(gen->module_targets[i]);
            gen->module_targets[i] = replacement;
            return true;
        }
    }
    if (gen->module_alias_count == gen->module_alias_cap) {
        size_t cap = gen->module_alias_cap == 0 ? 8 : gen->module_alias_cap * 2;
        char **names = realloc(gen->module_aliases, cap * sizeof(char*));
        if (!names) { gen->failed = true; return false; }
        gen->module_aliases = names;
        char **tgts = realloc(gen->module_targets, cap * sizeof(char*));
        if (!tgts) { gen->failed = true; return false; }
        gen->module_targets = tgts;
        gen->module_alias_cap = cap;
    }
    char *al = strdup(alias);
    char *tg = strdup(target);
    if (!al || !tg) { free(al); free(tg); gen->failed = true; return false; }
    gen->module_aliases[gen->module_alias_count] = al;
    gen->module_targets[gen->module_alias_count] = tg;
    gen->module_alias_count++;
    return true;
}

static const char *module_resolve(c99_codegen *gen, const char *alias) {
    if (!gen || !gen->module_aliases || !gen->module_targets || !alias) {
        return NULL;
    }
    for (size_t i = 0; i < gen->module_alias_count; i++) {
        if (strcmp(gen->module_aliases[i], alias) == 0) {
            return gen->module_targets[i];
        }
    }
    return NULL;
}

/**
 * @brief Generate C99 code for an expression
 */
static void c99_codegen_expr(c99_codegen *gen, tess_expr *expr) {
    if (!gen || !expr) return;
    
    switch (expr->kind) {
        case TESS_EXPR_INT: {
            append_fprintf(gen, "%" PRId64, expr->as.int_value);
            break;
        }
        case TESS_EXPR_FLOAT: {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%.17g", expr->as.float_value);
            append_str(gen, tmp);
            if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E'))
                append_str(gen, ".0");
            break;
        }
        case TESS_EXPR_STRING: {
            append_c_string_literal(gen, expr->as.string.data,
                                    expr->as.string.len);
            break;
        }
        case TESS_EXPR_BOOL: {
            append_str(gen, expr->as.bool_value ? "true" : "false");
            break;
        }
        case TESS_EXPR_NULL: {
            append_str(gen, "NULL");
            break;
        }
        case TESS_EXPR_IDENT: {
            /* Parameters and locals shadow fields of the same name. */
            if (gen->current_class && !is_declared_var(gen, expr->as.ident) &&
                is_field_of_current_class(gen, expr->as.ident)) {
                append_str(gen, "self->");
            }
            append_ident(gen, expr->as.ident);
            break;
        }
        case TESS_EXPR_ASSIGN: {
            /* Assignment expressions are parenthesized by their surrounding
             * C context when required by the AST's precedence. */
            c99_codegen_expr(gen, expr->as.assign.target);
            append_str(gen, " = ");
            c99_codegen_expr(gen, expr->as.assign.value);
            break;
        }
        case TESS_EXPR_MEMBER: {
            if (!gen || !expr) return;
            const char *member = expr->as.member.name ? expr->as.member.name : "INVALID";
            tess_expr *obj_expr = expr->as.member.object;
            if (obj_expr && obj_expr->kind == TESS_EXPR_MODULE) {
                const char *mod = obj_expr->as.module;
                const char *mem = expr->as.member.name ? expr->as.member.name : "INVALID";
                /* An alias may hide a native module: resolve it before the
                 * whitelist check so `import c as libc` keeps working. */
                const char *resolved = module_resolve(gen, mod);
                if (resolved != NULL) mod = resolved;
                if (mod && (strcmp(mod, "c") == 0 || strcmp(mod, "libc") == 0 ||
                            strcmp(mod, "std") == 0 || strcmp(mod, "math") == 0)) {
                    append_str(gen, mem);
                } else {
                    append_fprintf(gen, "%s_%s", mod ? mod : "", mem);
                }
                break;
            }

            if (obj_expr && obj_expr->kind == TESS_EXPR_IDENT &&
                module_resolve(gen, obj_expr->as.ident) != NULL) {
                append_ident(gen, module_resolve(gen, obj_expr->as.ident));
                append_str(gen, "_");
                append_ident(gen, member);
                break;
            }

            /* `Class.new` / static member of a known class. */
            if (obj_expr && obj_expr->kind == TESS_EXPR_IDENT &&
                is_class_name(gen, obj_expr->as.ident)) {
                const char *cls = obj_expr->as.ident;
                if (strcmp(member, "new") == 0) {
                    append_fprintf(gen, "(%s*)calloc(1, sizeof(%s))", cls, cls);
                } else {
                    append_ident(gen, cls);
                    append_str(gen, "_");
                    append_ident(gen, member);
                }
                break;
            }

            /* Method referenced without parentheses (`t.run;`) is a call. */
            const char *cls = expr_class(gen, obj_expr);
            if (cls != NULL && class_has_method(gen, cls, member)) {
                bool static_call = obj_expr != NULL &&
                    obj_expr->kind == TESS_EXPR_IDENT &&
                    is_class_name(gen, obj_expr->as.ident);
                append_ident(gen, cls);
                append_str(gen, "_");
                append_ident(gen, member);
                append_str(gen, "(");
                if (obj_expr != NULL) {
                    if (static_call) append_str(gen, "NULL");
                    else c99_codegen_expr(gen, obj_expr);
                } else {
                    append_str(gen, "self");
                }
                append_str(gen, ")");
                break;
            }

            if (obj_expr) {
                append_str(gen, "(");
                c99_codegen_expr(gen, obj_expr);
                append_str(gen, ")->");
                append_ident(gen, member);
            } else {
                append_str(gen, "self->");
                append_ident(gen, member);
            }
            break;
        }
        case TESS_EXPR_CALL: {
            /* Handle member calls (methods) with dispatch. */
            if (expr->as.call.callee && expr->as.call.callee->kind == TESS_EXPR_MEMBER) {
                tess_expr *obj_expr = expr->as.call.callee->as.member.object;
                const char *method = expr->as.call.callee->as.member.name ? expr->as.call.callee->as.member.name : "INVALID";
                
                /* Special: ClassName.new / ClassName.init */
                if (obj_expr && obj_expr->kind == TESS_EXPR_IDENT && is_class_name(gen, obj_expr->as.ident)) {
                    const char *cls = obj_expr->as.ident;
                    if (strcmp(method, "new") == 0) {
                        append_fprintf(gen, "(%s*)calloc(1, sizeof(%s))", cls, cls);
                        break;
                    }
                    if (strcmp(method, "init") == 0) {
                        if (!class_has_method(gen, cls, "init")) {
                            /* Sema rejects this first; stay defensive here. */
                            gen->failed = true;
                            break;
                        }
                        append_ident(gen, cls);
                        append_str(gen, "_init_new(");
                        for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                            if (i != 0) append_str(gen, ", ");
                            if (expr->as.call.args[i]) c99_codegen_expr(gen, expr->as.call.args[i]);
                            else append_str(gen, "NULL");
                        }
                        append_str(gen, ")");
                        break;
                    }
                }
                
                /* Native C interop: @c.printf(...) -> printf(...) */
                if (obj_expr && obj_expr->kind == TESS_EXPR_MODULE) {
                    const char *mod = obj_expr->as.module;
                    const char *mem = method;
                    /* Resolve the alias before the native whitelist check. */
                    const char *resolved = module_resolve(gen, mod);
                    if (resolved != NULL) mod = resolved;
                    char fn[128];
                    if (mod && (strcmp(mod, "c") == 0 || strcmp(mod, "libc") == 0 ||
                                strcmp(mod, "std") == 0 || strcmp(mod, "math") == 0)) {
                        if (mem == NULL || strlen(mem) >= sizeof fn) {
                            gen->failed = true;
                            break;
                        }
                        snprintf(fn, sizeof fn, "%s", mem);
                    } else {
                        const char *target = module_resolve(gen, mod);
                        if (!target) { gen->failed = true; break; }
                        if (snprintf(fn, sizeof fn, "%s_%s", target, mem) >=
                            (int)sizeof fn) { gen->failed = true; break; }
                    }
                    append_str(gen, fn);
                    append_str(gen, "(");
                    for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                        if (i > 0) append_str(gen, ", ");
                        if (expr->as.call.args[i]) c99_codegen_expr(gen, expr->as.call.args[i]);
                        else append_str(gen, "NULL");
                    }
                    append_str(gen, ")");
                    break;
                }
                if (obj_expr && obj_expr->kind == TESS_EXPR_IDENT) {
                    const char *target = module_resolve(gen, obj_expr->as.ident);
                    if (target != NULL) {
                        if (strcmp(target, "c") == 0 || strcmp(target, "libc") == 0 ||
                            strcmp(target, "std") == 0 || strcmp(target, "math") == 0) {
                            append_str(gen, method);
                        } else {
                            append_ident(gen, target);
                            append_str(gen, "_");
                            append_ident(gen, method);
                        }
                        append_str(gen, "(");
                        for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                            if (i) append_str(gen, ", ");
                            c99_codegen_expr(gen, expr->as.call.args[i]);
                        }
                        append_str(gen, ")");
                        break;
                    }
                }
                const char *cls = expr_class(gen, obj_expr);
                if (!cls) {
                    gen->failed = true;
                    append_str(gen, "/* UNSUPPORTED_CALL */");
                    break;
                }
                if (cls) {
                    bool static_call = obj_expr != NULL &&
                                       obj_expr->kind == TESS_EXPR_IDENT &&
                                       is_class_name(gen, obj_expr->as.ident);
                    append_ident(gen, cls);
                    append_str(gen, "_");
                    append_ident(gen, method);
                    append_str(gen, "(");
                    if (static_call) {
                        /* No instance was created; pass a NULL receiver. */
                        append_str(gen, "NULL");
                    } else if (obj_expr) {
                        c99_codegen_expr(gen, obj_expr);
                    }
                    for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                        append_str(gen, ", ");
                        if (expr->as.call.args[i]) c99_codegen_expr(gen, expr->as.call.args[i]);
                        else append_str(gen, "NULL");
                    }
                    append_str(gen, ")");
                    break;
                }
            }
            
            const char *call_name = NULL;
            if (expr->as.call.callee && expr->as.call.callee->kind == TESS_EXPR_IDENT) {
                call_name = expr->as.call.callee->as.ident;
            }
            tess_expr *arg0 = expr->as.call.arg_count == 1
                                  ? expr->as.call.args[0]
                                  : NULL;

            if (!expr->as.call.callee) {
                gen->failed = true;
                append_str(gen, "/* UNSUPPORTED_CALL */");
            } else if (call_name != NULL && arg0 != NULL &&
                       (strcmp(call_name, "put") == 0 ||
                        strcmp(call_name, "print") == 0 ||
                        strcmp(call_name, "putln") == 0)) {
                /* Dispatch by argument type: put_int / put_float / put_str. */
                if (expr_is_string_like(gen, arg0) || expr_is_null(gen, arg0)) {
                    gen->runtime_used |= RT_PUT_STR;
                    append_str(gen, "put_str");
                } else if (expr_is_float_like(gen, arg0)) {
                    gen->runtime_used |= RT_PUT_FLOAT;
                    append_str(gen, "put_float");
                } else {
                    gen->runtime_used |= RT_PUT_INT;
                    append_str(gen, "put_int");
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "str") == 0) {
                if (expr_is_string_like(gen, arg0)) {
                    c99_codegen_expr(gen, arg0);
                    break;
                }
                if (expr_is_float_like(gen, arg0)) {
                    gen->runtime_used |= RT_STR_FLOAT;
                    append_str(gen, "terra_str_float");
                } else {
                    gen->runtime_used |= RT_STR_INT;
                    append_str(gen, "terra_str_int");
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "int") == 0) {
                if (expr_is_string_like(gen, arg0)) {
                    gen->runtime_used |= RT_INT_STR;
                    append_str(gen, "terra_int_from_str");
                } else if (expr_is_float_like(gen, arg0)) {
                    gen->runtime_used |= RT_INT_FLOAT;
                    append_str(gen, "terra_int_from_float");
                } else {
                    c99_codegen_expr(gen, arg0);
                    break;
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "float") == 0) {
                if (expr_is_string_like(gen, arg0)) {
                    gen->runtime_used |= RT_FLOAT_STR;
                    append_str(gen, "terra_float_from_str");
                } else if (expr_is_float_like(gen, arg0)) {
                    c99_codegen_expr(gen, arg0);
                    break;
                } else {
                    gen->runtime_used |= RT_FLOAT_INT;
                    append_str(gen, "terra_float_from_int");
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "len") == 0) {
                gen->runtime_used |= RT_LEN;
                append_str(gen, "terra_len");
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "assert") == 0) {
                gen->runtime_used |= RT_ASSERT;
                append_str(gen, "terra_assert");
            } else if (call_name != NULL && expr->as.call.arg_count == 0 &&
                       strcmp(call_name, "input") == 0) {
                gen->runtime_used |= RT_INPUT;
                append_str(gen, "terra_input()");
                break;
            } else {
                c99_codegen_expr(gen, expr->as.call.callee);
            }
            append_str(gen, "(");
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                if (i > 0) append_str(gen, ", ");
                if (expr->as.call.args[i]) {
                    c99_codegen_expr(gen, expr->as.call.args[i]);
                } else {
                    append_str(gen, "NULL");
                }
            }
            append_str(gen, ")");
            break;
        }
        case TESS_EXPR_BINARY: {
            bool lhs_str = expr_is_string_like(gen, expr->as.binary.lhs);
            bool rhs_str = expr_is_string_like(gen, expr->as.binary.rhs);
            if (expr->as.binary.op == TESS_BIN_ADD && lhs_str && rhs_str) {
                gen->runtime_used |= RT_CONCAT;
                append_str(gen, "concat(");
                c99_codegen_expr(gen, expr->as.binary.lhs);
                append_str(gen, ", ");
                c99_codegen_expr(gen, expr->as.binary.rhs);
                append_str(gen, ")");
                break;
            }
            /* String-typed operands compare by content, not by address. */
            if (expr->as.binary.op >= TESS_BIN_EQ &&
                expr->as.binary.op <= TESS_BIN_GE && lhs_str && rhs_str) {
                const char *tail =
                    expr->as.binary.op == TESS_BIN_EQ ? "== 0)" :
                    expr->as.binary.op == TESS_BIN_NE ? "!= 0)" :
                    expr->as.binary.op == TESS_BIN_LT ? "< 0)" :
                    expr->as.binary.op == TESS_BIN_LE ? "<= 0)" :
                    expr->as.binary.op == TESS_BIN_GT ? "> 0)" : ">= 0)";
                append_str(gen, "(strcmp(");
                c99_codegen_expr(gen, expr->as.binary.lhs);
                append_str(gen, ", ");
                c99_codegen_expr(gen, expr->as.binary.rhs);
                append_fprintf(gen, ") %s", tail);
                break;
            }
            c99_codegen_expr(gen, expr->as.binary.lhs);
            
            switch (expr->as.binary.op) {
                case TESS_BIN_OR: append_str(gen, " || "); break;
                case TESS_BIN_AND: append_str(gen, " && "); break;
                case TESS_BIN_BIT_OR: append_str(gen, " | "); break;
                case TESS_BIN_BIT_XOR: append_str(gen, " ^ "); break;
                case TESS_BIN_BIT_AND: append_str(gen, " & "); break;
                case TESS_BIN_EQ: append_str(gen, " == "); break;
                case TESS_BIN_NE: append_str(gen, " != "); break;
                case TESS_BIN_LT: append_str(gen, " < "); break;
                case TESS_BIN_LE: append_str(gen, " <= "); break;
                case TESS_BIN_GT: append_str(gen, " > "); break;
                case TESS_BIN_GE: append_str(gen, " >= "); break;
                case TESS_BIN_ADD: append_str(gen, " + "); break;
                case TESS_BIN_SUB: append_str(gen, " - "); break;
                case TESS_BIN_MUL: append_str(gen, " * "); break;
                case TESS_BIN_DIV: append_str(gen, " / "); break;
                case TESS_BIN_MOD: append_str(gen, " % "); break;
            }
            
            c99_codegen_expr(gen, expr->as.binary.rhs);
            break;
        }
        case TESS_EXPR_UNARY: {
            switch (expr->as.unary.op) {
                case TESS_UN_NEG: append_str(gen, "-("); break;
                case TESS_UN_POS: append_str(gen, "+("); break;
                case TESS_UN_NOT: append_str(gen, "!("); break;
                case TESS_UN_BIT_NOT: append_str(gen, "~("); break;
            }
            c99_codegen_expr(gen, expr->as.unary.operand);
            append_str(gen, ")");
            break;
        }
        case TESS_EXPR_MODULE: {
            gen->failed = true;
            break;
        }
        case TESS_EXPR_TERNARY: {
            append_str(gen, "(");
            c99_codegen_expr(gen, expr->as.ternary.cond);
            append_str(gen, " ? ");
            if (expr->as.ternary.then_expr) c99_codegen_expr(gen, expr->as.ternary.then_expr);
            else append_str(gen, "0");
            append_str(gen, " : ");
            c99_codegen_expr(gen, expr->as.ternary.else_expr);
            append_str(gen, ")");
            break;
        }
        default: {
            gen->failed = true;
            append_str(gen, "/* UNSUPPORTED_EXPR */");
            break;
        }
    }
}

static void c99_codegen_stmt(c99_codegen *gen, tess_stmt *stmt);

/* Emit a statement used as a loop/branch body. A block body is emitted inline
 * because the surrounding `if`/`while` already printed the braces. */
static void c99_codegen_body(c99_codegen *gen, tess_stmt *body) {
    if (body == NULL) return;
    if (body->kind == TESS_STMT_BLOCK) {
        for (size_t i = 0; i < body->as.block.count; i++) {
            c99_codegen_stmt(gen, body->as.block.items[i]);
        }
    } else {
        c99_codegen_stmt(gen, body);
    }
}

/**
 * @brief Generate C99 code for a statement
 */
static void c99_codegen_stmt(c99_codegen *gen, tess_stmt *stmt) {
    if (!gen || !stmt) return;
    
    switch (stmt->kind) {
        case TESS_STMT_EMPTY:
            break;
            
        case TESS_STMT_CLASS:
            // Already handled in generate_classes
            break;
            
        case TESS_STMT_EXPR: {
            append_indent(gen);
            c99_codegen_expr(gen, stmt->as.expr);
            append_str(gen, ";\n");
            break;
        }
        
        case TESS_STMT_ASSIGN: {
            append_indent(gen);
            if (stmt->as.assign.target && stmt->as.assign.value) {
                /* Declare new bare variables on first assignment. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT) {
                    const char *name = stmt->as.assign.target->as.ident;
                    /* A first assignment is a declaration; resetting this
                     * set at branch boundaries preserves C block scope. */
                    if (!is_declared_var(gen, name) &&
                        !(gen->current_class && is_field_of_current_class(gen, name))) {
                        const char *class_type = construction_class(
                            gen, stmt->as.assign.value);
                        if (class_type != NULL) {
                            append_fprintf(gen, "%s* ", class_type);
                            record_var_class(gen, name, class_type);
                        } else {
                            append_fprintf(gen, "%s ",
                                           expr_type_name(gen,
                                               stmt->as.assign.value));
                        }
                        mark_declared_var(gen, name);
                    }
                }
                c99_codegen_expr(gen, stmt->as.assign.target);
                /* Refresh tracked type: a variable can be reassigned with a
                 * different type, so stale entries must not survive. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT) {
                    forget_var_class(gen, stmt->as.assign.target->as.ident);
                }
                /* Track string variables for concat handling. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_STRING) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                }
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_BINARY &&
                    stmt->as.assign.value->as.binary.op == TESS_BIN_ADD &&
                    expr_is_string_like(gen, stmt->as.assign.value->as.binary.lhs) &&
                    expr_is_string_like(gen, stmt->as.assign.value->as.binary.rhs)) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                }
                /* Record float variables */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_FLOAT) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                }
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_BINARY &&
                    expr_is_float_like(gen, stmt->as.assign.value)) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                }
                /* Builtins that return a non-int type. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_CALL &&
                    stmt->as.assign.value->as.call.callee &&
                    stmt->as.assign.value->as.call.callee->kind == TESS_EXPR_IDENT) {
                    const char *fn = stmt->as.assign.value->as.call.callee->as.ident;
                    if (strcmp(fn, "str") == 0 || strcmp(fn, "input") == 0) {
                        record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                    } else if (strcmp(fn, "float") == 0) {
                        record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                    }
                }
                /* Ternaries and calls typed through declared return types:
                 * `s = f();`, `s = obj.m();`, `s = flag ? "a" : "b";` must
                 * not fall back to `long long`. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_CALL &&
                    stmt->as.assign.value->as.call.callee) {
                    const char *ret = NULL;
                    if (stmt->as.assign.value->as.call.callee->kind == TESS_EXPR_IDENT) {
                        ret = fn_ret_type_name(
                            gen, stmt->as.assign.value->as.call.callee->as.ident);
                    } else if (stmt->as.assign.value->as.call.callee->kind == TESS_EXPR_MEMBER) {
                        ret = method_ret_type_name(
                            gen,
                            stmt->as.assign.value->as.call.callee->as.member.object,
                            stmt->as.assign.value->as.call.callee->as.member.name);
                    }
                    if (ret != NULL) {
                        if (strcmp(ret, "str") == 0 || strcmp(ret, "string") == 0) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                        } else if (strcmp(ret, "float") == 0 || strcmp(ret, "f64") == 0) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                        } else if (is_class_name(gen, ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, ret);
                        }
                    }
                }
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_TERNARY &&
                    expr_is_string_like(gen, stmt->as.assign.value)) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                }
                /* A bare method reference (`n = a.get;`) yields the method's
                 * declared return type. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_MEMBER &&
                    stmt->as.assign.value->as.member.object != NULL &&
                    stmt->as.assign.value->as.member.name != NULL) {
                    const char *ret = method_ret_type_name(
                        gen, stmt->as.assign.value->as.member.object,
                        stmt->as.assign.value->as.member.name);
                    if (ret != NULL) {
                        if (strcmp(ret, "str") == 0 || strcmp(ret, "string") == 0) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                        } else if (strcmp(ret, "float") == 0 || strcmp(ret, "f64") == 0) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                        } else if (is_class_name(gen, ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, ret);
                        }
                    }
                }
                /* Record variable -> class for method dispatch, for both
                 * `Class.new` and `Class.init(...)`. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT) {
                    const char *built = construction_class(gen, stmt->as.assign.value);
                    if (built != NULL) {
                        record_var_class(gen, stmt->as.assign.target->as.ident, built);
                    }
                }
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_IDENT) {
                    const char *cls = get_var_class(gen, stmt->as.assign.value->as.ident);
                    if (cls) record_var_class(gen, stmt->as.assign.target->as.ident, cls);
                }
                append_str(gen, " = ");
                c99_codegen_expr(gen, stmt->as.assign.value);
                append_str(gen, ";\n");
            } else {
                append_str(gen, "// NULL assignment\n");
            }
            break;
        }
        
        case TESS_STMT_IF: {
            size_t saved_declared = gen->declared_count;
            size_t saved_vars = gen->var_count;
            append_indent(gen);
            append_str(gen, "if (");
            c99_codegen_expr(gen, stmt->as.if_stmt.cond);
            append_str(gen, ") {\n");
            gen->indent_level++;
            c99_codegen_body(gen, stmt->as.if_stmt.then_branch);
            gen->declared_count = saved_declared;
            gen->var_count = saved_vars;
            gen->indent_level--;
            
            if (stmt->as.if_stmt.else_branch && 
                stmt->as.if_stmt.else_branch->kind != TESS_STMT_EMPTY) {
                append_indent(gen);
                append_str(gen, "} else {\n");
                gen->indent_level++;
                c99_codegen_body(gen, stmt->as.if_stmt.else_branch);
                gen->declared_count = saved_declared;
                gen->var_count = saved_vars;
                gen->indent_level--;
            }
            
            append_indent(gen);
            append_str(gen, "}\n");
            break;
        }
        
        case TESS_STMT_WHILE: {
            size_t saved_declared = gen->declared_count;
            size_t saved_vars = gen->var_count;
            append_indent(gen);
            append_str(gen, "while (");
            c99_codegen_expr(gen, stmt->as.while_stmt.cond);
            append_str(gen, ") {\n");
            gen->indent_level++;
            c99_codegen_body(gen, stmt->as.while_stmt.body);
            gen->declared_count = saved_declared;
            gen->var_count = saved_vars;
            gen->indent_level--;
            append_indent(gen);
            append_str(gen, "}\n");
            break;
        }
        
        case TESS_STMT_RETURN: {
            append_indent(gen);
            if (gen->in_function) {
                /* Inside a fn/method body the return form follows its return
                 * type; sema has already validated the value presence. */
                if (gen->fn_returns_void) {
                    append_str(gen, "return;\n");
                } else {
                    append_str(gen, "return ");
                    if (stmt->as.ret) c99_codegen_expr(gen, stmt->as.ret);
                    else append_str(gen, "0");
                    append_str(gen, ";\n");
                }
                break;
            }
            /* `main` returns int; a top-level bare `return;` is not valid C. */
            if (stmt->as.ret == NULL) {
                append_str(gen, "return 0;\n");
                break;
            }
            append_str(gen, "return ");
            c99_codegen_expr(gen, stmt->as.ret);
            append_str(gen, ";\n");
            break;
        }
        
        case TESS_STMT_BLOCK: {
            /* Branch/loop bodies are emitted inline by their parent
             * statement, which already printed the opening `{`. */
            append_indent(gen);
            append_str(gen, "{\n");
            size_t saved_declared = gen->declared_count;
            size_t saved_vars = gen->var_count;
            gen->indent_level++;
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                c99_codegen_stmt(gen, stmt->as.block.items[i]);
            }
            gen->declared_count = saved_declared;
            gen->var_count = saved_vars;
            gen->indent_level--;
            append_indent(gen);
            append_str(gen, "}\n");
            break;
        }
        case TESS_STMT_IMPORT:
            /* Imports are compile-time declarations. */
            break;
        case TESS_STMT_FN:
            /* Emitted by the top-level function pass. */
            break;
    }
}
/**
 * @brief Generate method for a class
 */
static void c99_codegen_generate_method(c99_codegen *gen, tess_class *cls, tess_method *method) {
    if (!gen || !cls || !method) return;
    
    const char *saved_class = gen->current_class;
    gen->current_class = cls->name;
    reset_function_state(gen);
    
    /* Parameters typed `str` or `float` are tracked so put() picks the right function */
    for (size_t pi = 0; pi < method->param_count; pi++) {
        tess_param *param = &method->params[pi];
        if (param->type && (strcmp(param->type, "str") == 0 || strcmp(param->type, "string") == 0)) {
            record_var_class(gen, param->name, "str");
        } else if (param->type && (strcmp(param->type, "float") == 0 || strcmp(param->type, "f64") == 0)) {
            record_var_class(gen, param->name, "float");
        }
        /* Mark all parameters as declared so they don't get __auto_type */
        mark_declared_var(gen, param->name);
    }
    
    append_str(gen, "\n");
    append_indent(gen);
    
    // Method signature: return_type class_MethodName(class_name* self, ...)
    bool method_void = method->ret_type == NULL ||
                       strcmp(method->ret_type, "null") == 0;
    gen->in_function = true;
    gen->fn_returns_void = method_void;
    append_fprintf(gen, "%s ",
                   method_void ? "void" : tess_type_to_c(method->ret_type));
    append_ident(gen, cls->name);
    append_str(gen, "_");
    append_ident(gen, method->name);
    
    append_str(gen, "(");
    
    bool first = true;
    
    // First parameter is always 'self'
    append_fprintf(gen, "%s* self", cls->name);
    first = false;
    
    // Other parameters
    for (size_t i = 0; i < method->param_count; i++) {
        if (!first) append_str(gen, ", ");
        tess_param *param = &method->params[i];
        append_declarator(gen, param->type, param->name);
        first = false;
    }
    
    append_str(gen, ") {\n");
    gen->indent_level++;
    append_indent(gen);
    append_str(gen, "(void)self;\n");
    
    // Generate method body
    if (method->body && method->body->kind != TESS_STMT_EMPTY) {
        if (method->body->kind == TESS_STMT_BLOCK) {
            // Method has a block body
            for (size_t i = 0; i < method->body->as.block.count; i++) {
                c99_codegen_stmt(gen, method->body->as.block.items[i]);
            }
        } else {
            // Single statement body
            c99_codegen_stmt(gen, method->body);
        }
    }
    
    bool ends_with_return = false;
    if (method->body) {
        if (method->body->kind == TESS_STMT_BLOCK) {
            ends_with_return = method->body->as.block.count > 0 &&
                method->body->as.block.items[method->body->as.block.count - 1]->kind == TESS_STMT_RETURN;
        } else {
            ends_with_return = method->body->kind == TESS_STMT_RETURN;
        }
    }

    if (!ends_with_return) {
        append_indent(gen);
        append_str(gen, method_void ? "return;\n" : "return 0;\n");
    }
    gen->indent_level--;
    append_indent(gen);
    append_str(gen, "}\n\n");
    
    gen->current_class = saved_class;
    reset_function_state(gen);
}

static void c99_codegen_generate_function(c99_codegen *gen, tess_stmt *stmt) {
    if (!gen || !stmt || stmt->kind != TESS_STMT_FN) return;
    reset_function_state(gen);
    gen->current_class = NULL;
    tess_stmt *fn = stmt;
    const char *name = fn->as.fn.name;
    if (!name) { gen->failed = true; return; }
    bool returns_void = fn->as.fn.ret_type == NULL ||
                        strcmp(fn->as.fn.ret_type, "null") == 0;
    gen->in_function = true;
    gen->fn_returns_void = returns_void;
    append_str(gen, "static ");
    append_fprintf(gen, "%s ", returns_void ? "void" : tess_type_to_c(fn->as.fn.ret_type));
    append_ident(gen, name);
    append_str(gen, "(");
    if (fn->as.fn.param_count == 0) append_str(gen, "void");
    for (size_t i = 0; i < fn->as.fn.param_count; i++) {
        if (i) append_str(gen, ", ");
        append_declarator(gen, fn->as.fn.params[i].type,
                          fn->as.fn.params[i].name);
        mark_declared_var(gen, fn->as.fn.params[i].name);
        if (fn->as.fn.params[i].type &&
            (strcmp(fn->as.fn.params[i].type, "str") == 0 ||
             strcmp(fn->as.fn.params[i].type, "string") == 0))
            record_var_class(gen, fn->as.fn.params[i].name, "str");
        if (fn->as.fn.params[i].type &&
            (strcmp(fn->as.fn.params[i].type, "float") == 0 ||
             strcmp(fn->as.fn.params[i].type, "f64") == 0))
            record_var_class(gen, fn->as.fn.params[i].name, "float");
    }
    append_str(gen, ") {\n");
    gen->indent_level++;
    c99_codegen_body(gen, fn->as.fn.fn_body);
    if (returns_void) {
        append_indent(gen);
        append_str(gen, "return;\n");
    }
    gen->indent_level--;
    append_str(gen, "}\n\n");
    reset_function_state(gen);
}

static void c99_codegen_generate_functions(c99_codegen *gen,
                                            tess_program *program) {
    /* Emit every prototype before any body so declaration order in the source
     * does not constrain calls or recursion. */
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (!stmt || stmt->kind != TESS_STMT_FN) continue;
        bool returns_void = stmt->as.fn.ret_type == NULL ||
            strcmp(stmt->as.fn.ret_type, "null") == 0;
        append_str(gen, "static ");
        append_fprintf(gen, "%s ", returns_void ? "void" :
                      tess_type_to_c(stmt->as.fn.ret_type));
        append_ident(gen, stmt->as.fn.name);
        append_str(gen, "(");
        if (stmt->as.fn.param_count == 0) append_str(gen, "void");
        for (size_t j = 0; j < stmt->as.fn.param_count; j++) {
            if (j) append_str(gen, ", ");
            append_declarator(gen, stmt->as.fn.params[j].type,
                              stmt->as.fn.params[j].name);
        }
        append_str(gen, ");\n");
    }
    append_str(gen, "\n");
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt && stmt->kind == TESS_STMT_FN)
            c99_codegen_generate_function(gen, stmt);
    }
}

static void c99_codegen_generate_class_init_helpers(c99_codegen *gen,
                                                     tess_program *program) {
    /* Method prototypes are already published by c99_codegen_generate_classes
     * before this point. */
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (!stmt || stmt->kind != TESS_STMT_CLASS || !stmt->as.class_decl)
            continue;
        tess_class *cls = stmt->as.class_decl;
        tess_method *init = NULL;
        for (size_t j = 0; j < cls->method_count; j++)
            if (strcmp(cls->methods[j].name, "init") == 0) init = &cls->methods[j];
        if (!init) continue;
        append_fprintf(gen, "static TERRA_MAYBE_UNUSED %s* ", cls->name);
        append_ident(gen, cls->name);
        append_str(gen, "_init_new(");
        if (init->param_count == 0) append_str(gen, "void");
        for (size_t j = 0; j < init->param_count; j++) {
            if (j) append_str(gen, ", ");
            append_declarator(gen, init->params[j].type, init->params[j].name);
        }
        append_str(gen, ") {\n");
        for (size_t j = 0; j < init->param_count; j++)
            append_fprintf(gen, "    (void)%s;\n", init->params[j].name);
        append_fprintf(gen, "    %s* instance = (%s*)calloc(1, sizeof(%s));\n",
                       cls->name, cls->name, cls->name);
        append_str(gen, "    if (instance == NULL) return NULL;\n");
        append_ident(gen, cls->name);
        append_str(gen, "_init(instance");
        for (size_t j = 0; j < init->param_count; j++)
            append_fprintf(gen, ", %s", init->params[j].name);
        append_str(gen, ");\n    return instance;\n}\n\n");
    }
}

/**
 * @brief Generate all class definitions and methods
 */
static void c99_codegen_generate_classes(c99_codegen *gen, tess_program *program) {
    if (!gen || !program) return;

    /* Forward typedefs let fields and signatures refer to later classes. */
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt && stmt->kind == TESS_STMT_CLASS && stmt->as.class_decl) {
            append_str(gen, "typedef struct ");
            append_ident(gen, stmt->as.class_decl->name);
            append_str(gen, " ");
            append_ident(gen, stmt->as.class_decl->name);
            append_str(gen, ";\n");
        }
    }

    /* Method prototypes are needed before init helpers and method bodies. */
    append_str(gen, "\n");

    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt == NULL || stmt->kind != TESS_STMT_CLASS) continue;
        tess_class *cls = stmt->as.class_decl;
        for (size_t j = 0; j < cls->method_count; j++) {
            tess_method *method = &cls->methods[j];
            bool m_void = method->ret_type == NULL ||
                          strcmp(method->ret_type, "null") == 0;
            append_fprintf(gen, "%s ", m_void ? "void" :
                           tess_type_to_c(method->ret_type));
            append_ident(gen, cls->name);
            append_str(gen, "_");
            append_ident(gen, method->name);
            append_fprintf(gen, "(%s* self", cls->name);
            for (size_t k = 0; k < method->param_count; k++) {
                append_str(gen, ", ");
                append_declarator(gen, method->params[k].type,
                                  method->params[k].name);
            }
            append_str(gen, ");\n");
        }
    }

    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt == NULL || stmt->kind != TESS_STMT_CLASS) continue;

        tess_class *cls = stmt->as.class_decl;

        // Generate struct definition
        append_str(gen, "\ntypedef struct ");
        append_ident(gen, cls->name);
        append_str(gen, " {\n");

        gen->indent_level++;

        bool any_field = false;
        for (size_t j = 0; j < cls->field_count; j++) {
            append_indent(gen);
            tess_field *field = &cls->fields[j];
            append_declarator(gen, field->type, field->name);
            append_str(gen, ";\n");
            any_field = true;
        }
        if (!any_field) {
            /* C99 forbids empty structs */
            append_indent(gen);
            append_str(gen, "char _terra_pad;\n");
        }

        gen->indent_level--;
        append_indent(gen);
        append_str(gen, "} ");
        append_ident(gen, cls->name);
        append_str(gen, ";\n");
    }

    c99_codegen_generate_class_init_helpers(gen, program);

    c99_codegen_generate_functions(gen, program);

    /* Bodies come last: a method may touch fields of any class. */
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt == NULL || stmt->kind != TESS_STMT_CLASS) continue;
        tess_class *cls = stmt->as.class_decl;
        for (size_t j = 0; j < cls->method_count; j++) {
            c99_codegen_generate_method(gen, cls, &cls->methods[j]);
        }
    }
}

/**
 * @brief Generate main function with program execution logic
 */
static void c99_codegen_generate_main(c99_codegen *gen, tess_program *program) {
    if (!gen || !program) return;

    reset_function_state(gen);
    gen->current_class = NULL;

    append_str(gen, "\nint main(int argc, char** argv) {\n");
    gen->indent_level++;

    append_indent(gen);
    append_str(gen, "(void)argc;\n");
    append_indent(gen);
    append_str(gen, "(void)argv;\n\n");

    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt && stmt->kind != TESS_STMT_CLASS) {
            c99_codegen_stmt(gen, stmt);
        }
    }

    append_str(gen, "\n");
    append_indent(gen);
    append_str(gen, "return 0;\n");
    gen->indent_level--;
    append_indent(gen);
    append_str(gen, "}\n");
}


bool c99_codegen_init(c99_codegen *gen, size_t initial_capacity) {
    if (!gen) return false;
    
    gen->buffer = malloc(initial_capacity);
    if (!gen->buffer) return false;
    
    gen->capacity = initial_capacity;
    gen->size = 0;
    gen->indent_level = 0;
    gen->failed = false;
    gen->program = NULL;
    gen->current_class = NULL;
    gen->declared = NULL;
    gen->declared_count = 0;
    gen->declared_cap = 0;
    gen->var_names = NULL;
    gen->var_classes = NULL;
    gen->var_count = 0;
    gen->var_cap = 0;
    gen->module_aliases = NULL;
    gen->module_targets = NULL;
    gen->module_alias_count = 0;
    gen->module_alias_cap = 0;
    gen->module = NULL;
    gen->runtime_used = 0;
    gen->in_function = false;
    gen->fn_returns_void = false;

    return true;
}

void c99_codegen_destroy(c99_codegen *gen) {
    if (!gen) return;
    if (gen->buffer) {
        free(gen->buffer);
        gen->buffer = NULL;
        gen->capacity = 0;
        gen->size = 0;
    }
    free(gen->declared);
    gen->declared = NULL;
    gen->declared_count = 0;
    gen->declared_cap = 0;
    /* Variable names and classes point into the AST arena; only the arrays
     * themselves are owned here. */
    free(gen->var_names);
    free(gen->var_classes);
    gen->var_names = NULL;
    gen->var_classes = NULL;
    gen->var_count = 0;
    gen->var_cap = 0;
    for (size_t i = 0; i < gen->module_alias_count; i++) {
        free(gen->module_aliases[i]);
        free(gen->module_targets[i]);
    }
    free(gen->module_aliases);
    free(gen->module_targets);
    gen->module_aliases = NULL;
    gen->module_targets = NULL;
    gen->module_alias_count = 0;
    gen->module_alias_cap = 0;
}

/* Emit the runtime library block: shared includes always, each helper only
 * when the generated program references it. Call after the program body is
 * generated so that gen->runtime_used is complete. */
void c99_codegen_print_runtime_info(c99_codegen *gen) {
    if (!gen) return;

    append_str(gen, "/* Terra Runtime Library */\n");
    append_str(gen, "#include <stdio.h>\n");
    append_str(gen, "#include <stdlib.h>\n");
    append_str(gen, "#include <string.h>\n");
    append_str(gen, "#include <stdbool.h>\n");
    append_str(gen, "#include <stdint.h>\n#include <math.h>\n\n");
    append_str(gen, "#include <errno.h>\n#include <limits.h>\n\n");
    /* Class init helpers may legitimately end up unused in a given program;
     * keep them out of -Wunused-function's way without dropping `static`. */
    append_str(gen,
        "#if defined(__GNUC__) || defined(__clang__)\n"
        "#define TERRA_MAYBE_UNUSED __attribute__((unused))\n"
        "#else\n"
        "#define TERRA_MAYBE_UNUSED\n"
        "#endif\n\n");
    if (gen->runtime_used & RT_PUT_INT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline void put_int(long long v){ printf(\"%lld\\n\", v); }\n");
    }
    if (gen->runtime_used & RT_PUT_FLOAT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline void put_float(double v){ printf(\"%g\\n\", v); }\n");
    }
    if (gen->runtime_used & RT_PUT_STR) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline void put_str(const char* s){ printf(\"%s\\n\", s ? s : \"(null)\"); }\n");
    }
    if (gen->runtime_used & RT_CONCAT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline char* concat(const char* a, const char* b){\n"
            "    if(!a) a=\"\";\n"
            "    if(!b) b=\"\";\n"
            "    size_t la=strlen(a), lb=strlen(b);\n"
            "    if(la == SIZE_MAX || lb > SIZE_MAX - la - 1) return NULL;\n"
            "    char* r=(char*)malloc(la+lb+1);\n"
            "    if(!r) return NULL;\n"
            "    memcpy(r,a,la); memcpy(r+la,b,lb); r[la+lb]=0;\n"
            "    return r;\n"
            "}\n");
    }
    if (gen->runtime_used & RT_LEN) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline long long terra_len(const char* s){ if(!s) return 0; size_t n=strlen(s); return n>(size_t)LLONG_MAX ? LLONG_MAX : (long long)n; }\n");
    }
    if (gen->runtime_used & RT_STR_INT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline char* terra_str_int(long long v){\n"
            "    char* r=(char*)malloc(32); if(!r) return NULL; snprintf(r,32,\"%lld\",v); return r;\n"
            "}\n");
    }
    if (gen->runtime_used & RT_STR_FLOAT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline char* terra_str_float(double v){\n"
            "    char* r=(char*)malloc(32); if(!r) return NULL; snprintf(r,32,\"%g\",v); return r;\n"
            "}\n");
    }
    if (gen->runtime_used & RT_INT_FLOAT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline long long terra_int_from_float(double v){ if(isnan(v)) return 0; if(v>=(double)LLONG_MAX) return LLONG_MAX; if(v<=(double)LLONG_MIN) return LLONG_MIN; return (long long)v; }\n");
    }
    if (gen->runtime_used & RT_INT_STR) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline long long terra_int_from_str(const char* s){ if(!s) return 0; errno=0; char* end=NULL; long long v=strtoll(s,&end,10); return (end==s || errno==ERANGE) ? 0 : v; }\n");
    }
    if (gen->runtime_used & RT_FLOAT_INT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline double terra_float_from_int(long long v){ return (double)v; }\n");
    }
    if (gen->runtime_used & RT_FLOAT_STR) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline double terra_float_from_str(const char* s){ if(!s) return 0.0; errno=0; char* end=NULL; double v=strtod(s,&end); return (end==s || errno==ERANGE) ? 0.0 : v; }\n");
    }
    if (gen->runtime_used & RT_ASSERT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline void terra_assert(int cond){\n"
            "    if(!cond){ fputs(\"assertion failed\\n\", stderr); exit(1); }\n"
            "}\n");
    }
    if (gen->runtime_used & RT_INPUT) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline char* terra_input(void){\n"
            "    size_t cap=256, len=0; char* buf=(char*)malloc(cap); if(!buf) return NULL;\n"
            "    int c;\n"
            "    while((c=fgetc(stdin))!=EOF && c!='\\n'){\n"
            "        if(len+1>=cap){ if(cap>SIZE_MAX/2){ free(buf); return NULL; } size_t next=cap*2; char* nb=(char*)realloc(buf,next); if(!nb){ free(buf); return NULL; } buf=nb; cap=next; }\n"
            "        buf[len++]=(char)c;\n"
            "    }\n"
            "    buf[len]='\\0'; return buf;\n"
            "}\n\n");
    }
    append_str(gen, "\n");
}

/* Build the runtime block in a scratch context and prepend it to the already
 * generated program body (runtime code must precede its call sites in C). */
static bool prepend_runtime_block(c99_codegen *gen) {
    if (!gen) return false;

    c99_codegen rt;
    if (!c99_codegen_init(&rt, 1024)) {
        gen->failed = true;
        return false;
    }
    rt.runtime_used = gen->runtime_used;
    c99_codegen_print_runtime_info(&rt);

    bool ok = !rt.failed && !gen->failed;
    if (ok && rt.size > 0) {
        ok = ensure_capacity(gen, rt.size);
        if (ok) {
            memmove(gen->buffer + rt.size, gen->buffer, gen->size);
            memcpy(gen->buffer, rt.buffer, rt.size);
            gen->size += rt.size;
        }
    }
    if (!ok) {
        gen->failed = true;
    }
    c99_codegen_destroy(&rt);
    return ok;
}

static void c99_codegen_collect_modules(c99_codegen *gen, tess_program *program) {
    if (!gen || !gen->program) return;
    /* Build alias lookup once; expression emission then stays independent of
     * source declaration order. */
    gen->module_alias_count = 0;
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *st = program->items[i];
        if (st && st->kind == TESS_STMT_IMPORT) {
            const char *module = st->as.import.module;
            const char *alias = st->as.import.alias;
            const char *module_key = module ? module : (alias ? alias : "");
            module_add(gen, alias, module_key);
        }
    }
}

static char *c99_codegen_run(c99_codegen *gen, tess_program *program) {
    // Generate class definitions (records used runtime helpers)
    c99_codegen_generate_classes(gen, program);

    if (gen->module != NULL) {
        /* Module mode deliberately emits declarations without a program main;
         * the caller owns linking the resulting translation unit. */
        if (gen->failed || !ensure_capacity(gen, 1)) return NULL;
        if (!prepend_runtime_block(gen)) return NULL;
        gen->buffer[gen->size] = '\0';
        char *result = gen->buffer;
        gen->buffer = NULL;
        return result;
    }

    // Generate main function
    c99_codegen_generate_main(gen, program);

    /* Never return partial output: callers must not mistake a stubbed
     * expression for successfully generated code. */
    if (gen->failed) {
        return NULL;
    }

    // Prepend the runtime helpers the generated body actually references.
    if (!prepend_runtime_block(gen)) {
        return NULL;
    }

    // Reserve room for the NUL terminator.
    if (!ensure_capacity(gen, 1)) {
        return NULL;
    }
    gen->buffer[gen->size] = '\0';

    // Hand ownership of the grown buffer to the caller.
    char *result = gen->buffer;
    gen->buffer = NULL;
    return result;
}

char *c99_codegen_generate(tess_program *program) {
    if (!program) {
        return NULL;
    }

    c99_codegen gen;
    if (!c99_codegen_init(&gen, 4096)) {
        return NULL;
    }
    gen.program = program;
    c99_codegen_collect_modules(&gen, program);

    char *result = c99_codegen_run(&gen, program);
    /* Release every auxiliary table; on success the output buffer itself has
     * already been handed to the caller and gen.buffer is NULL. */
    c99_codegen_destroy(&gen);
    return result;
}

char *c99_codegen_generate_module(tess_program *program, const char *module) {
    if (!program) {
        return NULL;
    }

    c99_codegen gen;
    if (!c99_codegen_init(&gen, 4096)) {
        return NULL;
    }
    gen.program = program;
    gen.module = module;
    c99_codegen_collect_modules(&gen, program);

    char *result = c99_codegen_run(&gen, program);
    c99_codegen_destroy(&gen);
    return result;
}

size_t c99_codegen_program(tess_program *program, char *out_buffer, size_t buffer_size) {
    if (!program || !out_buffer || buffer_size == 0) {
        return 0;
    }

    out_buffer[0] = '\0';

    char *code = c99_codegen_generate(program);
    if (!code) {
        return 0;
    }

    size_t len = strlen(code);
    if (len >= buffer_size) {
        // The caller's buffer is too small: do not truncate silently.
        free(code);
        return 0;
    }

    memcpy(out_buffer, code, len + 1);
    free(code);
    return len;
}
