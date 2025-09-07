#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <stdio.h>
#include <stdbool.h>

char* init_file_buffer(FILE *input_file, size_t *file_size);
void cleanup_buffer(char* buffer);

#endif
