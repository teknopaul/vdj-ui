#!/usr/bin/env python3
# Writes a small RGBA png (a gradient, a transparent corner): gen_png.py FILE [WIDTH HEIGHT]
import struct, sys, zlib

path = sys.argv[1]
w = int(sys.argv[2]) if len(sys.argv) > 2 else 120
h = int(sys.argv[3]) if len(sys.argv) > 3 else 80
rows = b""
for y in range(h):
    rows += b"\0"
    for x in range(w):
        a = 0 if (x < 8 and y < 8) else 255
        rows += bytes((x * 255 // w, y * 255 // h, (x + y) % 256, a))

def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(path, "wb").write(png)
