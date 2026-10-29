/* Host harness for extracted, unchanged production storage and EEPROM bodies.
 * test_storage.py generates storage_production.inc from the live firmware.
 * SPI emulates busy NOR and page programming; it is not a decoder model. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "platform.h"
#include "../../shared/oepl-proto.h"
#undef BLOCK_XFER_BUFFER_SIZE
#include "g5.h"
#include "syncedproto.h"

#define DATATYPE_IMG_G5 0x31
#define EEPROM_IMG_START 0x19000UL
#define EEPROM_ERZ_SECTOR_SZ 4096
#define EEPROM_WRITE_PAGE_SZ 256
#define EEPROM_IMG_SECTORS (EEPROM_IMG_EACH / EEPROM_ERZ_SECTOR_SZ)
#define IMAGE_SLOTS ((EEPROM_SIZE - EEPROM_IMG_START) / EEPROM_IMG_EACH)
#define EEPROM_IMG_VALID 0x474d4721UL
#define CUSTOM_IMAGE_NOCUSTOM 0
#define CUSTOM_IMAGE_SLIDESHOW 15
#define INIT_EEPROM 1
#define LOGA(...) ((void)0)
#define LOGE(...) ((void)0)
#define BLOCK_LOG(...) ((void)0)
#define PROTO_LOG(...) ((void)0)
#define wdt10s() ((void)0)
#define wdt60s() ((void)0)
#define eih ((struct EepromImageHeader *)blockbuffer)

union ImageScratch imageScratch;
static struct blockRequest curBlock;
static uint8_t curDispDataVer[8];
static struct AvailDataInfo xferDataInfo;
static uint8_t xferImgSlot, curImgSlot, nextImgSlot;
static uint32_t curHighSlotId, imageSize;
static uint16_t dataRequestSize;
static uint32_t markerValid = EEPROM_IMG_VALID;
struct imageDataTypeArgStruct arg;

static uint8_t nor[EEPROM_SIZE];
static unsigned powered, selected, command, phase, busy, write_enabled;
static uint32_t spi_address, program_start;
static unsigned marker_commits, replacement_erases, incoming_type, incoming_argument;
static uint64_t incoming_version;
static unsigned ack_count, draw_count, get_count, program_count, erase_count;
static unsigned busy_reads, unsafe_acks;
static const uint8_t *payload;
static uint32_t payload_length;
static int fail_block;
static uint8_t source_slot;

static void eepromPrvBusyWait(void);
static uint32_t getAddressForSlot(uint8_t slot);

static void eepromPrvSelect(void)
{
    assert(powered && !selected);
    selected = 1; command = phase = 0;
}
static void eepromPrvDeselect(void)
{
    assert(selected);
    if (command == 0x02) {
        busy = 3; write_enabled = 0; ++program_count;
    }
    selected = 0;
}
static uint8_t eepromByte(uint8_t value)
{
    assert(selected);
    if (!command) {
        command = value;
        if (command == 0x06) write_enabled = 1;
        if (command == 0x03 && busy) ++busy_reads;
        if (command == 0x02) assert(!busy && write_enabled);
        return 0;
    }
    if (command == 0x05) {
        if (busy) { --busy; return 1; }
        return 0;
    }
    if (command == 0x03 || command == 0x02) {
        if (phase < 3) {
            spi_address = phase ? (spi_address << 8) | value : value;
            ++phase;
            if (phase == 3 && command == 0x02) program_start = spi_address;
            return 0;
        }
        assert(spi_address < sizeof(nor));
        if (command == 0x03) return busy ? 0xff : nor[spi_address++];
        /* Publishing the first validity byte must be a separate operation,
         * after every other header field has finished programming. */
        if (spi_address >= EEPROM_IMG_START &&
            (spi_address - EEPROM_IMG_START) % EEPROM_IMG_EACH ==
                offsetof(struct EepromImageHeader, validMarker) && value == 0x21) {
            uint32_t base = spi_address - offsetof(struct EepromImageHeader, validMarker);
            struct EepromImageHeader header;
            memcpy(&header, nor + base, sizeof(header));
            assert(program_start == spi_address);
            assert(header.validMarker == UINT32_MAX);
            assert(header.version == xferDataInfo.dataVer && header.size == imageSize);
            assert(header.dataType == xferDataInfo.dataType && header.id == curHighSlotId);
            assert(header.argument == xferDataInfo.dataTypeArgument);
            ++marker_commits;
        }
        nor[spi_address++] &= value;
    }
    return 0;
}
static void powerUp(unsigned what) { assert(what == INIT_EEPROM); powered = 1; }
static void powerDown(unsigned what)
{
    assert(what == INIT_EEPROM);
    if (powered) eepromPrvBusyWait();
    powered = 0;
}
bool eepromErase(uint32_t address, uint16_t sectors)
{
    assert(powered && !busy && !(address % EEPROM_ERZ_SECTOR_SZ));
    uint32_t length = (uint32_t)sectors * EEPROM_ERZ_SECTOR_SZ;
    assert(address >= EEPROM_IMG_START && address + length <= sizeof(nor));
    struct EepromImageHeader old;
    memcpy(&old, nor + address, sizeof(old));
    if (incoming_type == DATATYPE_IMG_G5 && old.validMarker == EEPROM_IMG_VALID
        && (old.argument & 0xf8)) {
        struct EepromImageHeader replacement;
        bool found = false;
        /* A cache-hit cleanup may coexist with another pending transfer.
         * Locate its committed replacement without using transfer globals. */
        for (unsigned slot = 0; slot < IMAGE_SLOTS; ++slot) {
            uint32_t base = getAddressForSlot(slot);
            if (base == address) continue;
            memcpy(&replacement, nor + base, sizeof(replacement));
            if (replacement.validMarker == EEPROM_IMG_VALID &&
                replacement.version == incoming_version &&
                replacement.argument == incoming_argument) { found = true; break; }
        }
        assert(found);
        /* Cache-hit recovery has no active transfer state: compare the
         * committed entry to the announcement and fixture's raw geometry. */
        assert(payload_length >= 6 && (payload[5] == 1 || payload[5] == 2));
        assert(replacement.version == incoming_version);
        assert(replacement.size == (uint32_t)(SCREEN_WIDTH / 8) * SCREEN_HEIGHT * payload[5]);
        assert(replacement.dataType == (payload[5] == 1 ? DATATYPE_IMG_RAW_1BPP : DATATYPE_IMG_RAW_2BPP));
        assert(replacement.id && replacement.id <= curHighSlotId);
        assert(replacement.argument == incoming_argument);
        ++replacement_erases;
    }
    memset(nor + address, 255, length); ++erase_count; return true;
}
static bool xMemEqual(const void *a, const void *b, uint16_t n) { return !memcmp(a,b,n); }
static bool xMemEqual4(const void *a, const void *b) { return !memcmp(a,b,4); }
static void xMemCopy8(void *a, const void *b) { memcpy(a,b,8); }
static void xMemCopyShort(void *a, const void *b, uint8_t n) { memcpy(a,b,n); }
static void sendXferComplete(void) { ++ack_count; if (busy) ++unsafe_acks; }
static void drawImageFromEeprom(uint8_t slot, uint8_t lut)
{
    (void)lut;
    assert(slot < IMAGE_SLOTS);
    struct EepromImageHeader header;
    memcpy(&header, nor + getAddressForSlot(slot), sizeof(header));
    assert(header.validMarker == EEPROM_IMG_VALID && header.size <= EEPROM_IMG_EACH - sizeof(header));
    assert(header.dataType == DATATYPE_IMG_RAW_1BPP || header.dataType == DATATYPE_IMG_RAW_2BPP);
    ++draw_count;
}
static bool getDataBlock(uint16_t amount)
{
    ++get_count; source_slot = xferImgSlot;
    if ((int)curBlock.blockId == fail_block) { fail_block = -1; return false; }
    uint32_t offset = (uint32_t)curBlock.blockId * BLOCK_DATA_SIZE;
    assert(offset + amount <= payload_length && xferImgSlot < IMAGE_SLOTS);
    uint32_t address = getAddressForSlot(xferImgSlot) + sizeof(struct EepromImageHeader) + offset;
    assert(address + amount <= getAddressForSlot(xferImgSlot) + EEPROM_IMG_EACH);
    powerUp(INIT_EEPROM);
    while (amount) {
        uint16_t count = amount < sizeof(blockbuffer) ? amount : sizeof(blockbuffer);
        memcpy(blockbuffer, payload + offset, count);
        eepromWrite(address, blockbuffer, count);
        address += count; offset += count; amount -= count;
    }
    powerDown(INIT_EEPROM);
    return true;
}

