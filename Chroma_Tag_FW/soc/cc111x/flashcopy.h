#ifndef _FLASHCOPY_H_
#define _FLASHCOPY_H_

// Pieces of the code that copies SPI flash data into the CC1110's flash, used
// by the OTA update applier (soc.c) and spidump (spidump/spidump.c).  They are
// inline assembler for a routine running from RAM: it can't call anything,
// and only relative jumps work because it's copied there.

// Erase B flash pages starting at FADDRH.  FADDR is a word address.
#define FLASH_ERASE_PAGES_ASM \
   "00001$:                \n" \
   "  orl   _FCTL, #0x01         \n" \
   "  nop                     \n" \
   "00002$:                \n" \
   "  mov   A, _FCTL          \n" \
   "  jb    A.7, 00002$       \n" \
   "  inc   _FADDRH           \n" \
   "  inc   _FADDRH           \n" \
   "  djnz  B, 00001$            \n"

// Write flash words from DPTR up to word 0x3fff with the data of the read in
// progress on the SPI flash.  It waits for each word to avoid the 40 usec
// timeout of the flash controller.  FWT is for a 26 MHz clock.
#define FLASH_WRITE_FROM_SPI_ASM \
   "00003$:                \n" \
   "  mov   _FADDRH, DPH         \n" \
   "  mov   _FADDRL, DPL         \n" \
   "  inc   DPTR              \n" \
   "  mov   _FWT, #0x22       \n" \
   "   mov   B, #2               \n" \
   "00090$:                \n" \
   "  mov   _U1DBUF, #0x00    \n" \
   "00091$:                \n" \
   "  mov   A, _U1CSR            \n" \
   "  jnb   A.1, 00091$       \n" \
   "  anl   _U1CSR, #~0x02    \n" \
   "00092$:                \n" \
   "  mov   A, _U1CSR            \n" \
   "  jb    A.0, 00092$       \n" \
   "  push  _U1DBUF           \n" \
   "  djnz  B, 00090$            \n" \
   "  orl   _FCTL, #0x02         \n" \
   "00012$:                \n" \
   "  mov   A, _FCTL          \n" \
   "  jb    A.6, 00012$       \n" \
   "  pop   A                 \n" \
   "  pop   _FWDATA           \n" \
   "  mov   _FWDATA, A        \n" \
   "00004$:                \n" \
   "  mov   A, _FCTL          \n" \
   "  jb    A.6, 00004$       \n" \
   "  anl   _FCTL, #~0x02        \n" \
   "00005$:                \n" \
   "  mov   A, _FCTL          \n" \
   "  jb    A.7, 00005$       \n" \
   "  mov   A, DPH            \n" \
   "  cjne  A, #0x40, 00003$     \n"

#endif
