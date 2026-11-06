#!/usr/bin/env python3
"""Capture a tag's debug UART through the Tag_Flasher, with time stamps.

The OEPL Chroma firmware logs at 115200 baud on P1.6 (USART1 TX).  Wire it to
the flasher's external RXD: GPIO36 on the S2 Mini Tag_Flasher, cable pin 5,
which the flasher's README labels TXD (the tag's side of the link).

usage: taglog.py -p COM12 [-t seconds] [-o log.txt] [--no-reset]
"""
import argparse
import sys
import time

from tagflasher import (Flasher, CMD_PASS_THROUGH, CMD_RESET, CMD_SELECT_PORT,
                        CMD_SET_POWER, PORT_EXTERNAL)


def main():
    import serial
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-p', '--port', required=True)
    ap.add_argument('-t', '--time', type=float, default=60, help='seconds to capture')
    ap.add_argument('-o', '--out', help='also write the log to this file')
    ap.add_argument('--no-reset', action='store_true', help="don't reset the tag first")
    args = ap.parse_args()

    f = Flasher(serial.Serial(args.port, baudrate=115200, timeout=3))
    f.send(CMD_SELECT_PORT, bytes([PORT_EXTERNAL]))
    f.cmd(CMD_SET_POWER, b'\x01')
    if not args.no_reset:
        f.cmd(CMD_RESET)        # no controller selected: pulses RESET
    f.send(CMD_PASS_THROUGH)
    out = open(args.out, 'w', encoding='utf-8') if args.out else None
    start = time.time()
    end = start + args.time
    f.ser.timeout = 0.2
    bol = True        # at the beginning of a line: add a time stamp
    while time.time() < end:
        d = f.ser.read(256)
        if not d:
            continue
        s = ''
        for c in d.decode('utf-8', 'replace').replace('\r', ''):
            if bol:
                s += f'{time.time() - start:7.2f} '
            s += c
            bol = c == '\n'
        sys.stdout.write(s)
        sys.stdout.flush()
        if out:
            out.write(s)
            out.flush()
    print(f'\n--- {args.time:.0f} s captured')


if __name__ == '__main__':
    main()
