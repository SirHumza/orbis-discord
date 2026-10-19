#!/usr/bin/env python3
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PNG_PIXELS = [
    [(255, 0, 0, 255), (0, 255, 0, 255)],
    [(0, 0, 255, 255), (255, 255, 255, 255)],
]


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def make_png():
    raw = b"".join(
        b"\x00" + b"".join(bytes(px) for px in row)
        for row in PNG_PIXELS
    )
    ihdr = struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def lzw_pixels(index):
    codes = []
    for _ in range(4):
        codes.extend((4, index))
    codes.append(5)
    acc = 0
    nbits = 0
    out = bytearray()
    for code in codes:
        acc |= code << nbits
        nbits += 3
        while nbits >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nbits -= 8
    if nbits:
        out.append(acc & 0xFF)
    return bytes(out)


def gif_frame(index, delay_cs):
    gce = (b"\x21\xf9\x04\x00" + struct.pack("<H", delay_cs) +
           b"\x00\x00")
    descriptor = b"\x2c" + struct.pack("<HHHH", 0, 0, 2, 2) + b"\x00"
    data = lzw_pixels(index)
    return gce + descriptor + b"\x02" + bytes([len(data)]) + data + b"\x00"


def make_gif(frames, loop):
    out = bytearray(b"GIF89a")
    out += struct.pack("<HH", 2, 2)
    out += bytes([0xF1, 0x00, 0x00])
    out += bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255))
    if loop:
        out += b"\x21\xff\x0bNETSCAPE2.0\x03\x01" + struct.pack("<H", 0) + b"\x00"
    for index, delay_cs in frames:
        out += gif_frame(index, delay_cs)
    out += b";"
    return bytes(out)


def c_array(name, data):
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 12):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",")
    lines.append("};")
    lines.append(f"const unsigned int {name}_len = sizeof {name};")
    return "\n".join(lines)


def main():
    fixtures = {
        "png_2x2": make_png(),
        "gif_anim_2f": make_gif([(0, 10), (2, 10)], True),
        "gif_static": make_gif([(1, 0)], False),
        "bad_bytes": bytes([0x00, 0x01, 0x02, 0x03, 0xFF, 0xD8, 0x00, 0x13, 0x37]),
    }

    header = ["#ifndef FIXTURES_H", "#define FIXTURES_H", ""]
    for name in fixtures:
        header.append(f"extern const unsigned char {name}[];")
        header.append(f"extern const unsigned int {name}_len;")
    header += ["", "#endif", ""]

    source = ['#include "fixtures.h"', ""]
    source.extend(c_array(name, data) for name, data in fixtures.items())

    (ROOT / "tests" / "fixtures.h").write_text("\n".join(header))
    (ROOT / "tests" / "fixtures.c").write_text("\n\n".join(source) + "\n")


if __name__ == "__main__":
    main()
