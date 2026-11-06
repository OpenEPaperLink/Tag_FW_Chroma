#include <stdbool.h>
#include "asmUtil.h"
#include "eeprom.h"
#include "printf.h"
#include "screen.h"
#include "board.h"
#include "adc.h"
#include "powermgt.h"
#include "settings.h"
#include "logging.h"
#include "timer.h"
#include "flashcopy.h"

#define pHARD_CODED_SN  ((const void __xdata*) &gDefaultEEPROM[8])

void LoadGlobalsFromEEPROM();
void ResetFactoryNVRAM(void);

char __xdata gMacString[17];
// Set when the factory NVRAM must be (re)written: no valid SN in it, or a
// patched SN in gDefaultEEPROM differs from it.
__bit gEEpromFailure;

#pragma callee_saves prvReadSetting
//returns token length if found, copies at most maxLen. returns -1 if not found
static int8_t prvReadSetting(uint8_t type, uint8_t __xdata *dst, uint8_t maxLen) 
{
   static const uint8_t __xdata magicNum[4] = {0x56, 0x12, 0x09, 0x85};
   uint8_t __xdata tmpBuf[4];
   uint8_t pg, gotLen = 0;
   __bit found = false;
   
   for (pg = 0; pg < 10; pg++) {
      uint16_t __pdata addr = (uint16_t)pg * EEPROM_ERZ_SECTOR_SZ;
      uint16_t __pdata ofst = 4;
      
      eepromRead(addr, tmpBuf, 4);
      if (xMemEqual4(tmpBuf, magicNum)) {
         while (ofst < EEPROM_ERZ_SECTOR_SZ) {
         // first byte is type, (0xff for done), second is length
            eepromRead(addr + ofst, tmpBuf, 2);
         // a length < 2 can't be walked (seen on Chroma60), stop here
            if (tmpBuf[0] == 0xff || tmpBuf[1] < 2) {
               break;
            }
            
            if (tmpBuf[0] == type && tmpBuf[1] >= 2) {
               uint8_t copyLen = gotLen = tmpBuf[1] - 2;
               if (copyLen > maxLen) {
                  copyLen = maxLen;
               }
               eepromRead(2 + addr + ofst, dst, copyLen);
               found = true;
            }
            ofst += tmpBuf[1];
         }
      }
   }
   return found ? gotLen : -1;
}

void boardInitStage2(void)
{
   powerUp(INIT_EEPROM);

#ifndef SFDP_DISABLED
   if(!eepromInit()) {
      pr("failed to init eeprom\n");
      while(1);
   }
#endif
   LoadGlobalsFromEEPROM();
#ifdef RELEASE_BUILD
// A single failed read must not wipe the factory NVRAM: after a bouncy
// battery insertion the SPI flash may not be ready yet, so retry first.
   for(uint8_t Retry = 0; gEEpromFailure && Retry < 3; Retry++) {
      timerDelay(TIMER_TICKS_PER_SECOND / 10);
      eepromWakeFromPowerdown();
      LoadGlobalsFromEEPROM();
   }
// Only write the NVRAM if the flash answers, otherwise the decision is based
// on reads of a flash that isn't there.  main() then shows the "no EEPROM"
// screen, and the NVRAM is read again when the battery is reinserted.
   if(gEEpromFailure && eepromResponds()) {
   // Not good!  Try to set defaults so we can communicate
      NV_DATA_LOG("First call to LoadGlobalsFromEEPROM failed\n");
      ResetFactoryNVRAM();
   // Try to load globals again
      LoadGlobalsFromEEPROM();
      if(gEEpromFailure) {
         NV_DATA_LOG("Second call to LoadGlobalsFromEEPROM failed\n");
      }
   }
#endif

   powerDown(INIT_EEPROM);
// On some board (Chroma29 for example) we don't know how to deal
// with the display's GPIO until we know the SN.
   screenInitGPIO(false);
   irqsOn();
}