/* Actual production function bodies, extracted at test-build time. */
#include "storage_production.inc"

void storage_reset(void)
{
    memset(nor, 255, sizeof(nor)); memset(&imageScratch, 0xa5, sizeof(imageScratch));
    memset(&curBlock, 0, sizeof(curBlock)); memset(&xferDataInfo, 0, sizeof(xferDataInfo));
    memset(curDispDataVer, 0, sizeof(curDispDataVer)); memset(&arg, 0, sizeof(arg));
    xferImgSlot = curImgSlot = source_slot = 255; nextImgSlot = 0; curHighSlotId = imageSize = 0;
    ack_count = draw_count = get_count = program_count = erase_count = busy_reads = unsafe_acks = 0;
    powered = selected = command = phase = busy = write_enabled = marker_commits = 0;
    replacement_erases = incoming_type = incoming_argument = 0; incoming_version = 0; fail_block = -1;
}
void storage_seed(unsigned slot, uint64_t version, unsigned argument, unsigned current)
{
    assert(slot < IMAGE_SLOTS);
    struct EepromImageHeader header = {0};
    header.version = version; header.validMarker = EEPROM_IMG_VALID;
    header.size = SCREEN_WIDTH / 8 * SCREEN_HEIGHT; header.dataType = DATATYPE_IMG_RAW_1BPP;
    header.id = ++curHighSlotId; header.argument = argument;
    memcpy(nor + getAddressForSlot(slot), &header, sizeof(header));
    memset(nor + getAddressForSlot(slot) + sizeof(header), 0xa6, header.size);
    if (current) { curImgSlot = slot; memcpy(curDispDataVer, &version, 8); }
}
int storage_process(const void *data, uint32_t length, uint64_t version,
                    unsigned type, unsigned argument, int fail_at_block)
{
    struct AvailDataInfo avail = {0};
    avail.dataVer = version; avail.dataSize = length;
    avail.dataType = type; avail.dataTypeArgument = argument;
    payload = data; payload_length = length; fail_block = fail_at_block; incoming_type = type;
    incoming_version = version; incoming_argument = argument;
    return processImageDataAvail(&avail);
}
unsigned storage_slot_count(void) { return IMAGE_SLOTS; }
unsigned storage_valid(unsigned slot)
{
    struct EepromImageHeader h;
    assert(slot < IMAGE_SLOTS); memcpy(&h, nor + getAddressForSlot(slot), sizeof(h));
    return h.validMarker == EEPROM_IMG_VALID;
}
void storage_select_current(unsigned slot)
{
    struct EepromImageHeader header;
    assert(slot < IMAGE_SLOTS);
    memcpy(&header, nor + getAddressForSlot(slot), sizeof(header));
    assert(header.validMarker == EEPROM_IMG_VALID);
    curImgSlot = slot; memcpy(curDispDataVer, &header.version, 8);
}
uint32_t storage_slot_field(unsigned slot, unsigned field)
{
    struct EepromImageHeader h;
    assert(slot < IMAGE_SLOTS); memcpy(&h, nor + getAddressForSlot(slot), sizeof(h));
    switch (field) { case 0:return h.size; case 1:return h.dataType; case 2:return h.argument;
        case 3:return (uint32_t)h.version; default:return h.validMarker; }
}
void storage_copy(unsigned slot, void *destination, uint32_t amount)
{
    assert(slot < IMAGE_SLOTS && amount <= EEPROM_IMG_EACH);
    memcpy(destination, nor + getAddressForSlot(slot), amount);
}
unsigned storage_stat(unsigned field)
{
    switch (field) { case 0:return ack_count;case 1:return draw_count;case 2:return get_count;
        case 3:return source_slot;case 4:return xferImgSlot;case 5:return curImgSlot;
        case 6:return busy_reads;case 7:return unsafe_acks;case 8:return program_count;
        case 9:return erase_count;case 10:return marker_commits;case 11:return replacement_erases;default:return busy; }
}
int storage_spi_completion(void)
{
    uint8_t original[300], readback[300];
    storage_reset(); powerUp(INIT_EEPROM);
    for (unsigned i=0;i<sizeof(original);++i) original[i]=(uint8_t)(i*73);
    uint32_t address = EEPROM_IMG_START + 250;
    eepromWrite(address, original, sizeof(original));
    unsigned incomplete = busy;
    eepromRead(address, readback, sizeof(readback));
    return !incomplete && !busy_reads && !memcmp(original, readback, sizeof(original))
        && nor[address-1] == 255 && nor[address+sizeof(original)] == 255
        && program_count == 3;
}
