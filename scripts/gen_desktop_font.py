#!/usr/bin/env python3
"""Regenerate native-size proportional UI and fixed-cell text atlases.

Requires Pillow and local DejaVu fonts. The checked-in header means
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
    parser.add_argument("--ui-font", type=Path,
                        default=Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"))
    parser.add_argument("--bold-font", type=Path,
                        default=Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"))
    args = parser.parse_args()
    lines = ["/* Generated from DejaVu Sans and Sans Mono. See third_party/DejaVu-FONT-LICENSE.txt.",
             " * Regenerate with python3 scripts/gen_desktop_font.py (Pillow required). */",
             "#ifndef NV_DESKTOP_FONT_H", "#define NV_DESKTOP_FONT_H"]
    for face, source, size in [("mono", args.font, 10), ("ui", args.ui_font, 14),
                               ("bold", args.bold_font, 16), ("editor", args.font, 13)]:
        for scale in range(1, 5):
            font = ImageFont.truetype(str(source), size * scale)
            ascent, descent = font.getmetrics()
            advances = [round(font.getlength(chr(code))) for code in range(32, 127)]
            bearings = [min(0, font.getbbox(chr(code))[0]) for code in range(32, 127)]
            width = max(advances + [font.getbbox(chr(code))[2] - bearings[code - 32]
                                    for code in range(32, 127)])
            height = ascent + descent
            if face == "mono":
                assert all(n == 6 * scale for n in advances)
                width = 6 * scale
            elif face == "editor":
                assert width <= 8 * scale
                width = 8 * scale
                advances = [width] * 95
            lines.append(f"#define NV_FONT_{face.upper()}_{scale}_WIDTH {width}u")
            lines.append(f"#define NV_FONT_{face.upper()}_{scale}_HEIGHT {height}u")
            lines.append(f"static const u8 nv_font_{face}_{scale}_advance[95] = {{" +
                         ",".join(map(str, advances)) + "};")
            lines.append(f"static const signed char nv_font_{face}_{scale}_bearing[95] = {{" +
                         ",".join(map(str, bearings)) + "};")
            lines.append(f"static const u8 nv_font_{face}_{scale}[95][{width * height}] = {{")
            for code in range(32, 127):
                canvas = Image.new("L", (width, height))
                ImageDraw.Draw(canvas).text((-bearings[code - 32], ascent), chr(code), font=font,
                                            fill=255, anchor="ls")
                lines.append("    {" + ",".join(f"0x{n:02x}" for n in canvas.tobytes()) +
                             f"}}, /* {code} */")
            lines.append("};")
    lines.append("#endif")
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
