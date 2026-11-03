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
    RT_INPUT      = 1u << 12,
    RT_PUT_LEN    = 1u << 13
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

static bool safe_header_name(const char *header) {
    if (header == NULL || header[0] == '\0' || header[0] == '/') return false;
    size_t segment = 0;
    for (size_t i = 0;; i++) {
        unsigned char c = (unsigned char)header[i];
        if (c == '/' || c == '\0') {
            size_t n = i - segment;
            if (n == 0 || (n == 1 && header[segment] == '.') ||
                (n == 2 && header[segment] == '.' && header[segment + 1] == '.'))
                return false;
            if (c == '\0') return true;
            segment = i + 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                     c == '.' || c == '+')) {
            return false;
        }
    }
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
static bool is_record_name(c99_codegen *gen, const char *name);
static tess_record *find_record(c99_codegen *gen, const char *name);
static tess_field *find_field(tess_class *cls, const char *name);
static tess_class *find_class(c99_codegen *gen, const char *name);
static const char *expr_class(c99_codegen *gen, tess_expr *e);
static const char *fn_ret_type_name(c99_codegen *gen, const char *name);
static bool fn_returns_opaque_pointer(c99_codegen *gen, const char *name);
static bool is_builtin_type_name(const char *tess_type);
static const char *method_ret_type_name(c99_codegen *gen, tess_expr *obj_expr,
                                        const char *method_name);
static void c99_codegen_expr(c99_codegen *gen, tess_expr *expr);
static void c99_codegen_array_initializer(c99_codegen *gen, tess_expr *expr);
static const char *tess_type_to_c(const char *tess_type);
static const char *expr_element_type(c99_codegen *gen, tess_expr *expr);
static const char *expr_record_type(c99_codegen *gen, tess_expr *expr);
static bool expr_is_float_like(c99_codegen *gen, tess_expr *e);

static bool array_is_float(c99_codegen *gen, tess_expr *expr) {
    if (expr == NULL || expr->kind != TESS_EXPR_ARRAY) return false;
    for (size_t i = 0; i < expr->as.array.count; i++)
        if (expr_is_float_like(gen, expr->as.array.items[i])) return true;
    return false;
}

static void append_array_length(c99_codegen *gen, tess_expr *array) {
    if (array != NULL && array->kind == TESS_EXPR_ARRAY) {
        append_fprintf(gen, "%zu", array->as.array.count);
    } else if (array != NULL && array->kind == TESS_EXPR_IDENT &&
               get_var_class(gen, array->as.ident) != NULL &&
               strcmp(get_var_class(gen, array->as.ident), "fixedarray") == 0) {
        append_str(gen, "(sizeof("); c99_codegen_expr(gen, array);
        append_str(gen, ") / sizeof(("); c99_codegen_expr(gen, array);
        append_str(gen, ")[0]))");
    } else {
        gen->failed = true;
    }
}
static bool is_float_type_name(const char *name) {
    if (name != NULL && strncmp(name, "volatile ", 9) == 0) name += 9;
    return name != NULL && (strcmp(name, "float") == 0 ||
        strcmp(name, "f64") == 0 || strcmp(name, "c_float") == 0 ||
        strcmp(name, "c_double") == 0);
}

static bool is_pointer_type_name(const char *name) {
    return name != NULL && (strchr(name, '*') != NULL || strcmp(name, "ptr") == 0 ||
        strcmp(name, "c_void") == 0 || strcmp(name, "null") == 0 ||
        strcmp(name, "None") == 0);
}

