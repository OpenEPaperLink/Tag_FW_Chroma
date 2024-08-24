#include <stddef.h>
#include "board.h"
#include "asmUtil.h"
#include "screen.h"
#include "printf.h"
#include "cpu.h"
#include "timer.h"
#include "sleep.h"
#include "powermgt.h"
#include "adc.h"
#include "settings.h"
#include "logging.h"

// #define VERBOSE_DEBUGING
#ifdef VERBOSE_DEBUGING
   #define LOG_EPD_DATA(format, ... ) pr(format,## __VA_ARGS__)
#else
   #define LOG_EPD_DATA(format, ... )
#endif

/*
The commands were found to match the UC8176c fairly closely.  

  CS0 400 pixels wide
+--------------------------------+
|                                |
                                 | 300 high
|                                |
+--------------------------------+
*/

// uc8176 like commands
#define CMD_PANEL_SETTING 0x00
#define CMD_POWER_SETTING 0x01
#define CMD_POWER_OFF 0x02
#define CMD_POWER_OFF_SEQUENCE 0x03
#define CMD_POWER_ON 0x04
#define CMD_POWER_ON_MEASURE 0x05
#define CMD_BOOSTER_SOFT_START 0x06
#define CMD_DEEP_SLEEP 0x07
#define CMD_DISPLAY_START_TRANSMISSION_DTM1 0x10
#define CMD_DATA_STOP 0x11
#define CMD_DISPLAY_REFRESH 0x12
#define CMD_DISPLAY_START_TRANSMISSION_DTM2 0x13
// note:  LUT format is described in the IL0373 spec sheet
#define CMD_VCOM_LUT    0x20
#define CMD_W2W_LUT     0x21
#define CMD_B2W_LUT     0x22
#define CMD_W2B_LUT     0x23
#define CMD_B2B_LUT     0x24
#define CMD_PLL_CONTROL 0x30
#define CMD_TEMPERATURE_CALIB 0x40
#define CMD_TEMPERATURE_SELECT 0x41
#define CMD_TEMPERATURE_WRITE 0x42
#define CMD_TEMPERATURE_READ 0x43
#define CMD_VCOM_INTERVAL 0x50
#define CMD_LOWER_POWER_DETECT 0x51
#define CMD_TCON_SETTING 0x60
#define CMD_RESOLUTION_SETTING 0x61
#define CMD_REVISION 0x70
#define CMD_STATUS   0x71
#define CMD_AUTO_MEASUREMENT_VCOM 0x80
#define CMD_READ_VCOM 0x81
#define CMD_VCOM_DC_SETTING 0x82
#define CMD_PARTIAL_WINDOW 0x90
#define CMD_PARTIAL_IN 0x91
#define CMD_PARTIAL_OUT 0x92
#define CMD_PROGRAM_MODE 0xA0
#define CMD_ACTIVE_PROGRAM 0xA1
#define CMD_READ_OTP 0xA2
#define CMD_CASCADE_SET 0xE0
#define CMD_POWER_SAVING 0xE3
#define CMD_FORCE_TEMPERATURE 0xE5

uint8_t __xdata mScreenVcom;
uint8_t __xdata gSentTxCount;


__xdata __at (0xfda2) uint8_t gTempBuf320[320];  //350 bytes here, we use 320 of them

__bit gScreenPowered = false;
__bit gRedPass;

void SendEpdTbl(uint8_t const __code *pData);

static const uint8_t __code gPwrUpEpd[] = {
   2,
   CMD_POWER_OFF_SEQUENCE,
   0x00,

   4, 
   CMD_BOOSTER_SOFT_START, // Booster Soft Start (BTST)
   0x0e,0xcd,0x26,

   2,
   CMD_PANEL_SETTING,
   0x2f, // 00 1 0 1 1 1 1
         // ^^ ^ ^ ^ ^ ^ ^
         //  | | | | | | |
         //  | | | | | | +-- RST_N controller not 
         //  | | | | | +---- SHD_N DC-DC converter on
         //  | | | | +------ SHL Shift left
         //  | | | +-------- UD scan down
         //  | | +---------- BWR 
         //  | +------------ LUT from register
         //  +-------------- RES 400x300

   2,
   CMD_TEMPERATURE_SELECT,
   0x00,

   2,
   CMD_VCOM_INTERVAL,
   0x87, // 10 00 0111             
         // ^^ ^^ ^^^^
         //  |  |    |
         //  |  |    +-- CDI Vcom & Data Interval 10
         //  |  +------- DDX 00 = zero BW pixel = white, red overrides white
         //  +---------- VBD 10 white boarder

   2,
   CMD_TCON_SETTING,
   0x22,

   43,
   CMD_W2W_LUT,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,

   5,
   CMD_RESOLUTION_SETTING,
   0x01,0x90,  // HRES 400
   0x01,0x2C,  // VRES 300

   0
};

