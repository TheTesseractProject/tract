#include "ast.h"

#include "core_alloc.h"
#include "core_diagnostics.h"
#include "lexer.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TESS_MAX_DEPTH 256


typedef struct {
    void *items;
    size_t count;
    size_t cap;
} tess_vec;

typedef struct {
    const char *src;
    size_t len;
    const char *path;
    tess_lexer lx;
    tess_token cur;
    tess_token next;
    bool failed;
    char message[256];
    uint32_t err_start;
    uint32_t err_end;
    tess_program *program;
    int depth;
} tess_parser;

static tess_stmt *parse_statement(tess_parser *p);
static tess_stmt *parse_statement_impl(tess_parser *p);
static tess_stmt *parse_import(tess_parser *p);
static tess_stmt *parse_fn(tess_parser *p, bool external);

static bool fail_at(tess_parser *p, const char *message, size_t start,
                    size_t end) {
    if (p->failed) {
        return false;
    }
    p->failed = true;
    snprintf(p->message, sizeof p->message, "%s", message);
    if (end > p->len) {
        end = p->len;
    }
    if (start > end) {
        start = end;
    }
    p->err_start = (uint32_t)start;
    p->err_end = (uint32_t)end;
    return false;
}

static bool fail_oom(tess_parser *p) {
    return fail_at(p, "out of memory", p->cur.start, p->cur.end);
}

/* Bound statement/block nesting so a pathological input cannot exhaust the C
 * stack before the parser has a chance to report an error. */
static tess_stmt *parse_statement(tess_parser *p) {
    if (p->depth >= TESS_MAX_DEPTH) {
        fail_at(p, "maximum statement nesting depth exceeded", p->cur.start,
                p->cur.end);
        return NULL;
    }
    p->depth++;
    tess_stmt *stmt = parse_statement_impl(p);
    p->depth--;
    return stmt;
}

/* Kind of the token `ahead` positions past `next` (1 = the token after
 * `next`), without consuming anything: class member dispatch needs three
 * lookahead tokens to tell `name: type;` (field) from `name: Type { ... }`
 * (method with a return type). */
static tess_token_kind peek_kind_ahead(tess_parser *p, int ahead) {
    tess_lexer save = p->lx;
    tess_token tok;
    tok.kind = TESS_TOK_EOF;
    for (int i = 0; i < ahead; i++) {
        if (!tess_lexer_next(&p->lx, &tok)) {
            tok.kind = TESS_TOK_EOF;
            break;
        }
    }
    p->lx = save;
    return tok.kind;
}

static bool lex_into(tess_parser *p, tess_token *tok) {
    if (!tess_lexer_next(&p->lx, tok)) {
        fail_at(p, p->lx.error ? p->lx.error : "lexical error",
                p->lx.error_start, p->lx.error_end);
        return false;
    }
    return true;
}

static void advance(tess_parser *p) {
    p->cur = p->next;
    tess_token tok;
    if (!lex_into(p, &tok)) {
        tok.kind = TESS_TOK_EOF;
        tok.start = p->len;
        tok.end = p->len;
    }
    p->next = tok;
}

static void token_desc(const tess_parser *p, tess_token tok, char *buf,
                       size_t cap) {
    if (tok.kind == TESS_TOK_EOF) {
        snprintf(buf, cap, "end of input");
        return;
    }
    if (tok.kind == TESS_TOK_IDENT || tok.kind == TESS_TOK_INT ||
        tok.kind == TESS_TOK_FLOAT || tok.kind == TESS_TOK_STRING) {
        size_t n = tok.end - tok.start;
        if (n > 32) {
            n = 32;
        }
        snprintf(buf, cap, "'%.*s'", (int)n, p->src + tok.start);
        return;
    }
    snprintf(buf, cap, "%s", tess_token_kind_name(tok.kind));
}

static bool expect(tess_parser *p, tess_token_kind kind, const char *what) {
    if (p->failed) {
        return false;
    }
    if (p->cur.kind == kind) {
        advance(p);
        return true;
    }
    char got[64];
    char message[256];
    token_desc(p, p->cur, got, sizeof got);
    snprintf(message, sizeof message, "expected %s, found %s", what, got);
    return fail_at(p, message, p->cur.start, p->cur.end);
}

static bool vec_push(tess_parser *p, tess_vec *v, size_t elem,
                     const void *item) {
    if (v->count == v->cap) {
        size_t cap = v->cap == 0 ? 4 : v->cap * 2;
        if (cap > SIZE_MAX / elem) {
            return fail_oom(p);
        }
        /* Arena growth leaves prior arrays alive; that is safe because the
         * whole AST is reclaimed together after parsing/compilation. */
        void *items = tess_arena_alloc(&p->program->arena, cap * elem);
        if (items == NULL) {
            return fail_oom(p);
        }
        if (v->items != NULL && v->count != 0) {
            memcpy(items, v->items, v->count * elem);
        }
        v->items = items;
        v->cap = cap;
    }
    memcpy((unsigned char *)v->items + v->count * elem, item, elem);
    v->count++;
    return true;
}

static tess_expr *new_expr(tess_parser *p, tess_expr_kind kind, uint32_t start,
                           uint32_t end) {
    tess_expr *e = tess_arena_alloc(&p->program->arena, sizeof *e);
    if (e == NULL) {
        fail_oom(p);
        return NULL;
    }
    e->kind = kind;
    e->start = start;
    e->end = end;
    return e;
}

static tess_stmt *new_stmt(tess_parser *p, tess_stmt_kind kind, uint32_t start,
                           uint32_t end) {
    tess_stmt *s = tess_arena_alloc(&p->program->arena, sizeof *s);
    if (s == NULL) {
        fail_oom(p);
        return NULL;
    }
    s->kind = kind;
    s->start = start;
    s->end = end;
    return s;
}

static char *copy_ident(tess_parser *p, tess_token tok) {
    char *name = tess_arena_strndup(&p->program->arena, p->src + tok.start,
                                    tok.end - tok.start);
    if (name == NULL) {
        fail_oom(p);
    }
    return name;
}

