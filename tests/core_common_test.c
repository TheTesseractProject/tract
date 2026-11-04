#include "core_alloc.h"
#include "core_crc.h"
#include "core_diagnostics.h"
#include "core_json.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void expect(const char *actual, const char *expected, const char *what) {
    if (!actual || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", what,
                expected, actual ? actual : "(null)");
        failures++;
    }
}

static char *render_trim(core_json_value *v) {
    char *out = core_json_render(v);
    if (out) {
        size_t n = strlen(out);
        if (n > 0 && out[n - 1] == '\n') {
            out[n - 1] = '\0';
        }
    }
    return out;
}

static void test_crc(void) {
    check(core_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u,
          "crc32 known value");
    check(core_crc32((const unsigned char *)"", 0) == 0, "crc32 empty");
}

static void test_json_controls(void) {
    for (unsigned b = 0; b < 0x20; b++) {
        unsigned char raw = (unsigned char)b;
        core_json_value *v = core_json_string_len((const char *)&raw, 1);
        char *got = render_trim(v);
        char expected[32];
        switch (b) {
            case 0x08: snprintf(expected, sizeof expected, "\"\\b\""); break;
            case 0x09: snprintf(expected, sizeof expected, "\"\\t\""); break;
            case 0x0A: snprintf(expected, sizeof expected, "\"\\n\""); break;
            case 0x0C: snprintf(expected, sizeof expected, "\"\\f\""); break;
            case 0x0D: snprintf(expected, sizeof expected, "\"\\r\""); break;
            default:
                snprintf(expected, sizeof expected, "\"\\u00%02x\"", b);
                break;
        }
        expect(got, expected, "json control byte");
        core_free(got);
        core_json_free(v);
    }
}

