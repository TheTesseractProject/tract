#include <stdio.h>

#include "constants.h"
#include "cmd_parser/cmd_parser.h"
#include "file_check/file_check.h"

int main(int argc, char* argv[]) {
    cmd_data cmdresult = parse_cmd(&argc, argv);

    switch (cmdresult.action) {
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
            if (!file_check(cmdresult.path)) {
                fputs(ACCESS_ERROR, stderr);
                return 1;
            }
            
            fputs(BUILD_PLACEHOLDER, stdout);
            fputs(TRACT_COLON OK, stdout);
            break;
    }

    return 0;
}
