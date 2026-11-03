#include "lexer.h"

#include <string.h>

static bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static bool is_ident_start(unsigned char c) {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_ident_continue(unsigned char c) {
    return is_ident_start(c) || is_digit(c);
}

static bool lexeme_is(const tess_lexer *lx, size_t start, size_t end,
                      const char *word) {
    size_t n = strlen(word);
    return end - start == n && memcmp(lx->src + start, word, n) == 0;
}

static tess_token_kind keyword_kind(const tess_lexer *lx, size_t start,
                                    size_t end) {
    if (lexeme_is(lx, start, end, "if")) return TESS_TOK_KW_IF;
    if (lexeme_is(lx, start, end, "else")) return TESS_TOK_KW_ELSE;
    if (lexeme_is(lx, start, end, "while")) return TESS_TOK_KW_WHILE;
    if (lexeme_is(lx, start, end, "return")) return TESS_TOK_KW_RETURN;
    if (lexeme_is(lx, start, end, "true")) return TESS_TOK_KW_TRUE;
    if (lexeme_is(lx, start, end, "false")) return TESS_TOK_KW_FALSE;
    if (lexeme_is(lx, start, end, "null")) return TESS_TOK_KW_NULL;
    if (lexeme_is(lx, start, end, "import")) return TESS_TOK_KW_IMPORT;
    if (lexeme_is(lx, start, end, "fn")) return TESS_TOK_KW_FN;
    return TESS_TOK_IDENT;
}

static bool fail(tess_lexer *lx, const char *message, size_t at) {
    lx->error = message;
    lx->error_start = at;
    lx->error_end = at < lx->len ? at + 1 : at;
    return false;
}

void tess_lexer_init(tess_lexer *lx, const char *src, size_t len) {
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    /* Skip a UTF-8 byte-order mark so it does not become a stray token. */
    if (len >= 3 && (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
        lx->pos = 3;
    }
    lx->error = NULL;
    lx->error_start = 0;
    lx->error_end = 0;
}

static bool skip_space_and_comments(tess_lexer *lx) {
    for (;;) {
        while (lx->pos < lx->len) {
            unsigned char c = (unsigned char)lx->src[lx->pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
                c == '\v') {
                lx->pos++;
            } else {
                break;
            }
        }
        if (lx->pos + 1 >= lx->len || lx->src[lx->pos] != '/') {
            return true;
        }
        if (lx->src[lx->pos + 1] == '/') {
            lx->pos += 2;
            while (lx->pos < lx->len && lx->src[lx->pos] != '\n') {
                lx->pos++;
            }
            continue;
        }
        if (lx->src[lx->pos + 1] == '*') {
            size_t start = lx->pos;
            lx->pos += 2;
            while (lx->pos + 1 < lx->len &&
                   !(lx->src[lx->pos] == '*' && lx->src[lx->pos + 1] == '/')) {
                lx->pos++;
            }
            if (lx->pos + 1 >= lx->len) {
                return fail(lx, "unterminated block comment", start);
            }
            lx->pos += 2;
            continue;
        }
        return true;
    }
}

static bool lex_ident(tess_lexer *lx, tess_token *out) {
    size_t start = lx->pos++;
    while (lx->pos < lx->len &&
           is_ident_continue((unsigned char)lx->src[lx->pos])) {
        lx->pos++;
    }
    out->kind = keyword_kind(lx, start, lx->pos);
    out->start = start;
    out->end = lx->pos;
    return true;
}

static bool lex_number(tess_lexer *lx, tess_token *out) {
    size_t start = lx->pos;
    bool is_float = false;
    while (lx->pos < lx->len && is_digit((unsigned char)lx->src[lx->pos])) {
        lx->pos++;
    }
    if (lx->pos + 1 < lx->len && lx->src[lx->pos] == '.' &&
        is_digit((unsigned char)lx->src[lx->pos + 1])) {
        is_float = true;
        lx->pos++;
        while (lx->pos < lx->len && is_digit((unsigned char)lx->src[lx->pos])) {
            lx->pos++;
        }
    }
    if (lx->pos < lx->len &&
        (lx->src[lx->pos] == 'e' || lx->src[lx->pos] == 'E')) {
        size_t mark = lx->pos;
        lx->pos++;
        if (lx->pos < lx->len &&
            (lx->src[lx->pos] == '+' || lx->src[lx->pos] == '-')) {
            lx->pos++;
        }
        if (lx->pos >= lx->len ||
            !is_digit((unsigned char)lx->src[lx->pos])) {
            return fail(lx, "malformed exponent", mark);
        }
        is_float = true;
        while (lx->pos < lx->len && is_digit((unsigned char)lx->src[lx->pos])) {
            lx->pos++;
        }
    }
    if (lx->pos < lx->len &&
        is_ident_start((unsigned char)lx->src[lx->pos])) {
        return fail(lx, "invalid suffix on number", lx->pos);
    }
    out->kind = is_float ? TESS_TOK_FLOAT : TESS_TOK_INT;
    out->start = start;
    out->end = lx->pos;
    return true;
}

static bool lex_string(tess_lexer *lx, tess_token *out) {
    size_t start = lx->pos++;
    while (lx->pos < lx->len) {
        unsigned char c = (unsigned char)lx->src[lx->pos];
        if (c == '\\') {
            if (lx->pos + 1 >= lx->len) {
                return fail(lx, "unterminated string", start);
            }
            lx->pos += 2;
            continue;
        }
        lx->pos++;
        if (c == '"') {
            out->kind = TESS_TOK_STRING;
            out->start = start;
            out->end = lx->pos;
            return true;
        }
    }
    return fail(lx, "unterminated string", start);
}

static bool one(tess_lexer *lx, tess_token *out, tess_token_kind kind,
                size_t width) {
    out->kind = kind;
    out->start = lx->pos;
    out->end = lx->pos + width;
    lx->pos += width;
    return true;
}

bool tess_lexer_next(tess_lexer *lx, tess_token *out) {
    if (!skip_space_and_comments(lx)) {
        out->kind = TESS_TOK_EOF;
        out->start = lx->pos;
        out->end = lx->pos;
        return false;
    }
    if (lx->pos >= lx->len) {
        out->kind = TESS_TOK_EOF;
        out->start = lx->len;
        out->end = lx->len;
        return true;
    }

    size_t at = lx->pos;
    unsigned char c = (unsigned char)lx->src[at];
    if (is_ident_start(c)) return lex_ident(lx, out);
    if (is_digit(c)) return lex_number(lx, out);
    if (c == '"') return lex_string(lx, out);

    bool has_next = at + 1 < lx->len;
    unsigned char n = has_next ? (unsigned char)lx->src[at + 1] : 0;
    switch (c) {
        case '@': return one(lx, out, TESS_TOK_AT, 1);
        case '.': return one(lx, out, TESS_TOK_DOT, 1);
        case ':': return one(lx, out, TESS_TOK_COLON, 1);
        case ';': return one(lx, out, TESS_TOK_SEMI, 1);
        case ',': return one(lx, out, TESS_TOK_COMMA, 1);
        case '(': return one(lx, out, TESS_TOK_LPAREN, 1);
        case ')': return one(lx, out, TESS_TOK_RPAREN, 1);
        case '{': return one(lx, out, TESS_TOK_LBRACE, 1);
        case '}': return one(lx, out, TESS_TOK_RBRACE, 1);
        case '?': return one(lx, out, TESS_TOK_QUESTION, 1);
        case '~': return one(lx, out, TESS_TOK_TILDE, 1);
        case '+': return one(lx, out, TESS_TOK_PLUS, 1);
        case '-': return one(lx, out, TESS_TOK_MINUS, 1);
        case '*': return one(lx, out, TESS_TOK_STAR, 1);
        case '%': return one(lx, out, TESS_TOK_PERCENT, 1);
        case '^': return one(lx, out, TESS_TOK_CARET, 1);
        case '/': return one(lx, out, TESS_TOK_SLASH, 1);
        case '!':
            if (has_next && n == '=') {
                return one(lx, out, TESS_TOK_NEQ, 2);
            }
            return one(lx, out, TESS_TOK_BANG, 1);
        case '=':
            if (has_next && n == '=') {
                return one(lx, out, TESS_TOK_EQEQ, 2);
            }
            return one(lx, out, TESS_TOK_EQ, 1);
        case '<':
            if (has_next && n == '=') {
                return one(lx, out, TESS_TOK_LE, 2);
            }
            return one(lx, out, TESS_TOK_LT, 1);
        case '>':
            if (has_next && n == '=') {
                return one(lx, out, TESS_TOK_GE, 2);
            }
            return one(lx, out, TESS_TOK_GT, 1);
        case '&':
            if (has_next && n == '&') {
                return one(lx, out, TESS_TOK_ANDAND, 2);
            }
            return one(lx, out, TESS_TOK_AMP, 1);
        case '|':
            if (has_next && n == '|') {
                return one(lx, out, TESS_TOK_OROR, 2);
            }
            return one(lx, out, TESS_TOK_PIPE, 1);
        default:
            break;
    }
    return fail(lx, "unexpected character", at);
}

const char *tess_token_kind_name(tess_token_kind kind) {
    switch (kind) {
        case TESS_TOK_EOF: return "end of input";
        case TESS_TOK_IDENT: return "identifier";
        case TESS_TOK_INT: return "integer";
        case TESS_TOK_FLOAT: return "float";
        case TESS_TOK_STRING: return "string";
        case TESS_TOK_AT: return "`@`";
        case TESS_TOK_DOT: return "`.`";
        case TESS_TOK_COLON: return "`:`";
        case TESS_TOK_SEMI: return "`;`";
        case TESS_TOK_COMMA: return "`,`";
        case TESS_TOK_LPAREN: return "`(`";
        case TESS_TOK_RPAREN: return "`)`";
        case TESS_TOK_LBRACE: return "`{`";
        case TESS_TOK_RBRACE: return "`}`";
        case TESS_TOK_QUESTION: return "`?`";
        case TESS_TOK_BANG: return "`!`";
        case TESS_TOK_EQ: return "`=`";
        case TESS_TOK_EQEQ: return "`==`";
        case TESS_TOK_NEQ: return "`!=`";
        case TESS_TOK_PLUS: return "`+`";
        case TESS_TOK_MINUS: return "`-`";
        case TESS_TOK_STAR: return "`*`";
        case TESS_TOK_SLASH: return "`/`";
        case TESS_TOK_PERCENT: return "`%`";
        case TESS_TOK_LT: return "`<`";
        case TESS_TOK_LE: return "`<=`";
        case TESS_TOK_GT: return "`>`";
        case TESS_TOK_GE: return "`>=`";
        case TESS_TOK_ANDAND: return "`&&`";
        case TESS_TOK_OROR: return "`||`";
        case TESS_TOK_AMP: return "`&`";
        case TESS_TOK_PIPE: return "`|`";
        case TESS_TOK_CARET: return "`^`";
        case TESS_TOK_TILDE: return "`~`";
        case TESS_TOK_KW_IF: return "`if`";
        case TESS_TOK_KW_ELSE: return "`else`";
        case TESS_TOK_KW_WHILE: return "`while`";
        case TESS_TOK_KW_RETURN: return "`return`";
        case TESS_TOK_KW_TRUE: return "`true`";
        case TESS_TOK_KW_FALSE: return "`false`";
        case TESS_TOK_KW_NULL: return "`null`";
        case TESS_TOK_KW_IMPORT: return "`import`";
        case TESS_TOK_KW_FN: return "`fn`";
    }
    return "token";
}
