#include "file_check.h"

#include <string.h>
#include <stdio.h>

#include "constants.h"

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
    if (path == NULL) {
        return NULL;
    }

    if (!has_valid_extension(path)) {
        ERRFLN(ERR_INVALID_EXTENSION, path);
        return NULL;
    }

    // Binary mode keeps file sizes and line endings identical on every
    // platform (no CRLF translation on Windows).
    FILE* file = fopen(path, "rb");
    if (!file) {
        ERRFLN(ERR_FILE_UNABLE_ACCESS, path);
        return NULL;
    }
    return file;
}
