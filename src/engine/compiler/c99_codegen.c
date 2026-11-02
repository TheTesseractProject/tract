#include "c99_codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include "../../constants.h"

/**
 * @brief Ensure buffer has enough space
 */
static bool ensure_capacity(c99_codegen *gen, size_t extra) {
    if (!gen || extra == 0) return true;
    
    if (gen->size + extra + 1 > gen->capacity) {
        size_t needed = gen->size + extra + 1;
        size_t new_capacity = gen->capacity ? gen->capacity : 64;
        while (new_capacity < needed) new_capacity *= 2;
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
    if (!gen || !str) return false;
    
    size_t len = strlen(str);
    if (!ensure_capacity(gen, len)) return false;
    
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
        return false;
    }
    
    size_t needed = (size_t)written + 1;
    if (!ensure_capacity(gen, needed)) {
        return false;
    }
    
    va_start(args, format);
    int actual_written = vsnprintf(gen->buffer + gen->size, gen->capacity - gen->size, format, args);
    va_end(args);
    
    if (actual_written < 0) return false;
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
                if (c < 0x20 || c >= 0x7f) {
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
    gen->declared_count = 0;
    gen->var_count = 0;
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
        if (!arr) return;
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
        if (!names) return;
        gen->var_names = names;
        char **classes = realloc(gen->var_classes, cap * sizeof(char*));
        if (!classes) return; /* names grew but cap stays; next call retries */
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
    append_fprintf(gen, "%s%s %s", c_type,
                   is_builtin_type_name(tess_type) ? "" : "*", name);
}

static bool expr_is_string_like(c99_codegen *gen, tess_expr *e);
static bool expr_is_float_like(c99_codegen *gen, tess_expr *e);
static tess_field *find_field(tess_class *cls, const char *name);
static tess_class *find_class(c99_codegen *gen, const char *name);
static const char *expr_class(c99_codegen *gen, tess_expr *e);
static bool class_has_method(c99_codegen *gen, const char *cls,
                             const char *method);

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
            /* String-typed field access (self->b, t->name) */
            if (!e->as.member.object || !e->as.member.name) return false;
            const char *cls = expr_class(gen, e->as.member.object);
            if (cls == NULL) return false;
            tess_field *field = find_field(find_class(gen, cls), e->as.member.name);
            return field != NULL && field->type != NULL &&
                   (strcmp(field->type, "str") == 0 ||
                    strcmp(field->type, "string") == 0);
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
            append_str(gen, expr->as.ident);
            break;
        }
        case TESS_EXPR_MEMBER: {
            if (!gen || !expr) return;
            const char *member = expr->as.member.name ? expr->as.member.name : "INVALID";
            tess_expr *obj_expr = expr->as.member.object;
            if (obj_expr && obj_expr->kind == TESS_EXPR_MODULE) {
                const char *mod = obj_expr->as.module;
                const char *mem = expr->as.member.name ? expr->as.member.name : "INVALID";
                if (mod && (strcmp(mod, "c") == 0 || strcmp(mod, "libc") == 0 ||
                            strcmp(mod, "std") == 0 || strcmp(mod, "math") == 0)) {
                    append_str(gen, mem);
                } else {
                    append_fprintf(gen, "%s_%s", mod ? mod : "mod", mem);
                }
                break;
            }

            /* `Class.new` / static member of a known class. */
            if (obj_expr && obj_expr->kind == TESS_EXPR_IDENT &&
                is_class_name(gen, obj_expr->as.ident)) {
                const char *cls = obj_expr->as.ident;
                if (strcmp(member, "new") == 0) {
                    append_fprintf(gen, "(%s*)calloc(1, sizeof(%s))", cls, cls);
                } else {
                    append_fprintf(gen, "%s_%s", cls, member);
                }
                break;
            }

            /* Method referenced without parentheses (`t.run;`) is a call. */
            const char *cls = expr_class(gen, obj_expr);
            if (cls != NULL && class_has_method(gen, cls, member)) {
                append_fprintf(gen, "%s_%s(", cls, member);
                if (obj_expr != NULL) {
                    c99_codegen_expr(gen, obj_expr);
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
                append_str(gen, member);
            } else {
                append_str(gen, "self->");
                append_str(gen, member);
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
                        append_str(gen, "({ ");
                        append_fprintf(gen, "%s* _terra_tmp = calloc(1, sizeof(%s)); ", cls, cls);
                        append_fprintf(gen, "%s_init(_terra_tmp", cls);
                        for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                            append_str(gen, ", ");
                            if (expr->as.call.args[i]) c99_codegen_expr(gen, expr->as.call.args[i]);
                            else append_str(gen, "NULL");
                        }
                        append_str(gen, "); _terra_tmp; })");
                        break;
                    }
                }
                
                /* Native C interop: @c.printf(...) -> printf(...) */
                if (obj_expr && obj_expr->kind == TESS_EXPR_MODULE) {
                    const char *mod = obj_expr->as.module;
                    const char *mem = method;
                    char fn[128];
                    if (mod && (strcmp(mod, "c") == 0 || strcmp(mod, "libc") == 0 ||
                                strcmp(mod, "std") == 0 || strcmp(mod, "math") == 0)) {
                        snprintf(fn, sizeof fn, "%s", mem);
                    } else {
                        snprintf(fn, sizeof fn, "%s_%s", mod ? mod : "mod", mem);
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
                    append_fprintf(gen, "%s_%s(", cls, method);
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
                        strcmp(call_name, "print") == 0)) {
                /* Dispatch by argument type: put_int / put_float / put_str. */
                if (expr_is_string_like(gen, arg0) || expr_is_null(gen, arg0)) {
                    append_str(gen, "put_str");
                } else if (expr_is_float_like(gen, arg0)) {
                    append_str(gen, "put_float");
                } else {
                    append_str(gen, "put_int");
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "str") == 0) {
                if (expr_is_string_like(gen, arg0)) {
                    c99_codegen_expr(gen, arg0);
                    break;
                }
                append_str(gen, expr_is_float_like(gen, arg0)
                                    ? "terra_str_float"
                                    : "terra_str_int");
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "int") == 0) {
                if (expr_is_string_like(gen, arg0)) {
                    append_str(gen, "terra_int_from_str");
                } else if (expr_is_float_like(gen, arg0)) {
                    append_str(gen, "terra_int_from_float");
                } else {
                    c99_codegen_expr(gen, arg0);
                    break;
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "float") == 0) {
                if (expr_is_string_like(gen, arg0)) {
                    append_str(gen, "terra_float_from_str");
                } else if (expr_is_float_like(gen, arg0)) {
                    c99_codegen_expr(gen, arg0);
                    break;
                } else {
                    append_str(gen, "terra_float_from_int");
                }
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "len") == 0) {
                append_str(gen, "terra_len");
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "assert") == 0) {
                append_str(gen, "terra_assert");
            } else if (call_name != NULL && expr->as.call.arg_count == 0 &&
                       strcmp(call_name, "input") == 0) {
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
                append_str(gen, "concat(");
                c99_codegen_expr(gen, expr->as.binary.lhs);
                append_str(gen, ", ");
                c99_codegen_expr(gen, expr->as.binary.rhs);
                append_str(gen, ")");
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
            append_str(gen, "0");
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
                    if (!is_declared_var(gen, name) &&
                        !(gen->current_class && is_field_of_current_class(gen, name))) {
                        append_str(gen, "__auto_type ");
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
            append_indent(gen);
            append_str(gen, "if (");
            c99_codegen_expr(gen, stmt->as.if_stmt.cond);
            append_str(gen, ") {\n");
            gen->indent_level++;
            c99_codegen_body(gen, stmt->as.if_stmt.then_branch);
            gen->indent_level--;
            
            if (stmt->as.if_stmt.else_branch && 
                stmt->as.if_stmt.else_branch->kind != TESS_STMT_EMPTY) {
                append_indent(gen);
                append_str(gen, "} else {\n");
                gen->indent_level++;
                c99_codegen_body(gen, stmt->as.if_stmt.else_branch);
                gen->indent_level--;
            }
            
            append_indent(gen);
            append_str(gen, "}\n");
            break;
        }
        
        case TESS_STMT_WHILE: {
            append_indent(gen);
            append_str(gen, "while (");
            c99_codegen_expr(gen, stmt->as.while_stmt.cond);
            append_str(gen, ") {\n");
            gen->indent_level++;
            c99_codegen_body(gen, stmt->as.while_stmt.body);
            gen->indent_level--;
            append_indent(gen);
            append_str(gen, "}\n");
            break;
        }
        
        case TESS_STMT_RETURN: {
            append_indent(gen);
            /* `main` returns int; a top-level bare `return;` is not valid C. */
            if (gen->current_class == NULL && stmt->as.ret == NULL) {
                append_str(gen, "return 0;\n");
                break;
            }
            append_str(gen, "return ");
            if (stmt->as.ret) c99_codegen_expr(gen, stmt->as.ret);
            else append_str(gen, "0");
            append_str(gen, ";\n");
            break;
        }
        
        case TESS_STMT_BLOCK: {
            /* Branch/loop bodies are emitted inline by their parent
             * statement, which already printed the opening `{`. */
            append_indent(gen);
            append_str(gen, "{\n");
            gen->indent_level++;
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                c99_codegen_stmt(gen, stmt->as.block.items[i]);
            }
            gen->indent_level--;
            append_indent(gen);
            append_str(gen, "}\n");
            break;
        }
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
    append_str(gen, "int ");
    append_str(gen, cls->name);
    append_str(gen, "_");
    append_str(gen, method->name);
    
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
        append_str(gen, "return 0;\n");
    }
    gen->indent_level--;
    append_indent(gen);
    append_str(gen, "}\n\n");
    
    gen->current_class = saved_class;
    reset_function_state(gen);
}

