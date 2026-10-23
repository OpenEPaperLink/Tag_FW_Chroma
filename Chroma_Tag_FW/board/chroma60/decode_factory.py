#!/usr/bin/env python3
"""Decode the factory data in a Chroma60 SPI flash dump.

The LUTs in screen.c were taken from the 20C block this prints.

usage: decode_factory.py dump.bin [temperature block index]

Prints the live factory NVRAM settings (0x0000), the waveform script's
temperature table (0x2000) and the LUTs of one temperature block.
"""
import sys

MAGIC = bytes([0x56, 0x12, 0x09, 0x85])
SCRIPT_BASE = 0x202a     # temperature table offsets are relative to this
TEMP_TABLE = 0x20e1      # 9 byte entries: temp (int8), offset (u16 LE), 6 x 0
TEMP_ENTRIES = 14        # the last entry (-100C) only marks the end


def nvram(d):
    """Factory NVRAM: <type> <len incl. 2 byte header> <data>, 0xff = end.
    Records with type 0 have been invalidated, later records win."""
    live = {}
    for pg in range(10):
        a = pg * 0x1000
        if d[a:a + 4] != MAGIC:
            continue
        o = 4
        while o < 0x1000:
            t, l = d[a + o], d[a + o + 1]
            if t == 0xff:
                break
            if l < 2:
                print(f"  zero length record at 0x{a + o:05x}, the rest of the page is unreadable")
                break
            if t:
                live[t] = d[a + o + 2:a + o + l]
            o += l
    return live


def script(d, start, end):
    """Waveform script: 01 <len16 LE> <cmd> <len-1 data> = send command,
    04 <len16 LE> <cmd> = send <len-1> zeros, other 3 byte records = waits."""
    o, out = start, []
    while o < end:
        t = d[o]
        if t == 1:
            l = d[o + 1] | d[o + 2] << 8
            out.append((d[o + 3], d[o + 4:o + 3 + l]))
            o += 3 + l
        elif t == 4:
            out.append((d[o + 3], None))
            o += 4
        else:
            o += 3
    return out


def main():
    d = open(sys.argv[1], 'rb').read()
    blk = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    print("Factory NVRAM:")
    for t, v in sorted(nvram(d).items()):
        print(f"  type 0x{t:02x}: {v.hex(' ')}")

    print("\nWaveform script:", d[0x2006:0x201e].decode('ascii', 'replace'))
    ents = []
    for i in range(TEMP_ENTRIES):
        e = d[TEMP_TABLE + i * 9:TEMP_TABLE + i * 9 + 9]
        temp = e[0] - 256 if e[0] > 127 else e[0]
        ents.append((temp, SCRIPT_BASE + (e[1] | e[2] << 8)))
    for i in range(TEMP_ENTRIES - 1):
        cmds = script(d, ents[i][1], ents[i + 1][1])
        small = ' '.join(f"{c:02x}:{v.hex()}" for c, v in cmds if v is not None and len(v) < 8)
        print(f"  block {i:2d} >= {ents[i][0]:3d}C @0x{ents[i][1]:05x}  {small}")

    print(f"\nLUTs of block {blk} (>= {ents[blk][0]}C):")
    for c, v in script(d, ents[blk][1], ents[blk + 1][1]):
        if v is None or len(v) < 100:
            continue
        step = 11 if c == 0x20 else 13
        rows = [v[i:i + step] for i in range(0, len(v), step)]
        while rows and not any(rows[-1]):
            rows.pop()
        print(f"  cmd 0x{c:02x}:")
        for r in rows:
            print("    " + r.hex(' '))


if __name__ == '__main__':
    main()
