#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdbool.h>
#include <stdint.h>
#include "uc8154.h"

extern uint8_t __xdata mScreenVcom;
extern int8_t __xdata mCurTemperature;
extern __bit gRedPass;
extern __bit gDrawFromFlash;
extern __bit gAnyYellow;

// Physical: 104 sources x 212 gates
#define SCREEN_WIDTH             104L
#define SCREEN_HEIGHT            212L

// Logical
#define DISPLAY_WIDTH             SCREEN_HEIGHT
#define DISPLAY_HEIGHT            SCREEN_WIDTH

// or 21 x 6 characters with a 10 x 16 font

/*
   Same mapping as the Chroma29:
   Logical->rotated right 90 deg and mirrored -> Physical
   13 bytes per physical line
   physical X = logical y
   physical Y = 211 - logical x
*/
#define SCREEN_ROTATED           0

// UI screen layout constants

// Compact layouts in userinterface.c for 212 x 104 (preview them with
// tools/ui_preview.py).  The side logo is scaled to 96 pixels
// (common/bitmaps_small.h), 38 pixels wide.
#define UI_SMALL
// \f centers text in the room left of the side logo
#define TEXT_WIDTH               (DISPLAY_WIDTH - 40)

// Location of OELP logo (CloudTop)
#define LOGO_ON_RIGHT            1
#define SPLASH_LOGO_X            DISPLAY_WIDTH
#define SPLASH_LOGO_Y            ((DISPLAY_HEIGHT - 96) / 2)
#define SPLASH_ADJ_START(BMP)    gBmpX -= BMP##[0]

#define SCREEN_BYTE_FILL         0x66  //white for normal mode

#define EPD_COLOR_BLACK          0
#define EPD_COLOR_RED            1

#define EPD_DIRECTION_X          0
#define EPD_DIRECTION_Y          1

#define EPD_SIZE_SINGLE          0
#define EPD_SIZE_DOUBLE          1

void screenInitGPIO(bool bActivate);
void screenShutdown(void);
void screenTxStart();

void drawWithSleep(void);

void P1INT_ISR(void) __interrupt (15);


// 350 bytes here which do not retain data in power modes PM2 or PM3,
// we use 320 of them for mScreenRow which doesn't need to be retained
__xdata __at (0xfda2) uint8_t mScreenRow[320];

#endif
