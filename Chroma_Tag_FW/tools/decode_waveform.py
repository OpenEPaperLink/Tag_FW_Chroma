#!/usr/bin/env python3
"""Decode the factory waveform in a Chroma SPI flash dump (any board).

usage: decode_waveform.py dump.bin [block] [--steps]

  block    print the LUTs of this block only (default: all blocks)
  --steps  print each block as a step table (Markdown), one column per LUT

Two formats are known, both starting at 0x2000 with the magic ce fa ef be:

  table  (version x.2: Chroma21, Chroma29, Chroma42 JC0)
         a parameter block, a temperature table, an index map and a pool of
         15-byte UC8154-style LUTs.  See docs/chroma21_waveform.md.
  script (version x.3: Chroma42 JH1 (UC8176), Chroma60, Chroma74 (UC8159))
         command scripts the stock firmware interprets.  See
         docs/chroma60_waveform.md.

Both share the header: 0x2025 payload length (u16), 0x2027 payload start
minus 0x2004, 0x2028 checksum (payload bytes + this byte = 0 mod 256).
"""
import struct
import sys

BASE = 0x2000
NV_MAGIC = bytes([0x56, 0x12, 0x09, 0x85])


def s8(v):
    return v - 256 if v > 127 else v


# ---------------------------------------------------------------- NVRAM ---
def nvram(d):
    """Factory NVRAM: <type> <len incl. 2 byte header> <data>, 0xff = end.
    Type 0 = invalidated record, later records win."""
    live, where = {}, None
    for pg in range(10):
        a = pg * 0x1000
        if d[a:a + 4] != NV_MAGIC:
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


# ------------------------------------------------------------ frame rates ---
# UC8154 PLL (R30h) = M << 3 | N, from the UC8154 datasheet
PLL_UC8154 = {1: [20, 10, 7, 5, 4, 3, 3], 2: [39, 20, 13, 10, 8, 7, 6],
              3: [59, 29, 20, 15, 12, 10, 8], 4: [78, 39, 26, 20, 16, 13, 11],
              5: [98, 50, 33, 24, 20, 16, 14], 6: [117, 59, 39, 29, 23, 20, 17],
              7: [137, 68, 46, 34, 27, 23, 20]}
# UC8159 PLL, rows N = 1..3 from the UC8159 datasheet; other rows ~200*M/(7*N)
PLL_UC8159 = {1: [29, 57, 86, 114, 143, 171, 200],
              2: [14, 29, 43, 59, 71, 86, 100],
              3: [10, 19, 29, 38, 48, 57, 67]}


def pll_hz(v, ctrl):
    m, n = (v >> 3) & 7, v & 7
    if not (m and n):
        return None
    if ctrl == 'UC8154':
        return PLL_UC8154[m][n - 1]
    if n in PLL_UC8159:
        return PLL_UC8159[n][m - 1]
    return round(200 * m / (7 * n))


# ------------------------------------------------------------------- LUTs ---
def lut_uc8154(l):
    """15 bytes: 5 groups of [phase A][phase B][repeat]; phase = level<<6 | frames"""
    rows = []
    for g in range(5):
        a, b, rep = l[g * 3:g * 3 + 3]
        if a == b == rep == 0:
            continue
        rows.append((rep, [a & 63, b & 63], [a >> 6, b >> 6]))
    return rows


