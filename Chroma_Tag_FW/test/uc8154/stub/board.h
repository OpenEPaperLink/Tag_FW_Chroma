// Stands in for board/chroma21/board.h in the host test
#ifndef _BOARD_H_
#define _BOARD_H_

#include <stdint.h>

#define __xdata
#define __code
#define __bit uint8_t

#include "../../../board/boardPins.h"

extern uint8_t P0, P1, P0DIR, P1DIR, P0INP, P1INP, P1SEL, U0BAUD, U0GCR, U0CSR;
extern uint8_t gNEnable;
extern uint8_t gTempBuf320[320];

// a write to U0DBUF is one byte on the SPI bus
uint8_t *TxReg(void);
#define U0DBUF   (*TxReg())

#define P1_0     1           // BUSY: never busy
#define P0_6     gNEnable

#define screenPrvWaitByteSent()
#define einkSelect()    do { P1 &= (uint8_t) ~P1_EPD_nCS0; } while(0)
#define einkDeselect()  do { P1 |= P1_EPD_nCS0; } while(0)

#endif
