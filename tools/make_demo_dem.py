# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tools/make_demo_dem.py
# Purpose: Writes a synthetic elevation model (GeoTIFF) around the Long Beach example network.
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
"""Developer tool: synthetic DEM for documentation pictures and manual tests.

    python tools/make_demo_dem.py out.tif

The surface is a few smooth hills on a slope that falls towards the south,
in metres, on a 10 ft grid in NAD83 / California zone 5 (EPSG:2229, the
coordinate system of examples/epanet/mirab-demo-longbeach.inp). The file is
tiled, Deflate-compressed with the floating-point predictor, and has a small
NoData area, so it also exercises the GeoTIFF reader. Plain Python only.
"""

from __future__ import annotations

import math
import struct
import sys
import zlib

WIDTH, HEIGHT, TILE = 560, 560, 256
ORIGIN_X, ORIGIN_Y, PIXEL = 6500950.0, 1744300.0, 10.0
NODATA = -9999.0
HILLS = [  # x, y (ftUS), height (m), radius (ft)
    (6503300.0, 1742600.0, 14.0, 900.0),
    (6504800.0, 1741200.0, 9.0, 700.0),
    (6502300.0, 1740600.0, 6.0, 600.0),
    (6505600.0, 1743500.0, 7.0, 500.0),
]


def elevation(x: float, y: float) -> float:
    z = 4.0 + (y - 1738800.0) * 0.0025  # gentle rise to the north
    z += 1.2 * math.sin(x / 420.0) * math.cos(y / 530.0)
    for hx, hy, h, r in HILLS:
        z += h * math.exp(-((x - hx) ** 2 + (y - hy) ** 2) / (2.0 * r * r))
    return z


def tile_bytes(tx: int, ty: int) -> bytes:
    rows = []
    for r in range(ty * TILE, ty * TILE + TILE):
        values = []
        for c in range(tx * TILE, tx * TILE + TILE):
            if r >= HEIGHT or c >= WIDTH:
                values.append(NODATA)
                continue
            x = ORIGIN_X + (c + 0.5) * PIXEL
            y = ORIGIN_Y - (r + 0.5) * PIXEL
            pond = (x - 6505900.0) ** 2 + (y - 1739400.0) ** 2 < 220.0 ** 2
            values.append(NODATA if pond else elevation(x, y))
        # Floating-point predictor: big-endian byte planes, then byte differences.
        raw = b"".join(struct.pack(">f", v) for v in values)
        planes = bytearray(len(raw))
        n = len(values)
        for i in range(n):
            for b in range(4):
                planes[b * n + i] = raw[4 * i + b]
        for i in range(len(planes) - 1, 0, -1):
            planes[i] = (planes[i] - planes[i - 1]) & 0xFF
        rows.append(bytes(planes))
    return zlib.compress(b"".join(rows), 9)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    across = (WIDTH + TILE - 1) // TILE
    down = (HEIGHT + TILE - 1) // TILE
    data = bytearray(b"II*\0\0\0\0\0")
    offsets, counts = [], []
    for ty in range(down):
        for tx in range(across):
            t = tile_bytes(tx, ty)
            offsets.append(len(data))
            counts.append(len(t))
            data += t
            if len(data) % 2:
                data += b"\0"

    geokeys = [1, 1, 0, 4, 1024, 0, 1, 1, 1025, 0, 1, 1, 3072, 0, 1, 2229, 4099, 0, 1, 9001]
    nodata = b"-9999\0"
    fields = [  # tag, type, values
        (256, 4, [WIDTH]), (257, 4, [HEIGHT]), (258, 3, [32]), (259, 3, [8]), (262, 3, [1]),
        (277, 3, [1]), (284, 3, [1]), (317, 3, [3]), (322, 4, [TILE]), (323, 4, [TILE]),
        (324, 4, offsets), (325, 4, counts), (339, 3, [3]),
        (33550, 12, [PIXEL, PIXEL, 0.0]), (33922, 12, [0.0, 0.0, 0.0, ORIGIN_X, ORIGIN_Y, 0.0]),
        (34735, 3, geokeys), (42113, 2, nodata),
    ]
    fmt = {3: "<H", 4: "<I", 12: "<d"}
    ifd = len(data)
    data[4:8] = struct.pack("<I", ifd)
    extra = ifd + 2 + 12 * len(fields) + 4
    tail = bytearray()
    entries = bytearray(struct.pack("<H", len(fields)))
    for tag, typ, values in fields:
        blob = values if typ == 2 else b"".join(struct.pack(fmt[typ], v) for v in values)
        count = len(values)
        entries += struct.pack("<HHI", tag, typ, count)
        if len(blob) <= 4:
            entries += bytes(blob).ljust(4, b"\0")
        else:
            entries += struct.pack("<I", extra + len(tail))
            tail += blob
            if len(tail) % 2:
                tail += b"\0"
    entries += struct.pack("<I", 0)
    data += entries + tail
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    print(f"wrote {sys.argv[1]} ({WIDTH} x {HEIGHT}, {len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
