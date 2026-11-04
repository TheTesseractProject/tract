#ifndef CORE_DIAGNOSTICS_H
#define CORE_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "core_json.h"

/*
 * Compiler diagnostics, ported from pico's common-core Rust `diagnostics.rs`.
 *
 * A diagnostic keeps the frozen first line — `path:line:col: error: message` —
 * and adds a tab-expanded excerpt, a caret aligned against that excerpt, and a
 * rule-based hint. Columns count characters, not bytes, and a leading UTF-8 BOM
 * does not shift column 1.
 */

/* Byte offsets into the source; `end` is exclusive. */
typedef struct {
    uint32_t start;
    uint32_t end;
} core_span;

typedef struct {
    uint32_t line;
    uint32_t col;
    char *snippet;   /* owned: tab-expanded, windowed with a leading U+2026 */
    char *caret;     /* owned: spaces plus '^' */
    const char *hint; /* static, or NULL */
    bool windowed;
} core_located;

/* NULL on allocation failure. Free with core_located_free. */
core_located *core_locate(const char *src, core_span span, const char *message);
void core_located_free(core_located *at);

/* Human-facing multi-line block. NULL on allocation failure. */
char *core_format_block(const char *path, const char *message,
                        const core_located *at);

/* Same facts as fields, for `--json`. NULL on allocation failure. */
core_json_value *core_diagnostics_json(const char *path, const char *message,
                                       const core_located *at);

/* Static hint whose prefix matches `message`, or NULL. */
const char *core_get_hint(const char *message);

#endif /* CORE_DIAGNOSTICS_H */
