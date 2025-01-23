#include "CMDParser.h"
#include "constants.h"

#include <stdio.h>
#include <string.h>

CMDdata parsecmd(int *argc, char* argv[]) {

    CMDdata data = {0, 0, 0, 0, 0};
    
    if (*argc <= 1) {
        printf(HELP_TEXT);
        return data;
    }

    for (unsigned i = 1; i < *argc; i++) {

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf(VER_NL);
            data.version_flag = true;
            break;
        } else
        
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf(HELP_TEXT);
            data.help_flag = true;
            break;
        }
        
        else {
            fprintf(stderr, BAD_ARGUMENT, argv[i]);
            data.error = true;
            break;
        }

    }

    return data;

}