#pragma once

#include "colors.h"

#define MSG(str) fputs(str, stdout)
#define MSGF(str, ...) printf(str, __VA_ARGS__)
#define ERR(str) fputs(str, stderr)
#define ERRF(str, ...) fprintf(stderr, str, __VA_ARGS__)
#define MSGLN(str) do { fputs(str, stdout); fputc('\n', stdout); } while(0)
#define MSGFLN(str, ...) do { printf(str, __VA_ARGS__); fputc('\n', stdout); } while(0)
#define ERRLN(str) do { fputs(str, stderr); fputc('\n', stderr); } while(0)
#define ERRFLN(str, ...) do { fprintf(stderr, str, __VA_ARGS__); fputc('\n', stderr); } while(0)

#define TERRA_CODENAME "alpha"

#if defined(_WIN32)
#define TERRA_PLATFORM "Windows"
#elif defined(__APPLE__)
#define TERRA_PLATFORM "macOS"
#elif defined(__linux__)
#define TERRA_PLATFORM "Linux"
#elif defined(__FreeBSD__)
#define TERRA_PLATFORM "FreeBSD"
#else
#define TERRA_PLATFORM "unknown"
#endif

#if defined(__clang__)
#define TERRA_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#define TERRA_COMPILER "GCC " __VERSION__
#elif defined(_MSC_VER)
#define TERRA_COMPILER "MSVC"
#else
#define TERRA_COMPILER "unknown"
#endif

#define VERSION_TEXT \
    TERRA " " TERRA_VERSION " (" TERRA_CODENAME ")\n" \
    "Tesseract compiler (prototype)\n" \
    "Backend: C99\n" \
    "Source language: Tesseract\n" \
    "Built with: " TERRA_COMPILER "\n" \
    "Platform: " TERRA_PLATFORM "\n"

#define TERRA COLOR_BRIGHT_MAGENTA "TERRA" COLOR_RESET
#define TERRA_COLON TERRA ": "
#define ERROR_MSG COLOR_BRIGHT_RED "Error" COLOR_RESET ": "
#define WARNING_MSG COLOR_BRIGHT_YELLOW "Warning" COLOR_RESET ": "
#define OK COLOR_GREEN "Ok" COLOR_RESET

#define VALID_EXTENSIONS ".t", ".tes", ".tt", ".T", ".TES", ".TT"

#define BUILDING_MSG "[%d/%d] Building '" COLOR_BRIGHT_WHITE "%s" COLOR_RESET "'... "
#define BUILDING_BUILT_MSG "Built to '" COLOR_BRIGHT_WHITE "%s" COLOR_RESET "'"
#define TERRA_BUILD_FINISHED TERRA_COLON COLOR_GREEN "Build finished" COLOR_RESET

#define ERR_BAD_ARGUMENT ERROR_MSG "'" COLOR_RED "%s" COLOR_RESET "' is not a recognized argument. See '-h' for help"
#define ERR_FILE_UNABLE_ACCESS ERROR_MSG "Unable to access file '" COLOR_RED "%s" COLOR_RESET "'"
#define ERR_INVALID_EXTENSION ERROR_MSG "File '" COLOR_RED "%s" COLOR_RESET "' does not have a valid extension (.t, .tes, or .tt)"
#define TERRA_MAX_SOURCE_SIZE 1048576u /* 1 MB safety limit for source files */
#define ERR_SOURCE_TOO_LARGE ERROR_MSG "Source file is too large (max 1 MB)"
#define ERR_BUILD_FILE_NOT_SPECIFIED ERROR_MSG "Build file not specified"
#define ERR_OUTPUT_FILE_NOT_SPECIFIED ERROR_MSG "Output file not specified"
#define ERR_MULTIPLE_BUILD ERROR_MSG "Cannot specify multiple files for build, build a single file or the entire project directory"
#define ERR_MULTIPLE_OUTPUT ERROR_MSG "Attempt to specify multiple files for output"
#define ERR_OUTPUT_FILE_CREATE ERROR_MSG "Unable to create output file '" COLOR_RED "%s" COLOR_RESET "'"
#define ERR_CHECK_STDOUT_CONFLICT ERROR_MSG "--check cannot be combined with --stdout"
#define ERR_CHECK_OUTPUT_CONFLICT ERROR_MSG "--check cannot be combined with --output"
#define ERR_CHECK_ONLY_MSG TERRA ": " COLOR_GREEN "Check finished" COLOR_RESET
#define ERR_OUTPUT_STDOUT_MSG "Generated C written to stdout"

// Buffer manager errors
#define ERR_FILE_SIZE_ERROR ERROR_MSG "Could not determine file size"
#define ERR_MEMORY_ALLOCATION_ERROR ERROR_MSG "Memory allocation failed for file buffer"
#define ERR_FILE_READ_ERROR ERROR_MSG "Failed to read entire file"

#define AN_ERROR_OCCURRED_WHILE TERRA_COLON COLOR_RED "An error occurred while "
#define TERRA_ARGS_ERROR AN_ERROR_OCCURRED_WHILE "parsing arguments" COLOR_RESET
#define TERRA_ACCESS_ERROR AN_ERROR_OCCURRED_WHILE "accessing the file" COLOR_RESET
#define TERRA_BUILD_ERROR AN_ERROR_OCCURRED_WHILE "building the file" COLOR_RESET

// C99 Codegen messages
#define GENERATING_MSG "[%d/%d] Generating C99 code for '" COLOR_BRIGHT_WHITE "%s" COLOR_RESET "'... "
#define GEN_FAIL_MSG COLOR_RED "Failed to generate C99 code" COLOR_RESET
#define GENERATION_DONE_MSG COLOR_GREEN "Generation completed in %.3fs" COLOR_RESET
#define OUTPUT_SAVED_MSG "C99 output saved to '" COLOR_BRIGHT_WHITE "%s" COLOR_RESET "'"
#define WARNING_EMPTY_SOURCE WARNING_MSG "Empty source file '%s'" COLOR_RESET

#define HELP_TEXT "\n"\
\
COLOR_BRIGHT_WHITE "About" COLOR_RESET "\n"\
TERRA " - The " COLOR_BRIGHT_WHITE "Tesseract" COLOR_RESET " compiler.\n"\
"Lightning-fast compilation and\n"\
"aggressive optimization for peak performance.\n"\
"Made with " COLOR_BRIGHT_RED "LOVE" COLOR_RESET " by " COLOR_BRIGHT_WHITE "Tesseract Authors" COLOR_RESET ".\n\n"\
\
COLOR_BRIGHT_WHITE "Available options" COLOR_RESET "\n"\
"-h, --help              Show this help text\n"\
"-v, -V, --version       Show version and build information\n"\
"-b, --build <path>      Compile a source file (path may also be positional)\n"\
"-o, --output <path>     Write generated C to <path> (use '-' for stdout)\n"\
"-c, --check             Parse and type-check only, do not generate C\n"\
"    --stdout            Write generated C to standard output\n"\
"-q, --quiet             Suppress progress messages\n"\
"    --                  Treat every following argument as a path\n\n"\
\
COLOR_BRIGHT_WHITE "Usage" COLOR_RESET "\n"\
COLOR_GRAY "terra <path> [-o output.c]\n"\
"terra --check <path>\n"\
"terra --version" COLOR_RESET "\n\n"

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
