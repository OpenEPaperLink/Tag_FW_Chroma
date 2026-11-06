#!/usr/bin/env python3
"""Run chroma_spidump.py against a simulated Tag_Flasher and tag.

The simulation speaks the flasher's serial protocol, and "runs" a dumper image
written to it: from the tag it copies 28K of SPI flash to 0x1000.  So this
tests the flow and its failure handling, not the CC1110 code.

usage: run_tests.py [-v]
"""
import contextlib
import io
import os
import random
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..', 'spidump')))
import chroma_spidump                                          # noqa: E402
import spidump_join                                            # noqa: E402
import tagflasher                                              # noqa: E402
from spidump_join import BASE_OFS, DATA_ADR, PASS_LEN, TAG, TAG_ADR  # noqa: E402

FW_SIZE = 0x8000


class FakeTag:
    """A serial port with a Tag_Flasher and a tag behind it"""

    def __init__(self, spi, fw, **faults):
        self.spi, self.flash = spi, bytearray(fw)
        self.faults = faults
        self.rx = bytearray()
        self.ofs = self.erases = self.runs = 0
        self.reads = 0
        self.timeout = 3

    def reset_input_buffer(self):
        self.rx.clear()

    def read(self, n):
        d = bytes(self.rx[:n])
        del self.rx[:n]
        return d

    def answer(self, cmd, data=b''):
        pkt = bytes([cmd]) + len(data).to_bytes(4, 'big') + bytes(data)
        crc = (0xAB34 + sum(pkt)) & 0xffff
        self.rx += b'AT' + pkt + crc.to_bytes(2, 'big')

    def write(self, pkt):
        cmd = pkt[2]
        n = int.from_bytes(pkt[3:7], 'big')
        data = pkt[7:7 + n]
        if cmd == tagflasher.CMD_GET_VERSION:
            self.answer(cmd, (50).to_bytes(4, 'big'))
        elif cmd == tagflasher.CMD_SELECT_PORT:
            pass                                      # no answer
        elif cmd == tagflasher.CMD_SELECT_CC:
            self.ofs = 0
            self.answer(cmd, b'\x01')                 # always "connected"
        elif cmd == tagflasher.CMD_ERASE_FLASH:
            self.flash = bytearray(b'\xff' * FW_SIZE)
            self.erases += 1
            self.answer(cmd)
        elif cmd == tagflasher.CMD_WRITE_FLASH:
            if self.faults.get('bad_write') and self.erases == self.faults['bad_write']:
                data = bytes([data[0] ^ 1]) + data[1:]
            self.flash[self.ofs:self.ofs + n] = data
            self.ofs += n
            self.answer(cmd)
        elif cmd == tagflasher.CMD_READ_FLASH:
            self.read_flash(cmd)
        elif cmd == tagflasher.CMD_RESET:
            self.run()
            self.answer(cmd)
        else:
            self.answer(cmd)

    def read_flash(self, cmd):
        if self.ofs >= FW_SIZE:
            self.answer(tagflasher.CMD_COMPLETE, b'\0')
            return
        d = bytes(self.flash[self.ofs:self.ofs + tagflasher.CHUNK])
        if self.faults.get('dead_link'):
            d = b'\xff' * len(d)
        if self.ofs == 0:
            self.reads += 1
            if self.faults.get('flaky') and self.reads == self.faults['flaky']:
                d = bytes([d[0] ^ 0x10]) + d[1:]
        self.ofs += len(d)
        self.answer(cmd, d)

    def run(self):
        """The dumper does its work.  A real firmware just runs."""
        self.runs += 1
        tag = self.flash[TAG_ADR:TAG_ADR + BASE_OFS + 3]
        if tag[:BASE_OFS] != TAG or self.faults.get('no_run'):
            return
        base = int.from_bytes(tag[BASE_OFS:], 'big')
        d = bytes(PASS_LEN) if self.faults.get('spi_dead') else self.spi[base:base + PASS_LEN]
        self.flash[DATA_ADR:DATA_ADR + PASS_LEN] = d.ljust(PASS_LEN, b'\xff')


def spi_image(size):
    r = random.Random(size)
    d = bytearray(r.randbytes(size))
    d[0:0x1000] = b'\xff' * 0x1000
    d[0x1000:0x1004] = spidump_join.NVRAM_MAGIC        # sector 1, like JG tags
    d[0x2000:0x2006] = spidump_join.WAVEFORM_MAGIC + b'\x04\x02'
    d[0x2006:0x2021] = b'TEST'.ljust(0x1b, b'\0')
    return bytes(d)


class T:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.fw = bytes(random.Random(1).randbytes(FW_SIZE))
        self.oepl = bytes(random.Random(2).randbytes(FW_SIZE - 300))
        self.fails = 0
        img = bytearray(b'\xff' * (TAG_ADR + BASE_OFS + 3))
        img[:4] = b'\x02\x00\x06\x00'
        img[TAG_ADR:TAG_ADR + BASE_OFS] = TAG
        for name in ('spidump.bin', 'spidump_chroma21.bin'):
            with open(os.path.join(self.dir, name), 'wb') as f:
                f.write(img)
        chroma_spidump.HERE = self.dir
        self.ref = self.file('ref.bin', self.fw)
        self.other = self.file('other.bin', self.oepl)

    def file(self, name, data):
        p = os.path.join(self.dir, name)
        with open(p, 'wb') as f:
            f.write(data)
        return p

    def go(self, tag, *argv):
        out = os.path.join(self.dir, 'out')
        args = chroma_spidump.parse_args(['-p', 'X', '--wait', '0', '--screen-wait', '0',
                                          '-o', out] + list(argv))
        log = io.StringIO()
        with contextlib.redirect_stdout(log):
            ok = chroma_spidump.run(tagflasher.Flasher(tag), args)
        self.log = log.getvalue()
        return ok

    def dump(self):
        with open(os.path.join(self.dir, 'out', 'spi_dump.bin'), 'rb') as f:
            return f.read()

    def check(self, name, cond):
        print(f'{"ok  " if cond else "FAIL"} {name}')
        if not cond:
            self.fails += 1
            if '-v' in sys.argv:
                print(self.log)


