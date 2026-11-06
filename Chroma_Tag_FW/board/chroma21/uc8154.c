// Chroma21 (JG, BWY) UC8154 panel driver
//
// The panel is driven like the stock firmware does it, see
// docs/chroma21_waveform.md:
//
// - LUTs, PLL, voltages and VCOM come from the factory waveform tables in
//   SPI flash at 0x2000 (format 04 02), selected by temperature.  A copy of
//   the 20C block from JG10014896 is used if the tables aren't there.
// - Two refreshes: the first in K/W mode (PSR KWR = 1) with the B/W LUTs,
//   the yellow pixels are driven by LUTG2.  The second in K/W/R mode with
//   null B/W LUTs and LUTC2/LUTR0/LUTR1 for the yellow pixels.  It's
//   skipped when there are no yellow pixels.
// - The red/yellow source voltage is applied a few ms after the second
//   refresh starts.
//
// Also built into spidump (without the default waveform) and into the host
// test in test/uc8154, so it only uses the pins and uc8154DelayMs(),
// uc8154WfRead().
#include "board.h"
#include "screen.h"
#include "uc8154.h"

// UC8154 commands
#define CMD_PANEL_SETTING                    0x00
#define CMD_POWER_SETTING                    0x01
#define CMD_POWER_OFF                        0x02
#define CMD_POWER_ON                         0x04
#define CMD_BOOSTER_SOFT_START               0x06
#define CMD_DISPLAY_START_TRANSMISSION_DTM1  0x10
#define CMD_DISPLAY_REFRESH                  0x12
#define CMD_DISPLAY_START_TRANSMISSION_DTM2  0x13
#define CMD_LUTC1                            0x20
#define CMD_LUTW                             0x21
#define CMD_LUTB                             0x22
#define CMD_LUTG2                            0x24
#define CMD_LUTC2                            0x25
#define CMD_LUTR0                            0x26
#define CMD_LUTR1                            0x27
#define CMD_PLL_CONTROL                      0x30
#define CMD_VCOM_INTERVAL                    0x50
#define CMD_TCON_SETTING                     0x60
#define CMD_RESOLUTION_SETTING               0x61
#define CMD_VCOM_DC_SETTING                  0x82

// Panel setting: RES 128x296 (overridden by TRES), scan up, shift right,
// DC-DC on, no reset.  The stock firmware uses 0x83 (scan down, shift left),
// that's upside down with OEPL's layout (seen on JG10014233).  The same scan
// direction as the Chroma29.
#define PSR_KWR      0x8f     // K/W/R: run LUT1 and LUT2
#define PSR_KW       (PSR_KWR | 0x10) // K/W: run LUT1 only

#define CDI_BORDER_FLOATING   0x37

// Factory waveform tables in SPI flash, offsets from
// EEPROM_FACTORY_WAVEFORM_ADR
#define WF_NTEMP        0x2c  // number of temperature entries
#define WF_MAX_NTEMP    31
#define WF_PARAMS       0x35  // 18 bytes of parameters, see WF_* below
#define WF_TEMPS        0x47  // temperature table
#define WF_ENTRY_LEN    10    // temperature table entry length
#define WF_MAP_LEN      12    // index map row length
#define WF_POOL_SPAN    (WF_ENTRY_LEN + WF_MAP_LEN)   // per entry
#define LUT_LEN         15

// gWf[]: parameters followed by the selected temperature entry
#define WF_VCM_DC       0     // VCM_DC (R82h)
#define WF_TCON         1     // TCON (R60h)
#define WF_CDI          2     // CDI (R50h)
#define WF_BTST         3     // BTST (R06h), 3 bytes
#define WF_PWR          6     // PWR (R01h) bytes 1 and 2
#define WF_OFF_DLY      10    // u16 ms to wait before power off
#define WF_T            18    // temperature entry
#define WF_T_PLL        (WF_T + 1) // PLL for the B/W refresh
#define WF_T_VDPS       (WF_T + 4) // bit 7: VSH = 15V, else VDPS_LV
#define WF_T_VDNS       (WF_T + 5) // bit 7: VSL = -15V, else VDNS_LV
#define WF_T_RDLY       (WF_T + 6) // ms before the red voltage is applied
#define WF_T_PLL2       (WF_T + 7) // PLL for the yellow refresh
#define WF_SIZE         (WF_T + WF_ENTRY_LEN)

#define MAX_OFF_DELAY   4000  // ms, in case the tables are bad

// LUT index map slots, see PIXEL_* in uc8154.h for what they do on this panel
#define SLOT_C1         0
#define SLOT_C2         1
#define SLOT_R22        2
#define SLOT_G2         4
#define SLOT_R21        5
#define SLOT_R0         6
#define SLOT_R1         7
#define SLOT_NULL       0xff

