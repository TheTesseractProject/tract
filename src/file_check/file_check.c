#include "file_check.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "constants.h"

bool file_access(const char* path) {
    return access(path, F_OK) == 0;
}

bool has_valid_extension(const char* path) {
    static const char* const VALID_EXTENSIONS_LIST[] = {
        VALID_EXTENSIONS
    };
    const char* ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }
    for (unsigned long i = 0; i < sizeof(VALID_EXTENSIONS_LIST) / sizeof(VALID_EXTENSIONS_LIST[0]); ++i) {
        if (strcmp(ext, VALID_EXTENSIONS_LIST[i]) == 0) {
            return true;
        }
    }
    return false;
}

FILE* file_open(const char* path) {
    if (!file_access(path)) {
        fprintf(stderr, ERR_FILE_UNABLE_ACCESS_MSG, path);
        return NULL;
    }
    
    if (!has_valid_extension(path)) {
        fprintf(stderr, ERR_INVALID_EXTENSION_MSG, path);
        return NULL;
    }
    
    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, ERR_FILE_UNABLE_ACCESS_MSG, path);
        return NULL;
    }
    return file;
}
