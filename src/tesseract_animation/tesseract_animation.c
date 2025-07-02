#include "tesseract_animation.h"

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "constants.h"
#include "render/render_tesseract.h"
#include "terminal_utils/terminal_utils.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <fcntl.h>
#endif

void tesseract_animation(void) {
    PIXEL_ASPECT = get_char_aspect_ratio();

    update_screen_size();
    init_screen();

    long long previous_time = get_microseconds();
    long long current_time;
    float deltaTime;

    float rotation = 0;

    tesseract(50); // tesseract, size 50.

    allocate_drawings_buffer();

    bool perspective = true;
    float fov_degrees = 60.0f;
    float zoom = 0.8f;

    // text
    char text[] = TESSERACT_ANIMATION_TEXT;
    unsigned short text_size = sizeof(text);

    bool reverse = false;
    unsigned short offset = 1;

#ifndef _WIN32
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
#endif

    while (1) {
        current_time = get_microseconds();
        deltaTime = (current_time - previous_time) / 1000000.0f; // Convert microseconds to seconds
        previous_time = current_time;

        memcpy(drawings_buffer, drawings, drawings_size * sizeof(drawing));

        rotate_world_XZ(rotation);
        rotate_world_YW(rotation);

        if (update_screen_size()) {
            reinit_screen();
            #ifdef _WIN32
                system("cls");
            #endif
        }

        if (offset / 2 > text_size + (reverse ? 50 : 70)) {
            reverse = !reverse;
            offset = 1;
        }

        clear_terminal();
        draw(perspective, fov_degrees, zoom, text, offset / 2, reverse);

        rotation += 0.013f * (deltaTime * 60.0f); // Adjust speed based on deltaTime

        long long sleep = 10000 - (get_microseconds() - current_time);
        if (sleep > 0) {
            usleep(sleep);
        }

        ++offset;

        // --- Keypress check for quitting ---
        int ch = -1;
#ifdef _WIN32
        if (_kbhit()) {
            ch = _getch();
        }
#else
        ch = getchar();
#endif
        if (ch == 'q' || ch == 'Q') {
            break;
        }
    }

#ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif

    clear_terminal();
    fputs(TESSERACT_ANIMATION_END, stdout);

    free_all();
}
