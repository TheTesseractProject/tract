#include "core_json.h"

#include "core_alloc.h"
#include "core_float.h"

#include <string.h>

#define CORE_JSON_FFFD "\xEF\xBF\xBD"

static char json_error[128] = {0};

static void set_error(const char *msg) {
    size_t n = strlen(msg);
    if (n >= sizeof(json_error)) {
        n = sizeof(json_error) - 1;
    }
    memcpy(json_error, msg, n);
    json_error[n] = '\0';
}

const char *core_json_error(void) {
    return json_error;
}

/* ------------------------------------------------------------------------- */
/* UTF-8                                                                      */
/* ------------------------------------------------------------------------- */

static bool is_cont(unsigned char b) {
    return (b & 0xC0u) == 0x80u;
}

/* Expected length of a well-formed sequence starting with `b`; 0 if impossible. */
static size_t seq_len(unsigned char b) {
    if (b < 0x80u) {
        return 1;
    }
    if (b >= 0xC2u && b <= 0xDFu) {
        return 2;
    }
    if (b >= 0xE0u && b <= 0xEFu) {
        return 3;
    }
    if (b >= 0xF0u && b <= 0xF4u) {
        return 4;
    }
    return 0;
}

/* Is the second byte in the range this lead byte allows? */
static bool second_ok(unsigned char lead, unsigned char b) {
    if (lead == 0xE0u) return b >= 0xA0u && b <= 0xBFu;
    if (lead == 0xEDu) return b >= 0x80u && b <= 0x9Fu;
    if (lead == 0xF0u) return b >= 0x90u && b <= 0xBFu;
    if (lead == 0xF4u) return b >= 0x80u && b <= 0x8Fu;
    return is_cont(b);
}

/*
 * Rust's String::from_utf8_lossy semantics: every maximal invalid subpart
 * becomes one U+FFFD, and a truncated sequence at the end becomes one
 * replacement for the whole remainder.
 */