#define BUSY_TIMEOUT    60000 // ms

// All these bytes are used before the first sleep or right after a sleep:
// gTempBuf320 doesn't need to survive power modes PM2, PM3
#define gBuf   gTempBuf320

static __bit gWfInFlash;
static __xdata uint8_t gWf[WF_SIZE];
static __xdata uint8_t gWfMap[8];
static __xdata uint16_t gWfPool;

static const uint8_t __code gWfMagic[] = {0xce,0xfa,0xef,0xbe,0x04,0x02};

#ifndef UC8154_NO_DEFAULT_WAVEFORM
// Used when the SPI flash doesn't have the factory waveform tables.
// From JG10014896 ("D18Y00010WY005_UC8154 21E4"), the >= 20C block.
static const uint8_t __code gWfDefault[WF_SIZE] = {
// params 0x2035-0x2046
   0x0a,0x22,0x17,0x07,0x07,0x0f,0x03,0x00,0x32,0x02,0xd0,0x07,
   0x00,0x00,0x00,0x00,0x00,0x00,
// temperature entry 1 (>= 20 C)
   0x14,0x39,0x00,0x01,0x03,0x80,0x10,0x29,0x00,0x00,
};

// slots 0, 1, 2, 4, 5, 6, 7 (slot 3, LUTG1, isn't used)
static const uint8_t __code gLutDefault[7 * LUT_LEN] = {
   0x0b,0x0a,0x01,0x02,0x03,0x1e,0x08,0x08,0x3c,0x3f,0x3f,0x06,0x05,0x02,0x1e,
   0x28,0x28,0x03,0x05,0x3f,0x04,0x03,0x3f,0x08,0x19,0x19,0x01,0x00,0x00,0x00,
   0x0b,0x0a,0x01,0x42,0x03,0x1e,0x48,0x88,0x3c,0x7f,0xbf,0x06,0x05,0x82,0x1e,
   0x8b,0x8a,0x01,0x82,0x83,0x1e,0x48,0x88,0x3c,0x7f,0xbf,0x06,0x05,0x82,0x1e,
   0x0b,0x0a,0x01,0x82,0x83,0x1e,0x48,0x88,0x3c,0x7f,0xbf,0x06,0x45,0x02,0x1e,
   0x68,0x68,0x03,0x85,0x7f,0x04,0x83,0x7f,0x08,0x59,0x59,0x01,0x00,0x00,0x00,
   0x28,0x28,0x03,0x05,0x3f,0x04,0x03,0x3f,0x08,0x19,0x19,0x01,0x00,0x00,0x00,
};

static void CodeCopy(uint8_t __xdata *Dst,const uint8_t __code *Src,uint8_t Len)
{
   while(Len--) {
      *Dst++ = *Src++;
   }
}
#endif

// NB: not mathPrvMul8x8(), asmUtil.c only has it when barcodes or SFDP are
// enabled
static uint16_t Mul8x8(uint8_t a,uint8_t b)
{
   return (uint16_t) a * b;
}

void uc8154TxByte(uint8_t Byte)
{
   screenPrvWaitByteSent();
   U0DBUF = Byte;
}

// Send a command with Len data bytes from Data
static void SendCmd(uint8_t Cmd,const uint8_t __xdata *Data,uint8_t Len)
{
   einkSelect();
   screenPrvWaitByteSent();
   P0 &= (uint8_t) ~P0_EPD_D_nCMD;
   __asm__("nop");
   uc8154TxByte(Cmd);
   __asm__("nop");
   screenPrvWaitByteSent();
   P0 |= P0_EPD_D_nCMD;
   while(Len--) {
      uc8154TxByte(*Data++);
   }
   einkDeselect();
}

static void SendCmd0(uint8_t Cmd)
{
   SendCmd(Cmd,(const uint8_t __xdata *) 0,0);
}

static void SendCmd1(uint8_t Cmd,uint8_t Data)
{
   gBuf[0] = Data;
   SendCmd(Cmd,gBuf,1);
}

