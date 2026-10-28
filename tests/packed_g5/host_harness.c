/* Host-only bounded NOR mock; compile the real common/g5.c beside this file. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "g5.h"
#include "syncedproto.h"
#include "host_harness.h"

union ImageScratch imageScratch;
#define SOURCE_START 0x19016UL
#define DESTINATION_START 0xe8016UL
static uint8_t nor[EEPROM_SIZE];
static const uint8_t *original;
static size_t sourceSize, destinationSize, pitch, rowsWritten;
static uint32_t stats[6];

enum { READS, WRITES, FULL_REFILLS, SHORT_REFILLS, PAGE_CROSSINGS, HEADER_READS };
uint32_t test_stat(unsigned index) { return index < 6 ? stats[index] : 0; }

void eepromRead(uint32_t address, void *destination, uint16_t length)
{
    assert(address >= SOURCE_START && address + length <= SOURCE_START + sourceSize);
    assert(length && length <= 128);
    assert((uint8_t *)destination >= blockbuffer &&
           (uint8_t *)destination + length <= blockbuffer + sizeof(blockbuffer));
    memcpy(destination, nor + address, length);
    ++stats[READS];
    if (length == 128) ++stats[FULL_REFILLS];
    else ++stats[SHORT_REFILLS];
    if (length == 6 && address == SOURCE_START) ++stats[HEADER_READS];
}

void eepromWrite(uint32_t address, const void *source, uint16_t length)
{
    const uint8_t *pixels = source;
    assert(address == DESTINATION_START + rowsWritten * pitch && length == pitch);
    assert(address >= DESTINATION_START && address + length <= DESTINATION_START + destinationSize);
    assert(pixels == blockbuffer || pixels == blockbuffer + 80);
    if ((address & 255) + length > 256) ++stats[PAGE_CROSSINGS];
    /* Emulate programming pre-erased NOR; the firmware API handles page splits. */
    for (unsigned i = 0; i < length; ++i) nor[address + i] &= pixels[i];
    ++rowsWritten;
    ++stats[WRITES];
}

static void prepare(const uint8_t *source, size_t length, size_t capacity, size_t rowPitch)
{
    assert(SOURCE_START + length + 32 < DESTINATION_START - 32);
    assert(DESTINATION_START + capacity + 32 <= sizeof(nor));
    original = source; sourceSize = length; destinationSize = capacity;
    pitch = rowPitch; rowsWritten = 0;
    memset(stats, 0, sizeof(stats));
    memset(&imageScratch, 0xa5, sizeof(imageScratch));
    memset(nor + SOURCE_START - 32, 0xa5, length + 64);
    memcpy(nor + SOURCE_START, source, length);
    memset(nor + DESTINATION_START - 32, 0xa5, capacity + 64);
    memset(nor + DESTINATION_START, 255, capacity);
    imageScratch.g5.transfer.value.source = SOURCE_START;
    imageScratch.g5.transfer.value.remaining = length;
    imageScratch.g5.transfer.value.destination = DESTINATION_START;
    imageScratch.g5.transfer.value.pos = imageScratch.g5.transfer.value.count = 0;
}

static void check_guards(void)
{
    for (unsigned i = 0; i < 32; ++i) {
        assert(nor[SOURCE_START - 32 + i] == 0xa5);
        assert(nor[SOURCE_START + sourceSize + i] == 0xa5);
        assert(nor[DESTINATION_START - 32 + i] == 0xa5);
        assert(nor[DESTINATION_START + destinationSize + i] == 0xa5);
    }
    assert(!memcmp(nor + SOURCE_START, original, sourceSize));
    for (unsigned i = 384; i < sizeof(imageScratch); ++i)
        assert(((const uint8_t *)&imageScratch)[i] == 0xa5);
    if (pitch <= 80) for (size_t i = pitch; i < 80; ++i) {
        assert(blockbuffer[i] == 0xa5);
        assert(blockbuffer[80 + i] == 0xa5);
    }
}

int test_bits(const uint8_t *source, size_t length, uint16_t width,
              uint16_t height, uint8_t *output)
{
    size_t rowPitch = (width + 7u) / 8u;
    prepare(source, length, rowPitch * height, rowPitch);
    int result = g5p_init(width, height, blockbuffer, blockbuffer + 80);
    if (!result) for (unsigned y = 0; y < height; ++y) {
        result = g5p_decode_line();
        if (result) break;
        memcpy(output + y * rowPitch, g5p_row(), rowPitch);
    }
    if (!result) assert(g5p_decode_line() == 1);
    else if (result < 0) assert(g5p_decode_line() == result);
    check_guards();
    return result;
}

int test_image(const uint8_t *source, size_t length, uint8_t *output,
               size_t capacity)
{
    prepare(source, length, capacity, SCREEN_WIDTH / 8u);
    int result = g5Decode();
    check_guards();
    memcpy(output, nor + DESTINATION_START, capacity);
    return result;
}
