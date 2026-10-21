# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/examples/swmm_return_periods.py
# Purpose: Example: design storm sweep over return periods on the SWMM demonstration catchment.
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
"""Design storms of several return periods on a SWMM model.

For each return period the script builds a two-hour alternating-block
hyetograph from an intensity-duration-frequency relation

    i = K * T**m / (d + c)**n      (i in mm/h, T in years, d in minutes)

writes it into the rain gage's time series, runs every storm in parallel
and reports the peak flow of a conduit and the flooded volume. The default
coefficients are illustrative; use the IDF relation of your site. For
models in US units the intensities are converted to in/h.

    python swmm_return_periods.py --periods 2 5 10 25 50 100 --link C5
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import mirab

DEFAULT_MODEL = Path(__file__).resolve().parents[3] / "examples" / "swmm" / "mirab-urban.inp"


def idf(T: float, d: float, K: float, m: float, c: float, n: float) -> float:
    """Average intensity (mm/h) of a storm of duration d minutes and return period T years."""
    return K * T ** m / (d + c) ** n


def alternating_blocks(T: float, duration: int, step: int, K: float, m: float, c: float, n: float):
    """Block intensities (mm/h) of an alternating-block hyetograph, peak in the middle."""
    blocks = duration // step
    depths = [idf(T, step * (k + 1), K, m, c, n) * step * (k + 1) / 60 for k in range(blocks)]
    increments = [depths[0]] + [depths[k] - depths[k - 1] for k in range(1, blocks)]
    order = sorted(increments, reverse=True)
    out = [0.0] * blocks
    mid = (blocks - 1) // 2
    for k, value in enumerate(order):
        offset = (k + 1) // 2 * (1 if k % 2 else -1)
        out[min(max(mid + offset, 0), blocks - 1)] = value * 60 / step
    return out


def clock(minutes: int) -> str:
    return f"{minutes // 60}:{minutes % 60:02d}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("model", nargs="?", default=str(DEFAULT_MODEL), help="SWMM .inp file")
    parser.add_argument("--periods", type=float, nargs="+", default=[2, 5, 10, 25, 50, 100])
    parser.add_argument("--gage", default=None, help="rain gage (default: the first)")
    parser.add_argument("--link", default=None, help="conduit to report (default: the last)")
    parser.add_argument("--duration", type=int, default=120, help="storm duration, minutes")
    parser.add_argument("--idf", type=float, nargs=4, default=[900.0, 0.2, 10.0, 0.75], metavar=("K", "m", "c", "n"))
    args = parser.parse_args()

    project = mirab.Project.open(args.model)
    if project.dialect != "SWMM":
        parser.error("this example needs a SWMM model")
    gage = project.gages[args.gage] if args.gage else next(iter(project.gages.values()))
    source = str(gage.source).split()
    if len(source) != 2 or source[0].upper() != "TIMESERIES":
        parser.error(f"rain gage {gage.id} does not read a time series")
    series = source[1]
    hours, _, minutes = str(gage.interval).partition(":")  # recording interval, h:mm
    step = int(hours) * 60 + int(minutes or 0)
    link = args.link or list(project.links)[-1]
    si = project.units.get("system") == "SI"
    volume, depth, rate = ("m3", "mm", "mm/h") if si else ("ft3", "in", "in/h")

    cases = []
    for T in args.periods:
        intensities = [i if si else i / 25.4 for i in alternating_blocks(T, args.duration, step, *args.idf)]
        rows = [[clock(k * step), round(i, 3)] for k, i in enumerate(intensities)]
        rows.append([clock(args.duration), 0])
        cases.append(mirab.Case(f"T{T:g}", [mirab.rows("TIMESERIES", rows, id=series),
                                            mirab.edit(gage.id, "Format", "INTENSITY")],
                                params={"T": T, "depth": sum(intensities) * step / 60,
                                        "peak": max(intensities)}))

    def summary(results):
        flow = [float(x) for x in results.link.series("Flow", link)]
        flooding = [float(x) for x in results.system.series("Flooding", 0)]
        step_s = results.report_step
        return {"peak": max(flow), "flooded": sum(flooding) * step_s}

    out = mirab.run_batch(args.model, cases, reduce=summary)
    flow_units = project.units.get("flow", "")
    print(f"{'T (yr)':>7} {'depth':>8} {'peak i':>8} {'peak Q ' + link:>12} {'flooded':>10}")
    print(f"{'':>7} {'(' + depth + ')':>8} {'(' + rate + ')':>8} {'(' + flow_units + ')':>12} {'(' + volume + ')':>10}")
    for r in out:
        if not r.success:
            print(f"{r.case.params['T']:7g}  failed: {r.message}", file=sys.stderr)
            continue
        p = r.case.params
        print(f"{p['T']:7g} {p['depth']:8.1f} {p['peak']:8.1f} {r.value['peak']:12.3f} {r.value['flooded']:10.1f}")
    return 0 if all(r.success for r in out) else 1


if __name__ == "__main__":
    sys.exit(main())
