#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "file_check.h"
#include "constants.h"

bool file_access(const char* path) {
    return access(path, F_OK) == 0;
}

bool has_valid_extension(const char* path) {
    const char* ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }
    
    return (strcmp(ext, ".t") == 0 || 
            strcmp(ext, ".tes") == 0 || 
            strcmp(ext, ".tt") == 0);
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
