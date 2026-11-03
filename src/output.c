#define _POSIX_C_SOURCE 200809L

#include "output.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ANSI escapes are only wanted on interactive terminals, and never when the
 * user asked for plain output via NO_COLOR. */
bool terra_colors_enabled(FILE *stream) {
    static int no_color = -1;
    if (no_color < 0) {
        const char *env = getenv("NO_COLOR");
        no_color = (env != NULL && env[0] != '\0') ? 1 : 0;
    }
    if (no_color) return false;
    int fd = fileno(stream);
    return fd >= 0 && isatty(fd);
}

/* Copy `src` to `dst` (cap `cap` bytes) with ANSI CSI colour sequences
 * removed. Escape sequences never contain '%' specifiers, so the filtered
 * text stays a valid format string. Returns the filtered length. */
static size_t strip_ansi(const char *src, char *dst, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; src[i] != '\0'; i++) {
        if ((unsigned char)src[i] == 0x1B && src[i + 1] == '[') {
            i += 2;
            while (src[i] != '\0' && !((src[i] >= '@') && (src[i] <= '~'))) {
                i++;
            }
            continue; /* also skips the final letter */
        }
        if (o + 1 < cap) {
            dst[o++] = src[i];
        }
    }
    if (o < cap) dst[o] = '\0';
    return o;
}

static void emit(FILE *stream, const char *fmt, va_list ap) {
    if (terra_colors_enabled(stream)) {
        vfprintf(stream, fmt, ap);
        return;
    }
    char plain[1024];
    if (strip_ansi(fmt, plain, sizeof plain) >= sizeof plain - 1) {
        /* Filtered text did not fit: print without colour instead of
         * truncating the message. */
        vfprintf(stream, fmt, ap);
        return;
    }
    vfprintf(stream, plain, ap);
}

void terra_out(const char *s) {
    if (terra_colors_enabled(stdout)) {
        fputs(s, stdout);
        return;
    }
    char plain[1024];
    if (strip_ansi(s, plain, sizeof plain) >= sizeof plain - 1) {
        fputs(s, stdout);
        return;
    }
    fputs(plain, stdout);
}

void terra_outf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, fmt, ap);
    va_end(ap);
}

void terra_err(const char *s) {
    if (terra_colors_enabled(stderr)) {
        fputs(s, stderr);
        return;
    }
    char plain[1024];
    if (strip_ansi(s, plain, sizeof plain) >= sizeof plain - 1) {
        fputs(s, stderr);
        return;
    }
    fputs(plain, stderr);
}

void terra_errf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stderr, fmt, ap);
    va_end(ap);
}
