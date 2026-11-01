#ifndef CORE_FLOAT_H
#define CORE_FLOAT_H

#include <stddef.h>

/*
 * Float-to-text, ported from the pico engine's float_format.c so that every
 * consumer of the shared core renders a double identically.
 *
 * The representation is PHP's default precision-14 `%G` shape:
 *   - 14 significant decimal digits, correctly rounded (half to even);
 *   - trailing fractional zeros removed;
 *   - fixed notation when the decimal exponent of the leading digit is in
 *     [-4, 13], scientific `d.dddE±X` otherwise;
 *   - "NAN", "INF", "-INF" for non-finite values; -0 prints as "0".
 *
 * No printf, no locale, no allocation: safe for EMBEDDED and bit-identical on
 * every host. `buf` must hold at least CORE_FLOAT_TEXT_MAX bytes.
 */
#define CORE_FLOAT_TEXT_MAX 40u

size_t core_format_float(double v, char *buf);

#endif /* CORE_FLOAT_H */
