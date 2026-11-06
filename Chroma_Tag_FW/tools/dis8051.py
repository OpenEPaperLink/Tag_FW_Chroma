#!/usr/bin/env python3
"""Minimal linear 8051 disassembler with CC1110 SFR names.
usage: dis8051.py file.bin [start] [end]"""
import sys

SFR = {
    0x80: 'P0', 0x81: 'SP', 0x82: 'DPL0', 0x83: 'DPH0', 0x84: 'DPL1', 0x85: 'DPH1',
    0x86: 'U0CSR', 0x87: 'PCON', 0x88: 'TCON', 0x89: 'P0IFG', 0x8A: 'P1IFG', 0x8B: 'P2IFG',
    0x8C: 'PICTL', 0x8D: 'P1IEN', 0x8F: 'P0INP', 0x90: 'P1', 0x91: 'RFIM', 0x92: 'DPS',
    0x93: 'MPAGE', 0x95: 'ENDIAN', 0x98: 'S0CON', 0x9A: 'IEN2', 0x9B: 'S1CON',
    0x9C: 'T2CT', 0x9D: 'T2PR', 0x9E: 'T2CTL', 0xA0: 'P2', 0xA1: 'WORIRQ', 0xA2: 'WORCTRL',
    0xA3: 'WOREVT0', 0xA4: 'WOREVT1', 0xA5: 'WORTIME0', 0xA6: 'WORTIME1', 0xA8: 'IEN0',
    0xA9: 'IP0', 0xAB: 'FWT', 0xAC: 'FADDRL', 0xAD: 'FADDRH', 0xAE: 'FCTL', 0xAF: 'FWDATA',
    0xB1: 'ENCDI', 0xB2: 'ENCDO', 0xB3: 'ENCCS', 0xB4: 'ADCCON1', 0xB5: 'ADCCON2',
    0xB6: 'ADCCON3', 0xB8: 'IEN1', 0xB9: 'IP1', 0xBA: 'ADCL', 0xBB: 'ADCH', 0xBC: 'RNDL',
    0xBD: 'RNDH', 0xBE: 'SLEEP', 0xC0: 'IRCON', 0xC1: 'U0DBUF', 0xC2: 'U0BAUD',
    0xC4: 'U0UCR', 0xC5: 'U0GCR', 0xC6: 'CLKCON', 0xC7: 'MEMCTR', 0xC9: 'WDCTL',
    0xCA: 'T3CNT', 0xCB: 'T3CTL', 0xCC: 'T3CCTL0', 0xCD: 'T3CC0', 0xCE: 'T3CCTL1',
    0xCF: 'T3CC1', 0xD0: 'PSW', 0xD1: 'DMAIRQ', 0xD2: 'DMA1CFGL', 0xD3: 'DMA1CFGH',
    0xD4: 'DMA0CFGL', 0xD5: 'DMA0CFGH', 0xD6: 'DMAARM', 0xD7: 'DMAREQ', 0xD8: 'TIMIF',
    0xD9: 'RFD', 0xDA: 'T1CC0L', 0xDB: 'T1CC0H', 0xDC: 'T1CC1L', 0xDD: 'T1CC1H',
    0xDE: 'T1CC2L', 0xDF: 'T1CC2H', 0xE0: 'ACC', 0xE1: 'RFST', 0xE2: 'T1CNTL',
    0xE3: 'T1CNTH', 0xE4: 'T1CTL', 0xE5: 'T1CCTL0', 0xE6: 'T1CCTL1', 0xE7: 'T1CCTL2',
    0xE8: 'IRCON2', 0xE9: 'RFIF', 0xEA: 'T4CNT', 0xEB: 'T4CTL', 0xEC: 'T4CCTL0',
    0xED: 'T4CC0', 0xEE: 'T4CCTL1', 0xEF: 'T4CC1', 0xF0: 'B', 0xF1: 'PERCFG',
    0xF2: 'ADCCFG', 0xF3: 'P0SEL', 0xF4: 'P1SEL', 0xF5: 'P2SEL', 0xF6: 'P1INP',
    0xF7: 'P2INP', 0xF8: 'U1CSR', 0xF9: 'U1DBUF', 0xFA: 'U1BAUD', 0xFB: 'U1UCR',
    0xFC: 'U1GCR', 0xFD: 'P0DIR', 0xFE: 'P1DIR', 0xFF: 'P2DIR',
}
BITBASE = {0x80: 'P0', 0x88: 'TCON', 0x90: 'P1', 0x98: 'S0CON', 0xA0: 'P2',
           0xA8: 'IEN0', 0xB8: 'IEN1', 0xC0: 'IRCON', 0xD0: 'PSW', 0xD8: 'TIMIF',
           0xE0: 'ACC', 0xE8: 'IRCON2', 0xF0: 'B', 0xF8: 'U1CSR'}


