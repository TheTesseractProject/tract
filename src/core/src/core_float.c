#include "core_float.h"

#include <stdint.h>
#include <string.h>

/*
 * Exact decimal conversion without division of big integers.
 *
 * A finite double is m * 2^e with m < 2^53. For e < 0 write the value as
 * (m * 5^-e) * 10^e, so it has a finite decimal expansion whose digits are
 * exactly the digits of m * 5^-e with the point shifted. We build that integer
 * with a small limb array, print its digits, then round to 14 significant
 * digits. For e >= 0 the value is already an integer (m << e).
 *
 * Worst case is a subnormal: 53 + 1074 * log2(5) ~ 2547 bits ~ 80 limbs and
 * ~770 decimal digits. The arrays below are sized with a margin; every growth
 * path is checked so a wrong assumption cannot write out of bounds.
 */

#define CORE_FLT_MAG_LIMBS 160u
#define CORE_FLT_DIGITS_MAX 800u

static size_t mag_trim(const uint32_t *m, size_t n) {
    while (n > 0 && m[n - 1] == 0) {
        n--;
    }
    return n;
}

static void mag_mul_small(uint32_t *m, size_t *np, uint32_t mul) {
    size_t n = *np;
    uint64_t carry = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t cur = (uint64_t)m[i] * (uint64_t)mul + carry;
        m[i] = (uint32_t)cur;
        carry = cur >> 32;
    }
    /*
     * Unreachable for every double (the maximum is 81 limbs), but a bounds
     * check is cheaper than trusting that argument forever.
     */
    while (carry != 0 && n < CORE_FLT_MAG_LIMBS) {
        m[n++] = (uint32_t)carry;
        carry >>= 32;
    }
    *np = n;
}

static void mag_shl(uint32_t *m, size_t *np, uint32_t bits) {
    size_t n = *np;
    uint32_t words = bits / 32u;
    uint32_t rem = bits % 32u;
    if (rem != 0) {
        uint32_t carry = 0;
        for (size_t i = 0; i < n; ++i) {
            uint32_t v = m[i];
            m[i] = (v << rem) | carry;
            /* rem is in 1..31, so 32 - rem is in 1..31: no shift by 32. */
            carry = v >> (32u - rem);
        }
        if (carry != 0 && n < CORE_FLT_MAG_LIMBS) {
            m[n++] = carry;
        }
    }
    if (words != 0 && n + (size_t)words <= CORE_FLT_MAG_LIMBS) {
        for (size_t i = n; i > 0; --i) {
            m[i + words - 1] = m[i - 1];
        }
        for (uint32_t i = 0; i < words; ++i) {
            m[i] = 0;
        }
        n += (size_t)words;
    }
    *np = n;
}

/* Divide the magnitude by a small value, return the remainder. */
static uint32_t mag_div_small(uint32_t *m, size_t *np, uint32_t d) {
    uint64_t rem = 0;
    for (size_t i = *np; i > 0; --i) {
        uint64_t cur = (rem << 32) | (uint64_t)m[i - 1];
        m[i - 1] = (uint32_t)(cur / (uint64_t)d);
        rem = cur % (uint64_t)d;
    }
    *np = mag_trim(m, *np);
    return (uint32_t)rem;
}

