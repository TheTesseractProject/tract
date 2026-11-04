#ifndef CORE_JSON_H
#define CORE_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Machine-facing JSON writer, ported from pico's common-core Rust `json.rs`.
 *
 * Two rules are part of the contract:
 *  - a float is never emitted as a JSON number (NaN/INF have no spelling), and
 *    an integer re-encoded as a double loses precision past 2^53. Floats are
 *    emitted as {"tag":"float","text":...,"bits":...}, where `text` comes from
 *    core_format_float and `bits` is the exact IEEE-754 payload;
 *  - bytes are not strings. Binary output carries lossy UTF-8 text, the exact
 *    byte length, and an explicit `utf8` flag.
 *
 * Ownership: constructors take ownership of the child values on success. Array
 * and object constructors copy their pointer arrays (and object keys), so the
 * caller may free the arrays it passed in. Free the whole tree with
 * core_json_free. On failure a constructor returns NULL and does not take
 * ownership of the children.
 */

#define CORE_JSON_SCHEMA "common-lib/1"

/* Guards against unbounded recursion in core_json_render. */
#define CORE_JSON_MAX_DEPTH 64

typedef enum {
    CORE_JSON_NULL,
    CORE_JSON_BOOL,
    CORE_JSON_INT,
    CORE_JSON_UINT,
    CORE_JSON_STR,
    CORE_JSON_BYTES,
    CORE_JSON_FLOAT,
    CORE_JSON_ARRAY,
    CORE_JSON_OBJECT
} core_json_type;

typedef struct core_json_value core_json_value;

struct core_json_value {
    core_json_type type;
    union {
        bool bool_val;
        int64_t int_val;
        uint64_t uint_val;
        struct {
            char *text;
            size_t len;
        } str;
        struct {
            char *text;
            size_t text_len;
            size_t len;
            bool utf8;
        } bytes;
        struct {
            char *text;
            uint64_t bits;
        } flt;
        struct {
            core_json_value **items;
            size_t count;
        } array;
        struct {
            char **keys;
            core_json_value **values;
            size_t count;
        } object;
    } as;
};

core_json_value *core_json_null(void);
core_json_value *core_json_bool(bool val);
core_json_value *core_json_int(int64_t val);
core_json_value *core_json_uint(uint64_t val);

/* Copies `str`, sanitizing invalid UTF-8 to U+FFFD. */
core_json_value *core_json_string(const char *str);
/* Copies exactly `len` bytes (may include NUL), sanitizing invalid UTF-8. */
core_json_value *core_json_string_len(const char *str, size_t len);
/* Lossy UTF-8 text plus the original byte length and validity flag. */
core_json_value *core_json_bytes(const unsigned char *raw, size_t len);

/* Never a JSON number; `bits` preserves the exact payload. */
core_json_value *core_json_float(double val);

/* Copies the item-pointer array; takes ownership of the items. */
core_json_value *core_json_array(core_json_value **items, size_t count);
/* Duplicates the keys and copies the value pointer array; owns the values. */
core_json_value *core_json_object(const char **keys,
                                  core_json_value **values, size_t count);

/* {"schema":...,"cmd":...,"ok":...,"exit":...,...fields} */
core_json_value *core_json_envelope(const char *cmd, bool ok, uint8_t exit,
                                    const char **fields,
                                    core_json_value **values,
                                    size_t field_count);

/* Compact document followed by one newline. NULL on allocation/depth error. */
char *core_json_render(const core_json_value *root);

void core_json_free(core_json_value *val);

/* Message from the last failed render; "" if the last one succeeded. */
const char *core_json_error(void);

#endif /* CORE_JSON_H */
