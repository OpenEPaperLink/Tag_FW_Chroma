/* Exact-sized source/output allocations for ASan and UBSan. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "host_harness.h"
static uint32_t seed = 0x12345678;
static uint32_t next_random(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *input = fopen(argv[1], "rb"); assert(input);
    uint32_t fields[4]; unsigned cases = 0;
    while (fread(fields, 4, 4, input) == 4) {
        uint8_t *pixels = malloc(fields[2]), *source = malloc(fields[3]), *output = malloc(fields[2]);
        assert(pixels && source && output);
        assert(fread(pixels, 1, fields[2], input) == fields[2]);
        assert(fread(source, 1, fields[3], input) == fields[3]);
        assert(test_bits(source, fields[3], fields[0], fields[1], output) == 0);
        assert(!memcmp(pixels, output, fields[2]));
        if (fields[0] == SCREEN_WIDTH && (fields[1] == SCREEN_HEIGHT || fields[1] == 2 * SCREEN_HEIGHT)) {
            uint8_t *image = malloc(fields[3] + 6); assert(image);
            image[0] = 6; image[1] = SCREEN_WIDTH & 255; image[2] = SCREEN_WIDTH >> 8;
            image[3] = SCREEN_HEIGHT & 255; image[4] = SCREEN_HEIGHT >> 8;
            image[5] = fields[1] / SCREEN_HEIGHT; memcpy(image + 6, source, fields[3]);
            assert(test_image(image, fields[3] + 6, output, fields[2]) == (image[5] == 1 ? 0x20 : 0x21));
            assert(!memcmp(pixels, output, fields[2]));
            image[5] = 3;
            assert(test_image(image, fields[3] + 6, output, fields[2]) == 0);
            assert(test_stat(1) == 0);
            for (unsigned n = 0; n < 6; ++n) assert(test_image(image, n, output, fields[2]) == 0);
            free(image);
        }
        ++cases; free(pixels); free(source); free(output);
    }
    fclose(input);
    for (unsigned i = 0; i < 100000; ++i) {
        static const uint16_t widths[] = {0, 1, 7, 8, 9, 127, 128, 129, 295, 296, 400, 599, 600, 639, 640, 641};
        unsigned width = widths[next_random() % 16], height = 1 + next_random() % 8;
        size_t length = next_random() % 128, outsize = ((width + 7) / 8) * height;
        uint8_t *source = malloc(length ? length : 1), *output = malloc(outsize ? outsize : 1);
        assert(source && output);
        for (unsigned j = 0; j < length; ++j) source[j] = next_random();
        int result = test_bits(source, length, width, height, output);
        assert(result == 0 || result == -1 || result == -2 || result == -3);
        free(source); free(output);
    }
    printf("ASan/UBSan: %u valid cases, integrated headers, 100000 malformed streams passed.\n", cases);
    return 0;
}
