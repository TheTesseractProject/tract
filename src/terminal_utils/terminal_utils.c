#include "terminal_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#endif

#include "constants.h"

void get_terminal_size(unsigned *rows, unsigned *cols) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        *rows = 0;
        *cols = 0;
    } else {
        *rows = w.ws_row;
        *cols = w.ws_col;
    }
#endif
}

void clear_terminal(void) {
#ifdef _WIN32
    HANDLE hOut;
    COORD Position;
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    Position.X = 0;
    Position.Y = 0;
    SetConsoleCursorPosition(hOut, Position);
#else
    MSG("\033[2J\033[H");
#endif
}

float get_char_aspect_ratio(void) {
#ifdef _WIN32
    system("cls");
#else
    clear_terminal();
#endif
    unsigned height = 5;
    unsigned width = height;
    int ch;
    bool changed = true;

#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hStdin, &mode);
    SetConsoleMode(hStdin, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
#endif

    while (1) {
        if (changed) {
#ifdef _WIN32
            system("cls");
#else
            clear_terminal();
#endif
            MSGLN(TESSERACT_ANIMATION_CHAR_ASPECT_TEXT);
            for (unsigned i = 0; i < height; i++) {
                for (unsigned j = 0; j < width; j++) {
                    putchar((i == 0 || i == height-1 || j == 0 || j == width-1) ? '@' : '.');
                }
                putchar('\n');
            }
            changed = false;
        }

#ifdef _WIN32
        if (_kbhit()) {
            ch = _getch();
            if (ch == 0xE0 || ch == 0x00) {
                ch = _getch();
                switch(ch) {
                    case 'K': if(width > height) { width--; changed = true; } break;
                    case 'M': if(width < 15) { width++; changed = true; } break;
                }
            }
            else if (ch == '\r') break;
        }
#else
        if ((ch = getchar()) != EOF) {
            if (ch == '\n' || ch == '\r') break;
            if (ch == '\033') {
                getchar();
                switch(getchar()) {
                    case 'D': if(width > height) { width--; changed = true; } break;
                    case 'C': if(width < 15) { width++; changed = true; } break;
                }
            }
        }
#endif
        usleep(10000);
    }

#ifdef _WIN32
    SetConsoleMode(hStdin, mode);
    system("cls");
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    return (float)height / width;
}
