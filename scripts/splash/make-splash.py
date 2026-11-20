#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Make the boot splash image (for rpi-splash-screen-support) from a PNG.

    python3 make-splash.py splash.png splash.tga

The picture is stretched to the BMW display (400 x 240), reduced to 224 colours (the
kernel's logo palette) and written as an uncompressed 24-bit TGA, top row first.
Python standard library only.
"""

import struct
import sys
import zlib

WIDTH, HEIGHT = 400, 240
MAX_COLOURS = 224
BLACK = (0, 0, 0)


def unfilter(line, prev, kind, bpp):
    n = len(line)
    if kind == 1:
        for i in range(bpp, n):
            line[i] = (line[i] + line[i - bpp]) & 0xFF
    elif kind == 2:
        for i in range(n):
            line[i] = (line[i] + prev[i]) & 0xFF
    elif kind == 3:
        for i in range(n):
            left = line[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
    elif kind == 4:
        for i in range(n):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            if pa <= pb and pa <= pc:
                line[i] = (line[i] + a) & 0xFF
            elif pb <= pc:
                line[i] = (line[i] + b) & 0xFF
            else:
                line[i] = (line[i] + c) & 0xFF
    elif kind != 0:
        sys.exit(f"unknown PNG filter {kind}")


def read_png(path):
    """Rows of R, G, B bytes. Transparent pixels are drawn on black."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG file")

    header, idat, pos = None, [], 8
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat.append(chunk)
        elif kind == b"IEND":
            break

    width, height, depth, colour, _, _, interlace = header
    bpp = {2: 3, 6: 4}.get(colour)
    if depth != 8 or bpp is None or interlace:
        sys.exit(f"{path}: only 8-bit RGB or RGBA PNG files, not interlaced")

    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        unfilter(line, prev, raw[pos], bpp)
        pos += 1 + stride
        prev = line
        if bpp == 4:
            rgb = bytearray(width * 3)
            for x in range(width):
                alpha = line[4 * x + 3]
                for c in range(3):
                    rgb[3 * x + c] = (line[4 * x + c] * alpha + 127) // 255
            line = rgb
        rows.append(line)
    return width, height, rows


def box_weights(size_in, size_out):
    """For each output pixel, the input pixels it covers and their share."""
    scale = size_in / size_out
    table = []
    for o in range(size_out):
        start, end = o * scale, (o + 1) * scale
        taps, i = [], int(start)
        while i < end and i < size_in:
            share = min(end, i + 1) - max(start, i)
            if share > 0:
                taps.append((i, share / scale))
            i += 1
        table.append(taps)
    return table


def resize(width, height, rows):
    """Area average: each output pixel is the mean of the input area it covers."""
    across = box_weights(width, WIDTH)
    down = box_weights(height, HEIGHT)

    narrow = []
    for row in rows:
        out = [0.0] * (WIDTH * 3)
        for x, taps in enumerate(across):
            r = g = b = 0.0
            for i, w in taps:
                r += row[3 * i] * w
                g += row[3 * i + 1] * w
                b += row[3 * i + 2] * w
            out[3 * x:3 * x + 3] = (r, g, b)
        narrow.append(out)

    pixels = []
    for taps in down:
        acc = [0.0] * (WIDTH * 3)
        for i, w in taps:
            src = narrow[i]
            for k in range(WIDTH * 3):
                acc[k] += src[k] * w
        for x in range(WIDTH):
            pixels.append(tuple(min(255, int(acc[3 * x + c] + 0.5)) for c in range(3)))
    return pixels


def median_cut(counts, boxes_wanted):
    """Split the colours into boxes_wanted boxes; each colour maps to its box's mean."""
    boxes = [list(counts.items())]
    while len(boxes) < boxes_wanted:
        best, best_range, best_channel = None, 0, 0
        for n, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for c in range(3):
                values = [colour[c] for colour, _ in box]
                spread = max(values) - min(values)
                if spread > best_range:
                    best, best_range, best_channel = n, spread, c
        if best is None:
            break
        box = sorted(boxes.pop(best), key=lambda item: item[0][best_channel])
        half, seen, cut = sum(n for _, n in box) / 2, 0, 1
        for cut in range(1, len(box)):
            seen += box[cut - 1][1]
            if seen >= half:
                break
        boxes += [box[:cut], box[cut:]]

    mapping = {}
    for box in boxes:
        total = sum(n for _, n in box)
        mean = tuple(int(sum(colour[c] * n for colour, n in box) / total + 0.5) for c in range(3))
        for colour, _ in box:
            mapping[colour] = mean
    return mapping


def reduce_colours(pixels):
    """At most MAX_COLOURS colours. Black stays pure black (the background)."""
    counts = {}
    for p in pixels:
        counts[p] = counts.get(p, 0) + 1
    if len(counts) <= MAX_COLOURS:
        return pixels
    counts.pop(BLACK, None)
    mapping = median_cut(counts, MAX_COLOURS - 1)
    mapping[BLACK] = BLACK
    return [mapping[p] for p in pixels]


def write_tga(path, pixels):
    # Uncompressed true colour (type 2), 24 bits, origin top left (descriptor bit 5).
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, WIDTH, HEIGHT, 24, 0x20)
    body = bytearray()
    for r, g, b in pixels:
        body += bytes((b, g, r))
    with open(path, "wb") as f:
        f.write(header + body)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    width, height, rows = read_png(sys.argv[1])
    pixels = reduce_colours(resize(width, height, rows))
    write_tga(sys.argv[2], pixels)
    print(f"{sys.argv[2]}: {WIDTH} x {HEIGHT}, {len(set(pixels))} colours")


if __name__ == "__main__":
    main()
