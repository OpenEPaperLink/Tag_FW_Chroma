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

// uc8151D like commands
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
#define CMD_LUTC        0x20
#define CMD_LUTWW       0x21
#define CMD_LUTR        0x22
#define CMD_LUTW        0x23
#define CMD_LUTB        0x24
#define CMD_LUTOPT      0x2a
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
#define CMD_STATUS 0x71
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

#ifdef DEBUG_SCREEN_INIT
uint8_t __xdata gDataByteLogged;
void LogEpdData(uint8_t DataByte);
#define LOG_EPD_DATA(x) LogEpdData(x)
#else
#define LOG_EPD_DATA(x)
#endif




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
   0x17,0x17,0x1e,
   0     // end of table
};

static const uint8_t __code gSetupEpd[] = {
   3,
   CMD_PANEL_SETTING,   // Panel Setting (PSR)
   0xa3, // 10 1 0 0 0 1 1
         // ^^ ^ ^ ^ ^ ^ ^- RST_N controller not reset
         //  | | | | | +--- SHD_N DC-DC converter on
         //  | | | | +----- SHL Shift left
         //  | | | +------- UD scan down
         //  | | +--------- KW/R Pixel with Black/White/Red, KWR mode
         //  | +----------- LUT from register
         //  +------------- RES 128x296
   0x89,

   2,
   CMD_TEMPERATURE_SELECT,
   0x00,

   2,
   CMD_VCOM_INTERVAL,
   0x57,

   2,
   CMD_TCON_SETTING,
   0x22,

   2,
   CMD_VCOM_DC_SETTING,
   0x12,

   3,
   CMD_LUTOPT,
   0x80,0x00,
    
// opcode 4: write 35 (0x23) bytes of zeros to LUT_WW (0x21)
   36,
   CMD_LUTWW,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,

   4,
   CMD_RESOLUTION_SETTING, // RESOLUTION SETTING (TRES)
   0x80,       // 0x80 = 128
   0x01,0x28,  // 0x128 = 296

   6,
   CMD_POWER_SETTING,
   0x03,0x00,0x2b,0x2b,0x09,

   2,
   CMD_PLL_CONTROL,  // PLL control (PLL)
   0x21,

   61,
   CMD_LUTC,
   0x00,0x0f,0x16,0x1f,0x3e,0x01,0x00,0x28,
   0x28,0x00,0x00,0x0a,0x00,0x0a,0x0a,0x00,
   0x00,0x19,0x00,0x02,0x03,0x00,0x00,0x19,
   0x00,0x03,0x28,0x00,0x00,0x08,0x00,0x8e,
   0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,

   37,
   CMD_LUTW,
   0x01,0x0f,0x16,0x1f,0x3e,0x01,0x90,0x28,
   0x28,0x00,0x00,0x0a,0x90,0x0a,0x0a,0x00,
   0x00,0x19,0x80,0x02,0x03,0x00,0x00,0x19,
   0x00,0x39,0x00,0x00,0x00,0x08,0x80,0x02,
   0x03,0x00,0x00,0x06,

   37,
   CMD_LUTB,
   0x0a,0x0f,0x16,0x1f,0x3e,0x01,0x90,0x28,
   0x28,0x00,0x00,0x0a,0x90,0x0a,0x0a,0x00,
   0x00,0x19,0x10,0x02,0x03,0x00,0x00,0x19,
   0x00,0x39,0x00,0x00,0x00,0x08,0x10,0x02,
   0x03,0x00,0x00,0x06,

   61,
   CMD_LUTR,
   0xaa,0x0f,0x16,0x1f,0x3e,0x01,0x90,0x28,
   0x28,0x00,0x00,0x0a,0x90,0x0a,0x0a,0x00,
   0x00,0x19,0x90,0x02,0x03,0x00,0x00,0x19,
   0xb0,0x03,0x28,0x00,0x00,0x08,0xc0,0x8e,
   0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x00,0x00,0x00,0x00,

   0     // end of table
};


static const uint8_t __code gPwrOffEpd[] = {
   1,
   CMD_POWER_OFF,
   0     // end of table
};

static const uint8_t __code gPwrOffEpd1[] = {
   1,
   CMD_DEEP_SLEEP,
   0     // end of table
};


static const uint8_t __code gStartRefresh[] = {
   1,
   CMD_DISPLAY_REFRESH,
   0     // end of table
};


#pragma callee_saves screenPrvSendCommand
static inline void screenPrvSendCommand(uint8_t cmdByte)
{
   screenPrvWaitByteSent();
#ifdef DEBUG_SCREEN_INIT
   if(gDataByteLogged > 0) {
      SCR_INIT_LOG("\n");
      gDataByteLogged = 0;
   }
   SCR_INIT_LOG("Cmd 0x%02x\n",cmdByte);
#endif

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
      SCR_INIT_LOG("select\n");
      einkSelect();
      screenPrvSendCommand(*pData++);
      CmdBytes--;
      while(CmdBytes > 0) {
         screenPrvWaitByteSent();
         LOG_EPD_DATA(*pData);
         U0DBUF = *pData++;
         CmdBytes--;
      }
      SCR_INIT_LOG("deselect\n");
      einkDeselect();
   }
}

