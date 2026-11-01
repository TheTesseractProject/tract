#include "sema.h"

#include "core_alloc.h"
#include "core_diagnostics.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Symbol table: open-addressing hash map, keys are interned C strings.
 * Each entry stores the symbol name, kind, and inferred type.
 * ------------------------------------------------------------------------- */

typedef enum {
    SYM_VAR,        /* local variable or parameter                */
    SYM_CLASS,      /* class name                                  */
    SYM_BUILTIN,    /* built-in function (put, assert, ...)         */
} sym_kind;

typedef enum {
    TYPE_INT,       /* integer                                      */
    TYPE_FLOAT,     /* floating-point                               */
    TYPE_STR,       /* string                                       */
    TYPE_BOOL,      /* boolean                                      */
    TYPE_NULL,      /* null                                         */
    TYPE_CLASS,     /* class instance (class name provided)         */
} tess_type;

typedef struct {
    const char *name;   /* NULL -> empty slot                         */
    sym_kind    kind;
    tess_type   type;
    union {
        tess_class *cls;        /* for SYM_CLASS or TYPE_CLASS          */
        struct {
            int arity;          /* for SYM_BUILTIN: expected args      */
            bool variadic;      /* true if variadic                    */
        } builtin;
    } as;
} sym_entry;

#define SEMA_HTABLE_CAP 64u   /* must be power-of-two */

typedef struct tess_scope {
    struct tess_scope *parent;
    bool in_method;           /* true -> return is allowed           */
    sym_entry table[SEMA_HTABLE_CAP];
} tess_scope;

/* FNV-1a 32-bit (fast, good distribution for short strings) */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

