# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/examples/results_to_pandas.py
# Purpose: Example: results of a run in pandas, with a matplotlib plot when matplotlib is installed.
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
"""Results in pandas and a pressure plot.

Runs a model (or reads an existing ``.anr`` file with ``--results``), puts
the node pressures in a pandas DataFrame indexed by hours, prints summary
statistics, writes them to CSV and, when matplotlib is installed, plots the
lowest elements to a PNG file.

    python results_to_pandas.py --csv pressure.csv --plot pressure.png
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

import mirab

DEFAULT_MODEL = Path(__file__).resolve().parents[3] / "examples" / "epanet" / "mirab-demo.inp"

# Fixed series order: each element keeps its colour whatever else is plotted.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
INK, MUTED, GRID = "#1f1f1e", "#6b6a64", "#e4e3de"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("model", nargs="?", default=str(DEFAULT_MODEL), help="EPANET or SWMM .inp file")
    parser.add_argument("--results", help="read this .anr file instead of running the model")
    parser.add_argument("--table", default="node", help="node, link or subcatchment")
    parser.add_argument("--variable", default=None, help="default: Pressure (EPANET) or Depth (SWMM)")
    parser.add_argument("--csv", default=None, help="write the DataFrame to this CSV file")
    parser.add_argument("--plot", default=None, help="write a plot to this PNG file (needs matplotlib)")
    parser.add_argument("--lowest", type=int, default=4, choices=range(1, 5), help="elements to plot (1-4)")
    args = parser.parse_args()

    try:
        import pandas  # noqa: F401
    except ImportError:
        print("this example needs pandas: pip install pandas", file=sys.stderr)
        return 1

    if args.results:
        results = mirab.Results.load(args.results)
    else:
        # Run into a scratch folder so the example leaves no files behind.
        with tempfile.TemporaryDirectory() as tmp:
            run = mirab.Project.open(args.model).run(out=Path(tmp) / "run.anr", report=Path(tmp) / "run.rpt")
            results = run.results  # read into memory
    variable = args.variable or ("Depth" if "subcatchment" in results else "Pressure")

    df = results.to_dataframe(args.table, variable, time_unit="h")
    unit = df.attrs["unit"]
    if args.table == "node" and Path(args.model).is_file():
        # Pressure of reservoirs and tanks says little; keep the other nodes.
        nodes = mirab.Project.open(args.model).nodes
        df = df[[c for c in df.columns if c not in nodes or nodes[c].kind not in ("Reservoir", "Tank")]]
    print(f"{results.engine_name}: {variable} [{unit}] of {df.shape[1]} {args.table}s over {df.shape[0]} periods\n")
    print(df.describe().T[["min", "mean", "max"]].round(3).to_string())

    if args.csv:
        df.to_csv(args.csv)
        print(f"\nwrote {args.csv}")

    if args.plot:
        try:
            import matplotlib

            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            print("matplotlib is not installed; no plot written", file=sys.stderr)
            return 0
        lowest = sorted(df.min().nsmallest(args.lowest).index, key=list(df.columns).index)
        fig, ax = plt.subplots(figsize=(8, 4.5))
        for colour, element in zip(SERIES, lowest):
            ax.plot(df.index, df[element], color=colour, linewidth=2, label=element)
        # Direct labels at the line ends, spread apart where the lines end close together.
        low, high = ax.get_ylim()
        gap, last = (high - low) * 0.045, None
        for element in sorted(lowest, key=lambda e: df[e].iloc[-1]):
            y = df[element].iloc[-1] if last is None else max(df[element].iloc[-1], last + gap)
            ax.annotate(element, (df.index[-1], y), xytext=(6, 0), textcoords="offset points", va="center",
                        color=INK, fontsize=9, annotation_clip=False)
            last = y
        ax.set_xlabel("Time (h)", color=MUTED)
        ax.set_ylabel(f"{variable} ({unit})", color=MUTED)
        ax.set_title(f"{results.title or 'Results'}: lowest {variable.lower()}", color=INK, loc="left")
        ax.grid(True, axis="y", color=GRID, linewidth=0.8)
        ax.tick_params(colors=MUTED)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            ax.spines[side].set_color(GRID)
        if len(lowest) > 1:
            ax.legend(title=args.table, frameon=False, labelcolor=INK, ncol=len(lowest), loc="upper center",
                      bbox_to_anchor=(0.5, -0.14))
        fig.tight_layout()
        fig.savefig(args.plot, dpi=120)
        print(f"wrote {args.plot}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
