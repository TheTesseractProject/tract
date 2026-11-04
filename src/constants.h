#pragma once

#include "colors.h"

#define MSG(str) fputs(str, stdout)
#define MSGF(str, ...) printf(str, __VA_ARGS__)
#define ERR(str) fputs(str, stderr)
#define ERRF(str, ...) fprintf(stderr, str, __VA_ARGS__)
#define MSGLN(str) fputs(str, stdout); fputc('\n', stdout)
#define MSGFLN(str, ...) printf(str, __VA_ARGS__); fputc('\n', stdout)
#define ERRLN(str) fputs(str, stderr); fputc('\n', stderr)
#define ERRFLN(str, ...) fprintf(stderr, str, __VA_ARGS__); fputc('\n', stderr)

#define VER "alpha0.0"

#define TRACT COLOR_BRIGHT_MAGENTA "TRACT" COLOR_RESET
#define TRACT_COLON TRACT ": "
#define ERROR_MSG COLOR_BRIGHT_RED "Error" COLOR_RESET ": "
#define WARNING_MSG COLOR_BRIGHT_YELLOW "Warning" COLOR_RESET ": "
#define OK COLOR_GREEN "Ok" COLOR_RESET

#define VALID_EXTENSIONS ".t", ".tes", ".tt", ".T", ".TES", ".TT"

#define BUILDING_MSG "[%d/%d] Building '" COLOR_BRIGHT_WHITE "%s" COLOR_RESET "'... "
#define BUILDING_BUILT_MSG "Built to '" COLOR_BRIGHT_WHITE "%s" COLOR_RESET "'"
#define TRACT_BUILD_FINISHED TRACT_COLON COLOR_GREEN "Build finished" COLOR_RESET

#define ERR_BAD_ARGUMENT ERROR_MSG "'" COLOR_RED "%s" COLOR_RESET "'" COLOR_RESET " is not a recognized argument. See '-h' for help"
#define ERR_FILE_UNABLE_ACCESS ERROR_MSG "Unable to access file '" COLOR_RED "%s" COLOR_RESET "'"
#define ERR_INVALID_EXTENSION ERROR_MSG "File '" COLOR_RED "%s" COLOR_RESET "' does not have a valid extension (.t, .tes, or .tt)"
#define ERR_BUILD_FILE_NOT_SPECIFIED ERROR_MSG "Build file not specified"
#define ERR_OUTPUT_FILE_NOT_SPECIFIED ERROR_MSG "Output file not specified"
#define ERR_MULTIPLE_BUILD ERROR_MSG "Cannot specify multiple files for build, build a single file or the entire project directory"
#define ERR_MULTIPLE_OUTPUT ERROR_MSG "Attempt to specify multiple files for output"

// Buffer manager errors
#define ERR_FILE_SIZE_ERROR ERROR_MSG "Could not determine file size"
#define ERR_MEMORY_ALLOCATION_ERROR ERROR_MSG "Memory allocation failed for file buffer"
#define ERR_FILE_READ_ERROR ERROR_MSG "Failed to read entire file"

// Code parser warnings
#define WARN_DEBUG_LOG WARNING_MSG "Could not open debug.log for writing"
#define WARN_PARSER_UNEXPECTED_CHAR WARNING_MSG "Unexpected '%c'"
#define WARN_PARSER_UNEXPECTED_DELIMITER WARNING_MSG "Unexpected delimiter '%c', expected '%c'"
#define WARN_PARSER_UNEXPECTED_DELIMITER_BEFORE_END WARNING_MSG "Unexpected delimiter before end of arguments"
#define WARN_PARSER_COMMA_EXPECTED WARNING_MSG "',' expected before new token"
#define WARN_PARSER_SEMICOLON_EXPECTED WARNING_MSG "';' expected before new token"
#define WARN_PARSER_DELIMITER_EXPECTED WARNING_MSG "Expected delimiter before end of file"
#define WARN_PARSER_UNTERMINATED_ARGS WARNING_MSG "Unterminated argument list detected at end of file"

#define AN_ERROR_OCCURRED_WHILE TRACT_COLON COLOR_RED "An error occurred while "
#define TRACT_ARGS_ERROR AN_ERROR_OCCURRED_WHILE "parsing arguments" COLOR_RESET
#define TRACT_ACCESS_ERROR AN_ERROR_OCCURRED_WHILE "accessing the file" COLOR_RESET
#define TRACT_BUILD_ERROR AN_ERROR_OCCURRED_WHILE "building the file" COLOR_RESET

#define HELP_TEXT "\n"\
\
COLOR_BRIGHT_WHITE "About" COLOR_RESET "\n"\
TRACT " - The " COLOR_BRIGHT_WHITE "Tesseract" COLOR_RESET " compiler.\n"\
"Lightning-fast compilation and\n"\
"aggressive optimization for peak performance.\n"\
"Made with " COLOR_BRIGHT_RED "LOVE" COLOR_RESET " by " COLOR_BRIGHT_WHITE "@pka_human" COLOR_RESET ".\n\n"\
\
COLOR_BRIGHT_WHITE "Available options" COLOR_RESET "\n"\
"-v, --version: Show version\n"\
"-h, --help: Show help text\n"\
"-b, --build <path>: Build the program (or just specify the path without arguments)\n"\
"-o, --output <path>: Specify the path to the compiled file\n--tesseract: Beautiful ASCII 4D tesseract animation\n\n"\
\
COLOR_BRIGHT_WHITE "Usage" COLOR_RESET "\n"\
COLOR_GRAY "tract <args>" COLOR_RESET "\n\n"

#define TESSERACT_ANIMATION_TEXT TRACT " BUILD TOOL FOR " COLOR_BRIGHT_WHITE "TESSERACT" COLOR_RESET "\nVersion: " COLOR_BRIGHT_WHITE VER COLOR_RESET ", UwU! (Quit - 'Q')"
#define TESSERACT_ANIMATION_CHAR_ASPECT_TEXT "Use left/right arrow keys to adjust\nthe width until it looks like a square.\nPress Enter when it's right.\n\n"
#define TESSERACT_ANIMATION_TOO_SMALL "Too small :("
#define TESSERACT_ANIMATION_GRADIENT "@&Xx+;:,. "
#define TESSERACT_ANIMATION_END \
"                            ,--.\n"\
"                           {    }\n"\
"                           K,   }\n"\
"                          /  ~Y`\n"\
"                     ,   /   /\n"\
"                    {_'-K.__/\n"\
"                      `/-.__L._\n"\
"                      /  ' /`\\_}\n"\
"                     /  ' /\n"\
"             ____   /  ' /\n"\
"      ,-'~~~~    ~~/  ' /_\n"\
"    ,'             ``~~~  ',\n"\
"   (                        Y\n"\
"  {                         I\n"\
" {      -                    `,\n"\
" |       ',                   )\n"\
" |        |   ,..__      __. Y\n"\
" |    .,_./  Y ' / ^Y   J   )|\n"\
" \\           |' /   |   |   ||\n"\
"  \\          L_/    . _ (_,.'(\n"\
"   \\,   ,      ^^\"\"' / |      )\n"\
"     \\_  \\          /,L]     /\n"\
"       '-_~-,       ` `   ./`\n"\
"          `'{_            )\n"\
"              ^^\\..___,.--`\n\n"