static char *lossy_utf8(const unsigned char *raw, size_t len, size_t *out_len,
                        bool *valid) {
    if (len > (SIZE_MAX - 1) / 3) {
        set_error("input too large");
        return NULL;
    }
    char *out = core_alloc(len * 3 + 1);
    if (!out) {
        set_error("memory allocation failed");
        return NULL;
    }

    size_t i = 0;
    size_t o = 0;
    bool all_valid = true;
    while (i < len) {
        unsigned char b = raw[i];
        if (b < 0x80u) {
            out[o++] = (char)b;
            i++;
            continue;
        }

        size_t need = seq_len(b);
        if (need == 0) {
            memcpy(out + o, CORE_JSON_FFFD, 3);
            o += 3;
            all_valid = false;
            i++;
            continue;
        }
        if (i + 1 >= len) {
            memcpy(out + o, CORE_JSON_FFFD, 3);
            o += 3;
            all_valid = false;
            i = len;
            continue;
        }
        if (!second_ok(b, raw[i + 1])) {
            memcpy(out + o, CORE_JSON_FFFD, 3);
            o += 3;
            all_valid = false;
            i++;
            continue;
        }
        if (i + need > len) {
            memcpy(out + o, CORE_JSON_FFFD, 3);
            o += 3;
            all_valid = false;
            i = len;
            continue;
        }
        size_t k = 2;
        while (k < need && is_cont(raw[i + k])) {
            k++;
        }
        if (k < need) {
            /* Keep the bytes that did form a prefix, replace just those. */
            memcpy(out + o, CORE_JSON_FFFD, 3);
            o += 3;
            all_valid = false;
            i += k;
            continue;
        }
        memcpy(out + o, raw + i, need);
        o += need;
        i += need;
    }

    out[o] = '\0';
    *out_len = o;
    if (valid) {
        *valid = all_valid;
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Growable output buffer                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} jbuf;

static bool jb_grow(jbuf *b, size_t extra) {
    if (extra > SIZE_MAX - b->len - 1) {
        set_error("output too large");
        return false;
    }
    size_t need = b->len + extra + 1;
    if (need <= b->cap) {
        return true;
    }
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    char *p = core_realloc(b->data, b->cap, cap);
    if (!p) {
        set_error("memory allocation failed");
        return false;
    }
    b->data = p;
    b->cap = cap;
    return true;
}

static bool jb_putn(jbuf *b, const char *s, size_t n) {
    if (!jb_grow(b, n)) {
        return false;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool jb_puts(jbuf *b, const char *s) {
    return jb_putn(b, s, strlen(s));
}

static bool jb_putc(jbuf *b, char c) {
    return jb_putn(b, &c, 1);
}

static bool jb_u64(jbuf *b, uint64_t v) {
    char tmp[21];
    size_t i = sizeof(tmp) - 1;
    tmp[i] = '\0';
    do {
        i--;
        tmp[i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0);
    return jb_putn(b, tmp + i, sizeof(tmp) - 1 - i);
}

static bool jb_i64(jbuf *b, int64_t v) {
    if (v < 0) {
        if (!jb_putc(b, '-')) {
            return false;
        }
        uint64_t mag = (uint64_t)(-(v + 1)) + 1u;
        return jb_u64(b, mag);
    }
    return jb_u64(b, (uint64_t)v);
}

static char hex_digit(unsigned nibble) {
    return (char)('0' + (nibble & 0x0Fu) + ((nibble & 0x0Fu) > 9 ? 39 : 0));
}

/* Escape a UTF-8 string of known length, byte by byte. */
static bool write_str(jbuf *b, const char *text, size_t len) {
    if (!jb_putc(b, '"')) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
            case '\\':
                if (!jb_puts(b, "\\\\")) return false;
                break;
            case '"':
                if (!jb_puts(b, "\\\"")) return false;
                break;
            case '\b':
                if (!jb_puts(b, "\\b")) return false;
                break;
            case '\t':
                if (!jb_puts(b, "\\t")) return false;
                break;
            case '\n':
                if (!jb_puts(b, "\\n")) return false;
                break;
            case '\f':
                if (!jb_puts(b, "\\f")) return false;
                break;
            case '\r':
                if (!jb_puts(b, "\\r")) return false;
                break;
            default:
                if (c < 0x20u) {
                    char esc[6] = {'\\', 'u', '0', '0', hex_digit((unsigned)(c >> 4)),
                                   hex_digit(c)};
                    if (!jb_putn(b, esc, sizeof(esc))) return false;
                } else {
                    if (!jb_putc(b, (char)c)) return false;
                }
                break;
        }
    }
    return jb_putc(b, '"');
}

/* ------------------------------------------------------------------------- */
/* Constructors                                                               */
/* ------------------------------------------------------------------------- */

static core_json_value *new_value(core_json_type type) {
    core_json_value *v = core_alloc(sizeof(*v));
    if (!v) {
        set_error("memory allocation failed");
        return NULL;
    }
    memset(v, 0, sizeof(*v));
    v->type = type;
    return v;
}

core_json_value *core_json_null(void) {
    return new_value(CORE_JSON_NULL);
}

core_json_value *core_json_bool(bool val) {
    core_json_value *v = new_value(CORE_JSON_BOOL);
    if (v) {
        v->as.bool_val = val;
    }
    return v;
}

core_json_value *core_json_int(int64_t val) {
    core_json_value *v = new_value(CORE_JSON_INT);
    if (v) {
        v->as.int_val = val;
    }
    return v;
}

core_json_value *core_json_uint(uint64_t val) {
    core_json_value *v = new_value(CORE_JSON_UINT);
    if (v) {
        v->as.uint_val = val;
    }
    return v;
}

core_json_value *core_json_string_len(const char *str, size_t len) {
    core_json_value *v = new_value(CORE_JSON_STR);
    if (!v) {
        return NULL;
    }
    size_t out_len = 0;
    char *text = lossy_utf8((const unsigned char *)(str ? str : ""), str ? len : 0,
                            &out_len, NULL);
    if (!text) {
        core_free(v);
        return NULL;
    }
    v->as.str.text = text;
    v->as.str.len = out_len;
    return v;
}

core_json_value *core_json_string(const char *str) {
    return core_json_string_len(str, str ? strlen(str) : 0);
}

core_json_value *core_json_bytes(const unsigned char *raw, size_t len) {
    core_json_value *v = new_value(CORE_JSON_BYTES);
    if (!v) {
        return NULL;
    }
    size_t out_len = 0;
    bool valid = false;
    char *text = lossy_utf8(raw ? raw : (const unsigned char *)"", raw ? len : 0,
                            &out_len, &valid);
    if (!text) {
        core_free(v);
        return NULL;
    }
    v->as.bytes.text = text;
    v->as.bytes.text_len = out_len;
    v->as.bytes.len = raw ? len : 0;
    v->as.bytes.utf8 = valid;
    return v;
}

core_json_value *core_json_float(double val) {
    core_json_value *v = new_value(CORE_JSON_FLOAT);
    if (!v) {
        return NULL;
    }
    char text[CORE_FLOAT_TEXT_MAX];
    size_t n = core_format_float(val, text);
    char *copy = core_alloc(n + 1);
    if (!copy) {
        core_free(v);
        set_error("memory allocation failed");
        return NULL;
    }
    memcpy(copy, text, n);
    copy[n] = '\0';
    uint64_t bits = 0;
    memcpy(&bits, &val, sizeof bits);
    v->as.flt.text = copy;
    v->as.flt.bits = bits;
    return v;
}

core_json_value *core_json_array(core_json_value **items, size_t count) {
    core_json_value *v = new_value(CORE_JSON_ARRAY);
    if (!v) {
        return NULL;
    }
    core_json_value **copy = NULL;
    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*copy)) {
            core_free(v);
            set_error("array too large");
            return NULL;
        }
        copy = core_alloc(count * sizeof(*copy));
        if (!copy) {
            core_free(v);
            set_error("memory allocation failed");
            return NULL;
        }
        memcpy(copy, items, count * sizeof(*copy));
    }
    v->as.array.items = copy;
    v->as.array.count = count;
    return v;
}