static const char *float_c_type(const char *name) {
    if (name != NULL && strncmp(name, "volatile ", 9) == 0) name += 9;
    return strcmp(name, "c_float") == 0 ? "float" : "double";
}

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
            if (cls && strcmp(cls, "ptr") == 0) {
                static char pointer_type[160];
                snprintf(pointer_type, sizeof pointer_type, "%s*",
                         tess_type_to_c(expr_element_type(gen, expr)));
                return pointer_type;
            }
            if (is_pointer_type_name(cls)) return "void*";
            if (cls && strcmp(cls, "terra_slice") == 0) return "terra_slice";
            if (cls && (strcmp(cls, "str") == 0 || strcmp(cls, "string") == 0))
                return "const char*";
            if (is_float_type_name(cls)) return float_c_type(cls);
            if (gen->current_class != NULL) {
                tess_field *field = find_field(
                    find_class(gen, gen->current_class), expr->as.ident);
                if (field != NULL) {
                    if (strcmp(field->type, "str") == 0 ||
                        strcmp(field->type, "string") == 0) return "const char*";
                    if (is_float_type_name(field->type))
                        return float_c_type(field->type);
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
                    if (fn_returns_opaque_pointer(gen, name)) return "void*";
                    if (is_pointer_type_name(ret)) return "void*";
                    if (strcmp(ret, "str") == 0 || strcmp(ret, "string") == 0)
                        return "const char*";
                    if (is_float_type_name(ret)) return float_c_type(ret);
                    if (is_class_name(gen, ret)) return ret;
                    if (is_record_name(gen, ret)) return ret;
                }
            } else if (expr->as.call.callee &&
                       expr->as.call.callee->kind == TESS_EXPR_MEMBER) {
                const char *ret = method_ret_type_name(
                    gen, expr->as.call.callee->as.member.object,
                    expr->as.call.callee->as.member.name);
                if (ret != NULL) {
                    if (is_pointer_type_name(ret)) return "void*";
                    if (strcmp(ret, "str") == 0 || strcmp(ret, "string") == 0)
                        return "const char*";
                    if (is_float_type_name(ret)) return float_c_type(ret);
                    if (is_class_name(gen, ret)) return ret;
                    if (is_record_name(gen, ret)) return ret;
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
                if (field == NULL) {
                    tess_record *record = find_record(gen,
                        expr_record_type(gen, expr->as.member.object));
                    if (record != NULL) for (size_t i = 0; i < record->field_count; i++)
                        if (strcmp(record->fields[i].name, expr->as.member.name) == 0)
                            field = &record->fields[i];
                }
                if (field != NULL && field->type != NULL) {
                    if (strcmp(field->type, "str") == 0 ||
                        strcmp(field->type, "string") == 0)
                        return "const char*";
                    if (is_float_type_name(field->type))
                        return float_c_type(field->type);
                    if (is_pointer_type_name(field->type)) return "void*";
                    if (is_class_name(gen, field->type)) return field->type;
                } else if (field == NULL) {
                    /* A bare method reference (`n = a.get;`) calls it. */
                    const char *ret = method_ret_type_name(
                        gen, expr->as.member.object, expr->as.member.name);
                    if (ret != NULL) {
                        if (strcmp(ret, "str") == 0 ||
                            strcmp(ret, "string") == 0)
                            return "const char*";
                        if (is_float_type_name(ret))
                            return float_c_type(ret);
                        if (is_pointer_type_name(ret)) return "void*";
                        if (is_class_name(gen, ret)) return ret;
                    }
                }
            }
            return "long long";
        }
        case TESS_EXPR_BINARY: {
            if (expr_is_string_like(gen, expr)) return "const char*";
            if (expr_is_float_like(gen, expr)) return "double";
            const char *lhs_type = expr->as.binary.lhs != NULL &&
                expr->as.binary.lhs->kind == TESS_EXPR_IDENT
                ? get_var_class(gen, expr->as.binary.lhs->as.ident) : NULL;
            const char *rhs_type = expr->as.binary.rhs != NULL &&
                expr->as.binary.rhs->kind == TESS_EXPR_IDENT
                ? get_var_class(gen, expr->as.binary.rhs->as.ident) : NULL;
            if (expr->as.binary.op == TESS_BIN_SUB && lhs_type != NULL &&
                rhs_type != NULL && strcmp(lhs_type, "ptr") == 0 &&
                strcmp(rhs_type, "ptr") == 0) return "long long";
            if ((expr->as.binary.op == TESS_BIN_ADD || expr->as.binary.op == TESS_BIN_SUB) &&
                lhs_type != NULL && strcmp(lhs_type, "ptr") == 0)
                return expr_type_name(gen, expr->as.binary.lhs);
            if (expr->as.binary.op == TESS_BIN_ADD && expr->as.binary.rhs != NULL &&
                rhs_type != NULL && strcmp(rhs_type, "ptr") == 0)
                return expr_type_name(gen, expr->as.binary.rhs);
            return "long long";
        }
        case TESS_EXPR_UNARY: {
            if (expr->as.unary.op == TESS_UN_DEREFERENCE)
                return tess_type_to_c(expr_element_type(gen, expr->as.unary.operand));
            if (expr->as.unary.op == TESS_UN_ADDRESS) {
                static char pointer_type[160];
                snprintf(pointer_type, sizeof pointer_type, "%s*",
                         expr_type_name(gen, expr->as.unary.operand));
                return pointer_type;
            }
            return expr_type_name(gen, expr->as.unary.operand);
        }
        case TESS_EXPR_TERNARY:
            return expr_type_name(gen, expr->as.ternary.then_expr);
        case TESS_EXPR_ARRAY: return array_is_float(gen, expr) ? "double*" : "int64_t*";
        case TESS_EXPR_SLICE: return "terra_slice";
        case TESS_EXPR_INDEX: return tess_type_to_c(expr_element_type(gen, expr->as.index.object));
        case TESS_EXPR_RECORD_INIT: return expr->as.record_init.type_name;
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
        char **names = malloc(cap * sizeof(char*));
        char **classes = malloc(cap * sizeof(char*));
        char **elements = malloc(cap * sizeof(char*));
        if (!names || !classes || !elements) {
            free(names); free(classes); free(elements); gen->failed = true; return;
        }
        if (gen->var_count) {
            memcpy(names, gen->var_names, gen->var_count * sizeof(char*));
            memcpy(classes, gen->var_classes, gen->var_count * sizeof(char*));
            memcpy(elements, gen->var_element_types, gen->var_count * sizeof(char*));
        }
        free(gen->var_names); free(gen->var_classes); free(gen->var_element_types);
        gen->var_names = names;
        gen->var_classes = classes;
        gen->var_element_types = elements;
        gen->var_cap = cap;
    }
    gen->var_names[gen->var_count] = (char*)name;
    gen->var_classes[gen->var_count] = (char*)cls;
    gen->var_element_types[gen->var_count] = NULL;
    gen->var_count++;
}

static void record_var_element(c99_codegen *gen, const char *name,
                               const char *category, const char *element_type) {
    record_var_class(gen, name, category);
    const char *stable_type = element_type != NULL && gen->program != NULL
        ? tess_arena_strndup(&gen->program->arena, element_type, strlen(element_type))
        : element_type;
    if (stable_type == NULL) { gen->failed = true; return; }
    for (size_t i = 0; i < gen->var_count; i++) {
        if (strcmp(gen->var_names[i], name) == 0) {
            gen->var_element_types[i] = (char *)stable_type;
            return;
        }
    }
}

static const char *get_var_element(c99_codegen *gen, const char *name) {
    if (!gen || !name) return NULL;
    for (size_t i = 0; i < gen->var_count; i++)
        if (strcmp(gen->var_names[i], name) == 0) return gen->var_element_types[i];
    return NULL;
}

static void forget_var_class(c99_codegen *gen, const char *name) {
    if (!gen || !name) return;
    for (size_t i = 0; i < gen->var_count; i++) {
        if (strcmp(gen->var_names[i], name) == 0) {
            /* Swap-remove keeps updates O(1) after the lookup. */
            gen->var_names[i] = gen->var_names[gen->var_count - 1];
            gen->var_classes[i] = gen->var_classes[gen->var_count - 1];
            gen->var_element_types[i] = gen->var_element_types[gen->var_count - 1];
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

static tess_record *find_record(c99_codegen *gen, const char *name) {
    if (!gen || !gen->program || !name) return NULL;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st && st->kind == TESS_STMT_RECORD && st->as.record_decl &&
            strcmp(st->as.record_decl->name, name) == 0) return st->as.record_decl;
    }
    return NULL;
}

static bool is_record_name(c99_codegen *gen, const char *name) {
    return find_record(gen, name) != NULL;
}

static const char *expr_record_type(c99_codegen *gen, tess_expr *expr) {
    if (expr == NULL) return NULL;
    if (expr->kind == TESS_EXPR_RECORD_INIT) return expr->as.record_init.type_name;
    if (expr->kind == TESS_EXPR_IDENT) {
        const char *type = get_var_class(gen, expr->as.ident);
        return is_record_name(gen, type) ? type : NULL;
    }
    if (expr->kind == TESS_EXPR_CALL && expr->as.call.callee != NULL) {
        if (expr->as.call.callee->kind == TESS_EXPR_IDENT) {
            const char *ret = fn_ret_type_name(gen, expr->as.call.callee->as.ident);
            return is_record_name(gen, ret) ? ret : NULL;
        }
    }
    if (expr->kind == TESS_EXPR_MEMBER) {
        const char *parent_type = expr_record_type(gen, expr->as.member.object);
        tess_record *record = find_record(gen, parent_type);
        if (record != NULL) {
            for (size_t i = 0; i < record->field_count; i++)
                if (strcmp(record->fields[i].name, expr->as.member.name) == 0 &&
                    is_record_name(gen, record->fields[i].type))
                    return record->fields[i].type;
        }
    }
    return NULL;
}

static bool has_language_main(c99_codegen *gen) {
    if (gen == NULL || gen->program == NULL) return false;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st != NULL && st->kind == TESS_STMT_FN &&
            !st->as.fn.external && st->as.fn.name != NULL &&
            strcmp(st->as.fn.name, "main") == 0) return true;
    }
    return false;
}

