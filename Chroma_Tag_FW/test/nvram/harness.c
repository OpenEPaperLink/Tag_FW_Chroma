// Host test harness for the factory NVRAM boot code.
//
// Compiles the real prvReadSetting(), LoadGlobalsFromEEPROM(),
// boardInitStage2() and ResetFactoryNVRAM() (see extract.py) with gcc,
// against a simulated SPI flash loaded from a file.
//
// usage: harness <in.bin> <out.bin> <fail_until_ms> <patched_sn_hex|->
//
// fail_until_ms: flash reads return 0xff until the simulated clock reaches
//                this (0 = never fail), to model a flash that isn't ready.
// Prints one JSON line with the result.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SDCC keywords and attributes
#define __xdata
#define __code
#define __pdata
#define __reentrant
#define __bit bool

#define RELEASE_BUILD
#define SFDP_DISABLED
#define EEPROM_ERZ_SECTOR_SZ 4096
#define TIMER_TICKS_PER_SECOND 1000UL   // simulated: 1 tick = 1 ms
#define INIT_EEPROM 0x02

#define LOGA(...) printf(__VA_ARGS__)
#define NV_DATA_LOG(...) printf(__VA_ARGS__)
#define pr(...) printf(__VA_ARGS__)
#define spr(out, ...) sprintf(out, __VA_ARGS__)

#define FLASH_SIZE 0x100000UL
#define READ_LIMIT 2000000UL   // more flash reads than this = hung boot

static uint8_t gFlash[FLASH_SIZE];
static uint32_t gNowMs;
static uint32_t gFailUntilMs;
static unsigned long gReads, gErases, gWrites;

// firmware globals
uint8_t mSelfMac[8];
uint8_t gTempBuf320[320];
uint8_t mScreenVcom;
uint16_t mAdcSlope, mAdcIntercept;
char gMacString[17];
bool gEEpromFailure;

// board defaults, as in board/<board>/board.h and board.c
#define DEFAULT_VCOM           0x30
#define DEFAULT_ADC_INTERCEPT  0x02ec
#define DEFAULT_ADC_SLOPE      0x0a5a

uint8_t gDefaultEEPROM[] = {
   0x56, 0x12, 0x09, 0x85,
   0x01, 8, 'J', 'D', 0, 0, 0, 0,
#ifdef SCREEN_EXPECTS_VCOM
   0x23, 3, DEFAULT_VCOM,
#endif
   0x09, 4, DEFAULT_ADC_INTERCEPT & 0xff, DEFAULT_ADC_INTERCEPT >> 8,
   0x12, 4, DEFAULT_ADC_SLOPE & 0xff, DEFAULT_ADC_SLOPE >> 8,
};

#define pHARD_CODED_SN ((const void *) &gDefaultEEPROM[8])

static void done(const char *outcome);

void eepromRead(uint32_t addr, void *dst, uint16_t len)
{
   if(++gReads > READ_LIMIT) {
      done("hang");
   }
   if(gNowMs < gFailUntilMs) {
      memset(dst, 0xff, len);
   }
   else {
      memcpy(dst, &gFlash[addr], len);
   }
}

void eepromErase(uint32_t addr, uint16_t sectors)
{
   gErases++;
   memset(&gFlash[addr], 0xff, (size_t) sectors * EEPROM_ERZ_SECTOR_SZ);
}

void eepromWrite(uint32_t addr, const void *src, uint16_t len)
{
   const uint8_t *p = src;
   gWrites++;
   for(uint16_t i = 0; i < len; i++) {
      gFlash[addr + i] &= p[i];   // NOR flash: programming only clears bits
   }
}

void timerDelay(uint32_t ticks) { gNowMs += ticks; }
bool eepromResponds(void) { return gNowMs >= gFailUntilMs; }
void eepromWakeFromPowerdown(void) {}
void powerUp(uint8_t parts) { (void) parts; }
void powerDown(uint8_t parts) { (void) parts; }
void screenInitGPIO(bool activate) { (void) activate; }
void irqsOn(void) {}

uint32_t mathPrvMul16x8(uint16_t a, uint8_t b) { return (uint32_t) a * b; }
void xMemSet(void *mem, uint8_t val, uint16_t num) { memset(mem, val, num); }
bool xMemEqual(const void *a, const void *b, uint8_t num) { return !memcmp(a, b, num); }

static int8_t prvReadSetting(uint8_t type, uint8_t *dst, uint8_t maxLen);
void LoadGlobalsFromEEPROM();
void ResetFactoryNVRAM(void);

#include "extracted.c"

static const char *gOutPath;

static void done(const char *outcome)
{
   FILE *f = fopen(gOutPath, "wb");
   fwrite(gFlash, 1, FLASH_SIZE, f);
   fclose(f);
   printf("\n@@RESULT {\"outcome\":\"%s\",\"failure\":%d,\"mac\":\"%s\","
          "\"vcom\":%u,\"slope\":%u,\"intercept\":%u,"
          "\"erases\":%lu,\"writes\":%lu,\"reads\":%lu,\"ms\":%u}\n",
          outcome, gEEpromFailure, gMacString, mScreenVcom, mAdcSlope,
          mAdcIntercept, gErases, gWrites, gReads, gNowMs);
   exit(0);
}

int main(int argc, char *argv[])
{
   if(argc != 5) {
      fprintf(stderr, "usage: %s in.bin out.bin fail_until_ms sn_hex|-\n", argv[0]);
      return 2;
   }
   FILE *f = fopen(argv[1], "rb");
   memset(gFlash, 0xff, sizeof(gFlash));
   fread(gFlash, 1, FLASH_SIZE, f);
   fclose(f);
   gOutPath = argv[2];
   gFailUntilMs = (uint32_t) atoi(argv[3]);
   if(strcmp(argv[4], "-")) {
      // patched SN, as patch_sn.py writes it: 4 bytes of hex digits
      for(int i = 0; i < 4; i++) {
         unsigned v;
         sscanf(&argv[4][i * 2], "%2x", &v);
         gDefaultEEPROM[8 + i] = (uint8_t) v;
      }
   }
   boardInitStage2();
   done("ok");
}