static char *dup_utf8(const char *s) {
    size_t n = 0;
    return lossy_utf8((const unsigned char *)s, strlen(s), &n, NULL);
}

core_json_value *core_json_object(const char **keys,
                                  core_json_value **values, size_t count) {
    core_json_value *v = new_value(CORE_JSON_OBJECT);
    if (!v) {
        return NULL;
    }
    char **key_copy = NULL;
    core_json_value **val_copy = NULL;
    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*key_copy)) {
            core_free(v);
            set_error("object too large");
            return NULL;
        }
        key_copy = core_alloc(count * sizeof(*key_copy));
        val_copy = core_alloc(count * sizeof(*val_copy));
        if (!key_copy || !val_copy) {
            core_free(key_copy);
            core_free(val_copy);
            core_free(v);
            set_error("memory allocation failed");
            return NULL;
        }
        for (size_t i = 0; i < count; i++) {
            key_copy[i] = dup_utf8(keys[i] ? keys[i] : "");
            if (!key_copy[i]) {
                for (size_t j = 0; j < i; j++) {
                    core_free(key_copy[j]);
                }
                core_free(key_copy);
                core_free(val_copy);
                core_free(v);
                return NULL;
            }
        }
        memcpy(val_copy, values, count * sizeof(*val_copy));
    }
    v->as.object.keys = key_copy;
    v->as.object.values = val_copy;
    v->as.object.count = count;
    return v;
}

core_json_value *core_json_envelope(const char *cmd, bool ok, uint8_t exit,
                                    const char **fields,
                                    core_json_value **values,
                                    size_t field_count) {
    if (field_count > SIZE_MAX / sizeof(char *) - 4) {
        set_error("too many fields");
        return NULL;
    }
    size_t total = 4 + field_count;
    const char **keys = core_alloc(total * sizeof(*keys));
    core_json_value **vals = core_alloc(total * sizeof(*vals));
    if (!keys || !vals) {
        core_free(keys);
        core_free(vals);
        set_error("memory allocation failed");
        return NULL;
    }

    keys[0] = "schema";
    vals[0] = core_json_string(CORE_JSON_SCHEMA);
    keys[1] = "cmd";
    vals[1] = core_json_string(cmd ? cmd : "");
    keys[2] = "ok";
    vals[2] = core_json_bool(ok);
    keys[3] = "exit";
    vals[3] = core_json_uint(exit);

    bool ok_build = vals[0] && vals[1] && vals[2] && vals[3];
    for (size_t i = 0; ok_build && i < field_count; i++) {
        keys[4 + i] = fields[i];
        vals[4 + i] = values[i];
    }

    core_json_value *doc = NULL;
    if (ok_build) {
        doc = core_json_object(keys, vals, total);
    }
    if (!doc) {
        for (size_t i = 0; i < 4; i++) {
            core_json_free(vals[i]);
        }
    }
    /* `keys` is a temporary array; core_json_object duplicated its contents. */
    core_free(keys);
    core_free(vals);
    return doc;
}

