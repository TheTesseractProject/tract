#pragma once

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

cmd_data parse_cmd(int *argc, char* argv[]);