static void append_function_ident(c99_codegen *gen, const char *name) {
    if (name != NULL && strcmp(name, "main") == 0 && has_language_main(gen))
        append_str(gen, "terra_user_main");
    else
        append_ident(gen, name);
}

/**
 * @brief Convert Tesseract type name to C99 type
 */
static const char* tess_type_to_c(const char *tess_type) {
    if (!tess_type) return "void";
    if (strncmp(tess_type, "volatile ", 9) == 0) tess_type += 9;
    const char *original_type = tess_type;
    char base_type[128];
    size_t base_len = strcspn(tess_type, "*[");
    if (base_len < sizeof base_type && tess_type[base_len] != '\0') {
        memcpy(base_type, tess_type, base_len);
        base_type[base_len] = '\0';
        tess_type = base_type;
    }
    
    if (strcmp(tess_type, "int") == 0) {
        return "int";
    } else if (strcmp(tess_type, "float") == 0 || strcmp(tess_type, "f64") == 0) {
        return "double";
    } else if (strcmp(tess_type, "c_float") == 0) {
        return "float";
    } else if (strcmp(tess_type, "c_double") == 0) {
        return "double";
    } else if (strcmp(tess_type, "bool") == 0 || strcmp(tess_type, "boolean") == 0) {
        return "bool";
    } else if (strcmp(tess_type, "c_bool") == 0) {
        return "_Bool";
    } else if (strcmp(tess_type, "str") == 0 || strcmp(tess_type, "string") == 0) {
        return "const char*";
    } else if (strcmp(tess_type, "null") == 0 || strcmp(tess_type, "None") == 0) {
        return "void*";
    } else if (strcmp(tess_type, "ptr") == 0 ||
               strcmp(tess_type, "c_void") == 0) {
        return "void*";
    } else if (strcmp(tess_type, "void") == 0) {
        return "void";
    } else if (strcmp(tess_type, "slice") == 0 ||
               strcmp(tess_type, "terra_slice") == 0) {
        return "terra_slice";
    } else if (strcmp(tess_type, "i8") == 0) {
        return "int8_t";
    } else if (strcmp(tess_type, "u8") == 0) {
        return "uint8_t";
    } else if (strcmp(tess_type, "i16") == 0) {
        return "int16_t";
    } else if (strcmp(tess_type, "u16") == 0) {
        return "uint16_t";
    } else if (strcmp(tess_type, "i32") == 0) {
        return "int32_t";
    } else if (strcmp(tess_type, "u32") == 0) {
        return "uint32_t";
    } else if (strcmp(tess_type, "i64") == 0) {
        return "int64_t";
    } else if (strcmp(tess_type, "u64") == 0) {
        return "uint64_t";
    } else if (strcmp(tess_type, "usize") == 0) {
        return "size_t";
    } else if (strcmp(tess_type, "isize") == 0) {
        return "ptrdiff_t";
    } else if (strcmp(tess_type, "c_int") == 0) {
        return "int";
    } else if (strcmp(tess_type, "c_long") == 0) {
        return "long";
    } else if (strcmp(tess_type, "c_ulong") == 0) {
        return "unsigned long";
    } else if (strcmp(tess_type, "c_char") == 0) {
        return "char";
    } else if (strcmp(tess_type, "c_schar") == 0) {
        return "signed char";
    } else if (strcmp(tess_type, "c_uchar") == 0) {
        return "unsigned char";
    } else if (strcmp(tess_type, "c_short") == 0) {
        return "short";
    } else if (strcmp(tess_type, "c_ushort") == 0) {
        return "unsigned short";
    } else if (strcmp(tess_type, "c_uint") == 0) {
        return "unsigned int";
    } else if (strcmp(tess_type, "c_longlong") == 0) {
        return "long long";
    } else if (strcmp(tess_type, "c_ulonglong") == 0) {
        return "unsigned long long";
    } else if (strcmp(tess_type, "size_t") == 0) {
        return "size_t";
    } else if (strcmp(tess_type, "ptrdiff_t") == 0) {
        return "ptrdiff_t";
    }
    
    // Assume it's a class type (user-defined)
    return original_type;
}

static const char *expr_element_type(c99_codegen *gen, tess_expr *expr) {
    static char base[128];
    const char *type = NULL;
    if (expr == NULL) return "i64";
    if (expr->kind == TESS_EXPR_UNARY && expr->as.unary.op == TESS_UN_ADDRESS)
        return expr_type_name(gen, expr->as.unary.operand);
    if (expr->kind == TESS_EXPR_UNARY && expr->as.unary.op == TESS_UN_DEREFERENCE)
        return expr_element_type(gen, expr->as.unary.operand);
    if (expr->kind == TESS_EXPR_ARRAY) return array_is_float(gen, expr) ? "f64" : "i64";
    if (expr->kind == TESS_EXPR_SLICE) return expr_element_type(gen, expr->as.slice.object);
    if (expr->kind == TESS_EXPR_IDENT) type = get_var_element(gen, expr->as.ident);
    if (expr->kind == TESS_EXPR_INDEX) return expr_element_type(gen, expr->as.index.object);
    if (type == NULL) return "i64";
    if (strncmp(type, "slice:", 6) == 0) type += 6;
    if (strncmp(type, "fixedarray:", 11) == 0) type += 11;
    if (strncmp(type, "volatile ", 9) == 0) type += 9;
    size_t n = strcspn(type, "*[");
    if (n >= sizeof base) n = sizeof base - 1;
    memcpy(base, type, n); base[n] = '\0';
    return base;
}

