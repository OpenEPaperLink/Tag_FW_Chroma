#ifndef _UC8154_H_
#define _UC8154_H_

// Chroma21 UC8154 panel driver, used by the firmware (screen.c) and by the
// test screen of spidump (spidump/epd_uc8154.c).  See uc8154.c.

#include <stdbool.h>
#include <stdint.h>

// DTM1 pixel codes with DDX = 1 (CDI 0x17).  The register names don't match
// what the factory LUTs do on this panel (seen on hardware): the LUT the stock
// firmware loads into LUTW drives pixels black, like the Chroma29.
#define PIXEL_BLACK              3     // R21h (LUTW), map slot 5
#define PIXEL_YELLOW             2     // R24h (LUTG2), slot 4: to white, then
                                       // the yellow refresh drives LUTR0
#define PIXEL_WHITE              0     // R22h (LUTB), slot 2
#define PIXEL_RED_YELLOW         PIXEL_YELLOW

// Provided by the user of this driver
void uc8154DelayMs(uint8_t Ms);
// Read Len bytes at offset Ofst of the factory waveform tables
void uc8154WfRead(uint16_t Ofst,uint8_t __xdata *Dst,uint8_t Len);

// Load the waveform for this temperature, power up and set up the EPD.  The
// SPI flash must be usable.  Returns false, with the EPD still off, if there
// is no waveform (UC8154_NO_DEFAULT_WAVEFORM builds only).
bool uc8154PowerOn(int8_t Temperature);

// Start the pixel data: bYellow false: DTM1, then uc8154TxPixels() for each
// 8 pixels.  true: DTM2, then uc8154TxByte() with a 0 bit for yellow pixels.
// The caller selects the EPD (einkSelect) around the data.
void uc8154TxStart(bool bYellow);
void uc8154TxPixels(uint8_t Black,uint8_t Yellow);
void uc8154TxByte(uint8_t Byte);

// Start a refresh.  It's over when EPD_BUSY() is high.
void uc8154Refresh(bool bYellow);
void uc8154WaitNotBusy(void);

// Between the two refreshes: run LUT2 with null B/W LUTs
void uc8154SetupYellowRefresh(void);

// Power off in two steps, wait for the returned time in ms in between
uint16_t uc8154PowerOffStart(void);
void uc8154PowerOff(void);

// EPD pins low (so the EPD isn't back powered), EPD power off
void uc8154PinsOff(void);

#endif
