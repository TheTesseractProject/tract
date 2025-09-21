/*
    I haven't yet had time to break the code into functions and eliminate spaghetti-code, this is a working prototype of the parser in debug.log
*/
#include "code_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

//#include "token.h"

// Bit flags for parser state
#define FLAG_IS_TOKEN                 0     // bit 0
#define FLAG_CAN_START_TOKEN          1     // bit 1
#define FLAG_NO_ARGUMENTS             2     // bit 2
#define FLAG_COMMA_BEFORE_ARGS_END    3     // bit 3
#define FLAG_COMMA_ENCOUNTERED        4     // bit 4
#define FLAG_PENDING_LOG_BRACE        5     // bit 5


// Functions for bit operations
static inline void set_flag(uint8_t* flags, uint8_t bit) {
    *flags |= (1 << bit);
}

static inline void clear_flag(uint8_t* flags, uint8_t bit) {
    *flags &= ~(1 << bit);
}

/*static inline void toggle_flag(uint8_t* flags, uint8_t bit) {
    *flags ^= (1 << bit);
}*/

static inline bool check_flag(uint8_t flags, uint8_t bit) {
    return (flags & (1 << bit)) != 0;
}

// Internal parser structure
typedef struct {
    char* input;
    size_t input_size;
    size_t nesting_level;
    size_t pending_reduce_nesting;
    size_t pos;
    size_t start;
    size_t end;
    bool parse_success;
    FILE* log;
    
    uint8_t flags;  // Bit flags: is_token, can_start_token, reduce_nesting, no_arguments, pending_log_brace
} parser_t;

// Internal parser functions
static parser_t* parser_init(char* input, size_t input_size) {
    parser_t* parser = (parser_t*)malloc(sizeof(parser_t));
    if (parser == NULL) {
        return NULL;
    }
    
    parser->input = input;
    parser->input_size = input_size;
    parser->nesting_level = 0;
    parser->pending_reduce_nesting = 0;
    parser->pos = 0;
    parser->start = 0;
    parser->end = 0;
    parser->parse_success = true;
    parser->log = NULL;
    
    // Initialize flags - only can_start_token is set initially
    parser->flags = 0;
    set_flag(&parser->flags, FLAG_CAN_START_TOKEN);
    
    return parser;
}

static void parser_cleanup(parser_t* parser) {
    if (parser->log != NULL) {
        fclose(parser->log);
    }
    free(parser);
}

static void parser_log_indent(parser_t* parser, size_t level) {
    for (size_t i = 0; i < level; ++i) {
        fputc('\t', parser->log);
    }
}

static void parser_log_token(parser_t* parser, size_t start, size_t end) {
    if (!parser->log) return;
    
    parser_log_indent(parser, parser->nesting_level);
    size_t len = end - start + 1;
    fprintf(parser->log, "'%.*s' (start=%zu, end=%zu, length=%zu)\n", 
            (int)len, &parser->input[start], start, end, len);
}

static void parser_log_brace(parser_t* parser, bool opening) {
    if (!parser->log) return;
    
    size_t level = opening ? parser->nesting_level - 1 : parser->nesting_level;
    parser_log_indent(parser, level);
    fputs(opening ? "{\n" : "}\n", parser->log);
}

static void parser_error(parser_t* parser, const char* message, ...) {
    if (parser->parse_success) {
        putc('\n', stderr);
    }

    va_list args;
    va_start(args, message);

    fprintf(stderr, "Error at pos %zu: ", parser->pos);
    vfprintf(stderr, message, args);
    fputc('\n', stderr);

    va_end(args);

    parser->parse_success = false;
}

