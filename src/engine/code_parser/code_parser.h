#pragma once

#include <stddef.h>

bool tesseract_parse(const char *buffer, size_t buffer_size,
                     const char *path);
