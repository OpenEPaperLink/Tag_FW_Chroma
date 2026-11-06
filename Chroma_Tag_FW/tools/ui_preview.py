#!/usr/bin/env python3
"""Preview the OEPL UI screens of a small Chroma display on the PC.

Uses the firmware's own font (common/packed_font.h) and bitmaps
(common/bitmaps.h) and mimics epdpr() / loadRawBitmap() from
common/draw_common.c and board/chroma21/drawing.c, so text widths and bitmap
placement match the tag.  The screens below mirror OEPL/userinterface.c and
must be kept in sync with it by hand.

usage: ui_preview.py out_dir [width height]
"""
import os
import re
import struct
import sys
import zlib

FONT_W, FONT_H = 10, 16
WHITE, BLACK, YELLOW = 0, 1, 2


def c_array(src, name):
    m = re.search(r'\b' + name + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', src, re.S)
    body = re.sub(r'//[^\n]*', '', m.group(1))
    return [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', body)]


class Screen:
    def __init__(self, fw, w, h, bitmaps=None):
        f = open(os.path.join(fw, 'common/packed_font.h')).read()
        self.idx = c_array(f, 'gFontIndexTbl')
        self.dic = c_array(f, 'gFontColumnDictionary')
        self.cols = c_array(f, 'gPackedGlyphColumnIndices')
        self.bmp_src = bitmaps or open(os.path.join(fw, 'common/bitmaps.h')).read()
        self.w, self.h = w, h
        self.px = [[WHITE] * w for _ in range(h)]
        self.defaults()

    def defaults(self):
        self.x = self.y = self.margin = 0
        self.large = False
        self.color = BLACK

    def bitmap(self, name):
        return c_array(self.bmp_src, name)

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def char_w(self, ch):
        w = self.idx[ord(ch) - 0x20] >> 12
        return w * 2 if self.large else w

    def putc(self, ch):
        e = self.idx[ord(ch) - 0x20]
        w, ofs = e >> 12, e & 0xfff
        s = 2 if self.large else 1
        for k in range(w * s):
            bits = self.dic[self.cols[ofs + k // s]]
            for r in range(FONT_H * s):
                if bits & (0x8000 >> (r // s)):
                    self.set(self.x + k, self.y + r, self.color)
        self.x += w * s + 1

    def pr(self, text):
        """epdpr(): \\f center (first char), \\b large toggle, \\n, \\t, \\v, \\r"""
        if text.startswith('\f'):
            large, cx = self.large, 0
            for ch in text:
                if ch == '\b':
                    self.large = not self.large
                elif ch >= ' ':
                    cx += self.char_w(ch) + 1
            self.large = large
            self.x = (self.w - cx) // 2
        for ch in text:
            if ch == '\b':
                self.large = not self.large
            elif ch == '\n':
                self.y += FONT_H * (2 if self.large else 1)
                self.x = self.margin
            elif ch == '\t':
                self.margin = self.x
            elif ch == '\v':
                self.margin = 0
            elif ch == '\r':
                self.color = YELLOW if self.color == BLACK else BLACK
            elif ch >= ' ':
                self.putc(ch)

    def raw_bitmap(self, name, bx, by):
        """loadRawBitmap(): row r of a [W, H] bitmap is logical column
        bx + H - 1 - r, bits MSB first going down from by"""
        b = self.bitmap(name)
        W, H, d = b[0], b[1], b[2:]
        for r in range(H):
            for i in range(W):
                if d[r * (W // 8) + i // 8] & (0x80 >> (i % 8)):
                    self.set(bx + H - 1 - r, by + i, self.color)

    def png(self, path, scale=3):
        rgb = {WHITE: b'\xff\xff\xff', BLACK: b'\x00\x00\x00', YELLOW: b'\xe6\xc8\x00'}
        rows = b''
        for y in range(self.h):
            line = b''.join(rgb[c] * scale for c in self.px[y])
            rows += (b'\x00' + line) * scale

        def chunk(t, b):
            return (struct.pack('>I', len(b)) + t + b +
                    struct.pack('>I', zlib.crc32(t + b) & 0xffffffff))
        open(path, 'wb').write(
            b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', self.w * scale, self.h * scale, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ui_screens import SCREENS
    fw = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    out = sys.argv[1]
    w, h = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (212, 104)
    os.makedirs(out, exist_ok=True)
    for name, fn in SCREENS.items():
        s = Screen(fw, w, h)
        fn(s)
        s.png(os.path.join(out, name + '.png'))
        print('wrote', name)
