#include <stddef.h>
#include "asmUtil.h"
#include "screen.h"
#include "printf.h"
#include "cpu.h"
#include "timer.h"
#include "sleep.h"
#include "board.h"
#include "powermgt.h"
#include "adc.h"
#include "eeprom.h"
#include "settings.h"
#include "logging.h"

// uc8159 like commands
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
#define CMD_IPC 0x13
#define CMD_PLL_CONTROL 0x30
#define CMD_TEMPERATURE_CALIB 0x40
#define CMD_TEMPERATURE_SELECT 0x41
#define CMD_TEMPERATURE_WRITE 0x42
#define CMD_TEMPERATURE_READ 0x43
#define CMD_VCOM_INTERVAL 0x50
#define CMD_LOWER_POWER_DETECT 0x51
#define CMD_TCON_SETTING 0x60
#define CMD_RESOLUTION_SETTING 0x61
#define CMD_SPI_FLASH_CONTROL 0x65
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
// Factory waveform script in SPI flash, format 3.3 (see docs/chroma74_waveform.md)
const uint8_t __code gWfMagic[] = {0xce,0xfa,0xef,0xbe,0x03,0x03};
#define WF_ADR            0x2000UL
#define WF_SCRIPT_BASE    0x2a     // the script offsets are relative to this
#define WF_REFRESH_OFS    0x32     // u32 offset of the refresh script
#define WF_MAX_RECORDS    32

// The stock firmware refreshes with the VCOM in the factory refresh script
// (01 02 00 82 <vcom>), not the measured value in NVRAM record 0x23.  On some
// tags record 0x23 is far off (JM10261788B: 0x0e, -0.70 V, where the script
// has 0x2f, -2.35 V); black and white still switch, but yellow does not
// develop (seen on JM10261788B; yellow came back with the script's value).
// Use the script's value when the script is there, otherwise keep the NVRAM
// value.  Called from LoadGlobalsFromEEPROM with the SPI flash on.
void screenVcomFromWaveform(void)
{
   uint16_t Ofst;
   uint8_t i;

   eepromRead(WF_ADR,gTempBuf320,WF_REFRESH_OFS + 2);
   for(i = 0; i < sizeof(gWfMagic); i++) {
      if(gTempBuf320[i] != gWfMagic[i]) {
         return;
      }
   }
   Ofst = WF_SCRIPT_BASE + (gTempBuf320[WF_REFRESH_OFS] |
                            (uint16_t) gTempBuf320[WF_REFRESH_OFS + 1] << 8);
   for(i = 0; i < WF_MAX_RECORDS; i++) {
   // 01 <len16> <cmd> <data>, 04 <len16> <cmd>, 00 = end, others 3 bytes
      eepromRead(WF_ADR + Ofst,gTempBuf320,5);
      if(gTempBuf320[0] == 0) {
         return;
      }
      if(gTempBuf320[0] == 1) {
         if(gTempBuf320[3] == CMD_VCOM_DC_SETTING) {
            mScreenVcom = gTempBuf320[4];
            return;
         }
         Ofst += 3 + (gTempBuf320[1] | (uint16_t) gTempBuf320[2] << 8);
      }
      else {
         Ofst += gTempBuf320[0] == 4 ? 4 : 3;
      }
   }
}

