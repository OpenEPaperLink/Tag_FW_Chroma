// Optional screen for spidump (build.sh --screen <board>), see docs/spidump.md
//
// Layers, so another board only needs a board header (and maybe a driver):
//   spidump.c          generic dumper, calls dumpScreenShow() when done
//   dumpscreen.c       generic content: text, serial number, test patches,
//                      computed per pixel in logical (OEPL) coordinates
//   epd_<driver>.c     panel glue: dumpScreenShow() streams dsPixels8() to
//                      the board's panel driver and refreshes it
//   boards/<board>.h   geometry, logical mapping
//
// The board header (DS_BOARD_H) must define:
//   DS_DRIVER             picks epd_<driver>.c, and board/<board>/<driver>.c
//                         if there is one
//   DS_SOURCES, DS_GATES  physical line width (pixels) and number of lines
//   DS_WIDTH, DS_HEIGHT   logical size
//   DS_LX(py,px), DS_LY(py,px)  logical x/y of physical pixel px of line py,
//                         the same mapping as board/<board>/drawing.c
#ifndef _DUMPSCREEN_H_
#define _DUMPSCREEN_H_

#include <stdint.h>
#include DS_BOARD_H

// pointer to CC1110 flash, the dumped data is at 0x1000 +
#ifdef DS_HOST_TEST
#define __xdata
#define __code
extern uint8_t gHostCode[0x8000];
extern uint8_t gDumpTagData[];
#define DS_CODE_PTR(a)     (&gHostCode[(a)])
#define gDumpTag           gDumpTagData
#else
#define DS_CODE_PTR(a)     ((const uint8_t __code *) (uint16_t) (a))
#endif

#include "spidump.h"

// set by dsPixels8(): 8 pixels of line py from pixel x8 * 8, MSB first
extern uint8_t __xdata gDsBlack;
extern uint8_t __xdata gDsYellow;

void dsInit(void);
void dsPixels8(uint16_t py,uint8_t x8);

// provided by the panel glue
void dumpScreenShow(void);

// provided by spidump.c
void SpiRead(uint32_t Adr,uint8_t __xdata *Dst,uint8_t Len);

#endif
