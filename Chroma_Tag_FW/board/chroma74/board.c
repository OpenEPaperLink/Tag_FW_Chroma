#include <stdint.h>
#include "board.h"
#include "settings.h"

#ifdef RELEASE_BUILD
// NB: The patch utility searches for 0x56,0x12,0x09,0x85,0x01,0x08
// to find this structure so DO NOT change the order !
const uint8_t __code gDefaultEEPROM[] = {
   0x56, 0x12, 0x09, 0x85, // magic number
   0x01,8,'J','M',0,0,0,0, // Default to JM00000000 if we can't get the SN 
   0x23,3,0x28,            // VCOM
   0x09,4,0xec,0x02, // ADC intercept
   0x12,4,0x5a,0x0a, // ADC slope
// 0xff        // end of settings (erased EEPROM)
};
#endif

#include "../boardChroma.c"

#define P0_EPD_PINS (P0_EPD_BS1 | P0_EPD_D_nCMD)
#define P1_EPD_PINS (P1_EPD_DI | P1_EPD_SCK | P1_EPD_nRESET | P1_EPD_nCS0 | P1_EPD_BUSY)

void powerPortsDownForSleep(void)
{
   T1CTL = 0;  //timer off
   U1GCR = 0;  // Disable Rx

// Enable pullups on all P2 pins and select pullups (when enabled) on P0, P1
// (This shouldn't be necessary, this is the default)
   P2INP = 0;

// P1.1 is P1_EPD_nCS0 so we leave it as an output
   P1DIR = P1_EPD_nCS0;
// As far as we know all Chroma74 verions implement EPD power control

// Disable pullups on pins connected to the EPD
   P0INP = P0_EPD_PINS;
   P1INP = P1_EPD_PINS;
// Set all pins to input mode except the pins connected to the EPD
   P0DIR = P0_EPD_PINS;
   P1DIR = P1_EPD_PINS;
// drive EPD pins low prevent back powering the display
   P0 &= ~P0_EPD_PINS;
   P1 &= ~P1_EPD_PINS;

// Disconnect all perpherials from pins
   P0SEL = 0;
   P1SEL = 0;
   P2SEL = 0;
   COPY_CFG();
}

