#include "code_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

bool tesseract_parse(FILE *input_file) {
    if (input_file == NULL) {
        // file error
        return false;
    }

    bool parse_success = true;
    int c;
    size_t pos = 0;
    size_t start = 0;
    bool is_name = false;

    FILE* log = fopen("debug.log", "w");
    if (log == NULL) {
        fprintf(stderr, "Warning: Could not open debug.log for writing\n");
    }

    while ((c = fgetc(input_file)) != EOF) {
        pos = ftell(input_file);  // Current position after reading character
        size_t char_pos = pos - 1;  // Position of the character we just read
        
        switch (c) {
            case ' ':
            case '\n':
            case '\r':
            case '\t': {
                if (is_name) {
                    fprintf(stderr, "\nError: ';' expected (pos=%zu)\n", char_pos);
                    parse_success = false;
                    goto END_PARSE;
                }

                if (log != NULL) {
                    fprintf(log, "%zu) <empty>\n", char_pos);
                }

                break;
            }
            case ';': {
                if (!is_name) {
                    fprintf(stderr, "\nError: Unexpected ';' (pos=%zu)\n", char_pos);
                    parse_success = false;
                    goto END_PARSE;
                }

                size_t length = char_pos - start;
                char* name = (char*)malloc(length + 1);
                
                if (name == NULL) {
                    parse_success = false;
                    goto END_PARSE;
                }

                size_t current_pos = ftell(input_file);
                
                // Seek to start position and read the entire chunk at once
                if (fseek(input_file, start, SEEK_SET) == 0) {
                    size_t bytes_read = fread(name, 1, length, input_file);
                    name[bytes_read] = '\0';
                    if (log != NULL) {
                        fprintf(log, "%zu) (';' found) Found command: '%s' (start=%zu, end=%zu, length=%zu)\n", 
                            char_pos, name, start, char_pos - 1, length);
                    }
                    // Here you would process the command string
                    // For now, just print it
                    printf("Parsed command: %s\n", name);
                } else {
                    free(name);
                    parse_success = false;
                    goto END_PARSE;
                }
                
                // Restore original position
                fseek(input_file, current_pos, SEEK_SET);
                free(name);

                is_name = false;
                
                break;
            }
            default: {
                if (!is_name) {
                    // First non-whitespace character - start of a name
                    is_name = true;
                    start = char_pos;  // Position of this character
                }
                
                if (log != NULL) {
                    fprintf(log, "%zu) %c\n", char_pos, c);
                }
                break;
            }
        }
    }
    
    // Check if we ended with an incomplete command
    if (is_name) {
        fprintf(stderr, "\nError: Unexpected end of file - ';' expected\n");
        parse_success = false;
    }
    
END_PARSE:
    rewind(input_file);
    
    if (log != NULL) {
        fclose(log);
    }

    return parse_success;
}
