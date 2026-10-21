# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/conftest.py
# Purpose: Shared fixtures of the Python package tests: mirab-cli, example models and engines.
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
"""Fixtures for the tests of the ``mirab`` package.

The tests run the real ``mirab-cli``: set ``MIRAB_CLI`` to the program (the
ctest registration does) or put it on ``PATH``. Without it every test that
needs it is skipped. ``MIRAB_EXAMPLES`` overrides the examples folder.
"""

from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Callable, Set

import pytest

import mirab

REPO = Path(__file__).resolve().parents[3]


@pytest.fixture(scope="session")
def cli() -> mirab.Cli:
    program = mirab.find_cli(required=False)
    if program is None:
        pytest.skip("mirab-cli not found (set MIRAB_CLI)")
    return mirab.set_cli(program)


@pytest.fixture(scope="session")
def examples() -> Path:
    path = Path(os.environ.get("MIRAB_EXAMPLES", REPO / "examples"))
    if not (path / "epanet" / "mirab-demo.inp").is_file():
        pytest.skip(f"example models not found in {path}")
    return path


@pytest.fixture(scope="session")
def available_engines(cli: mirab.Cli) -> Set[str]:
    return {e["id"] for e in mirab.engines() if e.get("available")}


@pytest.fixture
def copy_example(examples: Path, tmp_path: Path) -> Callable[[str], Path]:
    """Copies an example (and its settings file) into the test's folder."""

    def copy(rel: str) -> Path:
        src = examples / rel
        dst = tmp_path / src.name
        shutil.copyfile(src, dst)
        side = src.with_name(src.name + ".mirab.json")
        if side.is_file():
            shutil.copyfile(side, dst.with_name(dst.name + ".mirab.json"))
        return dst

    return copy


@pytest.fixture
def epanet_demo(cli: mirab.Cli, copy_example: Callable[[str], Path]) -> Path:
    return copy_example("epanet/mirab-demo.inp")


@pytest.fixture
def swmm_demo(cli: mirab.Cli, copy_example: Callable[[str], Path]) -> Path:
    return copy_example("swmm/mirab-urban.inp")


@pytest.fixture
def epanet_engine(available_engines: Set[str]) -> str:
    for engine in ("epanet-2.3-owa", "epanet-2.2-epa"):
        if engine in available_engines:
            return engine
    pytest.skip("no EPANET engine installed")


@pytest.fixture
def swmm_engine(available_engines: Set[str]) -> str:
    if "swmm-5.2-epa" not in available_engines:
        pytest.skip("EPA SWMM 5.2 is not installed")
    return "swmm-5.2-epa"
