#include "engine.h"

#include <stdio.h>
#include <stdbool.h>

#include "file_check/file_check.h"
#include "code_parser/code_parser.h"
#include "constants.h"
//#include "tesseract_types.h"

bool engine_build_file(FILE *input_file, const char *input_path, const char *output_path) {
    if (!input_file) {
        ERR(TRACT_ACCESS_ERROR);
        return false;
    }

    MSGF(BUILDING_MSG, 1, 1, input_path);
    fflush(stdout);

    bool parse_success = tesseract_parse(input_file);

    if (parse_success) {
        MSG(OK);
        MSGF(BUILDING_BUILT_MSG, output_path);
    }

    return parse_success;
}
