#!/usr/bin/env python3
"""Self-contained production G5 tests. Run from any directory on Linux/WSL."""
import sys
sys.dont_write_bytecode = True
import argparse
import ctypes
import json
import os
from pathlib import Path
import random
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
from collections import Counter
from reference import encode, integer_row, normalize, pack_bits, pixels_row

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
COMMON = REPO / "Chroma_Tag_FW" / "common"
MODE_COVERAGE = Counter()


def random_bytes(rng, length):
    return rng.getrandbits(length * 8).to_bytes(length, "little")


def header(width, height, planes=1):
    return struct.pack("<BHHB", 6, width, height, planes)


class Decoder:
    def __init__(self, path):
        self.lib = ctypes.CDLL(str(path))
        self.lib.test_bits.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_uint16, ctypes.c_uint16, ctypes.c_void_p]
        self.lib.test_bits.restype = ctypes.c_int
        self.lib.test_image.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                        ctypes.c_void_p, ctypes.c_size_t]
        self.lib.test_image.restype = ctypes.c_int
        self.lib.test_stat.argtypes = [ctypes.c_uint]
        self.lib.test_stat.restype = ctypes.c_uint32

    def call(self, source, width, height, image=False):
        size = (width + 7) // 8 * height
        data = ctypes.create_string_buffer(source)
        guard = bytes([0xA5]) * 16
        output = ctypes.create_string_buffer(guard + bytes([0xA5]) * size + guard, size + 32)
        pointer = ctypes.c_void_p(ctypes.addressof(output) + 16)
        if image:
            result = self.lib.test_image(data, len(source), pointer, size)
        else:
            result = self.lib.test_bits(data, len(source), width, height, pointer)
        assert output.raw[:16] == guard and output.raw[-16:] == guard, "output guard overwritten"
        return result, output.raw[16:-16]

    def stat(self, index):
        return self.lib.test_stat(index)


def compile_library(directory, name, width=640, height=384, slot=0x17000, extra=()):
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    command = compiler + ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-DG5_HOST_TEST",
                           "-DTEST_SCREEN_WIDTH=%d" % width, "-DTEST_SCREEN_HEIGHT=%d" % height,
                           "-DTEST_SLOT_BYTES=%d" % slot,
                           "-include", str(HERE / "include/platform.h"),
                           "-I" + str(HERE / "include"), "-I" + str(COMMON)]
    output = Path(directory) / (name + ".so")
    subprocess.run(command + list(extra) + ["-shared", "-fPIC", str(HERE / "host_harness.c"),
                                           str(COMMON / "g5.c"), "-o", str(output)], check=True)
    return Decoder(output)


