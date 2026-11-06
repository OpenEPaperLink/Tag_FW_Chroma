// Board independent content of the spidump screen, see dumpscreen.h
//
// Logical layout (OEPL coordinates, text 2x scaled 5x7 font):
//   corner marker  6x6 black at logical 0,0 (top left)
//   line 0 black   "SPI DUMP DONE"
//   line 1 black   tag SN from the *dumped copy* of the factory NVRAM,
//                  or "NO NVRAM" (expected for passes other than 0)
//   line 2 yellow  "BASE xxxxx"
//   bottom         black, yellow and outlined white patches
// If the text reads normally the panel orientation and mapping are right.
#include <stdint.h>
#include "dumpscreen.h"

#define TEXT_X      8
#define CHAR_W      12    // 5 columns x2 + 2 spacing
#define LINE_H      20
#define TEXT_Y      4
#define LINES       3
#define LINE_LEN    17
#define PATCH_Y     72
#define PATCH_H     26
#define PATCH_W     40

uint8_t __xdata gDsBlack;
uint8_t __xdata gDsYellow;

static char __xdata gText[LINES][LINE_LEN + 1];

// 5x7 font, 5 columns per character, bit 0 = top row: ' ', 0-9, A-Z
static const uint8_t __code gFont[37][5] = {
   {0x00,0x00,0x00,0x00,0x00},
   {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
   {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
   {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
   {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
   {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
   {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
   {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
   {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
   {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
   {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
   {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
   {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
   {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
   {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
   {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
   {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
   {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
   {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};

static void StrCopy(char __xdata *Dst,const char __code *Src)
{
   while((*Dst++ = *Src++) != 0);
}

static char __xdata *Hex(char __xdata *Dst,uint8_t v)
{
   static const char __code Digits[] = "0123456789ABCDEF";
   *Dst++ = Digits[v >> 4];
   *Dst++ = Digits[v & 0xf];
   *Dst = 0;
   return Dst;
}

// Find the SN (record type 1: 2 letters + 4 bytes) in the dumped copy of
// the factory NVRAM: SPI flash 0x0000 or 0x1000 at CC1110 flash 0x1000 +
static uint8_t FindSn(char __xdata *Dst)
{
   const uint8_t __code *p;
   const uint8_t __code *End;
   uint8_t Sector;
   uint8_t Len;

   if(gDumpTag[DUMP_TAG_BASE] | gDumpTag[DUMP_TAG_BASE + 1] |
      gDumpTag[DUMP_TAG_BASE + 2])
   {
      return 0;      // only the first pass has the NVRAM
   }
   for(Sector = 0x10; Sector <= 0x20; Sector += 0x10) {
      p = DS_CODE_PTR((uint16_t) Sector << 8);
      End = p + 0x1000;
      if(p[0] != 0x56 || p[1] != 0x12 || p[2] != 0x09 || p[3] != 0x85) {
         continue;
      }
      for(p += 4; p < End; p += Len) {
         Len = p[1];
         if(p[0] == 0xff || Len < 2) {
            break;
         }
         if(p[0] == 0x01 && Len == 8) {
            *Dst++ = p[2];
            *Dst++ = p[3];
            for(Len = 4; Len < 8; Len++) {
               Dst = Hex(Dst,p[Len]);
            }
            return 1;
         }
      }
   }
   return 0;
}

void dsInit()
{
   static const char __code Digits[] = "0123456789ABCDEF";
   char __xdata *p;

   StrCopy(gText[0],"SPI DUMP DONE");
   if(!FindSn(gText[1])) {
      StrCopy(gText[1],"NO NVRAM");
   }
   StrCopy(gText[2],"BASE ");
   p = &gText[2][5];
   *p++ = Digits[gDumpTag[DUMP_TAG_BASE] & 0xf];
   p = Hex(p,gDumpTag[DUMP_TAG_BASE + 1]);
   Hex(p,gDumpTag[DUMP_TAG_BASE + 2]);
}

// 0 = white, 1 = black, 2 = yellow
static uint8_t Color(uint16_t lx,uint16_t ly)
{
   uint8_t Line;
   uint8_t c;

   if(lx < 6 && ly < 6) {
      return 1;
   }
   if(ly >= PATCH_Y && ly < PATCH_Y + PATCH_H && lx >= TEXT_X) {
      lx -= TEXT_X;
      if(lx < PATCH_W) {
         return 1;
      }
      lx -= PATCH_W + 8;
      if(lx < PATCH_W) {
         return 2;
      }
      lx -= PATCH_W + 8;
      if(lx < PATCH_W) {
      // white with a 2 pixel black outline
         return (lx < 2 || lx >= PATCH_W - 2 || ly < PATCH_Y + 2 ||
                 ly >= PATCH_Y + PATCH_H - 2) ? 1 : 0;
      }
      return 0;
   }
   if(ly < TEXT_Y || lx < TEXT_X) {
      return 0;
   }
   ly -= TEXT_Y;
   Line = ly / LINE_H;
   ly = (ly % LINE_H) / 2;           // font row
   if(Line >= LINES || ly >= 7) {
      return 0;
   }
   lx -= TEXT_X;
   c = lx / CHAR_W;
   if(c >= LINE_LEN) {
      return 0;
   }
   c = gText[Line][c];
   lx = (lx % CHAR_W) / 2;           // font column
   if(lx >= 5 || c == 0) {
      return 0;
   }
   if(c >= '0' && c <= '9') {
      c = c - '0' + 1;
   }
   else if(c >= 'A' && c <= 'Z') {
      c = c - 'A' + 11;
   }
   else {
      c = 0;
   }
   if(!(gFont[c][lx] & (1 << ly))) {
      return 0;
   }
   return Line == 2 ? 2 : 1;         // the BASE line is yellow
}

void dsPixels8(uint16_t py,uint8_t x8)
{
   uint8_t Mask;
   uint16_t px = x8 * 8;
   uint8_t c;

   gDsBlack = 0;
   gDsYellow = 0;
   for(Mask = 0x80; Mask; Mask >>= 1, px++) {
      c = Color(DS_LX(py,px),DS_LY(py,px));
      if(c == 1) {
         gDsBlack |= Mask;
      }
      else if(c == 2) {
         gDsYellow |= Mask;
      }
   }
}
