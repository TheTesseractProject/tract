#include <stdio.h>
#include "cmdparser/CMDParser.h"

int main(int argc, char* argv[]) {
    bool cmdresult = parsecmd(&argc, argv);
    printf("%d\n", get_test());
    return cmdresult;
}