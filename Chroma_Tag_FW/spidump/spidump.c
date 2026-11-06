// spidump: copy part of a Chroma tag's SPI flash into the CC1110's own flash
// so it can be read back with a CC debugger.  See docs/spidump.md.
//
// One pass copies 28K of SPI flash, from the address in the last 3 bytes of
// gDumpTag, to CC1110 flash 0x1000 - 0x7fff.  chroma_spidump.py patches that
// address into the image before flashing it.
//
// The SPI flash is only read.  The firmware in the CC1110 is lost.
//
// Optional: build.sh --screen <board> adds a screen shown after the copy
// (DUMP_SCREEN, see dumpscreen.h).
#include <stdint.h>
#include "cc111x.h"
#include "boardPins.h"
#include "clock.h"
#include "flashcopy.h"
#include "spidump.h"

#ifdef DUMP_SCREEN
#include "dumpscreen.h"
#endif

__code __at (DUMP_TAG_ADR) const uint8_t gDumpTag[DUMP_TAG_LEN] = {
   'S','P','I','D','U','M','P',0,0,0
};

// Where the copy loop runs from (SRAM is executable at its XDATA address)
#define RAM_CODE_ADR    0xf800
__xdata __at (RAM_CODE_ADR) uint8_t gRamCode[128];

uint8_t SpiByte(uint8_t v)
{
   U1DBUF = v;
   while(!(U1CSR & U1CSR_TX_BYTE));
   U1CSR &= (uint8_t) ~U1CSR_TX_BYTE;
   while(U1CSR & U1CSR_ACTIVE);
   return U1DBUF;
}

// Start reading the SPI flash
static void SpiReadStart(uint8_t AdrHi,uint8_t AdrMid,uint8_t AdrLo)
{
   P2_0 = 0;
   SpiByte(0x03);
   SpiByte(AdrHi);
   SpiByte(AdrMid);
   SpiByte(AdrLo);
}

#ifdef DUMP_SCREEN
void SpiRead(uint32_t Adr,uint8_t __xdata *Dst,uint8_t Len)
{
   SpiReadStart((uint8_t) (Adr >> 16),(uint8_t) (Adr >> 8),(uint8_t) Adr);
   while(Len--) {
      *Dst++ = SpiByte(0);
   }
   P2_0 = 1;
}
#endif

// The copy loop jumps here when it's done.  Stay here, a reset would erase
// and write the flash again and again.
void DumpDone(void)
{
   MEMCTR = 0;       // cache and prefetch back on, we run from flash again
#ifdef DUMP_SCREEN
   dumpScreenShow();
#endif
   while(1);
}

// The copy loop, run from RAM because it erases and writes flash.  Returns
// its address in DPTR and its length in B (bits 16-23 of the return value).
static uint32_t CopyLoopGet(void) __naked
{
   __asm__(
      "  mov   DPTR, #00098$     \n"
      "  mov   A, #00099$        \n"
      "  clr   C                 \n"
      "  subb  A, DPL            \n"
      "  mov   B, A              \n"
      "  ret                     \n"

      "00098$:                   \n"
   // erase pages 4..31 (0x1000 - 0x7fff)
      "  mov   B, #28            \n"
      "  mov   _FADDRH, #0x08    \n"
      "  mov   _FADDRL, #0x00    \n"
      FLASH_ERASE_PAGES_ASM

   // write them from word address 0x0800, end the SPI read, back to flash
      "  mov   DPTR, #0x0800     \n"
      FLASH_WRITE_FROM_SPI_ASM
      "  setb  _P2_0             \n"
      "  ljmp  _DumpDone         \n"
      "00099$:                   \n"
   );
}

void main(void)
{
   uint32_t Info;
   const uint8_t __code *Src;
   uint8_t Len;
   uint8_t i;

   clockingAndIntsInit();
   DMAARM = 0xff;    // all DMA channels off

// EPD off, its control pins low, SPI flash deselected
   P0 = P0_EEPROM_CLK | P0_EEPROM_MOSI | P0_EPD_nENABLE;
   P0DIR = P0_EPD_BS1 | P0_EEPROM_CLK | P0_EEPROM_MOSI | P0_EPD_nENABLE
         | P0_EPD_D_nCMD;
   P1 = 0;
   P1DIR = P1_EPD_nCS0 | P1_EPD_nRESET | P1_EPD_SCK | P1_EPD_DI;
   P2 = P2_EEPROM_nCS;
   P2DIR = P2_EEPROM_nCS;

// USART1 alt 1 (P0) in SPI master mode, as u1setEepromMode().  USART0 must
// move to P1 (alt 2): at its reset location it shares P0.2-P0.5 with USART1
// alt 1 and has priority, all reads would return 0x00.
   PERCFG = PERCFG_U0CFG;
   U1BAUD = 0;
   U1GCR = U1GCR_ORDER | U1GCR_BAUD_E(0x11);
   U1CSR = U1CSR_RE;
   P0SEL = P0_EEPROM_CLK | P0_EEPROM_MOSI | P0_EEPROM_MISO;

// Release from deep power down (tRES1 < 10 us on the parts seen so far)
   P2_0 = 0;
   SpiByte(0xab);
   P2_0 = 1;
   for(i = 0; i != 255; i++) {
      __asm__("nop");
   }

// The copy loop clocks the data out
   SpiReadStart(gDumpTag[DUMP_TAG_BASE],gDumpTag[DUMP_TAG_BASE + 1],
                gDumpTag[DUMP_TAG_BASE + 2]);

   Info = CopyLoopGet();
   Src = (const uint8_t __code *) (uint16_t) Info;
   Len = (uint8_t) (Info >> 16);
   if(Len > sizeof(gRamCode)) {
      while(1);
   }
   for(i = 0; i < Len; i++) {
      gRamCode[i] = Src[i];
   }
   MEMCTR = 3;       // cache and prefetch off
   __asm__("  ljmp  0xf800");
}
