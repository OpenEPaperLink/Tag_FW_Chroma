#ifndef G5_TEST_PLATFORM_H
#define G5_TEST_PLATFORM_H
#include <stdint.h>
#include <stdbool.h>
#define __xdata
#define __idata
#define __pdata
#define __code
#define __reentrant
#ifndef TEST_SCREEN_WIDTH
#define TEST_SCREEN_WIDTH 640
#endif
#ifndef TEST_SCREEN_HEIGHT
#define TEST_SCREEN_HEIGHT 384
#endif
#define SCREEN_WIDTH TEST_SCREEN_WIDTH
#define SCREEN_HEIGHT TEST_SCREEN_HEIGHT
#define EEPROM_SIZE 0x100000UL
#ifndef TEST_SLOT_BYTES
#define TEST_SLOT_BYTES 0x17000UL
#endif
#define EEPROM_IMG_EACH TEST_SLOT_BYTES
#define DATATYPE_IMG_RAW_1BPP 0x20
#define DATATYPE_IMG_RAW_2BPP 0x21
/* Suppress the firmware header's board-specific dependencies; keep its API. */
#define _WDT_H_
#define wdtPet() ((void)0)
#define _EEPROM_H_
#pragma pack(push, 1)
struct EepromImageHeader {
    uint64_t version;
    uint32_t validMarker, size;
    uint8_t dataType;
    uint32_t id;
    uint8_t argument;
};
#pragma pack(pop)
void eepromRead(uint32_t address, void *destination, uint16_t length);
void eepromWrite(uint32_t address, const void *source, uint16_t length);
#endif
