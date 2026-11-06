#!/usr/bin/env python3
"""Run board/chroma21/uc8154.c on the host against crafted waveform tables and
check the commands it sends against the stock firmware's sequence, see
docs/chroma21_waveform.md.  Needs gcc; run it on Linux, macOS or WSL.

usage: run_tests.py [-v]
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.normpath(os.path.join(HERE, '..', '..', 'board', 'chroma21'))

N_TEMP = 3
PARAMS = bytes([0x0a, 0x22, 0x17, 0x07, 0x08, 0x0f, 0x03, 0x00, 0x32, 0x02,
                0xd0, 0x07]) + bytes(6)
# threshold, PLL, 0, block, VDPS, VDNS, red delay, PLL2, 0, 0
ENTRIES = [
    bytes([25, 0x39, 0, 0, 0x02, 0x80, 0x10, 0x29, 0, 0]),
    bytes([12, 0x21, 0, 1, 0x05, 0x81, 0x14, 0x22, 0, 0]),
    bytes([0xfb, 0x11, 0, 2, 0x10, 0x85, 0x27, 0x13, 0, 0]),     # -5 C
]
LUTS_PER_BLOCK = 7
SLOTS = (0, 1, 2, 4, 5, 6, 7)     # slot 3 is 0xff
FALLBACK_C1 = '0b 0a 01 02 03 1e 08 08 3c 3f 3f 06 05 02 1e'


def lut(block, slot):
    """A recognizable LUT for a block and map slot"""
    return bytes([0xa0 + block, slot]) + bytes(range(13))


def waveform(magic=True, n_temp=N_TEMP, params=PARAMS):
    d = bytearray(b'\xff' * 0x47)
    if magic:
        d[:6] = bytes([0xce, 0xfa, 0xef, 0xbe, 0x04, 0x02])
    d[0x2c] = n_temp
    d[0x35:0x47] = params
    for e in ENTRIES:
        d += e
    pool = bytearray()
    for b in range(N_TEMP):
        row = bytearray(b'\xff' * 8 + bytes(4))
        for i, s in enumerate(SLOTS):
            row[s] = b * LUTS_PER_BLOCK + i
            pool += lut(b, s)
        d += row
    return bytes(d + pool)


def h(b):
    return b.hex(' ')


def expected(block, e):
    """The stock sequence, delays included"""
    pwr = f'07 00 {e[4] & 0x1f:02x} {e[5] & 0x1f:02x}'
    null = h(bytes(15))
    seq = ['delay 20', 'delay 1', 'delay 10',
           f'cmd 01 {pwr}', 'cmd 06 07 08 0f', 'cmd 04',
           'cmd 00 9f', 'cmd 61 68 00 d4', f'cmd 30 {e[1]:02x}', 'cmd 50 17',
           'cmd 82 0a', 'cmd 60 22']
    for reg, slot in ((0x20, 0), (0x21, 5), (0x22, 2), (0x24, 4), (0x25, 1),
                      (0x26, 6), (0x27, 7)):
        seq.append(f'cmd {reg:02x} {h(lut(block, slot))}')
    seq += ['powered nEnable 0',
            'cmd 10 e0 00',            # black 11, yellow 10, white 00
            'cmd 13 bf',
            'cmd 12', 'delay 1']
    seq += [f'cmd {r:02x} {null}' for r in (0x20, 0x21, 0x22, 0x24)]
    seq += ['cmd 00 8f', 'cmd 61 68 00 d4', 'cmd 01 02 00 00 00',
            f'cmd 30 {e[7]:02x}', 'cmd 12', 'delay 1', f'delay {e[6]}',
            f'cmd 01 {pwr}']
    seq += ['cmd 82 00', 'cmd 01 02 00 00 00', 'offdelay 2000',
            'cmd 50 37', 'cmd 02', 'delay 20',
            'off nEnable 1 P0 00 P1 00 P1SEL 00']
    return seq


class Harness:
    def __init__(self, tmp, name, flags=()):
        d = os.path.join(tmp, name)
        os.makedirs(d)
        # uc8154.c must find the stubs, not board/chroma21/board.h next to it
        for f in ('uc8154.c', 'uc8154.h'):
            shutil.copy(os.path.join(BOARD, f), d)
        for f in ('board.h', 'screen.h'):
            shutil.copy(os.path.join(HERE, 'stub', f), d)
        with open(os.path.join(d, 'board.h')) as f:
            s = f.read().replace('../../../board/boardPins.h',
                                 os.path.join(BOARD, '..', 'boardPins.h').replace('\\', '/'))
        with open(os.path.join(d, 'board.h'), 'w') as f:
            f.write(s)
        self.exe = os.path.join(d, 'harness')
        self.wave = os.path.join(d, 'wave.bin')
        subprocess.run(['gcc', '-I', d, '-Wall', '-o', self.exe, '-fsanitize=address,undefined',
                        os.path.join(HERE, 'harness.c'), os.path.join(d, 'uc8154.c')] + list(flags),
                       check=True)

    def run(self, wave, temperature):
        with open(self.wave, 'wb') as f:
            f.write(wave)
        r = subprocess.run([self.exe, self.wave, str(temperature)], text=True,
                           stdout=subprocess.PIPE, check=True)
        return [l.strip() for l in r.stdout.splitlines() if l.strip()]


def main():
    verbose = '-v' in sys.argv
    fails = 0

    def check(name, cond, detail=''):
        nonlocal fails
        print(f'{"ok  " if cond else "FAIL"} {name}')
        if not cond:
            fails += 1
            if detail:
                print(detail)

    def seq_check(name, got, want):
        bad = next((i for i, (g, w) in enumerate(zip(got, want)) if g != w),
                   None if len(got) == len(want) else min(len(got), len(want)))
        d = '' if bad is None else (f'  line {bad}: got  {got[bad:bad + 3]}\n'
                                    f'  line {bad}: want {want[bad:bad + 3]}')
        check(name, bad is None, d)
        if verbose:
            print('\n'.join(got))

    with tempfile.TemporaryDirectory() as tmp:
        fw = Harness(tmp, 'fw')
        dump = Harness(tmp, 'dump', ['-DUC8154_NO_DEFAULT_WAVEFORM'])

        for temp, block in ((40, 0), (25, 0), (24, 1), (12, 1), (11, 2), (-5, 2), (-30, 2)):
            seq_check(f'{temp} C uses block {block}', fw.run(waveform(), temp),
                      expected(block, ENTRIES[block]))

        seq_check('spidump build sends the same', dump.run(waveform(), 25),
                  expected(0, ENTRIES[0]))

        for name, wave in (('bad magic', waveform(magic=False)),
                           ('no temperature entries', waveform(n_temp=0)),
                           ('too many temperature entries', waveform(n_temp=32))):
            got = fw.run(wave, 25)
            check(f'{name}: default waveform', f'cmd 20 {FALLBACK_C1}' in got
                  and 'cmd 30 39' in got and got[-1].startswith('off nEnable 1'), '\n'.join(got))
            got = dump.run(wave, 25)
            check(f'{name}: spidump leaves the EPD off', got == ['no waveform nEnable 1'],
                  '\n'.join(got))

        params = bytearray(PARAMS)
        params[10:12] = b'\xff\xff'
        check('power off delay is limited', 'offdelay 4000' in
              fw.run(waveform(params=bytes(params)), 25))
        params[10:12] = bytes(2)
        check('no power off delay', 'offdelay 0' in fw.run(waveform(params=bytes(params)), 25))

    print('all passed' if not fails else f'{fails} FAILED')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