/*
   //eInk pinout:
   // pin  1 = MFCSB    = ? optional
   // pin  8 = BS1      = P0.0
   // pin  9 = BUSY     = P1.0
   // pin 10 = nRESET      = P1.2
   // pin 11 = D/nC     = P0.7
   // pin 12 = nCS      = P1.1
   // pin 13 = D0(SCK)  = P1.3
   // pin 14 = D1(SDIN) = P1.5 (also can be data out)
   // pin 19 = FMSDO    = ?   optional
   //extra pins
   // nEnable           = P0.6
   
   
   //controller is 99.9% likely to be UC8159 version c_B
   
   //LUT format info from: https://www.buydisplay.com/download/ic/UC8159C.pdf


      VCOM LUT is: 20x VcomLutInfo = 20 * 11 = 220 bytes
      
         VcomLutInfo {
            u8 numRepeats;    //how many times to perform this LUT step
            u2 level[8];      //MSB to LSB. 00 = VCM_DC, 01 = 15V + VCM_DC, 10 = -15V + VCM_DC, 11 = float
            u8 numFrames[8];  //for each level
         }


      COLOR LUTs ARE: 20x ColorLutInfo =  20 * 13 =  260 bytes
      
         ColorLutInfo {
            u8 numRepeats;    //how many times to perform this LUT step
            u4 level[8];      //MSB to LSB. top bit ignored. 000 = VCM_DC, 001 = 15V (VSH), 010 = -15V (VSL), 011 = VSH_LV, 100 = VSL_LV, 101 = VSH_LVX, 110 = VSL_LVX, 111 = float
            u8 numFrames[8];  //for each level
         }
      
      
      XON LUT is: 20x XonLutInfo = 20 * 10 = 200 bytes
      
         XonLutInfo {
            u8 numRepeats;    //how many times to perform this LUT step
            u1 level[8];      //MSB to LSB. 0 = all gates ON, 1 = normal gate scan
            u8 numFrames[8];  //for each level
         }
*/
__xdata __at (0xfda2) uint8_t gTempBuf320[320];  //350 bytes here, we use 320 of them

__bit gScreenPowered = false;

#define LUT_CMD_000  0x21
#define LUT_CMD_001  0x23
#define LUT_CMD_010  0x24
#define LUT_CMD_011  0x22
#define LUT_CMD_100  0x25
#define LUT_CMD_101  0x26
#define LUT_CMD_110  0x27
#define LUT_CMD_111  0x28

// The power setting, PLL and LUTs come from the factory waveform script in
// SPI flash at WF_ADR, as on the Chroma 60: LoadWaveformBlock() picks the
// block for the current temperature and SendFlashCmd() streams each record
// out of it, so every panel gets its own tuned waveform.  The power setting
// and PLL below are the 20 C values, used only when the script is missing.
static const uint8_t __code gBoosterEpd[] = {
   4,CMD_BOOSTER_SOFT_START,
   0x0e,0xcd,0x26,     // not per-temperature, not in the script blocks

   0
};

#ifndef HW_VARIANT
// HW_VARIANT 0: BWY
#define FALLBACK_PWR_VDHR  0x02
#define FALLBACK_PLL       0x22
#elif HW_VARIANT == 1
// HW_VARIANT 1: BWR
#define FALLBACK_PWR_VDHR  0x04
#define FALLBACK_PLL       0x3a
#else
#error "Unknown HW_VARIANT"
#endif

static const uint8_t __code gPwrSettingEpd[] = {
   5,CMD_POWER_SETTING,
   0x07,0x01,FALLBACK_PWR_VDHR,0x3c,

   0
};

static const uint8_t __code gPowerOnEpd[] = {
   1,CMD_POWER_ON,

   0
};

static const uint8_t __code gSetupEpd[] = {
   2,CMD_SPI_FLASH_CONTROL,
   0, // disabled

   3,CMD_PANEL_SETTING,
   0xef,0x80,

   5,CMD_RESOLUTION_SETTING,
   SCREEN_WIDTH >> 8,SCREEN_WIDTH & 0xff,SCREEN_HEIGHT >> 8,
   SCREEN_HEIGHT & 0xff,

   0
};

static const uint8_t __code gPllEpd[] = {
   2,CMD_PLL_CONTROL,
   FALLBACK_PLL,

   0
};

static const uint8_t __code gPwrOffEpd[] = {
   1,CMD_POWER_OFF,

   2,CMD_DEEP_SLEEP,
   0xa5,

   0
};


#define screenPrvSendCommand(cmdByte) do { \
   uint8_t epdCommandByte_ = (cmdByte); \
   screenPrvWaitByteSent(); \
   P0 &= (uint8_t) ~P0_EPD_D_nCMD; \
   __asm__("nop"); \
   screenByteTx(epdCommandByte_); \
   __asm__("nop"); \
   screenPrvWaitByteSent(); \
   P0 |= P0_EPD_D_nCMD; \
} while (0)


void P1INT_ISR(void) __interrupt (15)
{
   SLEEP &= (uint8_t)~(3 << 0);  //wake up
}

// <CommandBytes> <Command> [<Data>] ...]
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

