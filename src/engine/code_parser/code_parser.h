#pragma once

#include <stddef.h>
#include "../frontend/ast.h"

/**
 * @brief Parse Tesseract source code into an AST program
 * @param buffer Source code buffer
 * @param buffer_size Size of the buffer
 * @param path Source file path (for error messages)
 * @return Parsed tess_program, or NULL on error
 */
tess_program* tesseract_parse(const char *buffer, size_t buffer_size, const char *path);