static char *parse_type_name(tess_parser *p, const char *message) {
    bool is_volatile = p->cur.kind == TESS_TOK_KW_VOLATILE;
    if (is_volatile) advance(p);
    if (p->cur.kind != TESS_TOK_IDENT) {
        fail_at(p, message, p->cur.start, p->cur.end);
        return NULL;
    }
    char *base = copy_ident(p, p->cur);
    if (base == NULL) return NULL;
    advance(p);
    size_t n = strlen(base), cap = n + 128;
    char *type = tess_arena_alloc(&p->program->arena, cap);
    if (type == NULL) { fail_oom(p); return NULL; }
    memcpy(type, base, n + 1);
    while (p->cur.kind == TESS_TOK_STAR) {
        if (n + 2 >= cap) return fail_oom(p), NULL;
        type[n++] = '*'; type[n] = '\0'; advance(p);
    }
    bool slice_parameter = false;
    while (p->cur.kind == TESS_TOK_LBRACKET) {
        advance(p);
        if (strcmp(base, "slice") == 0 && !slice_parameter && p->cur.kind == TESS_TOK_IDENT) {
            slice_parameter = true;
            char *element = copy_ident(p, p->cur);
            if (element == NULL) return NULL;
            advance(p);
            if (!expect(p, TESS_TOK_RBRACKET, "`]` after slice element type")) return NULL;
            size_t en = strlen(element);
            if (n + en + 8 >= cap) return fail_oom(p), NULL;
            type[n++] = '['; memcpy(type + n, element, en); n += en;
            type[n++] = ']'; type[n] = '\0';
        } else {
            size_t begin = p->cur.start;
            if (p->cur.kind != TESS_TOK_INT) {
                fail_at(p, "fixed array type needs a constant length", p->cur.start, p->cur.end);
                return NULL;
            }
            size_t end = p->cur.end;
            advance(p);
            if (!expect(p, TESS_TOK_RBRACKET, "`]` after array length")) return NULL;
            size_t dim_len = end - begin;
            if (n + dim_len + 3 >= cap) return fail_oom(p), NULL;
            type[n++] = '['; memcpy(type + n, p->src + begin, dim_len); n += dim_len;
            type[n++] = ']'; type[n] = '\0';
        }
    }
    if (is_volatile) {
        if (n + 10 >= cap) return fail_oom(p), NULL;
        memmove(type + 9, type, n + 1); memcpy(type, "volatile ", 9);
    }
    return type;
}

static bool is_hex(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c - 'A' + 10;
}

static char *unescape_string(tess_parser *p, tess_token tok, size_t *out_len) {
    size_t content = tok.end - tok.start - 2;
    char *out = tess_arena_alloc(&p->program->arena, content + 1);
    if (out == NULL) {
        fail_oom(p);
        return NULL;
    }
    size_t n = 0;
    size_t i = tok.start + 1;
    size_t end = tok.end - 1;
    while (i < end) {
        unsigned char c = (unsigned char)p->src[i++];
        if (c != '\\') {
            out[n++] = (char)c;
            continue;
        }
        if (i >= end) {
            fail_at(p, "unterminated escape sequence", i - 1, i);
            return NULL;
        }
        unsigned char e = (unsigned char)p->src[i++];
        switch (e) {
            case '"': out[n++] = '"'; break;
            case '\'': out[n++] = '\''; break;
            case '\\': out[n++] = '\\'; break;
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = '\t'; break;
            case 'r': out[n++] = '\r'; break;
            case '0': out[n++] = '\0'; break;
            case 'x': {
                if (i + 2 > end || !is_hex((unsigned char)p->src[i]) ||
                    !is_hex((unsigned char)p->src[i + 1])) {
                    fail_at(p, "`\\x` needs two hex digits", i - 2, i);
                    return NULL;
                }
                out[n++] = (char)(hex_value((unsigned char)p->src[i]) * 16 +
                                  hex_value((unsigned char)p->src[i + 1]));
                i += 2;
                break;
            }
            default:
                fail_at(p, "unknown escape sequence", i - 2, i);
                return NULL;
        }
    }
    out[n] = '\0';
    *out_len = n;
    return out;
}

static bool parse_int_token(tess_parser *p, tess_token tok, int64_t *out) {
    size_t n = tok.end - tok.start;
    char buf[64];
    if (n >= sizeof buf) {
        return fail_at(p, "integer literal is too long", tok.start, tok.end);
    }
    memcpy(buf, p->src + tok.start, n);
    buf[n] = '\0';
    errno = 0;
    char *end = NULL;
    /* The lexer keeps hex and binary literals as single tokens. strtoll
     * accepts the 0x prefix for base 16, but not the 0b prefix, so skip that
     * prefix explicitly. */
    int base = 10;
    if (n >= 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        base = 16;
    } else if (n >= 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        base = 2;
        memmove(buf, buf + 2, n - 1);
        n -= 2;
    }
    long long value = strtoll(buf, &end, base);
    if (errno == ERANGE || end == buf || *end != '\0') {
        return fail_at(p, "invalid integer literal", tok.start, tok.end);
    }
    *out = (int64_t)value;
    return true;
}

static bool parse_float_token(tess_parser *p, tess_token tok, double *out) {
    size_t n = tok.end - tok.start;
    char buf[64];
    if (n >= sizeof buf) {
        return fail_at(p, "float literal is too long", tok.start, tok.end);
    }
    memcpy(buf, p->src + tok.start, n);
    buf[n] = '\0';
    errno = 0;
    char *end = NULL;
    double value = strtod(buf, &end);
    if (errno == ERANGE || end == buf || *end != '\0') {
        return fail_at(p, "invalid float literal", tok.start, tok.end);
    }
    *out = value;
    return true;
}

static tess_expr *parse_expr(tess_parser *p);
static tess_expr *parse_expr_impl(tess_parser *p);

static tess_expr **parse_args(tess_parser *p, size_t *out_count,
                              uint32_t *out_end) {
    advance(p);
    tess_vec args = {0};
    if (p->cur.kind != TESS_TOK_RPAREN) {
        for (;;) {
            tess_expr *arg = parse_expr(p);
            if (arg == NULL) {
                return NULL;
            }
            if (!vec_push(p, &args, sizeof arg, &arg)) {
                return NULL;
            }
            if (p->cur.kind == TESS_TOK_COMMA) {
                advance(p);
                if (p->cur.kind == TESS_TOK_RPAREN) {
                    fail_at(p, "expected a parameter after `,`",
                            p->cur.start, p->cur.end);
                    return NULL;
                }
                continue;
            }
            break;
        }
    }
    *out_end = (uint32_t)p->cur.end;
    if (!expect(p, TESS_TOK_RPAREN, "`)` to close the argument list")) {
        return NULL;
    }
    *out_count = args.count;
    return args.items;
}

