#include "code_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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
    
    // Flags
    bool is_token;
    bool can_start_token;
    bool reduce_nesting;
    bool no_arguments;
    bool pending_log_brace;
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
    
    // Initialize flags
    parser->is_token = false;
    parser->can_start_token = true;
    parser->reduce_nesting = false;
    parser->no_arguments = false;
    parser->pending_log_brace = false;
    
    return parser;
}

static void parser_cleanup(parser_t* parser) {
    if (parser != NULL) {
        if (parser->log != NULL) {
            fclose(parser->log);
        }
        free(parser);
    }
}

static void parser_log_token(parser_t* parser, const char* token) {
    if (parser != NULL && parser->log != NULL && token != NULL) {
        for (size_t i = 1; i <= parser->nesting_level; ++i) {
            putc('\t', parser->log);
        }
        fprintf(parser->log, "'%s' (start=%zu, end=%zu, length=%zu)\n", 
            token, parser->start, parser->end, parser->end - parser->start + 1);
    }
}

static void parser_log_brace(parser_t* parser, bool opening) {
    size_t tabs_amount = opening ? parser->nesting_level - 1 : parser->nesting_level;
    if (parser != NULL && parser->log != NULL) {
        for (size_t i = 1; i <= tabs_amount; ++i) {
            putc('\t', parser->log);
        }
        fputs(opening ? "{\n" : "}\n", parser->log);
    }
}

static void parse_error(parser_t* parser, char c, size_t pos) {
    if (parser != NULL) {
        fprintf(stderr, "\nError: Unexpected '%c' (pos=%zu)\n", c, pos);
        parser->parse_success = false;
    }
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
        
        switch (c) {
            case ' ':
            case '\n':
            case '\r':
            case '\t':
            case '\0': {
                if (parser->is_token) {
                    parser->end = parser->pos - 1;
                    parser->is_token = false;
                }

                break;
            }
            case '(':
            case ',':
            case ';': {
                size_t new_argument_nesting = parser->nesting_level - parser->pending_reduce_nesting;
                if ((parser->can_start_token && !parser->no_arguments) ||
                    (c == ';' && new_argument_nesting) ||
                    (c == ',' && !new_argument_nesting) ||
                    (c == '(' && parser->reduce_nesting))
                {
                    parse_error(parser, c, parser->pos);
                    goto PARSE_END;
                }
                parser->reduce_nesting = false;

                bool is_arguments = c == '(';

                parser->is_token = false;
                parser->can_start_token = true;
                
                if (parser->pending_log_brace) {
                    parser->pending_log_brace = false;
                    if (!parser->no_arguments) {
                        parser_log_brace(parser, true);
                    }
                }

                if (parser->no_arguments) {
                    goto NO_ARGUMENTS;
                }

                if (!parser->end) {
                    parser->end = parser->pos - 1;
                }

                size_t length = parser->end - parser->start + 1;
                char* token = (char*)malloc(length + 1);
                
                if (token == NULL) {
                    fputs("\nError: Memory allocation failed\n", stderr);
                    parser->parse_success = false;
                    goto PARSE_END;
                }

                // Copy token from buffer
                memcpy(token, parser->input + parser->start, length);
                token[length] = '\0';
                
                parser_log_token(parser, token);
                free(token);

                if (is_arguments) {
                    parser->nesting_level++;
                    parser->pending_log_brace = true;
                }

                NO_ARGUMENTS:

                if (parser->pending_reduce_nesting) {
                    // Comment during debug
                    // parser->nesting_level -= parser->pending_reduce_nesting;

                    for (size_t j = 1; j <= parser->pending_reduce_nesting; ++j) {
                        parser->nesting_level--;
                        if (!parser->no_arguments) {
                            parser_log_brace(parser, false);
                        } else {
                            parser->no_arguments = false;
                        }
                    }

                    parser->pending_reduce_nesting = 0;
                }
                
                break;
            }
            case ')': {
                if (parser->nesting_level - parser->pending_reduce_nesting) {
                    parser->pending_reduce_nesting++;
                } else {
                    parse_error(parser, c, parser->pos);
                    goto PARSE_END;
                }

                if (parser->is_token) {
                    parser->end = parser->pos - 1;
                    parser->is_token = false;
                }

                if (parser->can_start_token) {
                    parser->no_arguments = true;
                }

                parser->reduce_nesting = true;

                break;
            }
            default: {
                if (!parser->is_token) {
                    if (!parser->can_start_token) {
                        if (parser->nesting_level) {
                            fprintf(stderr, "\nError: ',' expected (pos=%zu)\n", parser->pos);
                        } else {
                            fprintf(stderr, "\nError: ';' expected (pos=%zu)\n", parser->pos);
                        }

                        parser->parse_success = false;
                        goto PARSE_END;
                    }
                    parser->is_token = true;
                    parser->can_start_token = false;
                    parser->start = parser->pos;
                    parser->end = 0;
                }

                break;
            }
        }
        parser->pos++;
    }
    
    if (!parser->can_start_token) {
        fprintf(stderr, "\nError: Expected ';' before end of file\n");
        parser->parse_success = false;
    }

    if (parser->nesting_level) {
        fprintf(stderr, "\nError: Unterminated argument list detected at end of file (nesting_level=%zu)\n", parser->nesting_level);
        parser->parse_success = false;
    }
    
    PARSE_END:
    result = parser->parse_success;
    parser_cleanup(parser);
    
    return result;
}
