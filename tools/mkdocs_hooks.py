# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tools/mkdocs_hooks.py
# Purpose: MkDocs hooks of the Mirab website: links from docs/ to other files of the repository open them on GitHub.
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
"""MkDocs hooks for the Mirab website.

The pages in docs/ link to sources, examples and other files of the
repository with relative paths such as ``../integrations/python``, which work
when the Markdown is read on GitHub. The website only holds docs/, so these
links are pointed at the files on the main branch instead.
"""

from __future__ import annotations

import re

# A Markdown link or image target that leaves docs/: ](../path) or ](../path#anchor)
_OUTSIDE_DOCS = re.compile(r"\]\(\.\./([^)\s]+)\)")


def on_page_markdown(markdown, page, config, files):
    repo = config.repo_url.rstrip("/")
    return _OUTSIDE_DOCS.sub(lambda m: f"]({repo}/blob/main/{m.group(1)})", markdown)
