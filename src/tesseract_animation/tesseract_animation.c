#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "tesseract_animation.h"
#include "constants.h"
#include "render/render_tesseract.h"

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

    }

    //free_all();
}
