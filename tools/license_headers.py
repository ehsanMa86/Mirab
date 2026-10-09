#!/usr/bin/env python3
# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tools/license_headers.py
# Purpose: Applies and verifies the GPL-3.0-or-later copyright header on Mirab source files.
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
"""Developer tool: expand and verify Mirab license headers.

New files start with a single marker line such as::

    // @mirab-header Short description of what this file does.

Running ``python tools/license_headers.py --apply`` replaces the marker with the
full copyright and license header in the comment style of the file type.
``--check`` (used by CI) fails when an Mirab source file has no header.
Files under third_party/ or build trees are never touched.
"""

from __future__ import annotations

import argparse
import datetime
import pathlib
import sys

OWNER = "Ehsan Madadi"
PROJECT = "Mirab - Unified Water Infrastructure Modeling Environment"
URL = "https://github.com/ehsanMa86/Mirab"
MARKER = "@mirab-header"
SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"

LICENSE_TEXT = [
    "This program is free software: you can redistribute it and/or modify it",
    "under the terms of the GNU General Public License as published by the Free",
    "Software Foundation, either version 3 of the License, or (at your option)",
    "any later version.",
    "",
    "This program is distributed in the hope that it will be useful, but WITHOUT",
    "ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or",
    "FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for",
    "more details.",
    "",
    "You should have received a copy of the GNU General Public License along",
    "with this program. If not, see <https://www.gnu.org/licenses/>.",
]

BLOCK_STYLES = {".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".mm"}
HASH_STYLES = {".cmake", ".py", ".sh", ".ps1", ".yml", ".yaml", ".txt", ".in", ".desktop", ".nsi"}
BANG_STYLES = {".f90", ".f08", ".F90"}
SEMI_STYLES = {".inp", ".msx"}
XML_STYLES = {".xml", ".qrc", ".ui", ".svg"}

SKIP_DIRS = {"third_party", ".git", "_deps", "build", "out", "dist"}


def comment_style(path: pathlib.Path) -> str | None:
    if path.name == "CMakeLists.txt":
        return "hash"
    if path.name.endswith((".h.in", ".hpp.in", ".c.in", ".cpp.in", ".rc.in", ".rc")):
        return "block"
    if path.name.endswith((".plist.in", ".plist", ".xml.in")):
        return "xml"
    suffix = path.suffix
    if suffix in BLOCK_STYLES:
        return "block"
    if suffix in BANG_STYLES:
        return "bang"
    if suffix in SEMI_STYLES:
        return "semi"
    if suffix in XML_STYLES:
        return "xml"
    if suffix in HASH_STYLES and path.name != "requirements.txt":
        return "hash"
    return None


def header_lines(rel: str, purpose: str, year: int) -> list[str]:
    body = [
        PROJECT,
        URL,
        "",
        f"File:    {rel}",
        f"Purpose: {purpose}",
        "",
        f"Copyright (C) {year} {OWNER}",
        "",
        *LICENSE_TEXT,
        "",
        SPDX,
    ]
    return body


def render(style: str, lines: list[str]) -> str:
    if style == "block":
        out = ["/*"] + [(" * " + l).rstrip() for l in lines] + [" */"]
    elif style == "xml":
        out = ["<!--"] + [("  " + l).rstrip() for l in lines] + ["-->"]
    else:
        prefix = {"hash": "#", "bang": "!", "semi": ";"}[style]
        out = [(f"{prefix} " + l).rstrip() for l in lines]
    return "\n".join(out) + "\n"


def iter_files(root: pathlib.Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel_parts = path.relative_to(root).parts
        if any(p in SKIP_DIRS or p.startswith("build") for p in rel_parts[:-1]):
            continue
        if comment_style(path) is None:
            continue
        yield path


def apply(root: pathlib.Path, year: int) -> int:
    changed = 0
    for path in iter_files(root):
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines(keepends=True)
        for i, line in enumerate(lines[:3]):
            if MARKER in line:
                purpose = line.split(MARKER, 1)[1].strip()
                for closer in ("*/", "-->"):
                    if purpose.endswith(closer):
                        purpose = purpose[: -len(closer)].strip()
                rel = path.relative_to(root).as_posix()
                style = comment_style(path)
                header = render(style, header_lines(rel, purpose, year))
                # Lines before the marker (a shebang or an XML declaration) stay first.
                new = "".join(lines[:i]) + header + "".join(lines[i + 1 :])
                path.write_text(new, encoding="utf-8", newline="\n")
                changed += 1
                break
    print(f"license headers applied to {changed} file(s)")
    return 0


def check(root: pathlib.Path) -> int:
    missing = []
    for path in iter_files(root):
        head = "".join(path.read_text(encoding="utf-8").splitlines(keepends=True)[:30])
        if SPDX not in head or OWNER not in head:
            missing.append(path.relative_to(root).as_posix())
    for m in missing:
        print(f"missing license header: {m}")
    print(f"{len(missing)} file(s) without a license header")
    return 1 if missing else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--apply", action="store_true", help="expand header markers")
    group.add_argument("--check", action="store_true", help="verify every file has a header")
    parser.add_argument("--year", type=int, default=datetime.date.today().year)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    return apply(root, args.year) if args.apply else check(root)


if __name__ == "__main__":
    sys.exit(main())
