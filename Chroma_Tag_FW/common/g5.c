/* Packed-row Group5 decoder: MSB-first pixels, 1 white and 0 black.
 * Single-instance and non-reentrant; state and rows overlay the transfer
 * buffer. No 32-bit bit reservoir, transition arrays, or heap allocation. */
#include <stdbool.h>
#include "g5.h"
#include "syncedproto.h"
#include "eeprom.h"
#include "screen.h"
#include "wdt.h"
#include "oepl-definitions.h"

#define g5p (imageScratch.g5.state.value)
#define work (imageScratch.g5.work.value)
#define stream (imageScratch.g5.transfer.value)
#define input (imageScratch.g5.input)
#define G5P_MAX_WIDTH G5_MAX_WIDTH

static int16_t g5p_read_byte(void)
{
    if (stream.pos == stream.count) {
        if (!stream.remaining) return -1;
        stream.count = stream.remaining >= G5_INPUT_BYTES ? G5_INPUT_BYTES : (uint8_t)stream.remaining;
        eepromRead(stream.source, input, stream.count);
        stream.source += stream.count;
        stream.remaining -= stream.count;
        stream.pos = 0;
    }
    return input[stream.pos++];
}



static uint8_t get_bit(void)
{
    uint8_t bit;
    if (!g5p.mask) {
        int16_t next = g5p_read_byte();
        if (next < 0) { g5p.error = -2; return 0; }
        g5p.byte = (uint8_t)next;
        g5p.mask = 0x80;
    }
    bit = (g5p.byte & g5p.mask) != 0;
    g5p.mask >>= 1;
    return bit;
}

static uint16_t get_bits(uint8_t count)
{
    uint16_t value = 0;
    while (count--) value = (value << 1) | get_bit();
    return value;
}

/* 0..+/-3 = vertical displacement; 4 horizontal; 5 pass. */
static int8_t get_mode(void)
{
    if (get_bit()) return 0;
    if (get_bit()) return get_bit() ? 1 : -1;
    if (get_bit()) return 4;
    if (get_bit()) return 5;
    if (get_bit()) return get_bit() ? 2 : -2;
    if (get_bit()) return get_bit() ? 3 : -3;
    g5p.error = -3;
    return 6;
}

/* Monotone reference-edge scan; width is also the virtual end sentinel. */
static uint16_t next_edge(uint16_t x, uint8_t white)
{
    uint8_t __xdata *p = g5p.ref + (x >> 3);
    uint8_t mask = 0x80 >> (x & 7);
    while (x < g5p.width) {
#ifndef G5P_NO_BYTE_SKIP
        if (mask == 0x80 && x + 8 <= g5p.width && *p == (white ? 0xff : 0)) {
            x += 8; ++p; continue;
        }
#endif
        if (((*p & mask) != 0) != white) break;
        ++x;
        mask >>= 1;
        if (!mask) { mask = 0x80; ++p; }
    }
    return x;
}

static void black_run_do(void)
{
    uint8_t __xdata *p;
    uint16_t bytes;

    if (work.br_start == work.br_end) return;
    p = g5p.cur + (work.br_start >> 3);
    work.br_tail = work.br_end & 7;
    work.br_left = (uint8_t)(0xffu << (8 - (work.br_start & 7)));
    work.br_right = (uint8_t)(0xffu >> work.br_tail);
    bytes = (work.br_end >> 3) - (work.br_start >> 3);
    if (!bytes) { *p &= work.br_left | work.br_right; return; }
    *p++ &= work.br_left;
    while (--bytes) *p++ = 0;
    /* Aligned ends need no sentinel byte and must not write past the row. */
    if (work.br_tail) *p &= work.br_right;
}


#define black_run(s,e) do { work.br_start=(s);work.br_end=(e);black_run_do(); } while (0)

int8_t g5p_init(uint16_t width, uint16_t height,
                uint8_t __xdata *row0, uint8_t __xdata *row1)
{
    uint8_t i;
    uint16_t n;
    g5p.error = -1;
    if (!width || width > G5P_MAX_WIDTH || !height || !row0 || !row1 || row0 == row1)
        return -1;
    g5p.width = width; g5p.height = height; g5p.line = 0;
    g5p.pitch = (width + 7) >> 3;
    g5p.longbits = 0;
    for (n = width; n; n >>= 1) ++g5p.longbits;
    g5p.cur = row0; g5p.ref = row1;
    for (i = 0; i < g5p.pitch; ++i) row1[i] = 0xff;
    g5p.mask = 0; g5p.byte = 0; g5p.error = 0;
    return 0;
}