// ---- Per-temperature waveform from the factory script in SPI flash --------
//
// The script at WF_ADR is a list of records:
//   01 <len16 LE> <cmd> <data...>   send <cmd> with the data (len = 1 + data)
//   04 <len16 LE> <cmd>             send <cmd> with (len - 1) zero bytes
//   <xx> <yy> <zz>                  a 3-byte wait (anything not 01 / 04)
// The header (gWfMagic) is followed at WF_SCRIPT_BASE by four u32 offsets,
// relative to WF_SCRIPT_BASE: init, temperature table, refresh, VCOM measure.
// The temperature table has 9-byte entries, warmest first, each a signed
// temperature and the offset of that temperature's block, ended by a
// <= WF_TEMP_END sentinel.  Each block holds the power setting (01h), PLL
// (30h), and the VCOM (20h), colour (22h/21h/25h) and XON (29h) LUTs.
//
// The table is not at the same place on every Chroma 74 (0x20e2 on the JM
// dumps, 0x20e7 on JL10420082B), so it is found through its header offset.
//
// The factory puts the colour LUTs on registers 21/22/25; OEPL's drawing.c
// encodes pixels as 000/110/111, so the factory 22h/25h LUT *contents* are
// sent to registers 27h/28h.
#define WF_TABLE_OFS     0x2e    // u32 offset of the temperature table
#define WF_TEMP_ENTRY    9       // bytes per temperature table entry
#define WF_TEMP_END      (-100)  // sentinel temperature ending the table
#define WF_MAX_TEMPS     20      // sanity cap on the table scan

static __bit gWfInFlash;
static __xdata uint16_t gWfBlock;   // offset of the chosen temperature block
static __xdata uint8_t gWfRec[4];   // a record header being scanned

// Pick the block for Temperature: the warmest entry at or below it, or the
// coldest entry if Temperature is below the table.  Sets gWfInFlash.
static void LoadWaveformBlock(int8_t Temperature)
{
   uint16_t Ofst;
   uint8_t i;

   gWfInFlash = false;
   gWfBlock = 0;
   eepromRead(WF_ADR,gTempBuf320,WF_TABLE_OFS + 2);
   for(i = 0; i < sizeof(gWfMagic); i++) {
      if(gTempBuf320[i] != gWfMagic[i]) {
         return;
      }
   }
   Ofst = WF_SCRIPT_BASE + (gTempBuf320[WF_TABLE_OFS] |
                            (uint16_t) gTempBuf320[WF_TABLE_OFS + 1] << 8);
   for(i = 0; i < WF_MAX_TEMPS; i++) {
      eepromRead(WF_ADR + Ofst,gWfRec,3);
      if((int8_t) gWfRec[0] <= WF_TEMP_END) {
         break;      // sentinel: keep the coldest real block in gWfBlock
      }
      gWfBlock = WF_SCRIPT_BASE + (gWfRec[1] | (uint16_t) gWfRec[2] << 8);
      if((int8_t) gWfRec[0] <= Temperature) {
         break;
      }
      Ofst += WF_TEMP_ENTRY;
   }
   gWfInFlash = gWfBlock != 0;
}

// Within the chosen block, find factory command Cmd and return the SPI offset
// of its data (from WF_ADR), with *Len set to the data length; returns 0 if
// the block's command records end before Cmd is found.
static uint16_t WfFind(uint8_t Cmd,uint16_t *Len)
{
   uint16_t Ofst = gWfBlock;

   for(;;) {
      eepromRead(WF_ADR + Ofst,gWfRec,sizeof(gWfRec));
      if(gWfRec[0] != 0x01) {
         return 0;      // a wait or end of the block's command records
      }
      *Len = (gWfRec[1] | (uint16_t) gWfRec[2] << 8) - 1;
      if(gWfRec[3] == Cmd) {
         return Ofst + 4;
      }
      Ofst += 4 + *Len;
   }
}

// Send register Reg: Stored bytes from gTempBuf320, zero-filled to Len bytes
static void SendReg(uint8_t Reg,uint16_t Stored,uint16_t Len)
{
   uint16_t j;

   einkSelect();
   screenPrvSendCommand(Reg);
   for(j = 0; j < Len; j++) {
      screenPrvWaitByteSent();
      U0DBUF = (j < Stored) ? gTempBuf320[j] : 0;
   }
   einkDeselect();
}

