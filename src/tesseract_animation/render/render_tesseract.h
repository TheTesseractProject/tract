#ifndef RENDER_TESSERACT_H
#define RENDER_TESSERACT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Structures
typedef struct {
    uint8_t x, y;
} Vector2;

typedef struct {
    int8_t x, y, z;
} Vector3;

typedef struct {
    int8_t x, y, z, w;
} Vector4;

typedef struct {
    Vector4 a, b;
} drawing;

// Global variables (used by tesseract_animation.c)
extern drawing *drawings;
extern drawing *drawings_buffer;
extern size_t drawings_size;
extern float PIXEL_ASPECT;

// Utility functions
long long get_microseconds(void);

// Screen management functions (used by tesseract_animation.c)
void init_screen(void);
void reinit_screen(void);
bool update_screen_size(void);
void free_all(void);

// Drawing management functions (used by tesseract_animation.c)
void allocate_drawings_buffer(void);

// Drawing functions (used by tesseract_animation.c)
void draw(bool perspective, float fov_degrees, float zoom, const char* text, unsigned short offset, bool reverse);

// World rotation functions
void rotate_world_XY(float theta);
void rotate_world_XZ(float theta);
void rotate_world_XW(float theta);
void rotate_world_YZ(float theta);
void rotate_world_YW(float theta);
void rotate_world_ZW(float theta);

// Shape generation functions (used by tesseract_animation.c)
void tesseract(const int8_t s);

#endif