static const uint8_t __code gBWY[] = {
// Temperature & color dependent settings ---------------- VVVVVVVVVV
   6,
   CMD_POWER_SETTING,
   0x03,0x00,0x2B,0x2B,0x0A,

   2,
   CMD_PLL_CONTROL,
   0x3A,

   45,
   CMD_VCOM_LUT,
   0x00,0x18,0x00,0x00,0x00,0x01,0x00,0x84,
   0x84,0x00,0x00,0x01,0x00,0x0A,0x0A,0x00,
   0x00,0x1E,0x00,0x50,0x02,0x50,0x02,0x05,
   0x00,0x03,0x02,0x06,0x02,0x24,0x00,0x0A,
   0x05,0x32,0x05,0x06,0x00,0x05,0x06,0x32,
   0x03,0x08,0x00,0x00,

   43,
   CMD_W2B_LUT,
   0x00,0x18,0x00,0x00,0x00,0x01,0x10,0x84,
   0x84,0x00,0x00,0x01,0x60,0x0A,0x0A,0x00,
   0x00,0x1E,0x48,0x50,0x02,0x50,0x02,0x05,
   0x80,0x03,0x02,0x06,0x02,0x24,0x00,0x0A,
   0x05,0x32,0x05,0x06,0x02,0x05,0x06,0x32,
   0x03,0x08,

   43,
   CMD_B2B_LUT,
   0x00,0x18,0x00,0x00,0x00,0x01,0xA0,0x84,
   0x84,0x00,0x00,0x01,0x60,0x0A,0x0A,0x00,
   0x00,0x1E,0x48,0x50,0x02,0x50,0x02,0x05,
   0x04,0x03,0x02,0x06,0x02,0x24,0x00,0x0A,
   0x05,0x32,0x05,0x06,0x10,0x05,0x06,0x32,
   0x03,0x08,

   43,
   CMD_B2W_LUT,
   0x80,0x18,0x00,0x00,0x00,0x01,0xA0,0x84,
   0x84,0x00,0x00,0x01,0x60,0x0A,0x0A,0x00,
   0x00,0x1E,0x48,0x50,0x02,0x50,0x02,0x05,
   0x84,0x03,0x02,0x06,0x02,0x24,0x8C,0x0A,
   0x05,0x32,0x05,0x06,0x8C,0x05,0x06,0x32,
   0x03,0x08,

   2,
   CMD_VCOM_DC_SETTING,
   0x12,

   0
};

static const uint8_t __code gBWR[] = {
   6,
   CMD_POWER_SETTING,
   0x03,0x00,0x2B,0x2B,0x0c,

   2,
   CMD_PLL_CONTROL,
   0x29,

   45,
   CMD_VCOM_LUT,
   0x00,0x84,0xc8,0x84,0xaa,0x01,
   0x60,0x23,0x23,0x00,0x00,0x0a,
   0x60,0x0e,0x0e,0x00,0x00,0x19,
   0x60,0x04,0x04,0x00,0x00,0x14,
   0x00,0x09,0x3c,0x14,0x00,0x04,
   0x00,0x07,0x3c,0x14,0x00,0x08,
   0x00,0x05,0x64,0x64,0x32,0x01,
   0x00,0x00,

   43,
   CMD_W2B_LUT,
   0x01,0x84,0xc8,0x84,0xaa,0x01,
   0x90,0x23,0x23,0x00,0x00,0x0a,
   0x90,0x0e,0x0e,0x00,0x00,0x19,
   0xa0,0x04,0x04,0x00,0x00,0x14,
   0x00,0x50,0x50,0x28,0x00,0x06,
   0x00,0x06,0x0a,0x29,0x00,0x01,
   0x80,0x02,0x08,0x00,0x00,0x05,

   43,
   CMD_B2B_LUT,
   0x20,0x84,0xc8,0x84,0xaa,0x01,
   0x90,0x23,0x23,0x00,0x00,0x0a,
   0x90,0x0e,0x0e,0x00,0x00,0x19,
   0x50,0x04,0x04,0x00,0x00,0x14,
   0x00,0x50,0x50,0x28,0x00,0x06,
   0x00,0x06,0x0a,0x29,0x00,0x01,
   0x10,0x02,0x08,0x00,0x00,0x05,

   43,
   CMD_B2W_LUT,
   0xa8,0x84,0xc8,0x84,0xaa,0x01,
   0x90,0x23,0x23,0x00,0x00,0x0a,
   0x90,0x0e,0x0e,0x00,0x00,0x19,
   0x50,0x04,0x04,0x00,0x00,0x14,
   0xb0,0x09,0x3c,0x14,0x00,0x04,
   0xb0,0x07,0x3c,0x14,0x00,0x08,
   0xbc,0x05,0x64,0x64,0x32,0x01,

   2,
   CMD_VCOM_DC_SETTING,
   0x1e,

   0
};
// Temperature & color dependent settings ---------------- ^^^^^^^

static const uint8_t __code gPwrOffEpd[] = {
   1,
   CMD_POWER_OFF,
   0
};

static const uint8_t __code gPwrOffEpd1[] = {
   2,
   CMD_DEEP_SLEEP,
   0xa5,

   0
};

static const uint8_t __code gStartRefresh[] = {
   1,
   CMD_DISPLAY_REFRESH,

   0
};


