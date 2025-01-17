#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "bahnschrift20.h"
#include "bahnschrift30.h"
#include "tahoma9.h"
#include "calibrib16.h"
#include "calibrib30.h"
#include "refsan12.h"
#include "twcondensed20.h"

#ifdef _WIN32
int optind = 1;
int optopt;
char *optarg;
int getopt(int argc,char **argv,char *opts);
#define strdup _strdup
#else
#include <unistd.h>
#endif

#define __code
#include "font.h"

#define DEFINED_CHARS   96
#define FONT_HEIGHT  16
#define FONT_WIDTH   10

#define OPTION_STRING  "lv"
static struct option gLongOpts[] = {
   {"all",no_argument,NULL,0},
   {"legacy",no_argument,NULL,0},
   {"bahnschrift20",no_argument,NULL,0},
   {"bahnschrift30",no_argument,NULL,0},
   {"tahoma9",no_argument,NULL,0},
   {"calibrib16",no_argument,NULL,0},
   {"calibrib30",no_argument,NULL,0},
   {"refsan12",no_argument,NULL,0},
   {"twcondensed20",no_argument,NULL,0},
   {NULL,0,0,0}
};


#define LOG(format, ... ) fprintf(stderr,format,## __VA_ARGS__)
#define LOGV(format, ... ) if(gVerbose) LOG(format,## __VA_ARGS__)

uint16_t gFontIndexTbl[DEFINED_CHARS];
uint16_t gPackedData[DEFINED_CHARS * FONT_HEIGHT];
uint16_t gFontData[DEFINED_CHARS][FONT_HEIGHT];
bool gVerbose = false;

int PackFont(void);
int ListFont(void);
void FillFontData(void);
void ListVfwFont(int OptNdx);

bool gList = false;

int main(int argc, char *argv[])
{
   int Ret = 0; // Assume the best
   int Option;
   int OptNdx = -1;

   while(Ret == 0) {
      Option = getopt_long(argc,argv,OPTION_STRING,gLongOpts,&OptNdx);
      if(Option == -1) {
         break;
      }
      switch(Option) {
         case 0:  // long option
            break;

         case 'l':
            gList = true;
            break;

         case 'v':
            gVerbose = true;
            break;

         case '?':
            Ret = EINVAL;
            break;
      }
   }

   if(!gList && OptNdx == -1) {
   // Font not specified
      Ret = EINVAL;
   }

   if(Ret != 0) {
      printf("Usage:\n");
      printf("  Generate packed_font header:\n");
      printf("    mkfont [-v] --<font> > <output_file>\n");
      printf("      -v: add verbose comments\n");
      printf("  Generate ASCII dump of font:\n");
      printf("    mkfont -l\n");
      printf("    mkfont -l --<font>\n\n");
      printf("  Supported fonts:\n");
      for(int i = 0; gLongOpts[i].name != NULL; i++) {
         printf("    --%s\n",gLongOpts[i].name);
      }
   }
   else if(gList) {
      if(OptNdx == -1) {
         for(int i = 1; gLongOpts[i].name != NULL; i++) {
            printf("%s font:\n",gLongOpts[i].name);
            if(i == 1) {
               ListFont();
            }
            else {
               ListVfwFont(i);
            }
         }
      }
      else if(OptNdx == 0) {
         printf("%s font:\n",gLongOpts[OptNdx].name);
         ListFont();
      }
      else {
         printf("%s font:\n",gLongOpts[OptNdx].name);
         ListVfwFont(OptNdx);
      }
   }
   else {
      PackFont();
   }
   return Ret;
}

int PackFont()
{
   int FontDataOffset = 0;
   int FontDataLen;
   int i;
   int j;
   int FirstBit;
   int LastBit;
   int IndexValue;
   int CharWidth;
   int TotalCharWidth = 0;
   uint8_t *pPackedData = (uint8_t *) gPackedData;

   FillFontData();

// Create a 7 bit wide space (averge char with is 6.7 bits)
   for(i = 0; i < DEFINED_CHARS; i++) {
      for(j = 0; j < FONT_WIDTH; j++) {
         if(gFontData[i][j] != 0) {
            break;
         }
      }

      if(j == FONT_WIDTH) {
      // completely Empty, space or not implmented
         if(i != 0) {
            LOGV("Font for '%c' not defined\n",(char) ' ' + i);
            exit(1);
         }
         FirstBit = 0;
         CharWidth = 7;
      }
      else {
         FirstBit = j;
         LastBit = 0;
         while(j < FONT_WIDTH) {
            if(gFontData[i][j] != 0) {
               LastBit = j;
            }
            j++;
         }
         CharWidth = LastBit - FirstBit + 1;
      }
      TotalCharWidth += CharWidth;
      LOGV("Char '%c' width %d bits\n",' ' + i,CharWidth);
   // Save index to packed font data and character width
      IndexValue = (CharWidth << 12) | FontDataOffset;
      gFontIndexTbl[i] = (uint16_t) IndexValue;
      FontDataOffset += CharWidth;
   // copy bits to packed data array
      memcpy(pPackedData,&gFontData[i][FirstBit],CharWidth * 2);
      pPackedData += CharWidth * 2;
   }
   FontDataLen = FontDataOffset;

   for(i = 0; i < DEFINED_CHARS; i++) {
      CharWidth = gFontIndexTbl[i] >> 12;
      FontDataOffset = gFontIndexTbl[i] & 0xfff;
      LOGV("Char '%c' width %d, data offset %d\n  ",' ' + i, CharWidth,
             FontDataOffset);
      for(j = 0; j < CharWidth; j++) {
         LOGV("%s0x%04x",j == 0 ?  "" : ", ",gPackedData[FontDataOffset]);
         FontDataOffset++;
      }
      LOGV("\n");
   }

   LOGV("Average char width %d.%d bits\n",
       TotalCharWidth / DEFINED_CHARS,
       TotalCharWidth % DEFINED_CHARS);

   LOG("Unpacked font data size %ld.\n",sizeof(font));
   LOG("Packed font data size %ld.\n",FontDataOffset * sizeof(uint16_t));
   LOG("Packed font index size %ld.\n",sizeof(gFontIndexTbl));
   LOG("Packing font file saved %ld bytes.\n",
       sizeof(font) 
       - sizeof(gFontIndexTbl) 
       - (FontDataOffset * sizeof(uint16_t)));

   printf("static const uint16_t __code gFontIndexTbl[96] = {\n");
   for(i = 0; i < DEFINED_CHARS; i++) {
      CharWidth = gFontIndexTbl[i] >> 12;
      FontDataOffset = gFontIndexTbl[i] & 0xfff;
      printf("   0x%04x,",(unsigned int) gFontIndexTbl[i]);
      printf("   // Char ");
      if((' ' + i) == 0x7f) {
         printf("0x%x",' ' + i);
      }
      else {
         printf("'%c'",' ' + i);
      }
      printf(" width %d, data offset 0x%x\n",CharWidth,FontDataOffset);
   }
   printf("\n};\n\n");

   printf("static const uint16_t __code gPackedData[%d] = {",FontDataLen);
   for(i = 0; i < FontDataLen; i++) {
      if((i % 8) == 0) {
         printf("\n// 0x%x\n   ",i);
      }
      printf("0x%04x,",(unsigned int) gPackedData[i]);
   }
   printf("\n};\n\n");

   return 0;
}

