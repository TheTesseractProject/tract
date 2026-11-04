#pragma once

#include <stdbool.h>
#include <stdio.h>

/* Colour-aware output helpers. ANSI escapes embedded in message strings are
 * stripped automatically when the target stream is not a terminal or when the
 * user set NO_COLOR, so redirected output stays clean. */

bool terra_colors_enabled(FILE *stream);

void terra_out(const char *s);
void terra_outf(const char *fmt, ...);
void terra_err(const char *s);
void terra_errf(const char *fmt, ...);
