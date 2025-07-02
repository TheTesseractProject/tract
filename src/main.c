#include <stdio.h>

#include "constants.h"
#include "tesseract_types.h"
#include "cmd_parser/cmd_parser.h"
#include "tesseract_animation/tesseract_animation.h"
#include "file_check/file_check.h"
#include "engine/engine.h"

int main(int argc, char *argv[]) {
    cmd_data cmd_result = parse_cmd(&argc, argv);

    switch (cmd_result.action) {
        case ERROR: {
            fputs(ARGS_ERROR, stderr);
            return 1;
        }
        case HELP: {
            fputs(HELP_TEXT, stdout);
            break;
        }
        case VERSION: {
            fputs(VER "\n", stdout);
            break;
        }
        case BUILD_FILE: {
            FILE *input_file = file_open(cmd_result.path);
            if (!input_file) {
                fputs(ACCESS_ERROR, stderr);
                return 1;
            }

            bool build_success = engine_build_file(input_file, cmd_result.path, cmd_result.output);
            fclose(input_file);

            if (!build_success) {
                fputs(BUILD_ERROR, stderr);
                return 1;
            }

            fputs(TRACT_BUILD_FINISHED, stdout);
            break;
        }
        case ANIMATION: {
            tesseract_animation();
            break;
        }
    }

    return 0;
}
