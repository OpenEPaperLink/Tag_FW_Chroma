// Chroma21 EPD support for the OEPL firmware.  The panel itself is driven by
// uc8154.c, here is what's specific to the firmware: powering the SPI flash,
// sleeping during refreshes and measuring the battery while refreshing.
#include "board.h"
#include "asmUtil.h"
#include "screen.h"
#include "printf.h"
#include "cpu.h"
#include "timer.h"
#include "sleep.h"
#include "powermgt.h"
#include "adc.h"
#include "eeprom.h"
#include "settings.h"
#include "logging.h"

uint8_t __xdata mScreenVcom;

__xdata __at (0xfda2) uint8_t gTempBuf320[320];  //350 bytes here, we use 320 of them

__bit gScreenPowered = false;
__bit gRedPass;

void P1INT_ISR(void) __interrupt (15)
{
   SLEEP &= (uint8_t)~(3 << 0);  //wake up
}

void uc8154DelayMs(uint8_t Ms)
{
   timerDelay((uint16_t) (TIMER_TICKS_PER_SECOND / 1000) * Ms);
}

void uc8154WfRead(uint16_t Ofst,uint8_t __xdata *Dst,uint8_t Len)
{
   eepromRead(EEPROM_FACTORY_WAVEFORM_ADR + Ofst,Dst,Len);
}

static void screenInitIfNeeded()
{
// drawImageAtAddress() has the SPI flash powered up, DrawScreen() doesn't
   __bit WasUp = gEEPROM_PoweredUp;

   if(gScreenPowered) {
      return;
   }
   gScreenPowered = true;
   powerUp(INIT_EEPROM);
   uc8154PowerOn(gTemperature);
   if(!WasUp) {
      powerDown(INIT_EEPROM);
   }

   LOG_CONFIG("screenInitIfNeeded");
}

void screenInitGPIO(bool bActivate)
{
// uc8154PowerOn() does the activation
   if(!bActivate) {
      uc8154PinsOff();
   }
}

void screenShutdown(void)
{
   if (!gScreenPowered) {
      return;
   }
   uc8154PowerOff();
   gScreenPowered = false;

   LOG_CONFIG("screenShutdown");
}

void screenTxStart()
{
   screenInitIfNeeded();
   uc8154TxStart(gRedPass);
}

// sleepForMsec() and clockingAndIntsInit() leave all interrupts off, restart
// the timer and its overflow irq so timerDelay() works again.  The EPD must
// stay powered so we can't use powerUp(INIT_BASE), PortInit() turns it off.
static void SleepMs(uint16_t Ms)
{
   sleepForMsec(Ms);
   clockingAndIntsInit();  // restart clocks
   timerInit();
   irqsOn();
}

// Refresh and sleep until the EPD isn't busy any more, return the lowest
// raw battery reading seen
static uint16_t RefreshWithSleep(uint16_t Lowest)
{
   uc8154Refresh(gRedPass);

   while(!EPD_BUSY()) {
   // Wake up every 50 milliseconds to check BattV and EPD_BUSY
      SleepMs(50);
      ADCRead(ADC_CHAN_VDD_3);
      if(Lowest > gRawA2DValue) {
         Lowest = gRawA2DValue;
      }
   }
   return Lowest;
}

void drawWithSleep()
{
#ifndef DISABLE_DISPLAY
   uint16_t Lowest;

   LOGA("Updating display\n");

   gRedPass = false;
   Lowest = RefreshWithSleep(0xffff);

   if(gAnyYellow) {
      uc8154SetupYellowRefresh();
      gRedPass = true;
      Lowest = RefreshWithSleep(Lowest);
   }
   gRefreshBattV = ADCScaleVDD(Lowest);

   Lowest = uc8154PowerOffStart();
   if(Lowest) {
      SleepMs(Lowest);
   }
   screenShutdown();
   powerUp(INIT_BASE);
   UpdateVBatt();
   LOGA("Update complete\n");
#endif
}
