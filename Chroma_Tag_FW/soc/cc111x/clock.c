#include "cc111x.h"
#include "clock.h"

void clockingAndIntsInit(void)
{
   uint8_t i, j;
   
   IEN0 = 0;
   IEN1 = 0;
   IEN2 = 0;

// enable cache and prefetch
   MEMCTR = 0; 
   
   SLEEP = 0;              //SLEEP.MODE = 0 to use HFXO
   while (!(SLEEP & SLEEP_HFRC_S));   //wait for HFRC to stabilize
// high speed RC osc, timer clock is 203.125KHz, 13MHz system clock
//   CLKCON = 0x79;          //high speed RC osc, timer clock is 203.125KHz, 13MHz system clock
   CLKCON = CLKCON_OSC | CLKCON_TICKSPD(7) | CLKCON_CLKSPD(1);
   while (!(SLEEP & SLEEP_XOSC_S));   //wait for HFXO to stabilize
   
   //we need to delay more (chip erratum)
   for (i = 0; i != 128; i++) for (j = 0; j != 128; j++) __asm__ ("nop");
   
// switch to HFXO
   CLKCON = CLKCON_CLKSPD(1) | CLKCON_TICKSPD(7);
   while(CLKCON & CLKCON_OSC);   //wait for the switch
// Select 26MHz system clock and 203.125KHz timer clock
   CLKCON = CLKCON_CLKSPD(0) | CLKCON_TICKSPD(7);

   SLEEP = SLEEP_OSC_PD;   //power down the unused (HFRC oscillator)
}
