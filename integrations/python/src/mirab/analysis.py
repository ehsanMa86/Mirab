# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/analysis.py
# Purpose: Wrappers of the mirab-cli analyses: information, compatibility, resilience, elevations and export.
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
"""Model information, engine checks, resilience analyses, DEM elevations and export.

Each function runs one ``mirab-cli`` command with ``--json`` (or in machine
mode for the long analyses, which report progress) and returns the decoded
JSON; ``docs/cli.md`` describes the members.
"""

from __future__ import annotations

import os
from typing import Any, Dict, List, Optional, Sequence, Union

from ._cli import Cli, PathLike, resolve_cli
from .simulation import ProgressCallback, machine_payload

CliArg = Union[Cli, PathLike, None]


def _ids(values: Optional[Sequence[str]]) -> Optional[str]:
    return ",".join(values) if values else None


def _opts(**kwargs: Any) -> List[str]:
    """``--name value`` pairs for the options that are set (``_`` becomes ``-``)."""
    out: List[str] = []
    for name, value in kwargs.items():
        if value is None or value is False:
            continue
        flag = "--" + name.replace("_", "-")
        if value is True:
            out.append(flag)
        else:
            out += [flag, os.fspath(value) if isinstance(value, os.PathLike) else str(value)]
    return out


def engines(cli: CliArg = None) -> List[Dict[str, Any]]:
    """Engines Mirab knows, with ``available`` telling whether each is installed."""
    return resolve_cli(cli).json(["engines", "--json"])


def info(model: PathLike, cli: CliArg = None) -> Dict[str, Any]:
    """Counts, element kinds, detected features and validation messages."""
    return resolve_cli(cli).json(["info", model, "--json"])


def check(model: PathLike, engine: Optional[str] = None, msx: Optional[PathLike] = None,
          cli: CliArg = None) -> Dict[str, Any]:
    """Whether an engine can run the model: ``{"engine", "runnable", "issues": [...]}``."""
    return resolve_cli(cli).json(["check", model, "--json"] + _opts(engine=engine, msx=msx), ok_codes=(0, 2))


def topology(model: PathLike, cli: CliArg = None) -> Dict[str, Any]:
    """Graph measures, critical nodes and links, dead ends (EPANET and SWMM)."""
    return resolve_cli(cli).json(["resilience", "topology", model, "--json"])


def todini(model: PathLike, results: PathLike, required_pressure: Optional[float] = None,
           cli: CliArg = None) -> Dict[str, Any]:
    """Todini resilience index over time from a run's results."""
    return resolve_cli(cli).json(["resilience", "todini", model, "--json"] +
                                 _opts(results=results, required_pressure=required_pressure))


def service(results: PathLike, cli: CliArg = None) -> Dict[str, Any]:
    """Water service availability (needs a pressure-driven run)."""
    return resolve_cli(cli).json(["resilience", "service", "--json"] + _opts(results=results))


def criticality(model: PathLike, engine: Optional[str] = None, threshold: Optional[float] = None,
                links: Optional[Sequence[str]] = None, pressure_driven: bool = True,
                minimum_pressure: Optional[float] = None, required_pressure: Optional[float] = None,
                progress: Optional[ProgressCallback] = None, cli: CliArg = None) -> Dict[str, Any]:
    """Closes each pipe in turn and reports the nodes below ``threshold`` and the unmet demand."""
    args = ["resilience", "criticality", model] + _opts(
        engine=engine, threshold=threshold, links=_ids(links), dda=not pressure_driven,
        minimum_pressure=minimum_pressure, required_pressure=required_pressure)
    return machine_payload(args, cli, progress)


def fireflow(model: PathLike, flow: float, residual: Optional[float] = None, time: Optional[float] = None,
             nodes: Optional[Sequence[str]] = None, max_flow: Optional[float] = None,
             available_flow: bool = True, engine: Optional[str] = None,
             progress: Optional[ProgressCallback] = None, cli: CliArg = None) -> Dict[str, Any]:
    """Fire flow at each node (``time`` in hours of the day, flows in model units)."""
    args = ["resilience", "fireflow", model] + _opts(
        flow=flow, residual=residual, time=time, nodes=_ids(nodes), max_flow=max_flow,
        no_available=not available_flow, engine=engine)
    return machine_payload(args, cli, progress)


def elevations(model: PathLike, dem: PathLike, mode: str = "all", nodes: Optional[Sequence[str]] = None,
               reservoirs: bool = False, tanks: bool = True, junctions: bool = True, swmm: str = "invert",
               dem_units: str = "auto", crs: Optional[str] = None, decimals: int = 2,
               out: Optional[PathLike] = None, cli: CliArg = None) -> Dict[str, Any]:
    """Node elevations sampled from a GeoTIFF or ASCII grid DEM.

    Returns the planned change of every node (``nodes``), counts by status and
    notes; with ``out`` the changed model is written there (``out`` may be
    the model itself).
    """
    args = ["elevations", model, "--json"] + _opts(
        dem=dem, mode=mode, nodes=_ids(nodes), reservoirs=reservoirs, no_tanks=not tanks,
        no_junctions=not junctions, swmm=swmm, dem_units=dem_units, crs=crs, decimals=decimals, out=out)
    return resolve_cli(cli).json(args)


def export(model: PathLike, format: str, out: PathLike, crs: Optional[str] = None,
           results: Optional[PathLike] = None, period: Optional[int] = None, cli: CliArg = None) -> Dict[str, Any]:
    """Writes ``geojson``, ``gpkg`` (GeoPackage) or ``wntr`` (WNTR JSON) and returns ``{"path", "notes"}``."""
    return resolve_cli(cli).json(["export", format, model, "--json"] +
                                 _opts(out=out, crs=crs, results=results, period=period))


def import_wntr(network: PathLike, out: PathLike, cli: CliArg = None) -> Dict[str, Any]:
    """Converts a WNTR JSON network to an EPANET input file."""
    return resolve_cli(cli).json(["import", "wntr", network, "--json"] + _opts(out=out))