def patterns(width):
    pitch = (width + 7) // 8
    rows = [bytes([255]) * pitch, bytes([0]) * pitch,
            bytes([0xAA]) * pitch, bytes([0x55]) * pitch]
    for shift in range(-3, 4):
        edge = max(0, min(width, width // 2 + shift))
        rows.append(pixels_row(width, [x >= edge for x in range(width)]))
        rows.append(pixels_row(width, [not (max(0, width // 4 + shift) <= x <
                                               min(width, 3 * width // 4 + shift))
                                       for x in range(width)]))
    return normalize(width, b"".join(rows))


class G5Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="chroma-g5-tests-")
        cls.decoder = compile_library(cls.temp.name, "normal")
        cls.noskip = compile_library(cls.temp.name, "noskip", extra=("-DG5P_NO_BYTE_SKIP",))
        cls.boards = {(640, 384): cls.decoder}
        for width, height, slot in [(128, 296, 0x4000), (400, 300, 0xB000), (600, 448, 0x17000)]:
            cls.boards[width, height] = compile_library(cls.temp.name, str(width), width, height, slot)
        cls.small_slot = compile_library(cls.temp.name, "small_slot", slot=1024)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, width, height, raw, decoder=None):
        raw = normalize(width, raw)
        stream = encode(width, height, raw, MODE_COVERAGE)
        result, output = (decoder or self.decoder).call(stream, width, height)
        if result or output != raw:
            self.fail("G5 mismatch: %dx%d, status %d, stream %s" %
                      (width, height, result, stream.hex()))
        return stream

    def test_01_frozen_ap_interoperability(self):
        fixtures = json.loads((HERE / "fixtures/ap_streams.json").read_text())["fixtures"]
        for item in fixtures:
            width, height = item["width"], item["height"]
            raw, stream = bytes.fromhex(item["pixels_hex"]), bytes.fromhex(item["g5_hex"])
            self.assertEqual(encode(width, height, raw), stream)
            result, output = self.decoder.call(stream, width, height)
            self.assertEqual((result, output), (0, raw))

    def test_02_exhaustive_short_row_pairs(self):
        count = 0
        for width in range(1, 9):
            rows = [integer_row(width, value) for value in range(1 << width)]
            for first in rows:
                for second in rows:
                    self.check(width, 2, first + second)
                    count += 1
        self.assertEqual(count, 87380)

    def test_03_modes_boundaries_and_random_rows(self):
        rng = random.Random(20261028)
        widths = [1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
                  127, 128, 129, 295, 296, 297, 399, 400, 401, 599, 600, 601, 639, 640]
        for width in widths:
            raw = patterns(width)
            self.check(width, 18, raw)
            self.check(width, 18, raw, self.noskip)
            pitch = (width + 7) // 8
            for _ in range(12):
                height = rng.choice([1, 2, 3, 8, 17])
                self.check(width, height, random_bytes(rng, pitch * height))
            for probability in [0.01, 0.1, 0.9, 0.99]:
                raw = b"".join(pixels_row(width, [rng.random() < probability for _ in range(width)])
                               for _ in range(16))
                self.check(width, 16, raw)
        expected = {"pass"} | {"vertical%+d" % n for n in range(-3, 4)} | {
            "horizontal%d" % n for n in range(4)}
        self.assertTrue(expected <= MODE_COVERAGE.keys(), MODE_COVERAGE)

    def test_04_dense_flips_and_full_two_plane_images(self):
        rng = random.Random(4681)
        for width, half_height in [(128, 296), (296, 128), (400, 300), (600, 448), (640, 384)]:
            pitch, height = (width + 7) // 8, half_height * 2
            for kind in ["noise", "alternating", "repeated"]:
                if kind == "noise":
                    raw = random_bytes(rng, pitch * height)
                elif kind == "alternating":
                    raw = b"".join(bytes([0xAA if y % 2 else 0x55]) * pitch for y in range(height))
                else:
                    raw = bytes([0xAA]) * pitch * height
                self.check(width, height, raw)

    def test_05_streaming_image_headers_nor_and_plane_boundary(self):
        rng = random.Random(9481)
        for (width, height), decoder in self.boards.items():
            pitch = width // 8
            for planes in [1, 2]:
                # Different planes, including a sharp boundary, stay in one decoder session.
                raw = random_bytes(rng, pitch * height * planes)
                compressed = encode(width, height * planes, raw)
                result, output = decoder.call(header(width, height, planes) + compressed,
                                               width, height * planes, image=True)
                self.assertEqual((result, output), (0x20 if planes == 1 else 0x21, raw))
                self.assertEqual(decoder.stat(1), height * planes)
                self.assertEqual(decoder.stat(5), 1)
                self.assertGreater(decoder.stat(2), 0)  # 128-byte refills
                self.assertGreater(decoder.stat(4), 0)  # unaligned NOR page crossings

    def test_06_header_rejection_preserves_destination_metadata(self):
        width, height = 640, 384
        valid = header(width, height) + encode(width, height, bytes([255]) * (80 * height))
        invalid = [valid[:n] for n in range(6)]
        invalid += [bytes([size]) + valid[1:] for size in [0, 5, 7, 255]]
        invalid += [header(w, h, planes) + valid[6:] for w, h, planes in
                    [(0, height, 1), (width - 8, height, 1), (width + 8, height, 1),
                     (width, 0, 1), (width, height - 1, 1), (width, height + 1, 1),
                     (width, height, 0), (width, height, 3), (width, height, 255)]]
        for stream in invalid:
            result, _ = self.decoder.call(stream, width, height, image=True)
            self.assertEqual(result, 0)
            self.assertEqual(self.decoder.stat(1), 0)
        result, _ = self.small_slot.call(valid, width, height, image=True)
        self.assertEqual(result, 0)
        self.assertEqual(self.small_slot.stat(1), 0)
        result, _ = self.decoder.call(header(width, height), width, height, image=True)
        self.assertEqual(result, 0)
        self.assertEqual(self.decoder.stat(1), 0)
        # C guards also cover the 22-byte slot header preceding each payload.

    def test_07_truncated_invalid_and_random_streams(self):
        rng = random.Random(12345)
        for width, height in [(1, 3), (7, 8), (128, 17), (296, 8), (600, 8), (640, 17)]:
            raw = normalize(width, random_bytes(rng, (width + 7) // 8 * height))
            stream = encode(width, height, raw)
            cuts = range(len(stream)) if len(stream) < 256 else sorted(
                {0, 1, len(stream) - 1} | {rng.randrange(len(stream)) for _ in range(128)})
            for cut in cuts:
                result, output = self.decoder.call(stream[:cut], width, height)
                self.assertIn(result, [0, -1, -2, -3])
                if result == 0:
                    # A final encoder flush byte can be redundant. Successful shorter
                    # prefixes must still reconstruct every requested pixel exactly.
                    self.assertEqual(output, raw)
        for stream in [b"", b"\x00", bytes.fromhex("2000"), bytes.fromhex("03"), bytes.fromhex("3f84")]:
            result, _ = self.decoder.call(stream, 16, 1)
            self.assertLess(result, 0)
        for width, height in [(0, 1), (1, 0), (641, 1), (65535, 1)]:
            result, _ = self.decoder.call(b"\xff", width, height)
            self.assertLess(result, 0)
        for _ in range(20000):
            width = rng.choice([1, 7, 8, 9, 127, 128, 129, 295, 296, 400, 599, 600, 639, 640])
            result, _ = self.decoder.call(random_bytes(rng, rng.randrange(65)), width,
                                           rng.choice([1, 2, 3, 8]))
            self.assertIn(result, [0, -1, -2, -3])


def run_sanitizers():
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    rng = random.Random(6182)
    with tempfile.TemporaryDirectory(prefix="chroma-g5-asan-") as directory:
        corpus = Path(directory) / "corpus.bin"
        with corpus.open("wb") as output:
            for width, height in [(1, 2), (7, 17), (8, 17), (9, 17), (128, 592),
                                   (296, 256), (400, 600), (600, 896), (640, 384), (640, 768)]:
                pitch = (width + 7) // 8
                for raw in [random_bytes(rng, pitch * height), bytes([0xAA]) * pitch * height]:
                    raw = normalize(width, raw)
                    stream = encode(width, height, raw)
                    output.write(struct.pack("<4I", width, height, len(raw), len(stream)))
                    output.write(raw)
                    output.write(stream)
        executable = Path(directory) / "sanitizers"
        subprocess.run(compiler + ["-std=c11", "-O1", "-g", "-DG5_HOST_TEST",
                                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                    "-include", str(HERE / "include/platform.h"),
                                    "-I" + str(HERE / "include"), "-I" + str(COMMON),
                                    str(HERE / "sanitize_main.c"), str(HERE / "host_harness.c"),
                                    str(COMMON / "g5.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(corpus)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="also run ASan/UBSan with exact source/output allocations")
    args = parser.parse_args()
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    if not compiler or not shutil.which(compiler[0]):
        parser.error("a host C compiler is required (set HOST_CC, for example HOST_CC=clang)")
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(G5Tests))
    if result.wasSuccessful() and args.sanitize:
        run_sanitizers()
    sys.exit(0 if result.wasSuccessful() else 1)
