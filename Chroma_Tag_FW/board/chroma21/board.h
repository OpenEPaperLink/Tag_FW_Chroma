#ifndef _BOARD_H_
#define _BOARD_H_

#include "soc.h"
#include "u1.h"

#define CHROMA21
#define BOARD_NAME "Chroma21"
#define HW_TYPE    0x85

// JG = BWY, the only Chroma21 variant known to use a CC1110.
// The JB (BWR) Chroma21 probably has a different factory waveform, which
// we read from SPI flash, so it may work too.  Untested.
#define BWY

// Defaults for the factory NVRAM, used in gDefaultEEPROM and when a
// calibration value is missing from the NVRAM (values from JG10014896)
#define DEFAULT_ADC_INTERCEPT  0x02e3
#define DEFAULT_ADC_SLOPE      0x0a5a

#ifndef DISABLE_BARCODES
#define DISABLE_BARCODES   // no room on a 212 x 104 screen
#endif

extern const char * __code gBoardName;
extern const uint8_t __code gDefaultEEPROM[];

//eeprom spi
#define EEPROM_SIZE              0x0020000L
#define EEPROM_4K_ERASE_OPCODE   0x20
#define EEPROM_32K_ERASE_OPCODE  0
#define EEPROM_64K_ERASE_OPCODE  0
#define eepromByte               u1byte
#define eepromPrvSelect()     do { __asm__("nop"); P2_0 = 0; __asm__("nop"); } while(0)
#define eepromPrvDeselect()      do { __asm__("nop"); P2_0 = 1; __asm__("nop"); } while(0)

//debug uart (enable only when needed, on some boards it inhibits eeprom access)
#define dbgUartOn()                 u1setUartMode()
#define dbgUartOff()                u1setEepromMode()
#define dbgUartByte                 u1byte

/* eeprom map

EEPROM size: 0x2.0000 / 128k

Image size:  (104 x 212 x 2 bits/pixel) / 8 bit / byte = 5,512 bytes
rounded up to erase boundary = 8192 / 0x.2000 / 8k
Image slots: 8

| Start Adr | End Adr  | Size | Usage |
|     -     |    -     |   -  |   -   |
| 0x0.0000  | 0x0.1fff |   8k | Factory NVRAM (the stock FW uses either 4k sector)
| 0x0.2000  | 0x0.2fff |   4k | Factory waveform tables, used by screen.c
| 0x0.3000  | 0x0.5fff |  12k | unused
| 0x0.6000  | 0x0.6fff |   4k | OEPL settings
| 0x0.7000  | 0x0.ffff |  36k | OTA
| 0x1.0000  | 0x1.1fff |   8k | slot 0
| ...       |          |      |
| 0x1.e000  | 0x1.ffff |   8k | slot 7

*/
#define EEPROM_FACTORY_NVRAM_ADR    (0x00000UL)
#define EEPROM_FACTORY_NVRAM_LEN    (EEPROM_ERZ_SECTOR_SZ * 2)

#define EEPROM_FACTORY_WAVEFORM_ADR (0x02000UL)

#define EEPROM_SETTINGS_AREA_START  (0x06000UL)
#define EEPROM_SETTINGS_AREA_LEN    (EEPROM_ERZ_SECTOR_SZ)

#define EEPROM_UPDATA_AREA_START    (0x07000UL)
#define EEPROM_UPDATE_AREA_LEN      (0x09000UL)

#define EEPROM_IMG_START            (0x10000UL)
#define EEPROM_IMG_EACH             (0x02000UL)
#define EEPROM_IMG_SECTORS          (EEPROM_IMG_EACH / EEPROM_ERZ_SECTOR_SZ)
#define IMAGE_SLOTS                 ((EEPROM_SIZE - EEPROM_IMG_START)/EEPROM_IMG_EACH)

#include "../boardCommon.h"

#endif
