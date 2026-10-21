# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_run.py
# Purpose: Tests of simulations through the solver protocol: progress, live frames, pause, resume and cancel.
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
"""Live runs and run control from Python."""

from __future__ import annotations

import pytest

import mirab


def test_progress_and_live_frames(epanet_demo, epanet_engine):
    p = mirab.Project.open(epanet_demo)
    progress = []
    frames = []
    layouts = []

    def on_event(event, sim):
        if event.type == "frame":
            frames.append(event)
        elif event.type == "layout":
            layouts.append(event)

    result = p.run(engine=epanet_engine, callback=on_event, frame_interval=0,
                   progress=lambda f, stage: progress.append(f))
    assert result.success and progress and progress[-1] <= 1.0
    assert len(layouts) == 1 and frames
    final = result.results
    last = frames[-1]
    assert last.time == final.times[-1]
    assert last.value("node", "Pressure", "J5") == pytest.approx(final.node.value(-1, "Pressure", "J5"))
    # Every period was streamed (frame interval 0), so the frames rebuild the results.
    rebuilt = mirab.Results.from_layout(layouts[0].data, frames)
    assert rebuilt.times == final.times
    assert rebuilt.link.value(4, "Flow", "P2") == pytest.approx(final.link.value(4, "Flow", "P2"))


def test_pause_resume_cancel(epanet_demo, epanet_engine):
    p = mirab.Project.open(epanet_demo)
    p.set_option("Duration", "5000:00", section="TIMES")
    p.set_option("Hydraulic Timestep", "0:01", section="TIMES")
    p.set_option("Report Timestep", "0:01", section="TIMES")
    p.save()
    seen = []
    state = {"frames": 0}

    def control(event, sim):
        seen.append(event.type)
        if event.type == "frame":
            state["frames"] += 1
            if state["frames"] == 1:
                sim.pause()
            elif state["frames"] == 3:
                sim.cancel()
        elif event.type == "paused":
            assert sim.paused
            sim.resume()

    result = p.run(engine=epanet_engine, callback=control, frame_interval=0.02)
    assert "paused" in seen and "resumed" in seen
    assert seen.index("paused") < seen.index("resumed")
    assert result.cancelled and not result.success and result.returncode == 3
    with pytest.raises(mirab.CliError):
        result.raise_for_status()


def test_simulation_object_and_failure(epanet_demo, epanet_engine, tmp_path):
    with mirab.Project.open(epanet_demo).start(engine=epanet_engine, out=tmp_path / "x.anr") as sim:
        types = [e.type for e in sim.events()]
    assert types[-1] == "done" and "progress" in types
    assert sim.result is not None and sim.result.success
    assert sim.result.results_path == tmp_path / "x.anr"

    bad = mirab.run(epanet_demo, engine="no-such-engine")
    assert not bad.success and "no-such-engine" in bad.message
    with pytest.raises(mirab.CliError):
        mirab.Project.open(epanet_demo).run(engine="no-such-engine")
