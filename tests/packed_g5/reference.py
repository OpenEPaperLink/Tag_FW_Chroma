"""Small independent G5 test encoder; no production or third-party encoder code.

Rows are packed MSB first, with 1 representing white. The encoder keeps a
Python list of transition positions, deliberately unlike the firmware's
packed reference row. The prefix map and fixed-length horizontal fields are
wire-format definitions, not a copy of an encoder implementation.
"""
from collections import Counter

VERTICAL = {-3: "0000010", -2: "000010", -1: "010", 0: "1",
            1: "011", 2: "000011", 3: "0000011"}


def normalize(width, pixels):
    """Set unused low bits of each row to white."""
    pitch = (width + 7) // 8
    result = bytearray(pixels)
    if width % 8:
        for offset in range(pitch - 1, len(result), pitch):
            result[offset] |= (1 << (8 - width % 8)) - 1
    return bytes(result)


def pixels_row(width, values):
    result = bytearray([255] * ((width + 7) // 8))
    for x, value in enumerate(values):
        if not value:
            result[x // 8] &= ~(128 >> (x % 8))
    return bytes(result)


def integer_row(width, value):
    shift = (-width) % 8
    return normalize(width, (value << shift).to_bytes((width + 7) // 8, "big"))


def _transitions(width, row):
    white = True
    changes = []
    for x in range(width):
        pixel = bool(row[x // 8] & (128 >> (x % 8)))
        if pixel != white:
            changes.append(x)
            white = pixel
    # Repeated end sentinels let b1/b2 and a1/a2 use the same rules at the edge.
    return changes + [width] * 4


def pack_bits(bits):
    # AP's encoder emits one final flush byte even on an exact byte boundary.
    bits += "0" * (8 - len(bits) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def encode(width, height, pixels, coverage=None):
    """Return a G5 bitstream (without the six-byte image header)."""
    if not 1 <= width <= 640 or height < 1:
        raise ValueError("unsupported test dimensions")
    pitch = (width + 7) // 8
    if len(pixels) != pitch * height:
        raise ValueError("wrong pixel length")
    counts = coverage if coverage is not None else Counter()
    reference = [width] * 4
    bits = []
    long_bits = width.bit_length()
    for y in range(height):
        current = _transitions(width, pixels[y * pitch:(y + 1) * pitch])
        position, index, white = -1, 0, True
        while position < width:
            ref_index = 0 if white else 1
            while reference[ref_index] < width and reference[ref_index] <= position:
                ref_index += 2
            b1, b2 = reference[ref_index:ref_index + 2]
            a1 = current[index]
            if b2 < a1:
                bits.append("0001")
                counts["pass"] += 1
                position = b2
                continue
            displacement = a1 - b1
            if -3 <= displacement <= 3:
                bits.append(VERTICAL[displacement])
                counts["vertical%+d" % displacement] += 1
                position = a1
                index += 1
                white = not white
                continue
            start = max(position, 0)
            first, second = a1 - start, current[index + 1] - a1
            prefix = 2 * (first >= 8) + (second >= 8)
            bits.extend(("001", format(prefix, "02b"),
                         format(first, "0%db" % (long_bits if prefix & 2 else 3)),
                         format(second, "0%db" % (long_bits if prefix & 1 else 3))))
            counts["horizontal%d" % prefix] += 1
            position = current[index + 1]
            index += 2
        reference = current
    return pack_bits("".join(bits))
