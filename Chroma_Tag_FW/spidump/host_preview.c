// Host preview of the spidump screen: runs dumpscreen.c on the PC and
// writes what the panel should show (logical/OEPL orientation) as a PPM.
//
// build (from this directory, one line):
//   gcc -DDS_HOST_TEST '-DDS_BOARD_H="boards/chroma21.h"'
//       -o /tmp/preview host_preview.c dumpscreen.c
// run:  /tmp/preview spi_dump.bin preview.ppm [base]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dumpscreen.h"

uint8_t gHostCode[0x8000];
uint8_t gDumpTagData[DUMP_TAG_LEN];

int main(int argc,char **argv)
{
   static uint8_t Img[DS_HEIGHT][DS_WIDTH];
   FILE *f;
   int py, x8, b;
   unsigned long Base;

   memset(gHostCode,0xff,sizeof(gHostCode));
   if(argc < 3 || (f = fopen(argv[1],"rb")) == NULL) {
      fprintf(stderr,"usage: preview spi_dump.bin out.ppm [base]\n");
      return 1;
   }
   Base = argc > 3 ? strtoul(argv[3],NULL,0) : 0;
   gDumpTagData[DUMP_TAG_BASE] = Base >> 16;
   gDumpTagData[DUMP_TAG_BASE + 1] = Base >> 8;
   gDumpTagData[DUMP_TAG_BASE + 2] = Base;
// the dumper's copy: SPI flash at the base address, at CC1110 flash 0x1000
   fseek(f,Base,SEEK_SET);
   if(fread(&gHostCode[0x1000],1,0x7000,f) == 0) {
      return 1;
   }
   fclose(f);

   dsInit();
// walk the panel in the order the driver sends it, map back to logical
   for(py = 0; py < DS_GATES; py++) {
      for(x8 = 0; x8 < DS_SOURCES / 8; x8++) {
         dsPixels8(py,x8);
         for(b = 0; b < 8; b++) {
            int px = x8 * 8 + b;
            uint8_t m = 0x80 >> b;
            Img[DS_LY(py,px)][DS_LX(py,px)] =
               (gDsYellow & m) ? 2 : (gDsBlack & m) ? 1 : 0;
         }
      }
   }
   f = fopen(argv[2],"wb");
   fprintf(f,"P6\n%d %d\n255\n",DS_WIDTH,DS_HEIGHT);
   for(py = 0; py < DS_HEIGHT; py++) {
      for(b = 0; b < DS_WIDTH; b++) {
         static const uint8_t Rgb[3][3] = {{255,255,255},{0,0,0},{230,200,0}};
         fwrite(Rgb[Img[py][b]],1,3,f);
      }
   }
   fclose(f);
   return 0;
}
