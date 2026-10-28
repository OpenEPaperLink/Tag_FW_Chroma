#!/usr/bin/env python3
"""Compile actual production storage/EEPROM function bodies with busy-NOR SPI.
Run: python3 tests/packed_g5/test_storage.py (HOST_CC may select clang).
No firmware source is rewritten; extracted C exists only in a temporary folder.
"""
import sys
sys.dont_write_bytecode = True
import ctypes
import os
from pathlib import Path
import random
import re
import shlex
import struct
import subprocess
import tempfile
import unittest
from reference import encode

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
FIRMWARE = REPO / "Chroma_Tag_FW"
RAW1, RAW2, G5 = 0x20, 0x21, 0x31
PRELOAD = 4
SPLASH = (1 << 3) | PRELOAD
SLIDESHOW = (15 << 3) | PRELOAD
HEADER_BYTES = 22


def extract_function(source, name):
    """Keep the live C body verbatim, balancing braces outside comments/quotes."""
    pattern = (r"(?m)^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[\w \t*]+\b" +
               re.escape(name) + r"\s*\([^;{}]*\)\s*(?:__reentrant\s*)?\{")
    match = re.search(pattern, source)
    if not match:
        raise AssertionError("production function not found: " + name)
    index, depth, state = match.end(), 1, "code"
    while index < len(source):
        ch, following = source[index], source[index:index + 2]
        if state == "line":
            if ch == "\n": state = "code"
        elif state == "comment":
            if following == "*/": state = "code"; index += 1
        elif state in ("'", '"'):
            if ch == "\\": index += 1
            elif ch == state: state = "code"
        elif following == "//": state = "line"; index += 1
        elif following == "/*": state = "comment"; index += 1
        elif ch in ("'", '"'): state = ch
        elif ch == "{": depth += 1
        elif ch == "}":
            depth -= 1
            if not depth:
                definition = source[match.start():index + 1]
                # Give the firmware's external-inline entry internal host linkage.
                return re.sub(r"^inline\b", "static", definition.strip(), count=1)
        index += 1
    raise AssertionError("unbalanced production function: " + name)