static const char *slice_element_decl_type(const char *type) {
    static char element[128];
    if (type == NULL || strncmp(type, "slice[", 6) != 0) return "i64";
    const char *begin = type + 6;
    size_t n = strcspn(begin, "]");
    if (n >= sizeof element) n = sizeof element - 1;
    memcpy(element, begin, n); element[n] = '\0';
    return element;
}

/**
 * @brief True when the type name is a built-in scalar/string type.
 */
static bool is_builtin_type_name(const char *tess_type) {
    if (!tess_type) return false;
    if (strncmp(tess_type, "volatile ", 9) == 0) tess_type += 9;
    if (strpbrk(tess_type, "*[") != NULL) return true;
    return strcmp(tess_type, "int") == 0 || strcmp(tess_type, "i32") == 0 ||
           strcmp(tess_type, "float") == 0 || strcmp(tess_type, "f64") == 0 ||
           strcmp(tess_type, "c_float") == 0 ||
           strcmp(tess_type, "c_double") == 0 ||
           strcmp(tess_type, "bool") == 0 || strcmp(tess_type, "boolean") == 0 ||
           strcmp(tess_type, "str") == 0 || strcmp(tess_type, "string") == 0 ||
           strcmp(tess_type, "null") == 0 || strcmp(tess_type, "None") == 0 ||
           strcmp(tess_type, "ptr") == 0 || strcmp(tess_type, "void") == 0 ||
           strcmp(tess_type, "c_void") == 0 || strcmp(tess_type, "c_bool") == 0 ||
           strcmp(tess_type, "i8") == 0 || strcmp(tess_type, "u8") == 0 ||
           strcmp(tess_type, "i16") == 0 || strcmp(tess_type, "u16") == 0 ||
           strcmp(tess_type, "u32") == 0 || strcmp(tess_type, "i64") == 0 ||
           strcmp(tess_type, "u64") == 0 || strcmp(tess_type, "usize") == 0 ||
           strcmp(tess_type, "isize") == 0 || strcmp(tess_type, "c_int") == 0 ||
           strcmp(tess_type, "c_long") == 0 || strcmp(tess_type, "c_ulong") == 0 ||
           strcmp(tess_type, "c_char") == 0 || strcmp(tess_type, "c_schar") == 0 ||
           strcmp(tess_type, "c_uchar") == 0 || strcmp(tess_type, "c_short") == 0 ||
           strcmp(tess_type, "c_ushort") == 0 || strcmp(tess_type, "c_uint") == 0 ||
           strcmp(tess_type, "c_longlong") == 0 ||
           strcmp(tess_type, "c_ulonglong") == 0 ||
           strcmp(tess_type, "size_t") == 0 || strcmp(tess_type, "ptrdiff_t") == 0 ||
           strcmp(tess_type, "terra_slice") == 0 || strcmp(tess_type, "slice") == 0 ||
           strcmp(tess_type, "int64_t*") == 0;
}

/**
 * @brief Emit a C declarator: "<type><* if class> <name>"
 *
 * Instances of user-defined classes are heap objects, so fields and
 * parameters of class type are pointers.
 */
static void append_declarator(c99_codegen *gen, const char *tess_type,
                              const char *name) {
    bool is_volatile = tess_type != NULL && strncmp(tess_type, "volatile ", 9) == 0;
    if (is_volatile) tess_type += 9;
    bool generic_slice = tess_type != NULL && strncmp(tess_type, "slice[", 6) == 0;
    size_t base_len = generic_slice ? 5 : (tess_type != NULL ? strcspn(tess_type, "*[") : 0);
    char base[128];
    if (base_len >= sizeof base) base_len = sizeof base - 1;
    if (tess_type != NULL) memcpy(base, tess_type, base_len);
    base[base_len] = '\0';
    const char *c_type = tess_type_to_c(base);
    append_fprintf(gen, "%s%s", is_volatile ? "volatile " : "", c_type);
    size_t pointer_count = 0;
    for (size_t i = base_len; tess_type != NULL && tess_type[i] == '*'; i++) pointer_count++;
    if (pointer_count == 0 && !is_builtin_type_name(tess_type) && !is_record_name(gen, base))
        pointer_count = 1;
    while (pointer_count--) append_str(gen, "*");
    append_str(gen, " ");
    append_ident(gen, name);
    if (!generic_slice && tess_type != NULL && tess_type[base_len] == '*') {
        size_t i = base_len;
        while (tess_type[i] == '*') i++;
        if (tess_type[i] == '[') append_str(gen, tess_type + i);
    } else if (!generic_slice && tess_type != NULL && tess_type[base_len] == '[') {
        append_str(gen, tess_type + base_len);
    }
}

static void append_return_type(c99_codegen *gen, const char *tess_type) {
    bool is_volatile = tess_type != NULL && strncmp(tess_type, "volatile ", 9) == 0;
    if (is_volatile) tess_type += 9;
    bool generic_slice = tess_type != NULL && strncmp(tess_type, "slice[", 6) == 0;
    size_t base_len = generic_slice ? 5 : (tess_type != NULL ? strcspn(tess_type, "*[") : 0);
    char base[128];
    if (base_len >= sizeof base) base_len = sizeof base - 1;
    if (tess_type != NULL) memcpy(base, tess_type, base_len);
    base[base_len] = '\0';
    append_fprintf(gen, "%s%s", is_volatile ? "volatile " : "", tess_type_to_c(base));
    if (tess_type != NULL) {
        for (size_t i = base_len; tess_type[i] == '*'; i++) append_str(gen, "*");
        if (!generic_slice && tess_type[base_len] == '[') append_str(gen, "*");
    }
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
            return is_float_type_name(cls);
        }
        case TESS_EXPR_MEMBER: {
            /* Float-typed field access (self->b, t->value) */
            if (!e->as.member.object || !e->as.member.name) return false;
            const char *cls = expr_class(gen, e->as.member.object);
            if (cls == NULL) return false;
            tess_field *field = find_field(find_class(gen, cls), e->as.member.name);
            return field != NULL && is_float_type_name(field->type);
        }
        case TESS_EXPR_CALL: {
            if (e->as.call.callee && e->as.call.callee->kind == TESS_EXPR_IDENT) {
                if (strcmp(e->as.call.callee->as.ident, "float") == 0) {
                    return true;
                }
                const char *ret = fn_ret_type_name(
                    gen, e->as.call.callee->as.ident);
                if (is_float_type_name(ret)) {
                    return true;
                }
            } else if (e->as.call.callee &&
                       e->as.call.callee->kind == TESS_EXPR_MEMBER) {
                const char *ret = method_ret_type_name(
                    gen, e->as.call.callee->as.member.object,
                    e->as.call.callee->as.member.name);
                if (is_float_type_name(ret)) {
                    return true;
                }
            }
            return false;
        }
        case TESS_EXPR_INDEX:
            return is_float_type_name(expr_element_type(gen, e->as.index.object));
        case TESS_EXPR_UNARY:
            if (e->as.unary.op == TESS_UN_DEREFERENCE)
                return is_float_type_name(expr_element_type(gen, e->as.unary.operand));
            return e->as.unary.op != TESS_UN_ADDRESS &&
                   expr_is_float_like(gen, e->as.unary.operand);
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