static sym_entry *scope_lookup_own(tess_scope *sc, const char *name) {
    uint32_t idx = fnv1a(name) & (SEMA_HTABLE_CAP - 1);
    for (uint32_t i = 0; i < SEMA_HTABLE_CAP; i++) {
        uint32_t slot = (idx + i) & (SEMA_HTABLE_CAP - 1);
        sym_entry *e = &sc->table[slot];
        if (e->name == NULL) return NULL;
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

static sym_entry *scope_lookup(tess_scope *sc, const char *name) {
    for (tess_scope *cur = sc; cur != NULL; cur = cur->parent) {
        sym_entry *e = scope_lookup_own(cur, name);
        if (e != NULL) return e;
    }
    return NULL;
}

/* Returns false if the table is full (shouldn't happen at cap=64 for programs
 * we expect, but we handle it gracefully). */
static bool scope_define(tess_scope *sc, const char *name, sym_kind kind,
                         tess_type type, tess_class *cls, int arity, bool variadic) {
    uint32_t idx = fnv1a(name) & (SEMA_HTABLE_CAP - 1);
    for (uint32_t i = 0; i < SEMA_HTABLE_CAP; i++) {
        uint32_t slot = (idx + i) & (SEMA_HTABLE_CAP - 1);
        sym_entry *e = &sc->table[slot];
        if (e->name == NULL) {
            e->name  = name;
            e->kind  = kind;
            e->type  = type;
            if (kind == SYM_CLASS) {
                e->as.cls = cls;
            } else if (kind == SYM_BUILTIN) {
                e->as.builtin.arity = arity;
                e->as.builtin.variadic = variadic;
            }
            return true;
        }
        if (strcmp(e->name, name) == 0) {
            /* Redefinition: update in place so caller can detect via return. */
            return false;
        }
    }
    return false; /* table full */
}

/* -------------------------------------------------------------------------
 * Analyser context
 * ------------------------------------------------------------------------- */

typedef struct {
    const char  *src;
    const char  *path;
    bool         failed;
    tess_scope  *scope;
    /* Current class being analysed (NULL at top level). */
    tess_class  *current_class;
} tess_sema;

static void report(tess_sema *s, uint32_t start, uint32_t end,
                   const char *msg) {
    if (s->failed) return;   /* report only the first error */
    s->failed = true;

    const char *name = s->path ? s->path : "<input>";
    core_located *at = core_locate(s->src, (core_span){start, end}, msg);
    if (at == NULL) {
        fprintf(stderr, "%s: error: %s\n", name, msg);
        return;
    }
    char *block = core_format_block(name, msg, at);
    if (block == NULL) {
        fprintf(stderr, "%s: error: %s\n", name, msg);
        core_located_free(at);
        return;
    }
    fputs(block, stderr);
    size_t n = strlen(block);
    if (n == 0 || block[n - 1] != '\n') fputc('\n', stderr);
    core_free(block);
    core_located_free(at);
}

/* Push/pop scopes on the C stack — they are stack-allocated, so no heap. */
static void push_scope(tess_sema *s, tess_scope *sc, bool in_method) {
    memset(sc, 0, sizeof *sc);
    sc->parent    = s->scope;
    sc->in_method = in_method ? true :
                    (s->scope != NULL && s->scope->in_method);
    s->scope = sc;
}

static void pop_scope(tess_sema *s) {
    s->scope = s->scope->parent;
}

/* -------------------------------------------------------------------------
 * Type checking helpers
 * ------------------------------------------------------------------------- */

static tess_type infer_expr_type(tess_sema *s, tess_expr *e) {
    if (e == NULL) return TYPE_INT; // Default fallback
    
    switch (e->kind) {
        case TESS_EXPR_INT:
            return TYPE_INT;
        case TESS_EXPR_FLOAT:
            return TYPE_FLOAT;
        case TESS_EXPR_STRING:
            return TYPE_STR;
        case TESS_EXPR_BOOL:
            return TYPE_BOOL;
        case TESS_EXPR_NULL:
            return TYPE_NULL;
        case TESS_EXPR_IDENT: {
            /* Look up variable type from scope */
            sym_entry *sym = scope_lookup(s->scope, e->as.ident);
            if (sym != NULL && sym->kind == SYM_VAR) {
                return sym->type;
            }
            return TYPE_INT; // Fallback
        }
        case TESS_EXPR_MEMBER: {
            /* Method calls return void, field access returns field type */
            /* For now, assume methods - detailed type inference requires more AST info */
            return TYPE_NULL;
        }
        case TESS_EXPR_CALL: {
            /* Built-in function calls */
            if (e->as.call.callee->kind == TESS_EXPR_IDENT) {
                sym_entry *sym = scope_lookup(s->scope, e->as.call.callee->as.ident);
                if (sym != NULL && sym->kind == SYM_BUILTIN) {
                    const char *name = e->as.call.callee->as.ident;
                    if (strcmp(name, "str") == 0) return TYPE_STR;
                    if (strcmp(name, "int") == 0) return TYPE_INT;
                    if (strcmp(name, "float") == 0) return TYPE_FLOAT;
                    if (strcmp(name, "len") == 0) return TYPE_INT;
                    if (strcmp(name, "input") == 0) return TYPE_STR;
                }
            }
            return TYPE_NULL;
        }
        case TESS_EXPR_BINARY: {
            /* Determine result type based on operator */
            tess_binary_op op = e->as.binary.op;
            tess_type lhs = infer_expr_type(s, e->as.binary.lhs);
            tess_type rhs = infer_expr_type(s, e->as.binary.rhs);
            
            /* Comparison operators always return bool */
            if (op >= TESS_BIN_EQ && op <= TESS_BIN_GE) {
                return TYPE_BOOL;
            }
            
            /* Logical AND/OR */
            if (op == TESS_BIN_AND || op == TESS_BIN_OR) {
                return TYPE_BOOL;
            }
            
            /* String concatenation (only addition, not subtraction) */
            if ((lhs == TYPE_STR || rhs == TYPE_STR) && op == TESS_BIN_ADD) {
                return TYPE_STR;
            }
            
            /* Numeric operations require numeric types */
            if (lhs == TYPE_INT && rhs == TYPE_INT) {
                if (op == TESS_BIN_DIV || op == TESS_BIN_MOD) {
                    return TYPE_INT;
                }
                return TYPE_INT;
            }
            if (lhs == TYPE_FLOAT || rhs == TYPE_FLOAT) {
                return TYPE_FLOAT;
            }
            
            return TYPE_INT; // Default
        }
        case TESS_EXPR_UNARY: {
            /* Logical NOT always returns bool, regardless of operand type */
            if (e->as.unary.op == TESS_UN_NOT) {
                return TYPE_BOOL;
            }
            /* Negation/bit-not preserve numeric type */
            tess_type operand = infer_expr_type(s, e->as.unary.operand);
            if (operand == TYPE_INT || operand == TYPE_FLOAT) {
                return operand;
            }
            return operand;
        }
        case TESS_EXPR_TERNARY: {
            /* Both branches must have same type */
            tess_type then_type = infer_expr_type(s, e->as.ternary.then_expr);
            tess_type else_type = infer_expr_type(s, e->as.ternary.else_expr);
            if (then_type == else_type) {
                return then_type;
            }
            return TYPE_NULL; // Type mismatch
        }
        default:
            return TYPE_NULL;
    }
}

const char* tess_sema_type_name(tess_expr_kind kind, const char *class_name) {
    (void)class_name;
    switch (kind) {
        case TESS_EXPR_INT: return "int";
        case TESS_EXPR_FLOAT: return "float";
        case TESS_EXPR_STRING: return "str";
        case TESS_EXPR_BOOL: return "bool";
        case TESS_EXPR_NULL: return "null";
        default: return "unknown";
    }
}

/* Check if two types are compatible (for assignments and operations) */
static bool types_compatible(tess_type a, tess_type b) {
    if (a == b) return true;
    
    /* int and float are interchangeable in numeric context */
    if ((a == TYPE_INT && b == TYPE_FLOAT) || (a == TYPE_FLOAT && b == TYPE_INT)) {
        return true;
    }
    
    return false;
}

/* -------------------------------------------------------------------------
 * lvalue check (assignment target must be ident or member access)
 * ------------------------------------------------------------------------- */

static bool is_lvalue(tess_expr *e) {
    return e->kind == TESS_EXPR_IDENT || e->kind == TESS_EXPR_MEMBER;
}

/* -------------------------------------------------------------------------
 * Expression checker with type checking
 * ------------------------------------------------------------------------- */

static bool check_expr(tess_sema *s, tess_expr *e) {
    if (s->failed || e == NULL) return !s->failed;

    switch (e->kind) {
        /* Literals are always valid. */
        case TESS_EXPR_INT:
        case TESS_EXPR_FLOAT:
        case TESS_EXPR_STRING:
        case TESS_EXPR_BOOL:
        case TESS_EXPR_NULL:
            return true;

        case TESS_EXPR_IDENT: {
            sym_entry *sym = scope_lookup(s->scope, e->as.ident);
            if (sym == NULL) {
                char msg[256];
                snprintf(msg, sizeof msg, "undefined name `%s`", e->as.ident);
                report(s, e->start, e->end, msg);
                return false;
            }
            return true;
        }

        case TESS_EXPR_MODULE:
            /* @module — checked at runtime; accept any name at compile time. */
            return true;

        case TESS_EXPR_MEMBER: {
            tess_expr *obj = e->as.member.object;
            const char *member = e->as.member.name;

            if (!check_expr(s, obj)) return false;

            /* If the object is a known class instance, validate the member exists. */
            if (obj->kind == TESS_EXPR_IDENT) {
                sym_entry *sym = scope_lookup(s->scope, obj->as.ident);
                if (sym != NULL && sym->kind == SYM_VAR && sym->type == TYPE_CLASS) {
                    tess_class *cls = sym->as.cls;
                    bool found = false;
                    
                    for (size_t i = 0; i < cls->field_count && !found; i++)
                        if (strcmp(cls->fields[i].name, member) == 0) found = true;
                    for (size_t i = 0; i < cls->method_count && !found; i++)
                        if (strcmp(cls->methods[i].name, member) == 0) found = true;
                    
                    /* Special pseudo-members */
                    if (!found && strcmp(member, "new") == 0) found = true;
                    if (!found && strcmp(member, "init") == 0) found = true;
                    
                    if (!found) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "class `%s` has no member `%s`",
                                 cls->name, member);
                        report(s, e->start, e->end, msg);
                        return false;
                    }
                }
            }
            return true;
        }

        case TESS_EXPR_CALL: {
            tess_expr *callee = e->as.call.callee;
            size_t argc = e->as.call.arg_count;

            /* Check arity for known builtins and validate argument types */
            if (callee->kind == TESS_EXPR_IDENT) {
                sym_entry *sym = scope_lookup(s->scope, callee->as.ident);
                if (sym != NULL && sym->kind == SYM_BUILTIN) {
                    int expected = sym->as.builtin.arity;
                    
                    if (!sym->as.builtin.variadic && argc != (size_t)expected) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "`%s` expects exactly %d argument%s, got %zu",
                                 callee->as.ident, expected,
                                 expected == 1 ? "" : "s", argc);
                        report(s, e->start, e->end, msg);
                        return false;
                    }
                    
                    /* Type checking for built-in arguments */
                    for (size_t i = 0; i < argc; i++) {
                        tess_expr *arg = e->as.call.args[i];
                        tess_type arg_type = infer_expr_type(s, arg);
                        
                        /* Simple type validation - built-ins generally accept various types */
                        (void)arg_type; // Future enhancement
                    }
                } else {
                    /* User-defined method call - check method signature */
                    if (callee->as.ident != NULL) {
                        sym_entry *obj_sym = scope_lookup(s->scope, callee->as.ident);
                        if (obj_sym != NULL && obj_sym->type == TYPE_CLASS) {
                            /* TODO: Method signature checking */
                        }
                    }
                }
            }

            if (!check_expr(s, callee)) return false;
            for (size_t i = 0; i < argc; i++)
                if (!check_expr(s, e->as.call.args[i])) return false;
            return true;
        }

        case TESS_EXPR_ASSIGN: {
            if (!is_lvalue(e->as.assign.target)) {
                report(s, e->as.assign.target->start, e->as.assign.target->end,
                       "expression is not assignable");
                return false;
            }
            
            /* Type checking for assignment */
            tess_type target_type = TYPE_NULL;
            tess_expr *target = e->as.assign.target;
            
            if (target->kind == TESS_EXPR_IDENT) {
                sym_entry *sym = scope_lookup(s->scope, target->as.ident);
                if (sym != NULL && sym->kind == SYM_VAR) {
                    target_type = sym->type;
                }
            }
            
            tess_type value_type = infer_expr_type(s, e->as.assign.value);
            
            /* Allow assign-to-declare (no prior type known) or compatible types */
            if (target_type != TYPE_NULL && !types_compatible(target_type, value_type)) {
                char msg[256];
                const char *t1 = "unknown", *t2 = "unknown";
                
                /* Convert types to names */
                switch (target_type) {
                    case TYPE_INT: t1 = "int"; break;
                    case TYPE_FLOAT: t1 = "float"; break;
                    case TYPE_STR: t1 = "str"; break;
                    case TYPE_BOOL: t1 = "bool"; break;
                    default: break;
                }
                
                switch (value_type) {
                    case TYPE_INT: t2 = "int"; break;
                    case TYPE_FLOAT: t2 = "float"; break;
                    case TYPE_STR: t2 = "str"; break;
                    case TYPE_BOOL: t2 = "bool"; break;
                    default: break;
                }
                
                snprintf(msg, sizeof msg,
                         "cannot assign %s to %s", t2, t1);
                report(s, e->start, e->end, msg);
                return false;
            }
            
            return check_expr(s, e->as.assign.target) &&
                   check_expr(s, e->as.assign.value);
        }

        case TESS_EXPR_BINARY: {
            if (!check_expr(s, e->as.binary.lhs)) return false;
            if (!check_expr(s, e->as.binary.rhs)) return false;
            
            /* Type checking for binary operations */
            tess_type lhs_type = infer_expr_type(s, e->as.binary.lhs);
            tess_type rhs_type = infer_expr_type(s, e->as.binary.rhs);
            tess_binary_op op = e->as.binary.op;
            
            /* Comparison operators: operands must be comparable types */
            if (op >= TESS_BIN_EQ && op <= TESS_BIN_GE) {
                /* Can compare same types, or int/float */
                if (!types_compatible(lhs_type, rhs_type) && 
                    lhs_type != TYPE_NULL && rhs_type != TYPE_NULL) {
                    char msg[256];
                    const char *lt = "unknown", *rt = "unknown";
                    switch (lhs_type) { case TYPE_INT: lt = "int"; break; case TYPE_FLOAT: lt = "float"; break; case TYPE_STR: lt = "str"; break; case TYPE_BOOL: lt = "bool"; break; default: break; }
                    switch (rhs_type) { case TYPE_INT: rt = "int"; break; case TYPE_FLOAT: rt = "float"; break; case TYPE_STR: rt = "str"; break; case TYPE_BOOL: rt = "bool"; break; default: break; }
                    snprintf(msg, sizeof msg, "incompatible types in comparison: %s vs %s", lt, rt);
                    report(s, e->start, e->end, msg);
                    return false;
                }
            }
            
            /* Arithmetic operators: numeric types only */
            if (op >= TESS_BIN_ADD && op <= TESS_BIN_MOD) {
                bool lhs_numeric = (lhs_type == TYPE_INT || lhs_type == TYPE_FLOAT);
                bool rhs_numeric = (rhs_type == TYPE_INT || rhs_type == TYPE_FLOAT);
                
                /* String addition allowed */
                bool string_add = (lhs_type == TYPE_STR && rhs_type == TYPE_STR);
                
                if (!((lhs_numeric && rhs_numeric) || string_add)) {
                    char msg[256];
                    const char *lt = "unknown", *rt = "unknown";
                    switch (lhs_type) { case TYPE_INT: lt = "int"; break; case TYPE_FLOAT: lt = "float"; break; case TYPE_STR: lt = "str"; break; case TYPE_BOOL: lt = "bool"; break; default: break; }
                    switch (rhs_type) { case TYPE_INT: rt = "int"; break; case TYPE_FLOAT: rt = "float"; break; case TYPE_STR: rt = "str"; break; case TYPE_BOOL: rt = "bool"; break; default: break; }
                    snprintf(msg, sizeof msg, "binary operator requires numeric types, got %s and %s", lt, rt);
                    report(s, e->start, e->end, msg);
                    return false;
                }
            }
            
            /* Logical operators: both operands should be boolean;
             * emit a warning-level note but do not hard-fail, since many
             * languages allow truthy/falsy values in logical expressions. */
            if (op == TESS_BIN_AND || op == TESS_BIN_OR) {
                if (lhs_type != TYPE_BOOL && lhs_type != TYPE_INT &&
                    lhs_type != TYPE_FLOAT && lhs_type != TYPE_NULL) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "logical operator operand has unexpected type");
                    report(s, e->start, e->end, msg);
                    return false;
                }
            }
            
            return true;
        }

        case TESS_EXPR_UNARY: {
            if (!check_expr(s, e->as.unary.operand)) return false;
            
            /* Negation: numeric only */
            if (e->as.unary.op == TESS_UN_NEG) {
                tess_type operand_type = infer_expr_type(s, e->as.unary.operand);
                if (operand_type != TYPE_INT && operand_type != TYPE_FLOAT) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "unary negation requires numeric type, got %s",
                             operand_type == TYPE_INT ? "int" : 
                             operand_type == TYPE_FLOAT ? "float" : "non-numeric");
                    report(s, e->start, e->end, msg);
                    return false;
                }
            }
            
            /* NOT: boolean or numeric (integers/floats are truthy/falsy) */
            if (e->as.unary.op == TESS_UN_NOT) {
                tess_type operand_type = infer_expr_type(s, e->as.unary.operand);
                if (operand_type == TYPE_STR) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "logical NOT cannot be applied to str");
                    report(s, e->start, e->end, msg);
                    return false;
                }
            }
            
            return true;
        }

        case TESS_EXPR_TERNARY: {
            if (!check_expr(s, e->as.ternary.cond)) return false;
            if (!check_expr(s, e->as.ternary.then_expr)) return false;
            if (!check_expr(s, e->as.ternary.else_expr)) return false;
            
            /* Condition must be boolean */
            tess_type cond_type = infer_expr_type(s, e->as.ternary.cond);
            if (cond_type != TYPE_BOOL) {
                char msg[256];
                snprintf(msg, sizeof msg, "ternary condition must be bool, got %s",
                         cond_type == TYPE_BOOL ? "bool" : "non-bool");
                report(s, e->start, e->end, msg);
                return false;
            }
            
            /* Both branches must have compatible types */
            tess_type then_type = infer_expr_type(s, e->as.ternary.then_expr);
            tess_type else_type = infer_expr_type(s, e->as.ternary.else_expr);
            
            if (!types_compatible(then_type, else_type)) {
                char msg[256];
                const char *tt = "unknown", *et = "unknown";
                switch (then_type) { case TYPE_INT: tt = "int"; break; case TYPE_FLOAT: tt = "float"; break; case TYPE_STR: tt = "str"; break; default: break; }
                switch (else_type) { case TYPE_INT: et = "int"; break; case TYPE_FLOAT: et = "float"; break; case TYPE_STR: et = "str"; break; default: break; }
                snprintf(msg, sizeof msg, 
                         "ternary expression types must match: %s vs %s", tt, et);
                report(s, e->start, e->end, msg);
                return false;
            }
            
            return true;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Statement checker with enhanced type checking
 * ------------------------------------------------------------------------- */

static bool check_stmt(tess_sema *s, tess_stmt *st) {
    if (s->failed || st == NULL) return !s->failed;

    switch (st->kind) {
        case TESS_STMT_EMPTY:
            return true;

        case TESS_STMT_EXPR:
            return check_expr(s, st->as.expr);

        case TESS_STMT_ASSIGN: {
            tess_expr *target = st->as.assign.target;
            tess_expr *value  = st->as.assign.value;

            if (!is_lvalue(target)) {
                report(s, target->start, target->end,
                       "expression is not assignable");
                return false;
            }
            
            /* Check value expression */
            if (!check_expr(s, value)) return false;

            /* Declare the variable in the current scope if it's a bare ident
             * that hasn't been seen yet (Tesseract uses assign-to-declare). */
            if (target->kind == TESS_EXPR_IDENT) {
                if (scope_lookup_own(s->scope, target->as.ident) == NULL) {
                    tess_type var_type = infer_expr_type(s, value);
                    
                    /* If we're assigning a class instance, track it as class type */
                    if (var_type == TYPE_NULL) {
                        sym_entry *callee_sym = NULL;
                        if (value->kind == TESS_EXPR_CALL &&
                            value->as.call.callee->kind == TESS_EXPR_MEMBER &&
                            value->as.call.callee->as.member.object != NULL &&
                            value->as.call.callee->as.member.object->kind == TESS_EXPR_IDENT) {
                            /* Class.new pattern */
                            callee_sym = scope_lookup(s->scope,
                                value->as.call.callee->as.member.object->as.ident);
                        }

                        if (callee_sym != NULL && callee_sym->kind == SYM_CLASS) {
                            var_type = TYPE_CLASS;
                        } else {
                            var_type = TYPE_INT; // Default fallback
                        }
                    }
                    
                    scope_define(s->scope, target->as.ident,
                                 SYM_VAR, var_type, NULL, -1, false);
                }
            } else {
                /* member assignment — object must resolve */
                if (!check_expr(s, target)) return false;
            }
            return true;
        }

        case TESS_STMT_IF: {
            if (!check_expr(s, st->as.if_stmt.cond)) return false;
            
            /* Condition must be boolean */
            tess_type cond_type = infer_expr_type(s, st->as.if_stmt.cond);
            if (cond_type != TYPE_BOOL) {
                char msg[256];
                snprintf(msg, sizeof msg, 
                         "if condition must be bool, got %s",
                         cond_type == TYPE_BOOL ? "bool" : "non-bool");
                report(s, st->as.if_stmt.cond->start, st->as.if_stmt.cond->end, msg);
                return false;
            }

            tess_scope then_sc;
            push_scope(s, &then_sc, false);
            bool ok = check_stmt(s, st->as.if_stmt.then_branch);
            pop_scope(s);
            if (!ok) return false;

            if (st->as.if_stmt.else_branch != NULL) {
                tess_scope else_sc;
                push_scope(s, &else_sc, false);
                ok = check_stmt(s, st->as.if_stmt.else_branch);
                pop_scope(s);
                if (!ok) return false;
            }
            return true;
        }

        case TESS_STMT_WHILE: {
            if (!check_expr(s, st->as.while_stmt.cond)) return false;
            
            /* Condition must be boolean */
            tess_type cond_type = infer_expr_type(s, st->as.while_stmt.cond);
            if (cond_type != TYPE_BOOL) {
                char msg[256];
                snprintf(msg, sizeof msg,
                         "while condition must be bool, got %s",
                         cond_type == TYPE_BOOL ? "bool" : "non-bool");
                report(s, st->as.while_stmt.cond->start, st->as.while_stmt.cond->end, msg);
                return false;
            }
            
            tess_scope body_sc;
            push_scope(s, &body_sc, false);
            bool ok = check_stmt(s, st->as.while_stmt.body);
            pop_scope(s);
            return ok;
        }

        case TESS_STMT_RETURN: {
            if (!s->scope->in_method) {
                report(s, st->start, st->end,
                       "`return` outside a method body");
                return false;
            }
            if (st->as.ret != NULL)
                return check_expr(s, st->as.ret);
            return true;
        }

        case TESS_STMT_BLOCK: {
            tess_scope block_sc;
            push_scope(s, &block_sc, false);
            for (size_t i = 0; i < st->as.block.count; i++) {
                if (!check_stmt(s, st->as.block.items[i])) {
                    pop_scope(s);
                    return false;
                }
            }
            pop_scope(s);
            return true;
        }

        case TESS_STMT_CLASS: {
            tess_class *cls = st->as.class_decl;

            /* Class name already pre-declared in pass 1; check for duplicate
             * field / method names inside the class. */
            tess_scope cls_ns;
            memset(&cls_ns, 0, sizeof cls_ns);

            for (size_t i = 0; i < cls->field_count; i++) {
                if (!scope_define(&cls_ns, cls->fields[i].name,
                                  SYM_VAR, TYPE_INT, NULL, -1, false)) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "duplicate field `%s` in class `%s`",
                             cls->fields[i].name, cls->name);
                    report(s, cls->fields[i].start, cls->fields[i].end, msg);
                    return false;
                }
            }
            for (size_t i = 0; i < cls->method_count; i++) {
                if (!scope_define(&cls_ns, cls->methods[i].name,
                                  SYM_VAR, TYPE_NULL, NULL, -1, false)) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "duplicate method `%s` in class `%s`",
                             cls->methods[i].name, cls->name);
                    report(s, cls->methods[i].start, cls->methods[i].end, msg);
                    return false;
                }
            }

            /* Check each method body in its own scope. */
            tess_class *saved_class = s->current_class;
            s->current_class = cls;

            for (size_t i = 0; i < cls->method_count; i++) {
                tess_method *m = &cls->methods[i];
                tess_scope msc;
                push_scope(s, &msc, true);

                /* Declare 'self' as implicit first parameter */
                scope_define(s->scope, "self", SYM_VAR, TYPE_CLASS, cls, -1, false);
                
                /* Declare parameters */
                for (size_t j = 0; j < m->param_count; j++) {
                    tess_type param_type = TYPE_INT; // Default
                    if (m->params[j].type != NULL) {
                        if (strcmp(m->params[j].type, "int") == 0) param_type = TYPE_INT;
                        else if (strcmp(m->params[j].type, "float") == 0) param_type = TYPE_FLOAT;
                        else if (strcmp(m->params[j].type, "str") == 0) param_type = TYPE_STR;
                        else if (strcmp(m->params[j].type, "bool") == 0) param_type = TYPE_BOOL;
                    }
                    scope_define(s->scope, m->params[j].name,
                                 SYM_VAR, param_type, NULL, -1, false);
                }
                
                /* Fields visible as bare names inside methods */
                for (size_t j = 0; j < cls->field_count && !s->failed; j++) {
                    if (scope_lookup_own(s->scope, cls->fields[j].name) == NULL)
                        scope_define(s->scope, cls->fields[j].name,
                                     SYM_VAR, TYPE_INT, NULL, -1, false);
                }
                
                if (s->failed) break;

                bool ok = check_stmt(s, m->body);
                pop_scope(s);
                if (!ok) {
                    s->current_class = saved_class;
                    return false;
                }
            }
            s->current_class = saved_class;
            return true;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */

