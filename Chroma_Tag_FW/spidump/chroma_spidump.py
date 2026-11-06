#!/usr/bin/env python3
"""Back up a Chroma tag's CC1110 firmware and dump its SPI flash through the
CC1110, with an OpenEPaperLink Tag_Flasher.  See docs/spidump.md.

  1. Read the CC1110 flash twice, both reads must match (and --reference if
     given).  Saved as cc1110_backup.bin.  Nothing is erased before this.
  2. For each pass: erase, write the spidump image with the pass's SPI flash
     address in it, verify it, reset so it runs, wait, read the 32K twice.
  3. Join the passes into spi_dump.bin and check the factory data.
  4. --restore: write the backup back and verify it, also after an error.
     --flash FILE: write FILE instead.

usage: chroma_spidump.py -p COM12 [--mode none|first|full] [--size 128K]
                         [--restore | --flash FILE] [--reference FILE]
                         [--screen BOARD] [-o DIR]
"""
import argparse
import os
import sys
import time

import spidump_join
from spidump_join import DATA_ADR, PASS_LEN, TAG_ADR, BASE_OFS, TAG
from tagflasher import Fail, Flasher, FLASH_SIZE

HERE = os.path.dirname(os.path.abspath(__file__))


def pass_bases(mode, size):
    if mode == 'none':
        return []
    if mode == 'first':
        return [0]
    return list(range(0, size, PASS_LEN))


def dumper_image(base, board=None):
    """The spidump image, set to dump from SPI flash address base"""
    name = f'spidump_{board}.bin' if board else 'spidump.bin'
    path = os.path.join(HERE, name)
    if not os.path.exists(path):
        cmd = f'build.sh --screen {board}' if board else 'build.sh'
        raise Fail(f'no {name}, build it first: {cmd}')
    d = bytearray(open(path, 'rb').read())
    if d[TAG_ADR:TAG_ADR + BASE_OFS] != TAG:
        raise Fail(f'{name} has no tag, rebuild it')
    d[TAG_ADR + BASE_OFS:TAG_ADR + BASE_OFS + 3] = base.to_bytes(3, 'big')
    return bytes(d)


def run_pass(f, base, board, wait):
    """Dump one pass, returns the 32K read back"""
    dumper = dumper_image(base, board)
    print(f'--- pass 0x{base:05x}')
    f.write_verified(dumper, f'dumper for 0x{base:05x}')
    f.run()
    if wait > 5:
        print(f'  waiting {wait:.0f} s for the screen, watch the display')
    time.sleep(wait)
    rb = f.read_twice(f'pass 0x{base:05x} result')
    if rb[:len(dumper)] != dumper:
        raise Fail('the dumper code changed, the read back is bad')
    data = rb[DATA_ADR:]
    if data == b'\xff' * len(data) and base == 0:
        raise Fail('no data at 0x1000: the dumper did not run (try a longer --wait)')
    if data == bytes(len(data)):
        raise Fail('the SPI flash read as all 0x00: it is not answering')
    return rb


def run(f, args):
    """Returns True if everything worked"""
    def out(name):
        return os.path.join(args.outdir, name)

    os.makedirs(args.outdir, exist_ok=True)
    final = open(args.flash, 'rb').read() if args.flash else None
    size = spidump_join.parse_size(args.size)
    ok = False
    try:
        f.connect()

        # 1. firmware backup
        backup = f.read_twice('CC1110 firmware')
        open(out('cc1110_backup.bin'), 'wb').write(backup)
        print(f'backup OK, saved {out("cc1110_backup.bin")}')
        if args.reference:
            ref = open(args.reference, 'rb').read()
            if ref != backup:
                diff = sum(1 for x, y in zip(ref, backup) if x != y) + abs(len(ref) - len(backup))
                raise Fail(f'the backup differs from {args.reference} in {diff} bytes')
            print(f'backup is identical to {args.reference}')
        if args.restore:
            final = backup

        # 2. dumper passes
        readbacks = []
        for base in pass_bases(args.mode, size):
            screen = args.screen if base == 0 else None
            rb = run_pass(f, base, screen, args.screen_wait if screen else args.wait)
            name = out(f'readback_{base:05x}.bin')
            open(name, 'wb').write(rb)
            readbacks.append((name, rb))

        # 3. join
        ok = True
        if readbacks:
            print('--- joining')
            image, ok = spidump_join.join(readbacks, size)
            open(out('spi_dump.bin'), 'wb').write(image)
            print(f'wrote {out("spi_dump.bin")}')
    except Fail as e:
        print(f'\nFAILED: {e}')
        ok = False

    # 4. restore / flash, if we touched the flash or everything went well
    try:
        if final is not None and (f.erased or ok):
            f.write_verified(final, args.flash or 'original firmware')
        elif f.erased:
            print('NOTE: the CC1110 still holds the dumper, flash a firmware '
                  'before using the tag (the backup is cc1110_backup.bin)')
        f.run()
    except Fail as e:
        print(f'\nFAILED while restoring: {e}')
        ok = False
    return ok


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-p', '--port', required=True, help='flasher serial port, e.g. COM12')
    ap.add_argument('--mode', choices=('none', 'first', 'full'), default='first',
                    help='none: only back up the firmware, first: SPI flash 0x0000-0x6fff '
                         '(NVRAM + waveform), full: all of --size')
    ap.add_argument('--size', default='128K',
                    help='SPI flash size for --mode full: 128K (Chroma21, 29), 1M (Chroma60)')
    ap.add_argument('--restore', action='store_true',
                    help='write the backed up firmware back at the end')
    ap.add_argument('--flash', help='write this image at the end instead')
    ap.add_argument('--reference', help='stop before erasing if the backup differs from this file')
    ap.add_argument('--wait', type=float, default=3, help='seconds the dumper runs')
    ap.add_argument('--screen', metavar='BOARD',
                    help='show a screen after pass 0, a test of the display code '
                         '(build.sh --screen BOARD)')
    ap.add_argument('--screen-wait', type=float, default=45,
                    help='seconds to wait for the screen pass')
    ap.add_argument('-o', '--outdir', default='.', help='output directory')
    args = ap.parse_args(argv)
    if args.restore and args.flash:
        ap.error('use --restore or --flash, not both')
    return args


def main():
    import serial
    args = parse_args()
    ok = run(Flasher(serial.Serial(args.port, baudrate=115200, timeout=3)), args)
    print('done' if ok else 'finished with errors')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
