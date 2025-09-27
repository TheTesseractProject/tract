#include "file_check.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "constants.h"

static bool file_access(const char* path) {
    return access(path, F_OK) == 0;
}

static bool has_valid_extension(const char* path) {
    static const char* const valid_extensions_list[] = {
        VALID_EXTENSIONS
    };
    const char* ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }
    for (unsigned long i = 0; i < sizeof(valid_extensions_list) / sizeof(valid_extensions_list[0]); i++) {
        if (strcmp(ext, valid_extensions_list[i]) == 0) {
            return true;
        }
    }
    return false;
}

FILE* file_open(const char* path) {
    if (!file_access(path)) {
        ERRFLN(ERR_FILE_UNABLE_ACCESS, path);
        return NULL;
    }
    
    if (!has_valid_extension(path)) {
        ERRFLN(ERR_INVALID_EXTENSION, path);
        return NULL;
    }
    
    FILE* file = fopen(path, "r");
    if (!file) {
        ERRFLN(ERR_FILE_UNABLE_ACCESS, path);
        return NULL;
    }
    return file;
}
