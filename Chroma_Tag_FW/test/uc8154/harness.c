// Host harness for board/chroma21/uc8154.c: runs the driver against a
// waveform image and prints every EPD command, see run_tests.py.
// usage: harness waveform.bin temperature
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "screen.h"
#include "uc8154.h"

uint8_t P0, P1, P0DIR, P1DIR, P0INP, P1INP, P1SEL, U0BAUD, U0GCR, U0CSR;
uint8_t gNEnable = 1;
uint8_t gTempBuf320[320];

static uint8_t gWave[4096];

struct Tx {
   uint8_t Byte;
   uint8_t bData;
   uint8_t bSelected;
};
static struct Tx gTx[1024];
static int gTxLen;
static int gShown;

uint8_t *TxReg(void)
{
   gTx[gTxLen].bData = (P0 & P0_EPD_D_nCMD) != 0;
   gTx[gTxLen].bSelected = (P1 & P1_EPD_nCS0) == 0;
   return &gTx[gTxLen++].Byte;
}

// everything sent since the last call, one line per command
static void Show(void)
{
   for(; gShown < gTxLen; gShown++) {
      if(!gTx[gShown].bSelected) {
         printf("NOT_SELECTED\n");
      }
      printf(gTx[gShown].bData ? " %02x" : "\ncmd %02x",gTx[gShown].Byte);
   }
}

void uc8154DelayMs(uint8_t Ms)
{
   Show();
   printf("\ndelay %d",Ms);
}

void uc8154WfRead(uint16_t Ofst,uint8_t *Dst,uint8_t Len)
{
   memcpy(Dst,&gWave[Ofst],Len);
}

int main(int argc,char **argv)
{
   FILE *f = fopen(argv[1],"rb");
   int Temperature = atoi(argv[2]);
   int OffDelay;

   if(f == NULL || fread(gWave,1,sizeof(gWave),f) == 0) {
      return 2;
   }
   fclose(f);

   if(!uc8154PowerOn(Temperature)) {
      Show();
      printf("\nno waveform nEnable %d\n",gNEnable);
      return 0;
   }
   Show();
   printf("\npowered nEnable %d",(P0 & P0_EPD_nENABLE) != 0);
   uc8154TxStart(false);
   einkSelect();
   uc8154TxPixels(0x80,0x40);    // black, yellow, 6 white
   einkDeselect();
   uc8154TxStart(true);
   einkSelect();
   uc8154TxByte(0xbf);
   einkDeselect();
   uc8154Refresh(false);
   uc8154SetupYellowRefresh();
   uc8154Refresh(true);
   OffDelay = uc8154PowerOffStart();
   Show();
   printf("\noffdelay %d",OffDelay);
   uc8154PowerOff();
   Show();
   printf("\noff nEnable %d P0 %02x P1 %02x P1SEL %02x\n",gNEnable,P0,P1,P1SEL);
   return 0;
}