int8_t g5p_decode_line(void)
{
    int16_t a0 = -1;
    uint16_t b1, end, first;
    uint8_t prefix, i;

    uint8_t __xdata *tmp;
    work.white = 1; work.refwhite = 1;
    if (g5p.error) return g5p.error;
    if (g5p.line >= g5p.height) return 1;
    for (i = 0; i < g5p.pitch; ++i) g5p.cur[i] = 0xff;
    work.e1 = next_edge(0, 1);
    work.e2 = next_edge(work.e1, 0);
    work.e3 = next_edge(work.e2, 1);
    while (a0 < (int16_t)g5p.width) {
        /* Keep the next three transitions strictly to the right of a0.
         * Caching all three avoids rescanning long reference runs. */
        while (work.e1 < g5p.width && (int16_t)work.e1 <= a0) {
            work.refwhite ^= 1;
            work.e1 = work.e2; work.e2 = work.e3;
            work.e3 = next_edge(work.e3, work.refwhite);
        }
        if (work.refwhite == work.white) { b1 = work.e1; work.b2 = work.e2; }
        else { b1 = work.e2; work.b2 = work.e3; }
        work.mode = get_mode();
        if (g5p.error) return g5p.error;
        work.start = a0 < 0 ? 0 : (uint16_t)a0;
        if (work.mode == 4) {
            prefix = (uint8_t)get_bits(2);
            first = get_bits((prefix & 2) ? g5p.longbits : 3);
            work.second = get_bits((prefix & 1) ? g5p.longbits : 3);
            if (g5p.error) return g5p.error;
            if (first > g5p.width - work.start || work.second > g5p.width - work.start - first
                || (!first && !work.second)) { g5p.error = -3; return -3; }
            end = work.start + first + work.second;
            if (!work.white) black_run(work.start, work.start + first);
            else black_run(work.start + first, end);
        } else {
            end = work.mode == 5 ? work.b2 : (uint16_t)((int16_t)b1 + work.mode);
            if (end > g5p.width || (int16_t)end <= a0) { g5p.error = -3; return -3; }
            if (!work.white) black_run(work.start, end);
            if (work.mode != 5) work.white ^= 1;
        }
        a0 = end;
    }
    tmp = g5p.ref; g5p.ref = g5p.cur; g5p.cur = tmp;
    ++g5p.line;
    return 0;
}

uint8_t __xdata *g5p_row(void) { return g5p.ref; }

/* Little-endian physical dimensions; both planes form one G5 stream. */
uint8_t g5Decode(void)
{
    uint8_t planes;
    int8_t result;
    if (stream.remaining < 7) return 0;
    eepromRead(stream.source, input, 6);
    stream.width = (uint16_t)input[1] | ((uint16_t)input[2] << 8);
    stream.height = (uint16_t)input[3] | ((uint16_t)input[4] << 8);
    planes = input[5];
    if (input[0] != 6 || stream.width != SCREEN_WIDTH || stream.height != SCREEN_HEIGHT
        || (planes != 1 && planes != 2)) return 0;
    stream.rawSize = (uint32_t)(SCREEN_WIDTH / 8) * SCREEN_HEIGHT;
    if (planes == 2) { stream.rawSize *= 2; stream.height *= 2; }
    if (stream.rawSize > EEPROM_IMG_EACH - sizeof(struct EepromImageHeader)) return 0;
    stream.source += 6;
    stream.remaining -= 6;
    stream.pos = stream.count = 0;
    if (g5p_init(stream.width, stream.height, imageScratch.g5.rows[0], imageScratch.g5.rows[1])) return 0;
    for (;;) {
        wdtPet();
        result = g5p_decode_line();
        if (result == 1) return planes == 1 ? DATATYPE_IMG_RAW_1BPP : DATATYPE_IMG_RAW_2BPP;
        if (result < 0) return 0;
        eepromWrite(stream.destination, g5p_row(), g5p.pitch);
        stream.destination += g5p.pitch;
    }
}
