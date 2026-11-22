#!/usr/bin/env python3
"""Back up a Chroma tag's CC1110 firmware and dump its SPI flash through the
CC1110, with an OpenEPaperLink Tag_Flasher.  See docs/spidump.md.

  1. Read the CC1110 flash twice, both reads must match (and --reference if
     given).  Saved as cc1110_backup.bin.  Nothing is erased before this.
     A rerun into the same -o DIR adds _2, _3... to its file names, nothing
     is overwritten.  The dumper, or a partly written image left by an
     interrupted run, is never saved as a backup, and --restore refuses it.
  2. For each pass: erase, write the spidump image with the pass's SPI flash
     address in it, verify it, reset so it runs, wait, read the 32K twice.
  3. Join the passes into spi_dump.bin and check the factory data.
  4. --restore: write the backup back and verify it, also after an error.
     --flash FILE: write FILE instead.  Any error counts, Ctrl-C and serial
     or disk errors too: those are raised again after the restore.

usage: chroma_spidump.py -p COM12 [--mode none|first|full] [--size 128K]
                         [--restore | --flash FILE] [--reference FILE]
                         [--screen BOARD] [-o DIR]
"""
import argparse
import glob
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
    f.write_verified(dumper, f'the dumper for 0x{base:05x}')
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


def session_suffix(outdir):
    """'' for the first run in outdir, then '_2', '_3'...  Earlier output is
    never overwritten: it may be the only copy of a tag's firmware or factory data."""
    n = 1
    while True:
        sfx = f'_{n}' if n > 1 else ''
        pats = (f'cc1110_backup{sfx}.bin', f'spi_dump{sfx}.bin', f'readback_?????{sfx}.bin')
        if not any(glob.glob(os.path.join(glob.escape(outdir), p)) for p in pats):
            return sfx
        n += 1


def saved_backups(outdir):
    """The firmware backups already in outdir, first run first, as (path, data)"""
    paths = glob.glob(os.path.join(glob.escape(outdir), 'cc1110_backup*.bin'))
    return [(p, open(p, 'rb').read()) for p in sorted(paths, key=lambda p: (len(p), p))]


def used_len(d):
    """Length without the erased (0xff) tail"""
    return len(d.rstrip(b'\xff'))


def is_dumper(d):
    """A spidump image, whole or partly written"""
    if spidump_join.tag_base(d) is not None:
        return True
    n = used_len(d)
    if n == 0 or n > TAG_ADR + BASE_OFS:
        return False
    for p in glob.glob(os.path.join(glob.escape(HERE), 'spidump*.bin')):
        if open(p, 'rb').read()[:n] == d[:n]:
            return True
    return False


def not_firmware(current, images):
    """Why the flash read from the tag can't be its original firmware, or None.
    images: (path, data) of the images an earlier run may have been writing."""
    if is_dumper(current):
        return 'the CC1110 holds the spidump dumper'
    n = used_len(current)
    for path, b in images:
        if b != current and used_len(b) > n and b[:n] == current[:n]:
            return f'the CC1110 holds a partly written copy of {path}'
    return None


def check_reference(data, path, what):
    ref = open(path, 'rb').read()
    if ref != data:
        diff = sum(1 for x, y in zip(ref, data) if x != y) + abs(len(ref) - len(data))
        raise Fail(f'{what} differs from {path} in {diff} bytes')
    print(f'{what} is identical to {path}')


def save(path, data):
    with open(path, 'wb') as fp:
        fp.write(data)


def describe(e):
    if isinstance(e, Fail):
        return str(e)
    if isinstance(e, KeyboardInterrupt):
        return 'interrupted'
    return f'{type(e).__name__}: {e}'


def report(f, backup_path):
    """Say what the CC1110 holds, when this run left something else on it"""
    if f.erased:
        where = f' (the backup is {backup_path})' if backup_path else ''
        print(f'NOTE: the CC1110 holds {f.holds}, flash a firmware before using the tag{where}')


def finish(f, final, what, ok, error, backup_path):
    """Step 4: write final if the flash was touched or everything went well,
    then let the tag run.  error: what stopped steps 1-3, or None.
    Returns True if this worked.  Never raises over an earlier error."""
    try:
        if error is not None and not f.resync():
            print('the flasher does not answer, nothing more was written')
            report(f, backup_path)
            return False
        if final is not None and (f.erased or ok):
            if isinstance(error, KeyboardInterrupt):
                print(f'writing {what} back first, Ctrl-C again stops that')
            f.write_verified(final, what)
        else:
            report(f, backup_path)
        f.run()
        return True
    except BaseException as e:         # Fail, a serial or OS error, Ctrl-C
        print(f'\nFAILED while restoring: {describe(e)}')
        report(f, backup_path)
        if not isinstance(e, Fail) and (error is None or isinstance(error, Fail)):
            raise                      # nothing more important to show
        return False


def run(f, args):
    """Returns True if everything worked.  An exception other than Fail is
    raised again after the restore, so it isn't hidden."""
    os.makedirs(args.outdir, exist_ok=True)
    sfx = session_suffix(args.outdir)

    def out(name):
        base, ext = os.path.splitext(name)
        return os.path.join(args.outdir, base + sfx + ext)

    final = open(args.flash, 'rb').read() if args.flash else None
    size = spidump_join.parse_size(args.size)
    backup_path = None
    error = None
    ok = False
    try:
        if final is not None and is_dumper(final):
            raise Fail(f'{args.flash} is a spidump image, not a firmware')
        f.connect()

        # 1. firmware backup.  Checked before anything is saved or erased.
        current = f.read_twice('CC1110 firmware')
        backups = saved_backups(args.outdir)
        why = not_firmware(current, backups + ([(args.flash, final)] if final else []))
        if why:
            # An earlier run was interrupted, or ran without --restore
            print(f'{why}, not its firmware: no backup saved')
            if args.restore or args.reference:
                hint = (f', such as {backups[0][0]}' if backups else '')
                raise Fail(f'{why}, so it has no firmware to back up or check. '
                           f'To put the original back use --flash with its backup{hint}, '
                           'instead of --restore and --reference')
        else:
            if args.reference:
                check_reference(current, args.reference, 'the firmware')
            same = next((p for p, d in backups if d == current), None)
            if same:
                backup_path = same
                print(f'backup OK, identical to {same}, not saved again')
            else:
                backup_path = out('cc1110_backup.bin')
                save(backup_path, current)
                print(f'backup OK, saved {backup_path}')
            if args.restore:
                final = current

        # 2. dumper passes
        readbacks = []
        for base in pass_bases(args.mode, size):
            screen = args.screen if base == 0 else None
            rb = run_pass(f, base, screen, args.screen_wait if screen else args.wait)
            name = out(f'readback_{base:05x}.bin')
            save(name, rb)
            readbacks.append((name, rb))

        # 3. join
        ok = True
        if readbacks:
            print('--- joining')
            image, ok = spidump_join.join(readbacks, size)
            save(out('spi_dump.bin'), image)
            print(f'wrote {out("spi_dump.bin")}')
    except BaseException as e:         # Fail, a serial or OS error, Ctrl-C
        print(f'\nFAILED: {describe(e)}')
        error = e
        ok = False

    # 4. restore / flash, whatever stopped the steps above
    if not finish(f, final, args.flash or 'the original firmware', ok, error, backup_path):
        ok = False
    if error is not None and not isinstance(error, Fail):
        raise error
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
    try:
        ok = run(Flasher(serial.Serial(args.port, baudrate=115200, timeout=3)), args)
    except KeyboardInterrupt:
        print('interrupted')
        return 130
    print('done' if ok else 'finished with errors')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
