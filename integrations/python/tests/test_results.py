# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_results.py
# Purpose: Tests of the native .anr reader against mirab-cli, with and without NumPy.
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
"""Reading result files in Python."""

from __future__ import annotations

import pytest

import mirab
import mirab.results as results_module


@pytest.fixture
def anr(cli, epanet_demo, epanet_engine):
    return mirab.Project.open(epanet_demo).run(engine=epanet_engine).results_path


def cli_values(cli, path, table, variable):
    out = cli.json(["results", path, "--table", table, "--variable", variable, "--json", "--compact"])
    return out["elements"], out["variables"][0]["values"]


def test_matches_cli_for_every_variable(cli, anr):
    r = mirab.Results.load(anr)
    summary = cli.json(["results", anr, "--json"])
    assert r.times == summary["times"] and r.engine_id == summary["engineId"]
    assert set(r.tables) == {t["kind"] for t in summary["tables"]}
    for t in summary["tables"]:
        table = r[t["kind"]]
        assert table.ids == t["ids"]
        for v in t["variables"]:
            ids, values = cli_values(cli, anr, t["kind"], v["name"])
            got = table.values(v["name"])
            for p in range(len(values)):
                for e in range(len(ids)):
                    want = values[p][e]
                    if want is None:  # NaN
                        assert got[p][e] != got[p][e]
                    else:
                        assert float(got[p][e]) == pytest.approx(want, rel=1e-6, abs=1e-9)


def test_access_helpers(anr):
    r = mirab.Results.load(anr)
    node = r.node
    assert node.variable_index("pressure") == node.variable_index("Pressure")
    assert node.unit("Pressure") == "m"
    s = node.series("Pressure", "J5")
    assert len(s) == r.period_count
    assert float(s[3]) == node.value(3, "Pressure", "J5") == float(node.snapshot("Pressure", 3)[4])
    assert set(node.element("J5")) == set(node.variable_names)
    assert r.period_at(3 * 3600 + 100) == 3
    assert r.time_index("h")[:3] == [0.0, 1.0, 2.0]
    with pytest.raises(KeyError):
        node.series("Nope", "J5")
    with pytest.raises(KeyError):
        r["subcatchment"]


def test_without_numpy(anr, monkeypatch):
    with_np = mirab.Results.load(anr)
    monkeypatch.setattr(results_module, "_np", None)
    plain = mirab.Results.load(anr)
    assert isinstance(plain.node.values("Pressure"), list)
    assert plain.node.value(7, "Head", "J2") == with_np.node.value(7, "Head", "J2")
    assert list(plain.link.series("Flow", "P1")) == [float(x) for x in with_np.link.series("Flow", "P1")]


def test_numpy_memmap_and_dataframe(anr):
    np = pytest.importorskip("numpy")
    r = mirab.Results.load(anr, mmap=True)
    arr = r.node.to_numpy()
    assert arr.shape == (r.period_count, len(r.node.variables), len(r.node.ids))
    assert isinstance(arr, np.memmap)
    pd = pytest.importorskip("pandas")
    df = r.node.to_dataframe("Pressure", time_unit="h")
    assert list(df.columns) == r.node.ids and df.index[1] == 1.0
    assert df.attrs["unit"] == "m"
    wide = r.to_dataframe("link")
    assert isinstance(wide.columns, pd.MultiIndex)
    assert wide[("Flow", "P1")].iloc[2] == pytest.approx(float(r.link.value(2, "Flow", "P1")))
    del arr, r, df, wide


def test_rejects_other_files(tmp_path):
    bad = tmp_path / "bad.anr"
    bad.write_bytes(b"MIRAB-RESULTS\0\0\0" + b"\x09\0\0\0" + b"\0\0\0\0")
    with pytest.raises(ValueError):
        mirab.Results.load(bad)
