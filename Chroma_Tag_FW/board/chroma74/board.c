#include <stdint.h>
#include "board.h"
#include "settings.h"

// NB: The patch utility searches for 0x56,0x12,0x09,0x85,0x01,0x08
// to find this structure so DO NOT change the order !
#ifndef HW_VARIANT
const uint8_t __code gDefaultEEPROM[] = {
   0x56, 0x12, 0x09, 0x85, // magic number
   0x01,8,
   'J','M',0,0,0,0, // Default to JM00000000 if we can't get the SN 
};
#elif HW_VARIANT == 1
const uint8_t __code gDefaultEEPROM[] = {
   0x56, 0x12, 0x09, 0x85, // magic number
   0x01,8,
   'J','L',0,0,0,0, // Default to JL00000000 if we can't get the SN 
};
#else
   #error "HW_VARIANT Invalid"
#endif

#include "../boardChroma.c"

