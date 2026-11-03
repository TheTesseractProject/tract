#include <stdio.h>

#include "constants.h"
#include "cmd_parser/cmd_parser.h"
#include "tesseract_animation/tesseract_animation.h"
#include "file_check/file_check.h"
#include "engine/engine.h"

int main(int argc, char *argv[]) {
    cmd_data cmd_result = parse_cmd(argc, argv);

    switch (cmd_result.action) {
        case ERROR: {
            ERRLN(TERRA_ARGS_ERROR);
            return 1;
        }
        case HELP: {
            MSG(HELP_TEXT);
            break;
        }
        case VERSION: {
            MSG(VERSION_TEXT);
            break;
        }
        case BUILD_FILE: {
            if (cmd_result.path == NULL) {
                ERRLN(ERR_BUILD_FILE_NOT_SPECIFIED);
                return 1;
            }
            
            FILE *input_file = file_open(cmd_result.path);
            if (!input_file) {
                ERRLN(TERRA_ACCESS_ERROR);
                return 1;
            }

            terra_build_options options = {
                .check_only = cmd_result.check_only,
                .to_stdout = cmd_result.to_stdout,
                .quiet = cmd_result.quiet,
            };
            bool build_success = engine_build_file(input_file, cmd_result.path,
                                                   cmd_result.output, &options);
            fclose(input_file);

            if (!build_success) {
                ERRLN(TERRA_BUILD_ERROR);
                return 1;
            }
            
            break;
        }
        case ANIMATION: {
            tesseract_animation();
            break;
        }
    }

    return 0;
}