def d(a):
    if a in SFR:
        return SFR[a]
    if a < 0x80:
        return f'0x{a:02x}'
    return f'SFR_{a:02x}'


def bit(b):
    if b < 0x80:
        return f'bit_{0x20 + (b >> 3):02x}.{b & 7}'
    base = b & 0xF8
    return f'{BITBASE.get(base, "SFR_%02x" % base)}.{b & 7}'


REGS = ['R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7']

# (mnemonic, operand pattern) ; pattern tokens: d=direct, i=#imm, b=bit, r=rel,
# a11, a16, I16=#imm16
OPS = {}


def op(code, mn, *args):
    OPS[code] = (mn, args)


op(0x00, 'NOP')
op(0x02, 'LJMP', 'a16'); op(0x03, 'RR', 'A'); op(0x04, 'INC', 'A'); op(0x05, 'INC', 'd')
op(0x06, 'INC', '@R0'); op(0x07, 'INC', '@R1')
op(0x10, 'JBC', 'b', 'r'); op(0x12, 'LCALL', 'a16'); op(0x13, 'RRC', 'A'); op(0x14, 'DEC', 'A')
op(0x15, 'DEC', 'd'); op(0x16, 'DEC', '@R0'); op(0x17, 'DEC', '@R1')
op(0x20, 'JB', 'b', 'r'); op(0x22, 'RET'); op(0x23, 'RL', 'A'); op(0x24, 'ADD', 'A', 'i')
op(0x25, 'ADD', 'A', 'd'); op(0x26, 'ADD', 'A', '@R0'); op(0x27, 'ADD', 'A', '@R1')
op(0x30, 'JNB', 'b', 'r'); op(0x32, 'RETI'); op(0x33, 'RLC', 'A'); op(0x34, 'ADDC', 'A', 'i')
op(0x35, 'ADDC', 'A', 'd'); op(0x36, 'ADDC', 'A', '@R0'); op(0x37, 'ADDC', 'A', '@R1')
op(0x40, 'JC', 'r'); op(0x42, 'ORL', 'd', 'A'); op(0x43, 'ORL', 'd', 'i'); op(0x44, 'ORL', 'A', 'i')
op(0x45, 'ORL', 'A', 'd'); op(0x46, 'ORL', 'A', '@R0'); op(0x47, 'ORL', 'A', '@R1')
op(0x50, 'JNC', 'r'); op(0x52, 'ANL', 'd', 'A'); op(0x53, 'ANL', 'd', 'i'); op(0x54, 'ANL', 'A', 'i')
op(0x55, 'ANL', 'A', 'd'); op(0x56, 'ANL', 'A', '@R0'); op(0x57, 'ANL', 'A', '@R1')
op(0x60, 'JZ', 'r'); op(0x62, 'XRL', 'd', 'A'); op(0x63, 'XRL', 'd', 'i'); op(0x64, 'XRL', 'A', 'i')
op(0x65, 'XRL', 'A', 'd'); op(0x66, 'XRL', 'A', '@R0'); op(0x67, 'XRL', 'A', '@R1')
op(0x70, 'JNZ', 'r'); op(0x72, 'ORL', 'C', 'b'); op(0x73, 'JMP', '@A+DPTR'); op(0x74, 'MOV', 'A', 'i')
op(0x75, 'MOV', 'd', 'i'); op(0x76, 'MOV', '@R0', 'i'); op(0x77, 'MOV', '@R1', 'i')
op(0x80, 'SJMP', 'r'); op(0x82, 'ANL', 'C', 'b'); op(0x83, 'MOVC', 'A', '@A+PC'); op(0x84, 'DIV', 'AB')
op(0x85, 'MOV', 'd2', 'd1'); op(0x86, 'MOV', 'd', '@R0'); op(0x87, 'MOV', 'd', '@R1')
op(0x90, 'MOV', 'DPTR', 'I16'); op(0x92, 'MOV', 'b', 'C'); op(0x93, 'MOVC', 'A', '@A+DPTR')
op(0x94, 'SUBB', 'A', 'i'); op(0x95, 'SUBB', 'A', 'd'); op(0x96, 'SUBB', 'A', '@R0'); op(0x97, 'SUBB', 'A', '@R1')
op(0xA0, 'ORL', 'C', '/b'); op(0xA2, 'MOV', 'C', 'b'); op(0xA3, 'INC', 'DPTR'); op(0xA4, 'MUL', 'AB')
op(0xA5, '???'); op(0xA6, 'MOV', '@R0', 'd'); op(0xA7, 'MOV', '@R1', 'd')
op(0xB0, 'ANL', 'C', '/b'); op(0xB2, 'CPL', 'b'); op(0xB3, 'CPL', 'C'); op(0xB4, 'CJNE', 'A', 'i', 'r')
op(0xB5, 'CJNE', 'A', 'd', 'r'); op(0xB6, 'CJNE', '@R0', 'i', 'r'); op(0xB7, 'CJNE', '@R1', 'i', 'r')
op(0xC0, 'PUSH', 'd'); op(0xC2, 'CLR', 'b'); op(0xC3, 'CLR', 'C'); op(0xC4, 'SWAP', 'A')
op(0xC5, 'XCH', 'A', 'd'); op(0xC6, 'XCH', 'A', '@R0'); op(0xC7, 'XCH', 'A', '@R1')
op(0xD0, 'POP', 'd'); op(0xD2, 'SETB', 'b'); op(0xD3, 'SETB', 'C'); op(0xD4, 'DA', 'A')
op(0xD5, 'DJNZ', 'd', 'r'); op(0xD6, 'XCHD', 'A', '@R0'); op(0xD7, 'XCHD', 'A', '@R1')
op(0xE0, 'MOVX', 'A', '@DPTR'); op(0xE2, 'MOVX', 'A', '@R0'); op(0xE3, 'MOVX', 'A', '@R1')
op(0xE4, 'CLR', 'A'); op(0xE5, 'MOV', 'A', 'd'); op(0xE6, 'MOV', 'A', '@R0'); op(0xE7, 'MOV', 'A', '@R1')
op(0xF0, 'MOVX', '@DPTR', 'A'); op(0xF2, 'MOVX', '@R0', 'A'); op(0xF3, 'MOVX', '@R1', 'A')
op(0xF4, 'CPL', 'A'); op(0xF5, 'MOV', 'd', 'A'); op(0xF6, 'MOV', '@R0', 'A'); op(0xF7, 'MOV', '@R1', 'A')
for i in range(8):
    r = REGS[i]
    op(0x08 + i, 'INC', r); op(0x18 + i, 'DEC', r); op(0x28 + i, 'ADD', 'A', r)
    op(0x38 + i, 'ADDC', 'A', r); op(0x48 + i, 'ORL', 'A', r); op(0x58 + i, 'ANL', 'A', r)
    op(0x68 + i, 'XRL', 'A', r); op(0x78 + i, 'MOV', r, 'i'); op(0x88 + i, 'MOV', 'd', r)
    op(0x98 + i, 'SUBB', 'A', r); op(0xA8 + i, 'MOV', r, 'd'); op(0xB8 + i, 'CJNE', r, 'i', 'r')
    op(0xC8 + i, 'XCH', 'A', r); op(0xD8 + i, 'DJNZ', r, 'r'); op(0xE8 + i, 'MOV', 'A', r)
    op(0xF8 + i, 'MOV', r, 'A')
