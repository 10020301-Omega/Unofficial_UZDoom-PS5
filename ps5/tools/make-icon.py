#!/usr/bin/env python3
# PS5-UZDOOM - the home-screen icon, drawn as a DOS text-mode screen.
#
#   make-icon.py <font_cp437.inc> <icon0.png>
#
# 32 x 16 characters of the launcher's font (ps5/tools/make-font.py) at twice
# their size: 512 x 512 pixels. The picture is the port's own; it uses no
# artwork from the games or from the UZDoom project.
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import re
import sys

from PIL import Image

PALETTE = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170), (170, 85, 0),
           (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
           (255, 85, 255), (255, 255, 85), (255, 255, 255)]
COLS, ROWS = 32, 16


def main():
    font_path, out_path = sys.argv[1], sys.argv[2]
    font = []
    for line in open(font_path):
        values = re.findall(r"0x([0-9a-f]{2})", line.split("//")[0])
        if len(values) == 16:
            font.append([int(v, 16) for v in values])
    assert len(font) == 256

    cells = [[(0xB0, 0x19)] * COLS for _ in range(ROWS)]

    def put(x, y, ch, attr):
        if 0 <= x < COLS and 0 <= y < ROWS:
            cells[y][x] = (ch if isinstance(ch, int) else ord(ch), attr)

    def text(x, y, string, attr):
        for i, ch in enumerate(string):
            put(x + i, y, ch, attr)

    def box(x, y, w, h, attr):
        for row in range(y, y + h):
            for col in range(x, x + w):
                put(col, row, " ", attr)
        for col in range(x + 1, x + w - 1):
            put(col, y, 0xCD, attr)
            put(col, y + h - 1, 0xCD, attr)
        for row in range(y + 1, y + h - 1):
            put(x, row, 0xBA, attr)
            put(x + w - 1, row, 0xBA, attr)
        put(x, y, 0xC9, attr)
        put(x + w - 1, y, 0xBB, attr)
        put(x, y + h - 1, 0xC8, attr)
        put(x + w - 1, y + h - 1, 0xBC, attr)
        # the drop shadow
        for row in range(y + 1, y + h + 1):
            for col in (x + w, x + w + 1):
                if 0 <= col < COLS and 0 <= row < ROWS:
                    cells[row][col] = (cells[row][col][0], 0x08)
        for col in range(x + 2, x + w):
            if y + h < ROWS:
                cells[y + h][col] = (cells[y + h][col][0], 0x08)

    for col in range(COLS):
        put(col, 0, " ", 0x70)
        put(col, ROWS - 1, " ", 0x70)
    text(13, 0, "Setup", 0x70)
    text(1, ROWS - 1, "Cross", 0x74)
    text(7, ROWS - 1, "Start", 0x70)
    box(5, 3, 22, 9, 0x70)
    text(12, 5, "U Z D o o m", 0x74)
    text(8, 7, "PlayStation 5", 0x70)
    for col in range(7, 25):
        put(col, 9, " ", 0x4F)
    text(8, 9, "C:\\>uzdoom", 0x4F)
    put(18, 9, 0xDB, 0x4F)

    image = Image.new("RGB", (COLS * 8, ROWS * 16))
    pixels = image.load()
    for row in range(ROWS):
        for col in range(COLS):
            ch, attr = cells[row][col]
            fg, bg = PALETTE[attr & 15], PALETTE[attr >> 4]
            for line in range(16):
                bits = font[ch][line]
                for bit in range(8):
                    pixels[col * 8 + bit, row * 16 + line] = fg if bits & (0x80 >> bit) else bg
    image.resize((512, 512), Image.NEAREST).save(out_path)


main()
