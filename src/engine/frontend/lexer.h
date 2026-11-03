#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TESS_TOK_EOF,
    TESS_TOK_IDENT,
    TESS_TOK_INT,
    TESS_TOK_FLOAT,
    TESS_TOK_STRING,
    TESS_TOK_AT,
    TESS_TOK_DOT,
    TESS_TOK_COLON,
    TESS_TOK_SEMI,
    TESS_TOK_COMMA,
    TESS_TOK_LPAREN,
    TESS_TOK_RPAREN,
    TESS_TOK_LBRACE,
    TESS_TOK_RBRACE,
    TESS_TOK_QUESTION,
    TESS_TOK_BANG,
    TESS_TOK_EQ,
    TESS_TOK_EQEQ,
    TESS_TOK_NEQ,
    TESS_TOK_PLUS,
    TESS_TOK_MINUS,
    TESS_TOK_STAR,
    TESS_TOK_SLASH,
    TESS_TOK_PERCENT,
    TESS_TOK_LT,
    TESS_TOK_LE,
    TESS_TOK_GT,
    TESS_TOK_GE,
    TESS_TOK_ANDAND,
    TESS_TOK_OROR,
    TESS_TOK_AMP,
    TESS_TOK_PIPE,
    TESS_TOK_CARET,
    TESS_TOK_TILDE,
    TESS_TOK_KW_IF,
    TESS_TOK_KW_ELSE,
    TESS_TOK_KW_WHILE,
    TESS_TOK_KW_RETURN,
    TESS_TOK_KW_TRUE,
    TESS_TOK_KW_FALSE,
    TESS_TOK_KW_NULL,
    TESS_TOK_KW_IMPORT,
    TESS_TOK_KW_FN
} tess_token_kind;

typedef struct {
    tess_token_kind kind;
    size_t start;
    size_t end;
} tess_token;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    const char *error;
    size_t error_start;
    size_t error_end;
} tess_lexer;

void tess_lexer_init(tess_lexer *lx, const char *src, size_t len);
bool tess_lexer_next(tess_lexer *lx, tess_token *out);
const char *tess_token_kind_name(tess_token_kind kind);
