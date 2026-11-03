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

#define SEMA_HTABLE_INITIAL 16u /* must be a power of two */
#define SEMA_HTABLE_MAX_LOAD 2  /* grow when count * 2 >= cap */

typedef struct tess_scope {
    struct tess_scope *parent;
    bool in_method;           /* true -> return is allowed           */
    bool in_function;         /* inherited by nested blocks for return checks */
    sym_entry *table;
    size_t cap;               /* power of two, 0 when init failed    */
    size_t count;
} tess_scope;

/* Imported module alias: local name -> module key. Entries pre-registered for
 * the built-in native modules are marked, so a later `import` may rebind them
 * without counting as a duplicate declaration. */
typedef struct {
    char *alias;
    char *module;
    bool builtin;
} tess_module_ref;

typedef struct {
    tess_module_ref *items;
    size_t count;
    size_t cap;
} tess_module_table;

/* Top-level function: module key (NULL for the root module), name, ret type. */
typedef struct {
    char *module;
    char *name;
    char *ret_type;
    char **param_types;   /* one Tesseract type name per parameter */
    size_t param_count;
} tess_fn_entry;

typedef struct {
    tess_fn_entry *items;
    size_t count;
    size_t cap;
} tess_fn_table;

/* FNV-1a 32-bit (fast, good distribution for short strings) */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

static bool scope_init(tess_scope *sc) {
    sc->table = core_alloc(SEMA_HTABLE_INITIAL * sizeof *sc->table);
    if (sc->table == NULL) {
        sc->cap = 0;
        sc->count = 0;
        return false;
    }
    memset(sc->table, 0, SEMA_HTABLE_INITIAL * sizeof *sc->table);
    sc->cap = SEMA_HTABLE_INITIAL;
    sc->count = 0;
    return true;
}

static void scope_destroy(tess_scope *sc) {
    core_free(sc->table);
    sc->table = NULL;
    sc->cap = 0;
    sc->count = 0;
}

static sym_entry *scope_lookup_own(tess_scope *sc, const char *name) {
    if (sc->table == NULL || sc->cap == 0) return NULL;
    uint32_t mask = (uint32_t)sc->cap - 1u;
    uint32_t idx = fnv1a(name) & mask;
    for (uint32_t i = 0; i < sc->cap; i++) {
        sym_entry *e = &sc->table[(idx + i) & mask];
        if (e->name == NULL) return NULL;
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

static bool scope_grow(tess_scope *sc) {
    size_t new_cap = sc->cap * 2;
    if (new_cap < sc->cap) return false;
    sym_entry *new_table = core_alloc(new_cap * sizeof *new_table);
    if (new_table == NULL) return false;
    memset(new_table, 0, new_cap * sizeof *new_table);

    uint32_t mask = (uint32_t)new_cap - 1u;
    for (size_t i = 0; i < sc->cap; i++) {
        sym_entry *old = &sc->table[i];
        if (old->name == NULL) continue;
        uint32_t idx = fnv1a(old->name) & mask;
        size_t j = 0;
        for (; j < new_cap; j++) {
            sym_entry *slot = &new_table[(idx + j) & mask];
            if (slot->name == NULL) {
                *slot = *old;
                break;
            }
        }
        if (j == new_cap) {
            core_free(new_table);
            return false;
        }
    }

    core_free(sc->table);
    sc->table = new_table;
    sc->cap = new_cap;
    return true;
}

static sym_entry *scope_lookup(tess_scope *sc, const char *name) {
    for (tess_scope *cur = sc; cur != NULL; cur = cur->parent) {
        sym_entry *e = scope_lookup_own(cur, name);
        if (e != NULL) return e;
    }
    return NULL;
}

/* Returns false on duplicate name or allocation failure. */
static bool scope_define(tess_scope *sc, const char *name, sym_kind kind,
                         tess_type type, tess_class *cls, int arity, bool variadic) {
    if (sc->table == NULL) return false;

    /* Keep the load factor low so probing stays cheap and terminating. */
    if ((sc->count + 1) * SEMA_HTABLE_MAX_LOAD >= sc->cap) {
        if (!scope_grow(sc)) return false;
    }

    uint32_t mask = (uint32_t)sc->cap - 1u;
    uint32_t idx = fnv1a(name) & mask;
    for (size_t i = 0; i < sc->cap; i++) {
        sym_entry *e = &sc->table[(idx + i) & mask];
        if (e->name == NULL) {
            e->name  = name;
            e->kind  = kind;
            e->type  = type;
            if (kind == SYM_CLASS || (kind == SYM_VAR && type == TYPE_CLASS)) {
                e->as.cls = cls;
            } else if (kind == SYM_BUILTIN) {
                e->as.builtin.arity = arity;
                e->as.builtin.variadic = variadic;
            }
            sc->count++;
            return true;
        }
        if (strcmp(e->name, name) == 0) {
            return false;
        }
    }
    return false;
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
    /* Enclosing fn/method, for return-type validation. */
    tess_stmt   *current_fn;
    tess_method *current_method;
    /* Imported module aliases: local alias -> module key. */
    tess_module_table modules;
    tess_fn_table fns;
} tess_sema;

static tess_type type_from_name(tess_sema *s, const char *name,
                                tess_class **out_cls);
static tess_field *class_find_field(tess_class *cls, const char *name);
static tess_method *class_find_method(tess_class *cls, const char *name);
static tess_class *receiver_class(tess_sema *s, tess_expr *obj);
static bool check_fn_call(tess_sema *s, tess_expr *callee, size_t argc,
                          tess_expr **args);

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

/* Scopes themselves live on the C stack; their symbol tables are heap-backed
 * and grow on demand, so a scope is no longer limited to a fixed capacity. */
static bool push_scope(tess_sema *s, tess_scope *sc, bool in_method) {
    memset(sc, 0, sizeof *sc);
    sc->parent    = s->scope;
    sc->in_method = in_method ? true :
                    (s->scope != NULL && s->scope->in_method);
    sc->in_function = in_method ? true :
                      (s->scope != NULL && s->scope->in_function);
    if (!scope_init(sc)) {
        return false;
    }
    s->scope = sc;
    return true;
}

static void pop_scope(tess_sema *s) {
    tess_scope *sc = s->scope;
    s->scope = sc->parent;
    scope_destroy(sc);
}

/* Module alias table: local alias -> module key. */
static bool module_table_add(tess_module_table *t, const char *alias,
                             const char *module, bool builtin) {
    if (alias == NULL || module == NULL) return false;
    for (size_t i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].alias, alias) == 0) {
            /* An explicit import may rebind a built-in registration. */
            if (t->items[i].builtin && !builtin) {
                char *m = core_alloc(strlen(module) + 1);
                if (m == NULL) return false;
                strcpy(m, module);
                core_free(t->items[i].module);
                t->items[i].module = m;
                t->items[i].builtin = false;
            }
            return true;
        }
    }
    if (t->count == t->cap) {
        size_t cap = t->cap == 0 ? 8 : t->cap * 2;
        tess_module_ref *arr = core_alloc(cap * sizeof *arr);
        if (arr == NULL) return false;
        if (t->items != NULL && t->count != 0) {
            memcpy(arr, t->items, t->count * sizeof *arr);
        }
        core_free(t->items);
        t->items = arr;
        t->cap = cap;
    }
    char *a = core_alloc(strlen(alias) + 1);
    char *m = core_alloc(strlen(module) + 1);
    if (a == NULL || m == NULL) {
        core_free(a);
        core_free(m);
        return false;
    }
    strcpy(a, alias);
    strcpy(m, module);
    t->items[t->count].alias = a;
    t->items[t->count].module = m;
    t->items[t->count].builtin = builtin;
    t->count++;
    return true;
}

