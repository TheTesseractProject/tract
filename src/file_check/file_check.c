#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "file_check.h"
#include "constants.h"

bool file_exists(const char* path) {
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
    if (path[0] == '-') {
        fputs(BUILD_FILE_NOT_SPECIFIED, stderr);
        return false;
    }

    if (!file_exists(path)) {
        fprintf(stderr, FILE_NOT_EXIST, path);
        return false;
    }
    
    if (!has_valid_extension(path)) {
        fprintf(stderr, INVALID_EXTENSION, path);
        return false;
    }
    
    return true;
}