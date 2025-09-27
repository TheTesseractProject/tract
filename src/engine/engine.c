#include "engine.h"

#include <stdio.h>

#include "file_check/file_check.h"
#include "code_parser/code_parser.h"
#include "buffer_manager/buffer_manager.h"
#include "constants.h"
//#include "tesseract_types.h"

bool engine_build_file(FILE *input_file, const char *input_path, const char *output_path) {
    if (!input_file) {
        ERRLN(TRACT_ACCESS_ERROR);
        return false;
    }

    MSGF(BUILDING_MSG, 1, 1, input_path);
    fflush(stdout);

    // Initialize file buffer
    size_t file_size;
    char* buffer = init_file_buffer(input_file, &file_size);
    if (buffer == NULL) {
        return false;
    }

    bool parse_success = tesseract_parse(buffer, file_size);

    // Clean up buffer
    cleanup_buffer(buffer);

    if (parse_success) {
        MSGLN(OK);
        MSGFLN(BUILDING_BUILT_MSG, output_path);
    }

    return parse_success;
}
