#include "core_diagnostics.h"

#include "core_alloc.h"

#include <string.h>

#define CORE_TAB_WIDTH 4u
#define CORE_WINDOW 100u

/* ------------------------------------------------------------------------- */
/* UTF-8                                                                      */
/* ------------------------------------------------------------------------- */

static size_t seq_len(unsigned char b) {
    if (b < 0x80u) return 1;
    if (b >= 0xC2u && b <= 0xDFu) return 2;
    if (b >= 0xE0u && b <= 0xEFu) return 3;
    if (b >= 0xF0u && b <= 0xF4u) return 4;
    return 0;
}

static bool is_cont(unsigned char b) {
    return (b & 0xC0u) == 0x80u;
}

/*
 * Length in bytes of the character starting at `s[i]`. Invalid input is treated
 * as a one-byte character so callers always advance.
 */
static size_t char_len(const unsigned char *s, size_t n, size_t i) {
    size_t need = seq_len(s[i]);
    if (need == 0) return 1;
    if (need == 1) return 1;
    if (i + need > n) return 1;
    for (size_t k = 1; k < need; k++) {
        if (!is_cont(s[i + k])) return 1;
    }
    return need;
}

/* ------------------------------------------------------------------------- */
/* Growable buffer                                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} dbuf;

static bool db_grow(dbuf *b, size_t extra) {
    if (extra > SIZE_MAX - b->len - 1) return false;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return true;
    size_t cap = b->cap ? b->cap : 128;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    char *p = core_realloc(b->data, b->cap, cap);
    if (!p) return false;
    b->data = p;
    b->cap = cap;
    return true;
}

static bool db_putn(dbuf *b, const char *s, size_t n) {
    if (!db_grow(b, n)) return false;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool db_puts(dbuf *b, const char *s) {
    return db_putn(b, s, strlen(s));
}

static bool db_putc(dbuf *b, char c) {
    return db_putn(b, &c, 1);
}

static bool db_spaces(dbuf *b, size_t n) {
    if (!db_grow(b, n)) return false;
    memset(b->data + b->len, ' ', n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool db_u32(dbuf *b, uint32_t v) {
    char tmp[10];
    size_t i = sizeof(tmp);
    do {
        i--;
        tmp[i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0);
    return db_putn(b, tmp + i, sizeof(tmp) - i);
}

static size_t u32_digits(uint32_t v) {
    size_t n = 1;
    while (v >= 10u) {
        v /= 10u;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------------- */
/* Source navigation                                                          */
/* ------------------------------------------------------------------------- */

static void line_col(const char *src, size_t srclen, uint32_t start,
                     uint32_t *out_line, uint32_t *out_col) {
    size_t limit = start < srclen ? start : srclen;
    uint32_t line = 1;
    uint32_t col = 1;
    size_t offset = 0;
    size_t i = 0;
    const unsigned char *s = (const unsigned char *)src;

    while (i < srclen) {
        if (offset >= limit) break;
        size_t clen = char_len(s, srclen, i);
        bool bom = i == 0 && clen == 3 && s[0] == 0xEFu && s[1] == 0xBBu &&
                   s[2] == 0xBFu;
        offset += clen;
        if (bom) {
            i += clen;
            continue;
        }
        if (s[i] == '\n' && clen == 1) {
            line++;
            col = 1;
        } else {
            col++;
        }
        i += clen;
    }

    *out_line = line;
    *out_col = col;
}

/* The line itself, 1-based, without its terminator and without a trailing CR. */
static char *line_text(const char *src, size_t srclen, uint32_t line,
                       size_t *out_len) {
    size_t start = 0;
    for (uint32_t l = 1; l < line; l++) {
        const char *nl = memchr(src + start, '\n', srclen - start);
        if (!nl) {
            char *empty = core_alloc(1);
            if (empty) empty[0] = '\0';
            *out_len = 0;
            return empty;
        }
        start = (size_t)(nl - src) + 1;
    }
    const char *nl = memchr(src + start, '\n', srclen - start);
    size_t end = nl ? (size_t)(nl - src) : srclen;
    while (end > start && src[end - 1] == '\r') {
        end--;
    }
    size_t len = end - start;
    char *out = core_alloc(len + 1);
    if (!out) return NULL;
    memcpy(out, src + start, len);
    out[len] = '\0';
    *out_len = len;
    return out;
}

typedef struct {
    size_t off;
    size_t len;
    size_t col;
    size_t width;
} cell;