static const char *module_table_find(tess_module_table *t, const char *alias) {
    if (alias == NULL) return NULL;
    for (size_t i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].alias, alias) == 0) {
            return t->items[i].module;
        }
    }
    return NULL;
}

/* Function table: module-qualified or top-level functions. */
static bool fn_table_add(tess_fn_table *t, const char *module, const char *name,
                        const char *ret_type, tess_param *params,
                        size_t param_count) {
    if (name == NULL) return false;
    /* Names only collide within the same module namespace. */
    for (size_t i = 0; i < t->count; i++) {
        bool same_module = (module == NULL && t->items[i].module == NULL) ||
                           (module != NULL && t->items[i].module != NULL &&
                            strcmp(module, t->items[i].module) == 0);
        if (same_module && strcmp(t->items[i].name, name) == 0) return false;
    }
    if (t->count == t->cap) {
        size_t cap = t->cap == 0 ? 8 : t->cap * 2;
        tess_fn_entry *arr = core_alloc(cap * sizeof *arr);
        if (arr == NULL) return false;
        if (t->items != NULL && t->count != 0) {
            memcpy(arr, t->items, t->count * sizeof *arr);
        }
        core_free(t->items);
        t->items = arr;
        t->cap = cap;
    }
    char **pt = NULL;
    if (param_count != 0) {
        pt = core_alloc(param_count * sizeof *pt);
        if (pt == NULL) return false;
        memset(pt, 0, param_count * sizeof *pt);
        for (size_t i = 0; i < param_count; i++) {
            const char *ptype = params != NULL ? params[i].type : NULL;
            if (ptype != NULL) {
                pt[i] = core_alloc(strlen(ptype) + 1);
                if (pt[i] == NULL) {
                    for (size_t j = 0; j < i; j++) core_free(pt[j]);
                    core_free(pt);
                    return false;
                }
                strcpy(pt[i], ptype);
            }
        }
    }
    char *n = core_alloc(strlen(name) + 1);
    char *r = ret_type == NULL ? NULL : core_alloc(strlen(ret_type) + 1);
    char *m = module == NULL ? NULL : core_alloc(strlen(module) + 1);
    if (n == NULL || (ret_type != NULL && r == NULL) ||
        (module != NULL && m == NULL)) {
        if (pt != NULL) {
            for (size_t i = 0; i < param_count; i++) core_free(pt[i]);
            core_free(pt);
        }
        core_free(n); core_free(r); core_free(m); return false;
    }
    strcpy(n, name);
    if (r != NULL) strcpy(r, ret_type);
    if (m != NULL) strcpy(m, module);
    t->items[t->count].module = m;
    t->items[t->count].name = n;
    t->items[t->count].ret_type = r;
    t->items[t->count].param_types = pt;
    t->items[t->count].param_count = param_count;
    t->count++;
    return true;
}

static tess_fn_entry *fn_table_find(tess_fn_table *t, const char *module,
                                  const char *name) {
    for (size_t i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].name, name) != 0) continue;
        if (module == NULL) {
            if (t->items[i].module == NULL) return &t->items[i];
        } else if (t->items[i].module != NULL &&
                   strcmp(t->items[i].module, module) == 0) {
            return &t->items[i];
        }
    }
    return NULL;
}

static void module_table_destroy(tess_module_table *t) {
    for (size_t i = 0; i < t->count; i++) {
        core_free(t->items[i].alias);
        core_free(t->items[i].module);
    }
    core_free(t->items);
    memset(t, 0, sizeof *t);
}