#pragma callee_saves screenPrvSendCommand
static inline void screenPrvSendCommand(uint8_t cmdByte)
{
   screenPrvWaitByteSent();
   LOG_EPD_DATA("%sCmd ",(gSentTxCount & 0xf) != 0 ? "\n":"");
   gSentTxCount = 1;

   P0 &= (uint8_t) ~P0_EPD_D_nCMD;
   __asm__("nop");
   screenByteTx(cmdByte);
   __asm__("nop");
   screenPrvWaitByteSent();
   P0 |= P0_EPD_D_nCMD;
}

void P1INT_ISR(void) __interrupt (15)
{
   SLEEP &= (uint8_t)~(3 << 0);  //wake up
}

// <CommandBytes> <Command> [<Data>] ...]
// Send to both controllers
void SendEpdTbl(uint8_t const __code *pData)
{
   uint8_t CmdBytes;

   while((CmdBytes = *pData++) != 0) {
      einkSelect();
      screenPrvSendCommand(*pData++);
      CmdBytes--;
      while(CmdBytes > 0) {
         screenPrvWaitByteSent();
         screenByteTx(*pData++);
         CmdBytes--;
      }
      einkDeselect();
   }
}

static void screenInitIfNeeded()
{
   if(gScreenPowered) {
      return;
   }
   gScreenPowered = true;
   
// Don't select the EPD yet
   P1 |= P1_EPD_nCS0;
   P0 |= P0_EPD_nCS1;

// turn on the eInk power (keep in reset for now)
   P0 &= (uint8_t) ~P0_EPD_nENABLE;

// Connect the P1 EPD pins to USART0
   P1SEL |= P1_EPD_SCK | P1_EPD_DI;
   
   U0BAUD = 0;          // F/8 is max for spi - 3.25 MHz
   U0GCR = 0b00110001;  // BAUD_E = 0x11, msb first
   U0CSR = 0b00000000;  // SPI master mode, RX off
   
   timerDelay(TIMER_TICKS_PER_SECOND * 10 / 1000); //wait 10ms
   
// release reset
   P1 |= P1_EPD_nRESET;
   timerDelay(TIMER_TICKS_PER_SECOND * 10 / 1000); //wait 10ms
   
// Wait for Busy to go high
   while(!EPD_BUSY());
   
// we can now talk to it
   SendEpdTbl(gPwrUpEpd);

   if(mSelfMac[4] == ('C' - 'A')) {
   // SN == JC1xxxxxxx BWR
      LOG("0x%x BWR\n",mSelfMac[4]);
      SendEpdTbl(gBWR);
   }
   else {
      LOG("0x%x BWY\n",mSelfMac[4]);
      SendEpdTbl(gBWY);
   }
   
   LOG_CONFIG("screenInitIfNeeded");
}

void screenShutdown(void)
{
   if (!gScreenPowered) {
      return;
   }
   
   SendEpdTbl(gPwrOffEpd);

// Wait for Busy to go high again
   while(!EPD_BUSY());

   SendEpdTbl(gPwrOffEpd1);

// Reconfigure the EPD SPI pins as GPIO
// Disconnect the P1 EPD SPI pins from USART0 and connect them to GPIO
   P1SEL &= ~(P1_EPD_SCK | P1_EPD_DI);

// Drive all of the control pins LOW to avoid back powering the EPD via the 
// control pins

   P0 &= ~(P0_EPD_BS1 | P0_EPD_nCS1 | P0_EPD_D_nCMD);
   P1 &= ~(P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI);
// Turn off power to the EPD
   SET_EPD_nENABLE(1);
   
   gScreenPowered = false;

   LOG_CONFIG("screenShutdown");
}

void screenTxStart()
{
   screenInitIfNeeded();
   
// Send command to both controllers   
   einkSelect();
   screenPrvSendCommand(gRedPass ? CMD_DISPLAY_START_TRANSMISSION_DTM2 :
                                   CMD_DISPLAY_START_TRANSMISSION_DTM1);
   einkDeselect();     // Start with CS0
}


#pragma callee_saves screenByteTx
void screenByteTx(uint8_t byte)
{
   screenPrvWaitByteSent();
   U0DBUF = byte;
   gSentTxCount++;
   LOG_EPD_DATA("%02x%s",byte,(gSentTxCount & 0xf) == 0 ? "\n" : " ");
}

void drawWithSleep() 
{
#ifndef DISABLE_DISPLAY
   uint16_t Lowest = 0xffff;

   LOGA("Updating display\n");

// Power up display
   einkSelect();
   screenPrvSendCommand(CMD_POWER_ON);
   einkDeselect();

// Wait for Busy to go high
   while(!EPD_BUSY());
   SendEpdTbl(gStartRefresh);

   while(!EPD_BUSY()) {
   // Wake up every 50 milliseconds to check BattV and EPD_BUSY
      sleepForMsec(50UL);
      clockingAndIntsInit();  // restart clocks
      ADCRead(ADC_CHAN_VDD_3);
      if(Lowest > gRawA2DValue) {
         Lowest = gRawA2DValue;
      }
   }
   gRefreshBattV = ADCScaleVDD(Lowest);
   screenShutdown();
   UpdateVBatt();
   LOGA("Update complete\n");
#endif
}

