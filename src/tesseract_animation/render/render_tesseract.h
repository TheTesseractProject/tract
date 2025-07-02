#ifndef RENDER_TESSERACT_H
#define RENDER_TESSERACT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Constants
extern const char gradient[];
extern size_t gradient_size;

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

// Global variables
extern drawing *drawings;
extern drawing *drawings_buffer;
extern size_t drawings_size;
extern uint8_t *screen;
extern uint8_t screen_x;
extern uint8_t screen_y;
extern float PIXEL_ASPECT;
extern unsigned previous_rows;
extern unsigned previous_cols;

// Utility functions
long long get_microseconds(void);

// Screen management functions
void set_pixel(int x, int y, uint8_t value);
uint8_t get_pixel(int x, int y);
void init_screen(void);
void reinit_screen(void);
bool update_screen_size(void);
void free_all(void);
void clear_screen(void);

// Drawing management functions
void reallocate_drawings(void);
void allocate_drawings_buffer(void);

// Projection functions
Vector2 project3d2d(bool is_perspective, Vector3 point, float fov_degrees, float zoom);
Vector3 project4d3d(bool is_perspective, Vector4 point, float fov_degrees, float zoom);

// Drawing functions
void line(Vector4 point_a, Vector4 point_b);
void draw_line2d_with_depth(Vector2 point_a, Vector2 point_b, int8_t z_a, int8_t z_b);
void draw(bool perspective, float fov_degrees, float zoom, const char* text, unsigned short offset, bool reverse);

// Rotation functions
void rotateXY(Vector4 *v, float angle);
void rotateXZ(Vector4 *v, float angle);
void rotateXW(Vector4 *v, float angle);
void rotateYZ(Vector4 *v, float angle);
void rotateYW(Vector4 *v, float angle);
void rotateZW(Vector4 *v, float angle);

// World rotation functions
void rotate_world_XY(float theta);
void rotate_world_XZ(float theta);
void rotate_world_XW(float theta);
void rotate_world_YZ(float theta);
void rotate_world_YW(float theta);
void rotate_world_ZW(float theta);

// Shape generation functions
void tesseract(const int8_t s);

#endif