size_t core_format_float(double v, char *buf) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);

    int negative = (int)(bits >> 63);
    uint32_t exp_field = (uint32_t)((bits >> 52) & 0x7FFu);
    uint64_t frac = bits & 0x000FFFFFFFFFFFFFull;

    if (exp_field == 0x7FFu) {
        const char *text = frac != 0 ? "NAN" : (negative ? "-INF" : "INF");
        size_t n = strlen(text);
        memcpy(buf, text, n);
        return n;
    }
    if ((bits << 1) == 0) { /* +0.0 and -0.0 both print as "0" */
        buf[0] = '0';
        return 1;
    }

    uint64_t m;
    int32_t e;
    if (exp_field == 0) {
        m = frac;
        e = -1074;
    } else {
        m = frac | (1ull << 52);
        e = (int32_t)exp_field - 1075;
    }

    uint32_t mag[CORE_FLT_MAG_LIMBS];
    size_t n = 0;
    mag[n++] = (uint32_t)m;
    mag[n++] = (uint32_t)(m >> 32);
    n = mag_trim(mag, n);

    uint32_t dec_shift = 0;
    if (e >= 0) {
        mag_shl(mag, &n, (uint32_t)e);
    } else {
        static const uint32_t pow5[13] = {
            1u,        5u,        25u,       125u,      625u,
            3125u,     15625u,    78125u,    390625u,   1953125u,
            9765625u,  48828125u, 244140625u,
        };
        uint32_t k = (uint32_t)(-e);
        dec_shift = k;
        while (k >= 13u) {
            mag_mul_small(mag, &n, 1220703125u); /* 5^13 */
            k -= 13u;
        }
        if (k != 0) {
            mag_mul_small(mag, &n, pow5[k]);
        }
    }

    uint32_t groups[CORE_FLT_DIGITS_MAX / 9u + 2u];
    size_t gcount = 0;
    while (n > 0) {
        groups[gcount++] = mag_div_small(mag, &n, 1000000000u);
    }

    char digits[CORE_FLT_DIGITS_MAX];
    size_t len = 0;
    {
        uint32_t top = groups[gcount - 1];
        char tmp[10];
        size_t t = 0;
        while (top != 0) {
            tmp[t++] = (char)('0' + top % 10u);
            top /= 10u;
        }
        while (t > 0) {
            digits[len++] = tmp[--t];
        }
        for (size_t i = gcount - 1; i > 0; --i) {
            uint32_t grp = groups[i - 1];
            for (int d = 8; d >= 0; --d) {
                digits[len + (size_t)d] = (char)('0' + grp % 10u);
                grp /= 10u;
            }
            len += 9;
        }
    }

    int64_t dec_exp = (int64_t)len - 1 - (int64_t)dec_shift;

    uint64_t round = 0;
    size_t take = len < 14 ? len : 14;
    for (size_t i = 0; i < take; ++i) {
        round = round * 10u + (uint64_t)(digits[i] - '0');
    }
    int round_up = 0;
    if (len > 14) {
        char d15 = digits[14];
        if (d15 > '5') {
            round_up = 1;
        } else if (d15 == '5') {
            int rest_nonzero = 0;
            for (size_t i = 15; i < len; ++i) {
                if (digits[i] != '0') {
                    rest_nonzero = 1;
                    break;
                }
            }
            if (rest_nonzero || (round & 1u) != 0) {
                round_up = 1;
            }
        }
    }
    if (round_up) {
        round += 1;
        if (round == 100000000000000ull) {
            round = 10000000000000ull;
            dec_exp += 1;
        }
    }
    while (round != 0 && round % 10u == 0) {
        round /= 10u;
    }

    char rd[16] = {0};
    size_t nd = 0;
    {
        uint64_t t = round;
        while (t != 0) {
            rd[nd++] = (char)('0' + t % 10u);
            t /= 10u;
        }
        for (size_t i = 0; i < nd / 2; ++i) {
            char c = rd[i];
            rd[i] = rd[nd - 1 - i];
            rd[nd - 1 - i] = c;
        }
    }

    char out[CORE_FLOAT_TEXT_MAX];
    size_t o = 0;
    if (negative) {
        out[o++] = '-';
    }
    if (dec_exp < -4 || dec_exp >= 14) {
        out[o++] = rd[0];
        out[o++] = '.';
        if (nd == 1) {
            out[o++] = '0';
        } else {
            for (size_t i = 1; i < nd; ++i) {
                out[o++] = rd[i];
            }
        }
        out[o++] = 'E';
        int64_t ex = dec_exp;
        if (ex < 0) {
            out[o++] = '-';
            ex = -ex;
        } else {
            out[o++] = '+';
        }
        char et[4];
        size_t en = 0;
        if (ex == 0) {
            et[en++] = '0';
        }
        while (ex > 0) {
            et[en++] = (char)('0' + ex % 10);
            ex /= 10;
        }
        while (en > 0) {
            out[o++] = et[--en];
        }
    } else if (dec_exp >= 0) {
        size_t ip = (size_t)dec_exp + 1;
        if (nd <= ip) {
            for (size_t i = 0; i < nd; ++i) {
                out[o++] = rd[i];
            }
            for (size_t i = nd; i < ip; ++i) {
                out[o++] = '0';
            }
        } else {
            for (size_t i = 0; i < ip; ++i) {
                out[o++] = rd[i];
            }
            out[o++] = '.';
            for (size_t i = ip; i < nd; ++i) {
                out[o++] = rd[i];
            }
        }
    } else {
        out[o++] = '0';
        out[o++] = '.';
        for (int64_t i = 0; i < -dec_exp - 1; ++i) {
            out[o++] = '0';
        }
        for (size_t i = 0; i < nd; ++i) {
            out[o++] = rd[i];
        }
    }
    memcpy(buf, out, o);
    return o;
}