// Read the waveform for this temperature from the factory tables
static bool LoadWaveform(int8_t Temperature)
{
   uint8_t i;
   uint8_t n;
   uint16_t Ofst;

   uc8154WfRead(0,gBuf,WF_TEMPS);
   n = gBuf[WF_NTEMP];
   gWfInFlash = n != 0 && n <= WF_MAX_NTEMP;
   for(i = 0; i < sizeof(gWfMagic); i++) {
      if(gBuf[i] != gWfMagic[i]) {
         gWfInFlash = false;
      }
   }

   if(gWfInFlash) {
      for(i = 0; i < WF_T; i++) {
         gWf[i] = gBuf[WF_PARAMS + i];
      }
   // The entries are warmest first, use the first one at or below the
   // temperature, or the last (coldest) one
      Ofst = WF_TEMPS;
      for(i = 0; ; i++) {
         uc8154WfRead(Ofst,&gWf[WF_T],WF_ENTRY_LEN);
         if(i == n - 1 || (int8_t) gWf[WF_T] <= Temperature) {
            break;
         }
         Ofst += WF_ENTRY_LEN;
      }
   // index map row i
      uc8154WfRead(WF_TEMPS + Mul8x8(n,WF_ENTRY_LEN) + Mul8x8(i,WF_MAP_LEN),
                   gWfMap,sizeof(gWfMap));
      gWfPool = WF_TEMPS + Mul8x8(n,WF_POOL_SPAN);
      return true;
   }
#ifdef UC8154_NO_DEFAULT_WAVEFORM
   return false;
#else
   CodeCopy(gWf,gWfDefault,WF_SIZE);
   return true;
#endif
}

// Send LUT register Cmd from index map slot Slot, SLOT_NULL: an empty LUT
static void SendLut(uint8_t Cmd,uint8_t Slot)
{
   uint8_t i;

   if(Slot == SLOT_NULL) {
      for(i = 0; i < LUT_LEN; i++) {
         gBuf[i] = 0;
      }
   }
   else if(gWfInFlash) {
      uc8154WfRead(gWfPool + Mul8x8(gWfMap[Slot],LUT_LEN),gBuf,LUT_LEN);
   }
#ifndef UC8154_NO_DEFAULT_WAVEFORM
   else {
      if(Slot > 3) {
         Slot--;
      }
      CodeCopy(gBuf,&gLutDefault[Mul8x8(Slot,LUT_LEN)],LUT_LEN);
   }
#endif
   SendCmd(Cmd,gBuf,LUT_LEN);
}

// Power setting (R01h).  bRed: the voltages from the temperature entry
// otherwise the stock firmware's "red off" setting: VDS_EN only
static void SendPowerSetting(bool bRed)
{
   uint8_t i;

   if(bRed) {
      gBuf[0] = gWf[WF_PWR];
      if(gWf[WF_T_VDPS] & 0x80) {
         gBuf[0] |= 0x08;    // RVSHLS[1]: VSH = 15V
      }
      if(gWf[WF_T_VDNS] & 0x80) {
         gBuf[0] |= 0x04;    // RVSHLS[0]: VSL = -15V
      }
      gBuf[1] = gWf[WF_PWR + 1];
      gBuf[2] = gWf[WF_T_VDPS] & 0x1f;
      gBuf[3] = gWf[WF_T_VDNS] & 0x1f;
   }
   else {
      gBuf[0] = 0x02;
      for(i = 1; i < 4; i++) {
         gBuf[i] = 0;
      }
   }
   SendCmd(CMD_POWER_SETTING,gBuf,4);
}

static void SendPanelSetting(uint8_t Psr)
{
   gBuf[0] = Psr;
   gBuf[1] = SCREEN_WIDTH;         // HRES
   gBuf[2] = SCREEN_HEIGHT >> 8;   // VRES
   gBuf[3] = SCREEN_HEIGHT & 0xff;
   SendCmd(CMD_PANEL_SETTING,gBuf,1);
   SendCmd(CMD_RESOLUTION_SETTING,&gBuf[1],3);
}

void uc8154WaitNotBusy()
{
   uint16_t Ms = BUSY_TIMEOUT;

   while(!EPD_BUSY() && --Ms) {
      uc8154DelayMs(1);
   }
}

