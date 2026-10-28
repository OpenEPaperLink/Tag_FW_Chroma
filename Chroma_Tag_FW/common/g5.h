#ifndef G5_H
#define G5_H
#include <stdint.h>
#ifndef __SDCC
#define __xdata
#endif

/* A single non-reentrant decoder owns the buffer after download completion. */
#define G5_MAX_WIDTH 640
#define G5_ROW_BYTES 80
#define G5_INPUT_BYTES 128

typedef struct {
    uint16_t width, height, line;
    uint8_t pitch, longbits;
    uint8_t __xdata *cur, *ref;
    uint8_t byte, mask;
    int8_t error;
} G5State;
typedef struct {
    uint16_t e1, e2, e3, b2, start, second;
    uint8_t white, refwhite;
    int8_t mode;
    uint16_t br_start, br_end;
    uint8_t br_left, br_right, br_tail;
} G5Work;
typedef struct {
    uint32_t source, remaining, destination;
    uint16_t width, height;
    uint8_t pos, count;
    uint32_t rawSize;
} G5Transfer;

typedef union { G5State value; uint8_t bytes[32]; } G5StateRegion;
typedef union { G5Work value; uint8_t bytes[32]; } G5WorkRegion;
typedef union { G5Transfer value; uint8_t bytes[32]; } G5TransferRegion;
typedef struct {
    uint8_t rows[2][G5_ROW_BYTES];
    G5StateRegion state;
    G5WorkRegion work;
    G5TransferRegion transfer;
    uint8_t input[G5_INPUT_BYTES];
} G5Workspace;

typedef char G5StateFits[(sizeof(G5State) <= 32) ? 1 : -1];
typedef char G5WorkFits[(sizeof(G5Work) <= 32) ? 1 : -1];
typedef char G5TransferFits[(sizeof(G5Transfer) <= 32) ? 1 : -1];
typedef char G5WorkspaceFits[(sizeof(G5Workspace) <= 384) ? 1 : -1];

/* Caller supplies source (including the six-byte header), remaining bytes,
 * and a separate erased destination in imageScratch.g5.transfer.value.
 * Returns a raw image data type, or zero on failure. EEPROM must be awake.
 * This writes neither image-valid markers nor protocol acknowledgements. */
uint8_t g5Decode(void);
#ifdef G5_HOST_TEST
int8_t g5p_init(uint16_t width, uint16_t height,
                uint8_t __xdata *row0, uint8_t __xdata *row1);
int8_t g5p_decode_line(void);
uint8_t __xdata *g5p_row(void);
#endif
#endif
