#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
    TES_FUNCTION,
    TES_NUMBER_INTEGER,
    TES_NUMBER_FLOAT,
    TES_STRING,
    TES_BOOLEAN,
    TES_NULL,
    TES_UNDEFINED,
    TES_VARIABLE,
} tesseract_token_type;

typedef struct tesseract_token {
    size_t start;
    size_t end;
    struct tesseract_token **args;
    uint8_t args_count;
    tesseract_token_type type;
} tesseract_token;
