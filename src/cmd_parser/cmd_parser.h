#pragma once

#include <stdbool.h>

typedef enum {
    ERROR,
    HELP,
    VERSION,
    BUILD_FILE
} cmd_action;

typedef struct {
    cmd_action action;
    char* path;
    char* output;
    char* c_compiler;
    char* target_triple;
    bool check_only;  /* -c/--check: parse + semantic analysis only     */
    bool to_stdout;   /* --stdout / -o -: write generated C to stdout    */
    bool compile;     /* --compile: compile generated C into an executable */
    bool freestanding;
    bool quiet;       /* -q/--quiet: suppress progress messages          */
} cmd_data;

cmd_data parse_cmd(int argc, char* argv[]);
