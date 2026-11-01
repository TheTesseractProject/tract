#ifndef CORE_VALUE_H
#define CORE_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TESS_VAL_NULL = 0,
    TESS_VAL_BOOL,
    TESS_VAL_INT,
    TESS_VAL_FLOAT,
    TESS_VAL_STR,
    TESS_VAL_OBJECT,
    TESS_VAL_HANDLE
} tess_val_tag;

typedef struct {
    uint32_t tag;
    uint32_t len;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        uint32_t offset;
    } u;
} tess_value;

_Static_assert(sizeof(tess_value) == 16, "tess_value must be 16 bytes");

tess_value tess_value_null(void);
tess_value tess_value_bool(bool value);
tess_value tess_value_int(int64_t value);
tess_value tess_value_float(double value);
tess_value tess_value_handle(uint32_t module_id, uint32_t handle);

int tess_value_truthy(const tess_value *value);
int tess_value_equal(const tess_value *a, const tess_value *b);

#endif /* CORE_VALUE_H */