/* ------------------------------------------------------------------------- */
/* Render                                                                     */
/* ------------------------------------------------------------------------- */

static bool render_value(jbuf *b, const core_json_value *v, unsigned depth);

static bool render_value(jbuf *b, const core_json_value *v, unsigned depth) {
    if (!v) {
        return jb_puts(b, "null");
    }
    if (depth > CORE_JSON_MAX_DEPTH) {
        set_error("json nesting too deep");
        return false;
    }

    switch (v->type) {
        case CORE_JSON_NULL:
            return jb_puts(b, "null");
        case CORE_JSON_BOOL:
            return jb_puts(b, v->as.bool_val ? "true" : "false");
        case CORE_JSON_INT:
            return jb_i64(b, v->as.int_val);
        case CORE_JSON_UINT:
            return jb_u64(b, v->as.uint_val);
        case CORE_JSON_STR:
            return write_str(b, v->as.str.text, v->as.str.len);
        case CORE_JSON_BYTES:
            return jb_puts(b, "{\"text\":") &&
                   write_str(b, v->as.bytes.text, v->as.bytes.text_len) &&
                   jb_puts(b, ",\"len\":") && jb_u64(b, v->as.bytes.len) &&
                   jb_puts(b, ",\"utf8\":") &&
                   jb_puts(b, v->as.bytes.utf8 ? "true" : "false") &&
                   jb_putc(b, '}');
        case CORE_JSON_FLOAT:
            return jb_puts(b, "{\"tag\":\"float\",\"text\":") &&
                   write_str(b, v->as.flt.text, strlen(v->as.flt.text)) &&
                   jb_puts(b, ",\"bits\":") && jb_u64(b, v->as.flt.bits) &&
                   jb_putc(b, '}');
        case CORE_JSON_ARRAY:
            if (!jb_putc(b, '[')) {
                return false;
            }
            for (size_t i = 0; i < v->as.array.count; i++) {
                if (i != 0 && !jb_putc(b, ',')) {
                    return false;
                }
                if (!render_value(b, v->as.array.items[i], depth + 1)) {
                    return false;
                }
            }
            return jb_putc(b, ']');
        case CORE_JSON_OBJECT:
            if (!jb_putc(b, '{')) {
                return false;
            }
            for (size_t i = 0; i < v->as.object.count; i++) {
                if (i != 0 && !jb_putc(b, ',')) {
                    return false;
                }
                const char *key = v->as.object.keys[i];
                if (!write_str(b, key, strlen(key)) || !jb_putc(b, ':') ||
                    !render_value(b, v->as.object.values[i], depth + 1)) {
                    return false;
                }
            }
            return jb_putc(b, '}');
    }
    set_error("unknown json value type");
    return false;
}

char *core_json_render(const core_json_value *root) {
    json_error[0] = '\0';
    jbuf b = {0};
    if (!render_value(&b, root, 0) || !jb_putc(&b, '\n')) {
        core_free(b.data);
        if (json_error[0] == '\0') {
            set_error("render failed");
        }
        return NULL;
    }
    return b.data;
}

/* ------------------------------------------------------------------------- */
/* Free                                                                       */
/* ------------------------------------------------------------------------- */

void core_json_free(core_json_value *val) {
    if (!val) {
        return;
    }
    switch (val->type) {
        case CORE_JSON_NULL:
        case CORE_JSON_BOOL:
        case CORE_JSON_INT:
        case CORE_JSON_UINT:
            break;
        case CORE_JSON_STR:
            core_free(val->as.str.text);
            break;
        case CORE_JSON_BYTES:
            core_free(val->as.bytes.text);
            break;
        case CORE_JSON_FLOAT:
            core_free(val->as.flt.text);
            break;
        case CORE_JSON_ARRAY:
            for (size_t i = 0; i < val->as.array.count; i++) {
                core_json_free(val->as.array.items[i]);
            }
            core_free(val->as.array.items);
            break;
        case CORE_JSON_OBJECT:
            for (size_t i = 0; i < val->as.object.count; i++) {
                core_free(val->as.object.keys[i]);
                core_json_free(val->as.object.values[i]);
            }
            core_free(val->as.object.keys);
            core_free(val->as.object.values);
            break;
    }
    core_free(val);
}
