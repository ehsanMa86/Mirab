# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_scenarios.py
# Purpose: Scenarios from Python: reading, editing and running a scenario of a project.
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
"""Scenarios from Python: reading, editing and running a scenario of a project."""

from __future__ import annotations

import json

import mirab
from mirab.project import sidecar_of


def test_list_and_open_scenario(epanet_demo):
    names = [s["name"] for s in mirab.scenarios(epanet_demo)]
    assert names == ["Pipe upsizing", "Demand growth", "Growth with upsizing"]
    base = mirab.Project.open(epanet_demo)
    upsized = mirab.Project.open(epanet_demo, scenario="Pipe upsizing")
    assert base.links["P5"].diameter == 100
    assert upsized.links["P5"].diameter == 150
    child = mirab.open_project(epanet_demo, scenario="Growth with upsizing")
    assert child.option("DEMAND MULTIPLIER") == "1.3"


def test_save_records_scenario_changes(epanet_demo):
    text = epanet_demo.read_text()
    p = mirab.Project.open(epanet_demo, scenario="Demand growth")
    p.links["P1"].diameter = 300
    p.save()
    # The input file is unchanged; the scenario has one more change.
    assert epanet_demo.read_text() == text
    stored = json.loads(sidecar_of(epanet_demo).read_text())
    growth = next(s for s in stored["scenarios"] if s["name"] == "Demand growth")
    assert any(c["key"] == "P1" and c["to"] == "300" for c in growth["changes"])
    assert p.links["P1"].diameter == 300
    assert mirab.Project.open(epanet_demo).links["P1"].diameter == 200


def test_run_scenario(epanet_demo, epanet_engine):
    result = mirab.run(epanet_demo, engine=epanet_engine, scenario="Pipe upsizing", check=True)
    assert result.results_path is not None and result.results_path.name == "mirab-demo.Pipe-upsizing.anr"
    base = mirab.run(epanet_demo, engine=epanet_engine, check=True)
    up = result.results.node.series("Pressure", "J9")
    down = base.results.node.series("Pressure", "J9")
    assert list(up) != list(down)
    p = mirab.Project.open(epanet_demo, scenario="Pipe upsizing")
    assert p.run(engine=epanet_engine).results_path.name == "mirab-demo.Pipe-upsizing.anr"
