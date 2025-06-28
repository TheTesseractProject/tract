#include <stdio.h>

#include "constants.h"
#include "cmd_parser/cmd_parser.h"
#include "file_check/file_check.h"
#include "tesseract_animation/tesseract_animation.h"

int main(int argc, char* argv[]) {
    cmd_data cmd_result = parse_cmd(&argc, argv);

    switch (cmd_result.action) {
        case ERROR:
            fputs(ARGS_ERROR, stderr);
            return 1;
        case HELP:
            fputs(HELP_TEXT, stdout);
            break;
        case VERSION:
            fputs(VER "\n", stdout);
            break;
        case BUILD:
            if (!file_check(cmd_result.path)) {
                fputs(ACCESS_ERROR, stderr);
                return 1;
            }
            fputs(BUILD_PLACEHOLDER, stdout);
            fputs(cmd_result.output, stdout);
            putchar('\n');
            fputs(TRACT_COLON OK, stdout);
            break;
        case ANIMATION:
            tesseract_animation();
            break;
    }

    return 0;
}
