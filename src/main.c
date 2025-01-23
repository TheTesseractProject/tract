#include <stdio.h>
#include "cmdparser/CMDParser.h"
#include "constants.h"

int main(int argc, char* argv[]) {
    CMDdata cmdresult = parsecmd(&argc, argv);

    if (cmdresult.error) {
        fprintf(stderr, TRACT ARGS_ERROR);
        return 1;
    }

    //printf(TRACT OK_NL);

    return 0;
}