int ListFont()
{
   int i;
   int j;
   int k;
   uint16_t Mask;

   FillFontData();

   for(i = 0; i < DEFINED_CHARS; i++) {
      printf("Char 0x%x",' ' + i);
      if((' ' + i) != 0x7f) {
         printf(", '%c'",' ' + i);
      }
      printf("\n");

      Mask = 0x8000;
      for(j = 0; j < FONT_HEIGHT; j++) {
         printf("Line %2d: |",j);
         for(k = 0; k < FONT_WIDTH; k++) {
            printf("%c",(gFontData[i][k] & Mask) ? '*' : ' ');
         }
         Mask >>= 1;
         printf("|\n");
      }

      for(j = 0; j < FONT_HEIGHT; j++) {
         printf("0x%04x ",gFontData[i][j]);
      }
      printf("\n");
   }

   return 0;
}

void FillFontData()
{
   int i;
   int j;

// convert font[] from byte array to uint16_t array
   for(i = 0; i < DEFINED_CHARS; i++) {
      for(j = 0; j < FONT_WIDTH; j++) {
         gFontData[i][j] = font[i][j * 2] | (font[i][(j * 2) + 1] << 8);
      }
      LOGV("Char '%c':\n",' ' + i);
      for(j = 0; j < FONT_WIDTH; j++) {
         LOGV("%s0x%04x",j == 0 ?  "" : ", ",gFontData[i][j]);
      }
      if(gVerbose) {
         for(j = 0; j < FONT_WIDTH; j++) {
            LOGV("%s0x%04x",j == 0 ?  "" : ", ",gFontData[i][j]);
         }
      }
      LOGV("\n");
   }
}

void ListVfwFont(int OptNdx)
{
   uint8_t *cp;
   char Char;
   int FontHeight = 0;
   uint32_t Bits;
   uint32_t Mask;

   switch(OptNdx) {
      case 2:
         cp = bahnschrift20;
         FontHeight = 18;
         break;

      case 3:
         cp = bahnschrift30;
         FontHeight = 27;
         break;

      case 4:
         cp = tahoma9;
         FontHeight = 10;
         break;

      case 5:
         cp = calibrib16;
         FontHeight = 10;
         break;

      case 6:
         cp = calibrib30;
         FontHeight = 10;
         break;

      case 7:
         cp = refsan12;
         FontHeight = 10;
         break;

      case 8:
         cp = twcondensed20;
         FontHeight = 10;
         break;
   }

   if(FontHeight == 0) {
      printf("%s#%d: Internal error\n",__FUNCTION__,__LINE__);
      return;
   }

   for(Char = '!'; Char < 0x7f; Char++) {
      if(*cp != Char) {
         printf("Char 0x%x, '%c' is not defined in %s font\n",Char,Char,
                gLongOpts[OptNdx].name);
         continue;
      }
      cp++;
      int Width = *cp++;
      FontHeight = *cp++;
      printf("Char 0x%x, '%c', %d X %d:\n",Char,Char,Width,FontHeight);

      for(int i = 0; i < FontHeight; i++) {
      // Collect bits for this row

         if(Width < 9) {
            Bits = *cp++ << 24;
         }
         else if(Width < 17) {
            Bits = (cp[0] << 24) | (cp[1] << 16);
            cp += 2;
         }
         else if(Width < 25) {
            Bits = (cp[0] << 24) | (cp[1] << 16) | cp[2] << 8;
            cp += 3;
         }
         else {
            Bits = (cp[0] << 24) | (cp[1] << 16) | cp[2] << 8 | cp[3];
            cp += 4;
         }
         Mask = 0x80000000 >> (Width - 1);
         printf("Line %2d: ",i);
         printf("0x%08x |",Bits);
         for(int j = 0; j < Width; j++) {
            printf("%c",(Bits & Mask) ? '*' : ' ');
            Mask <<= 1;
         }
         printf("|\n");
      }
   }
}

