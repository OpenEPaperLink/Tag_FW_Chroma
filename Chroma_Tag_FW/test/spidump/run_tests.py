#!/usr/bin/env python3
"""Run chroma_spidump.py against a simulated Tag_Flasher and tag.

The simulation speaks the flasher's serial protocol, and "runs" a dumper image
written to it: from the tag it copies 28K of SPI flash to 0x1000.  So this
tests the flow and its failure handling, not the CC1110 code.

usage: run_tests.py [-v]
"""
import contextlib
import glob
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
        # raise=(exception, command, erase count): raised once, at that command
        # once the chip was erased that many times.  port_gone=n: every command
        # raises after the n-th erase, like an unplugged flasher.
        exc = self.faults.get('raise')
        if exc and cmd == exc[1] and self.erases == exc[2] and not self.faults.get('raised'):
            self.faults['raised'] = True
            raise exc[0]
        if self.faults.get('port_gone') and self.erases >= self.faults['port_gone']:
            raise OSError('the port is gone')
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
        self.dumper = bytes(img)
        self.ref = self.file('ref.bin', self.fw)
        self.other = self.file('other.bin', self.oepl)
        self.runs = 0

    def file(self, name, data):
        p = os.path.join(self.dir, name)
        with open(p, 'wb') as f:
            f.write(data)
        return p

    def go(self, tag, *argv, out=None):
        """Each run gets a new output directory, unless out names one"""
        self.runs += 1
        self.out = os.path.join(self.dir, out or f'out{self.runs}')
        args = chroma_spidump.parse_args(['-p', 'X', '--wait', '0', '--screen-wait', '0',
                                          '-o', self.out] + list(argv))
        log = io.StringIO()
        self.exc = ok = None
        with contextlib.redirect_stdout(log):
            try:
                ok = chroma_spidump.run(tagflasher.Flasher(tag), args)
            except BaseException as e:      # KeyboardInterrupt too
                self.exc = e
        self.log = log.getvalue()
        return ok

    def read(self, name):
        p = os.path.join(self.out, name)
        if not os.path.exists(p):
            return None
        with open(p, 'rb') as f:
            return f.read()

    def dump(self):
        return self.read('spi_dump.bin')

    def files(self, pattern):
        return sorted(os.path.basename(p) for p in glob.glob(os.path.join(self.out, pattern)))

    def check(self, name, cond):
        print(f'{"ok  " if cond else "FAIL"} {name}')
        if not cond:
            self.fails += 1
            if '-v' in sys.argv:
                print(self.log)


def rerun_tests(t, spi):
    """Runs into an output directory that already has results (issue #24)"""
    # The issue: a run without --restore leaves the dumper on the tag, then a
    # rerun into the same directory backed up the dumper over the firmware.
    tag = FakeTag(spi, t.fw)
    t.check('rerun: first run leaves the dumper', t.go(tag, out='rerun')
            and t.read('cc1110_backup.bin') == t.fw)
    erases = tag.erases
    ok = t.go(tag, '--restore', out='rerun')
    t.check('rerun on the dumper: --restore stops before erasing', not ok
            and tag.erases == erases and 'holds the spidump dumper' in t.log
            and os.path.join(t.out, 'cc1110_backup.bin') in t.log)
    t.check('rerun on the dumper: the backup is kept', t.read('cc1110_backup.bin') == t.fw
            and t.files('cc1110_backup*') == ['cc1110_backup.bin'])
    ok = t.go(tag, '--reference', t.ref, out='rerun')
    t.check('rerun on the dumper: --reference stops before erasing', not ok
            and tag.erases == erases and 'holds the spidump dumper' in t.log)
    ok = t.go(tag, '--flash', os.path.join(t.out, 'cc1110_backup.bin'), out='rerun')
    t.check('rerun on the dumper: dumps again, --flash puts the firmware back', ok
            and bytes(tag.flash) == t.fw and t.read('spi_dump_2.bin')[:PASS_LEN] == spi[:PASS_LEN]
            and t.files('cc1110_backup*') == ['cc1110_backup.bin'])
    t.check('rerun on the dumper: the first dump is kept', t.dump()[:PASS_LEN] == spi[:PASS_LEN]
            and 'readback_00000_2.bin' in t.files('readback_*'))

    # A rerun on a tag that has its firmware back: same backup, new dump
    ok = t.go(tag, '--restore', '--reference', t.ref, out='rerun')
    t.check('rerun after restore: backup not saved twice', ok and bytes(tag.flash) == t.fw
            and t.files('cc1110_backup*') == ['cc1110_backup.bin'] and 'not saved again' in t.log)
    t.check('rerun after restore: third dump', t.read('spi_dump_3.bin') is not None)

    # A second tag into the same directory: nothing of the first is replaced
    fw2 = bytes(random.Random(3).randbytes(FW_SIZE))
    spi2 = spi_image(0x20001)[:0x20000]
    tag2 = FakeTag(spi2, fw2)
    ok = t.go(tag2, '--restore', out='rerun')
    t.check('second tag: own backup and dump', ok and t.read('cc1110_backup_4.bin') == fw2
            and t.read('spi_dump_4.bin')[:PASS_LEN] == spi2[:PASS_LEN])
    t.check('second tag: the first tag\'s files are unchanged', t.read('cc1110_backup.bin') == t.fw
            and t.dump()[:PASS_LEN] == spi[:PASS_LEN])

    # Interrupted while writing the dumper: only part of it is on the tag
    part = t.dumper[:0x800] + b'\xff' * (FW_SIZE - 0x800)
    tag = FakeTag(spi, part)
    ok = t.go(tag, '--restore', out='rerun')
    t.check('partly written dumper: --restore stops, nothing saved', not ok
            and tag.erases == 0 and 'holds the spidump dumper' in t.log
            and t.read('cc1110_backup_5.bin') is None)

    # Interrupted while restoring: part of the firmware is on the tag
    part = t.fw[:0x4000] + b'\xff' * (FW_SIZE - 0x4000)
    tag = FakeTag(spi, part)
    ok = t.go(tag, '--restore', out='rerun')
    t.check('partly restored firmware: --restore stops, names the backup', not ok
            and tag.erases == 0 and 'partly written copy of' in t.log
            and 'cc1110_backup.bin' in t.log and t.read('cc1110_backup_5.bin') is None)
    ok = t.go(tag, '--mode', 'none', '--flash', os.path.join(t.out, 'cc1110_backup.bin'),
              out='rerun')
    t.check('partly restored firmware: --flash the backup', ok and bytes(tag.flash) == t.fw)

    # A dumper image is never a firmware to flash
    tag = FakeTag(spi, t.fw)
    ok = t.go(tag, '--flash', os.path.join(t.dir, 'spidump.bin'))
    t.check('--flash a dumper: refused before connecting', not ok and tag.erases == 0
            and tag.reads == 0 and 'is a spidump image' in t.log)


