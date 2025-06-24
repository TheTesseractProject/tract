#include <stdio.h>
#include <string.h>

#include "cmd_parser.h"
#include "constants.h"

cmd_data parse_cmd(int *argc, char* argv[]) {
    cmd_data data;
    data.action = BUILD;
    data.path = NULL;

    if (*argc <= 1) {
        data.action = HELP;
        return data;
    }

    for (unsigned i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            data.action = VERSION;
            return data;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            data.action = HELP;
            return data;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0) {
            if (i == *argc - 1) {
                fputs(BUILD_FILE_NOT_SPECIFIED, stderr);
                data.action = ERROR;
                return data;
            } else {
                break;
            }
        } else if (i != *argc - 1 || argv[i][0] == '-') {
            fprintf(stderr, BAD_ARGUMENT, argv[i]);
            data.action = ERROR;
            return data;
        }
    }

    data.path = argv[*argc - 1];

    return data;
}