static size_t cell_width(const unsigned char *line, size_t off, size_t col) {
    if (line[off] == '\t') {
        return CORE_TAB_WIDTH - (col % CORE_TAB_WIDTH);
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Locate                                                                     */
/* ------------------------------------------------------------------------- */

const char *core_get_hint(const char *message) {
    struct rule {
        const char *prefix;
        const char *hint;
    };
    static const struct rule rules[] = {
        {"unsupported keyword `class`", "this language has no classes; functions, arrays and scalars only."},
        {"unsupported keyword", "this construct is outside the strict subset; see documentation for supported features."},
        {"unsupported string interpolation", "use `\"...\" . $name . \"...\"`, or sprintf() with %s."},
        {"unsupported operator", "the operator is not in the subset; write the equivalent call or nested expression."},
        {"unsupported compound assignment", "only `.=`, `+=`, `-=`, `*=`, `/=` and `%=` are defined; write the long form."},
        {"unexpected character", "this byte cannot appear here; the usual cause is a smart quote or a string closed on one side only."},
        {"expected an expression, found end of file", "the file ends inside an expression: a `)`, `]` or `}` is probably missing."},
        {"expected `)`", "a parenthesised expression is not closed at this position."},
        {"expected `}`", "a block is not closed at this position."},
        {"expected `;`", "the statement before this position has no terminator."},
        {"expected an array literal", "an array element was expected; write `[..]` or `array(..)`."},
        {"unknown function", "there is no include, autoload or dynamic dispatch: declare the function in this file or use a builtin."},
        {"cannot redeclare builtin function", "builtins are always visible; choose a different name."},
        {"function declarations are only allowed at the top level", "move the `function` out of the block; declarations cannot be conditional."},
        {"too many variables", "one function holds at most 256 locals; split it into two functions."},
        {"array path is deeper than", "index depth is capped; build the key in a variable and index once."},
        {"duplicate parameter", "parameter names must be distinct within one declaration."},
        {"must be a variable", "this construct writes through its argument, so it needs a variable or an element, not a value."},
        {"expects at least", "check the argument count against the builtin's documented arity."},
        {"expects between", "check the argument count against the builtin's documented arity."},
        {"expects exactly", "check the argument count against the builtin's documented arity."},
    };

    if (!message) return NULL;
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
        size_t n = strlen(rules[i].prefix);
        if (strncmp(message, rules[i].prefix, n) == 0) {
            return rules[i].hint;
        }
    }
    return NULL;
}

core_located *core_locate(const char *src, core_span span, const char *message) {
    if (!src) src = "";
    size_t srclen = strlen(src);

    uint32_t line = 1;
    uint32_t col = 1;
    line_col(src, srclen, span.start, &line, &col);

    size_t llen = 0;
    char *text_line = line_text(src, srclen, line, &llen);
    if (!text_line) return NULL;

    /* Every byte is at most one character, so llen cells is an upper bound. */
    cell *cells = NULL;
    size_t ncells = 0;
    if (llen != 0) {
        cells = core_alloc(llen * sizeof(*cells));
        if (!cells) {
            core_free(text_line);
            return NULL;
        }
        const unsigned char *u = (const unsigned char *)text_line;
        size_t i = 0;
        size_t column = 0;
        while (i < llen) {
            size_t clen = char_len(u, llen, i);
            cells[ncells].off = i;
            cells[ncells].len = clen;
            cells[ncells].col = column;
            cells[ncells].width = cell_width(u, i, column);
            column += cells[ncells].width;
            ncells++;
            i += clen;
        }
    }

    size_t target = 0;
    size_t idx = (size_t)(col > 0 ? col - 1 : 0);
    if (idx < ncells) {
        target = cells[idx].col;
    } else {
        for (size_t i = 0; i < ncells; i++) {
            size_t end = cells[i].col + cells[i].width;
            if (end > target) target = end;
        }
    }

    /* Window long lines on both sides of the caret: a diagnostic must never
     * dump a 200 KB source line into a terminal. */
    bool windowed = target >= CORE_WINDOW || ncells > 4u * CORE_WINDOW;
    size_t cut = 0;
    if (windowed) {
        cut = target > (CORE_WINDOW - 10u) ? target - (CORE_WINDOW - 10u) : 0;
    }
    size_t right_limit = windowed ? cut + CORE_WINDOW : 0;
    bool truncated_right = false;

    size_t first_index = 0;
    if (windowed) {
        while (first_index < ncells && cells[first_index].col < cut) {
            first_index++;
        }
    }
    size_t first_col = first_index < ncells ? cells[first_index].col : 0;
    size_t caret_col = target - first_col + (windowed ? 1u : 0u);

    dbuf body = {0};
    bool ok = true;
    const unsigned char *u = (const unsigned char *)text_line;
    for (size_t i = first_index; ok && i < ncells; i++) {
        if (windowed && cells[i].col >= right_limit) {
            truncated_right = true;
            break;
        }
        if (u[cells[i].off] == '\t') {
            ok = db_spaces(&body, cells[i].width);
        } else {
            ok = db_putn(&body, text_line + cells[i].off, cells[i].len);
        }
    }
    if (!ok) {
        core_free(body.data);
        core_free(cells);
        core_free(text_line);
        return NULL;
    }
    if (!body.data) {
        body.data = core_alloc(1);
        if (!body.data) {
            core_free(cells);
            core_free(text_line);
            return NULL;
        }
        body.data[0] = '\0';
        body.len = 0;
    }

    size_t pre = windowed ? 3 : 0;
    size_t suf = truncated_right ? 3 : 0;
    char *snippet = core_alloc(pre + body.len + suf + 1);
    if (snippet) {
        size_t off = 0;
        if (pre != 0) {
            memcpy(snippet, "\xE2\x80\xA6", 3);
            off = 3;
        }
        memcpy(snippet + off, body.data, body.len);
        off += body.len;
        if (suf != 0) {
            memcpy(snippet + off, "\xE2\x80\xA6", 3);
            off += 3;
        }
        snippet[off] = '\0';
    }
    core_free(body.data);
    core_free(cells);
    core_free(text_line);

    if (!snippet) return NULL;

    char *caret = core_alloc(caret_col + 2);
    if (!caret) {
        core_free(snippet);
        return NULL;
    }
    memset(caret, ' ', caret_col);
    caret[caret_col] = '^';
    caret[caret_col + 1] = '\0';

    core_located *at = core_alloc(sizeof(*at));
    if (!at) {
        core_free(caret);
        core_free(snippet);
        return NULL;
    }
    at->line = line;
    at->col = col;
    at->snippet = snippet;
    at->caret = caret;
    at->hint = core_get_hint(message);
    at->windowed = windowed;
    return at;
}

void core_located_free(core_located *at) {
    if (!at) return;
    core_free(at->snippet);
    core_free(at->caret);
    core_free(at);
}

/* ------------------------------------------------------------------------- */
/* Formatting                                                                 */
/* ------------------------------------------------------------------------- */

char *core_format_block(const char *path, const char *message,
                        const core_located *at) {
    if (!path || !message || !at) return NULL;

    size_t width = u32_digits(at->line);
    size_t line_digits = width;
    dbuf b = {0};
    bool ok = true;

    ok = ok && db_puts(&b, path);
    ok = ok && db_putc(&b, ':');
    ok = ok && db_u32(&b, at->line);
    ok = ok && db_putc(&b, ':');
    ok = ok && db_u32(&b, at->col);
    ok = ok && db_puts(&b, ": error: ");
    ok = ok && db_puts(&b, message);
    ok = ok && db_putc(&b, '\n');

    ok = ok && db_spaces(&b, width - line_digits);
    ok = ok && db_u32(&b, at->line);
    ok = ok && db_puts(&b, " | ");
    ok = ok && db_puts(&b, at->snippet);
    ok = ok && db_putc(&b, '\n');

    ok = ok && db_spaces(&b, width);
    ok = ok && db_puts(&b, " | ");
    ok = ok && db_puts(&b, at->caret);
    ok = ok && db_putc(&b, '\n');

    if (at->hint) {
        ok = ok && db_spaces(&b, width + 1);
        ok = ok && db_puts(&b, "= note: ");
        ok = ok && db_puts(&b, at->hint);
        ok = ok && db_putc(&b, '\n');
    }

    if (!ok) {
        core_free(b.data);
        return NULL;
    }
    if (!b.data) {
        b.data = core_alloc(1);
        if (b.data) b.data[0] = '\0';
    }
    return b.data;
}

core_json_value *core_diagnostics_json(const char *path, const char *message,
                                       const core_located *at) {
    if (!at) return NULL;

    /* The caret is ASCII spaces plus '^', so its length-1 is the column. */
    size_t caret_len = strlen(at->caret);
    uint64_t caret_col = caret_len > 0 ? (uint64_t)(caret_len - 1) : 0;

    const char *keys[8] = {"path",   "line",     "column", "message",
                           "excerpt", "caret",    "windowed", "hint"};
    core_json_value *vals[8] = {0};

    vals[0] = core_json_string(path ? path : "");
    vals[1] = core_json_uint(at->line);
    vals[2] = core_json_uint(at->col);
    vals[3] = core_json_string(message ? message : "");
    vals[4] = core_json_string(at->snippet);
    vals[5] = core_json_uint(caret_col);
    vals[6] = core_json_bool(at->windowed);
    vals[7] = at->hint ? core_json_string(at->hint) : core_json_null();

    for (size_t i = 0; i < 8; i++) {
        if (!vals[i]) {
            for (size_t j = 0; j < 8; j++) {
                core_json_free(vals[j]);
            }
            return NULL;
        }
    }

    core_json_value *doc = core_json_object(keys, vals, 8);
    if (!doc) {
        for (size_t i = 0; i < 8; i++) {
            core_json_free(vals[i]);
        }
    }
    return doc;
}