static void test_json_strings(void) {
    core_json_value *v = core_json_string("\"");
    char *got = render_trim(v);
    expect(got, "\"\\\"\"", "json quote");
    core_free(got);
    core_json_free(v);

    v = core_json_string("a/b");
    got = render_trim(v);
    expect(got, "\"a/b\"", "json slash stays raw");
    core_free(got);
    core_json_free(v);

    v = core_json_string("line1\nline2 \"x\" \\");
    got = render_trim(v);
    expect(got, "\"line1\\nline2 \\\"x\\\" \\\\\"", "json nested escapes");
    core_free(got);
    core_json_free(v);

    v = core_json_string("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    got = render_trim(v);
    expect(got, "\"\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\"",
           "json non-ascii passes through");
    core_free(got);
    core_json_free(v);
}

static void test_json_ints(void) {
    struct {
        int64_t v;
        const char *want;
    } cases[] = {
        {0, "0"},
        {-1, "-1"},
        {INT64_MIN, "-9223372036854775808"},
        {INT64_MAX, "9223372036854775807"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        core_json_value *v = core_json_int(cases[i].v);
        char *got = render_trim(v);
        expect(got, cases[i].want, "json int");
        core_free(got);
        core_json_free(v);
    }

    core_json_value *v = core_json_uint(UINT64_MAX);
    char *got = render_trim(v);
    expect(got, "18446744073709551615", "json uint max");
    core_free(got);
    core_json_free(v);
}

static void test_json_floats(void) {
    uint64_t nan_bits = 0x7FF8000000000000ull;
    uint64_t inf_bits = 0x7FF0000000000000ull;
    uint64_t neg_zero_bits = 0x8000000000000000ull;
    double nan_v;
    double inf_v;
    double neg_zero_v;
    memcpy(&nan_v, &nan_bits, sizeof nan_v);
    memcpy(&inf_v, &inf_bits, sizeof inf_v);
    memcpy(&neg_zero_v, &neg_zero_bits, sizeof neg_zero_v);

    core_json_value *v = core_json_float(nan_v);
    char *got = render_trim(v);
    expect(got,
           "{\"tag\":\"float\",\"text\":\"NAN\",\"bits\":9221120237041090560}",
           "json nan");
    core_free(got);
    core_json_free(v);

    v = core_json_float(inf_v);
    got = render_trim(v);
    expect(got,
           "{\"tag\":\"float\",\"text\":\"INF\",\"bits\":9218868437227405312}",
           "json inf");
    core_free(got);
    core_json_free(v);

    v = core_json_float(neg_zero_v);
    got = render_trim(v);
    expect(got,
           "{\"tag\":\"float\",\"text\":\"0\",\"bits\":9223372036854775808}",
           "json negative zero");
    core_free(got);
    core_json_free(v);

    v = core_json_float(0.1);
    got = render_trim(v);
    check(got && strstr(got, "\"text\":\"0.1\"") != NULL, "json float 0.1 text");
    check(got && strstr(got, "\"tag\":\"float\"") != NULL, "json float tag");
    core_free(got);
    core_json_free(v);
}

static void test_json_bytes(void) {
    core_json_value *v = core_json_bytes((const unsigned char *)"ok", 2);
    char *got = render_trim(v);
    expect(got, "{\"text\":\"ok\",\"len\":2,\"utf8\":true}", "json bytes ok");
    core_free(got);
    core_json_free(v);

    unsigned char raw[] = {'a', 0xFF, 'b'};
    v = core_json_bytes(raw, sizeof raw);
    got = render_trim(v);
    expect(got, "{\"text\":\"a\xEF\xBF\xBD" "b\",\"len\":3,\"utf8\":false}",
           "json bytes invalid");
    core_free(got);
    core_json_free(v);

    unsigned char truncated[] = {0xE2, 0x82};
    v = core_json_bytes(truncated, sizeof truncated);
    got = render_trim(v);
    expect(got, "{\"text\":\"\xEF\xBF\xBD\",\"len\":2,\"utf8\":false}",
           "json bytes truncated");
    core_free(got);
    core_json_free(v);

    unsigned char nul[] = {'a', 0x00, 'b'};
    v = core_json_bytes(nul, sizeof nul);
    got = render_trim(v);
    expect(got, "{\"text\":\"a\\u0000b\",\"len\":3,\"utf8\":true}",
           "json bytes interior nul");
    core_free(got);
    core_json_free(v);
}

static void test_json_structure(void) {
    const char *keys[] = {"steps", "tags"};
    core_json_value *vals[2] = {
        core_json_uint(6),
        core_json_array((core_json_value *[]){core_json_string("int"),
                                              core_json_null()},
                        2),
    };
    core_json_value *doc = core_json_envelope("run", true, 0, keys, vals, 2);
    char *got = core_json_render(doc);
    expect(got,
           "{\"schema\":\"common-lib/1\",\"cmd\":\"run\",\"ok\":true,"
           "\"exit\":0,\"steps\":6,\"tags\":[\"int\",null]}\n",
           "json envelope");
    core_free(got);
    core_json_free(doc);

    doc = core_json_array(NULL, 0);
    got = render_trim(doc);
    expect(got, "[]", "json empty array");
    core_free(got);
    core_json_free(doc);

    doc = core_json_object(NULL, NULL, 0);
    got = render_trim(doc);
    expect(got, "{}", "json empty object");
    core_free(got);
    core_json_free(doc);
}

static void test_json_depth(void) {
    core_json_value *root = core_json_null();
    for (unsigned i = 0; i < CORE_JSON_MAX_DEPTH + 5; i++) {
        core_json_value **items = core_alloc(sizeof(*items));
        items[0] = root;
        root = core_json_array(items, 1);
        core_free(items);
    }
    char *got = core_json_render(root);
    check(got == NULL, "deep nesting is rejected");
    check(core_json_error()[0] != '\0', "deep nesting sets an error");
    core_free(got);
    core_json_free(root);
}

static void test_diagnostics_first_line(void) {
    const char *src = "<?php echo (1; ?>";
    core_located *at =
        core_locate(src, (core_span){13, 14},
                    "expected `)` to close the group, found `;`");
    check(at != NULL, "locate first line");
    check(at->line == 1 && at->col == 14, "locate first line line/col");

    char *block = core_format_block(
        "group.php", "expected `)` to close the group, found `;`", at);
    check(block != NULL, "format block");
    if (block) {
        const char *nl1 = strchr(block, '\n');
        const char *nl2 = nl1 ? strchr(nl1 + 1, '\n') : NULL;
        const char *nl3 = nl2 ? strchr(nl2 + 1, '\n') : NULL;
        check(nl1 && nl2 && nl3, "block has three lines");
        if (nl1 && nl2 && nl3) {
            check((size_t)(nl1 - block) ==
                      strlen("group.php:1:14: error: expected `)` to close the "
                             "group, found `;`"),
                  "block first line");
            check((size_t)(nl2 - nl1 - 1) == strlen("1 | <?php echo (1; ?>"),
                  "block excerpt line");
            check((size_t)(nl3 - nl2 - 1) == strlen("  |              ^"),
                  "block caret line");
            check(strncmp(nl1 + 1, "1 | <?php echo (1; ?>", nl2 - nl1 - 1) == 0,
                  "block excerpt content");
            check(strncmp(nl2 + 1, "  |              ^", nl3 - nl2 - 1) == 0,
                  "block caret content");
        }
    }
    core_free(block);
    core_located_free(at);
}

/* Byte index of the character at `index` (0-based), or strlen on overflow. */
static size_t char_offset(const char *s, size_t index) {
    size_t i = 0;
    while (s[i] != '\0' && index > 0) {
        unsigned char c = (unsigned char)s[i];
        size_t n = 1;
        if (c >= 0xC2u && c <= 0xDFu) n = 2;
        else if (c >= 0xE0u && c <= 0xEFu) n = 3;
        else if (c >= 0xF0u && c <= 0xF4u) n = 4;
        i += n;
        index--;
    }
    return i;
}

static char char_at(const char *s, size_t index) {
    size_t off = char_offset(s, index);
    return s[off];
}

static void test_diagnostics_caret(void) {
    const char *src = "<?php $x = 1; echo $y; ?>";
    core_located *at = core_locate(src, (core_span){14, 18}, "unknown function `echo`");
    check(at != NULL && at->col == 15, "caret column");
    if (at) {
        size_t caret_col = strlen(at->caret) - 1;
        check(char_at(at->snippet, caret_col) == 'e', "caret points at e");
        check(char_at(src, at->col - 1) == 'e', "source agrees");
    }
    core_located_free(at);

    const char *multi = "<?php\n$a = 1;\n$b = $a +;\n";
    at = core_locate(multi, (core_span){22, 23}, "expected an expression, found `;`");
    check(at != NULL, "later line located");
    if (at) {
        expect(at->snippet, "$b = $a +;", "later line snippet");
        expect(at->caret, "        ^", "later line caret");
        check(at->line == 3 && at->col == 9, "later line number");
    }
    core_located_free(at);
}

static void test_diagnostics_utf8_and_tabs(void) {
    const char *multi = "<?php // \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 "
                        "$x = ; ?>";
    core_located *at = core_locate(multi, (core_span){27, 28}, "expected an expression");
    check(at != NULL, "utf8 located");
    if (at) {
        size_t caret_col = strlen(at->caret) - 1;
        check(char_at(at->snippet, caret_col) == ';', "utf8 caret on semicolon");
        check(at->col == 22, "utf8 columns count characters");
    }
    core_located_free(at);

    const char *tabs = "<?php\n\t\t$x = ;\n";
    at = core_locate(tabs, (core_span){8, 9}, "expected an expression");
    check(at != NULL, "tabs located");
    if (at) {
        expect(at->snippet, "        $x = ;", "tabs expanded");
        check(char_at(at->snippet, strlen(at->caret) - 1) == '$',
              "tab caret on dollar");
    }
    core_located_free(at);

    const char *crlf = "<?php echo 1;\r\n<?php $x = ;\r\n";
    at = core_locate(crlf, (core_span){24, 25}, "expected an expression");
    check(at != NULL && at->line == 2, "crlf line");
    if (at) {
        size_t n = strlen(at->snippet);
        check(n == 0 || at->snippet[n - 1] != '\r', "crlf trimmed");
    }
    core_located_free(at);
}

static void test_diagnostics_window(void) {
    char src[512];
    size_t o = 0;
    o += (size_t)snprintf(src + o, sizeof src - o, "<?php ");
    for (int i = 0; i < 40; i++) {
        o += (size_t)snprintf(src + o, sizeof src - o, "$y = 1; ");
    }
    snprintf(src + o, sizeof src - o, "$x = ; ?>");

    size_t nchars = strlen(src);
    uint32_t at_pos = (uint32_t)(nchars - 9);
    core_located *at =
        core_locate(src, (core_span){at_pos, at_pos + 1}, "expected an expression");
    check(at != NULL, "window located");
    if (at) {
        check(at->windowed, "long line is windowed");
        check(strlen(at->snippet) <= 110, "window is bounded");
        size_t caret_col = strlen(at->caret) - 1;
        check(caret_col < strlen(at->snippet), "window caret inside snippet");
        check(char_at(at->snippet, caret_col) == '$', "window keeps target char");
    }
    core_located_free(at);
}

static void test_diagnostics_misc(void) {
    struct {
        const char *message;
        const char *hint_fragment;
    } cases[] = {
        {"expected `;` after an expression, found 'put'",
         "add the missing `;`"},
        {"unexpected character", "check for a typo or unsupported character"},
        {"undefined name `missing`", "declare the variable before using it"},
        {"undefined function `missing`", "declare the function with `fn`"},
        {"`put` expects exactly 1 argument, got 2", "single value"},
        {"math has no function `sqr`", "supported functions include"},
        {"division by zero", "make the divisor non-zero"},
        {"unknown escape sequence", "use a supported escape"},
        {"indexing requires an array, slice, string, or pointer",
         "index only an array, slice, string, or pointer"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const char *hint = core_get_hint(cases[i].message);
        check(hint != NULL, "Terra diagnostic has a hint");
        if (hint != NULL) {
            check(strstr(hint, cases[i].hint_fragment) != NULL,
                  "Terra hint is relevant to its diagnostic");
        }
    }
    check(core_get_hint("unsupported keyword `class`") == NULL,
          "removed stale hint about unsupported classes");
    check(core_get_hint("unsupported string interpolation") == NULL,
          "removed stale hint about string interpolation");
    check(core_get_hint("the parser invents nothing here") == NULL,
          "no invented hint");

    const char *src = "put(\"hello\") put(\"world\");";
    const char *error = "expected `;` after an expression, found 'put'";
    const char *second_call = strstr(src, "put(\"world\")");
    core_located *at = core_locate(
        src, (core_span){(uint32_t)(second_call - src),
                         (uint32_t)(second_call - src + 3)}, error);
    char *block = core_format_block("missing-semi.t", error, at);
    check(block != NULL, "block with hint");
    if (block) {
        check(strstr(block, "missing-semi.t:1:14: error:") != NULL,
              "Terra diagnostic location");
        check(strstr(block, "  = note: add the missing `;`") != NULL,
              "Terra diagnostic prints actionable hint");
    }
    core_free(block);

    core_json_value *doc = core_diagnostics_json(
        "missing-semi.t", error,
        (core_located *)at);
    char *rendered = core_json_render(doc);
    check(rendered != NULL, "diagnostics json renders");
    if (rendered) {
        check(strstr(rendered, "\"line\":1,\"column\":14") != NULL,
              "diagnostics json line/column");
        check(strstr(rendered, "\"excerpt\":\"put(\\\"hello\\\") put(\\\"world\\\");\"") != NULL,
              "diagnostics json excerpt");
        check(strstr(rendered, "\"windowed\":false") != NULL,
              "diagnostics json windowed");
    }
    core_free(rendered);
    core_json_free(doc);
    core_located_free(at);

    const char *one_past = "<?php echo (1";
    at = core_locate(one_past, (core_span){13, 13},
                     "expected an expression, found end of file");
    check(at != NULL, "one-past-end located");
    if (at) {
        expect(at->snippet, one_past, "one-past-end snippet");
        check(strlen(at->caret) - 1 == strlen(one_past),
              "one-past-end caret at width");
    }
    core_located_free(at);
}

int main(void) {
    test_crc();
    test_json_controls();
    test_json_strings();
    test_json_ints();
    test_json_floats();
    test_json_bytes();
    test_json_structure();
    test_json_depth();
    test_diagnostics_first_line();
    test_diagnostics_caret();
    test_diagnostics_utf8_and_tabs();
    test_diagnostics_window();
    test_diagnostics_misc();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("core_common: all tests passed\n");
    return 0;
}