def lut_uc8176(l):
    """6-byte groups: [levels, 4 x 2 bits MSB first][4 frame counts][repeat]"""
    rows = []
    for g in range(len(l) // 6):
        lv, f, rep = l[g * 6], list(l[g * 6 + 1:g * 6 + 5]), l[g * 6 + 5]
        if not any(l[g * 6:g * 6 + 6]):
            continue
        rows.append((rep, f, [(lv >> s) & 3 for s in (6, 4, 2, 0)]))
    return rows


def lut_uc8159(l, kind):
    """VCOM: 11-byte rows [repeat][2 level bytes][8 frames]
       colour: 13-byte rows [repeat][4 level bytes][8 frames]
       XON: 10-byte rows [repeat][XON bits][8 frames]"""
    step = {'vcom': 11, 'xon': 10}.get(kind, 13)
    rows = []
    for i in range(0, len(l) - step + 1, step):
        r = l[i:i + step]
        nlv = {'vcom': 2, 'xon': 1}.get(kind, 4)
        rep, lvb, fr = r[0], r[1:1 + nlv], list(r[1 + nlv:])
        if not any(fr):
            continue          # no frames: the row does nothing
        if kind == 'vcom':
            lv = [(lvb[j // 4] >> (6 - 2 * (j % 4))) & 3 for j in range(8)]
        elif kind == 'xon':
            lv = [(lvb[0] >> (7 - j)) & 1 for j in range(8)]
        else:
            lv = [(lvb[j // 2] >> (4 - 4 * (j % 2))) & 7 for j in range(8)]
        rows.append((rep, fr, lv))
    return rows


def trim(rows):
    """Drop trailing phases with 0 frames (they do nothing)"""
    out = []
    for rep, fr, lv in rows:
        n = len(fr)
        while n > 1 and fr[n - 1] == 0:
            n -= 1
        out.append((rep, fr[:n], lv[:n]))
    return out


def frames(rows):
    return sum(rep * sum(fr) for rep, fr, lv in rows)


# level symbols: 0 = 0 V / VCOM_DC, + = high, - = low, F = floating,
# L = low voltage (red/yellow) high, l = its negative, X/x = LVX, R = VDHR,
# XON: A = all gates on, . = normal gate scan
SYM = {
    'UC8154': '0+-F',
    'UC8176': '0+-R',
    'UC8176 vcom': '0+-F',
    'UC8159': '0+-LlXxF',
    'UC8159 vcom': '0+-F',
    'UC8159 xon': 'A.',     # XON bit 0 = all gates on, 1 = normal gate scan
}


def rows_str(rows, ctrl, kind=''):
    sym = SYM.get(f'{ctrl} {kind}', SYM[ctrl])
    return '  '.join(f"{rep}x[{' '.join(f'{sym[v]}:{f}' for f, v in zip(fr, lv))}]"
                     for rep, fr, lv in rows)


def step_table(luts, ctrl):
    """luts: list of (name, rows, kind).  Markdown table, one row per step."""
    n = max(len(r) for _, r, _ in luts)
    print('| Step | Repeat | Frames | ' + ' | '.join(nm for nm, _, _ in luts) + ' |')
    print('|---|---:|---|' + '---|' * len(luts))
    ref = luts[0][1]
    for i in range(n):
        cells = []
        for nm, rows, kind in luts:
            if i >= len(rows):
                cells.append('')
                continue
            sym = SYM.get(f'{ctrl} {kind}', SYM[ctrl]).replace('-', '−')
            cells.append('`' + ' '.join(sym[v] for v in rows[i][2]) + '`')
        rep, fr = (ref[i][0], ref[i][1]) if i < len(ref) else ('', [])
        print(f"| {i + 1} | {rep} | {'/'.join(map(str, fr))} | " + ' | '.join(cells) + ' |')


# ----------------------------------------------------------- table format ---
# LUT register each index map slot loads (stock firmware, see chroma21 doc).
SLOTS = ['0x20 VCOM1', '0x25 VCOM2', '0x22', '0x23 G1', '0x24 G2', '0x21', '0x26 R0', '0x27 R1']


def table_format(d, p, show, steps):
    prm = d[p:p + 0x12]
    print(f'parameters @0x{p:04x}: {prm.hex(" ")}')
    print(f'  VCM_DC (R82h) {prm[0]:02x} = -{prm[0] / 10:.1f} V   TCON {prm[1]:02x}   CDI {prm[2]:02x}'
          f'   BTST {prm[3:6].hex(" ")}   PWR 1-2 {prm[6:8].hex(" ")}')
    print(f'  0x{p + 8:04x} {prm[8]:02x} (x100 ms power-on delay)   0x{p + 9:04x} {prm[9]:02x}'
          f'   0x{p + 10:04x} {struct.unpack_from("<H", prm, 10)[0]} ms (before power off)'
          f'   rest {prm[12:].hex(" ")}')
    tabs = []
    for a in (0x202c, 0x202f, 0x2032):
        n, o = d[a], d[a + 1] | d[a + 2] << 8
        tabs.append((n, p + o))
    (n_t, tt), (n_m, mp), (n_l, pool) = tabs
    print(f'0x2029-0x202b {d[0x2029:0x202c].hex(" ")}')
    print(f'temperature table: {n_t} x 10 @0x{tt:04x}   index map: {n_m} x 12 @0x{mp:04x}'
          f'   LUT pool: {n_l} x 15 @0x{pool:04x}   end 0x{pool + n_l * 15:04x}')

    print('\n  #  >=C  blk  PLL1          PLL2          VDPS  VDNS  b6   b2 b8 b9')
    ents = []
    for i in range(n_t):
        e = d[tt + i * 10:tt + i * 10 + 10]
        ents.append(e)
        same = i and s8(e[0]) == s8(ents[i - 1][0])
        h1, h2 = pll_hz(e[1], 'UC8154'), pll_hz(e[7], 'UC8154')
        print(f' {i:2d} {s8(e[0]):4d}  {e[3]:3d}  {e[1]:02x} {h1:3d} Hz     {e[7]:02x} {h2:3d} Hz     '
              f'{e[4]:02x}    {e[5]:02x}    {e[6]:3d}  {e[2]:02x} {e[8]:02x} {e[9]:02x}'
              + ('   (extra pass)' if same else ''))

    print('\nindex map (slots: ' + ', '.join(SLOTS) + ')')
    maps = [d[mp + i * 12:mp + i * 12 + 12] for i in range(n_m)]
    for i, m in enumerate(maps):
        print(f'  block {i:2d}: {m[:8].hex(" ")} | {m[8:].hex(" ")}')

    for b, m in enumerate(maps):
        if show is not None and b != show:
            continue
        users = [i for i, e in enumerate(ents) if e[3] == b]
        print(f'\nblock {b} (temperature entries {users}):')
        luts = {}
        for s, idx in enumerate(m[:8]):
            if idx == 0xff:
                print(f'  {SLOTS[s]:11} not loaded')
                continue
            l = d[pool + idx * 15:pool + idx * 15 + 15]
            rows = lut_uc8154(l)
            luts[s] = rows
            print(f'  {SLOTS[s]:11} pool[{idx:2d}] {l.hex(" ")}   '
                  + (rows_str(rows, 'UC8154') if any(l) else '(all zero)'))
        bw = frames(luts.get(0, []))
        red = frames(luts.get(1, []))
        print(f'  frames: B/W stage {bw}, red stage {red}')
        if steps:
            for title, sl in (('B/W stage', [0, 5, 2, 4]), ('red stage', [1, 6, 7])):
                ls = [(SLOTS[s], luts[s], '') for s in sl if s in luts and luts[s]]
                if ls:
                    print(f'\n{title}:\n')
                    step_table(ls, 'UC8154')


# ---------------------------------------------------------- script format ---
def script(d, o, end=None):
    out = []
    while end is None or o < end:
        t, a = d[o], d[o + 1] | d[o + 2] << 8
        if t == 0:
            out.append((o, 0, None, None))
            break
        if t == 1:
            out.append((o, 1, d[o + 3], d[o + 4:o + 3 + a]))
            o += 3 + a
        elif t == 4:
            out.append((o, 4, d[o + 3], a))
            o += 4
        else:
            out.append((o, t, a, None))
            o += 3
    return out


REC = {2: 'delay {} ms', 3: 'wait for BUSY', 5: 'sleep until BUSY', 6: 'reset pulse',
       7: 'VDCS + TRES + image', 8: 'read R81h, save VCOM', 9: 'panel power {}'}


def print_script(d, o):
    for a, t, c, v in script(d, o):
        if t == 0:
            print(f'    {a:05x} end')
        elif t == 1:
            print(f'    {a:05x} R{c:02x}h {v.hex(" ") if len(v) < 24 else f"<{len(v)} bytes>"}')
        elif t == 4:
            print(f'    {a:05x} R{c:02x}h {v} zeros')
        else:
            print(f'    {a:05x} ' + REC.get(t, f'type {t:02x} arg {{}}').format(c))


def script_format(d, p, end, show, steps):
    offs = struct.unpack_from('<4I', d, 0x202a)
    names = ['init', 'temperature table', 'refresh', 'VCOM measure']
    for nm, o in zip(names, offs):
        print(f'  {nm:17} offset 0x{o:02x} -> 0x{p + o:04x}')
    print(f'  0x203a {d[0x203a]:02x} (x100 ms power-on delay)   0x203b {d[0x203b]:02x}'
          f'   0x203c-0x2047 {d[0x203c:0x2048].hex(" ")}')
    for nm, o in zip(names, offs):
        if nm != 'temperature table':
            print(f'\n  {nm} script:')
            print_script(d, p + o)

    tt = p + offs[1]
    ents = []
    for i in range(32):
        e = d[tt + i * 9:tt + i * 9 + 9]
        ents.append((s8(e[0]), p + struct.unpack_from('<I', e, 1)[0], e[5:]))
        if s8(e[0]) < -99:
            break
    print(f'\ntemperature table @0x{tt:04x}: {len(ents) - 1} blocks, end marker {ents[-1][0]}'
          f' -> 0x{ents[-1][1]:04x} (payload end 0x{end:04x})')

    blocks = []
    for i in range(len(ents) - 1):
        recs = [(c, v) for a, t, c, v in script(d, ents[i][1], ents[i + 1][1]) if t == 1]
        blocks.append(recs)
    lens = {c: len(v) for c, v in blocks[0]}
    ctrl = 'UC8176' if lens.get(0x20) == 44 else 'UC8159'
    print(f'controller (from the LUT sizes): {ctrl};  records per block: '
          + ', '.join(f'R{c:02x}h[{n}]' for c, n in lens.items()))

    print('\n  #  >=C   address  PWR             PLL       frames  time')
    for i, recs in enumerate(blocks):
        r = dict(recs)
        pll = r.get(0x30, b'\0')[0]
        hz = pll_hz(pll, ctrl)
        vc = r[0x20]
        rows = lut_uc8176(vc[:42]) if ctrl == 'UC8176' else lut_uc8159(vc, 'vcom')
        fr = frames(rows)
        print(f' {i:2d} {ents[i][0]:4d}  0x{ents[i][1]:04x}  {r.get(0x01, b"").hex(" "):15} '
              f'{pll:02x} {hz:3d} Hz  {fr:6d}  {fr / hz:5.1f} s   {ents[i][2].hex()}')

    for i, recs in enumerate(blocks):
        if show is not None and i != show:
            continue
        print(f'\nblock {i} (>= {ents[i][0]} C):')
        luts = []
        for c, v in recs:
            if len(v) < 40:
                print(f'  R{c:02x}h {v.hex(" ")}')
                continue
            if ctrl == 'UC8176':
                kind = 'vcom' if c == 0x20 else ''
                rows = trim(lut_uc8176(v[:42]))
                extra = f'   + {v[42:].hex(" ")}' if len(v) > 42 else ''
            else:
                kind = {0x20: 'vcom', 0x29: 'xon'}.get(c, '')
                rows = trim(lut_uc8159(v, kind))
                extra = ''
            luts.append((f'R{c:02x}h', rows, kind))
            print(f'  R{c:02x}h [{len(v)}] {rows_str(rows, ctrl, kind)}{extra}')
        if steps:
            print()
            step_table(luts, ctrl)


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    args =[a for a in sys.argv[1:] if not a.startswith('--')]
    steps = '--steps' in sys.argv
    d = open(args[0], 'rb').read()
    show = int(args[1]) if len(args) > 1 else None

    sector, live = nvram(d)
    print(f'Factory NVRAM (sector {sector}):')
    for t, v in sorted(live.items()):
        txt = ''.join(chr(c) if 32 <= c < 127 else '.' for c in v)
        print(f'  0x{t:02x}: {v.hex(" ")[:60]:60} |{txt[:24]}|')

    if d[BASE:BASE + 4] != bytes([0xce, 0xfa, 0xef, 0xbe]):
        sys.exit('\nno waveform magic at 0x2000')
    ver = d[0x2004:0x2006]
    name = d[0x2006:0x2025].split(b'\0')[0].decode('ascii', 'replace')
    ln, hdr, ck = d[0x2025] | d[0x2026] << 8, d[0x2027], d[0x2028]
    p = 0x2004 + hdr
    end = p + ln
    ok = (sum(d[p:end]) + ck) % 256 == 0
    print(f'\nwaveform format {ver[0]:02x} {ver[1]:02x}, name "{name}"')
    print(f'payload 0x{p:04x}-0x{end - 1:04x} ({ln} bytes), checksum {ck:02x} '
          f'{"OK" if ok else "BAD"}, byte after the end {d[end]:02x}')
    if ver[1] == 2:
        table_format(d, p, show, steps)
    elif ver[1] == 3:
        script_format(d, p, end, show, steps)
    else:
        sys.exit(f'unknown format {ver.hex(" ")}')


if __name__ == '__main__':
    main()
