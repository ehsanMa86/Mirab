# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/project.py
# Purpose: Project: model data from mirab-cli model get, edits through model set, runs and analyses.
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
"""Opening, editing and running Mirab projects.

A :class:`Project` holds the JSON view of a model printed by
``mirab-cli model get``. Edits are collected and written by
``mirab-cli model set``, which changes only the edited values of the
``.inp`` file and keeps its comments, order and layout.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import tempfile
import uuid
from pathlib import Path
from typing import Any, Dict, Iterator, List, Mapping, Optional, Sequence, Tuple, Union

from . import analysis
from ._cli import Cli, CliError, PathLike, resolve_cli
from .results import Results
from .simulation import EventCallback, ProgressCallback, RunResult, Simulation, drive, run_args

SIDECAR_SUFFIX = ".mirab.json"


def column_key(name: str) -> str:
    """Normalised column name: lower-case letters and digits only.

    ``"InitLevel"``, ``"init_level"`` and ``"Init level"`` all become
    ``"initlevel"``, the same rule mirab-cli uses to match columns.
    """
    return re.sub(r"[^0-9a-z]", "", name.lower())


def format_value(value: Any) -> Optional[str]:
    """Text written to the input file for a Python value (``None`` clears the value)."""
    if value is None:
        return None
    if isinstance(value, bool):
        return "YES" if value else "NO"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if value != value or value in (float("inf"), float("-inf")):
            raise ValueError("input files cannot hold NaN or infinite values")
        return str(int(value)) if value.is_integer() and abs(value) < 1e15 else repr(value)
    try:  # NumPy scalars and other number types
        import numbers

        if isinstance(value, numbers.Integral):
            return str(int(value))
        if isinstance(value, numbers.Real):
            return format_value(float(value))
    except ImportError:  # pragma: no cover
        pass
    return str(value)


def edit(id: str, column: Union[str, int], value: Any, section: Optional[str] = None,
         row: Optional[int] = None, category: Optional[str] = None) -> Dict[str, Any]:
    """An edit that sets one value of an element or object (for ``model set --edits``)."""
    e: Dict[str, Any] = {"id": id, "column": column, "value": format_value(value)}
    if section:
        e["section"] = section
    if row is not None:
        e["row"] = row
    if category:
        e["category"] = category
    return e


def option(key: str, value: Any, section: Optional[str] = None) -> Dict[str, Any]:
    """An edit that sets a key/value option (``None`` removes it)."""
    e: Dict[str, Any] = {"option": key, "value": format_value(value)}
    if section:
        e["section"] = section
    return e


def rows(section: str, rows: Sequence[Sequence[Any]], id: Optional[str] = None) -> Dict[str, Any]:
    """An edit that replaces the rows of a pattern, curve, time series, ...

    With ``id`` the rows are given without the key (``[["0:00", 0], ["0:15", 6]]``
    for a SWMM time series); without it every row of the section is replaced.
    """
    e: Dict[str, Any] = {"section": section,
                         "rows": [[format_value(v) or "" for v in r] for r in rows]}
    if id is not None:
        e["id"] = id
    return e


def sidecar_of(inp: PathLike) -> Path:
    """The project settings file next to a model (``model.inp.mirab.json``)."""
    p = Path(inp)
    return p.with_name(p.name + SIDECAR_SUFFIX)


def write_model(source: PathLike, target: PathLike, edits: Sequence[Mapping[str, Any]] = (),
                cli: Union[Cli, PathLike, None] = None) -> Dict[str, Any]:
    """Writes ``source`` with ``edits`` applied to ``target`` and copies its settings file.

    Returns the summary printed by ``mirab-cli model set --json``.
    """
    c = resolve_cli(cli)
    src, dst = Path(source), Path(target)
    if edits:
        summary = c.json(["model", "set", src, "--edits", "-", "--out", dst, "--json"],
                         input=json.dumps({"edits": list(edits)}))
    else:
        if src.resolve() != dst.resolve():
            shutil.copyfile(src, dst)
        summary = {"path": str(dst), "applied": 0, "changed": 0, "written": True, "issues": []}
    if src.resolve() != dst.resolve() and sidecar_of(src).is_file():
        shutil.copyfile(sidecar_of(src), sidecar_of(dst))
    return summary


class Element:
    """An element (node, link, subcatchment or rain gage) or a named object.

    Values are read and set by column name, as attributes in any spelling
    (``node.elevation``, ``tank.init_level``) or as items with the schema's
    name (``node["Elevation"]``). Setting a value records an edit; call
    :meth:`Project.save` to write it. Columns of other sections that hold one
    row for the element (SWMM ``[XSECTIONS]``, ``[SUBAREAS]``, EPANET
    ``[EMITTERS]`` once present, ...) are reachable the same way.
    """

    __slots__ = ("_project", "_category", "_data")

    def __init__(self, project: "Project", category: str, data: Dict[str, Any]) -> None:
        object.__setattr__(self, "_project", project)
        object.__setattr__(self, "_category", category)
        object.__setattr__(self, "_data", data)

    # -- identity --------------------------------------------------------------
    @property
    def id(self) -> str:
        return str(self._data["id"])

    @property
    def kind(self) -> str:
        return str(self._data.get("kind", ""))

    @property
    def category(self) -> str:
        """``node``, ``link``, ``subcatchment``, ``gage`` or the object kind."""
        return self._category

    @property
    def section(self) -> str:
        return str(self._data.get("section", ""))

    @property
    def properties(self) -> Dict[str, Any]:
        """Values of the defining row by schema column name."""
        return self._data.get("properties", {})

    @property
    def sections(self) -> Dict[str, List[Dict[str, Any]]]:
        """Rows of other sections keyed by this element (``{"XSECTIONS": [{...}]}``)."""
        return self._data.get("data", {})

    @property
    def raw(self) -> Dict[str, Any]:
        """The element as ``mirab-cli model get`` reports it."""
        return self._data

    @property
    def x(self) -> Optional[float]:
        return self._data.get("x")

    @property
    def y(self) -> Optional[float]:
        return self._data.get("y")

    @property
    def from_node(self) -> Optional[str]:
        return self._data.get("from")

    @property
    def to_node(self) -> Optional[str]:
        return self._data.get("to")

    # -- columns -----------------------------------------------------------------
    def _locate(self, column: str) -> Optional[Tuple[Optional[str], str, Dict[str, Any]]]:
        """(section or None for the defining row, schema name, row dict)."""
        want = column_key(column)
        for name in self.properties:
            if column_key(name) == want:
                return None, name, self.properties
        for section, rows in self.sections.items():
            if len(rows) == 1:
                for name in rows[0]:
                    if column_key(name) == want:
                        return section, name, rows[0]
        return None

    def columns(self) -> List[str]:
        """Names of the columns that can be read and set."""
        names = list(self.properties)
        for rows in self.sections.values():
            if len(rows) == 1:
                names += [n for n in rows[0] if n not in names]
        return names

    def get(self, column: str, default: Any = None) -> Any:
        found = self._locate(column)
        return default if found is None else found[2].get(found[1])

    def set(self, column: Union[str, int], value: Any, section: Optional[str] = None,
            row: Optional[int] = None) -> "Element":
        """Records an edit of one value (see :meth:`Project.set`)."""
        category = self._category if self._category in ("node", "link", "subcatchment", "gage") else None
        if section is None and isinstance(column, str):
            found = self._locate(column)
            if found is not None:
                section, name, target = found
                column = name
                target[name] = value
        self._project.set(self.id, column, value, section=section, row=row, category=category)
        return self

    def __getitem__(self, column: str) -> Any:
        found = self._locate(column)
        if found is None:
            raise KeyError(f"{self.id} has no column {column!r} (columns: {', '.join(self.columns())})")
        return found[2].get(found[1])

    def __setitem__(self, column: str, value: Any) -> None:
        if self._locate(column) is None:
            raise KeyError(f"{self.id} has no column {column!r}; use project.set(..., section=...)")
        self.set(column, value)

    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)
        found = self._locate(name)
        if found is None:
            raise AttributeError(f"{self.id} has no column {name!r} (columns: {', '.join(self.columns())})")
        return found[2].get(found[1])

    def __setattr__(self, name: str, value: Any) -> None:
        if name.startswith("_") or name in type(self).__dict__:
            raise AttributeError(f"{name!r} cannot be set")
        if self._locate(name) is None:
            raise AttributeError(f"{self.id} has no column {name!r}; use project.set(..., section=...)")
        self.set(name, value)

    def __dir__(self) -> List[str]:
        return sorted(set(object.__dir__(self)) | {column_key(c) for c in self.columns()})

    def __repr__(self) -> str:
        values = ", ".join(f"{k}={v!r}" for k, v in list(self.properties.items())[:6])
        return f"<{self.kind or self._category} {self.id}: {values}>"

    def to_dict(self) -> Dict[str, Any]:
        """``id``, ``kind`` and every readable column as one flat dictionary."""
        out: Dict[str, Any] = {"id": self.id, "kind": self.kind}
        for c in self.columns():
            out[c] = self.get(c)
        return out


class ElementCollection(Mapping[str, Element]):
    """Elements of one category by ID, in file order."""

    def __init__(self, project: "Project", category: str, items: Sequence[Dict[str, Any]]) -> None:
        self._project = project
        self._category = category
        self._items: Dict[str, Element] = {str(i["id"]): Element(project, category, i) for i in items}

    def __getitem__(self, id: str) -> Element:
        try:
            return self._items[id]
        except KeyError:
            raise KeyError(f"no {self._category} {id!r}") from None

    def __iter__(self) -> Iterator[str]:
        return iter(self._items)

    def __len__(self) -> int:
        return len(self._items)

    def __repr__(self) -> str:
        return f"<{len(self)} {self._category} element(s)>"

    def of_kind(self, *kinds: str) -> List[Element]:
        """Elements of the given kinds, e.g. ``nodes.of_kind("Junction")``."""
        want = {k.lower() for k in kinds}
        return [e for e in self._items.values() if e.kind.lower() in want]

    def to_dataframe(self) -> Any:
        """One row per element with its readable columns (needs pandas)."""
        try:
            import pandas as pd
        except ImportError:
            raise ImportError("to_dataframe() needs pandas (pip install pandas)") from None
        return pd.DataFrame([e.to_dict() for e in self._items.values()]).set_index("id")


class Project:
    """A model file opened through mirab-cli.

    Example::

        p = mirab.Project.open("network.inp")
        p.nodes["J1"].elevation = 12          # recorded edit
        p.set_option("DEMAND MODEL", "PDA")
        p.save()                              # written by mirab-cli model set
        result = p.run()
        print(result.results.node.series("Pressure", "J1"))
    """

    def __init__(self, path: PathLike, data: Dict[str, Any], cli: Cli) -> None:
        self.path = Path(path)
        self.cli = cli
        self._edits: List[Dict[str, Any]] = []
        self._load(data)

    @classmethod
    def open(cls, path: PathLike, cli: Union[Cli, PathLike, None] = None) -> "Project":
        """Reads a model with ``mirab-cli model get``."""
        c = resolve_cli(cli)
        p = Path(path)
        if not p.is_file():
            raise FileNotFoundError(f"no such model: {p}")
        return cls(p, c.json(["model", "get", p, "--compact"]), c)

    def _load(self, data: Dict[str, Any]) -> None:
        self.data = data
        self.nodes = ElementCollection(self, "node", data.get("nodes", []))
        self.links = ElementCollection(self, "link", data.get("links", []))
        self.subcatchments = ElementCollection(self, "subcatchment", data.get("subcatchments", []))
        self.gages = ElementCollection(self, "gage", data.get("gages", []))
        self.objects: Dict[str, ElementCollection] = {
            kind: ElementCollection(self, kind, items) for kind, items in data.get("objects", {}).items()}

    def reload(self) -> "Project":
        """Reads the file again; unsaved edits are discarded."""
        self._edits = []
        self._load(self.cli.json(["model", "get", self.path, "--compact"]))
        return self

    def __repr__(self) -> str:
        return (f"<Project {self.path.name}: {self.dialect}, {len(self.nodes)} nodes, {len(self.links)} links"
                f"{', %d unsaved edit(s)' % len(self._edits) if self._edits else ''}>")

    # -- model data --------------------------------------------------------------
    @property
    def dialect(self) -> str:
        """``EPANET`` or ``SWMM``."""
        return str(self.data.get("dialect", ""))

    @property
    def title(self) -> str:
        return str(self.data.get("title", ""))

    @property
    def units(self) -> Dict[str, str]:
        """``{"flow": "LPS", "system": "SI", ...}``."""
        return dict(self.data.get("units", {}))

    @property
    def times(self) -> Dict[str, float]:
        """Duration, hydraulic step, report step and report start in seconds."""
        return dict(self.data.get("times", {}))

    @property
    def options(self) -> Dict[str, Dict[str, str]]:
        """``{"OPTIONS": {"UNITS": "LPS", ...}, "TIMES": {...}, ...}``."""
        return self.data.get("options", {})

    @property
    def sections(self) -> Dict[str, List[List[str]]]:
        """Rows (lists of tokens) of the sections not reported per element or object."""
        return self.data.get("sections", {})

    def option(self, key: str, section: Optional[str] = None, default: Optional[str] = None) -> Optional[str]:
        """Value of an option; keys are matched without regard to case."""
        want = " ".join(key.upper().split())
        for name, values in self.options.items():
            if section and name != section.upper():
                continue
            if want in values:
                return values[want]
        return default

    def element(self, id: str, category: Optional[str] = None) -> Element:
        """An element or named object by ID."""
        groups = [("node", self.nodes), ("link", self.links), ("subcatchment", self.subcatchments),
                  ("gage", self.gages)] + list(self.objects.items())
        found = [g[id] for name, g in groups if id in g and (category is None or name.lower() == category.lower())]
        if not found:
            raise KeyError(f"no element or object {id!r}")
        if len(found) > 1:
            raise KeyError(f"{id!r} names several elements; pass category=")
        return found[0]

    def pattern(self, id: str) -> List[float]:
        """Multipliers of a time pattern (EPANET, or SWMM ``[PATTERNS]`` after the type)."""
        values: List[float] = []
        for row in self.sections.get("PATTERNS", []):
            if row and row[0] == id:
                tokens = row[1:]
                if self.dialect == "SWMM" and tokens and not _is_number(tokens[0]):
                    tokens = tokens[1:]
                values += [float(t) for t in tokens]
        if not values:
            raise KeyError(f"no pattern {id!r}")
        return values

    def curve(self, id: str) -> List[Tuple[float, float]]:
        """(x, y) points of a curve."""
        points: List[Tuple[float, float]] = []
        for row in self.sections.get("CURVES", []):
            if row and row[0] == id:
                nums = [t for t in row[1:] if _is_number(t)]
                points += [(float(nums[i]), float(nums[i + 1])) for i in range(0, len(nums) - 1, 2)]
        if not points:
            raise KeyError(f"no curve {id!r}")
        return points

    def timeseries(self, id: str) -> List[List[str]]:
        """Rows of a SWMM time series without the name (``[["0:00", "0"], ...]``)."""
        out = [row[1:] for row in self.sections.get("TIMESERIES", []) if row and row[0] == id]
        if not out:
            raise KeyError(f"no time series {id!r}")
        return out

    # -- edits ---------------------------------------------------------------------
    @property
    def edits(self) -> List[Dict[str, Any]]:
        """Edits recorded since the last save, in ``model set --edits`` format."""
        return list(self._edits)

    @property
    def modified(self) -> bool:
        return bool(self._edits)

    def discard(self) -> None:
        """Forgets unsaved edits and rereads the file."""
        self.reload()

    def set(self, id: str, column: Union[str, int], value: Any, section: Optional[str] = None,
            row: Optional[int] = None, category: Optional[str] = None) -> "Project":
        """Records an edit of one value of an element or object.

        Args:
            id: element or object ID.
            column: column name as in the schema (any case and spelling,
                ``"InitLevel"`` or ``"init_level"``) or token index.
            value: new value; ``None`` clears an optional value.
            section: section of the row (default: the element's own row,
                then other sections that hold one row for it). Naming a
                section where the element has no row yet adds one, e.g.
                ``section="EMITTERS"``.
            row: n-th row of the element in ``section`` (default 0).
            category: ``node``, ``link``, ``subcatchment``, ``gage`` or an
                object kind, when an ID is used by several elements.
        """
        self._edits.append(edit(id, column, value, section, row, category))
        return self

    def set_option(self, key: str, value: Any, section: Optional[str] = None) -> "Project":
        """Records an option edit (``[OPTIONS]`` unless ``section`` says otherwise)."""
        self._edits.append(option(key, value, section))
        sec = (section or "OPTIONS").upper()
        values = self.options.setdefault(sec, {})
        k = " ".join(key.upper().split())
        if value is None:
            values.pop(k, None)
        else:
            values[k] = format_value(value) or ""
        return self

    def set_rows(self, section: str, rows_: Sequence[Sequence[Any]], id: Optional[str] = None) -> "Project":
        """Records the replacement of the rows of a pattern, curve, time series, ..."""
        e = rows(section, rows_, id)
        self._edits.append(e)
        sec = section.upper()
        if sec in self.sections:
            kept = [r for r in self.sections[sec] if id is not None and (not r or r[0] != id)]
            self.sections[sec] = kept + [([id] if id is not None else []) + r for r in e["rows"]]
        return self

    def set_pattern(self, id: str, multipliers: Sequence[float], per_row: int = 6) -> "Project":
        """Replaces the multipliers of an EPANET time pattern."""
        values = list(multipliers)
        return self.set_rows("PATTERNS", [values[i:i + per_row] for i in range(0, len(values), per_row)], id)

    def apply(self, edits: Sequence[Mapping[str, Any]]) -> "Project":
        """Records edits given as dictionaries (see :func:`edit`, :func:`option`, :func:`rows`)."""
        self._edits += [dict(e) for e in edits]
        return self

    def save(self, path: Optional[PathLike] = None) -> Path:
        """Writes the recorded edits.

        Without ``path`` the project file is updated in place; with a path the
        edited model (and its settings file) is written there and the project
        refers to the new file from then on. The model is read again afterwards.
        """
        target = Path(path) if path is not None else self.path
        if self._edits or target.resolve() != self.path.resolve():
            write_model(self.path, target, self._edits, self.cli)
        self.path = target
        self.reload()
        return target

    def write_copy(self, path: PathLike) -> Path:
        """Writes the model with the recorded edits to ``path``; the project is unchanged."""
        write_model(self.path, path, self._edits, self.cli)
        return Path(path)

    # -- runs ----------------------------------------------------------------------
    def _run_input(self) -> Tuple[Path, Optional[Path]]:
        """The file to run: the project file, or a scratch copy with the unsaved edits."""
        if not self._edits:
            return self.path, None
        scratch = self.path.with_name(f".{self.path.stem}.mirab-py-{uuid.uuid4().hex[:8]}{self.path.suffix}")
        self.write_copy(scratch)
        return scratch, scratch

    def start(self, engine: Optional[str] = None, out: Optional[PathLike] = None,
              report: Optional[PathLike] = None, msx: Optional[PathLike] = None, live: bool = False,
              frame_interval: Optional[float] = None) -> Simulation:
        """Starts a run and returns the :class:`Simulation` to read events from.

        Unsaved edits are run from a scratch copy (removed at the end); their
        results go to ``out``, or to a temporary file read into memory.
        """
        inp, scratch = self._run_input()
        tmpdir: Optional[str] = None
        if scratch is not None and out is None:
            tmpdir = tempfile.mkdtemp(prefix="mirab-run-")
            out = Path(tmpdir) / (self.path.stem + ".anr")
            report = report or Path(tmpdir) / (self.path.stem + ".rpt")

        def cleanup() -> None:
            if scratch is not None:
                for f in (scratch, sidecar_of(scratch)):
                    try:
                        f.unlink()
                    except OSError:
                        pass

        sim = Simulation(run_args(inp, engine, out, report, msx, live, frame_interval), self.cli, cleanup)
        if tmpdir is not None:
            sim._tmpdir = tmpdir  # type: ignore[attr-defined]
        return sim

    def run(self, engine: Optional[str] = None, out: Optional[PathLike] = None,
            report: Optional[PathLike] = None, msx: Optional[PathLike] = None,
            callback: Optional[EventCallback] = None, progress: Optional[ProgressCallback] = None,
            live: Optional[bool] = None, frame_interval: Optional[float] = None,
            check: bool = True) -> RunResult:
        """Runs the model and returns the outcome; ``result.results`` reads the results.

        Args:
            engine: engine ID; the project's engine by default.
            out: results file (default ``<model>.anr``, or a temporary file
                when there are unsaved edits).
            callback: ``callback(event, simulation)`` for every protocol event,
                live frames included; it may pause, resume or cancel the run.
            progress: ``progress(fraction, stage)``.
            live: stream live frames (default: when a callback is given).
            frame_interval: minimum seconds between frames (0: every period).
            check: raise :class:`CliError` when the run fails (a cancelled
                run never raises).
        """
        live = callback is not None if live is None else live
        sim = self.start(engine, out, report, msx, live, frame_interval)
        result = drive(sim, callback, progress)
        tmpdir = getattr(sim, "_tmpdir", None)
        if tmpdir is not None:
            if result.success and result.results_path:
                result._results = Results.load(result.results_path)
            shutil.rmtree(tmpdir, ignore_errors=True)
            result.results_path = None
            result.report_path = None
        if check and not result.success and not result.cancelled:
            result.raise_for_status()
        return result

    # -- analyses ------------------------------------------------------------------
    def _saved_path(self) -> Path:
        if self._edits:
            raise RuntimeError("the project has unsaved edits; call save() first")
        return self.path

    def info(self) -> Dict[str, Any]:
        """``mirab-cli info --json``: counts, features and validation messages."""
        return analysis.info(self._saved_path(), cli=self.cli)

    def check(self, engine: Optional[str] = None) -> Dict[str, Any]:
        """Whether an engine can run the project, with its reasons."""
        return analysis.check(self._saved_path(), engine=engine, cli=self.cli)

    def topology(self) -> Dict[str, Any]:
        return analysis.topology(self._saved_path(), cli=self.cli)

    def todini(self, results: PathLike, required_pressure: Optional[float] = None) -> Dict[str, Any]:
        return analysis.todini(self._saved_path(), results, required_pressure, cli=self.cli)

    def criticality(self, **kwargs: Any) -> Dict[str, Any]:
        """Pipe criticality; see :func:`mirab.analysis.criticality`."""
        return analysis.criticality(self._saved_path(), cli=self.cli, **kwargs)

    def fireflow(self, flow: float, **kwargs: Any) -> Dict[str, Any]:
        """Fire flow; see :func:`mirab.analysis.fireflow`."""
        return analysis.fireflow(self._saved_path(), flow, cli=self.cli, **kwargs)

    def elevations(self, dem: PathLike, apply: bool = False, **kwargs: Any) -> Dict[str, Any]:
        """Node elevations from a DEM; ``apply=True`` writes them and rereads the model."""
        out = self._saved_path() if apply else None
        result = analysis.elevations(self._saved_path(), dem, out=out, cli=self.cli, **kwargs)
        if apply:
            self.reload()
        return result

    def export(self, format: str, out: PathLike, **kwargs: Any) -> Dict[str, Any]:
        """GeoJSON, GeoPackage or WNTR JSON export; see :func:`mirab.analysis.export`."""
        return analysis.export(self._saved_path(), format, out, cli=self.cli, **kwargs)


def _is_number(text: str) -> bool:
    try:
        float(text)
        return True
    except ValueError:
        return False


def open_project(path: PathLike, cli: Union[Cli, PathLike, None] = None) -> Project:
    """Same as :meth:`Project.open`."""
    return Project.open(path, cli)


__all__ = ["Element", "ElementCollection", "Project", "column_key", "edit", "format_value", "open_project",
           "option", "rows", "sidecar_of", "write_model", "CliError"]
