#ifndef G5_HOST_HARNESS_H
#define G5_HOST_HARNESS_H
#include <stddef.h>
#include <stdint.h>
int test_bits(const uint8_t *source, size_t length, uint16_t width,
              uint16_t height, uint8_t *output);
int test_image(const uint8_t *source, size_t length, uint8_t *output,
               size_t capacity);
uint32_t test_stat(unsigned index);
#endif
