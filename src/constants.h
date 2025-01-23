#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "colors.h"

#define TRACT COLOR_BRIGHT_MAGENTA "TRACT" COLOR_RESET ": "
#define ERROR COLOR_RED "Error" COLOR_RESET
#define ERROR_COLON ERROR ": "
#define VER "alpha0.0"
#define VER_NL VER "\n"
#define OK COLOR_GREEN "Ok" COLOR_RESET
#define OK_NL OK "\n"
#define HELP_TEXT COLOR_CYAN "HELP" COLOR_RESET ":" COLOR_BRIGHT_WHITE " <APP INFO>\nAvailable options" COLOR_RESET ":\n-v, --version: Show version\n-h, --help: Show help text\n"
#define BAD_ARGUMENT "'" COLOR_RED "%s" COLOR_RESET "'" COLOR_GRAY " <- This in not a valid argument! Read '-h'.\n" COLOR_RESET
#define ARGS_ERROR ERROR_COLON COLOR_BRIGHT_WHITE "An error occurred while parsing commands" COLOR_RESET ".\n"

#endif
