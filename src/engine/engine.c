#include "engine.h"

#include <stdio.h>
#include <stdbool.h>

#include "file_check/file_check.h"
#include "constants.h"
//#include "tesseract_types.h"

bool engine_build_file(FILE *input_file, const char *input_path, const char *output_path) {
    if (!input_file) {
        fputs(ACCESS_ERROR, stderr);
        return false;
    }

    printf(BUILDING_MSG, 1, 1, input_path);
    fflush(stdout);

    //parse(input_file);

    fputs(OK, stdout);
    printf(BUILDING_BUILT_MSG, output_path);

    return true;
}
