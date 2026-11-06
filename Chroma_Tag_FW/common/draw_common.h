#ifndef _DRAW_COMMON_H_
#define _DRAW_COMMON_H_
#include <stdint.h>

typedef void (*StrFormatOutputFunc)(uint32_t param /* low byte is data, bits 24..31 is char */) __reentrant;

// Width \f centers text in.  A board with a side logo can define it in
// screen.h (included first) to leave room for the logo.
#ifndef TEXT_WIDTH
#define TEXT_WIDTH   DISPLAY_WIDTH
#endif

extern const uint16_t __code gFontIndexTbl[96];
extern const uint16_t __code gPackedData[];



void SetFontSize();
void ProcessEscapes(uint8_t Char);

#pragma callee_saves CalcLineWidth
void CalcLineWidth(uint32_t data) __reentrant;

#pragma callee_saves prvPrintFormat
void prvPrintFormat(StrFormatOutputFunc formatF, uint16_t formatD, const char __code *fmt, va_list vl) __reentrant __naked;

#pragma callee_saves epdPutchar
void epdPutchar(uint32_t data) __reentrant;

#endif   // _DRAW_COMMON_H_