bool uc8154PowerOn(int8_t Temperature)
{
   if(!LoadWaveform(Temperature)) {
      return false;
   }
// All control pins are outputs, BUSY an input, EPD power off, BS1 low
// (4 wire SPI)
   P0 = (P0 & (uint8_t) ~(P0_EPD_BS1 | P0_EPD_D_nCMD)) | P0_EPD_nENABLE;
   P0INP |= P0_EPD_BS1 | P0_EPD_nENABLE | P0_EPD_D_nCMD;
   P0DIR |= P0_EPD_BS1 | P0_EPD_nENABLE | P0_EPD_D_nCMD;
   P1INP |= P1_EPD_BUSY;
   P1DIR &= (uint8_t) ~P1_EPD_BUSY;
   P1DIR |= P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI;

// turn on the eInk power (keep in reset for now)
   P0 &= (uint8_t) ~P0_EPD_nENABLE;
   uc8154DelayMs(20);
   P1 |= P1_EPD_nCS0 | P1_EPD_nRESET;

// Connect the P1 EPD pins to USART0
   P1SEL |= P1_EPD_SCK | P1_EPD_DI;

   U0BAUD = 0;          // F/8 is max for spi - 3.25 MHz
   U0GCR = 0b00110001;  // BAUD_E = 0x11, msb first
   U0CSR = 0b00000000;  // SPI master mode, RX off

// reset
   P1 &= (uint8_t) ~P1_EPD_nRESET;
   uc8154DelayMs(1);
   P1 |= P1_EPD_nRESET;
   uc8154DelayMs(10);

   SendPowerSetting(true);
   SendCmd(CMD_BOOSTER_SOFT_START,&gWf[WF_BTST],3);
   SendCmd0(CMD_POWER_ON);
   uc8154WaitNotBusy();

   SendPanelSetting(PSR_KW);
   SendCmd1(CMD_PLL_CONTROL,gWf[WF_T_PLL]);
   SendCmd1(CMD_VCOM_INTERVAL,gWf[WF_CDI]);
   SendCmd1(CMD_VCOM_DC_SETTING,gWf[WF_VCM_DC]);
   SendCmd1(CMD_TCON_SETTING,gWf[WF_TCON]);

// LUT1 for the B/W refresh, LUT2 for the yellow refresh.  LUT2 isn't
// used in K/W mode so we can load everything now.
   SendLut(CMD_LUTC1,SLOT_C1);
   SendLut(CMD_LUTW,SLOT_R21);
   SendLut(CMD_LUTB,SLOT_R22);
   SendLut(CMD_LUTG2,SLOT_G2);
   SendLut(CMD_LUTC2,SLOT_C2);
   SendLut(CMD_LUTR0,SLOT_R0);
   SendLut(CMD_LUTR1,SLOT_R1);
   return true;
}

void uc8154TxStart(bool bYellow)
{
   SendCmd0(bYellow ? CMD_DISPLAY_START_TRANSMISSION_DTM2 :
                      CMD_DISPLAY_START_TRANSMISSION_DTM1);
}

// DTM1: 8 pixels, 2 bits per pixel
void uc8154TxPixels(uint8_t Black,uint8_t Yellow)
{
   uint8_t Mask;
   uint8_t Value = 0;

   for(Mask = 0x80; Mask != 0; Mask >>= 1) {
      Value <<= 2;
      if(Yellow & Mask) {
         Value |= PIXEL_YELLOW;
      }
      else if(Black & Mask) {
         Value |= PIXEL_BLACK;
      }
      else {
         Value |= PIXEL_WHITE;
      }
      if(Mask & 0b00010001) {
      // Value ready, send it
         uc8154TxByte(Value);
      }
   }
}

void uc8154Refresh(bool bYellow)
{
   SendCmd0(CMD_DISPLAY_REFRESH);
   uc8154DelayMs(1);    // give BUSY time to go active
   if(bYellow) {
   // Like the stock firmware apply the red voltages a little later
      uc8154DelayMs(gWf[WF_T_RDLY]);
      SendPowerSetting(true);
   }
}

void uc8154SetupYellowRefresh()
{
   SendLut(CMD_LUTC1,SLOT_NULL);
   SendLut(CMD_LUTW,SLOT_NULL);
   SendLut(CMD_LUTB,SLOT_NULL);
   SendLut(CMD_LUTG2,SLOT_NULL);
   SendPanelSetting(PSR_KWR);
   SendPowerSetting(false);
   SendCmd1(CMD_PLL_CONTROL,gWf[WF_T_PLL2]);
}

// Stock power off sequence: VCM_DC to 0, red voltages off, wait
uint16_t uc8154PowerOffStart()
{
   uint16_t Ms = gWf[WF_OFF_DLY] | (uint16_t) gWf[WF_OFF_DLY + 1] << 8;

   SendCmd1(CMD_VCOM_DC_SETTING,0);
   SendPowerSetting(false);
   return Ms > MAX_OFF_DELAY ? MAX_OFF_DELAY : Ms;
}

// then border floating, power off
void uc8154PowerOff()
{
   SendCmd1(CMD_VCOM_INTERVAL,CDI_BORDER_FLOATING);
   SendCmd0(CMD_POWER_OFF);
   uc8154DelayMs(20);
   uc8154PinsOff();
}

void uc8154PinsOff()
{
// Reconfigure the EPD SPI pins as GPIO
// Disconnect the P1 EPD SPI pins from USART0 and connect them to GPIO
   P1SEL &= (uint8_t) ~(P1_EPD_SCK | P1_EPD_DI);

// Drive all of the control pins LOW to avoid back powering the EPD via
// the control pins
   P0 &= (uint8_t) ~(P0_EPD_BS1 | P0_EPD_D_nCMD);
   P1 &= (uint8_t) ~(P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI);
// Turn off power to the EPD
   SET_EPD_nENABLE(1);
}
