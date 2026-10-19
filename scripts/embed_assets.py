#!/usr/bin/env python3
"""Generate C headers embedding assets/*.ttf as byte arrays."""
import sys
from pathlib import Path

ASSETS = Path(__file__).resolve().parents[1] / "assets"
FONTS = [("roboto_ttf", "Roboto-Regular.ttf"), ("notoemoji_ttf", "NotoEmoji-Regular.ttf")]


def emit(name, src):
    data = src.read_bytes()
    chunks = []
    for i in range(0, len(data), 16):
        chunks.append('"' + "".join("\\x%02x" % b for b in data[i:i + 16]) + '"')
    guard = "ASSETS_%s_H" % name.upper()
    text = (
        "#ifndef %s\n#define %s\n"
        "unsigned char %s[] =\n%s;\n"
        "unsigned int %s_len = %du;\n"
        "#endif\n" % (guard, guard, name, "\n".join(chunks), name, len(data))
    )
    out = ASSETS / (name + ".h")
    out.write_text(text)
    print("%s: %d bytes -> %s" % (src.name, len(data), out.name))


def main():
    for name, fname in FONTS:
        src = ASSETS / fname
        if not src.exists():
            sys.exit("missing %s" % src)
        emit(name, src)


if __name__ == "__main__":
    main()
