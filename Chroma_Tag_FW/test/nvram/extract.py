#!/usr/bin/env python3
"""Cut the factory NVRAM functions out of the firmware sources.

usage: extract.py soc.c boardChroma.c > extracted.c

The harness compiles the real code with gcc, so these functions are copied
verbatim (by name, braces matched) instead of being duplicated by hand.
"""
import re
import sys

SOC_FUNCS = ('prvReadSetting', 'boardInitStage2', 'LoadGlobalsFromEEPROM')
BOARD_FUNCS = ('ResetFactoryNVRAM',)


def extract(src, name):
    # the definition is the first "name(" followed by a "{" before any ";"
    for m in re.finditer(r'^[^\n#;]*\b' + name + r'\s*\(', src, re.M):
        body = src.find('{', m.end())
        if body < 0 or ';' in src[m.end():body]:
            continue
        depth = 0
        for i in range(body, len(src)):
            if src[i] == '{':
                depth += 1
            elif src[i] == '}':
                depth -= 1
                if depth == 0:
                    return src[m.start():i + 1]
    sys.exit(f"extract.py: {name} not found")


def main():
    soc = open(sys.argv[1]).read().replace('\r\n', '\n')
    board = open(sys.argv[2]).read().replace('\r\n', '\n')
    out = [f'// extracted from {sys.argv[1]} and {sys.argv[2]}, do not edit\n']
    out += [extract(soc, f) + '\n' for f in SOC_FUNCS]
    out += [extract(board, f) + '\n' for f in BOARD_FUNCS]
    sys.stdout.write('\n'.join(out))


if __name__ == '__main__':
    main()
