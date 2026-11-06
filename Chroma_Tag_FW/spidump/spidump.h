#ifndef _SPIDUMP_H_
#define _SPIDUMP_H_

#include <stdint.h>

// In the image: "SPIDUMP" + the 3 byte SPI flash address, MSB first, which
// chroma_spidump.py and spidump_join.py use.  It must stay above the code
// (build.sh checks that) and below the data at 0x1000.
#define DUMP_TAG_ADR    0x0ff0
#define DUMP_TAG_BASE   7
#define DUMP_TAG_LEN    10

#ifndef DS_HOST_TEST
extern __code __at (DUMP_TAG_ADR) const uint8_t gDumpTag[DUMP_TAG_LEN];
#endif

uint8_t SpiByte(uint8_t v);

#endif
