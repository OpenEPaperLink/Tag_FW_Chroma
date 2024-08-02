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


// uc8154 like commands
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
#define CMD_LUTC1       0x20
#define CMD_LUTW        0x21
#define CMD_LUTB        0x22
#define CMD_LUTG2       0x24
#define CMD_LUTC2       0x25
#define CMD_LUTR0       0x26
#define CMD_LUTR1       0x27

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


__xdata __at (0xfda2) uint8_t gTempBuf320[320];  //350 bytes here, we use 320 of them

__bit gScreenPowered = false;
__bit gRedPass;

void SendEpdTbl(uint8_t const __code *pData);

static const uint8_t __code gPwrUpEpd[] = {
   5,
   CMD_POWER_SETTING,   // Power Setting (PWR)
   0x07,0x00,0x09,0x00,

   4, 
   CMD_BOOSTER_SOFT_START, // Booster Soft Start (BTST)
   0x07,0x07,0x0f,
   0     // end of table
};

static const uint8_t __code gSetupEpd[] = {
   2,
   CMD_PANEL_SETTING,   // Panel Setting (PSR)
   0x8f, // 10 x 0 1 1 1 1
         // ^^   ^ ^ ^ ^  ^- RST_N controller not 
         //  |   | | | +---- SHD_N DC-DC converter on
         //  |   | | +------ SHL Shift right
         //  |   | +-------- UD scan up
         //  |   +---------- KWR Pixel with K/W/Red run LU1 and LU2
         //  +-------------- RES 128x296
   4,
   CMD_RESOLUTION_SETTING, // RESOLUTION SETTING (TRES)
   0x80,       // 0x80 = 128
   0x01,0x28,  // 0x128 = 296

   2,
   CMD_PLL_CONTROL,  // PLL control (PLL)
   0x29,

   2,
   CMD_VCOM_INTERVAL,   // Vcom and data interval setting (CDI)
   0x17,

   2,
   CMD_VCOM_DC_SETTING, // VCM_DC Setting (VDCS)  NB: this is display dependent !
   0x08, //== -.8v

   2,
   CMD_TCON_SETTING, // TCON setting (TCON)
   0x22,

   16,
   CMD_LUTC1,  // VCOM1 LUT (LUTC1)
   0x01,0x01,0x01,0x03,0x04,0x09,0x06,0x06,
   0x0A,0x04,0x04,0x19,0x03,0x04,0x09,

   16,
   CMD_LUTW,   // WHITE LUT (LUTW) (R21H)
   0x01,0x01,0x01,0x03,0x84,0x09,0x86,0x46,
   0x0A,0x84,0x44,0x19,0x03,0x44,0x09,

   16,
   CMD_LUTB,   // BLACK LUT (LUTB) (R22H)
   0x01,0x01,0x01,0x43,0x04,0x09,0x86,0x46,
   0x0A,0x84,0x44,0x19,0x83,0x04,0x09,

   16,
   CMD_LUTC2, // VCOM2 LUT (LUTC2)
   0x0A,0x0A,0x01,0x02,0x14,0x0D,0x14,0x14,
   0x01,0x00,0x00,0x00,0x00,0x00,0x00,

   16,
   CMD_LUTR0,  // RED0 LUT (LUTR0)
   0x4A,0x4A,0x01,0x82,0x54,0x0D,0x54,0x54,
   0x01,0x00,0x00,0x00,0x00,0x00,0x00,

   16,
   CMD_LUTR1,  // RED0 LUT (LUTR1)
   0x0A,0x0A,0x01,0x02,0x14,0x0D,0x14,0x14,
   0x01,0x00,0x00,0x00,0x00,0x00,0x00,

   16,
   0x24, // GRAY2 LUT (LUTG2)
   0x01,0x81,0x01,0x83,0x84,0x09,0x86,0x46,
   0x0A,0x84,0x44,0x19,0x03,0x04,0x09,

   0     // end of table
};


static const uint8_t __code gPwrOffEpd[] = {
   2,
   0x82, // VCM_DC Setting (VDCS)
   0x00,

   0x05,
   0x01, // Power Setting (PWR)
   0x02,   // VDS_EN = 1, VDG_EN = RVSHLS = RVSHLS = 0
   0x00,   // VGHL_LV = 0 
   0x00,   // VDPS_LV = 0
   0x00,   // VDNS_LV

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
         U0DBUF = *pData++;
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
   
// Wait for Busy to go low
   while(EPD_BUSY());
   
// we can now talk to it
   SendEpdTbl(gPwrUpEpd);
   
// Send power on command 
   einkSelect();
   screenPrvSendCommand(CMD_POWER_ON);

// wait for not busy
   while(!EPD_BUSY());
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
   U0DBUF = byte;
}

void drawWithSleep() 
{
#ifndef DISABLE_DISPLAY
   uint16_t Lowest = 0xffff;

   LOGA("Updating display\n");

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
   powerUp(INIT_BASE);
   screenShutdown();
   UpdateVBatt();
   LOGA("Update complete\n");
#endif
}