static void screenInitIfNeeded()
{
   if(gScreenPowered) {
      return;
   }
   gScreenPowered = true;
   screenInitGPIO(true);
   
// Don't select the EPD yet
   P1 |= P1_EPD_nCS0;

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
   
// wait for not busy
   SCR_INIT_LOG("busy wait..");
   while(!EPD_BUSY());
   SCR_INIT_LOG("done\n");
   
// we can now talk to it
   SendEpdTbl(gPwrUpEpd);
   
// Send power on command 
   einkSelect();
   screenPrvSendCommand(CMD_POWER_ON);

// wait for busy to go high
   SCR_INIT_LOG("busy wait 1..");
   while(!EPD_BUSY());
   SCR_INIT_LOG("done\n");
   einkDeselect();
   
   SendEpdTbl(gSetupEpd);

   LOG_CONFIG("screenInitIfNeeded");
}

void screenInitGPIO(bool bActivate)
{
   if(bActivate) {
   // Nothing special to do
      if(!(mSelfMac[3] & 0xf0)) {
      // JA0xxxxxxx devices do not implement EPD power control.
      // 
      // Configure all control pins as outputs and disable pull-up resistors 
         P0INP |= P0_EPD_BS1 | P0_EPD_nENABLE | P0_EPD_D_nCMD;
         P0DIR |= P0_EPD_BS1 | P0_EPD_nENABLE | P0_EPD_D_nCMD;
         P1INP |= P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI;
         P1DIR |= P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI;
      }
   }
   else {
   // Reconfigure the EPD SPI pins as GPIO
   // Disconnect the P1 EPD SPI pins from USART0 and connect them to GPIO
      P1SEL &= ~(P1_EPD_SCK | P1_EPD_DI);

      if(mSelfMac[3] & 0xf0) {
      // JA1xxxxxxx devices implement EPD power control.
      // Drive all of the control pins LOW to avoid back powering the EPD via the 
      // control pins
         P0 &= ~(P0_EPD_BS1 | P0_EPD_D_nCMD);
         P1 &= ~(P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI);
   // Turn off power to the EPD
         SET_EPD_nENABLE(1);
      }
      else {
      // JA0xxxxxxx devices do not implement EPD power control.
      // Configure all control pins as inputs and enable pull-up 
      // resistors to keep them from floating
         P0INP &= ~(P0_EPD_BS1 | P0_EPD_nCS1 | P0_EPD_nENABLE | P0_EPD_D_nCMD );
         P0DIR &= ~(P0_EPD_BS1 | P0_EPD_nCS1 | P0_EPD_nENABLE | P0_EPD_D_nCMD );
         P1INP &= ~(P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI);
         P1DIR &= ~(P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI);
      }
   }
}

void screenShutdown(void)
{
   if (!gScreenPowered) {
      return;
   }
   SendEpdTbl(gPwrOffEpd);
   SCR_INIT_LOG("busy wait 2..");
   while(EPD_BUSY());
   SCR_INIT_LOG("done\n");
   SendEpdTbl(gPwrOffEpd1);
   screenInitGPIO(false);
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
   einkDeselect();
}

#pragma callee_saves screenByteTx
void screenByteTx(uint8_t byte)
{
   screenPrvWaitByteSent();
   LOG_EPD_DATA(byte);
   U0DBUF = byte;
}

void drawWithSleep() 
{
#ifndef DISABLE_DISPLAY
   uint16_t Lowest = 0xffff;

   LOGA("Updating display");

   SCR_INIT_LOG("\nBusy before refresh command %d\n",EPD_BUSY());

   SendEpdTbl(gStartRefresh);
   SCR_INIT_LOG("after %d\n",EPD_BUSY());
   while(EPD_BUSY());
   SCR_INIT_LOG("done\n");

   while(!EPD_BUSY()) {
   // Wake up every 50 milliseconds to check BattV and EPD_BUSY
      LOGA(".");
      sleepForMsec(50UL);
      clockingAndIntsInit();  // restart clocks
      ADCRead(ADC_CHAN_VDD_3);
      if(Lowest > gRawA2DValue) {
         Lowest = gRawA2DValue;
      }
   }
   LOGA("Done\n");
   gRefreshBattV = ADCScaleVDD(Lowest);
   powerUp(INIT_BASE);
   screenShutdown();
   UpdateVBatt();
   LOGA("Update complete\n");
#endif
}

#ifdef DEBUG_SCREEN_INIT

void LogEpdData(uint8_t DataByte)
{
   if(gDataByteLogged == 16) {
      SCR_INIT_LOG("\n");
      gDataByteLogged = 0;
   }
   SCR_INIT_LOG("%02x ",DataByte);
   gDataByteLogged++;
}
#endif
