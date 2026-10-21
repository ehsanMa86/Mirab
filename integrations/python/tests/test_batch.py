# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_batch.py
# Purpose: Tests of the parallel batch runner.
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
"""Running several variants of a model at once."""

from __future__ import annotations

import mirab


def min_pressure(results):
    return min(min(float(x) for x in row) for row in results.node.values("Pressure", ["J1", "J5", "J9"]))


def test_batch_runs_cases_in_order(epanet_demo, epanet_engine):
    text = epanet_demo.read_text()
    cases = [mirab.Case(f"x{m}", [mirab.option("Demand Multiplier", m)], params={"m": m}) for m in (0.5, 1, 2, 3)]
    seen = []
    out = mirab.run_batch(epanet_demo, cases, engine=epanet_engine, reduce=min_pressure, workers=3,
                          on_done=lambda r: seen.append(r.name))
    assert [r.name for r in out] == ["x0.5", "x1", "x2", "x3"]
    assert sorted(seen) == sorted(r.name for r in out)
    assert all(r.success for r in out), [r.message for r in out]
    values = [r.value for r in out]
    assert values == sorted(values, reverse=True)  # more demand, less pressure
    assert out[1].case.params == {"m": 1}
    assert epanet_demo.read_text() == text
    assert not [f for f in epanet_demo.parent.iterdir() if ".batch-" in f.name]


def test_batch_keeps_results_and_reports_failures(epanet_demo, epanet_engine, tmp_path):
    cases = {"base": [], "bad": [mirab.edit("J1", "Elevation", "not-a-number")],
             "missing": [mirab.edit("NOPE", "Elevation", 1)]}
    out = mirab.run_batch(epanet_demo, cases, engine=epanet_engine, out_dir=tmp_path / "runs")
    by = {r.name: r for r in out}
    assert by["base"].success and by["base"].results_path.is_file()
    assert by["base"].results().period_count == 25
    assert not by["bad"].success and by["bad"].message
    assert not by["missing"].success and "NOPE" in by["missing"].message