static tess_expr *parse_primary(tess_parser *p) {
    tess_token tok = p->cur;
    switch (tok.kind) {
        case TESS_TOK_IDENT: {
            advance(p);
            char *name = copy_ident(p, tok);
            if (name == NULL) {
                return NULL;
            }
            tess_expr *e = new_expr(p, TESS_EXPR_IDENT, (uint32_t)tok.start,
                                    (uint32_t)tok.end);
            if (e == NULL) {
                return NULL;
            }
            e->as.ident = name;
            if (p->cur.kind == TESS_TOK_LBRACE) {
                advance(p);
                tess_vec names = {0}, values = {0};
                while (p->cur.kind != TESS_TOK_RBRACE && p->cur.kind != TESS_TOK_EOF) {
                    if (p->cur.kind != TESS_TOK_IDENT) {
                        fail_at(p, "expected a record field name", p->cur.start, p->cur.end);
                        return NULL;
                    }
                    char *field = copy_ident(p, p->cur);
                    advance(p);
                    if (!expect(p, TESS_TOK_COLON, "`:` after record field name")) return NULL;
                    tess_expr *value = parse_expr(p);
                    if (!value || !vec_push(p, &names, sizeof field, &field) ||
                        !vec_push(p, &values, sizeof value, &value)) return NULL;
                    if (p->cur.kind != TESS_TOK_COMMA) break;
                    advance(p);
                }
                uint32_t end = (uint32_t)p->cur.end;
                if (!expect(p, TESS_TOK_RBRACE, "`}` after record initializer")) return NULL;
                tess_expr *init = new_expr(p, TESS_EXPR_RECORD_INIT, e->start, end);
                if (init == NULL) return NULL;
                init->as.record_init.type_name = name;
                init->as.record_init.names = names.items;
                init->as.record_init.values = values.items;
                init->as.record_init.count = names.count;
                return init;
            }
            return e;
        }
        case TESS_TOK_INT: {
            int64_t value = 0;
            if (!parse_int_token(p, tok, &value)) {
                return NULL;
            }
            advance(p);
            tess_expr *e = new_expr(p, TESS_EXPR_INT, (uint32_t)tok.start,
                                    (uint32_t)tok.end);
            if (e == NULL) {
                return NULL;
            }
            e->as.int_value = value;
            return e;
        }
        case TESS_TOK_FLOAT: {
            double value = 0;
            if (!parse_float_token(p, tok, &value)) {
                return NULL;
            }
            advance(p);
            tess_expr *e = new_expr(p, TESS_EXPR_FLOAT, (uint32_t)tok.start,
                                    (uint32_t)tok.end);
            if (e == NULL) {
                return NULL;
            }
            e->as.float_value = value;
            return e;
        }
        case TESS_TOK_STRING: {
            size_t len = 0;
            char *data = unescape_string(p, tok, &len);
            if (data == NULL) {
                return NULL;
            }
            advance(p);
            tess_expr *e = new_expr(p, TESS_EXPR_STRING, (uint32_t)tok.start,
                                    (uint32_t)tok.end);
            if (e == NULL) {
                return NULL;
            }
            e->as.string.data = data;
            e->as.string.len = len;
            return e;
        }
        case TESS_TOK_KW_TRUE:
        case TESS_TOK_KW_FALSE: {
            advance(p);
            tess_expr *e = new_expr(p, TESS_EXPR_BOOL, (uint32_t)tok.start,
                                    (uint32_t)tok.end);
            if (e == NULL) {
                return NULL;
            }
            e->as.bool_value = tok.kind == TESS_TOK_KW_TRUE;
            return e;
        }
        case TESS_TOK_KW_NULL: {
            advance(p);
            return new_expr(p, TESS_EXPR_NULL, (uint32_t)tok.start,
                            (uint32_t)tok.end);
        }
        case TESS_TOK_AT: {
            advance(p);
            if (p->cur.kind != TESS_TOK_IDENT) {
                fail_at(p, "expected a module name after `@`", p->cur.start,
                        p->cur.end);
                return NULL;
            }
            char *name = copy_ident(p, p->cur);
            if (name == NULL) {
                return NULL;
            }
            uint32_t end = (uint32_t)p->cur.end;
            advance(p);
            tess_expr *e = new_expr(p, TESS_EXPR_MODULE, (uint32_t)tok.start,
                                    end);
            if (e == NULL) {
                return NULL;
            }
            e->as.module = name;
            return e;
        }
        case TESS_TOK_LPAREN: {
            uint32_t start = (uint32_t)tok.start;
            advance(p);
            tess_expr *e = parse_expr(p);
            if (e == NULL) {
                return NULL;
            }
            uint32_t end = (uint32_t)p->cur.end;
            if (!expect(p, TESS_TOK_RPAREN, "`)` to close the group")) {
                return NULL;
            }
            e->start = start;
            e->end = end;
            return e;
        }
        case TESS_TOK_LBRACKET: {
            tess_vec items = {0};
            uint32_t start = (uint32_t)tok.start;
            advance(p);
            while (p->cur.kind != TESS_TOK_RBRACKET && p->cur.kind != TESS_TOK_EOF) {
                tess_expr *item = parse_expr(p);
                if (item == NULL || !vec_push(p, &items, sizeof item, &item)) return NULL;
                if (p->cur.kind != TESS_TOK_COMMA) break;
                advance(p);
            }
            uint32_t end = (uint32_t)p->cur.end;
            if (!expect(p, TESS_TOK_RBRACKET, "`]` after array literal")) return NULL;
            if (items.count == 0) {
                fail_at(p, "array literal cannot be empty", start, end);
                return NULL;
            }
            tess_expr *e = new_expr(p, TESS_EXPR_ARRAY, start, end);
            if (e == NULL) return NULL;
            e->as.array.items = items.items;
            e->as.array.count = items.count;
            return e;
        }
        default:
            fail_at(p, "expected an expression", tok.start, tok.end);
            return NULL;
    }
}

static tess_expr *parse_postfix(tess_parser *p) {
    tess_expr *e = parse_primary(p);
    if (e == NULL) {
        return NULL;
    }
    for (;;) {
        if (p->cur.kind == TESS_TOK_DOT) {
            advance(p);
            if (p->cur.kind != TESS_TOK_IDENT) {
                fail_at(p, "expected a member name after `.`", p->cur.start,
                        p->cur.end);
                return NULL;
            }
            tess_token name_tok = p->cur;
            advance(p);
            char *name = copy_ident(p, name_tok);
            if (name == NULL) {
                return NULL;
            }
            tess_expr *member =
                new_expr(p, TESS_EXPR_MEMBER, e->start, (uint32_t)name_tok.end);
            if (member == NULL) {
                return NULL;
            }
            member->as.member.object = e;
            member->as.member.name = name;
            e = member;
        } else if (p->cur.kind == TESS_TOK_LPAREN) {
            size_t argc = 0;
            uint32_t end = 0;
            tess_expr **args = parse_args(p, &argc, &end);
            if (args == NULL && p->failed) {
                return NULL;
            }
            tess_expr *call = new_expr(p, TESS_EXPR_CALL, e->start, end);
            if (call == NULL) {
                return NULL;
            }
            call->as.call.callee = e;
            call->as.call.args = args;
            call->as.call.arg_count = argc;
            e = call;
        } else if (p->cur.kind == TESS_TOK_LBRACKET) {
            advance(p);
            tess_expr *first = NULL;
            if (p->cur.kind != TESS_TOK_COLON) first = parse_expr(p);
            if (p->cur.kind == TESS_TOK_COLON) {
                advance(p);
                tess_expr *last = p->cur.kind == TESS_TOK_RBRACKET ? NULL : parse_expr(p);
                if (!expect(p, TESS_TOK_RBRACKET, "`]` after slice")) return NULL;
                tess_expr *sub = new_expr(p, TESS_EXPR_SLICE, e->start, (uint32_t)p->cur.end);
                if (sub == NULL) return NULL;
                sub->as.slice.object = e; sub->as.slice.start = first; sub->as.slice.end = last;
                e = sub;
                continue;
            }
            if (first == NULL || !expect(p, TESS_TOK_RBRACKET, "`]` after the index")) return NULL;
            tess_expr *sub = new_expr(p, TESS_EXPR_INDEX, e->start, first->end);
            if (sub == NULL) return NULL;
            sub->as.index.object = e; sub->as.index.index = first;
            e = sub;
        } else {
            break;
        }
    }
    return e;
}

