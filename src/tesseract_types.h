#ifndef TESSERACT_TYPES_H
#define TESSERACT_TYPES_H

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

#endif
