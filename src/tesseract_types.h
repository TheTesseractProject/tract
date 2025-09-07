#ifndef TESSERACT_TYPES_H
#define TESSERACT_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    ERROR,
    HELP,
    VERSION,
    BUILD_FILE,
    ANIMATION
} cmd_action;

typedef struct {
    cmd_action action;
    char* path;
    char* output;
} cmd_data;

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

#endif
