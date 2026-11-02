#include "buffer_manager.h"
#include "constants.h"

#include <stdio.h>

#include "core_alloc.h"

char* init_file_buffer(FILE *input_file, size_t *file_size) {
    if (input_file == NULL) {
        return NULL;
    }

    // Get file size
    if (fseek(input_file, 0, SEEK_END) != 0) {
        ERRLN(ERR_FILE_SIZE_ERROR);
        return NULL;
    }
    long size = ftell(input_file);
    if (size < 0) {
        ERRLN(ERR_FILE_SIZE_ERROR);
        return NULL;
    }
    if ((unsigned long)size > TERRA_MAX_SOURCE_SIZE) {
        ERRLN(ERR_SOURCE_TOO_LARGE);
        return NULL;
    }
    if (fseek(input_file, 0, SEEK_SET) != 0) {
        ERRLN(ERR_FILE_SIZE_ERROR);
        return NULL;
    }

    // Allocate buffer
    char* buffer = (char*)core_alloc((size_t)size + 1);
    if (buffer == NULL) {
        ERRLN(ERR_MEMORY_ALLOCATION_ERROR);
        return NULL;
    }

    // Read the whole file; a short read is an error rather than silently
    // compiling a truncated program.
    size_t total = 0;
    while (total < (size_t)size) {
        size_t n = fread(buffer + total, 1, (size_t)size - total, input_file);
        if (n == 0) break;
        total += n;
    }
    if (total != (size_t)size || ferror(input_file)) {
        core_free(buffer);
        ERRLN(ERR_FILE_READ_ERROR);
        return NULL;
    }

    buffer[total] = '\0';
    *file_size = total;

    return buffer;
}

void cleanup_buffer(char* buffer) {
    if (buffer != NULL) {
        core_free(buffer);
    }
}