for p in range(8):
    op(0x01 + p * 0x20, 'AJMP', 'a11')
    op(0x11 + p * 0x20, 'ACALL', 'a11')


def dis_one(c, pc):
    o = c[pc]
    mn, args = OPS.get(o, ('DB', ()))
    n = 1
    out = []
    pending = []
    # 0x85: MOV dst, src  encoded as src, dst
    if o == 0x85:
        src, dst = c[pc + 1], c[pc + 2]
        return 3, f'MOV {d(dst)}, {d(src)}', None
    target = None
    for a in args:
        if a == 'd':
            out.append(d(c[pc + n])); n += 1
        elif a == 'i':
            out.append(f'#0x{c[pc + n]:02x}'); n += 1
        elif a == 'b':
            out.append(bit(c[pc + n])); n += 1
        elif a == '/b':
            out.append('/' + bit(c[pc + n])); n += 1
        elif a == 'I16':
            out.append(f'#0x{c[pc + n] << 8 | c[pc + n + 1]:04x}'); n += 2
        elif a == 'a16':
            target = c[pc + n] << 8 | c[pc + n + 1]
            out.append(f'0x{target:04x}'); n += 2
        elif a == 'a11':
            target = ((pc + 2) & 0xF800) | ((o & 0xE0) << 3) | c[pc + 1]
            out.append(f'0x{target:04x}'); n += 1
        elif a == 'r':
            pending.append(n); n += 1
            out.append(None)
        else:
            out.append(a)
    for k, i in enumerate([j for j, x in enumerate(out) if x is None]):
        rel = c[pc + pending[k]]
        rel = rel - 256 if rel > 127 else rel
        target = pc + n + rel
        out[i] = f'0x{target:04x}'
    return n, (mn + ' ' + ', '.join(out)).strip(), target


def main():
    c = open(sys.argv[1], 'rb').read()
    size = len(c)
    c += bytes(4)    # an instruction cut off at the end of the file
    start = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    end = int(sys.argv[3], 0) if len(sys.argv) > 3 else size
    pc = start
    while pc < end:
        n, s, _ = dis_one(c, pc)
        print(f'{pc:04x}: {c[pc:pc + n].hex(" "):9}  {s}')
        pc += n


if __name__ == '__main__':
    main()
