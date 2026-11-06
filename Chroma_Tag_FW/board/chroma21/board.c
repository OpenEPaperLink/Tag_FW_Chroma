#include <stdint.h>
#include "board.h"
#include "settings.h"

#ifdef RELEASE_BUILD
// NB: The patch utility searches for 0x56,0x12,0x09,0x85,0x01,0x08
// to find this structure so DO NOT change the order !
const uint8_t __code gDefaultEEPROM[] = {
   0x56, 0x12, 0x09, 0x85, // magic number
   0x01,8,'J','G',0,0,0,0, // Default to JG00000000 if we can't get the SN
   0x09,4,DEFAULT_ADC_INTERCEPT & 0xff,DEFAULT_ADC_INTERCEPT >> 8, // ADC intercept
   0x12,4,DEFAULT_ADC_SLOPE & 0xff,DEFAULT_ADC_SLOPE >> 8, // ADC slope
// 0xff        // end of settings (erased EEPROM)
};
#endif

#include "../boardChroma.c"

#define P0_EPD_PINS (P0_EPD_BS1 | P0_EPD_D_nCMD)
#define P1_EPD_PINS (P1_EPD_DI | P1_EPD_SCK | P1_EPD_nRESET | P1_EPD_nCS0 | P1_EPD_BUSY)

// The Chroma21 switches the EPD power with P0.6 (EPD_nENABLE).  Like the
// stock firmware drive all EPD pins low while it's off so the EPD isn't
// back powered through them.
void powerPortsDownForSleep(void)
{
   T1CTL = 0;  //timer off
   U1GCR = 0;  // Disable Rx

// Enable pullups on all P2 pins and select pullups (when enabled) on P0, P1
   P2INP = 0;

// Disable pullups on pins connected to the EPD
   P0INP = P0_EPD_PINS;
   P1INP = P1_EPD_PINS;
// Like the stock firmware tristate P1.4 if it's pulled low externally
// (unknown use), so the pullup doesn't waste power while we sleep
   if(!P1_4) {
      P1INP |= P1_BIT_4;
   }
// Set all pins to input mode except the pins connected to the EPD
// and EPD_nENABLE which keeps the EPD power off
   P0DIR = P0_EPD_PINS | P0_EPD_nENABLE;
   P1DIR = P1_EPD_PINS;
// drive EPD pins low prevent back powering the display
   P0 &= ~P0_EPD_PINS;
   P0 |= P0_EPD_nENABLE;
   P1 &= ~P1_EPD_PINS;

// Disconnect all perpherials from pins
   P0SEL = 0;
   P1SEL = 0;
   P2SEL = 0;
   COPY_CFG();
}
