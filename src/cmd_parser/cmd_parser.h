#ifndef CMD_PARSER_H
#define CMD_PARSER_H

typedef enum {
    ERROR,
    HELP,
    VERSION,
    BUILD
} cmd_action;

typedef struct {
    cmd_action action;
    char* path;
} cmd_data;

cmd_data parse_cmd(int *argc, char* argv[]);

#endif