// JM 10339094 B
void LoadGlobalsFromEEPROM()
{
// Set mSelfMac from the device SN stored in the factory "NVRAM".
// Note: apparently some boards have a 6 character SN and some have a 7.
// Only the first 6 charcters are used.
   gEEpromFailure = true;  // Assume the worse !
      
   if(prvReadSetting(0x2a,gTempBuf320,7) < 0 && 
      prvReadSetting(1,gTempBuf320,6) < 0) 
   {  // Couldn't get SN from factory EEPROM, set to the hardcoded value 
      NV_DATA_LOG("failed to get SN\n");
      return;
   }
// Got the SN
#ifdef RELEASE_BUILD
   xMemSet(&gTempBuf320[16],0x00,4);

// If the built in default SN has been patched make sure it matches the SN
// in EEPROM.  This allows recovery from accidentially programming the same
// SN into two displays
   if(!xMemEqual4(pHARD_CODED_SN,&gTempBuf320[16]) &&
      !xMemEqual4(pHARD_CODED_SN,&gTempBuf320[2]))
   {  // The hardcoded SN is not the default (all zeros) and the SN in
      // EEPROM does not match, set EEPROM to the (new) defaults
      NV_DATA_LOG("Set SN to patched value\n");
      return;
   }
#endif

   mSelfMac[7] = 0x44;
   mSelfMac[6] = 0x67;
   mSelfMac[3] = gTempBuf320[2];
   mSelfMac[2] = gTempBuf320[3];
   mSelfMac[1] = gTempBuf320[4];
   mSelfMac[0] = gTempBuf320[5];
   gTempBuf320[2] = 0;
   LOGA("SN %s%02x%02x",gTempBuf320,mSelfMac[3],mSelfMac[2]);
   LOGA("%02x%02x\n",mSelfMac[1],mSelfMac[0]);
// Since the first 2 characters are known to be upper case ASCII subtract
// 'A' from the value to convert it from a 7 bit value to a 5 bit value.
// 
// This allows us to use the extra 3 bits to convey something else like the
// hardware variation.
// 
// i.e. Multiple incompatible hardware variations that need different FW 
// images, but otherwise are compatible can share a single HWID and the 
// AP can parse the "extra" bits in the MAC address to select the 
// correct OTA image.

   mSelfMac[5] = gTempBuf320[0] - 'A';
   mSelfMac[4] = gTempBuf320[1] - 'A';
#ifdef HW_VARIANT
   mSelfMac[5] |= HW_VARIANT << 5;
   LOGA("HW variant %d\n",HW_VARIANT);
#endif

   /* The formatter's addressed 64-bit hex mode preserves all leading zeros
    * and emits the same sixteen uppercase digits, most-significant byte first. */
   spr(gMacString,"%*016llX",(uintptr_near_t)mSelfMac);
#ifdef SCREEN_EXPECTS_VCOM
   if(prvReadSetting(0x23,&mScreenVcom,1) < 0) {
      NV_DATA_LOG("failed to get VCOM\n");
   // keep the SN, a missing calibration value is no reason to reset the NVRAM
      mScreenVcom = DEFAULT_VCOM;
   }
   NV_DATA_LOG("VCOM: 0x%02x\n", mScreenVcom);
#endif
   
   if(prvReadSetting(0x12,&mAdcSlope,2) < 0) {
      NV_DATA_LOG("failed to get ADC slope\n");
      mAdcSlope = DEFAULT_ADC_SLOPE;
   }
   NV_DATA_LOG("ADC slope %d\n",mAdcSlope);

   if(prvReadSetting(0x09,&mAdcIntercept,2) < 0) {
      NV_DATA_LOG("failed to get ADC intercept\n");
      mAdcIntercept = DEFAULT_ADC_INTERCEPT;
   }
   NV_DATA_LOG("ADC mAdcIntercept %d\n",mAdcIntercept);

// All is good, don't reset the EEPROM
   gEEpromFailure = false;
}


//copied to ram, after update has been verified, interrupts have been disabled, 
//and eepromReadStart() has been called.
//Does not return (resets using WDT)
//this func wraps the update code and returns its address (in DPTR), len in B
static uint32_t prvUpdateApplierGet(void) __naked
{
   __asm__(
      "  mov   DPTR, #00098$        \n"
      "  mov   A, #00099$        \n"
      "  clr   C                 \n"
      "  subb  A, DPL            \n"
      "  mov   B, A              \n"
      "  ret                     \n"
      
      ///actual updater code
      "00098$:                \n"
   
      "  mov   B, #32            \n"
      //erase all flash
      "  clr   _FADDRH           \n"
      "  clr   _FADDRL           \n"   
      FLASH_ERASE_PAGES_ASM
      
      //write all 32K
      //due to the 40 usec timeout, we wait each time to avoid it
      "  mov   DPTR, #0       \n"
      
      FLASH_WRITE_FROM_SPI_ASM
      
      //done
   
      //WDT reset
      "  mov   _WDCTL, #0x0b        \n"
      "00007$:                \n"
      "  sjmp  00007$            \n"
      
      "00099$:                \n"
   );
}

void selfUpdate(void)
{
   uint32_t updaterInfo = prvUpdateApplierGet();
   
   xMemCopyShort(mScreenRow, (void __xdata*)(uint16_t)updaterInfo, updaterInfo >> 16);
         
   DMAARM = 0xff; //all DMA channels off
   IEN0 = 0;   //ints off
   
   MEMCTR = 3; //cache and prefetch off
   
   __asm__(
      "  mov dptr, #_mScreenRow     \n"
      "  clr a                \n"
      "  jmp @a+dptr             \n"
   );
}

#if !defined(RELEASE_BUILD)
uint8_t rndGen8(void)
{
   ADCCON1 |= 4;
   while (ADCCON1 & 0x0c);
   return RNDH ^ RNDL;
}

uint32_t rndGen32(void) __naked
{
   __asm__ (
      //there simply is no way to get SDCC to generate this anywhere near as cleanly
      "  lcall  _rndGen8      \n"
      "  push   DPL        \n"
      "  lcall  _rndGen8      \n"
      "  push   DPL        \n"
      "  lcall  _rndGen8      \n"
      "  push   DPL        \n"
      "  lcall  _rndGen8      \n"
      "  pop    DPH        \n"
      "  pop    B       \n"
      "  pop    A       \n"
      "  ret               \n" 
   );
}

void rndSeed(uint8_t seedA, uint8_t seedB)
{
   RNDL = seedA;
   RNDL = seedB;
}
#endif