class Storage:
    def __init__(self, directory, name, width=640, height=384, slot=0x17000):
        self.width, self.height, self.slot = width, height, slot
        eeprom = (FIRMWARE / "common/eeprom.c").read_text()
        protocol = (FIRMWARE / "OEPL/syncedproto.c").read_text()
        functions = [extract_function(eeprom, fn) for fn in
                     ["eepromPrvSimpleCmd", "eepromPrvBusyWait", "eepromWriteLL",
                      "eepromRead", "eepromWrite"]]
        functions += [extract_function(protocol, fn) for fn in
                      ["getAddressForSlot", "findSlotVer", "findSlotDataTypeArg",
                       "eraseImageBlock", "findImageSlot", "removeOldSpecial",
                       "downloadImageDataToEEPROM",
                       "processImageDataAvail"]]
        Path(directory, "storage_production.inc").write_text("\n\n".join(functions))
        output = Path(directory, name + ".so")
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-O2", "-g", "-Wall", "-Wextra",
            "-Wno-unknown-pragmas", "-shared", "-fPIC", "-DG5_HOST_TEST",
            "-DTEST_SCREEN_WIDTH=%d" % width, "-DTEST_SCREEN_HEIGHT=%d" % height,
            "-DTEST_SLOT_BYTES=%d" % slot, "-include", str(HERE / "include/platform.h"),
            "-I" + str(HERE / "include"), "-I" + str(FIRMWARE / "common"),
            "-I" + str(REPO / "shared"), "-I" + directory,
            str(HERE / "storage_harness.c"), str(FIRMWARE / "common/g5.c"),
            "-o", str(output)], check=True)
        self.lib = ctypes.CDLL(str(output))
        self.lib.storage_seed.argtypes = [ctypes.c_uint, ctypes.c_uint64, ctypes.c_uint, ctypes.c_uint]
        self.lib.storage_process.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
            ctypes.c_uint64, ctypes.c_uint, ctypes.c_uint, ctypes.c_int]
        self.lib.storage_process.restype = ctypes.c_int
        self.lib.storage_select_current.argtypes = [ctypes.c_uint]
        self.lib.storage_slot_field.argtypes = [ctypes.c_uint, ctypes.c_uint]
        self.lib.storage_slot_field.restype = ctypes.c_uint32
        self.lib.storage_copy.argtypes = [ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint32]
        self.lib.storage_stat.argtypes = [ctypes.c_uint]
        self.lib.storage_stat.restype = ctypes.c_uint
        self.lib.storage_valid.argtypes = [ctypes.c_uint]
        self.reset()

    def reset(self): self.lib.storage_reset()
    def seed(self, slot, version, argument=0, current=False):
        self.lib.storage_seed(slot, version, argument, current)
    def process(self, payload, version, datatype=G5, argument=0, fail=-1):
        data = ctypes.create_string_buffer(payload, max(1, len(payload)))
        return bool(self.lib.storage_process(data, len(payload), version, datatype, argument, fail))
    def field(self, slot, field): return self.lib.storage_slot_field(slot, field)
    def stat(self, field): return self.lib.storage_stat(field)
    def valid(self, slot): return bool(self.lib.storage_valid(slot))
    @property
    def count(self): return self.lib.storage_slot_count()
    def snapshot(self, slot, length=None):
        length = self.slot if length is None else length
        output = ctypes.create_string_buffer(length)
        self.lib.storage_copy(slot, output, length)
        return output.raw
    def pixels(self, slot): return self.snapshot(slot, HEADER_BYTES + self.field(slot, 0))[HEADER_BYTES:]
    def g5(self, raw, planes=1):
        compressed = struct.pack("<BHHB", 6, self.width, self.height, planes) + encode(
            self.width, self.height * planes, raw)
        assert len(compressed) <= self.slot - HEADER_BYTES, "success fixture exceeds its source slot"
        return compressed
    def pattern(self, planes=1):
        # Plane-distinct repeated rows fit the source slot and exercise the
        # reference-row transition without triggering the AP's raw fallback.
        return b"".join(bytes([0xaa if y < self.height else 0x5a]) * (self.width // 8)
                        for y in range(self.height * planes))


class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="chroma-storage-tests-")
        cls.normal = Storage(cls.temp.name, "normal")
        cls.large = Storage(cls.temp.name, "chroma60", 600, 448, 0x11000)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def setUp(self):
        self.normal.reset(); self.large.reset()
    def assert_ready(self, store):
        self.assertEqual(store.stat(6), 0, "SPI READ occurred while NOR WIP was set")
        self.assertEqual(store.stat(7), 0, "ACK occurred before programming completed")
        if store.stat(0) and store.stat(8):
            self.assertGreater(store.stat(10), 0, "download ACK lacked a final marker commit")
    def matching(self, store, argument):
        return [slot for slot in range(store.count)
                if store.valid(slot) and store.field(slot, 2) & 0xf8 == argument & 0xf8]

    def test_01_actual_eeprom_write_completes_all_pages(self):
        self.assertEqual(self.normal.lib.storage_spi_completion(), 1)
        self.assert_ready(self.normal)

    def test_02_raw_download_current_and_cached_paths(self):
        s = self.normal
        s.seed(0, 100, current=True); old = s.snapshot(0)
        raw = s.pattern()
        self.assertTrue(s.process(raw, 101, RAW1))
        slot = s.stat(4)
        self.assertEqual(s.pixels(slot), raw)
        self.assertEqual((s.field(slot, 0), s.field(slot, 1)), (len(raw), RAW1))
        self.assertEqual((s.stat(0), s.stat(1)), (1, 1))
        gets = s.stat(2)
        self.assertTrue(s.process(raw, 101, RAW1))
        self.assertEqual((s.stat(0), s.stat(1), s.stat(2)), (2, 1, gets))
        self.assertEqual(s.snapshot(0), old)
        # Cache hit when the same version is not the current screen.
        s.reset(); s.seed(3, 202)
        self.assertTrue(s.process(raw, 202, RAW1))
        self.assertEqual((s.stat(0), s.stat(1), s.stat(2), s.stat(5)), (1, 1, 0, 3))
        self.assert_ready(s)

    def test_03_g5_commit_converts_type_and_reuses_decoded_cache(self):
        s = self.normal
        s.seed(1, 100, current=True); old = s.snapshot(1)
        raw = s.pattern(2); compressed = s.g5(raw, 2)
        self.assertTrue(s.process(compressed, 301))
        destination, source = s.stat(4), s.stat(3)
        self.assertNotEqual(source, destination)
        self.assertNotIn(1, (source, destination))
        self.assertFalse(s.valid(source))
        self.assertEqual((s.field(destination, 0), s.field(destination, 1)), (len(raw), RAW2))
        self.assertEqual(s.pixels(destination), raw)
        self.assertEqual(s.snapshot(1), old)
        gets = s.stat(2)
        # Select the previous image so this reaches cached-version lookup,
        # rather than the current-image shortcut. Keep the original argument.
        s.lib.storage_select_current(1)
        self.assertTrue(s.process(compressed, 301, G5))
        self.assertEqual((s.stat(0), s.stat(1), s.stat(2)), (2, 2, gets))
        self.assertEqual(s.stat(5), destination)
        self.assert_ready(s)

    def test_04_raw_and_g5_special_preloads(self):
        for datatype in (RAW1, G5):
            with self.subTest(datatype=datatype):
                s = self.normal; s.reset()
                s.seed(0, 400, SPLASH); s.seed(1, 401, current=True)
                current = s.snapshot(1)
                raw = s.pattern(); payload = s.g5(raw) if datatype == G5 else raw
                self.assertTrue(s.process(payload, 402, datatype, SPLASH))
                matches = self.matching(s, SPLASH)
                self.assertEqual(len(matches), 1)
                self.assertEqual((s.field(matches[0], 3), s.pixels(matches[0])), (402, raw))
                if datatype == G5: self.assertEqual(s.snapshot(1), current)
                self.assertEqual((s.stat(0), s.stat(1)), (1, 0))
                self.assert_ready(s)

    def test_05_failed_g5_preserves_old_custom_and_current(self):
        s = self.normal
        s.seed(0, 500, SPLASH); s.seed(1, 501, current=True)
        custom, current = s.snapshot(0), s.snapshot(1)
        malformed = struct.pack("<BHHB", 6, s.width, s.height, 1) + b"\0"
        self.assertFalse(s.process(malformed, 502, G5, SPLASH))
        self.assertEqual((s.stat(0), s.stat(1)), (0, 0))
        self.assertEqual(s.snapshot(0), custom); self.assertEqual(s.snapshot(1), current)
        self.assertEqual([i for i in range(s.count) if s.valid(i)], [0, 1])
        self.assert_ready(s)

    def test_06_output_slot_exhaustion_never_acknowledges(self):
        s = self.normal
        s.seed(1, 601, current=True)
        for slot in range(2, s.count): s.seed(slot, 600 + slot, SPLASH)
        protected = {slot: s.snapshot(slot) for slot in range(1, s.count)}
        self.assertFalse(s.process(s.g5(s.pattern()), 650))
        self.assertGreater(s.stat(2), 0)  # A source fit; no distinct output slot fit.
        self.assertEqual((s.stat(0), s.stat(1)), (0, 0))
        for slot, before in protected.items(): self.assertEqual(s.snapshot(slot), before)
        self.assertFalse(s.valid(0)); self.assert_ready(s)
        # No input slot either: do not request any payload or erase a protected slot.
        s.reset()
        for slot in range(s.count): s.seed(slot, 700 + slot, SPLASH)
        self.assertFalse(s.process(s.g5(s.pattern()), 750))
        self.assertEqual((s.stat(0), s.stat(2), s.stat(9)), (0, 0, 0))

    def test_07_resume_after_block_failure_keeps_header_invalid(self):
        s = self.normal
        s.seed(0, 800, current=True); old = s.snapshot(0)
        rng = random.Random(7301)
        raw = rng.getrandbits(s.width // 8 * s.height * 8).to_bytes(s.width // 8 * s.height, "little")
        compressed = s.g5(raw)
        self.assertGreater(len(compressed), 4096)
        self.assertLessEqual(len(compressed), s.slot - HEADER_BYTES)
        self.assertFalse(s.process(compressed, 801, fail=1))
        source = s.stat(3)
        self.assertFalse(s.valid(source)); self.assertEqual(s.stat(0), 0)
        self.assertEqual(s.stat(9), 1)  # One initial source erase.
        self.assertTrue(s.process(compressed, 801))
        self.assertEqual(s.stat(9), 2)  # Destination erase; source was resumed.
        self.assertEqual(s.pixels(s.stat(4)), raw)
        self.assertEqual((s.stat(0), s.stat(1)), (1, 1))
        self.assertEqual(s.snapshot(0), old); self.assert_ready(s)

    def test_08_multiple_slideshow_images_survive_g5_preload(self):
        s = self.normal
        s.seed(0, 900, SLIDESHOW); s.seed(1, 901, SLIDESHOW)
        old = {slot: s.snapshot(slot) for slot in (0, 1)}
        self.assertTrue(s.process(s.g5(s.pattern()), 902, G5, SLIDESHOW))
        self.assertEqual(len(self.matching(s, SLIDESHOW)), 3)
        for slot, before in old.items(): self.assertEqual(s.snapshot(slot), before)
        self.assertEqual((s.stat(0), s.stat(1)), (1, 0)); self.assert_ready(s)

    def test_09_chroma60_raw_and_g5_keep_67200_byte_size(self):
        s = self.large
        raw = s.pattern(2)
        self.assertEqual(len(raw), 67200)
        for datatype in (RAW2, G5):
            with self.subTest(datatype=datatype):
                s.reset()
                self.assertTrue(s.process(s.g5(raw, 2) if datatype == G5 else raw,
                                          1000 + datatype, datatype))
                slot = s.stat(4)
                self.assertEqual((s.field(slot, 0), s.field(slot, 1)), (67200, RAW2))
                self.assertEqual(s.pixels(slot), raw); self.assert_ready(s)

    def test_10_oversized_and_empty_payloads_do_not_touch_slots(self):
        s = self.normal
        s.seed(1, 1100, current=True); old = s.snapshot(1)
        for payload in (b"", bytes(s.slot - HEADER_BYTES + 1)):
            self.assertFalse(s.process(payload, 1101))
        self.assertEqual((s.stat(0), s.stat(2), s.stat(9)), (0, 0, 0))
        self.assertEqual(s.snapshot(1), old)


    def test_11_preload_retry_is_ack_only_and_arguments_are_exact(self):
        s = self.normal
        unrelated = (2 << 3) | PRELOAD
        for argument in (SPLASH, SLIDESHOW):
            with self.subTest(retry_argument=argument):
                s.reset(); s.seed(0, 1200, current=True); s.seed(1, 1201, unrelated)
                protected = {slot: s.snapshot(slot) for slot in (0, 1)}
                payload = s.g5(s.pattern())
                self.assertTrue(s.process(payload, 1202, G5, argument))
                self.assertEqual(len(self.matching(s, argument)), 1)
                source = s.stat(3)
                # Leave only the invalid compressed-source slot available.
                # A duplicate must not attempt a second source/output allocation.
                for slot in range(s.count):
                    if slot != source and not s.valid(slot):
                        s.seed(slot, 1250 + slot, ((slot + 2) << 3) | PRELOAD)
                self.assertEqual([slot for slot in range(s.count) if not s.valid(slot)], [source])
                before = {slot: s.snapshot(slot) for slot in range(s.count)}
                counters = tuple(s.stat(field) for field in (2, 8, 9, 10))
                acks = s.stat(0)
                self.assertTrue(s.process(payload, 1202, G5, argument))
                self.assertEqual(s.stat(0), acks + 1)
                self.assertEqual(tuple(s.stat(field) for field in (2, 8, 9, 10)), counters)
                self.assertEqual(s.stat(1), 0)
                self.assertEqual(len(self.matching(s, argument)), 1)
                for slot, image in before.items(): self.assertEqual(s.snapshot(slot), image)
                for slot, image in protected.items(): self.assertEqual(s.snapshot(slot), image)
                self.assert_ready(s)

        # The same content version under a different preload argument is a
        # distinct entry; finding an earlier wrong-argument version is insufficient.
        s.reset(); s.seed(0, 1300, current=True); s.seed(1, 1301, unrelated)
        protected = {slot: s.snapshot(slot) for slot in (0, 1)}
        payload = s.g5(s.pattern())
        self.assertTrue(s.process(payload, 1302, G5, SPLASH))
        gets = s.stat(2)
        self.assertTrue(s.process(payload, 1302, G5, SLIDESHOW))
        self.assertGreater(s.stat(2), gets)
        for argument in (SPLASH, SLIDESHOW):
            matches = self.matching(s, argument)
            self.assertEqual(len(matches), 1)
            self.assertEqual(s.field(matches[0], 3), 1302)
        counters = tuple(s.stat(field) for field in (2, 8, 9, 10))
        acks = s.stat(0)
        self.assertTrue(s.process(payload, 1302, G5, SLIDESHOW))
        self.assertEqual(s.stat(0), acks + 1)
        self.assertEqual(tuple(s.stat(field) for field in (2, 8, 9, 10)), counters)

        # Exact argument matching includes the low LUT bits, not only the
        # special-type mask. This update replaces the old splash entry.
        lut_argument = SPLASH | 1
        gets = s.stat(2)
        self.assertTrue(s.process(payload, 1302, G5, lut_argument))
        self.assertGreater(s.stat(2), gets)
        splash = self.matching(s, SPLASH)
        self.assertEqual(len(splash), 1)
        self.assertEqual(s.field(splash[0], 2), lut_argument)
        old = s.snapshot(splash[0]); acks = s.stat(0)
        malformed = struct.pack("<BHHB", 6, s.width, s.height, 1) + b"\0"
        self.assertFalse(s.process(malformed, 1303, G5, lut_argument))
        self.assertEqual(s.snapshot(splash[0]), old)
        self.assertEqual(s.stat(0), acks)
        self.assertTrue(s.process(payload, 1303, G5, lut_argument))
        splash = self.matching(s, SPLASH)
        self.assertEqual(len(splash), 1)
        self.assertEqual(s.field(splash[0], 3), 1303)
        self.assertGreater(s.stat(11), 0)  # Erases checked the new committed header.
        self.assertEqual(len(self.matching(s, SLIDESHOW)), 1)
        for slot, image in protected.items(): self.assertEqual(s.snapshot(slot), image)
        self.assert_ready(s)

    def test_12_cached_commit_finishes_interrupted_special_cleanup(self):
        s = self.normal
        # Power failed after version B's marker commit but before deleting A.
        # No scratch/output slot is available until the old A is removed.
        s.seed(0, 1400, SPLASH, current=True)
        replacement = 3
        for slot in range(1, s.count):
            if slot != replacement:
                s.seed(slot, 1450 + slot, ((slot + 3) << 3) | PRELOAD)
        s.seed(replacement, 1401, SPLASH)
        before = {slot: s.snapshot(slot) for slot in range(1, s.count)}
        self.assertTrue(all(s.valid(slot) for slot in range(s.count)))
        self.assertTrue(s.process(s.g5(s.pattern()), 1401, G5, SPLASH))
        self.assertEqual((s.stat(0), s.stat(1), s.stat(2), s.stat(8)), (1, 0, 0, 0))
        self.assertEqual((s.stat(4), s.stat(5)), (255, replacement))
        self.assertEqual((s.stat(9), s.stat(11)), (1, 1))
        self.assertFalse(s.valid(0))
        for slot, image in before.items(): self.assertEqual(s.snapshot(slot), image)
        self.assertEqual(self.matching(s, SPLASH), [replacement])
        self.assert_ready(s)

    def test_13_interrupted_special_and_slideshow_preloads_resume(self):
        s = self.normal
        rng = random.Random(130073)
        raw = rng.getrandbits(s.width // 8 * s.height * 8).to_bytes(s.width // 8 * s.height, "little")
        compressed = s.g5(raw)
        self.assertGreater(len(compressed), 4096)
        unrelated = (2 << 3) | PRELOAD
        for argument in (SPLASH, SLIDESHOW):
            with self.subTest(resume_argument=argument):
                s.reset(); s.seed(0, 1500, current=True); s.seed(1, 1501, unrelated)
                s.seed(2, 1502, argument)
                protected = {slot: s.snapshot(slot) for slot in (0, 1, 2)}
                self.assertFalse(s.process(compressed, 1503, G5, argument, fail=1))
                source = s.stat(3)
                self.assertEqual(s.stat(4), source)
                self.assertFalse(s.valid(source))
                self.assertEqual((s.stat(0), s.stat(9), s.stat(10)), (0, 1, 0))
                for slot, image in protected.items(): self.assertEqual(s.snapshot(slot), image)
                # Servicing an unrelated cached preload must leave this source,
                # version, block position and remaining-size state resumable.
                partial = s.snapshot(source)
                counters = tuple(s.stat(field) for field in (2, 8, 9, 10))
                self.assertTrue(s.process(compressed, 1501, G5, unrelated))
                self.assertEqual(s.stat(4), source)
                self.assertEqual(s.snapshot(source), partial)
                self.assertEqual(tuple(s.stat(field) for field in (2, 8, 9, 10)), counters)
                self.assertEqual(s.stat(0), 1)
                self.assertTrue(s.process(compressed, 1503, G5, argument))
                self.assertEqual(s.stat(3), source)
                self.assertFalse(s.valid(source))
                self.assertNotEqual(s.stat(4), source)
                self.assertEqual(s.pixels(s.stat(4)), raw)
                self.assertEqual(s.stat(2), (len(compressed) + 4095) // 4096 + 1)
                self.assertEqual(s.stat(9), 3 if argument == SPLASH else 2)
                self.assertEqual((s.stat(0), s.stat(1)), (2, 0))
                for slot in (0, 1): self.assertEqual(s.snapshot(slot), protected[slot])
                if argument == SLIDESHOW:
                    self.assertEqual(s.snapshot(2), protected[2])
                    self.assertEqual(len(self.matching(s, SLIDESHOW)), 2)
                else:
                    self.assertFalse(s.valid(2))
                    self.assertEqual(len(self.matching(s, SPLASH)), 1)
                self.assert_ready(s)

if __name__ == "__main__":
    unittest.main(verbosity=2)
