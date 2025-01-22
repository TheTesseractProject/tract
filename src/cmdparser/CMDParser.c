#include "CMDParser.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "constants.h"

bool parsecmd(int *argc, char* argv[]) {
    
    if (*argc <= 1) {
        printf(HELP_TEXT);
        return false;
    }

    for (unsigned i = 1; i < *argc; i++) {

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("Tract ver: %s\n", TRACT_VERSION);
            return false;
        } else
        
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf(HELP_TEXT);
            return false;
        }
        
        else {
            fprintf(stderr, BAD_ARGUMENT, argv[i]);
            return true;
        }

    }

    return false;

}
