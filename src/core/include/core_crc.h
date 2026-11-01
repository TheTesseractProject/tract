#ifndef CORE_CRC_H
#define CORE_CRC_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC32 with the standard reflected polynomial 0xEDB88320, compatible with the
 * Rust `common-lib` implementation and the pico bytecode container.
 */
uint32_t core_crc32(const unsigned char *data, size_t len);

#endif /* CORE_CRC_H */
