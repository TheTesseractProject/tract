#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "colors.h"

#define VER "alpha0.0"

#define TRACT COLOR_BRIGHT_MAGENTA "TRACT" COLOR_RESET
#define TRACT_COLON TRACT ": "
#define ERROR_MSG COLOR_BRIGHT_RED "Error" COLOR_RESET ": "
#define OK COLOR_GREEN "Ok" COLOR_RESET "\n"

#define BAD_ARGUMENT_MSG ERROR_MSG "'" COLOR_RED "%s" COLOR_RESET "'" COLOR_RESET " is not a recognized argument. See '-h' for help\n"
#define FILE_UNABLE_ACCESS_MSG ERROR_MSG "Unable to access file '" COLOR_RED "%s" COLOR_RESET "'\n"
#define INVALID_EXTENSION_MSG ERROR_MSG "File '" COLOR_RED "%s" COLOR_RESET "' does not have a valid extension (.t, .tes, or .tt)\n"
#define BUILD_FILE_NOT_SPECIFIED_MSG ERROR_MSG "Build file not specified\n"
#define OUTPUT_FILE_NOT_SPECIFIED_MSG ERROR_MSG "Output file not specified\n"
#define MULTIPLE_BUILD_MSG ERROR_MSG "Attempt to specify multiple files for build\n"
#define MULTIPLE_OUTPUT_MSG ERROR_MSG "Attempt to specify multiple files for output\n"

#define AN_ERROR_OCCURRED TRACT_COLON COLOR_RED "An error occurred while "
#define ARGS_ERROR AN_ERROR_OCCURRED "parsing arguments" COLOR_RESET "\n"
#define ACCESS_ERROR AN_ERROR_OCCURRED "accessing the file" COLOR_RESET "\n"
#define BUILD_ERROR AN_ERROR_OCCURRED "building the file" COLOR_RESET "\n"

#define BUILD_PLACEHOLDER "<BUILD PLACEHOLDER>\n"

#define HELP_TEXT "\n" COLOR_BRIGHT_WHITE "About" COLOR_RESET "\n" TRACT " - The " COLOR_BRIGHT_WHITE "Tesseract" COLOR_RESET " compiler.\nLightning-fast compilation and\naggressive optimization for peak performance.\nMade with " COLOR_BRIGHT_RED "LOVE" COLOR_RESET " by " COLOR_BRIGHT_WHITE "@pka_human" COLOR_RESET ".\n\n" COLOR_BRIGHT_WHITE "Available options" COLOR_RESET "\n-v, --version: Show version\n-h, --help: Show help text\n-b, --build <path>: Build the program (or just specify the path without arguments)\n-o, --output <path>: Specify the path to the compiled file\n\n" COLOR_BRIGHT_WHITE "Usage" COLOR_RESET "\n" COLOR_GRAY "tract <args>" COLOR_RESET "\n\n"

#endif
