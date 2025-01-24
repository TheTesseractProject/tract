#ifndef CMDPARSER_H
#define CMDPARSER_H

#include <stdbool.h>

typedef struct {
    bool error;
    bool version_flag;
    bool help_flag;
    bool build_flag;
    unsigned path_argument;
} CMDdata;

CMDdata parsecmd(int *argc, char* argv[]);

#endif