static bool fn_returns_opaque_pointer(c99_codegen *gen, const char *name) {
    if (gen == NULL || gen->program == NULL || name == NULL) return false;
    for (size_t i = 0; i < gen->program->count; i++) {
        tess_stmt *st = gen->program->items[i];
        if (st == NULL || st->kind != TESS_STMT_FN ||
            !st->as.fn.external || st->as.fn.ret_type == NULL ||
            strcmp(st->as.fn.name, name) != 0) continue;
        return !is_builtin_type_name(st->as.fn.ret_type) &&
               !is_class_name(gen, st->as.fn.ret_type) &&
               !is_record_name(gen, st->as.fn.ret_type);
    }
    return false;
}

static void append_fn_return_type(c99_codegen *gen, tess_stmt *stmt) {
    const char *ret = stmt->as.fn.ret_type;
    if (ret == NULL || strcmp(ret, "null") == 0) {
        append_str(gen, "void ");
    } else if (stmt->as.fn.external && !is_builtin_type_name(ret) &&
               !is_class_name(gen, ret) && !is_record_name(gen, ret)) {
        append_fprintf(gen, "%s* ", ret);
    } else {
        append_return_type(gen, ret);
        append_str(gen, " ");
    }
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
        case TESS_EXPR_INDEX: {
            const char *slice_type = expr->as.index.object != NULL &&
                expr->as.index.object->kind == TESS_EXPR_IDENT
                ? get_var_class(gen, expr->as.index.object->as.ident) : NULL;
            if ((expr->as.index.object != NULL &&
                 expr->as.index.object->kind == TESS_EXPR_SLICE) ||
                (slice_type != NULL && strcmp(slice_type, "terra_slice") == 0)) {
                append_str(gen, "((");
                append_str(gen, tess_type_to_c(expr_element_type(gen, expr->as.index.object)));
                append_str(gen, "*)(("); c99_codegen_expr(gen, expr->as.index.object);
                append_str(gen, ").data))[" ); c99_codegen_expr(gen, expr->as.index.index);
                append_str(gen, "]");
                break;
            }
            append_str(gen, "("); c99_codegen_expr(gen, expr->as.index.object);
            append_str(gen, "["); c99_codegen_expr(gen, expr->as.index.index);
            append_str(gen, "])");
            break;
        }
        case TESS_EXPR_ARRAY: {
            append_str(gen, array_is_float(gen, expr) ? "((double[]){" : "((int64_t[]){");
            for (size_t i = 0; i < expr->as.array.count; i++) {
                if (i) append_str(gen, ", ");
                c99_codegen_expr(gen, expr->as.array.items[i]);
            }
            append_str(gen, "})");
            break;
        }
        case TESS_EXPR_SLICE: {
            append_str(gen, "((terra_slice){ .data = (void*)&(");
            c99_codegen_expr(gen, expr->as.slice.object);
            append_str(gen, "[");
            if (expr->as.slice.start) c99_codegen_expr(gen, expr->as.slice.start);
            else append_str(gen, "0");
            append_str(gen, "]), .len = (size_t)(");
            if (expr->as.slice.end) c99_codegen_expr(gen, expr->as.slice.end);
            else append_array_length(gen, expr->as.slice.object);
            append_str(gen, " - ");
            if (expr->as.slice.start) c99_codegen_expr(gen, expr->as.slice.start);
            else append_str(gen, "0");
            append_str(gen, ") })");
            break;
        }
        case TESS_EXPR_RECORD_INIT: {
            append_str(gen, "("); append_str(gen, expr->as.record_init.type_name);
            append_str(gen, "){ ");
            for (size_t i = 0; i < expr->as.record_init.count; i++) {
                if (i) append_str(gen, ", ");
                append_str(gen, "."); append_ident(gen, expr->as.record_init.names[i]);
                append_str(gen, " = ");
                if (expr->as.record_init.values[i] != NULL &&
                    expr->as.record_init.values[i]->kind == TESS_EXPR_ARRAY)
                    c99_codegen_array_initializer(gen, expr->as.record_init.values[i]);
                else
                    c99_codegen_expr(gen, expr->as.record_init.values[i]);
            }
            append_str(gen, " }");
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
            append_function_ident(gen, expr->as.ident);
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

            if (is_record_name(gen, expr_record_type(gen, obj_expr))) {
                append_str(gen, "("); c99_codegen_expr(gen, obj_expr);
                append_str(gen, ")."); append_ident(gen, member);
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
                /* Dispatch by argument type: put_int / put_float / put_str.
                 * String literals carry their byte length in the AST, so they
                 * print through embedded NULs via the length-aware put_len. */
                if (arg0->kind == TESS_EXPR_STRING) {
                    gen->runtime_used |= RT_PUT_LEN;
                    append_str(gen, "put_len(");
                    append_c_string_literal(gen, arg0->as.string.data,
                                            arg0->as.string.len);
                    char lenbuf[32];
                    snprintf(lenbuf, sizeof lenbuf, "%zu",
                             arg0->as.string.len);
                    append_str(gen, ", ");
                    append_str(gen, lenbuf);
                    append_str(gen, ")");
                    break;
                }
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
                       strcmp(call_name, "len") == 0 &&
                       (arg0->kind == TESS_EXPR_SLICE ||
                        (arg0->kind == TESS_EXPR_IDENT &&
                         get_var_class(gen, arg0->as.ident) != NULL &&
                         strcmp(get_var_class(gen, arg0->as.ident), "terra_slice") == 0))) {
                append_str(gen, "("); c99_codegen_expr(gen, arg0);
                append_str(gen, ").len");
                break;
            } else if (call_name != NULL && arg0 != NULL &&
                       strcmp(call_name, "len") == 0 &&
                       (arg0->kind == TESS_EXPR_ARRAY ||
                        (arg0->kind == TESS_EXPR_IDENT &&
                         get_var_class(gen, arg0->as.ident) != NULL &&
                         strcmp(get_var_class(gen, arg0->as.ident), "fixedarray") == 0))) {
                append_array_length(gen, arg0);
                break;
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
                case TESS_UN_ADDRESS: append_str(gen, "(&("); break;
                case TESS_UN_DEREFERENCE: append_str(gen, "(*("); break;
            }
            c99_codegen_expr(gen, expr->as.unary.operand);
            append_str(gen, (expr->as.unary.op == TESS_UN_ADDRESS || expr->as.unary.op == TESS_UN_DEREFERENCE) ? "))" : ")");
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

static void c99_codegen_array_initializer(c99_codegen *gen, tess_expr *expr) {
    if (expr == NULL || expr->kind != TESS_EXPR_ARRAY) {
        c99_codegen_expr(gen, expr);
        return;
    }
    append_str(gen, "{");
    for (size_t i = 0; i < expr->as.array.count; i++) {
        if (i) append_str(gen, ", ");
        c99_codegen_array_initializer(gen, expr->as.array.items[i]);
    }
    append_str(gen, "}");
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
        case TESS_STMT_RECORD:
            /* Emitted in the type prelude. */
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
                bool target_emitted = false;
                bool array_initializer = false;
                /* Declare new bare variables on first assignment. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT) {
                    const char *name = stmt->as.assign.target->as.ident;
                    /* A first assignment is a declaration; resetting this
                     * set at branch boundaries preserves C block scope. */
                    if (!is_declared_var(gen, name) &&
                        !(gen->current_class && is_field_of_current_class(gen, name))) {
                        const char *class_type = stmt->as.assign.declared_type == NULL
                            ? construction_class(gen, stmt->as.assign.value) : NULL;
                        if (class_type != NULL) {
                            append_fprintf(gen, "%s* ", class_type);
                            record_var_class(gen, name, class_type);
                        } else {
                            if (stmt->as.assign.value->kind == TESS_EXPR_ARRAY &&
                                stmt->as.assign.declared_type == NULL) {
                                append_fprintf(gen, "%s %s[%zu] ",
                                    array_is_float(gen, stmt->as.assign.value) ? "double" : "int64_t", name,
                                    stmt->as.assign.value->as.array.count);
                                target_emitted = true;
                                array_initializer = true;
                            }
                            if (stmt->as.assign.value->kind == TESS_EXPR_CALL &&
                                stmt->as.assign.value->as.call.callee &&
                                stmt->as.assign.value->as.call.callee->kind == TESS_EXPR_IDENT) {
                                const char *ret = fn_ret_type_name(gen,
                                    stmt->as.assign.value->as.call.callee->as.ident);
                                if (is_record_name(gen, ret))
                                    record_var_class(gen, name, ret);
                            }
                            if (target_emitted) {
                                /* Fixed local array declarator emitted above. */
                            } else if (stmt->as.assign.declared_type != NULL) {
                                append_declarator(gen, stmt->as.assign.declared_type, name);
                                target_emitted = true;
                                array_initializer = strchr(stmt->as.assign.declared_type, '[') != NULL &&
                                    stmt->as.assign.value->kind == TESS_EXPR_ARRAY;
                                if (is_record_name(gen, stmt->as.assign.declared_type))
                                    record_var_class(gen, name, stmt->as.assign.declared_type);
                            } else {
                                append_fprintf(gen, "%s ", expr_type_name(gen, stmt->as.assign.value));
                            }
                        }
                        mark_declared_var(gen, name);
                    }
                }
                if (!target_emitted) c99_codegen_expr(gen, stmt->as.assign.target);
                /* Refresh tracked type: a variable can be reassigned with a
                 * different type, so stale entries must not survive. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT) {
                    forget_var_class(gen, stmt->as.assign.target->as.ident);
                    const char *assigned_c_type = expr_type_name(gen, stmt->as.assign.value);
                    if (!expr_is_string_like(gen, stmt->as.assign.value) &&
                        assigned_c_type != NULL && strchr(assigned_c_type, '*') != NULL)
                        record_var_element(gen, stmt->as.assign.target->as.ident,
                                           "ptr", assigned_c_type);
                    if (stmt->as.assign.value->kind == TESS_EXPR_RECORD_INIT)
                        record_var_class(gen, stmt->as.assign.target->as.ident,
                                         stmt->as.assign.value->as.record_init.type_name);
                    if (is_record_name(gen, stmt->as.assign.declared_type))
                        record_var_class(gen, stmt->as.assign.target->as.ident,
                                         stmt->as.assign.declared_type);
                    if (stmt->as.assign.declared_type != NULL &&
                        strchr(stmt->as.assign.declared_type, '[') != NULL)
                        record_var_element(gen, stmt->as.assign.target->as.ident,
                                           "fixedarray", stmt->as.assign.declared_type);
                    else if (array_initializer)
                        record_var_element(gen, stmt->as.assign.target->as.ident,
                                           "fixedarray", array_is_float(gen, stmt->as.assign.value) ? "f64" : "i64");
                    if (stmt->as.assign.value->kind == TESS_EXPR_SLICE)
                        record_var_element(gen, stmt->as.assign.target->as.ident,
                                           "terra_slice",
                                           expr_element_type(gen, stmt->as.assign.value));
                    if (stmt->as.assign.value->kind == TESS_EXPR_UNARY &&
                        stmt->as.assign.value->as.unary.op == TESS_UN_ADDRESS)
                        record_var_element(gen, stmt->as.assign.target->as.ident,
                                           "ptr",
                                           expr_type_name(gen,
                                               stmt->as.assign.value->as.unary.operand));
                    if (stmt->as.assign.declared_type != NULL &&
                        strchr(stmt->as.assign.declared_type, '*') != NULL)
                        record_var_element(gen, stmt->as.assign.target->as.ident,
                                           "ptr", stmt->as.assign.declared_type);
                }
                /* Track string variables for concat handling. */
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_STRING) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "str");
                }
                if (stmt->as.assign.target->kind == TESS_EXPR_IDENT &&
                    stmt->as.assign.value->kind == TESS_EXPR_SLICE) {
                    record_var_class(gen, stmt->as.assign.target->as.ident, "terra_slice");
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
                        } else if (is_float_type_name(ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                        } else if (is_pointer_type_name(ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "ptr");
                        } else if (is_class_name(gen, ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, ret);
                        } else if (is_record_name(gen, ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, ret);
                        }
                    }
                    if (stmt->as.assign.value->as.call.callee->kind ==
                            TESS_EXPR_IDENT &&
                        fn_returns_opaque_pointer(
                            gen, stmt->as.assign.value->as.call.callee->as.ident)) {
                        record_var_class(gen, stmt->as.assign.target->as.ident,
                                         "ptr");
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
                        } else if (is_float_type_name(ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "float");
                        } else if (is_pointer_type_name(ret)) {
                            record_var_class(gen, stmt->as.assign.target->as.ident, "ptr");
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
                    const char *element = get_var_element(gen, stmt->as.assign.value->as.ident);
                    if (element != NULL && cls != NULL)
                        record_var_element(gen, stmt->as.assign.target->as.ident, cls, element);
                }
                append_str(gen, " = ");
                if (array_initializer || (stmt->as.assign.declared_type != NULL &&
                    stmt->as.assign.value->kind == TESS_EXPR_ARRAY &&
                    strchr(stmt->as.assign.declared_type, '[') != NULL)) {
                    c99_codegen_array_initializer(gen, stmt->as.assign.value);
                } else {
                    c99_codegen_expr(gen, stmt->as.assign.value);
                }
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

        case TESS_STMT_FOR: {
            size_t saved_declared = gen->declared_count;
            size_t saved_vars = gen->var_count;
            append_indent(gen);
            append_str(gen, "{\n");
            gen->indent_level++;
            if (stmt->as.for_stmt.init != NULL &&
                stmt->as.for_stmt.init->kind != TESS_STMT_EMPTY) {
                c99_codegen_stmt(gen, stmt->as.for_stmt.init);
            }
            append_indent(gen);
            append_str(gen, "for (; ");
            if (stmt->as.for_stmt.condition != NULL)
                c99_codegen_expr(gen, stmt->as.for_stmt.condition);
            else
                append_str(gen, "true");
            append_str(gen, "; ");
            if (stmt->as.for_stmt.step != NULL)
                c99_codegen_expr(gen, stmt->as.for_stmt.step);
            append_str(gen, ") {\n");
            gen->indent_level++;
            c99_codegen_body(gen, stmt->as.for_stmt.body);
            gen->declared_count = saved_declared;
            gen->var_count = saved_vars;
            gen->indent_level--;
            append_indent(gen);
            append_str(gen, "}\n");
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
        case TESS_STMT_LINK:
            /* Link directives are consumed by the executable build driver. */
            break;
        case TESS_STMT_BREAK:
            append_indent(gen);
            append_str(gen, "break;\n");
            break;
        case TESS_STMT_CONTINUE:
            append_indent(gen);
            append_str(gen, "continue;\n");
            break;
        case TESS_STMT_ASM:
            append_indent(gen);
            append_str(gen, stmt->as.assembly.is_volatile ? "__asm__ volatile (" : "__asm__ (");
            append_c_string_literal(gen, stmt->as.assembly.text,
                                    strlen(stmt->as.assembly.text));
            append_str(gen, " : : : \"memory\");\n");
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
        } else if (is_float_type_name(param->type)) {
            record_var_class(gen, param->name, "float");
        } else if (is_record_name(gen, param->type)) {
            record_var_class(gen, param->name, param->type);
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
    if (method_void) append_str(gen, "void ");
    else { append_return_type(gen, method->ret_type); append_str(gen, " "); }
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
    if (stmt->as.fn.external) return;
    reset_function_state(gen);
    gen->current_class = NULL;
    tess_stmt *fn = stmt;
    const char *name = fn->as.fn.name;
    if (!name) { gen->failed = true; return; }
    bool returns_void = fn->as.fn.ret_type == NULL ||
                        strcmp(fn->as.fn.ret_type, "null") == 0;
    gen->in_function = true;
    gen->fn_returns_void = returns_void;
    if (!gen->freestanding) append_str(gen, "static ");
    if (returns_void) append_str(gen, "void ");
    else { append_return_type(gen, fn->as.fn.ret_type); append_str(gen, " "); }
    append_function_ident(gen, name);
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
        if (is_float_type_name(fn->as.fn.params[i].type))
            record_var_class(gen, fn->as.fn.params[i].name, "float");
        if (is_record_name(gen, fn->as.fn.params[i].type))
            record_var_class(gen, fn->as.fn.params[i].name,
                             fn->as.fn.params[i].type);
        if (fn->as.fn.params[i].type &&
            (strcmp(fn->as.fn.params[i].type, "slice") == 0 ||
             strcmp(fn->as.fn.params[i].type, "terra_slice") == 0 ||
             strncmp(fn->as.fn.params[i].type, "slice[", 6) == 0))
            record_var_element(gen, fn->as.fn.params[i].name, "terra_slice",
                               slice_element_decl_type(fn->as.fn.params[i].type));
        else if (fn->as.fn.params[i].type && strchr(fn->as.fn.params[i].type, '*'))
            record_var_element(gen, fn->as.fn.params[i].name, "ptr",
                               fn->as.fn.params[i].type);
        else if (fn->as.fn.params[i].type && strchr(fn->as.fn.params[i].type, '['))
            record_var_element(gen, fn->as.fn.params[i].name, "ptr",
                               fn->as.fn.params[i].type);
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
        if (!stmt->as.fn.external && !gen->freestanding) append_str(gen, "static ");
        append_fn_return_type(gen, stmt);
        append_function_ident(gen, stmt->as.fn.name);
        append_str(gen, "(");
        if (stmt->as.fn.param_count == 0) append_str(gen, "void");
        for (size_t j = 0; j < stmt->as.fn.param_count; j++) {
            if (j) append_str(gen, ", ");
            append_declarator(gen, stmt->as.fn.params[j].type,
                              stmt->as.fn.params[j].name);
        }
        if (stmt->as.fn.variadic) {
            if (stmt->as.fn.param_count != 0) append_str(gen, ", ");
            append_str(gen, "...");
        }
        append_str(gen, ");\n");
    }
    append_str(gen, "\n");
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (stmt && stmt->kind == TESS_STMT_FN && !stmt->as.fn.external)
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

    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (!stmt || stmt->kind != TESS_STMT_RECORD || !stmt->as.record_decl) continue;
        tess_record *record = stmt->as.record_decl;
        append_fprintf(gen, "typedef %s %s %s;\n",
                       record->is_union ? "union" : "struct",
                       record->name, record->name);
    }
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *stmt = program->items[i];
        if (!stmt || stmt->kind != TESS_STMT_RECORD || !stmt->as.record_decl) continue;
        tess_record *record = stmt->as.record_decl;
        append_fprintf(gen, "%s %s {\n", record->is_union ? "union" : "struct", record->name);
        for (size_t j = 0; j < record->field_count; j++) {
            append_str(gen, "    ");
            append_declarator(gen, record->fields[j].type, record->fields[j].name);
            append_str(gen, ";\n");
        }
        append_str(gen, "};\n\n");
    }

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
            if (m_void) append_str(gen, "void ");
            else { append_return_type(gen, method->ret_type); append_str(gen, " "); }
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

    if (gen->freestanding) {
        for (size_t i = 0; i < program->count; i++) {
            tess_stmt *st = program->items[i];
            if (st && st->kind != TESS_STMT_FN && st->kind != TESS_STMT_CLASS &&
                st->kind != TESS_STMT_RECORD && st->kind != TESS_STMT_IMPORT &&
                st->kind != TESS_STMT_LINK) {
                gen->failed = true;
                return;
            }
        }
        return;
    }

    reset_function_state(gen);
    gen->current_class = NULL;

    append_str(gen, "\nint main(int argc, char** argv) {\n");
    gen->indent_level++;

    append_indent(gen);
    append_str(gen, "(void)argc;\n");
    append_indent(gen);
    append_str(gen, "(void)argv;\n\n");

    if (has_language_main(gen)) {
        tess_stmt *entry = NULL;
        for (size_t i = 0; i < program->count; i++) {
            tess_stmt *stmt = program->items[i];
            if (stmt != NULL && stmt->kind == TESS_STMT_FN &&
                stmt->as.fn.name != NULL &&
                strcmp(stmt->as.fn.name, "main") == 0) {
                entry = stmt;
                break;
            }
        }
        append_indent(gen);
        if (entry != NULL && entry->as.fn.ret_type != NULL &&
            strcmp(entry->as.fn.ret_type, "null") != 0) {
            append_str(gen, "return terra_user_main();\n");
        } else {
            append_str(gen, "terra_user_main();\n");
            append_indent(gen);
            append_str(gen, "return 0;\n");
        }
    } else {
        for (size_t i = 0; i < program->count; i++) {
            tess_stmt *stmt = program->items[i];
            if (stmt && stmt->kind != TESS_STMT_CLASS) {
                c99_codegen_stmt(gen, stmt);
            }
        }
        append_str(gen, "\n");
        append_indent(gen);
        append_str(gen, "return 0;\n");
    }
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
    gen->var_element_types = NULL;
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
    free(gen->var_element_types);
    gen->var_names = NULL;
    gen->var_classes = NULL;
    gen->var_element_types = NULL;
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
    if (gen->freestanding) {
        append_str(gen, "#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n\n");
        if (gen->program != NULL) {
            for (size_t i = 0; i < gen->program->count; i++) {
                tess_stmt *st = gen->program->items[i];
                if (st == NULL || st->kind != TESS_STMT_IMPORT ||
                    st->as.import.module == NULL ||
                    strcmp(st->as.import.module, "c") != 0 ||
                    !safe_header_name(st->as.import.path)) continue;
                append_str(gen, "#include <"); append_str(gen, st->as.import.path);
                append_str(gen, ">\n");
            }
            append_str(gen, "\n");
        }
        append_str(gen, "typedef struct { void *data; size_t len; } terra_slice;\n\n");
        return;
    }
    append_str(gen, "#include <stdio.h>\n");
    append_str(gen, "#include <stdlib.h>\n");
    append_str(gen, "#include <string.h>\n");
    append_str(gen, "#include <stdbool.h>\n");
    append_str(gen, "#include <stdint.h>\n#include <stddef.h>\n#include <math.h>\n\n");
    /* Imports add only quoted, validated system header names. Include them in
     * the generated translation unit so the C compiler type-checks FFI calls. */
    if (gen->program != NULL) {
        for (size_t i = 0; i < gen->program->count; i++) {
            tess_stmt *st = gen->program->items[i];
            if (st == NULL || st->kind != TESS_STMT_IMPORT ||
                st->as.import.module == NULL ||
                strcmp(st->as.import.module, "c") != 0 ||
                !safe_header_name(st->as.import.path)) continue;
            append_str(gen, "#include <");
            append_str(gen, st->as.import.path);
            append_str(gen, ">\n");
        }
        append_str(gen, "\n");
    }
    append_str(gen, "#include <errno.h>\n#include <limits.h>\n\n");
    append_str(gen, "typedef struct { void *data; size_t len; } terra_slice;\n\n");
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
    if (gen->runtime_used & RT_PUT_LEN) {
        append_str(gen,
            "TERRA_MAYBE_UNUSED static inline void put_len(const char* s, size_t n){ if(s) fwrite(s, 1, n, stdout); putchar('\\n'); }\n");
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
    rt.program = gen->program;
    rt.freestanding = gen->freestanding;
    if (rt.freestanding) {
        bool has_class = false;
        for (size_t i = 0; i < gen->program->count; i++)
            if (gen->program->items[i] && gen->program->items[i]->kind == TESS_STMT_CLASS)
                has_class = true;
        if (has_class || rt.runtime_used != 0) {
            gen->failed = true;
            c99_codegen_destroy(&rt);
            return false;
        }
    }
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
    return c99_codegen_generate_mode(program, false);
}

char *c99_codegen_generate_mode(tess_program *program, bool freestanding) {
    if (!program) {
        return NULL;
    }

    c99_codegen gen;
    if (!c99_codegen_init(&gen, 4096)) {
        return NULL;
    }
    gen.program = program;
    gen.freestanding = freestanding;
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
