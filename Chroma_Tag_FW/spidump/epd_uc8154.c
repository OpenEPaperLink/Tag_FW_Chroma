// spidump screen on a UC8154 panel, see dumpscreen.h.  The panel is driven by
// board/<board>/uc8154.c, the same code as the firmware.
//
// spidump.c has set PERCFG for USART0 alt 2 and all EPD pins low with the EPD
// power off.
#include "board.h"
#include "screen.h"
#include "uc8154.h"
#include "dumpscreen.h"

#if DS_SOURCES != SCREEN_WIDTH || DS_GATES != SCREEN_HEIGHT
#error "boards/<board>.h doesn't match board/<board>/screen.h"
#endif

// Timer 1 free running at 203.125 kHz (CLKCON tick speed /128)
#define TICKS_PER_MS    203

// the driver's scratch buffer, defined in screen.c in the firmware
__xdata __at (0xfda2) uint8_t gTempBuf320[320];

void uc8154DelayMs(uint8_t Ms)
{
   uint16_t Start;
   uint16_t Now;

   while(Ms--) {
      Start = T1CNTL;
      Start |= (uint16_t) T1CNTH << 8;
      do {
         Now = T1CNTL;
         Now |= (uint16_t) T1CNTH << 8;
      } while((uint16_t) (Now - Start) < TICKS_PER_MS);
   }
}

void uc8154WfRead(uint16_t Ofst,uint8_t __xdata *Dst,uint8_t Len)
{
   SpiRead(EEPROM_FACTORY_WAVEFORM_ADR + Ofst,Dst,Len);
}

static void Refresh(bool bYellow)
{
   uc8154Refresh(bYellow);
   uc8154WaitNotBusy();
}

void dumpScreenShow()
{
   uint16_t y;
   uint16_t Ms;
   uint8_t x;

   T1CTL = 0x01;        // timer 1 free running, /1
   if(!uc8154PowerOn(DS_WF_TEMP)) {
      return;           // no waveform tables, no screen
   }
   dsInit();

   uc8154TxStart(false);
   einkSelect();
   for(y = 0; y < DS_GATES; y++) {
      for(x = 0; x < DS_SOURCES / 8; x++) {
         dsPixels8(y,x);
         uc8154TxPixels(gDsBlack,gDsYellow);
      }
   }
   einkDeselect();

   uc8154TxStart(true);
   einkSelect();
   for(y = 0; y < DS_GATES; y++) {
      for(x = 0; x < DS_SOURCES / 8; x++) {
         dsPixels8(y,x);
         uc8154TxByte(~gDsYellow);
      }
   }
   einkDeselect();

   Refresh(false);
   uc8154SetupYellowRefresh();
   Refresh(true);

   for(Ms = uc8154PowerOffStart(); Ms != 0; Ms--) {
      uc8154DelayMs(1);
   }
   uc8154PowerOff();
}