bool tesseract_parse(char* buffer, size_t buffer_size) {
    if (buffer == NULL) {
        return false;
    }

    // Initialize parser
    parser_t* parser = parser_init(buffer, buffer_size);
    if (parser == NULL) {
        return false;
    }

    bool result = true;

    // Open log file
    parser->log = fopen("debug.log", "w");
    if (parser->log == NULL) {
        fputs("Warning: Could not open debug.log for writing\n", stderr);
    }

    while (parser->pos < parser->input_size) {
        char c = parser->input[parser->pos];
        size_t argument_nesting = parser->nesting_level - parser->pending_reduce_nesting;
        
        switch (c) {
            case ' ': case '\n': case '\r': case '\t': case '\0': {
                if (check_flag(parser->flags, FLAG_IS_TOKEN)) {
                    parser->end = parser->pos - 1;
                    clear_flag(&parser->flags, FLAG_IS_TOKEN);
                }

                break;
            }
            case '(': case ',': case ';': {
                if ((check_flag(parser->flags, FLAG_CAN_START_TOKEN) && !check_flag(parser->flags, FLAG_NO_ARGUMENTS)) ||
                    (c == '(' && parser->pending_reduce_nesting)) {
                    parser_error(parser, "Unexpected '%c'", c);
                    break;
                }

                if ((c == ';' && argument_nesting) ||
                    (c == ',' && !argument_nesting))
                {
                    parser_error(parser, "Unexpected delimiter '%c', expected '%c'", c, c == ';' ? ',' : ';');
                }

                DELIMITER:

                if (parser->nesting_level && c != '(') {
                    set_flag(&parser->flags, FLAG_COMMA_ENCOUNTERED);
                }

                clear_flag(&parser->flags, FLAG_IS_TOKEN);
                set_flag(&parser->flags, FLAG_CAN_START_TOKEN);

                if (check_flag(parser->flags, FLAG_COMMA_BEFORE_ARGS_END)) {
                    clear_flag(&parser->flags, FLAG_COMMA_BEFORE_ARGS_END);
                    goto REDUCE_NESTING;
                }

                if (check_flag(parser->flags, FLAG_PENDING_LOG_BRACE)) {
                    clear_flag(&parser->flags, FLAG_PENDING_LOG_BRACE);
                    if (!check_flag(parser->flags, FLAG_NO_ARGUMENTS)) {
                        parser_log_brace(parser, true);
                    }
                }

                if (check_flag(parser->flags, FLAG_NO_ARGUMENTS)) {
                    goto REDUCE_NESTING;
                }

                if (!parser->end) {
                    if (parser->pos > 0) {
                        parser->end = parser->pos - 1;
                    } else {
                        parser->end = parser->pos;
                    }
                }

                // Use improved parser_log_token: log directly from buffer, no malloc
                if (parser->end >= parser->start && parser->log) {
                    parser_log_token(parser, parser->start, parser->end);
                }

                if (c == '(') {
                    parser->nesting_level++;
                    set_flag(&parser->flags, FLAG_PENDING_LOG_BRACE);
                }

                REDUCE_NESTING:

                if (parser->pending_reduce_nesting) {
                    // Comment during debug
                    // parser->nesting_level -= parser->pending_reduce_nesting;

                    for (size_t j = 1; j <= parser->pending_reduce_nesting; ++j) {
                        parser->nesting_level--;
                        if (!check_flag(parser->flags, FLAG_NO_ARGUMENTS)) {
                            parser_log_brace(parser, false);
                        } else {
                            clear_flag(&parser->flags, FLAG_NO_ARGUMENTS);
                        }
                    }

                    parser->pending_reduce_nesting = 0;
                }

                break;
            }
            case ')': {
                if (check_flag(parser->flags, FLAG_IS_TOKEN)) {
                    parser->end = parser->pos - 1;
                    clear_flag(&parser->flags, FLAG_IS_TOKEN);
                }

                if (argument_nesting) {
                    parser->pending_reduce_nesting++;
                } else {
                    parser_error(parser, "Unexpected '%c' at position %zu", c, parser->pos);
                    break;
                }

                if (check_flag(parser->flags, FLAG_COMMA_ENCOUNTERED)) {
                    parser_error(parser, "Unexpected delimiter before end of arguments");
                    clear_flag(&parser->flags, FLAG_COMMA_ENCOUNTERED);
                    clear_flag(&parser->flags, FLAG_CAN_START_TOKEN);
                    set_flag(&parser->flags, FLAG_COMMA_BEFORE_ARGS_END);
                } else if (check_flag(parser->flags, FLAG_CAN_START_TOKEN)) {
                    clear_flag(&parser->flags, FLAG_CAN_START_TOKEN);
                    set_flag(&parser->flags, FLAG_NO_ARGUMENTS);
                }

                break;
            }
            default: {
                if (!check_flag(parser->flags, FLAG_IS_TOKEN)) {
                    if (!check_flag(parser->flags, FLAG_CAN_START_TOKEN)) {
                        if (argument_nesting) {
                            parser_error(parser, "',' expected before new token");
                        } else {
                            parser_error(parser, "';' expected before new token");
                        }
                        parser->pos--;
                        goto DELIMITER;
                    }
                    set_flag(&parser->flags, FLAG_IS_TOKEN);
                    clear_flag(&parser->flags, FLAG_CAN_START_TOKEN);
                    clear_flag(&parser->flags, FLAG_COMMA_ENCOUNTERED);
                    parser->start = parser->pos;
                    parser->end = 0;
                }

                break;
            }
        }
        parser->pos++;
    }
    
    if (!check_flag(parser->flags, FLAG_CAN_START_TOKEN)) {
        parser->pos--;
        parser_error(parser, "Expected delimiter before end of file");
        goto DELIMITER;
    }

    if (parser->nesting_level) {
        parser->pos--;
        parser_error(parser, "Unterminated argument list detected at end of file");
        parser->pending_reduce_nesting = parser->nesting_level;
        goto REDUCE_NESTING;
    }

    //PARSE_END:
    result = parser->parse_success;
    parser_cleanup(parser);
    
    return result;
}
