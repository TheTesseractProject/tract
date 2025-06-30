#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "file_check.h"
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

bool file_check(const char* path) {
    if (!file_access(path)) {
        fprintf(stderr, FILE_UNABLE_ACCESS_MSG, path);
        return false;
    }
    
    if (!has_valid_extension(path)) {
        fprintf(stderr, INVALID_EXTENSION_MSG, path);
        return false;
    }
    
    return true;
}