/**
 * @brief Generate all class definitions and methods
 */
static void c99_codegen_generate_classes(c99_codegen *gen, tess_program *program) {
    if (!gen || !program) return;

    /* Methods may reference instances of classes declared later. */
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt == NULL || stmt->kind != TESS_STMT_CLASS) continue;
        append_str(gen, "\ntypedef struct ");
        append_str(gen, stmt->as.class_decl->name);
        append_str(gen, " ");
        append_str(gen, stmt->as.class_decl->name);
        append_str(gen, ";\n");
    }

    /* Methods may also call methods of classes defined further down. */
    append_str(gen, "\n");
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt == NULL || stmt->kind != TESS_STMT_CLASS) continue;
        tess_class *cls = stmt->as.class_decl;
        for (size_t j = 0; j < cls->method_count; j++) {
            tess_method *method = &cls->methods[j];
            append_fprintf(gen, "int %s_%s(%s* self", cls->name,
                           method->name, cls->name);
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
        append_str(gen, cls->name);
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
        append_str(gen, cls->name);
        append_str(gen, ";\n");
    }

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
    gen->has_main = false;
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
    for (size_t i = 0; i < gen->var_count; i++) {
        /* names/classes are owned by arena/program; only array freed */
    }
    free(gen->var_names);
    free(gen->var_classes);
    gen->var_names = NULL;
    gen->var_classes = NULL;
    gen->var_count = 0;
    gen->var_cap = 0;
}

