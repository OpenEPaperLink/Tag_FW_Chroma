#ifndef _BOARD_COMMON_H_
#define _BOARD_COMMON_H_

#include <stdint.h>

#include "boardPins.h"

extern uint8_t __xdata mSelfMac[];
extern char __xdata gMacString[17];

extern __xdata __at (0xfda2) uint8_t gTempBuf320[320];

/* SDCC 4.2 emits unused out-of-line copies of static inline functions.
 * Statement macros retain the SPI timing without those duplicate bodies. */
#define screenPrvWaitByteSent() do { \
   while (U0CSR & 0x01); \
} while (0)

#define einkSelect() do { \
   P1 &= (uint8_t)~P1_EPD_nCS0; \
   __asm__("nop"); /* select setup time */ \
} while (0)

#define einkSelect1() do { \
   P0 &= (uint8_t)~P0_EPD_nCS1; \
   __asm__("nop"); \
} while (0)

#define einkDeselect() do { \
   screenPrvWaitByteSent(); \
   __asm__("nop"); /* hold time before deselect */ \
   P1 |= P1_EPD_nCS0; \
   __asm__("nop"); /* delay before reselect */ \
} while (0)

#define einkDeselect1() do { \
   screenPrvWaitByteSent(); \
   __asm__("nop"); \
   P0 |= P0_EPD_nCS1; \
   __asm__("nop"); \
} while (0)

#pragma callee_saves powerPortsDownForSleep
void powerPortsDownForSleep(void);

//early - before most things
#pragma callee_saves boardInitStage2
void boardInitStage2(void);

void PortInit(void);

//late, after eeprom
void InitBcastFrame(void);
void UpdateBcastFrame(void);

//some sanity checks
#include "eeprom.h"


#if !EEPROM_SETTINGS_AREA_START
   #error "settings cannot be at address 0"
#endif

#if (EEPROM_SETTINGS_AREA_LEN % EEPROM_ERZ_SECTOR_SZ) != 0
   #error "settings area must be an integer number of eeprom blocks"
#endif

#if (EEPROM_SETTINGS_AREA_START % EEPROM_ERZ_SECTOR_SZ) != 0
   #error "settings must begin at an integer number of eeprom blocks"
#endif

#if (EEPROM_IMG_EACH % EEPROM_ERZ_SECTOR_SZ) != 0
   #error "each image must be an integer number of eeprom blocks"
#endif

#if (EEPROM_IMG_START % EEPROM_ERZ_SECTOR_SZ) != 0
   #error "images must begin at an integer number of eeprom blocks"
#endif

#if (EEPROM_UPDATE_AREA_LEN % EEPROM_ERZ_SECTOR_SZ) != 0
   #error "update must be an integer number of eeprom blocks"
#endif

#if (EEPROM_UPDATA_AREA_START % EEPROM_ERZ_SECTOR_SZ) != 0
   #error "images must begin at an integer number of eeprom blocks"
#endif

#endif
