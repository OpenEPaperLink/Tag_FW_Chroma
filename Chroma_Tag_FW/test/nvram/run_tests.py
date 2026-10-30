#!/usr/bin/env python3
"""Run the factory NVRAM boot code against crafted SPI flash images.

usage: run_tests.py [--ref GIT_REF ...] [-v]

Without --ref the working tree is tested. Each --ref adds a column for that
revision (e.g. --ref upstream/master) so behaviour can be compared. Needs gcc;
run it on Linux, macOS or WSL.

Each case states the desired behaviour. Cases marked xfail are known not to
meet it yet; they report XPASS once a fix lands, so update the mark then.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.dirname(FW)
SOC = 'Chroma_Tag_FW/soc/cc111x/soc.c'
BOARD = 'Chroma_Tag_FW/board/boardChroma.c'

MAGIC = bytes([0x56, 0x12, 0x09, 0x85])
SN = bytes([0x00, 0x00, 0x63, 0x03])          # JD00006303
FLASH_SIZE = 0x100000


def rec(t, data):
    return bytes([t, len(data) + 2]) + bytes(data)


def image(records, magic=True):
    d = bytearray(b'\xff' * FLASH_SIZE)
    body = (MAGIC if magic else b'') + b''.join(records)
    d[:len(body)] = body
    return bytes(d)


def image2(sector1, sector0=None):
    """NVRAM in sector 1, as the stock FW leaves it after moving it there;
    sector 0 is blank unless given"""
    d = bytearray(b'\xff' * FLASH_SIZE)
    if sector0 is not None:
        body = MAGIC + b''.join(sector0)
        d[:len(body)] = body
    body = MAGIC + b''.join(sector1)
    d[0x1000:0x1000 + len(body)] = body
    return bytes(d)


def r_sn(sn=SN):
    return rec(0x01, b'JD' + sn)


R_VCOM = rec(0x23, [0x20, 0, 0, 0, 0])
R_INT = rec(0x09, [0xe9, 0x02])
R_SLOPE = rec(0x12, [0x5a, 0x0a])
R_PANEL = rec(0x14, [0x58, 0x02, 0xc0, 0x01, 0, 0])     # 600x448, unused by FW
R_DEAD = bytes([0x00, 0x05, 1, 2, 3])                    # invalidated record
R_ZEROLEN = bytes([0x00, 0x00, 0x18, 0x00, 0x00, 0x00])  # type and len zeroed


def std(*extra, sn=SN, vcom=True):
    recs = [R_DEAD, r_sn(sn)] + ([R_VCOM] if vcom else []) + [R_INT, R_SLOPE, R_PANEL]
    return recs + list(extra)


MAC_KEPT = '4467090300006303'
MAC_ZERO = '4467090300000000'
OTHER_SN = b'\x10\x00\x89\x56'


def kept(r, img):
    return r['outcome'] == 'ok' and r['mac'] == MAC_KEPT and r['erases'] == 0


# name, config, image, fail_until_ms, patched SN, desired check, xfail, description
CASES = [
    ('1 all records ok', 'vcom', image(std()), 0, '-', kept, False,
     'nothing written, values from flash'),
    ('1b later record wins', 'vcom',
     image([bytes([0x00, 8]) + b'JD\x11\x11\x11\x11'] + std()),
     0, '-', kept, False, 'an invalidated SN before the live one is skipped'),
    ('2 flash not ready 50ms', 'vcom', image(std()), 50, '-', kept, False,
     'a retry succeeds, nothing written'),
    ('3 flash not ready 1s', 'vcom', image(std()), 1000, '-',
     lambda r, i: r['erases'] == 0 and i[:0x1000] == image(std())[:0x1000], False,
     'no write while the flash does not answer, retried next boot'),
    ('4 blank sector', 'vcom', image([], magic=False), 0, '-',
     lambda r, i: r['mac'] == MAC_ZERO and r['erases'] == 1, False,
     'defaults written so the tag can reach the AP (MAC ...00000000)'),
    ('5 VCOM missing', 'vcom', image([R_DEAD, r_sn(), R_INT, R_SLOPE]), 0, '-',
     lambda r, i: kept(r, i) and r['vcom'] == 0x30, False,
     'SN kept, default VCOM in RAM, no write'),
    ('6 ADC intercept missing', 'vcom', image([R_DEAD, r_sn(), R_VCOM, R_SLOPE]), 0, '-',
     lambda r, i: kept(r, i) and r['intercept'] == 0x2ec and r['vcom'] == 0x20, False,
     'SN kept, default intercept in RAM, no write'),
    ('6b ADC slope missing (no VCOM board)', 'novcom', image([r_sn(), R_INT]), 0, '-',
     lambda r, i: kept(r, i) and r['slope'] == 0xa5a and r['intercept'] == 0x2e9, False,
     'SN kept, default slope in RAM, no write'),
    ('7 patched SN, flash SN zero', 'vcom', image(std(sn=bytes(4))), 0, '00006303',
     lambda r, i: r['mac'] == MAC_KEPT, False, 'patch_sn recovery writes the SN'),
    ('8 patched SN, flash SN matches', 'vcom', image(std()), 0, '00006303', kept, False,
     'nothing written'),
    ('9 patched SN, other valid SN', 'vcom', image(std(sn=OTHER_SN)), 0,
     '00006303', lambda r, i: r['mac'] == MAC_KEPT, False,
     'current design: the patched SN replaces it (maintainers to decide)'),
    ('10 zero-len record before SN', 'vcom', image([R_ZEROLEN, r_sn(), R_VCOM, R_INT, R_SLOPE]),
     0, '-', lambda r, i: r['outcome'] == 'ok', False, 'must not hang'),
    ('11 zero-len record after needed', 'vcom', image(std(R_ZEROLEN, R_PANEL)), 0, '-',
     kept, False, 'must not hang, nothing written'),
    ('12 patched SN, other SN in sector 1', 'vcom', image2(std(sn=OTHER_SN)), 0, '00006303',
     lambda r, i: r['mac'] == MAC_KEPT and i[0x1000:0x1004] != MAGIC, False,
     'patched SN takes effect, sector 1 no longer overrides it'),
    ('13 patched SN, zero SN in sector 1', 'vcom', image2(std(sn=bytes(4))), 0, '00006303',
     lambda r, i: r['mac'] == MAC_KEPT and i[0x1000:0x1004] != MAGIC, False,
     'patch_sn recovery works when the NVRAM is in sector 1'),
    ('14 NVRAM in sector 1', 'vcom', image2(std()), 0, '-', kept, False,
     'read from sector 1, nothing written'),
    ('15 defaults in sector 0, SN in sector 1', 'vcom',
     image2(std(), sector0=[r_sn(bytes(4)), R_VCOM, R_INT, R_SLOPE]), 0, '-', kept, False,
     'later sector wins, real SN kept, nothing written'),
]


def source(ref, path):
    if ref is None:
        return open(os.path.join(REPO, path)).read()
    p = subprocess.run(['git', '-C', REPO, 'show', f'{ref}:{path}'],
                       capture_output=True, text=True)
    if p.returncode:
        sys.exit(f"run_tests.py: can't read {path} at {ref}: {p.stderr.strip()}")
    return p.stdout


def build(ref, tmp):
    tag = (ref or 'worktree').replace('/', '_')
    d = os.path.join(tmp, tag)
    os.makedirs(d)
    for path in (SOC, BOARD):
        open(os.path.join(d, os.path.basename(path)), 'w').write(source(ref, path))
    with open(os.path.join(d, 'extracted.c'), 'w') as f:
        subprocess.run([sys.executable, os.path.join(HERE, 'extract.py'),
                        os.path.join(d, 'soc.c'), os.path.join(d, 'boardChroma.c')],
                       stdout=f, check=True)
    bins = {}
    for cfg in ('vcom', 'novcom'):
        exe = os.path.join(d, f'harness_{cfg}')
        cmd = ['gcc', '-std=gnu99', '-O1', '-w', f'-I{d}', '-o', exe,
               os.path.join(HERE, 'harness.c')]
        if cfg == 'vcom':
            cmd.insert(1, '-DSCREEN_EXPECTS_VCOM')
        subprocess.run(cmd, check=True)
        bins[cfg] = exe
    return bins


def run(exe, img, fail_ms, sn, tmp, verbose):
    fin, fout = os.path.join(tmp, 'in.bin'), os.path.join(tmp, 'out.bin')
    open(fin, 'wb').write(img)
    p = subprocess.run([exe, fin, fout, str(fail_ms), sn], capture_output=True, text=True)
    if verbose:
        print(p.stdout)
    line = [l for l in p.stdout.splitlines() if l.startswith('@@RESULT ')][-1]
    return json.loads(line[9:]), open(fout, 'rb').read()


def summary(r):
    if r['outcome'] == 'hang':
        return 'HANG'
    wiped = 'WIPED' if r['erases'] else 'kept'
    return f"{wiped} mac=..{r['mac'][-8:]}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', action='append', default=[])
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    refs = a.ref + [None]
    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        builds = {ref: build(ref, tmp) for ref in refs}
        cols = [r or 'worktree' for r in refs]
        print(f"{'case':38}" + ''.join(f'{c:28}' for c in cols) + 'result')
        for name, cfg, img, fail_ms, sn, check, xfail, desc in CASES:
            row, status = f'{name:38}', ''
            for ref in refs:
                r, out = run(builds[ref][cfg], img, fail_ms, sn, tmp, a.v)
                row += f'{summary(r):28}'
                if ref is None:
                    ok = check(r, out)
                    status = ('XPASS' if ok else 'xfail') if xfail else ('pass' if ok else 'FAIL')
                    fails += status == 'FAIL'
            print(row + f'{status:6} {desc}')
    print(f"\n{'FAILED' if fails else 'OK'}: {fails} unexpected failure(s)")
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