def main():
    t = T()
    spi = spi_image(0x20000)
    spi1m = spi_image(0x100000)

    tag = FakeTag(spi, t.fw)
    ok = t.go(tag, '--restore', '--reference', t.ref)
    t.check('first: pass 0 is dumped', ok and t.dump()[:PASS_LEN] == spi[:PASS_LEN])
    t.check('first: the rest stays erased', t.dump()[PASS_LEN:] == b'\xff' * (0x20000 - PASS_LEN))
    t.check('first: one pass', tag.erases == 2)            # dumper + restore
    t.check('first: firmware restored', bytes(tag.flash) == t.fw)
    t.check('first: factory data reported', 'NVRAM found in sector 1' in t.log
            and 'format 04 02 "TEST"' in t.log)

    tag = FakeTag(spi, t.fw)
    t.check('full 128K', t.go(tag, '--mode', 'full', '--restore') and t.dump() == spi)
    t.check('full 128K: 5 passes', tag.erases == 6)

    tag = FakeTag(spi1m, t.fw)
    t.check('full 1M', t.go(tag, '--mode', 'full', '--size', '1M', '--restore')
            and t.dump() == spi1m)
    t.check('full 1M: 37 passes', tag.erases == 38)

    tag = FakeTag(spi, t.fw)
    ok = t.go(tag, '--mode', 'none', '--flash', t.other, '--reference', t.ref)
    t.check('none: flashes the image', ok and bytes(tag.flash[:len(t.oepl)]) == t.oepl)
    t.check('none: no dumper, one erase', tag.erases == 1)
    t.check('none: the tag runs it', tag.runs == 1)

    tag = FakeTag(spi, t.fw)
    ok = t.go(tag, '--screen', 'chroma21', '--restore')
    t.check('screen build for pass 0', ok and t.dump()[:PASS_LEN] == spi[:PASS_LEN])

    tag = FakeTag(spi, t.fw)
    ok = t.go(tag, '--reference', t.other, '--restore')
    t.check('wrong reference: stops before erasing', not ok and tag.erases == 0
            and bytes(tag.flash) == t.fw and 'differs from' in t.log)

    tag = FakeTag(spi, t.fw, dead_link=True)
    ok = t.go(tag, '--restore')
    t.check('dead link: stops before erasing', not ok and tag.erases == 0 and 'all 0xff' in t.log)

    tag = FakeTag(spi, t.fw, flaky=2)
    ok = t.go(tag, '--restore')
    t.check('backup reads differ: stops before erasing', not ok and tag.erases == 0
            and 'two reads differ' in t.log)

    tag = FakeTag(spi, t.fw, spi_dead=True)
    ok = t.go(tag, '--restore')
    t.check('dead SPI flash: fails, firmware restored', not ok and 'all 0x00' in t.log
            and bytes(tag.flash) == t.fw)

    tag = FakeTag(spi, t.fw, no_run=True)
    ok = t.go(tag, '--restore')
    t.check('dumper not running: fails, firmware restored', not ok and 'did not run' in t.log
            and bytes(tag.flash) == t.fw)

    tag = FakeTag(spi, t.fw, bad_write=1)
    ok = t.go(tag, '--restore')
    t.check('bad dumper write: fails, firmware restored', not ok and 'read back differs' in t.log
            and bytes(tag.flash) == t.fw)

    tag = FakeTag(spi, t.fw, bad_write=2)
    ok = t.go(tag, '--restore')
    t.check('bad restore: reported', not ok and 'FAILED while restoring' in t.log)

    tag = FakeTag(spi, t.fw, spi_dead=True)
    ok = t.go(tag)
    t.check('no --restore: says what is on the tag', not ok and 'still holds the dumper' in t.log)

    tag = FakeTag(spi, t.fw)
    os.remove(os.path.join(t.dir, 'spidump_chroma21.bin'))
    ok = t.go(tag, '--screen', 'chroma21')
    t.check('missing image: stops before erasing', not ok and tag.erases == 0
            and 'build.sh --screen chroma21' in t.log)

    no_nvram = spi[:0x1000] + bytes(4) + spi[0x1004:]
    tag = FakeTag(no_nvram, t.fw)
    t.check('no NVRAM in the dump: fails', not t.go(tag, '--restore') and 'NO factory NVRAM' in t.log)

    tag = FakeTag(b'\xff' * 0x20000, t.fw)
    t.check('blank SPI flash: looks like the dumper did not run', not t.go(tag, '--restore')
            and 'did not run' in t.log)

    # join on its own
    old = bytearray(b'\xff' * FW_SIZE)
    old[0xf00:0xf0a] = TAG + b'\x00\x00\x00'
    old[DATA_ADR:] = spi[:PASS_LEN]
    with contextlib.redirect_stdout(io.StringIO()):
        out, ok = spidump_join.join([('old', bytes(old))], 0x20000)
    t.check('join: old tag position', ok and out[:PASS_LEN] == spi[:PASS_LEN])
    t.check('parse_size', [spidump_join.parse_size(s) for s in ('128K', '1M', '0x1000', '4096')]
            == [0x20000, 0x100000, 0x1000, 4096])

    print('all passed' if not t.fails else f'{t.fails} FAILED')
    return 1 if t.fails else 0


if __name__ == '__main__':
    sys.exit(main())