void c99_codegen_reset(c99_codegen *gen) {
    if (!gen) return;
    
    gen->size = 0;
    gen->indent_level = 0;
    gen->has_main = false;
    gen->failed = false;
    gen->declared_count = 0;
    gen->var_count = 0;
}

void c99_codegen_print_runtime_info(c99_codegen *gen) {
    if (!gen) return;
    
    append_str(gen, "/* Terra Runtime Library */\n");
    append_str(gen, "#include <stdio.h>\n");
    append_str(gen, "#include <stdlib.h>\n");
    append_str(gen, "#include <string.h>\n");
    append_str(gen, "#include <stdbool.h>\n");
    append_str(gen, "#include <stdarg.h>\n#include <math.h>\n\n");
    append_str(gen,
        "#if defined(__GNUC__) || defined(__clang__)\n"
        "#define TERRA_UNUSED __attribute__((unused))\n"
        "#else\n"
        "#define TERRA_UNUSED\n"
        "#endif\n\n");
    append_str(gen,
        "TERRA_UNUSED static void put_int(long long v){ printf(\"%lld\\n\", v); }\n");
    append_str(gen,
        "TERRA_UNUSED static void put_float(double v){ printf(\"%g\\n\", v); }\n");
    append_str(gen,
        "TERRA_UNUSED static void put_str(const char* s){ printf(\"%s\\n\", s ? s : \"(null)\"); }\n");
    append_str(gen,
        "TERRA_UNUSED static char* concat(const char* a, const char* b){\n"
        "    if(!a) a=\"\";\n"
        "    if(!b) b=\"\";\n"
        "    size_t la=strlen(a), lb=strlen(b);\n"
        "    char* r=(char*)malloc(la+lb+1);\n"
        "    if(!r) return NULL;\n"
        "    memcpy(r,a,la); memcpy(r+la,b,lb); r[la+lb]=0;\n"
        "    return r;\n"
        "}\n");
    append_str(gen,
        "TERRA_UNUSED static long long terra_len(const char* s){ return s ? (long long)strlen(s) : 0; }\n");
    append_str(gen,
        "TERRA_UNUSED static char* terra_str_int(long long v){\n"
        "    char* r=(char*)malloc(32); if(!r) return NULL; snprintf(r,32,\"%lld\",v); return r;\n"
        "}\n");
    append_str(gen,
        "TERRA_UNUSED static char* terra_str_float(double v){\n"
        "    char* r=(char*)malloc(32); if(!r) return NULL; snprintf(r,32,\"%g\",v); return r;\n"
        "}\n");
    append_str(gen,
        "TERRA_UNUSED static long long terra_int_from_float(double v){ return (long long)v; }\n");
    append_str(gen,
        "TERRA_UNUSED static long long terra_int_from_str(const char* s){ return s ? atoll(s) : 0; }\n");
    append_str(gen,
        "TERRA_UNUSED static double terra_float_from_int(long long v){ return (double)v; }\n");
    append_str(gen,
        "TERRA_UNUSED static double terra_float_from_str(const char* s){ return s ? atof(s) : 0.0; }\n");
    append_str(gen,
        "TERRA_UNUSED static void terra_assert(int cond){\n"
        "    if(!cond){ fputs(\"assertion failed\\n\", stderr); exit(1); }\n"
        "}\n");
    append_str(gen,
        "TERRA_UNUSED static char* terra_input(void){\n"
        "    size_t cap=256, len=0; char* buf=(char*)malloc(cap); if(!buf) return NULL;\n"
        "    int c;\n"
        "    while((c=fgetc(stdin))!=EOF && c!='\\n'){\n"
        "        if(len+1>=cap){ cap*=2; char* nb=(char*)realloc(buf,cap); if(!nb){ free(buf); return NULL; } buf=nb; }\n"
        "        buf[len++]=(char)c;\n"
        "    }\n"
        "    buf[len]='\\0'; return buf;\n"
        "}\n\n");
}

char *c99_codegen_generate(tess_program *program) {
    if (!program) {
        return NULL;
    }

    c99_codegen gen;
    if (!c99_codegen_init(&gen, 65536)) {
        return NULL;
    }
    gen.program = program;

    // Print runtime info includes
    c99_codegen_print_runtime_info(&gen);

    // Generate class definitions
    c99_codegen_generate_classes(&gen, program);

    // Generate main function
    c99_codegen_generate_main(&gen, program);

    // Do not emit C that is known to be broken: an unsupported construct is a
    // compile error, not a comment that silently produces a no-op program.
    if (gen.failed) {
        c99_codegen_destroy(&gen);
        return NULL;
    }

    // Reserve room for the NUL terminator.
    if (!ensure_capacity(&gen, 1)) {
        c99_codegen_destroy(&gen);
        return NULL;
    }
    gen.buffer[gen.size] = '\0';

    // Hand ownership of the grown buffer to the caller.
    char *result = gen.buffer;
    gen.buffer = NULL;
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
