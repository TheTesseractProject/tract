#include "render_text.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

char rand_symbol(void) {
    return (char)(33 + (rand() % 94));
}

void render_text(const char* text, unsigned short offset, bool reverse) {
    if (text == NULL) {
        return;
    }
    
    unsigned short text_size = strlen(text) + 1;

    char text_buffer[text_size];
    text_buffer[text_size - 1] = '\0';

    for (unsigned short i = 0; i < text_size - 1; ++i) {
        if (text[i] == '\033' && i + 1 < text_size - 1 && text[i + 1] == '[') {
            unsigned short j = i;
            while (j < text_size - 1 && text[j] != 'm') {
                text_buffer[j] = text[j];
                ++j;
            }
            if (j < text_size - 1) {
                text_buffer[j] = text[j];
            }
            i = j;
        } else if (text[i] == '\n') {
            text_buffer[i] = '\n';
        } else if (i == offset || text[i] == ' ') {
            text_buffer[i] = ' ';
        } else if ((i > offset) ^ reverse) {
            text_buffer[i] = rand_symbol();
        } else {
            text_buffer[i] = text[i];
        }
    }

    printf("\033[2J\033[H%s\n", text_buffer);
}
