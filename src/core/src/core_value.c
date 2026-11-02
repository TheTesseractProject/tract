#include "core_value.h"

tess_value tess_value_null(void) {
    tess_value v = {0};
    v.tag = TESS_VAL_NULL;
    v.len = 0;
    v.u.integer = 0;
    return v;
}

tess_value tess_value_bool(bool value) {
    tess_value v = {0};
    v.tag = TESS_VAL_BOOL;
    v.u.boolean = value;
    return v;
}

tess_value tess_value_int(int64_t value) {
    tess_value v = {0};
    v.tag = TESS_VAL_INT;
    v.u.integer = value;
    return v;
}

tess_value tess_value_float(double value) {
    tess_value v = {0};
    v.tag = TESS_VAL_FLOAT;
    v.u.floating = value;
    return v;
}

tess_value tess_value_handle(uint32_t module_id, uint32_t handle) {
    tess_value v = {0};
    v.tag = TESS_VAL_HANDLE;
    /* Pack the module id in `len` and the handle in `offset`. */
    v.len = module_id;
    v.u.offset = handle;
    return v;
}

int tess_value_truthy(const tess_value *value) {
    if (value == NULL) return 0;
    switch (value->tag) {
        case TESS_VAL_NULL:
            return 0;
        case TESS_VAL_BOOL:
            return value->u.boolean ? 1 : 0;
        case TESS_VAL_INT:
            return value->u.integer != 0;
        case TESS_VAL_FLOAT:
            return value->u.floating != 0.0;
        case TESS_VAL_STR:
            return value->len != 0;
        case TESS_VAL_OBJECT:
        case TESS_VAL_HANDLE:
            return value->u.offset != 0;
        default:
            return 0;
    }
}

int tess_value_equal(const tess_value *a, const tess_value *b) {
    if (a == NULL || b == NULL) return a == b;
    if (a->tag != b->tag) {
        /* Numeric cross-tag comparison. */
        bool a_num = a->tag == TESS_VAL_INT || a->tag == TESS_VAL_FLOAT;
        bool b_num = b->tag == TESS_VAL_INT || b->tag == TESS_VAL_FLOAT;
        if (a_num && b_num) {
            double av = a->tag == TESS_VAL_INT ? (double)a->u.integer
                                               : a->u.floating;
            double bv = b->tag == TESS_VAL_INT ? (double)b->u.integer
                                               : b->u.floating;
            return av == bv;
        }
        return 0;
    }

    switch (a->tag) {
        case TESS_VAL_NULL:
            return 1;
        case TESS_VAL_BOOL:
            return a->u.boolean == b->u.boolean;
        case TESS_VAL_INT:
            return a->u.integer == b->u.integer;
        case TESS_VAL_FLOAT:
            return a->u.floating == b->u.floating;
        case TESS_VAL_STR:
            /* String storage is external; compare the recorded length only. */
            return a->len == b->len;
        case TESS_VAL_OBJECT:
        case TESS_VAL_HANDLE:
            return a->len == b->len && a->u.offset == b->u.offset;
        default:
            return 0;
    }
}
