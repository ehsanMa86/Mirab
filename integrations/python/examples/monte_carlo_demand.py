# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/examples/monte_carlo_demand.py
# Purpose: Example: Monte Carlo study of demand uncertainty on the EPANET demonstration network.
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
"""Monte Carlo study of demand uncertainty.

Every junction demand of the EPANET demonstration network is multiplied by
a random factor (normal, mean 1, coefficient of variation ``--cv``). Each
sample runs in its own mirab-cli process, several at a time, and only the
lowest pressure of each junction over the day is kept. The script prints,
for each junction, the 5th, 50th and 95th percentile of that minimum
pressure and the probability of falling below ``--threshold``.

    python monte_carlo_demand.py --samples 200 --cv 0.25 --threshold 30
"""

from __future__ import annotations

import argparse
import random
import statistics
import sys
import time
from pathlib import Path

import mirab

DEFAULT_MODEL = Path(__file__).resolve().parents[3] / "examples" / "epanet" / "mirab-demo.inp"


def percentile(values, q):
    s = sorted(values)
    k = (len(s) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("model", nargs="?", default=str(DEFAULT_MODEL), help="EPANET .inp file")
    parser.add_argument("--samples", type=int, default=100)
    parser.add_argument("--cv", type=float, default=0.25, help="coefficient of variation of the demands")
    parser.add_argument("--threshold", type=float, default=50.0, help="pressure of concern, model units")
    parser.add_argument("--workers", type=int, default=None, help="runs at a time (default: CPUs)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--engine", default=None)
    args = parser.parse_args()

    project = mirab.Project.open(args.model)
    junctions = project.nodes.of_kind("Junction")
    base = {j.id: j.demand or 0.0 for j in junctions}
    rng = random.Random(args.seed)

    cases = []
    for i in range(args.samples):
        factors = {jid: max(0.0, rng.gauss(1.0, args.cv)) for jid in base}
        edits = [mirab.edit(jid, "Demand", round(base[jid] * f, 4)) for jid, f in factors.items()]
        cases.append(mirab.Case(f"sample-{i + 1}", edits, params=factors))

    ids = list(base)

    def lowest_pressure(results):
        table = results.node
        return {jid: min(float(x) for x in table.series("Pressure", jid)) for jid in ids}

    started = time.perf_counter()
    done = mirab.run_batch(args.model, cases, engine=args.engine, reduce=lowest_pressure, workers=args.workers)
    ok = [r for r in done if r.success]
    print(f"{len(ok)} of {len(done)} runs succeeded in {time.perf_counter() - started:.1f} s "
          f"({project.units.get('flow')} flow units, pressure in model units)")
    for r in done:
        if not r.success:
            print(f"  {r.name}: {r.message}", file=sys.stderr)
    if not ok:
        return 1

    print(f"\n{'node':8} {'p5':>8} {'p50':>8} {'p95':>8} {'P(<' + format(args.threshold, 'g') + ')':>10}")
    for jid in ids:
        lows = [r.value[jid] for r in ok]
        below = sum(1 for v in lows if v < args.threshold) / len(lows)
        print(f"{jid:8} {percentile(lows, 0.05):8.2f} {percentile(lows, 0.5):8.2f} "
              f"{percentile(lows, 0.95):8.2f} {below:10.1%}")
    worst = min(ids, key=lambda j: statistics.mean(r.value[j] for r in ok))
    print(f"\nLowest mean minimum pressure: {worst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
