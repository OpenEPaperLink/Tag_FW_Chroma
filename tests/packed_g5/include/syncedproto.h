#ifndef G5_TEST_SYNCEDPROTO_H
#define G5_TEST_SYNCEDPROTO_H
#include "g5.h"
#define BLOCK_XFER_BUFFER_SIZE 2083
union ImageScratch {
    uint8_t bytes[BLOCK_XFER_BUFFER_SIZE];
    G5Workspace g5;
};
extern union ImageScratch imageScratch;
#define blockbuffer imageScratch.bytes
#endif
