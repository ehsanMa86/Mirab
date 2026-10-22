# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/__init__.py
# Purpose: Python scripting for Mirab through mirab-cli.
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
"""Scripting for Mirab water network and stormwater models.

The package drives ``mirab-cli``, the command-line program installed with
Mirab, and reads result files directly::

    import mirab

    project = mirab.Project.open("network.inp")
    project.nodes["J1"].elevation = 12
    result = project.run()
    pressure = result.results.node.values("Pressure")

NumPy is used when installed; pandas enables ``to_dataframe()``.
"""

from ._cli import Cli, CliError, CliNotFoundError, default_cli, find_cli, set_cli
from .analysis import (check, criticality, elevations, engines, export, fireflow, import_wntr, info, link_dwf,
                       service, todini, topology)
from .batch import Case, CaseResult, run_batch
from .project import Element, ElementCollection, Project, edit, open_project, option, rows, scenarios
from .results import Results, Table, Variable, has_numpy, load_results
from .simulation import Event, FrameEvent, RunResult, Simulation, decode_frame, run

__version__ = "0.2.0"

__all__ = [
    "Case", "CaseResult", "Cli", "CliError", "CliNotFoundError", "Element", "ElementCollection", "Event",
    "FrameEvent", "Project", "Results", "RunResult", "Simulation", "Table", "Variable", "check",
    "criticality", "decode_frame", "default_cli", "edit", "elevations", "engines", "export", "find_cli",
    "fireflow", "has_numpy", "import_wntr", "info", "link_dwf", "load_results", "open_project", "option", "rows", "run",
    "run_batch", "scenarios", "service", "set_cli", "todini", "topology", "__version__",
]
