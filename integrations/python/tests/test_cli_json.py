# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_cli_json.py
# Purpose: Tests of the JSON output of mirab-cli that scripts rely on (docs/cli.md).
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
"""The JSON layouts documented in docs/cli.md."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

import mirab


def keys(obj, *names):
    missing = [n for n in names if n not in obj]
    assert not missing, f"missing members {missing} in {sorted(obj)}"


def test_version_and_engines(cli):
    v = cli.json(["version", "--json"])
    keys(v, "name", "version", "license", "homepage")
    assert v["name"] == "Mirab"
    engines = mirab.engines()
    assert engines and all(isinstance(e, dict) for e in engines)
    for e in engines:
        keys(e, "id", "name", "dialect", "available", "limitations")
    where = cli.json(["engines", "where", "--json"])
    keys(where, "path")


def test_info_and_check(cli, epanet_demo):
    info = mirab.info(epanet_demo)
    keys(info, "path", "dialect", "title", "units", "duration", "reportStep", "counts", "kinds", "features", "issues")
    assert info["counts"] == {"nodes": 11, "links": 14, "subcatchments": 0, "gages": 0}
    assert info["kinds"]["Junction"] == 9
    check = mirab.check(epanet_demo, engine="epanet-2.2-epa")
    keys(check, "engine", "engineName", "runnable", "issues")
    assert check["engine"] == "epanet-2.2-epa"


def test_model_get_layout(cli, epanet_demo, swmm_demo):
    m = cli.json(["model", "get", epanet_demo])
    keys(m, "format", "path", "dialect", "title", "units", "times", "options", "nodes", "links", "subcatchments",
         "gages", "objects", "sections")
    assert m["format"] == "mirab-model/1"
    j1 = next(n for n in m["nodes"] if n["id"] == "J1")
    keys(j1, "id", "kind", "section", "properties", "data", "x", "y")
    assert j1["properties"]["Elevation"] == 20
    p1 = next(link for link in m["links"] if link["id"] == "P1")
    keys(p1, "from", "to", "vertices")
    assert m["options"]["TIMES"]["DURATION"] == "24:00"
    assert m["sections"]["PATTERNS"][0][0] == "PAT1"

    s = cli.json(["model", "get", swmm_demo, "--compact"])
    assert s["dialect"] == "SWMM"
    c1 = next(link for link in s["links"] if link["id"] == "C1")
    assert c1["data"]["XSECTIONS"][0]["Shape"] == "CIRCULAR"
    keys(s["subcatchments"][0], "outlet", "gage", "polygon")
    assert s["sections"]["TIMESERIES"][0][0] == "Storm1"


def test_model_set_keeps_other_lines(cli, epanet_demo):
    before = epanet_demo.read_text().splitlines()
    out = cli.json(["model", "set", epanet_demo, "--id", "J1", "--column", "Elevation", "--value", "21.5", "--json"])
    keys(out, "path", "applied", "changed", "written", "issues")
    assert out["changed"] == 1
    after = epanet_demo.read_text().splitlines()
    changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    assert len(before) == len(after) and len(changed) == 1
    assert "21.5" in after[changed[0]]

    # Option edit through flags, batch edits from standard input.
    cli.run(["model", "set", epanet_demo, "--option", "Demand Model", "--value", "PDA"])
    edits = [mirab.edit("P1", "diameter", 250), mirab.rows("PATTERNS", [[1, 1.2]], id="PAT1"),
             mirab.option("Duration", "6:00", section="TIMES")]
    copy = epanet_demo.with_name("copy.inp")
    res = cli.json(["model", "set", epanet_demo, "--edits", "-", "--out", copy, "--json"],
                   input=json.dumps({"edits": edits}))
    assert res["applied"] == 3 and res["changed"] == 3 and Path(res["path"]) == copy
    m = cli.json(["model", "get", copy])
    assert m["options"]["OPTIONS"]["DEMAND MODEL"] == "PDA"
    assert m["options"]["TIMES"]["DURATION"] == "6:00"
    assert m["sections"]["PATTERNS"] == [["PAT1", "1", "1.2"]]
    assert next(link for link in m["links"] if link["id"] == "P1")["properties"]["Diameter"] == 250


def test_model_set_errors(cli, epanet_demo):
    with pytest.raises(mirab.CliError, match="no element or object named NOPE"):
        cli.run(["model", "set", epanet_demo, "--id", "NOPE", "--column", "Elevation", "--value", "1"])
    with pytest.raises(mirab.CliError, match="has no column"):
        cli.run(["model", "set", epanet_demo, "--id", "J1", "--column", "Height", "--value", "1"])
    with pytest.raises(mirab.CliError, match="edit 2"):
        cli.run(["model", "set", epanet_demo, "--edits", "-"],
                input=json.dumps([mirab.edit("J1", "Elevation", 1), {"id": "J1"}]))


@pytest.fixture
def epanet_results(cli, epanet_demo, epanet_engine):
    done = cli.json(["run", epanet_demo, "--engine", epanet_engine, "--json"])
    keys(done, "success", "cancelled", "message", "engine", "seconds", "results", "report", "periods")
    assert done["success"] and done["periods"] == 25
    return Path(done["results"])


def test_results_json_npy_csv(cli, epanet_results, tmp_path):
    summary = cli.json(["results", epanet_results, "--json"])
    keys(summary, "engineId", "engineName", "flowUnits", "unitSystem", "reportStart", "reportStep", "periods",
         "times", "metadata", "messages", "tables")
    node = next(t for t in summary["tables"] if t["kind"] == "node")
    keys(node, "kind", "ids", "variables")

    sel = cli.json(["results", epanet_results, "--table", "node", "--variable", "Pressure", "--element", "J1,J5",
                    "--period", "3", "--json"])
    keys(sel, "table", "periods", "times", "elements", "variables")
    assert sel["periods"] == [3] and sel["elements"] == ["J1", "J5"]
    assert len(sel["variables"]) == 1 and len(sel["variables"][0]["values"][0]) == 2

    r = mirab.Results.load(epanet_results)
    assert sel["variables"][0]["values"][0][1] == pytest.approx(r.node.value(3, "Pressure", "J5"), rel=1e-6)

    npy = tmp_path / "pressure.npy"
    cli.run(["results", epanet_results, "--table", "node", "--variable", "Pressure", "--format", "npy",
             "--out", npy])
    raw = npy.read_bytes()
    assert raw[:6] == b"\x93NUMPY"
    hlen = struct.unpack_from("<H", raw, 8)[0]
    assert (10 + hlen) % 64 == 0
    header = raw[10:10 + hlen].decode("latin1")
    assert "'shape': (25, 11)" in header
    first = struct.unpack_from("<f", raw, 10 + hlen + 4 * 4)[0]
    assert first == pytest.approx(r.node.value(0, "Pressure", 4), rel=1e-6)

    csv = cli.run(["results", epanet_results, "--table", "link", "--variable", "Flow"]).stdout.splitlines()
    assert csv[0].startswith("time_s,P1") and len(csv) == 26

    copy = tmp_path / "copy.anr"
    cli.run(["results", epanet_results, "--format", "anr", "--out", copy])
    assert mirab.Results.load(copy).times == r.times


def test_export_and_elevations(cli, epanet_demo, tmp_path):
    out = mirab.export(epanet_demo, "geojson", tmp_path / "net.geojson")
    keys(out, "format", "path", "notes")
    assert json.loads((tmp_path / "net.geojson").read_text())["type"] == "FeatureCollection"

    # A plane rising 1 m per 100 m eastwards, as an ESRI ASCII grid.
    dem = tmp_path / "dem.asc"
    rows = ["ncols 41", "nrows 41", "xllcorner -500", "yllcorner -500", "cellsize 100", "NODATA_value -9999"]
    rows += [" ".join(str(10 + c) for c in range(41)) for _ in range(41)]
    dem.write_text("\n".join(rows) + "\n")
    plan = mirab.elevations(epanet_demo, dem, dem_units="m")
    keys(plan, "dem", "counts", "nodes", "notes")
    j1 = next(n for n in plan["nodes"] if n["id"] == "J1")
    keys(j1, "id", "kind", "field", "status")
    assert j1["new"] == pytest.approx(25.0, abs=0.6)
    applied = mirab.elevations(epanet_demo, dem, dem_units="m", out=tmp_path / "high.inp")
    assert applied["written"].endswith("high.inp") and applied["changed"] > 0