def error_tests(t, spi):
    """Errors other than Fail still restore, and are raised again (issue #25)"""
    R = tagflasher.CMD_READ_FLASH
    W = tagflasher.CMD_WRITE_FLASH

    # A disk error saving a read back, after the dumper ran
    real_save = chroma_spidump.save

    def bad_save(path, data):
        if 'readback' in path:
            raise OSError(28, 'No space left on device')
        real_save(path, data)
    chroma_spidump.save = bad_save
    try:
        tag = FakeTag(spi, t.fw)
        t.go(tag, '--restore')
        t.check('disk error: firmware restored', bytes(tag.flash) == t.fw and tag.erases == 2)
        t.check('disk error: raised again and shown', isinstance(t.exc, OSError)
                and 'FAILED: OSError' in t.log and 'No space left' in t.log)

        # ... and then the restore fails too: both are shown, the first is raised
        tag = FakeTag(spi, t.fw, bad_write=2)
        t.go(tag, '--restore')
        t.check('disk error, bad restore: both shown, the disk error raised',
                isinstance(t.exc, OSError) and 'No space left' in t.log
                and 'FAILED while restoring' in t.log
                and 'holds the original firmware, which failed its verify' in t.log)
    finally:
        chroma_spidump.save = real_save

    # A serial error verifying the dumper, then the link works again
    tag = FakeTag(spi, t.fw, **{'raise': (OSError('read failed'), R, 1)})
    t.go(tag, '--restore')
    t.check('serial error: firmware restored, raised again', bytes(tag.flash) == t.fw
            and isinstance(t.exc, OSError) and 'read failed' in t.log)

    # The flasher is unplugged after the erase: no restore is tried
    tag = FakeTag(spi, t.fw, port_gone=1)
    t.go(tag, '--restore')
    t.check('port gone: says so and what the tag holds', isinstance(t.exc, OSError)
            and 'does not answer' in t.log and tag.erases == 1
            and 'holds part of the dumper for 0x00000' in t.log)

    # Ctrl-C during the pass (verifying the dumper): restore, then stop
    tag = FakeTag(spi, t.fw, **{'raise': (KeyboardInterrupt(), R, 1)})
    t.go(tag, '--restore')
    t.check('Ctrl-C: firmware restored, interrupt raised again', bytes(tag.flash) == t.fw
            and isinstance(t.exc, KeyboardInterrupt) and 'Ctrl-C again' in t.log)

    # Ctrl-C while writing the dumper, before anything was saved but the backup
    tag = FakeTag(spi, t.fw, **{'raise': (KeyboardInterrupt(), W, 1)})
    t.go(tag, '--restore')
    t.check('Ctrl-C while writing the dumper: firmware restored',
            bytes(tag.flash) == t.fw and isinstance(t.exc, KeyboardInterrupt))

    # Ctrl-C before anything was erased: nothing written
    tag = FakeTag(spi, t.fw, **{'raise': (KeyboardInterrupt(), R, 0)})
    t.go(tag, '--restore')
    t.check('Ctrl-C while backing up: nothing erased', tag.erases == 0
            and bytes(tag.flash) == t.fw and isinstance(t.exc, KeyboardInterrupt))

    # Errors in the restore itself, after a run that went well
    tag = FakeTag(spi, t.fw, **{'raise': (OSError('write failed'), W, 2)})
    t.go(tag, '--restore')
    t.check('serial error while restoring: raised, says what the tag holds',
            isinstance(t.exc, OSError) and 'FAILED while restoring: OSError' in t.log
            and 'holds part of the original firmware' in t.log)

    tag = FakeTag(spi, t.fw, **{'raise': (KeyboardInterrupt(), W, 2)})
    t.go(tag, '--restore')
    t.check('Ctrl-C while restoring: raised, says what the tag holds',
            isinstance(t.exc, KeyboardInterrupt) and 'holds part of the original firmware' in t.log)

    # A Fail, then a serial error while restoring: the serial error is raised
    tag = FakeTag(spi, t.fw, spi_dead=True, **{'raise': (OSError('write failed'), W, 2)})
    t.go(tag, '--restore')
    t.check('Fail then a serial error restoring: both shown', isinstance(t.exc, OSError)
            and 'all 0x00' in t.log and 'FAILED while restoring: OSError' in t.log)


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
    t.check('wrong reference: nothing saved', t.files('*') == [])

    rerun_tests(t, spi)

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
    t.check('no --restore: says what is on the tag', not ok
            and 'holds the dumper for 0x00000, flash a firmware' in t.log)

    error_tests(t, spi)

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
