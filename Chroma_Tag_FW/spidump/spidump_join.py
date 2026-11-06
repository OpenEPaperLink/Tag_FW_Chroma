#!/usr/bin/env python3
"""Join CC1110 flash read backs made after running spidump passes.

usage: spidump_join.py [--size 128K] out.bin readback1.bin [readback2.bin ...]

Each read back must be the full 32K of CC1110 flash.  The tag in it says which
SPI flash address the 28K at 0x1000 came from.  Parts not covered stay 0xff
and are reported.  See docs/spidump.md.
"""
import argparse
import sys

DATA_ADR = 0x1000
PASS_LEN = 0x7000
TAG_ADR = 0x0ff0        # DUMP_TAG_ADR in spidump.h
OLD_TAG_ADR = 0x0f00    # the first dumper builds
TAG = b'SPIDUMP'
BASE_OFS = len(TAG)     # DUMP_TAG_BASE in spidump.h
SECTOR = 0x1000
NVRAM_MAGIC = bytes([0x56, 0x12, 0x09, 0x85])
WAVEFORM_ADR = 0x2000
WAVEFORM_MAGIC = bytes([0xce, 0xfa, 0xef, 0xbe])


def parse_size(s):
    """'128K', '1M' or a number"""
    s = s.strip().upper()
    mult = {'K': 1024, 'M': 1024 * 1024}.get(s[-1:], 1)
    return int(s[:-1] if mult > 1 else s, 0) * mult


def tag_base(d):
    """The SPI flash address in a read back, None without a tag"""
    for a in (TAG_ADR, OLD_TAG_ADR):
        t = d[a:a + BASE_OFS + 3]
        if t[:BASE_OFS] == TAG:
            return int.from_bytes(t[BASE_OFS:], 'big')
    return None


def check_contents(out, have):
    """Report the factory data in the dump.  False if the NVRAM isn't there"""
    found = False
    if not have[0]:
        return True
    for s in (0, SECTOR):
        if out[s:s + 4] == NVRAM_MAGIC:
            print(f'factory NVRAM found in sector {s // SECTOR}')
            found = True
    if not found:
        print('NO factory NVRAM found in sectors 0 and 1')
    if len(out) > WAVEFORM_ADR + 0x21 and out[WAVEFORM_ADR:WAVEFORM_ADR + 4] == WAVEFORM_MAGIC:
        fmt = out[WAVEFORM_ADR + 4:WAVEFORM_ADR + 6].hex(' ')
        name = bytes(out[WAVEFORM_ADR + 6:WAVEFORM_ADR + 0x21]).rstrip(b'\0')
        print(f'factory waveform: format {fmt} "{name.decode("ascii", "replace")}"')
    else:
        print('no factory waveform at 0x2000')
    return found


def join(readbacks, size):
    """readbacks: (name, data).  Returns the image and if it looks good"""
    out = bytearray(b'\xff' * size)
    have = bytearray(size)
    ok = True
    for name, d in readbacks:
        base = tag_base(d)
        if len(d) < DATA_ADR + PASS_LEN or base is None:
            print(f'{name}: not a spidump read back')
            ok = False
            continue
        part = d[DATA_ADR:DATA_ADR + PASS_LEN]
        if part == bytes(PASS_LEN):
            print(f'{name}: base 0x{base:05x} all 0x00, the SPI flash is not answering')
            ok = False
            continue
        if part == b'\xff' * PASS_LEN:
            # erased SPI flash, or the dumper didn't run: then pass 0 is too
            print(f'{name}: base 0x{base:05x} all 0xff (erased, or the dumper did not run)')
        n = min(PASS_LEN, size - base)
        if n <= 0:
            print(f'{name}: base 0x{base:05x} is outside of the {size} byte flash')
            ok = False
            continue
        out[base:base + n] = part[:n]
        have[base:base + n] = b'\x01' * n
        print(f'{name}: SPI flash 0x{base:05x}-0x{base + n - 1:05x}')

    missing = [a for a in range(0, size, SECTOR) if not have[a]]
    if missing:
        print(f'not covered: {len(missing)} of {size // SECTOR} 4K sectors, from 0x{missing[0]:05x}')
    return bytes(out), check_contents(out, have) and ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--size', default='128K', help='SPI flash size, e.g. 128K or 1M')
    ap.add_argument('out')
    ap.add_argument('readbacks', nargs='+')
    args = ap.parse_args()
    rb = [(n, open(n, 'rb').read()) for n in args.readbacks]
    out, ok = join(rb, parse_size(args.size))
    open(args.out, 'wb').write(out)
    print(f'wrote {args.out}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
