#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tess_arena_block tess_arena_block;

typedef struct {
    tess_arena_block *head;
} tess_arena;

void *tess_arena_alloc(tess_arena *arena, size_t bytes);
char *tess_arena_strndup(tess_arena *arena, const char *text, size_t len);
void tess_arena_destroy(tess_arena *arena);

typedef struct tess_expr tess_expr;
typedef struct tess_stmt tess_stmt;

typedef struct {
    char *name;
    char *type;
} tess_param;

typedef struct {
    char *name;
    char *type;
    uint32_t start;
    uint32_t end;
} tess_field;

typedef struct {
    char *name;
    char *ret_type;   /* NULL means the method returns nothing (void) */
    tess_param *params;
    size_t param_count;
    tess_stmt *body;
    uint32_t start;
    uint32_t end;
} tess_method;

typedef struct {
    char *name;
    tess_field *fields;
    size_t field_count;
    tess_method *methods;
    size_t method_count;
    uint32_t start;
    uint32_t end;
} tess_class;

typedef struct { char *name; tess_field *fields; size_t field_count; bool is_union; } tess_record;

typedef enum {
    TESS_STMT_EMPTY,
    TESS_STMT_CLASS,
    TESS_STMT_ASSIGN,
    TESS_STMT_EXPR,
    TESS_STMT_IF,
    TESS_STMT_WHILE,
    TESS_STMT_RETURN,
    TESS_STMT_BLOCK,
    TESS_STMT_IMPORT,
    TESS_STMT_FN,
    TESS_STMT_FOR,
    TESS_STMT_LINK,
    TESS_STMT_BREAK,
    TESS_STMT_CONTINUE
    ,TESS_STMT_ASM
    ,TESS_STMT_RECORD
} tess_stmt_kind;

struct tess_stmt {
    tess_stmt_kind kind;
    uint32_t start;
    uint32_t end;
    union {
        tess_class *class_decl;
        struct {
            tess_expr *target;
            tess_expr *value;
            bool declaration; /* explicitly introduced with `let` */
            char *declared_type;
        } assign;
        tess_expr *expr;
        struct {
            tess_expr *cond;
            tess_stmt *then_branch;
            tess_stmt *else_branch;
        } if_stmt;
        struct {
            tess_expr *cond;
            tess_stmt *body;
        } while_stmt;
        tess_expr *ret;
        struct {
            tess_stmt **items;
            size_t count;
        } block;
        struct {
            char *module;
            char *path;
            char *alias;
        } import;
        struct {
            tess_param *params;
            size_t param_count;
            char *name;
            char *ret_type;
            tess_stmt *fn_body;
            bool external; /* declaration provided by a C library */
            bool variadic; /* C-style trailing `...` */
        } fn;
        struct {
            tess_stmt *init;
            tess_expr *condition; /* NULL means an unconditional loop */
            tess_expr *step;
            tess_stmt *body;
        } for_stmt;
        struct {
            char *library;
        } link;
        struct { char *text; bool is_volatile; } assembly;
        tess_record *record_decl;
    } as;
};

typedef enum {
    TESS_EXPR_INT,
    TESS_EXPR_FLOAT,
    TESS_EXPR_STRING,
    TESS_EXPR_BOOL,
    TESS_EXPR_NULL,
    TESS_EXPR_IDENT,
    TESS_EXPR_MODULE,
    TESS_EXPR_MEMBER,
    TESS_EXPR_CALL,
    TESS_EXPR_ASSIGN,
    TESS_EXPR_BINARY,
    TESS_EXPR_UNARY,
    TESS_EXPR_TERNARY,
    TESS_EXPR_INDEX
    ,TESS_EXPR_ARRAY
    ,TESS_EXPR_SLICE
    ,TESS_EXPR_RECORD_INIT
} tess_expr_kind;

typedef enum {
    TESS_BIN_OR,
    TESS_BIN_AND,
    TESS_BIN_BIT_OR,
    TESS_BIN_BIT_XOR,
    TESS_BIN_BIT_AND,
    TESS_BIN_EQ,
    TESS_BIN_NE,
    TESS_BIN_LT,
    TESS_BIN_LE,
    TESS_BIN_GT,
    TESS_BIN_GE,
    TESS_BIN_ADD,
    TESS_BIN_SUB,
    TESS_BIN_MUL,
    TESS_BIN_DIV,
    TESS_BIN_MOD,
    TESS_BIN_SHL,
    TESS_BIN_SHR
} tess_binary_op;

typedef enum {
    TESS_UN_NEG,
    TESS_UN_POS,
    TESS_UN_NOT,
    TESS_UN_BIT_NOT,
    TESS_UN_ADDRESS,
    TESS_UN_DEREFERENCE
} tess_unary_op;

struct tess_expr {
    tess_expr_kind kind;
    uint32_t start;
    uint32_t end;
    union {
        int64_t int_value;
        double float_value;
        struct {
            char *data;
            size_t len;
        } string;
        bool bool_value;
        char *ident;
        char *module;
        struct {
            tess_expr *object;
            char *name;
        } member;
        struct {
            tess_expr *callee;
            tess_expr **args;
            size_t arg_count;
        } call;
        struct {
            tess_expr *target;
            tess_expr *value;
        } assign;
        struct {
            tess_binary_op op;
            tess_expr *lhs;
            tess_expr *rhs;
        } binary;
        struct {
            tess_unary_op op;
            tess_expr *operand;
        } unary;
        struct {
            tess_expr *cond;
            tess_expr *then_expr;
            tess_expr *else_expr;
        } ternary;
        struct { tess_expr *object; tess_expr *index; } index;
        struct { tess_expr **items; size_t count; } array;
        struct { tess_expr *object; tess_expr *start; tess_expr *end; } slice;
        struct { char *type_name; char **names; tess_expr **values; size_t count; } record_init;
    } as;
};

typedef struct {
    tess_stmt **items;
    size_t count;
    tess_arena arena;
} tess_program;

tess_program *tess_parse(const char *src, size_t len, const char *path);
void tess_program_free(tess_program *program);