bool tess_sema_check(tess_program *program, const char *src, const char *path) {
    if (program == NULL) return false;

    tess_sema s = {0};
    s.src  = src  ? src  : "";
    s.path = path ? path : "<input>";

    /* Global scope */
    tess_scope global;
    push_scope(&s, &global, false);

    /* Register built-in functions */
    scope_define(s.scope, "put",    SYM_BUILTIN, TYPE_NULL, NULL, 1, false);
    scope_define(s.scope, "assert", SYM_BUILTIN, TYPE_NULL, NULL, 1, false);
    scope_define(s.scope, "len",    SYM_BUILTIN, TYPE_INT, NULL, 1, false);
    scope_define(s.scope, "str",    SYM_BUILTIN, TYPE_STR, NULL, 1, false);
    scope_define(s.scope, "int",    SYM_BUILTIN, TYPE_INT, NULL, 1, false);
    scope_define(s.scope, "float",  SYM_BUILTIN, TYPE_FLOAT, NULL, 1, false);
    scope_define(s.scope, "print",  SYM_BUILTIN, TYPE_NULL, NULL, -1, true);  /* variadic */
    scope_define(s.scope, "input",  SYM_BUILTIN, TYPE_STR, NULL, 0, false);

    /* Pass 1: pre-declare all top-level class names so they can be referenced
     * before their definition (forward declarations). */
    for (size_t i = 0; i < program->count; i++) {
        tess_stmt *st = program->items[i];
        if (st->kind == TESS_STMT_CLASS) {
            tess_class *cls = st->as.class_decl;
            if (!scope_define(s.scope, cls->name, SYM_CLASS, TYPE_CLASS, cls, -1, false)) {
                char msg[256];
                snprintf(msg, sizeof msg, "duplicate class name `%s`",
                         cls->name);
                report(&s, cls->start, cls->end, msg);
                pop_scope(&s);
                return false;
            }
        }
    }

    /* Pass 2: check all statements */
    for (size_t i = 0; i < program->count && !s.failed; i++) {
        check_stmt(&s, program->items[i]);
    }

    pop_scope(&s);
    return !s.failed;
}
