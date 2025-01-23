#ifndef TYPEDEFS_H
#define TYPEDEFS_H

#include <stdbool.h>

typedef struct {
    bool error;
    bool version_flag;
    bool help_flag;
    bool build_flag;
    unsigned path_argument;
} CMDdata;

#endif