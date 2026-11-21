#!/usr/bin/env python3
"""Draws the grip button images the Pip-Boy shows in VR (VrPipBoyIcons).

Writes fnvvr_button_lgrip.dds, fnvvr_button_rgrip.dds and their glow_
variants: 64x64, uncompressed A8R8G8B8, a white outline and letters on a
transparent background like the game's own button images. They belong in
Data/textures/interface/shared/button/ next to the game's images.

Usage: make_grip_icons.py <output folder>
"""

import math
import os
import struct
import sys

SIZE = 64

FONT = {
    'L': ["11000", "11000", "11000", "11000", "11000", "11000", "11111"],
    'R': ["11110", "11011", "11011", "11110", "11011", "11011", "11011"],
    'G': ["01111", "11000", "11000", "11011", "11001", "11001", "01111"],
}


def stadium_distance(x, y, cx, cy, half_width, half_height):
    dy = max(abs(y - cy) - (half_height - half_width), 0.0)
    return math.hypot(x - cx, dy) - half_width


def draw(letters, glow):
    alpha = [[0.0] * SIZE for _ in range(SIZE)]
    cx = cy = SIZE // 2

    # The grip: an upright rounded outline, about 2.4 pixels wide
    for y in range(SIZE):
        for x in range(SIZE):
            d = stadium_distance(x + 0.5, y + 0.5, cx, cy, 15.5, 21.5)
            alpha[y][x] = max(0.0, min(1.0, 1.6 - abs(d + 0.8)))

    # The letters: a 5x7 font, doubled
    width = len(letters) * 10 + (len(letters) - 1) * 2
    x0, y0 = cx - width // 2, cy - 7

    for i, letter in enumerate(letters):
        for row, bits in enumerate(FONT[letter]):
            for column, bit in enumerate(bits):
                if bit == '1':
                    for dy in range(2):
                        for dx in range(2):
                            alpha[y0 + row * 2 + dy][x0 + i * 12 + column * 2 + dx] = 1.0

    if glow:
        halo = [[0.0] * SIZE for _ in range(SIZE)]

        for y in range(SIZE):
            for x in range(SIZE):
                around = 0.0

                for dy in range(-3, 4):
                    for dx in range(-3, 4):
                        if 0 <= y + dy < SIZE and 0 <= x + dx < SIZE:
                            around = max(around, alpha[y + dy][x + dx] * math.exp(-(dx * dx + dy * dy) / 6.0))

                halo[y][x] = max(alpha[y][x], 0.55 * around)

        alpha = halo

    return alpha


def write_dds(path, alpha):
    header = struct.pack('<4sIIIIIII44sIIIIIIIIIIIII',
        b'DDS ', 124, 0x100F, SIZE, SIZE, SIZE * 4, 0, 1, b'\0' * 44,
        32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,
        0x1000, 0, 0, 0, 0)
    pixels = bytes(value for row in alpha for a in row
                   for value in (255, 255, 255, int(round(a * 255))))

    with open(path, 'wb') as file:
        file.write(header + pixels)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    os.makedirs(sys.argv[1], exist_ok=True)

    for side in 'LR':
        for glow in (False, True):
            name = ('glow_' if glow else '') + 'fnvvr_button_' + side.lower() + 'grip.dds'
            write_dds(os.path.join(sys.argv[1], name), draw(side + 'G', glow))
            print(name)


if __name__ == '__main__':
    main()
