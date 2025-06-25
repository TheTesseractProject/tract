#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cmd_parser.h"
#include "constants.h"

cmd_data parse_cmd(int *argc, char* argv[]) {
    cmd_data data;
    data.action = BUILD;
    data.path = NULL;
    data.output = NULL;

    if (*argc <= 1) {
        data.action = HELP;
        return data;
    }


    for (unsigned i = 1; i < *argc; i++) {
        if (argv[i][0] != '-') {
            if (data.path == NULL) {
                data.path = argv[i];
                continue;
            } else {
                fprintf(stderr, BAD_ARGUMENT_MSG, argv[i]);
                data.action = ERROR;
                return data;
            }
        }

        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            data.action = VERSION;
            return data;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            data.action = HELP;
            return data;
        } else if (strcmp(argv[i], "--tesseract") == 0) {
            data.action = ANIMATION;
            return data;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0) {
            if (data.path != NULL) {
                fputs(MULTIPLE_BUILD_MSG, stderr);
                data.action = ERROR;
                return data;
            } else if (i + 1 < *argc && argv[i + 1][0] != '-') {
                data.path = argv[i + 1];
                i++;
                continue;
            } else {
                fputs(BUILD_FILE_NOT_SPECIFIED_MSG, stderr);
                data.action = ERROR;
                return data;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (data.output != NULL) {
                fputs(MULTIPLE_OUTPUT_MSG, stderr);
                data.action = ERROR;
                return data;
            } else if (i + 1 < *argc && argv[i + 1][0] != '-') {
                data.output = argv[i + 1];
                i++;
                continue;
            } else {
                fputs(OUTPUT_FILE_NOT_SPECIFIED_MSG, stderr);
                data.action = ERROR;
                return data;
            }
        } else {
            fprintf(stderr, BAD_ARGUMENT_MSG, argv[i]);
            data.action = ERROR;
            return data;
        }
    }

    if (data.output == NULL) {
        data.output = "output";
    }

    return data;
}