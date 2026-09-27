#!/usr/bin/env python3
"""Regenerate the desktop's small, antialiased ASCII atlas.

Requires Pillow and a local DejaVu Sans Mono font. The checked-in header means
normal Nuvora builds need neither of them. See third_party/DejaVu-FONT-LICENSE.txt.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path,
                        default=Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"))
    parser.add_argument("--output", type=Path, default=Path("user/desktop_font.h"))
    args = parser.parse_args()
    lines = ["/* Generated from DejaVu Sans Mono. See third_party/DejaVu-FONT-LICENSE.txt.",
             " * Regenerate with python3 scripts/gen_desktop_font.py (Pillow required). */",
             "#ifndef NV_DESKTOP_FONT_H", "#define NV_DESKTOP_FONT_H"]
    for scale in range(1, 5):
        font = ImageFont.truetype(str(args.font), 10 * scale)
        ascent, descent = font.getmetrics()
        width, height = round(font.getlength("M")), ascent + descent
        assert width == 6 * scale
        pixel_count = width * height
        lines.append(f"/* scale {scale}: {width}x{height} pixels, two 4-bit coverages per byte. */")
        lines.append(f"static const u8 nv_desktop_font_{scale}[95][{(pixel_count + 1)//2}] = {{")
        for code in range(32, 127):
            canvas = Image.new("L", (width, height))
            ImageDraw.Draw(canvas).text((0, ascent), chr(code), font=font,
                                        fill=255, anchor="ls")
            coverage = [min(15, (value + 8) // 17) for value in canvas.tobytes()]
            if len(coverage) & 1:
                coverage.append(0)
            packed = [(coverage[i] << 4) | coverage[i + 1]
                      for i in range(0, len(coverage), 2)]
            lines.append("    {" + ",".join(f"0x{n:02x}" for n in packed) +
                         f"}}, /* {code} */")
        lines.append("};")
    lines.append("#endif")
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
