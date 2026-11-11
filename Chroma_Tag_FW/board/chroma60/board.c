#include <stdint.h>
#include "board.h"
#include "settings.h"

// NB: The patch utility searches for 0x56,0x12,0x09,0x85,0x01,0x08
// to find this structure so DO NOT change the order !
// VCOM and ADC calibration are from the factory NVRAM of a JD Chroma60.
const uint8_t __code gDefaultEEPROM[] = {
   0x56, 0x12, 0x09, 0x85, // magic number
   0x01,8,
   'J','D',0,0,0,0, // Default to JD00000000 if we can't get the SN 
};

#include "../boardChroma.c"
