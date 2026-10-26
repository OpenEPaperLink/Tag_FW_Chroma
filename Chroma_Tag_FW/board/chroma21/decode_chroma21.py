#!/usr/bin/env python3
"""Decode the factory waveform tables (format 04 02) in a Chroma21 SPI flash dump.

usage: decode_chroma21.py dump.bin [block]

Layout found in JG10014896 (UC8154, "D18Y00010WY005_UC8154 21E4"):

  0x2000  4      magic ce fa ef be
  0x2004  2      format version 04 02 (the Chroma60 script is 03 03)
  0x2006  27     name, NUL padded
  0x2025  4      probably a checksum (same place as in the Chroma60 header)
  0x2029  30     header parameters, see HDR below
  0x2047  13x10  temperature table, coldest last
  0x20c9  13x12  LUT index map: 8 pool indexes for LUT registers 0x20-0x27
                 (0xff = register not loaded) + 4 unused bytes
  0x2165  91x15  LUT pool: UC8154 LUTs, 5 groups of [phase A, phase B, repeat];
                 phase byte = level << 6 | frames (00 VCOM_DC, 01 VDH,
                 10 VDL, 11 floating)
"""
import sys

BASE = 0x2000
# LUT register each map slot loads (inferred, see the .md). By timing the
# slots split into the two UC8154 stages (B/W: slots 0,2,4,5; red: 1,6,7).
# W vs B follows from drive polarity: the Chroma29 driver's working LUTs
# start black with VSH and end it with VSL, white the other way round.
SLOT_GUESS = ['0x20 VCOM1', '0x25 VCOM2', '0x22 B', '0x23 G1 (unused)',
              '0x24 G2', '0x21 W', '0x26 R0', '0x27 R1']

# UC8154 PLL (R30h) = M << 3 | N, frame rate in Hz, from the datasheet
PLL_HZ = {1: [20, 10, 7, 5, 4, 3, 3], 2: [39, 20, 13, 10, 8, 7, 6],
          3: [59, 29, 20, 15, 12, 10, 8], 4: [78, 39, 26, 20, 16, 13, 11],
          5: [98, 50, 33, 24, 20, 16, 14], 6: [117, 59, 39, 29, 23, 20, 17],
          7: [137, 68, 46, 34, 27, 23, 20]}


def pll(v):
    m, n = (v >> 3) & 7, v & 7
    return f'{PLL_HZ[m][n - 1]}Hz' if m and n else '?'


def vdps(v):
    """R01h byte 3, VDPS_LV (red VSH): 2.4 V + 0.2 V per step"""
    return f'{2.4 + 0.2 * v:.1f}V' if v < 0x1d else '4.0V'


def u16(d, a):
    return d[a] | d[a + 1] << 8


def lut_str(l):
    groups = []
    for g in range(5):
        a, b, rep = l[g * 3:g * 3 + 3]
        if a == b == rep == 0:
            continue
        groups.append(f'{a >> 6}:{a & 63:2d}/{b >> 6}:{b & 63:2d} x{rep}')
    return '  '.join(groups)


def nvram(d):
    live, where = {}, None
    for pg in range(10):
        a = pg * 0x1000
        if d[a:a + 4] != bytes([0x56, 0x12, 0x09, 0x85]):
            continue
        where = pg
        o = 4
        while o < 0x1000:
            t, l = d[a + o], d[a + o + 1]
            if t == 0xff or l < 2:
                break
            if t:
                live[t] = d[a + o + 2:a + o + l]
            o += l
    return where, live


def main():
    d = open(sys.argv[1], 'rb').read()
    show = int(sys.argv[2]) if len(sys.argv) > 2 else None

    sector, live = nvram(d)
    print(f'Factory NVRAM (sector {sector}):')
    for t, v in sorted(live.items()):
        txt = ''.join(chr(c) if 32 <= c < 127 else '.' for c in v)
        print(f'  0x{t:02x}: {v.hex(" "):48} |{txt}|')

    h = d[BASE:BASE + 0x47]
    print(f'\nmagic {h[0:4].hex(" ")}  version {h[4]:02x} {h[5]:02x}  '
          f'name "{h[6:0x21].rstrip(bytes(1)).decode("ascii", "replace")}"')
    print(f'0x2021: {h[0x21:0x25].hex(" ")}   0x2025 (checksum?): {h[0x25:0x29].hex(" ")}')
    print(f'header parameters 0x2029-0x2046: {h[0x29:0x47].hex(" ")}')

    n_temp, n_lut = d[0x202c], d[0x2032]
    tt, mp, pool = 0x2047, 0x2047 + n_temp * 10, 0x2047 + n_temp * 22
    end = pool + n_lut * 15
    print(f'{n_temp} temperature entries @0x{tt:04x}, map @0x{mp:04x}, '
          f'{n_lut} LUTs @0x{pool:04x}, end 0x{end:04x} '
          f'(next byte 0x{d[end]:02x})')

    print('\ntemperature table:   temp  b1   b2  blk  b4   b5   b6   b7   b8 b9   map'
          '                                 b1 PLL  b7 PLL  b4 as VDPS')
    for i in range(n_temp):
        e = d[tt + i * 10:tt + i * 10 + 10]
        t = e[0] - 256 if e[0] > 127 else e[0]
        m = d[mp + i * 12:mp + i * 12 + 12]
        print(f'  entry {i:2d}       >={t:4d}  ' + '  '.join(f'{x:02x}' for x in e[1:]) +
              f'   {m[:8].hex(" ")} | {m[8:].hex(" ")}'
              f'   {pll(e[1]):7} {pll(e[7]):7} {vdps(e[4])}')

    for i in range(n_temp):
        if show is not None and i != show:
            continue
        e = d[tt + i * 10]
        t = e - 256 if e > 127 else e
        m = d[mp + i * 12:mp + i * 12 + 8]
        print(f'\nblock {i} (>= {t} C):')
        for r, idx in enumerate(m):
            if idx == 0xff:
                print(f'  slot {r} {SLOT_GUESS[r]:18} -- not loaded')
                continue
            l = d[pool + idx * 15:pool + idx * 15 + 15]
            print(f'  slot {r} {SLOT_GUESS[r]:18} pool[{idx:2d}] {l.hex(" ")}   {lut_str(l)}')


if __name__ == '__main__':
    main()