static void fn_table_destroy(tess_fn_table *t) {
    for (size_t i = 0; i < t->count; i++) {
        if (t->items[i].param_types != NULL) {
            for (size_t j = 0; j < t->items[i].param_count; j++) {
                core_free(t->items[i].param_types[j]);
            }
            core_free(t->items[i].param_types);
        }
        core_free(t->items[i].module);
        core_free(t->items[i].name);
        core_free(t->items[i].ret_type);
    }
    core_free(t->items);
    memset(t, 0, sizeof *t);
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
            /* Infer field type when the receiver is a known class instance. */
            tess_class *cls = receiver_class(s, e->as.member.object);
            if (cls != NULL) {
                tess_field *field = class_find_field(cls, e->as.member.name);
                if (field != NULL) {
                    return type_from_name(s, field->type, NULL);
                }
                /* Methods carry a declared return type; NULL means void. */
                tess_method *m = class_find_method(cls, e->as.member.name);
                if (m != NULL) {
                    if (m->ret_type == NULL) return TYPE_NULL;
                    return type_from_name(s, m->ret_type, NULL);
                }
            }
            return TYPE_NULL;
        }
        case TESS_EXPR_CALL: {
            /* Built-in function calls */
            if (e->as.call.callee->kind == TESS_EXPR_IDENT) {
                tess_fn_entry *fn = fn_table_find(
                    &s->fns, NULL, e->as.call.callee->as.ident);
                if (fn != NULL)
                    return fn->ret_type == NULL ? TYPE_NULL :
                           type_from_name(s, fn->ret_type, NULL);
                sym_entry *sym = scope_lookup(s->scope, e->as.call.callee->as.ident);
                if (sym != NULL && sym->kind == SYM_BUILTIN) {
                    const char *name = e->as.call.callee->as.ident;
                    if (strcmp(name, "str") == 0) return TYPE_STR;
                    if (strcmp(name, "int") == 0) return TYPE_INT;
                    if (strcmp(name, "float") == 0) return TYPE_FLOAT;
                    if (strcmp(name, "len") == 0) return TYPE_INT;
                    if (strcmp(name, "input") == 0) return TYPE_STR;
                }
            } else if (e->as.call.callee->kind == TESS_EXPR_MEMBER &&
                       e->as.call.callee->as.member.object != NULL) {
                tess_class *cls = receiver_class(
                    s, e->as.call.callee->as.member.object);
                const char *mname = e->as.call.callee->as.member.name;
                if (cls != NULL && mname != NULL) {
                    tess_method *m = class_find_method(cls, mname);
                    if (m != NULL && m->ret_type != NULL) {
                        return type_from_name(s, m->ret_type, NULL);
                    }
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

/* Map a Tesseract type name to the internal type, resolving class names. */
static tess_type type_from_name(tess_sema *s, const char *name,
                                tess_class **out_cls) {
    if (out_cls != NULL) *out_cls = NULL;
    if (name == NULL) return TYPE_INT; /* legacy default for untyped params */

    if (strcmp(name, "int") == 0 || strcmp(name, "i32") == 0) return TYPE_INT;
    if (strcmp(name, "float") == 0 || strcmp(name, "f64") == 0) return TYPE_FLOAT;
    if (strcmp(name, "str") == 0 || strcmp(name, "string") == 0) return TYPE_STR;
    if (strcmp(name, "bool") == 0 || strcmp(name, "boolean") == 0) return TYPE_BOOL;
    if (strcmp(name, "null") == 0 || strcmp(name, "None") == 0) return TYPE_NULL;

    sym_entry *sym = scope_lookup(s->scope, name);
    if (sym != NULL && sym->kind == SYM_CLASS) {
        if (out_cls != NULL) *out_cls = sym->as.cls;
        return TYPE_CLASS;
    }
    return TYPE_NULL;
}

static bool type_name_known(tess_sema *s, const char *name) {
    if (name == NULL) return false;
    if (strcmp(name, "int") == 0 || strcmp(name, "i32") == 0 ||
        strcmp(name, "float") == 0 || strcmp(name, "f64") == 0 ||
        strcmp(name, "str") == 0 || strcmp(name, "string") == 0 ||
        strcmp(name, "bool") == 0 || strcmp(name, "boolean") == 0 ||
        strcmp(name, "null") == 0 || strcmp(name, "None") == 0) {
        return true;
    }
    sym_entry *sym = scope_lookup(s->scope, name);
    return sym != NULL && sym->kind == SYM_CLASS;
}

static tess_field *class_find_field(tess_class *cls, const char *name) {
    if (cls == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < cls->field_count; i++) {
        if (strcmp(cls->fields[i].name, name) == 0) return &cls->fields[i];
    }
    return NULL;
}

static tess_method *class_find_method(tess_class *cls, const char *name) {
    if (cls == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < cls->method_count; i++) {
        if (strcmp(cls->methods[i].name, name) == 0) return &cls->methods[i];
    }
    return NULL;
}

/* Resolve the class of a `obj.member` receiver when `obj` is a simple name. */
static tess_class *receiver_class(tess_sema *s, tess_expr *obj) {
    if (obj == NULL || obj->kind != TESS_EXPR_IDENT) return NULL;
    sym_entry *sym = scope_lookup(s->scope, obj->as.ident);
    if (sym == NULL) return NULL;
    if (sym->kind == SYM_CLASS) return sym->as.cls;
    if (sym->kind == SYM_VAR && sym->type == TYPE_CLASS) return sym->as.cls;
    return NULL;
}

/* If `value` constructs an instance (Class.new / Class.init(...)), return the
 * class, so assign-to-declare can remember the variable's type. */
static tess_class *class_from_construction(tess_sema *s, tess_expr *value) {
    if (value == NULL) return NULL;
    tess_expr *member = NULL;
    if (value->kind == TESS_EXPR_CALL &&
        value->as.call.callee != NULL &&
        value->as.call.callee->kind == TESS_EXPR_MEMBER) {
        member = value->as.call.callee;
    } else if (value->kind == TESS_EXPR_MEMBER) {
        member = value;
    }
    if (member == NULL) return NULL;
    if (member->as.member.name == NULL) return NULL;
    if (strcmp(member->as.member.name, "new") != 0 &&
        strcmp(member->as.member.name, "init") != 0) {
        return NULL;
    }
    tess_expr *obj = member->as.member.object;
    if (obj == NULL || obj->kind != TESS_EXPR_IDENT) return NULL;
    sym_entry *sym = scope_lookup(s->scope, obj->as.ident);
    if (sym != NULL && sym->kind == SYM_CLASS) return sym->as.cls;
    return NULL;
}

static bool can_assign(tess_type target, tess_type value) {
    if (target == value) return true;
    if ((target == TYPE_INT && value == TYPE_FLOAT) ||
        (target == TYPE_FLOAT && value == TYPE_INT)) {
        return true;
    }
    /* null is assignable to reference-like types */
    if (value == TYPE_NULL &&
        (target == TYPE_CLASS || target == TYPE_STR)) {
        return true;
    }
    return false;
}

static const char *type_label(tess_type t) {
    switch (t) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_STR: return "str";
        case TYPE_BOOL: return "bool";
        case TYPE_NULL: return "null";
        case TYPE_CLASS: return "class";
    }
    return "unknown";
}

/* Validate a method call's arity and argument types. */
static bool check_method_args(tess_sema *s, tess_expr *call,
                              tess_class *cls, tess_method *method,
                              const char *method_name, size_t argc) {
    size_t expected = method->param_count;
    if (argc != expected) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "method `%s` expects %zu argument%s, got %zu",
                 method_name, expected, expected == 1 ? "" : "s", argc);
        report(s, call->start, call->end, msg);
        return false;
    }
    for (size_t i = 0; i < argc; i++) {
        tess_type want = type_from_name(s, method->params[i].type, NULL);
        tess_type got = infer_expr_type(s, call->as.call.args[i]);
        if (want != TYPE_NULL && !can_assign(want, got)) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "argument %zu of `%s.%s` has type %s, expected %s",
                     i + 1, cls->name, method_name,
                     type_label(got), type_label(want));
            report(s, call->as.call.args[i]->start,
                   call->as.call.args[i]->end, msg);
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * lvalue check (assignment target must be ident or member access)
 * ------------------------------------------------------------------------- */

static bool is_lvalue(tess_expr *e) {
    return e != NULL && (e->kind == TESS_EXPR_IDENT ||
                         e->kind == TESS_EXPR_MEMBER);
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
            if (sym == NULL && fn_table_find(&s->fns, NULL,
                                              e->as.ident) != NULL)
                return true;
            if (sym == NULL) {
                char msg[256];
                snprintf(msg, sizeof msg, "undefined name `%s`", e->as.ident);
                report(s, e->start, e->end, msg);
                return false;
            }
            return true;
        }

        case TESS_EXPR_MODULE: {
            /* @module — validate that the module name is known */
            if (e->as.module == NULL) return true;
            if (module_table_find(&s->modules, e->as.module) == NULL) {
                char msg[256];
                snprintf(msg, sizeof msg, "unknown module `%s`", e->as.module);
                report(s, e->start, e->end, msg);
                return false;
            }
            
            return true;
        }

        case TESS_EXPR_MEMBER: {
            tess_expr *obj = e->as.member.object;
            if (obj != NULL && obj->kind == TESS_EXPR_MODULE) {
                const char *module = obj->as.module;
                if (module_table_find(&s->modules, module) == NULL) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "unknown module `%s`",
                             module ? module : "?");
                    report(s, e->start, e->end, msg);
                    return false;
                }
                return true;
            }
            const char *member = e->as.member.name;
            if (member == NULL) {
                report(s, e->start, e->end, "invalid member access");
                return false;
            }
            if (obj == NULL) {
                report(s, e->start, e->end, "invalid member access");
                return false;
            }
            if (!check_expr(s, obj)) return false;

            /* If the object is a known class, validate the member exists. */
            if (obj->kind == TESS_EXPR_IDENT) {
                tess_class *cls = receiver_class(s, obj);
                if (cls != NULL) {
                    tess_field *field = class_find_field(cls, member);
                    tess_method *method = class_find_method(cls, member);
                    bool pseudo = strcmp(member, "new") == 0 ||
                                  strcmp(member, "init") == 0;
                    sym_entry *objsym = scope_lookup(s->scope, obj->as.ident);
                    if (objsym != NULL && objsym->kind == SYM_CLASS && !pseudo) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "cannot access `%s` through the class name; create an instance first",
                                 member);
                        report(s, e->start, e->end, msg);
                        return false;
                    }
                    if (field == NULL && method == NULL && !pseudo) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "class `%s` has no member `%s`",
                                 cls->name, member);
                        report(s, e->start, e->end, msg);
                        return false;
                    }
                    if (method != NULL && field == NULL &&
                        method->param_count > 0) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "method `%s.%s` requires %zu argument%s",
                                 cls->name, member, method->param_count,
                                 method->param_count == 1 ? "" : "s");
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

            if (callee == NULL) {
                report(s, e->start, e->end, "invalid function call");
                return false;
            }
            if (argc > 0 && e->as.call.args == NULL) {
                report(s, e->start, e->end, "invalid argument list");
                return false;
            }
            if (!check_fn_call(s, callee, argc, e->as.call.args))
                return false;

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
                    bool output_builtin =
                        strcmp(callee->as.ident, "put") == 0 ||
                        strcmp(callee->as.ident, "print") == 0 ||
                        strcmp(callee->as.ident, "putln") == 0;
                    for (size_t i = 0; i < argc; i++) {
                        tess_expr *arg = e->as.call.args[i];
                        if (arg == NULL) continue;
                        if (output_builtin &&
                            receiver_class(s, arg) != NULL) {
                            char msg[256];
                            snprintf(msg, sizeof msg,
                                     "cannot print a class instance (`%s`)",
                                     callee->as.ident);
                            report(s, arg->start, arg->end, msg);
                            return false;
                        }
                    }
                }
                if (sym == NULL && fn_table_find(&s->fns, NULL,
                                                  callee->as.ident) == NULL) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "undefined function `%s`",
                             callee->as.ident);
                    report(s, callee->start, callee->end, msg);
                    return false;
                }
            } else if (callee->kind == TESS_EXPR_MEMBER &&
                       callee->as.member.object != NULL &&
                       callee->as.member.object->kind == TESS_EXPR_MODULE) {
                const char *mod = callee->as.member.object->as.module;
                const char *alias_target = module_table_find(&s->modules, mod);
                if (alias_target == NULL) {
                    if (!check_expr(s, callee->as.member.object)) return false;
                } else {
                    tess_fn_entry *fn = fn_table_find(&s->fns, alias_target,
                                                       callee->as.member.name);
                    (void)fn;
                }
            }

            /* Check module members as call targets without treating the
             * module expression as a runtime value. */
            bool module_call = callee->kind == TESS_EXPR_MEMBER &&
                callee->as.member.object != NULL &&
                callee->as.member.object->kind == TESS_EXPR_MODULE;
            if (module_call) {
                if (!check_expr(s, callee)) return false;
            }

            /* Method calls: validate arity and argument types when the
             * receiver's class is known. */
            if (callee->kind == TESS_EXPR_MEMBER && !module_call) {
                const char *mname = callee->as.member.name;
                tess_class *cls = receiver_class(s, callee->as.member.object);
                if (cls != NULL && mname != NULL) {
                    tess_method *method = class_find_method(cls, mname);
                    if (strcmp(mname, "new") == 0) {
                        if (argc != 0) {
                            report(s, e->start, e->end,
                                   "`new` does not take arguments");
                            return false;
                        }
                    } else if (strcmp(mname, "init") == 0) {
                        if (method != NULL) {
                            if (!check_method_args(s, e, cls, method, mname, argc))
                                return false;
                        } else {
                            char msg[256];
                            snprintf(msg, sizeof msg,
                                     "class `%s` has no `init` method",
                                     cls->name);
                            report(s, e->start, e->end, msg);
                            return false;
                        }
                    } else if (method != NULL) {
                        if (!check_method_args(s, e, cls, method, mname, argc))
                            return false;
                    } else if (class_find_field(cls, mname) != NULL) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "`%s.%s` is a field, not a method",
                                 cls->name, mname);
                        report(s, e->start, e->end, msg);
                        return false;
                    } else if (method == NULL &&
                               strcmp(mname, "new") != 0 &&
                               strcmp(mname, "init") != 0) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "class `%s` has no member `%s`",
                                 cls->name, mname);
                        report(s, e->start, e->end, msg);
                        return false;
                    }
                }
            }

            /* When the callee is a member access, checking it as a value would
             * wrongly demand that a parameterised method be called with no
             * arguments. Validate the receiver instead; the member itself was
             * checked above. */
            if (callee->kind == TESS_EXPR_MEMBER) {
                if (!check_expr(s, callee->as.member.object)) return false;
            } else if (callee->kind != TESS_EXPR_MEMBER && !check_expr(s, callee)) {
                return false;
            }
            for (size_t i = 0; i < argc; i++)
                if (!check_expr(s, e->as.call.args[i])) return false;
            return true;
        }

        case TESS_EXPR_ASSIGN: {
            if (e->as.assign.target == NULL || e->as.assign.value == NULL) {
                report(s, e->start, e->end, "invalid assignment");
                return false;
            }
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
            if (e->as.binary.lhs == NULL || e->as.binary.rhs == NULL) {
                report(s, e->start, e->end, "invalid binary expression");
                return false;
            }
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
            if (e->as.unary.operand == NULL) {
                report(s, e->start, e->end, "invalid unary expression");
                return false;
            }
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
            if (e->as.ternary.cond == NULL) {
                report(s, e->start, e->end, "invalid conditional expression");
                return false;
            }
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

static bool check_fn_call(tess_sema *s, tess_expr *callee, size_t argc,
                          tess_expr **args) {
    if (callee == NULL) return true;
    const char *name = NULL;
    const char *module = NULL;
    if (callee->kind == TESS_EXPR_IDENT) {
        name = callee->as.ident;
    } else if (callee->kind == TESS_EXPR_MEMBER &&
               callee->as.member.object != NULL &&
               callee->as.member.object->kind == TESS_EXPR_MODULE) {
        module = module_table_find(&s->modules,
            callee->as.member.object->as.module);
        name = callee->as.member.name;
    }
    if (name == NULL) return true;
    tess_fn_entry *fn = fn_table_find(&s->fns, module, name);
    if (fn == NULL) return true;
    if (fn->param_count != argc) {
        report(s, callee->start, callee->end,
               "wrong number of arguments to function");
        return false;
    }
    /* Argument types are validated against the declared parameter types. */
    for (size_t i = 0; i < argc; i++) {
        const char *want_name = fn->param_types != NULL
            ? fn->param_types[i] : NULL;
        if (want_name == NULL) continue;
        tess_class *want_cls = NULL;
        tess_type want = type_from_name(s, want_name, &want_cls);
        if (want == TYPE_NULL) continue;
        tess_type got = infer_expr_type(s, args != NULL ? args[i] : NULL);
        if (!can_assign(want, got) && got != TYPE_NULL) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "argument %zu of `%s` has type %s, expected %s",
                     i + 1, name, type_label(got), type_label(want));
            report(s, args != NULL && args[i] != NULL ? args[i]->start
                       : callee->start,
                   args != NULL && args[i] != NULL ? args[i]->end
                       : callee->end, msg);
            return false;
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

            /* Built-in functions and class names are not variables. */
            if (target->kind == TESS_EXPR_IDENT) {
                sym_entry *existing = scope_lookup(s->scope, target->as.ident);
                if (existing != NULL && existing->kind != SYM_VAR) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "cannot assign to %s `%s`",
                             existing->kind == SYM_CLASS ? "class" : "builtin",
                             target->as.ident);
                    report(s, target->start, target->end, msg);
                    return false;
                }
            }
            
            /* Check the value first so its type is known. */
            if (!check_expr(s, value)) return false;

            /* A call whose function/method returns nothing has no value to
             * assign; the backend cannot spell it in C either. */
            if (value->kind == TESS_EXPR_CALL && value->as.call.callee != NULL) {
                if (value->as.call.callee->kind == TESS_EXPR_IDENT) {
                    tess_fn_entry *fn = fn_table_find(
                        &s->fns, NULL, value->as.call.callee->as.ident);
                    if (fn != NULL && fn->ret_type == NULL) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "function `%s` returns no value",
                                 fn->name);
                        report(s, value->start, value->end, msg);
                        return false;
                    }
                } else if (value->as.call.callee->kind == TESS_EXPR_MEMBER &&
                           value->as.call.callee->as.member.object != NULL &&
                           value->as.call.callee->as.member.object->kind
                               != TESS_EXPR_MODULE) {
                    tess_class *cls = receiver_class(
                        s, value->as.call.callee->as.member.object);
                    const char *mname = value->as.call.callee->as.member.name;
                    /* `Class.init(...)` constructs an instance; it is not a
                     * plain method call and always yields a value. */
                    if (cls != NULL && mname != NULL &&
                        strcmp(mname, "new") != 0 &&
                        strcmp(mname, "init") != 0) {
                        tess_method *m = class_find_method(cls, mname);
                        if (m != NULL && m->ret_type == NULL) {
                            char msg[256];
                            snprintf(msg, sizeof msg,
                                     "method `%s.%s` returns no value",
                                     cls->name, mname);
                            report(s, value->start, value->end, msg);
                            return false;
                        }
                    }
                }
            }

            if (target->kind == TESS_EXPR_IDENT) {
                /* Declare the variable if this is its first assignment
                 * (Tesseract uses assign-to-declare). */
                sym_entry *visible = scope_lookup(s->scope, target->as.ident);
                if (visible == NULL || visible->kind != SYM_VAR) {
                    tess_type var_type = infer_expr_type(s, value);
                    tess_class *var_cls = NULL;

                    if (var_type == TYPE_NULL) {
                        var_cls = class_from_construction(s, value);
                        var_type = var_cls != NULL ? TYPE_CLASS : TYPE_INT;
                    }

                    if (!scope_define(s->scope, target->as.ident,
                                      SYM_VAR, var_type, var_cls, -1, false)) {
                        report(s, target->start, target->end,
                               "out of memory while declaring variable");
                        return false;
                    }
                } else {
                    /* Existing variable: enforce assignment compatibility. */
                    tess_type target_type = infer_expr_type(s, target);
                    tess_type value_type = infer_expr_type(s, value);
                    /* A construction value is class-typed even though the
                     * lightweight inference reports NULL. */
                    if (class_from_construction(s, value) != NULL) {
                        value_type = TYPE_CLASS;
                    }
                    if (!can_assign(target_type, value_type) &&
                        value_type != TYPE_NULL) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "cannot assign %s to %s",
                                 type_label(value_type), type_label(target_type));
                        report(s, st->start, st->end, msg);
                        return false;
                    }
                }
            } else {
                /* Member assignment: the object must resolve, and the field
                 * type must accept the value. */
                if (!check_expr(s, target)) return false;
                tess_type target_type = infer_expr_type(s, target);
                tess_type value_type = infer_expr_type(s, value);
                if (!can_assign(target_type, value_type) &&
                    value_type != TYPE_NULL && target_type != TYPE_NULL) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "cannot assign %s to %s",
                             type_label(value_type), type_label(target_type));
                    report(s, st->start, st->end, msg);
                    return false;
                }
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
            if (!push_scope(s, &then_sc, false)) {
                report(s, st->start, st->end, "out of memory while analysing scope");
                return false;
            }
            bool ok = check_stmt(s, st->as.if_stmt.then_branch);
            pop_scope(s);
            if (!ok) return false;

            if (st->as.if_stmt.else_branch != NULL) {
                tess_scope else_sc;
                if (!push_scope(s, &else_sc, false)) {
                    report(s, st->start, st->end,
                           "out of memory while analysing scope");
                    return false;
                }
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
            if (!push_scope(s, &body_sc, false)) {
                report(s, st->start, st->end, "out of memory while analysing scope");
                return false;
            }
            bool ok = check_stmt(s, st->as.while_stmt.body);
            pop_scope(s);
            return ok;
        }

        case TESS_STMT_RETURN: {
            /* Search enclosing scopes so returns remain valid inside nested
             * blocks while top-level returns are rejected. */
            if (!s->scope->in_function) {
                report(s, st->start, st->end,
                       "`return` outside a function or method body");
                return false;
            }
            const char *want_name = NULL;
            const char *what = NULL;
            if (s->current_method != NULL) {
                want_name = s->current_method->ret_type;
                what = "method";
            } else if (s->current_fn != NULL) {
                want_name = s->current_fn->as.fn.ret_type;
                what = "function";
            }
            if (want_name == NULL) {
                if (st->as.ret != NULL) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "void %s cannot return a value",
                             what != NULL ? what : "function");
                    report(s, st->start, st->end, msg);
                    return false;
                }
                return true;
            }
            if (st->as.ret == NULL) {
                char msg[256];
                snprintf(msg, sizeof msg,
                         "%s declares a return value; `return` needs one",
                         what != NULL ? what : "function");
                report(s, st->start, st->end, msg);
                return false;
            }
            if (!check_expr(s, st->as.ret)) return false;
            tess_class *want_cls = NULL;
            tess_type want = type_from_name(s, want_name, &want_cls);
            tess_type got = infer_expr_type(s, st->as.ret);
            if (!can_assign(want, got) && got != TYPE_NULL) {
                char msg[256];
                snprintf(msg, sizeof msg,
                         "return value has type %s, expected %s",
                         type_label(got), type_label(want));
                report(s, st->as.ret->start, st->as.ret->end, msg);
                return false;
            }
            return true;
        }

        case TESS_STMT_BLOCK: {
            tess_scope block_sc;
            if (!push_scope(s, &block_sc, false)) {
                report(s, st->start, st->end, "out of memory while analysing scope");
                return false;
            }
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

            /* Field types must name a primitive or a known class. */
            for (size_t i = 0; i < cls->field_count; i++) {
                if (!type_name_known(s, cls->fields[i].type)) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "unknown type `%s` for field `%s`",
                             cls->fields[i].type ? cls->fields[i].type : "?",
                             cls->fields[i].name);
                    report(s, cls->fields[i].start, cls->fields[i].end, msg);
                    return false;
                }
            }

            /* Duplicate field names. */
            for (size_t i = 0; i < cls->field_count; i++) {
                for (size_t j = 0; j < i; j++) {
                    if (strcmp(cls->fields[i].name, cls->fields[j].name) == 0) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "duplicate field `%s` in class `%s`",
                                 cls->fields[i].name, cls->name);
                        report(s, cls->fields[i].start, cls->fields[i].end, msg);
                        return false;
                    }
                }
            }
            /* Duplicate method names, and collisions with fields. */
            for (size_t i = 0; i < cls->method_count; i++) {
                for (size_t j = 0; j < i; j++) {
                    if (strcmp(cls->methods[i].name, cls->methods[j].name) == 0) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "duplicate method `%s` in class `%s`",
                                 cls->methods[i].name, cls->name);
                        report(s, cls->methods[i].start, cls->methods[i].end, msg);
                        return false;
                    }
                }
                for (size_t j = 0; j < cls->field_count; j++) {
                    if (strcmp(cls->methods[i].name, cls->fields[j].name) == 0) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "member `%s` in class `%s` is both a field and a method",
                                 cls->methods[i].name, cls->name);
                        report(s, cls->methods[i].start, cls->methods[i].end, msg);
                        return false;
                    }
                }
            }

            /* Method return types must name a primitive or a known class. */
            for (size_t i = 0; i < cls->method_count; i++) {
                const char *ret = cls->methods[i].ret_type;
                if (ret != NULL && !type_name_known(s, ret)) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "unknown return type `%s` for method `%s`",
                             ret, cls->methods[i].name);
                    report(s, cls->methods[i].start, cls->methods[i].end, msg);
                    return false;
                }
            }

            /* Check each method body in its own scope. */
            tess_class *saved_class = s->current_class;
            s->current_class = cls;

            for (size_t i = 0; i < cls->method_count; i++) {
                tess_method *m = &cls->methods[i];
                s->current_method = m;
                tess_scope msc;
                if (!push_scope(s, &msc, true)) {
                    report(s, m->start, m->end,
                           "out of memory while analysing method");
                    s->current_class = saved_class;
                    return false;
                }

                /* Declare 'self' as implicit first parameter */
                scope_define(s->scope, "self", SYM_VAR, TYPE_CLASS, cls, -1, false);

                /* Declare parameters, rejecting duplicates and unknown types. */
                for (size_t j = 0; j < m->param_count; j++) {
                    if (!type_name_known(s, m->params[j].type)) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "unknown type `%s` for parameter `%s`",
                                 m->params[j].type ? m->params[j].type : "?",
                                 m->params[j].name);
                        report(s, m->start, m->end, msg);
                        pop_scope(s);
                        s->current_class = saved_class;
                        return false;
                    }
                    tess_class *param_cls = NULL;
                    tess_type param_type = type_from_name(s, m->params[j].type,
                                                          &param_cls);
                    if (!scope_define(s->scope, m->params[j].name,
                                      SYM_VAR, param_type, param_cls,
                                      -1, false)) {
                        char msg[256];
                        snprintf(msg, sizeof msg,
                                 "duplicate parameter `%s` in method `%s`",
                                 m->params[j].name, m->name);
                        report(s, m->start, m->end, msg);
                        pop_scope(s);
                        s->current_class = saved_class;
                        return false;
                    }
                }

                /* Fields are visible as bare names inside methods, carrying
                 * their declared types. */
                for (size_t j = 0; j < cls->field_count && !s->failed; j++) {
                    if (scope_lookup_own(s->scope, cls->fields[j].name) == NULL) {
                        tess_class *field_cls = NULL;
                        tess_type field_type = type_from_name(
                            s, cls->fields[j].type, &field_cls);
                        scope_define(s->scope, cls->fields[j].name,
                                     SYM_VAR, field_type, field_cls,
                                     -1, false);
                    }
                }

                if (s->failed) {
                    pop_scope(s);
                    break;
                }

                bool ok = check_stmt(s, m->body);
                pop_scope(s);
                s->current_method = NULL;
                if (!ok) {
                    s->current_class = saved_class;
                    return false;
                }
            }
            s->current_class = saved_class;
            return !s->failed;
        }

        case TESS_STMT_IMPORT: {
            const char *module = st->as.import.module;
            const char *path = st->as.import.path;
            const char *alias = st->as.import.alias;
            if (module == NULL && path == NULL) {
                report(s, st->start, st->end, "invalid module import");
                return false;
            }
            if (path != NULL) {
                /* Parsing a path is useful for diagnostics/future linking,
                 * but this implementation supports only built-in modules. */
                report(s, st->start, st->end,
                       "file imports are not supported by this compiler yet");
                return false;
            }
            const char *module_key = module;
            if (module_key == NULL) module_key = "";
            if (module_table_find(&s->modules, module_key) == NULL) {
                char msg[256];
                snprintf(msg, sizeof msg, "unknown module `%s`", module_key);
                report(s, st->start, st->end, msg);
                return false;
            }
            for (size_t i = 0; i < s->modules.count; i++) {
                if (s->modules.items[i].builtin) continue;
                if (strcmp(s->modules.items[i].alias, alias) == 0 &&
                    strcmp(s->modules.items[i].module, module_key) != 0) {
                    report(s, st->start, st->end,
                           "duplicate import alias bound to another module");
                    return false;
                }
            }
            if (!module_table_add(&s->modules, alias, module_key, false)) {
                report(s, st->start, st->end,
                       "out of memory while declaring module");
                return false;
            }
            if (alias != NULL) {
                sym_entry *existing = scope_lookup_own(s->scope, alias);
                if (existing != NULL) {
                    report(s, st->start, st->end,
                           "module alias conflicts with an existing name");
                    return false;
                }
                if (!scope_define(s->scope, alias, SYM_VAR, TYPE_NULL, NULL,
                                  -1, false)) {
                    report(s, st->start, st->end,
                           "out of memory while declaring module alias");
                    return false;
                }
            }
            return true;
        }

        case TESS_STMT_FN: {
            const char *name = st->as.fn.name;
            if (name == NULL) {
                report(s, st->start, st->end, "function without a name");
                return false;
            }
            for (size_t i = 0; i < st->as.fn.param_count; i++) {
                for (size_t j = 0; j < i; j++) {
                    if (strcmp(st->as.fn.params[i].name,
                               st->as.fn.params[j].name) == 0) {
                        report(s, st->start, st->end,
                               "duplicate function parameter");
                        return false;
                    }
                }
            }
            if (st->as.fn.fn_body == NULL) return true;
            tess_scope fn_sc;
            if (!push_scope(s, &fn_sc, true)) {
                report(s, st->start, st->end,
                       "out of memory while analysing scope");
                return false;
            }
            fn_sc.in_function = true;
            for (size_t i = 0; i < st->as.fn.param_count; i++) {
                tess_param *param = &st->as.fn.params[i];
                tess_class *param_cls = NULL;
                tess_type param_type = type_from_name(s, param->type,
                                                      &param_cls);
                if (!type_name_known(s, param->type)) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "unknown type `%s` for parameter `%s`",
                             param->type ? param->type : "?", param->name);
                    report(s, st->start, st->end, msg);
                    pop_scope(s);
                    return false;
                }
                if (!scope_define(s->scope, param->name, SYM_VAR, param_type,
                                  param_cls, -1, false)) {
                    report(s, st->start, st->end,
                           "duplicate function parameter");
                    pop_scope(s);
                    return false;
                }
            }
            if (st->as.fn.ret_type != NULL &&
                !type_name_known(s, st->as.fn.ret_type)) {
                char msg[256];
                snprintf(msg, sizeof msg, "unknown return type `%s`",
                         st->as.fn.ret_type);
                report(s, st->start, st->end, msg);
                pop_scope(s);
                return false;
            }
            s->current_fn = st;
            bool ok = check_stmt(s, st->as.fn.fn_body);
            s->current_fn = NULL;
            pop_scope(s);
            return ok;
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
    if (!push_scope(&s, &global, false)) {
        return false; /* nothing to free: the table was never allocated */
    }

    /* Register built-in functions */
    bool builtins_ok =
        scope_define(s.scope, "put",    SYM_BUILTIN, TYPE_NULL, NULL, 1, false) &&
        scope_define(s.scope, "assert", SYM_BUILTIN, TYPE_NULL, NULL, 1, false) &&
        scope_define(s.scope, "len",    SYM_BUILTIN, TYPE_INT, NULL, 1, false) &&
        scope_define(s.scope, "str",    SYM_BUILTIN, TYPE_STR, NULL, 1, false) &&
        scope_define(s.scope, "int",    SYM_BUILTIN, TYPE_INT, NULL, 1, false) &&
        scope_define(s.scope, "float",  SYM_BUILTIN, TYPE_FLOAT, NULL, 1, false) &&
        scope_define(s.scope, "print",  SYM_BUILTIN, TYPE_NULL, NULL, 1, false) &&
        scope_define(s.scope, "input",  SYM_BUILTIN, TYPE_STR, NULL, 0, false) &&
        scope_define(s.scope, "putln",  SYM_BUILTIN, TYPE_NULL, NULL, 1, false);

    /* Built-in native modules available for `import`. */
    const char *native_modules[] = {"math", "c", "std", "libc"};
    for (size_t i = 0; builtins_ok &&
         i < sizeof native_modules / sizeof native_modules[0]; i++) {
        builtins_ok = module_table_add(&s.modules, native_modules[i],
                                       native_modules[i], true);
    }
    if (!builtins_ok) {
        report(&s, 0, 0, "out of memory while registering built-ins");
    }

    /* Pass 1: pre-declare classes for forward type references. Functions are
     * collected in a separate pass below for the same source-order freedom. */
    for (size_t i = 0; i < program->count && !s.failed; i++) {
        tess_stmt *st = program->items[i];
        if (st == NULL) continue;
        if (st->kind == TESS_STMT_CLASS) {
            tess_class *cls = st->as.class_decl;
            if (!scope_define(s.scope, cls->name, SYM_CLASS, TYPE_CLASS, cls, -1, false)) {
                char msg[256];
                snprintf(msg, sizeof msg, "duplicate class name `%s`",
                         cls->name);
                report(&s, cls->start, cls->end, msg);
                break;
            }
        }
    }

    /* Pass 2: check all statements */
    for (size_t i = 0; i < program->count && !s.failed; i++) {
        tess_stmt *st = program->items[i];
        if (!st || st->kind != TESS_STMT_FN) continue;
        for (size_t j = 0; j < i; j++) {
            tess_stmt *prior = program->items[j];
            if (prior && prior->kind == TESS_STMT_FN &&
                strcmp(prior->as.fn.name, st->as.fn.name) == 0) {
                report(&s, st->start, st->end, "duplicate function name");
                break;
            }
        }
        if (!s.failed && !fn_table_add(&s.fns, NULL, st->as.fn.name,
                                       st->as.fn.ret_type,
                                       st->as.fn.params,
                                       st->as.fn.param_count))
            report(&s, st->start, st->end,
                   "out of memory while declaring function");
    }
    for (size_t i = 0; i < program->count && !s.failed; i++) {
        check_stmt(&s, program->items[i]);
    }

    bool ok = !s.failed;
    pop_scope(&s);
    module_table_destroy(&s.modules);
    fn_table_destroy(&s.fns);
    return ok;
}