// Send controller register Reg from the block's factory command FactoryCmd,
// zero-filled (or all zero if absent) to Len bytes.  Only call when gWfInFlash.
static void SendFlashCmd(uint8_t FactoryCmd,uint8_t Reg,uint16_t Len)
{
   uint16_t Stored = 0;
   uint16_t Ofst = WfFind(FactoryCmd,&Stored);

   if(!Ofst) {
      Stored = 0;
   }
   else {
      if(Stored > Len) {
         Stored = Len;
      }
      eepromRead(WF_ADR + Ofst,gTempBuf320,Stored);
   }
   SendReg(Reg,Stored,Len);
}

static void screenInitIfNeeded()
{
   __bit EepromWasOn = gEEPROM_PoweredUp;
   uint8_t Reg;

   if(gScreenPowered) {
      return;
   }
   gScreenPowered = true;

// DrawScreen() gets here with the SPI flash asleep, drawImageAtAddress()
// with it awake and still needing it.
   powerUp(INIT_EEPROM);

// gTemperature may be from several check-ins ago
   ADCRead(ADC_CHAN_TEMP);
   ADCScaleTemperature();

// Read this temperature's waveform block from the factory script
   LoadWaveformBlock(gTemperature);

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
   while(!EPD_BUSY());
   
// we can now talk to it.  Power setting (voltages) must be set before power
// on, so take it from the block when present, else the hardcoded 20C value.
   SendEpdTbl(gBoosterEpd);
   if(gWfInFlash) {
      SendFlashCmd(CMD_POWER_SETTING,CMD_POWER_SETTING,4);
   }
   else {
      SendEpdTbl(gPwrSettingEpd);
   }
   SendEpdTbl(gPowerOnEpd);

// wait for not busy
   while(!EPD_BUSY());

   SendEpdTbl(gSetupEpd);
   if(gWfInFlash) {
      SendFlashCmd(CMD_PLL_CONTROL,CMD_PLL_CONTROL,1);
   }
   else {
      SendEpdTbl(gPllEpd);
   }

   einkSelect();
   screenPrvSendCommand(CMD_VCOM_DC_SETTING);
   screenByteTx(mScreenVcom);
   einkDeselect();

   einkSelect();
   screenPrvSendCommand(CMD_VCOM_INTERVAL);
// VBD = 110: the border uses R27h, which holds the white LUT here.  The
// factory value 0x77 would use R22h, which OEPL's pixel codes leave empty.
   screenByteTx(0xd7);
   einkDeselect();

// The LUTs OEPL's pixel codes don't use are all zero
   for(Reg = LUT_CMD_011; Reg <= LUT_CMD_101; Reg++) {
      SendReg(Reg,0,260);     // 22h to 26h
   }

   if(gWfInFlash) {
   // VCOM, XON and colour LUTs from the block.  The factory 22h/25h LUT
   // contents go to registers 27h/28h to match OEPL's pixel encoding.
      SendFlashCmd(0x20,0x20,220);          // VCOM
      SendFlashCmd(0x29,0x29,200);          // XON
      SendFlashCmd(0x21,LUT_CMD_000,260);   // black
      SendFlashCmd(0x22,LUT_CMD_110,260);   // white
      SendFlashCmd(0x25,LUT_CMD_111,260);   // red/yellow
   }
// else: no factory tables.  The LUTs stay empty and drawWithSleep() won't
// refresh.  Power-on still used the hardcoded power setting and PLL above.

   if(!EepromWasOn) {
      powerDown(INIT_EEPROM);
   }
   LOG_CONFIG("screenInitIfNeeded");
}

void screenShutdown(void)
{
   if (!gScreenPowered) {
      return;
   }
   
   SendEpdTbl(gPwrOffEpd);

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
   
   einkSelect();
   screenPrvSendCommand(CMD_DISPLAY_START_TRANSMISSION_DTM1);
}

#pragma callee_saves screenByteTx
void screenByteTx(uint8_t byte)
{
   screenPrvWaitByteSent();
   U0DBUF = byte;
}

void drawWithSleep() 
{
#ifdef DISABLE_DISPLAY
   LOGA("NOT updating display\n");
#else
   uint16_t Lowest = 0xffff;

   LOGA("Updating display\n");
   einkDeselect();
   einkSelect();
   screenPrvSendCommand(CMD_DISPLAY_REFRESH);
   einkDeselect();
   
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

