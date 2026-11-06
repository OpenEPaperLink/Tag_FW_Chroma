#ifndef _BOARD_PINS_H_
#define _BOARD_PINS_H_

// Pin assignments, the same on all CC1110 Chroma boards.  Needs only the SFR
// definitions from cc111x.h so the tools in spidump/ can use it too.

#define SET_EPD_BS1(x)     do { __asm__("nop"); P0_0 = x; __asm__("nop"); } while(0)
#define SET_EPD_nCS(x)     do { __asm__("nop"); P1_1 = x; __asm__("nop"); } while(0)
#define SET_EPD_nCS1(x)    do { __asm__("nop"); P0_2 = x; __asm__("nop"); } while(0)
#define EPD_BUSY()         (P1_0)
#define SET_EPD_nRST(x)    do { __asm__("nop"); P1_2 = x; __asm__("nop"); } while(0)
#define SET_EPD_nENABLE(x) do { __asm__("nop"); P0_6 = x; __asm__("nop"); } while(0)
#define SET_EPD_DAT_CMD(x) do { __asm__("nop"); P0_7 = x; __asm__("nop"); } while(0)
#define SET_EEPROM_CS(x)   do { __asm__("nop"); P2_0 = x; __asm__("nop"); } while(0)

// P0 bits
#define P0_EPD_BS1         0x01  // P0.0
#define P0_TP6             0x02
#define P0_EPD_nCS1        0x04  // some boards needing two EPD cs
#define P0_EEPROM_CLK      0x08
#define P0_EEPROM_MOSI     0x10
#define P0_EEPROM_MISO     0x20
#define P0_EPD_nENABLE     0x40
#define P0_EPD_D_nCMD      0x80

// P1 bits
#define P1_EPD_BUSY        0x01  // P1.0
#define P1_EPD_nCS0        0x02
#define P1_EPD_nRESET      0x04
#define P1_EPD_SCK         0x08
#define P1_BIT_4           0x10  // unknown usage, n/c on some boards
#define P1_EPD_DI          0x20
#define P1_SERIAL_OUT      0x40
#define P1_SERIAL_IN       0x80

// P2 bits
#define P2_EEPROM_nCS      0x01  // P2.0
#define P2_DBG_DAT         0x02
#define P2_DBG_CLK         0x04
#define P2_XOSC32_Q1       0x08
#define P2_XOSC32_Q2       0x10

#endif
