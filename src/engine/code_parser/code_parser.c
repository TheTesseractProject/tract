#include "code_parser.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

//#include "token.h"

// Bit flags for parser state
#define FLAG_IS_TOKEN               0b000001    // bit 1
#define FLAG_CAN_START_TOKEN        0b000010    // bit 2
#define FLAG_NO_ARGUMENTS           0b000100    // bit 3
#define FLAG_COMMA_BEFORE_ARGS_END  0b001000    // bit 4
#define FLAG_COMMA_ENCOUNTERED      0b010000    // bit 5
#define FLAG_PENDING_LOG_BRACE      0b100000    // bit 6


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
	parser->flags |= FLAG_CAN_START_TOKEN;
    
    return parser;
}

static void parser_cleanup(parser_t* parser) {
    if (parser->log != NULL) {
        fclose(parser->log);
    }
    free(parser);
}

static void parser_log_indent(parser_t* parser, size_t level) {
    for (size_t i = 0; i < level; i++) {
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

    vfprintf(stderr, message, args);
    fprintf(stderr, " (pos=%zu)\n", parser->pos);

    va_end(args);

    parser->parse_success = false;
}

// Helper function to reduce nesting level and log closing braces
static void parser_reduce_nesting(parser_t* parser) {
    if (parser->pending_reduce_nesting) {
        for (size_t j = 1; j <= parser->pending_reduce_nesting; j++) {
            parser->nesting_level--;
            if (!(parser->flags & FLAG_NO_ARGUMENTS)) {
                parser_log_brace(parser, false);
            } else {
                parser->flags &= ~FLAG_NO_ARGUMENTS;
            }
        }
        parser->pending_reduce_nesting = 0;
    }
}

// Handle whitespace characters (space, newline, carriage return, tab, null)
static void handle_whitespace(parser_t* parser) {
    if (parser->flags & FLAG_IS_TOKEN) {
        parser->end = parser->pos - 1;
        parser->flags &= ~FLAG_IS_TOKEN;
    }
}

// Handle delimiter characters (parentheses, commas, semicolons)
static void handle_delimiter(parser_t* parser, char c) {
    size_t argument_nesting = parser->nesting_level - parser->pending_reduce_nesting;
    
    if (((parser->flags & FLAG_CAN_START_TOKEN) && !(parser->flags & FLAG_NO_ARGUMENTS)) ||
        (c == '(' && parser->pending_reduce_nesting)) {
        parser_error(parser, WARN_PARSER_UNEXPECTED_CHAR, c);
        return;
    }

    if ((c == ';' && argument_nesting) ||
        (c == ',' && !argument_nesting))
    {
        parser_error(parser, WARN_PARSER_UNEXPECTED_DELIMITER, c, c == ';' ? ',' : ';');
    }

    if (parser->nesting_level && c != '(') {
        parser->flags |= FLAG_COMMA_ENCOUNTERED;
    }

    parser->flags &= ~FLAG_IS_TOKEN;
    parser->flags |= FLAG_CAN_START_TOKEN;

    if (parser->flags & FLAG_COMMA_BEFORE_ARGS_END) {
        parser->flags &= ~FLAG_COMMA_BEFORE_ARGS_END;
        goto REDUCE_NESTING;
    }

    if (parser->flags & FLAG_PENDING_LOG_BRACE) {
        parser->flags &= ~FLAG_PENDING_LOG_BRACE;
        if (!(parser->flags & FLAG_NO_ARGUMENTS)) {
            parser_log_brace(parser, true);
        }
    }

    if (parser->flags & FLAG_NO_ARGUMENTS) {
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
        parser->flags |= FLAG_PENDING_LOG_BRACE;
    }

    REDUCE_NESTING:
    parser_reduce_nesting(parser);
}

// Handle closing parenthesis
static void handle_closing_paren(parser_t* parser) {
    if (parser->flags & FLAG_IS_TOKEN) {
        parser->end = parser->pos - 1;
        parser->flags &= ~ FLAG_IS_TOKEN;
    }

    size_t argument_nesting = parser->nesting_level - parser->pending_reduce_nesting;
    
    if (argument_nesting) {
        parser->pending_reduce_nesting++;
    } else {
        parser_error(parser, WARN_PARSER_UNEXPECTED_CHAR, ')', parser->pos);
        return;
    }

    if (parser->flags & FLAG_COMMA_ENCOUNTERED) {
        parser_error(parser, WARN_PARSER_UNEXPECTED_DELIMITER_BEFORE_END);
        parser->flags &= ~FLAG_COMMA_ENCOUNTERED;
        parser->flags &= ~FLAG_CAN_START_TOKEN;
        parser->flags |= FLAG_COMMA_BEFORE_ARGS_END;
    } else if (parser->flags & FLAG_CAN_START_TOKEN) {
        parser->flags &= ~FLAG_CAN_START_TOKEN;
        parser->flags |= FLAG_NO_ARGUMENTS;
    }
}

// Handle token characters (non-whitespace, non-delimiter)
static bool handle_token_character(parser_t* parser) {
    size_t argument_nesting = parser->nesting_level - parser->pending_reduce_nesting;

    if (!(parser->flags & FLAG_IS_TOKEN)) {
        if (!(parser->flags & FLAG_CAN_START_TOKEN)) {
            if (argument_nesting) {
                parser_error(parser, WARN_PARSER_COMMA_EXPECTED);
            } else {
                parser_error(parser, WARN_PARSER_SEMICOLON_EXPECTED);
            }
            parser->pos--;
            handle_delimiter(parser, parser->input[parser->pos]);
            return true; // Signal that a re-parse happened
        }
        parser->flags |= FLAG_IS_TOKEN;
        parser->flags &= ~FLAG_CAN_START_TOKEN;
        parser->flags &= ~FLAG_COMMA_ENCOUNTERED;
        parser->start = parser->pos;
        parser->end = 0;
    }
    return false; // No re-parse needed
}

// Validate parser state at end of input
static bool validate_end_of_parse(parser_t* parser) {
    if (!(parser->flags & FLAG_CAN_START_TOKEN)) {
        parser->pos--;
        parser_error(parser, WARN_PARSER_DELIMITER_EXPECTED);
        handle_delimiter(parser, parser->input[parser->pos]);
        return false;
    }

    if (parser->nesting_level) {
        parser->pos--;
        parser_error(parser, WARN_PARSER_UNTERMINATED_ARGS);
        parser->pending_reduce_nesting = parser->nesting_level;
        parser_reduce_nesting(parser);
        return false;
    }
    
    return true;
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

    // Open log file
    parser->log = fopen("debug.log", "w");
    if (parser->log == NULL) {
        ERRLN(WARN_DEBUG_LOG);
    }

    // Main parsing loop
    while (parser->pos < parser->input_size) {
        char c = parser->input[parser->pos];
        
        switch (c) {
            case ' ': case '\n': case '\r': case '\t':
                handle_whitespace(parser);
                break;

            case '\0':
                goto END_PARSE_LOOP;

            case '(': case ',': case ';':
                handle_delimiter(parser, c);
                break;

            case ')':
                handle_closing_paren(parser);
                break;

            default:
                if (handle_token_character(parser)) {
                    continue; // Skip the pos increment and restart the loop
                }
                break;
        }
        parser->pos++;
    }
    
END_PARSE_LOOP:
    
    // Validate end of parse
    bool result = validate_end_of_parse(parser) && parser->parse_success;
    
    parser_cleanup(parser);
    return result;
}