static tess_expr *parse_unary_impl(tess_parser *p);

static tess_expr *parse_unary(tess_parser *p) {
    if (p->depth >= TESS_MAX_DEPTH) {
        fail_at(p, "maximum expression nesting depth exceeded", p->cur.start,
               p->cur.end);
        return NULL;
    }
    p->depth++;
    tess_expr *e = parse_unary_impl(p);
    p->depth--;
    return e;
}

static tess_expr *parse_unary_impl(tess_parser *p) {
    tess_unary_op op;
    switch (p->cur.kind) {
        case TESS_TOK_BANG: op = TESS_UN_NOT; break;
        case TESS_TOK_MINUS: op = TESS_UN_NEG; break;
        case TESS_TOK_PLUS: op = TESS_UN_POS; break;
        case TESS_TOK_TILDE: op = TESS_UN_BIT_NOT; break;
        case TESS_TOK_AMP: op = TESS_UN_ADDRESS; break;
        case TESS_TOK_STAR: op = TESS_UN_DEREFERENCE; break;
        default: return parse_postfix(p);
    }
    uint32_t start = (uint32_t)p->cur.start;
    advance(p);
    /* Recurse through the guarded wrapper: a chain of unary operators must be
     * depth-checked just like parenthesized nesting. */
    tess_expr *operand = parse_unary(p);
    if (operand == NULL) {
        return NULL;
    }
    tess_expr *e = new_expr(p, TESS_EXPR_UNARY, start, operand->end);
    if (e == NULL) {
        return NULL;
    }
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

static int binary_prec(tess_token_kind kind) {
    switch (kind) {
        case TESS_TOK_OROR: return 1;
        case TESS_TOK_ANDAND: return 2;
        case TESS_TOK_PIPE: return 3;
        case TESS_TOK_CARET: return 4;
        case TESS_TOK_AMP: return 5;
        case TESS_TOK_EQEQ:
        case TESS_TOK_NEQ: return 6;
        case TESS_TOK_LT:
        case TESS_TOK_LE:
        case TESS_TOK_GT:
        case TESS_TOK_GE: return 7;
        case TESS_TOK_SHL:
        case TESS_TOK_SHR: return 8;
        case TESS_TOK_PLUS:
        case TESS_TOK_MINUS: return 9;
        case TESS_TOK_STAR:
        case TESS_TOK_SLASH:
        case TESS_TOK_PERCENT: return 10;
        default: return 0;
    }
}

static tess_binary_op binary_op(tess_token_kind kind) {
    switch (kind) {
        case TESS_TOK_OROR: return TESS_BIN_OR;
        case TESS_TOK_ANDAND: return TESS_BIN_AND;
        case TESS_TOK_PIPE: return TESS_BIN_BIT_OR;
        case TESS_TOK_CARET: return TESS_BIN_BIT_XOR;
        case TESS_TOK_AMP: return TESS_BIN_BIT_AND;
        case TESS_TOK_EQEQ: return TESS_BIN_EQ;
        case TESS_TOK_NEQ: return TESS_BIN_NE;
        case TESS_TOK_LT: return TESS_BIN_LT;
        case TESS_TOK_LE: return TESS_BIN_LE;
        case TESS_TOK_GT: return TESS_BIN_GT;
        case TESS_TOK_GE: return TESS_BIN_GE;
        case TESS_TOK_PLUS: return TESS_BIN_ADD;
        case TESS_TOK_MINUS: return TESS_BIN_SUB;
        case TESS_TOK_STAR: return TESS_BIN_MUL;
        case TESS_TOK_SLASH: return TESS_BIN_DIV;
        case TESS_TOK_PERCENT: return TESS_BIN_MOD;
        case TESS_TOK_SHL: return TESS_BIN_SHL;
        case TESS_TOK_SHR: return TESS_BIN_SHR;
        default: return TESS_BIN_ADD;
    }
}

static tess_expr *parse_binary(tess_parser *p, int min_prec) {
    tess_expr *lhs = parse_unary(p);
    if (lhs == NULL) {
        return NULL;
    }
    for (;;) {
        int prec = binary_prec(p->cur.kind);
        if (prec == 0 || prec < min_prec) {
            break;
        }
        tess_token_kind kind = p->cur.kind;
        uint32_t start = lhs->start;
        advance(p);
        tess_expr *rhs = parse_binary(p, prec + 1);
        if (rhs == NULL) {
            return NULL;
        }
        tess_expr *e = new_expr(p, TESS_EXPR_BINARY, start, rhs->end);
        if (e == NULL) {
            return NULL;
        }
        e->as.binary.op = binary_op(kind);
        e->as.binary.lhs = lhs;
        e->as.binary.rhs = rhs;
        lhs = e;
    }
    return lhs;
}

/* Parenthesized expressions, nested calls, and ternary branches all recurse
 * through this entry point. Bound the complete expression tree before it can
 * exhaust the C stack in parsing or later recursive AST passes. */
static tess_expr *parse_expr(tess_parser *p) {
    if (p->depth >= TESS_MAX_DEPTH) {
        fail_at(p, "maximum expression nesting depth exceeded", p->cur.start,
                p->cur.end);
        return NULL;
    }
    p->depth++;
    tess_expr *e = parse_expr_impl(p);
    p->depth--;
    return e;
}

static tess_expr *parse_expr_impl(tess_parser *p) {
    tess_expr *cond = parse_binary(p, 1);
    if (cond == NULL) {
        return NULL;
    }
    if (p->cur.kind != TESS_TOK_QUESTION) {
        return cond;
    }
    advance(p);
    /* Recursive branches are guarded by parse_expr. */
    tess_expr *then_expr = parse_expr(p);
    if (then_expr == NULL) {
        return NULL;
    }
    if (!expect(p, TESS_TOK_COLON, "`:` in a conditional expression")) {
        return NULL;
    }
    tess_expr *else_expr = parse_expr(p);
    if (else_expr == NULL) {
        return NULL;
    }
    tess_expr *e = new_expr(p, TESS_EXPR_TERNARY, cond->start, else_expr->end);
    if (e == NULL) {
        return NULL;
    }
    e->as.ternary.cond = cond;
    e->as.ternary.then_expr = then_expr;
    e->as.ternary.else_expr = else_expr;
    return e;
}

static tess_stmt *parse_block_body(tess_parser *p) {
    uint32_t start = (uint32_t)p->cur.start;
    if (!expect(p, TESS_TOK_LBRACE, "`{` to open a block")) return NULL;
    tess_vec items = {0};
    while (!p->failed && p->cur.kind != TESS_TOK_RBRACE &&
           p->cur.kind != TESS_TOK_EOF) {
        tess_stmt *stmt = parse_statement(p);
        if (stmt == NULL) {
            return NULL;
        }
        if (stmt->kind != TESS_STMT_EMPTY) {
            if (!vec_push(p, &items, sizeof stmt, &stmt)) {
                return NULL;
            }
        }
    }
    uint32_t end = (uint32_t)p->cur.end;
    if (!expect(p, TESS_TOK_RBRACE, "`}` to close the block")) {
        return NULL;
    }
    tess_stmt *block = new_stmt(p, TESS_STMT_BLOCK, start, end);
    if (block == NULL) {
        return NULL;
    }
    block->as.block.items = items.items;
    block->as.block.count = items.count;
    return block;
}

static tess_stmt *parse_statement_impl(tess_parser *p) {
    if (p->cur.kind == TESS_TOK_KW_ASM) {
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        bool is_volatile = false;
        if (p->cur.kind == TESS_TOK_KW_VOLATILE) { is_volatile = true; advance(p); }
        if (!expect(p, TESS_TOK_LPAREN, "`(` after asm")) return NULL;
        if (p->cur.kind != TESS_TOK_STRING) {
            fail_at(p, "asm currently requires one string template", p->cur.start, p->cur.end);
            return NULL;
        }
        size_t n = 0;
        char *text = unescape_string(p, p->cur, &n);
        if (text == NULL) return NULL;
        uint32_t end = (uint32_t)p->cur.end;
        advance(p);
        if (!expect(p, TESS_TOK_RPAREN, "`)` after asm template") ||
            !expect(p, TESS_TOK_SEMI, "`;` after asm")) return NULL;
        tess_stmt *stmt = new_stmt(p, TESS_STMT_ASM, start, end);
        if (stmt != NULL) { stmt->as.assembly.text = text; stmt->as.assembly.is_volatile = is_volatile; }
        return stmt;
    }
    if (p->cur.kind == TESS_TOK_SEMI) {
        uint32_t start = (uint32_t)p->cur.start;
        uint32_t end = (uint32_t)p->cur.end;
        advance(p);
        return new_stmt(p, TESS_STMT_EMPTY, start, end);
    }
    if (p->cur.kind == TESS_TOK_LBRACE) {
        return parse_block_body(p);
    }
    if (p->cur.kind == TESS_TOK_KW_LINK) {
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        if (p->cur.kind != TESS_TOK_STRING) {
            fail_at(p, "expected a library name after `link`",
                    p->cur.start, p->cur.end);
            return NULL;
        }
        size_t len = 0;
        char *library = unescape_string(p, p->cur, &len);
        if (library == NULL) return NULL;
        if (strlen(library) != len) {
            fail_at(p, "library name cannot contain a NUL byte",
                    p->cur.start, p->cur.end);
            return NULL;
        }
        advance(p);
        uint32_t end = (uint32_t)p->cur.end;
        if (p->cur.kind == TESS_TOK_SEMI) {
            end = (uint32_t)p->cur.end;
            advance(p);
        }
        tess_stmt *stmt = new_stmt(p, TESS_STMT_LINK, start, end);
        if (stmt == NULL) return NULL;
        stmt->as.link.library = library;
        return stmt;
    }
    if (p->cur.kind == TESS_TOK_KW_IF) {
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        if (!expect(p, TESS_TOK_LPAREN, "`(` after `if`")) {
            return NULL;
        }
        tess_expr *cond = parse_expr(p);
        if (cond == NULL) {
            return NULL;
        }
        if (!expect(p, TESS_TOK_RPAREN, "`)` after the condition")) {
            return NULL;
        }
        tess_stmt *then_branch = parse_statement(p);
        if (then_branch == NULL) {
            return NULL;
        }
        tess_stmt *else_branch = NULL;
        if (p->cur.kind == TESS_TOK_KW_ELSE) {
            advance(p);
            else_branch = parse_statement(p);
            if (else_branch == NULL) {
                return NULL;
            }
        }
        uint32_t end = else_branch ? else_branch->end : then_branch->end;
        tess_stmt *stmt = new_stmt(p, TESS_STMT_IF, start, end);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->as.if_stmt.cond = cond;
        stmt->as.if_stmt.then_branch = then_branch;
        stmt->as.if_stmt.else_branch = else_branch;
        return stmt;
    }
    if (p->cur.kind == TESS_TOK_KW_WHILE) {
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        if (!expect(p, TESS_TOK_LPAREN, "`(` after `while`")) {
            return NULL;
        }
        tess_expr *cond = parse_expr(p);
        if (cond == NULL) {
            return NULL;
        }
        if (!expect(p, TESS_TOK_RPAREN, "`)` after the condition")) {
            return NULL;
        }
        tess_stmt *body = parse_statement(p);
        if (body == NULL) {
            return NULL;
        }
        tess_stmt *stmt = new_stmt(p, TESS_STMT_WHILE, start, body->end);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->as.while_stmt.cond = cond;
        stmt->as.while_stmt.body = body;
        return stmt;
    }
    if (p->cur.kind == TESS_TOK_KW_FOR) {
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        if (!expect(p, TESS_TOK_LPAREN, "`(` after `for`")) return NULL;

        /* The initializer reuses normal declaration/assignment syntax and
         * consumes its terminating semicolon. An empty one is legal. */
        tess_stmt *init = parse_statement(p);
        if (init == NULL) return NULL;

        tess_expr *condition = NULL;
        if (p->cur.kind != TESS_TOK_SEMI) {
            condition = parse_expr(p);
            if (condition == NULL) return NULL;
        }
        if (!expect(p, TESS_TOK_SEMI, "`;` after the for condition"))
            return NULL;

        tess_expr *step = NULL;
        if (p->cur.kind != TESS_TOK_RPAREN) {
            tess_expr *target = parse_expr(p);
            if (target == NULL) return NULL;
            if (!expect(p, TESS_TOK_EQ, "`=` in the for step")) return NULL;
            tess_expr *value = parse_expr(p);
            if (value == NULL) return NULL;
            step = new_expr(p, TESS_EXPR_ASSIGN, target->start, value->end);
            if (step == NULL) return NULL;
            step->as.assign.target = target;
            step->as.assign.value = value;
        }
        if (!expect(p, TESS_TOK_RPAREN, "`)` after the for clauses"))
            return NULL;
        tess_stmt *body = parse_statement(p);
        if (body == NULL) return NULL;

        tess_stmt *stmt = new_stmt(p, TESS_STMT_FOR, start, body->end);
        if (stmt == NULL) return NULL;
        stmt->as.for_stmt.init = init;
        stmt->as.for_stmt.condition = condition;
        stmt->as.for_stmt.step = step;
        stmt->as.for_stmt.body = body;
        return stmt;
    }
    if (p->cur.kind == TESS_TOK_KW_RETURN) {
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        tess_expr *value = NULL;
        if (p->cur.kind != TESS_TOK_SEMI) {
            value = parse_expr(p);
            if (value == NULL) {
                return NULL;
            }
        }
        uint32_t end = value != NULL ? value->end : (uint32_t)p->cur.start;
        if (!expect(p, TESS_TOK_SEMI, "`;` after `return`")) {
            return NULL;
        }
        tess_stmt *stmt = new_stmt(p, TESS_STMT_RETURN, start, end);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->as.ret = value;
        return stmt;
    }
    if (p->cur.kind == TESS_TOK_KW_BREAK ||
        p->cur.kind == TESS_TOK_KW_CONTINUE) {
        bool is_break = p->cur.kind == TESS_TOK_KW_BREAK;
        uint32_t start = (uint32_t)p->cur.start;
        advance(p);
        uint32_t end = (uint32_t)p->cur.end;
        if (!expect(p, TESS_TOK_SEMI, "`;` after the loop control statement"))
            return NULL;
        tess_stmt *stmt = new_stmt(p, is_break ? TESS_STMT_BREAK
                                              : TESS_STMT_CONTINUE,
                                  start, end);
        return stmt;
    }

    bool declaration = p->cur.kind == TESS_TOK_KW_LET;
    if (declaration) advance(p);
    tess_expr *first = parse_expr(p);
    if (first == NULL) {
        return NULL;
    }
    if (declaration && first->kind != TESS_EXPR_IDENT) {
        fail_at(p, "`let` must introduce a variable name", first->start,
                first->end);
        return NULL;
    }
    char *declared_type = NULL;
    if (declaration && p->cur.kind == TESS_TOK_COLON) {
        advance(p);
        declared_type = parse_type_name(p, "expected a type after `:`");
        if (declared_type == NULL) return NULL;
    }
    if (p->cur.kind == TESS_TOK_EQ) {
        if (first->kind != TESS_EXPR_IDENT && first->kind != TESS_EXPR_MEMBER && first->kind != TESS_EXPR_INDEX && !(first->kind == TESS_EXPR_UNARY && first->as.unary.op == TESS_UN_DEREFERENCE)) {
            fail_at(p, "invalid assignment target", first->start, first->end);
            return NULL;
        }
        advance(p);
        tess_expr *value = parse_expr(p);
        if (value == NULL) {
            return NULL;
        }
        if (!expect(p, TESS_TOK_SEMI, "`;` after an assignment")) {
            return NULL;
        }
        tess_stmt *stmt = new_stmt(p, TESS_STMT_ASSIGN, first->start, value->end);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->as.assign.target = first;
        stmt->as.assign.value = value;
        stmt->as.assign.declaration = declaration;
        stmt->as.assign.declared_type = declared_type;
        return stmt;
    }
    if (declaration) {
        fail_at(p, "expected `=` after the variable name", p->cur.start,
                p->cur.end);
        return NULL;
    }
    if (!expect(p, TESS_TOK_SEMI, "`;` after an expression")) {
        return NULL;
    }
    tess_stmt *stmt = new_stmt(p, TESS_STMT_EXPR, first->start, first->end);
    if (stmt == NULL) {
        return NULL;
    }
    stmt->as.expr = first;
    return stmt;
}

static bool parse_field(tess_parser *p, tess_vec *fields) {
    tess_token name_tok = p->cur;
    advance(p);
    if (!expect(p, TESS_TOK_COLON, "`:` after a field name")) {
        return false;
    }
    char *type_name = parse_type_name(p, "expected a type name");
    if (type_name == NULL) return false;
    tess_field field = {0};
    field.name = copy_ident(p, name_tok);
    if (field.name == NULL) {
        return false;
    }
    field.type = type_name;
    field.start = (uint32_t)name_tok.start;
    field.end = (uint32_t)p->cur.start;
    if (!expect(p, TESS_TOK_SEMI, "`;` after a field declaration")) {
        return false;
    }
    return vec_push(p, fields, sizeof field, &field);
}

static bool parse_method(tess_parser *p, tess_vec *methods) {
    tess_token name_tok = p->cur;
    advance(p);
    tess_vec params = {0};
    if (p->cur.kind == TESS_TOK_LPAREN) {
        advance(p);
        if (p->cur.kind != TESS_TOK_RPAREN) {
            for (;;) {
                if (p->cur.kind != TESS_TOK_IDENT) {
                    fail_at(p, "expected a parameter name", p->cur.start,
                            p->cur.end);
                    return false;
                }
                tess_token param_name = p->cur;
                advance(p);
                if (!expect(p, TESS_TOK_COLON,
                            "`:` after a parameter name")) {
                    return false;
                }
                char *param_type = parse_type_name(p, "expected a type name");
                if (param_type == NULL) return false;
                tess_param param = {0};
                param.name = copy_ident(p, param_name);
                if (param.name == NULL) {
                    return false;
                }
                param.type = param_type;
                if (!vec_push(p, &params, sizeof param, &param)) {
                    return false;
                }
                if (p->cur.kind == TESS_TOK_COMMA) {
                    advance(p);
                    continue;
                }
                break;
            }
        }
        if (!expect(p, TESS_TOK_RPAREN, "`)` after the parameters")) {
            return false;
        }
    }
    char *ret_type = NULL;
    if (p->cur.kind == TESS_TOK_COLON) {
        advance(p);
        ret_type = parse_type_name(p, "expected a return type after `:`");
        if (ret_type == NULL) return false;
    }

    tess_stmt *body = parse_block_body(p);
    if (body == NULL) {
        return false;
    }
    tess_method method = {0};
    method.name = copy_ident(p, name_tok);
    if (method.name == NULL) {
        return false;
    }
    method.ret_type = ret_type;
    method.params = params.items;
    method.param_count = params.count;
    method.body = body;
    method.start = (uint32_t)name_tok.start;
    method.end = body->end;
    return vec_push(p, methods, sizeof method, &method);
}

static tess_stmt *parse_class(tess_parser *p) {
    tess_token name_tok = p->cur;
    advance(p);
    
    /* At file scope, IDENT followed by `{` is the class declaration form;
     * ordinary blocks begin with `{` directly. */
    if (!expect(p, TESS_TOK_LBRACE, "`{` after a class name")) {
        return NULL;
    }
    
    uint32_t start = (uint32_t)p->cur.start;
    tess_vec fields = {0};
    tess_vec methods = {0};
    
    // Parse class fields and methods
    while (!p->failed && p->cur.kind != TESS_TOK_RBRACE &&
           p->cur.kind != TESS_TOK_EOF) {
        if (p->cur.kind == TESS_TOK_SEMI) {
            advance(p);
            continue;
        }
        if (p->cur.kind != TESS_TOK_IDENT) {
            fail_at(p, "expected a field or method declaration", p->cur.start,
                    p->cur.end);
            return NULL;
        }
        
        // Check next token
        if (p->next.kind == TESS_TOK_COLON &&
            (peek_kind_ahead(p, 2) == TESS_TOK_LBRACE ||
             (peek_kind_ahead(p, 2) == TESS_TOK_KW_VOLATILE &&
              peek_kind_ahead(p, 3) == TESS_TOK_LBRACE))) {
            // `name: Type { ... }` is a method with a return type; a field
            // would be terminated by `;` right after the type.
            if (!parse_method(p, &methods)) {
                return NULL;
            }
        } else if (p->next.kind == TESS_TOK_COLON) {
            // This is a field
            if (!parse_field(p, &fields)) {
                return NULL;
            }
        } else if (p->next.kind == TESS_TOK_LPAREN ||
                   p->next.kind == TESS_TOK_LBRACE) {
            // This is a method
            if (!parse_method(p, &methods)) {
                return NULL;
            }
        } else {
            fail_at(p, "expected `:` or a method body after the member name",
                    p->cur.start, p->next.end);
            return NULL;
        }
    }
    
    uint32_t end = (uint32_t)p->cur.end;
    if (!expect(p, TESS_TOK_RBRACE, "`}` to close the class")) {
        return NULL;
    }
    
    // Allocate the class structure
    tess_class *cls = tess_arena_alloc(&p->program->arena, sizeof *cls);
    if (cls == NULL) {
        return NULL;
    }
    
    cls->name = copy_ident(p, name_tok);
    if (cls->name == NULL) {
        return NULL;
    }
    
    cls->fields = fields.items;
    cls->field_count = fields.count;
    cls->methods = methods.items;
    cls->method_count = methods.count;
    cls->start = (uint32_t)name_tok.start;
    cls->end = end;
    
    tess_stmt *stmt = new_stmt(p, TESS_STMT_CLASS, start, end);
    if (stmt == NULL) {
        return NULL;
    }
    stmt->as.class_decl = cls;
    return stmt;
}

static tess_stmt *parse_record(tess_parser *p) {
    bool is_union = p->cur.kind == TESS_TOK_KW_UNION;
    uint32_t start = (uint32_t)p->cur.start;
    advance(p);
    if (p->cur.kind != TESS_TOK_IDENT) {
        fail_at(p, "expected a record name", p->cur.start, p->cur.end);
        return NULL;
    }
    char *name = copy_ident(p, p->cur);
    advance(p);
    if (!expect(p, TESS_TOK_LBRACE, "`{` after record name")) return NULL;
    tess_vec fields = {0};
    while (!p->failed && p->cur.kind != TESS_TOK_RBRACE && p->cur.kind != TESS_TOK_EOF) {
        if (p->cur.kind == TESS_TOK_SEMI) { advance(p); continue; }
        if (p->cur.kind != TESS_TOK_IDENT || !parse_field(p, &fields)) return NULL;
    }
    uint32_t end = (uint32_t)p->cur.end;
    if (!expect(p, TESS_TOK_RBRACE, "`}` after record fields")) return NULL;
    tess_record *record = tess_arena_alloc(&p->program->arena, sizeof *record);
    if (!record) { fail_oom(p); return NULL; }
    record->name = name; record->fields = fields.items;
    record->field_count = fields.count; record->is_union = is_union;
    tess_stmt *stmt = new_stmt(p, TESS_STMT_RECORD, start, end);
    if (stmt) stmt->as.record_decl = record;
    return stmt;
}

static bool ident_is_word(tess_parser *p, const char *word) {
    size_t n = strlen(word);
    return p->cur.kind == TESS_TOK_IDENT &&
           (size_t)(p->cur.end - p->cur.start) == n &&
           memcmp(p->src + p->cur.start, word, n) == 0;
}

static tess_stmt *parse_import(tess_parser *p) {
    uint32_t start = (uint32_t)p->cur.start;
    uint32_t end = (uint32_t)p->cur.end;
    advance(p); /* `import` */

    tess_stmt *stmt = new_stmt(p, TESS_STMT_IMPORT, start, end);
    if (stmt == NULL) return NULL;
    stmt->as.import.module = NULL;
    stmt->as.import.path = NULL;
    stmt->as.import.alias = NULL;

    if (p->cur.kind == TESS_TOK_IDENT) {
        char *name = copy_ident(p, p->cur);
        if (name == NULL) return NULL;
        stmt->as.import.module = name;
        stmt->as.import.alias = name;
        advance(p);
        /* Native C modules may attach one system header. `import c
         * "stdio.h" as io` gives calls through `io` a C namespace while the
         * header supplies the declarations to the generated translation unit. */
        if (strcmp(name, "c") == 0 && p->cur.kind == TESS_TOK_STRING) {
            size_t header_len = 0;
            stmt->as.import.path = unescape_string(p, p->cur, &header_len);
            if (stmt->as.import.path == NULL) return NULL;
            if (strlen(stmt->as.import.path) != header_len) {
                fail_at(p, "C header name cannot contain a NUL byte",
                        p->cur.start, p->cur.end);
                return NULL;
            }
            advance(p);
        }
    } else if (p->cur.kind == TESS_TOK_STRING) {
        size_t len = 0;
        char *path = unescape_string(p, p->cur, &len);
        if (path == NULL) return NULL;
        if (strlen(path) != len) {
            fail_at(p, "import path cannot contain a NUL byte",
                    p->cur.start, p->cur.end);
            return NULL;
        }
        size_t n = len;
        while (n > 0 && (path[n - 1] == '.' || path[n - 1] == '/')) {
            n--;
        }
        size_t slash = 0;
        for (size_t i = 0; i < n; i++) {
            if (path[i] == '/') slash = i;
        }
        size_t base = slash < n ? slash + 1 : 0;
        size_t blen = n - base;
        bool c_header = blen > 2 && path[blen + base - 2] == '.' &&
                        path[blen + base - 1] == 'h';
        if (c_header) {
            stmt->as.import.module = tess_arena_strndup(
                &p->program->arena, "c", 1);
            if (stmt->as.import.module == NULL) return fail_oom(p), NULL;
            blen -= 2;
        }
        char *alias = tess_arena_alloc(&p->program->arena, blen + 1);
        if (alias == NULL) return NULL;
        memcpy(alias, path + base, blen);
        alias[blen] = '\0';
        stmt->as.import.path = path;
        stmt->as.import.alias = alias;
        advance(p);
    } else {
        fail_at(p, "expected module name or string path after `import`",
                p->cur.start, p->cur.end);
        return NULL;
    }

    if (ident_is_word(p, "as")) {
        advance(p);
        if (p->cur.kind != TESS_TOK_IDENT) {
            fail_at(p, "expected an alias after `as`", p->cur.start, p->cur.end);
            return NULL;
        }
        char *alias = copy_ident(p, p->cur);
        if (alias == NULL) return NULL;
        stmt->as.import.alias = alias;
        advance(p);
    }

    if (p->cur.kind == TESS_TOK_SEMI) advance(p);
    else if (p->cur.kind != TESS_TOK_EOF &&
             p->cur.kind != TESS_TOK_RBRACE) {
        fail_at(p, "expected `;` after import", p->cur.start, p->cur.end);
        return NULL;
    }

    return stmt;
}

static tess_stmt *parse_fn(tess_parser *p, bool external) {
    uint32_t start = (uint32_t)p->cur.start;
    uint32_t end = (uint32_t)p->cur.end;
    if (external) {
        advance(p); /* `extern` */
        if (p->cur.kind != TESS_TOK_KW_FN) {
            fail_at(p, "expected `fn` after `extern`", p->cur.start,
                    p->cur.end);
            return NULL;
        }
    }
    advance(p); /* `fn` */

    /* Function position in the source is its keyword, with the full span
     * recorded after the body is parsed below. */

    if (p->cur.kind != TESS_TOK_IDENT) {
        fail_at(p, "expected a function name after `fn`", p->cur.start, p->cur.end);
        return NULL;
    }
    tess_token name_tok = p->cur;
    char *name = copy_ident(p, name_tok);
    if (name == NULL) return NULL;
    advance(p);

    if (!expect(p, TESS_TOK_LPAREN, "`(` after the function name")) return NULL;

    tess_vec params = {0};
    bool variadic = false;
    if (p->cur.kind != TESS_TOK_RPAREN) {
        for (;;) {
            if (p->cur.kind == TESS_TOK_ELLIPSIS) {
                if (!external || params.count == 0) {
                    fail_at(p, "variadic parameters require an extern function and at least one fixed parameter",
                            p->cur.start, p->cur.end);
                    return NULL;
                }
                variadic = true;
                advance(p);
                if (p->cur.kind != TESS_TOK_RPAREN) {
                    fail_at(p, "`...` must be the final parameter",
                            p->cur.start, p->cur.end);
                    return NULL;
                }
                break;
            }
            if (p->cur.kind != TESS_TOK_IDENT) {
                fail_at(p, "expected a parameter name", p->cur.start, p->cur.end);
                return NULL;
            }
            tess_token param_name = p->cur;
            advance(p);
            if (!expect(p, TESS_TOK_COLON, "`:` after a parameter name")) return NULL;
            char *param_type = parse_type_name(p, "expected a parameter type");
            if (param_type == NULL) return NULL;
            tess_param param = {0};
            param.name = copy_ident(p, param_name);
            if (param.name == NULL) return NULL;
            param.type = param_type;
            if (!vec_push(p, &params, sizeof param, &param)) return NULL;
            if (p->cur.kind == TESS_TOK_COMMA) {
                advance(p);
                if (p->cur.kind == TESS_TOK_RPAREN) {
                    fail_at(p, "expected a parameter after `,`",
                            p->cur.start, p->cur.end);
                    return NULL;
                }
                continue;
            }
            break;
        }
    }
    if (!expect(p, TESS_TOK_RPAREN, "`)` after the parameters")) return NULL;

    char *ret_type = NULL;
    if (p->cur.kind == TESS_TOK_COLON) {
        advance(p);
        ret_type = parse_type_name(p, "expected a return type after `:`");
        if (ret_type == NULL) return NULL;
        /* C's explicit `void` return is the language's no-value return. */
        if (external && strcmp(ret_type, "void") == 0) ret_type = NULL;
    }

    tess_stmt *body = NULL;
    if (external) {
        end = (uint32_t)p->cur.end;
        if (!expect(p, TESS_TOK_SEMI, "`;` after an extern declaration"))
            return NULL;
    } else {
        body = parse_block_body(p);
        if (body == NULL) return NULL;
        end = body->end;
    }

    tess_stmt *stmt = new_stmt(p, TESS_STMT_FN, start, end);
    if (stmt == NULL) return NULL;
    stmt->as.fn.params = params.items;
    stmt->as.fn.param_count = params.count;
    stmt->as.fn.name = name;
    stmt->as.fn.ret_type = ret_type;
    stmt->as.fn.fn_body = body;
    stmt->as.fn.external = external;
    stmt->as.fn.variadic = variadic;
    return stmt;
}

static void report_error(const char *src, const char *path, uint32_t start,
                         uint32_t end, const char *message) {
    const char *name = path != NULL ? path : "<input>";
    core_located *at = core_locate(src, (core_span){start, end}, message);
    if (at == NULL) {
        fprintf(stderr, "%s: error: %s\n", name, message);
        return;
    }
    char *block = core_format_block(name, message, at);
    if (block == NULL) {
        fprintf(stderr, "%s: error: %s\n", name, message);
        core_located_free(at);
        return;
    }
    fputs(block, stderr);
    size_t n = strlen(block);
    if (n == 0 || block[n - 1] != '\n') {
        fputc('\n', stderr);
    }
    core_free(block);
    core_located_free(at);
}

tess_program *tess_parse(const char *src, size_t len, const char *path) {
    if (src == NULL && len != 0) {
        return NULL;
    }
    tess_program *program = core_alloc(sizeof *program);
    if (program == NULL) {
        return NULL;
    }
    memset(program, 0, sizeof *program);

    if (len > UINT32_MAX || len == SIZE_MAX) {
        core_free(program);
        return NULL;
    }
    char *source = core_alloc(len + 1);
    if (source == NULL) {
        core_free(program);
        return NULL;
    }
    if (len != 0) {
        memcpy(source, src, len);
    }
    source[len] = '\0';

    tess_parser p = {0};
    p.src = source;
    p.len = len;
    p.path = path;
    p.program = program;
    p.cur.kind = TESS_TOK_EOF;
    p.next.kind = TESS_TOK_EOF;
    tess_lexer_init(&p.lx, source, len);
    lex_into(&p, &p.cur);
    if (!p.failed) {
        lex_into(&p, &p.next);
    }

    tess_vec items = {0};
    /* Top-level dispatch is explicit because class declarations, imports and
     * functions have dedicated syntax that is not a regular statement. */
    while (!p.failed && p.cur.kind != TESS_TOK_EOF) {
        tess_stmt *stmt = NULL;
        if (p.cur.kind == TESS_TOK_KW_IMPORT) {
            stmt = parse_import(&p);
        } else if (p.cur.kind == TESS_TOK_KW_FN) {
            stmt = parse_fn(&p, false);
        } else if (p.cur.kind == TESS_TOK_KW_EXTERN) {
            stmt = parse_fn(&p, true);
        } else if (p.cur.kind == TESS_TOK_KW_STRUCT || p.cur.kind == TESS_TOK_KW_UNION) {
            stmt = parse_record(&p);
        } else {
            bool is_class = p.cur.kind == TESS_TOK_IDENT &&
                            p.next.kind == TESS_TOK_LBRACE;
            stmt = is_class ? parse_class(&p) : parse_statement(&p);
        }
        if (stmt == NULL) {
            break;
        }
        if (stmt->kind != TESS_STMT_EMPTY) {
            if (!vec_push(&p, &items, sizeof stmt, &stmt)) {
                break;
            }
        }
    }

    if (p.failed) {
        report_error(source, path, p.err_start, p.err_end, p.message);
        tess_program_free(program);
        core_free(source);
        return NULL;
    }

    program->items = items.items;
    program->count = items.count;
    core_free(source);
    return program;
}
