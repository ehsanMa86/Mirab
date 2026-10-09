#!/usr/bin/env python3
# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tools/make_icons.py
# Purpose: Developer tool that draws the Mirab application icon and writes PNG, ICO, ICNS and SVG files.
#
# Copyright (C) 2026 Ehsan Madadi
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerates every icon file from one geometric definition.

Requires Pillow on the developer machine only:  python tools/make_icons.py
"""

import math
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = 1024
TOP = (11, 79, 138)       # deep blue
BOTTOM = (24, 163, 168)   # teal
PIPE = (11, 95, 143)
NODE = (24, 163, 168)


def drop_polygon(cx=512.0, cy=600.0, r=250.0, apex_y=170.0, steps=120):
    d = cy - apex_y
    a = math.acos(r / d)
    start = -math.pi / 2 + a          # right tangent point, measured from +x axis, y down
    end = -math.pi / 2 - a + 2 * math.pi
    pts = [(cx, apex_y)]
    for i in range(steps + 1):
        t = start + (end - start) * i / steps
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


NODES = [(418, 575), (612, 535), (522, 712)]
EDGES = [(0, 1), (1, 2), (2, 0)]


def draw(size):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        gd.line([(0, y), (S, y)], fill=tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)) + (255,))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([24, 24, S - 24, S - 24], radius=220, fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.polygon(drop_polygon(), fill=(255, 255, 255, 255))
    for a, b in EDGES:
        d.line([NODES[a], NODES[b]], fill=PIPE + (255,), width=26)
    for x, y in NODES:
        d.ellipse([x - 40, y - 40, x + 40, y + 40], fill=NODE + (255,), outline=PIPE + (255,), width=14)
    return img.resize((size, size), Image.LANCZOS)


def svg():
    pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in drop_polygon(steps=60))
    edges = "".join(
        f'<line x1="{NODES[a][0]}" y1="{NODES[a][1]}" x2="{NODES[b][0]}" y2="{NODES[b][1]}" '
        f'stroke="#0b5f8f" stroke-width="26" stroke-linecap="round"/>' for a, b in EDGES)
    nodes = "".join(
        f'<circle cx="{x}" cy="{y}" r="33" fill="#18a3a8" stroke="#0b5f8f" stroke-width="14"/>' for x, y in NODES)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- Mirab application icon. Copyright (C) 2026 Ehsan Madadi. SPDX-License-Identifier: GPL-3.0-or-later -->
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#0b4f8a"/>
      <stop offset="1" stop-color="#18a3a8"/>
    </linearGradient>
  </defs>
  <rect x="24" y="24" width="976" height="976" rx="220" fill="url(#bg)"/>
  <polygon points="{pts}" fill="#ffffff"/>
  {edges}
  {nodes}
</svg>
"""


def main():
    big = draw(1024)
    (ROOT / "packaging/icons").mkdir(parents=True, exist_ok=True)
    big.save(ROOT / "packaging/icons/mirab-1024.png")
    for s in (512, 256, 128, 64, 48, 32, 16):
        draw(s).save(ROOT / f"packaging/icons/mirab-{s}.png")
    draw(256).save(ROOT / "src/app/resources/mirab.png")
    big.save(ROOT / "packaging/windows/mirab.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(ROOT / "packaging/macos/mirab.icns")
    (ROOT / "packaging/icons/mirab.svg").write_text(svg(), encoding="utf-8")
    print("icons written")


if __name__ == "__main__":
    main()
