#pragma once

#include <stdio.h>

char* init_file_buffer(FILE *input_file, size_t *file_size);
void cleanup_buffer(char* buffer);
