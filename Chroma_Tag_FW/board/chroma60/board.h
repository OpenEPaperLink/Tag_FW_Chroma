#ifndef _BOARD_H_
#define _BOARD_H_

#include "soc.h"
#include "u1.h"

#define CHROMA60
#define BOARD_NAME "Chroma60"
// The AP needs resources/tagtypes/84.json to recognize the tag.
#define HW_TYPE    0x84

// Defaults for the factory NVRAM, used in gDefaultEEPROM and when a
// calibration value is missing from the NVRAM
#define DEFAULT_VCOM           0x20
#define DEFAULT_ADC_INTERCEPT  0x02e9
#define DEFAULT_ADC_SLOPE      0x0a5a

#ifdef HW_VARIANT
   #error "Chroma60 has no HW variants"
#endif
#define BWR

extern const char * __code gBoardName;
extern const uint8_t __code gDefaultEEPROM[];

//eeprom spi
// MX25V8006E, 1MB, same pinout as the Chroma74
#define EEPROM_SIZE              0x00100000L
#define EEPROM_4K_ERASE_OPCODE   0x20
#define EEPROM_32K_ERASE_OPCODE  0
#define EEPROM_64K_ERASE_OPCODE  0xD8
#define eepromByte               u1byte
#define eepromPrvSelect()     do { __asm__("nop"); P2_0 = 0; __asm__("nop"); } while(0)
#define eepromPrvDeselect()      do { __asm__("nop"); P2_0 = 1; __asm__("nop"); } while(0)

//debug uart (enable only when needed, on some boards it inhibits eeprom access)
#define dbgUartOn()                 u1setUartMode()
#define dbgUartOff()                u1setEepromMode()
#define dbgUartByte                 u1byte

/* eeprom map
 
EEPROM size: 0x10.0000 / 1024k / 1 Megabyte 
Image size:  (600 x 448 x 2 bits/pixel) / 8 bit / byte = 67200 bytes 
plus image header, rounded up to erase boundary = 69,632 / 0x1.1000 
Image slots: 13 
 
| Start Adr | End Adr  | Size | Usage |
|     -     |    -     |   -  |   -   |
| 0x0.0000  | 0x0.1fff |   8k | Factory NVRAM
| 0x0.2000  | 0x0.5fff |  16k | Factory LUT ???
| 0x0.6000  | 0x0.bfff |  24k | unused ???
| 0x0.c000  | 0x0.cfff |   4k | OEPL Settings
| 0x0.d000  | 0x0.dfff |   4k | OEPL NVRAM
| 0x0.e000  | 0x0.ffff |   8k | unused
| 0x1.0000  | 0x1.8fff |  36k | OTA FW update
| 0x1.9000  | 0x2.9fff |  68k | Image slot 0
|    ...    |   ...    |  68k | Image slots 1 - 11
| 0xe.5000  | 0xf.5fff |  68k | Image slot 12
| 0xf.6000  | 0xf.ffff |  40k | unused

*/
#define EEPROM_FACTORY_NVRAM_ADR       (0x00000UL)
#define EEPROM_FACTORY_NVRAM_LEN       (EEPROM_ERZ_SECTOR_SZ * 2)

#define EEPROM_SETTINGS_AREA_START     (0x0c000UL)
#define EEPROM_SETTINGS_AREA_LEN       (EEPROM_ERZ_SECTOR_SZ)

#define EEPROM_OEPL_NVRAM_ADR          (0x0d000UL)
#define EEPROM_OEPL_NVRAM_LEN          (EEPROM_ERZ_SECTOR_SZ)
//some free space here

#define EEPROM_UPDATA_AREA_START    (0x10000UL)
#define EEPROM_UPDATE_AREA_LEN      (0x09000UL)

#define EEPROM_IMG_START            (0x19000UL)
#define EEPROM_IMG_EACH             (0x11000UL)
#define EEPROM_IMG_SECTORS          (EEPROM_IMG_EACH / EEPROM_ERZ_SECTOR_SZ)
#define IMAGE_SLOTS                 ((EEPROM_SIZE - EEPROM_IMG_START)/EEPROM_IMG_EACH)

#define screenInitGPIO(x)
#include "../boardCommon.h"

#endif
