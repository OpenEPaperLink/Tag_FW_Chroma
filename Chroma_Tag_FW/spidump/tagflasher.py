"""OpenEPaperLink Tag_Flasher serial protocol, for CC1110 tags.

The same protocol as Tag_Flasher/OEPL-Flasher.py in the OpenEPaperLink repo,
which can't be imported: it's a script that opens the port on its own, and
reopening an S2 Mini's port right after a write can hang its USB.  This keeps
one session open.

The flasher doesn't check anything for CC chips: "connected" and "write OK" are
always reported and a dead link reads as 0xff, so verify by reading back.
"""
import time

CMD_GET_VERSION = 1
CMD_RESET = 11
CMD_SET_POWER = 13
CMD_ERASE_FLASH = 26
CMD_PASS_THROUGH = 50
CMD_SELECT_CC = 62
CMD_SELECT_PORT = 70
CMD_READ_FLASH = 81
CMD_WRITE_FLASH = 83
CMD_COMPLETE = 88

FLASH_SIZE = 0x8000     # CC1110F32
CHUNK = 256
PORT_EXTERNAL = 1


class Fail(Exception):
    """Something went wrong, the message says what."""


class Flasher:
    def __init__(self, ser):
        """ser: an open serial port (115200 baud), or anything like it"""
        self.ser = ser
        self.erased = False     # the CC1110 flash was erased
        time.sleep(0.1)
        ser.reset_input_buffer()

    def send(self, cmd, data=b''):
        pkt = bytes([cmd]) + len(data).to_bytes(4, 'big') + bytes(data)
        crc = (0xAB34 + sum(pkt)) & 0xffff
        self.ser.write(b'AT' + pkt + crc.to_bytes(2, 'big'))

    def recv(self, timeout=5):
        end = time.time() + timeout
        buf = b''
        while time.time() < end:
            c = self.ser.read(1)
            if not c:
                continue
            buf = (buf + c)[-2:]
            if buf != b'AT':
                continue
            hdr = self.ser.read(5)
            n = int.from_bytes(hdr[1:5], 'big')
            data = self.ser.read(n)
            crc = int.from_bytes(self.ser.read(2), 'big')
            if (0xAB34 + sum(hdr) + sum(data)) & 0xffff != crc:
                raise Fail('bad CRC from the flasher')
            return hdr[0], data
        raise Fail('timeout waiting for the flasher')

    def cmd(self, cmd, data=b'', expect=None, timeout=5):
        self.send(cmd, data)
        c, d = self.recv(timeout)
        if c != (cmd if expect is None else expect):
            raise Fail(f'flasher answered {c} to command {cmd}')
        return d

    def connect(self):
        """Select the external port and power the tag"""
        v = self.cmd(CMD_GET_VERSION)
        print(f'flasher version {int.from_bytes(v, "big")}')
        self.send(CMD_SELECT_PORT, bytes([PORT_EXTERNAL]))   # no answer
        self.cmd(CMD_SET_POWER, b'\x01')
        time.sleep(0.2)

    def select(self):
        """(Re)enter debug mode: the CPU is halted and the offset is 0"""
        self.cmd(CMD_SELECT_CC)

    def run(self):
        """Pulse RESET: the CC1110 leaves debug mode and runs its program"""
        self.cmd(CMD_RESET)

    def read(self, length=FLASH_SIZE):
        self.select()
        out = bytearray()
        t = time.time()
        while len(out) < length:
            self.send(CMD_READ_FLASH)
            c, d = self.recv()
            if c == CMD_COMPLETE:
                break
            if c != CMD_READ_FLASH:
                raise Fail(f'unexpected answer {c} while reading')
            out += d
            print(f'\r  read {len(out):5d} bytes', end='', flush=True)
        print(f'  ({time.time() - t:.0f} s)')
        return bytes(out[:length])

    def write(self, image):
        """Erases the whole chip first"""
        self.select()
        self.erased = True
        self.cmd(CMD_ERASE_FLASH)
        for i in range(0, len(image), CHUNK):
            self.cmd(CMD_WRITE_FLASH, image[i:i + CHUNK])
            print(f'\r  wrote {min(i + CHUNK, len(image)):5d} bytes', end='', flush=True)
        print()

    def write_verified(self, image, what):
        print(f'writing {what} ({len(image)} bytes)')
        self.write(image)
        back = self.read(len(image))
        if back != image:
            bad = next((i for i, (x, y) in enumerate(zip(back, image)) if x != y), len(back))
            raise Fail(f'{what}: read back differs at 0x{bad:04x}')
        print(f'  {what} verified')

    def read_twice(self, what):
        """Two reads must match and show something, the link isn't checked otherwise"""
        print(f'reading {what} (1/2)')
        a = self.read()
        print(f'reading {what} (2/2)')
        b = self.read()
        if a != b:
            diff = sum(1 for x, y in zip(a, b) if x != y)
            raise Fail(f'{what}: the two reads differ in {diff} bytes, bad connection?')
        if a == b'\xff' * len(a):
            raise Fail(f'{what}: all 0xff, the CC1110 is not answering '
                       '(check RESET, DD, DC) or it is locked')
        return a
