# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_project.py
# Purpose: Tests of mirab.Project: model data, edits, saving and runs.
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
"""Opening, editing, saving and running projects."""

from __future__ import annotations

import pytest

import mirab


def test_open_and_read(epanet_demo):
    p = mirab.Project.open(epanet_demo)
    assert p.dialect == "EPANET"
    assert p.units["flow"] == "LPS"
    assert len(p.nodes) == 11 and len(p.links) == 14
    j1 = p.nodes["J1"]
    assert j1.kind == "Junction"
    assert j1.elevation == 20 and j1["Elevation"] == 20 and j1.get("ELEVATION") == 20
    assert j1.pattern == "PAT1"
    assert (j1.x, j1.y) == (1000, 3000)
    tank = p.nodes["T1"]
    assert tank.init_level == 4 and tank.vol_curve is None
    assert p.links["P13"].to_node == "T1"
    assert [n.id for n in p.nodes.of_kind("Reservoir")] == ["R1"]
    assert p.option("duration") == "24:00"
    assert p.option("demand multiplier", section="OPTIONS") == "1.0"
    assert len(p.pattern("PAT1")) == 24
    assert p.curve("C1") == [(60.0, 40.0)]
    assert p.element("PU1").properties["Parameters"] == "HEAD C1"
    with pytest.raises(AttributeError):
        _ = j1.height
    with pytest.raises(KeyError):
        p.nodes["J99"]


def test_edit_and_save_preserves_file(epanet_demo, tmp_path):
    original = epanet_demo.read_text().splitlines()
    p = mirab.Project.open(epanet_demo)
    p.nodes["J1"].elevation = 12.5
    p.links["P1"].diameter = 250
    p.set("J3", "Coefficient", 0.4, section="EMITTERS")
    p.set_option("Demand Model", "PDA")
    p.set_pattern("PAT1", [1.0] * 24)
    assert p.modified and len(p.edits) == 5
    assert p.nodes["J1"].elevation == 12.5  # visible before saving
    with pytest.raises(AttributeError):
        p.nodes["J1"].height = 3

    copy = p.write_copy(tmp_path / "copy.inp")
    assert p.modified and epanet_demo.read_text().splitlines() == original
    assert mirab.Project.open(copy).nodes["J1"].elevation == 12.5

    p.save()
    assert not p.modified
    saved = epanet_demo.read_text().splitlines()
    assert p.nodes["J1"].elevation == 12.5
    assert p.nodes["J3"].sections["EMITTERS"][0]["Coefficient"] == 0.4
    assert p.nodes["J3"].coefficient == 0.4
    assert p.option("DEMAND MODEL") == "PDA"
    assert p.pattern("PAT1") == [1.0] * 24
    # Comments and untouched rows are kept verbatim.
    kept = set(saved)
    assert all(line in kept for line in original if line.startswith(";") or " P13 " in line)

    other = tmp_path / "other.inp"
    assert p.save(other) == other and p.path == other


def test_swmm_columns_of_other_sections(swmm_demo):
    p = mirab.Project.open(swmm_demo)
    c1 = p.links["C1"]
    assert c1.shape == "CIRCULAR" and c1.geom1 == 0.6
    c1.geom1 = 0.75
    p.subcatchments["S1"].n_perv = 0.2
    p.set_rows("TIMESERIES", [["0:00", 0], ["0:30", 30], ["1:00", 0]], id="Storm1")
    p.save()
    assert p.links["C1"].geom1 == 0.75
    assert p.subcatchments["S1"].n_perv == 0.2
    assert p.timeseries("Storm1") == [["0:00", "0"], ["0:30", "30"], ["1:00", "0"]]
    assert p.gages["RG1"].format == "INTENSITY"


def test_run_with_unsaved_edits(epanet_demo, epanet_engine):
    p = mirab.Project.open(epanet_demo)
    base = p.run(engine=epanet_engine)
    assert base.success and base.results_path == epanet_demo.with_suffix(".anr")
    low = base.results.node.value(5, "Pressure", "J5")

    p.set_option("Demand Multiplier", 2.0)
    text = epanet_demo.read_text()
    r = p.run(engine=epanet_engine)
    assert r.success and r.results_path is None  # read into memory, scratch files removed
    assert r.results.node.value(5, "Pressure", "J5") < low
    assert epanet_demo.read_text() == text
    assert not [f for f in epanet_demo.parent.iterdir() if ".mirab-py-" in f.name]


def test_check_info_and_analyses(epanet_demo, epanet_engine):
    p = mirab.Project.open(epanet_demo)
    assert p.check(engine=epanet_engine)["runnable"]
    assert p.info()["counts"]["nodes"] == 11
    topo = p.topology()
    assert topo["nodes"] == 11 and topo["components"] == 1
    r = p.run(engine=epanet_engine)
    tod = p.todini(r.results_path)
    assert 0 < tod["mean"] <= 1
    crit = p.criticality(engine=epanet_engine, links=["P1", "P2"])
    assert [e["link"] for e in crit["entries"]] == ["P1", "P2"]
    p.set_option("Duration", "1:00", section="TIMES")
    with pytest.raises(RuntimeError, match="unsaved"):
        p.info()
