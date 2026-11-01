#ifndef CODEGEN_H
#define CODEGEN_H

#include <stddef.h>

typedef struct {
    char* type;
    char* name;
} TesseractNode;

size_t generate_code(const TesseractNode* node, char* output_buffer, size_t buffer_size);

#endif // CODEGEN_H

