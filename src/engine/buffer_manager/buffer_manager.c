#include "buffer_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

char* init_file_buffer(FILE *input_file, size_t *file_size) {
    if (input_file == NULL) {
        return NULL;
    }

    // Get file size
    fseek(input_file, 0, SEEK_END);
    long size = ftell(input_file);
    if (size < 0) {
        fputs("Error: Could not determine file size\n", stderr);
        return NULL;
    }
    
    rewind(input_file);
    
    // Allocate buffer
    char* buffer = (char*)malloc(size + 1);
    if (buffer == NULL) {
        fputs("Error: Memory allocation failed for file buffer\n", stderr);
        return NULL;
    }
    
    // Read file into buffer
    size_t bytes_read = fread(buffer, 1, size, input_file);
    if (bytes_read != (size_t)size) {
        free(buffer);
        fputs("Error: Failed to read entire file\n", stderr);
        return NULL;
    }
    
    buffer[size] = '\0';
    *file_size = (size_t)size;
    
    return buffer;
}

void cleanup_buffer(char* buffer) {
    if (buffer != NULL) {
        free(buffer);
    }
}
