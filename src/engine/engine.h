#ifndef ENGINE_H
#define ENGINE_H

#include <stdio.h>
#include <stdbool.h>

bool engine_build_file(FILE *input_file, const char *input_path, const char *output_path);

#endif
