# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/results.py
# Purpose: Reader for Mirab result files (.anr) in pure Python, with NumPy when available.
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
"""Reading Mirab result files (``.anr``).

The format is described in ``docs/file-formats.md``: a 16-byte magic text, a
format version, a JSON header and one block of little-endian ``float32``
values per table, ordered by period, then variable, then element. The file
is read here directly; NumPy is used when it is installed (optionally with
a memory map), otherwise values come back as Python lists.

EPANET and SWMM binary output files are converted with ``mirab-cli`` first.
"""

from __future__ import annotations

import json
import os
import struct
import sys
import tempfile
from array import array
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, Iterator, List, NamedTuple, Optional, Sequence, Union

try:  # NumPy is optional.
    import numpy as _np
except ImportError:  # pragma: no cover - exercised only without NumPy
    _np = None

if TYPE_CHECKING:  # pragma: no cover
    from ._cli import Cli, PathLike

MAGIC = b"MIRAB-RESULTS"
FORMAT_VERSION = 1
_PREAMBLE = 24  # magic (16) + version (4) + header length (4)

#: Seconds in each time unit accepted by ``time_unit`` arguments.
TIME_UNITS = {"s": 1.0, "min": 60.0, "h": 3600.0, "d": 86400.0}


def has_numpy() -> bool:
    """True when NumPy is available (arrays are returned instead of lists)."""
    return _np is not None


class Variable(NamedTuple):
    """A result variable: name (``"Pressure"``) and display unit (``"m"``)."""

    name: str
    unit: str


def _time_factor(unit: str) -> float:
    try:
        return TIME_UNITS[unit]
    except KeyError:
        raise ValueError(f"time_unit must be one of {', '.join(TIME_UNITS)}") from None


class Table:
    """Results of one class of elements: ``node``, ``link``, ``subcatchment`` or ``system``.

    Values are indexed by period, variable and element. Variables are given
    by name (case does not matter) or index, elements by ID or index.
    """

    def __init__(self, kind: str, ids: Sequence[str], variables: Sequence[Variable],
                 periods: int, data: Any, times: Sequence[float]) -> None:
        self.kind = kind
        self.ids: List[str] = list(ids)
        self.variables: List[Variable] = list(variables)
        self.periods = periods
        self._data = data  # ndarray (P, V, E) or flat array('f')
        self._numpy = not isinstance(data, array)
        self._times = times
        self._index = {e: i for i, e in enumerate(self.ids)}

    def __repr__(self) -> str:
        names = ", ".join(v.name for v in self.variables)
        return f"<Table {self.kind}: {len(self.ids)} elements, {self.periods} periods, variables {names}>"

    # -- lookup ----------------------------------------------------------------
    @property
    def variable_names(self) -> List[str]:
        return [v.name for v in self.variables]

    def variable_index(self, variable: Union[str, int]) -> int:
        """Index of a variable given by name (any case) or index."""
        if isinstance(variable, int):
            if not 0 <= variable < len(self.variables):
                raise IndexError(f"variable index {variable} out of range")
            return variable
        want = variable.lower()
        for i, v in enumerate(self.variables):
            if v.name.lower() == want:
                return i
        raise KeyError(f"no variable {variable!r} in the {self.kind} table "
                       f"(variables: {', '.join(self.variable_names)})")

    def element_index(self, element: Union[str, int]) -> int:
        """Index of an element given by ID or index."""
        if isinstance(element, int):
            if not 0 <= element < len(self.ids):
                raise IndexError(f"element index {element} out of range")
            return element
        try:
            return self._index[element]
        except KeyError:
            raise KeyError(f"no element {element!r} in the {self.kind} table") from None

    def unit(self, variable: Union[str, int]) -> str:
        return self.variables[self.variable_index(variable)].unit

    # -- values ----------------------------------------------------------------
    def _flat(self, p: int, v: int, e: int) -> int:
        return (p * len(self.variables) + v) * len(self.ids) + e

    def value(self, period: int, variable: Union[str, int], element: Union[str, int]) -> float:
        """One value."""
        v, e = self.variable_index(variable), self.element_index(element)
        if not -self.periods <= period < self.periods:
            raise IndexError(f"period {period} out of range")
        period %= self.periods
        if self._numpy:
            return float(self._data[period, v, e])
        return float(self._data[self._flat(period, v, e)])

    def values(self, variable: Union[str, int], elements: Optional[Sequence[Union[str, int]]] = None) -> Any:
        """All periods of one variable: shape (periods, elements)."""
        v = self.variable_index(variable)
        cols = None if elements is None else [self.element_index(e) for e in elements]
        if self._numpy:
            block = self._data[:, v, :]
            return block if cols is None else block[:, cols]
        n = len(self.ids)
        out = []
        for p in range(self.periods):
            start = self._flat(p, v, 0)
            row = list(self._data[start:start + n])
            out.append(row if cols is None else [row[c] for c in cols])
        return out

    def series(self, variable: Union[str, int], element: Union[str, int]) -> Any:
        """Time series of one element: shape (periods,)."""
        v, e = self.variable_index(variable), self.element_index(element)
        if self._numpy:
            return self._data[:, v, e]
        return [self._data[self._flat(p, v, e)] for p in range(self.periods)]

    def snapshot(self, variable: Union[str, int], period: int) -> Any:
        """Every element at one period: shape (elements,)."""
        v = self.variable_index(variable)
        if not -self.periods <= period < self.periods:
            raise IndexError(f"period {period} out of range")
        period %= self.periods
        if self._numpy:
            return self._data[period, v, :]
        start = self._flat(period, v, 0)
        return list(self._data[start:start + len(self.ids)])

    def element(self, element: Union[str, int]) -> Dict[str, Any]:
        """Every variable of one element: ``{variable name: series}``."""
        return {v.name: self.series(i, element) for i, v in enumerate(self.variables)}

    def to_numpy(self) -> Any:
        """The whole table as a NumPy array of shape (periods, variables, elements)."""
        if _np is None:
            raise ImportError("to_numpy() needs NumPy")
        if self._numpy:
            return self._data
        return _np.asarray(self._data, dtype="<f4").reshape(self.periods, len(self.variables), len(self.ids))

    def to_dataframe(self, variable: Union[str, int, None] = None,
                     elements: Optional[Sequence[Union[str, int]]] = None, time_unit: str = "h") -> Any:
        """A pandas DataFrame indexed by time.

        With ``variable`` the columns are element IDs; without it the columns
        are a MultiIndex of (variable, element). The index is the time from
        the start of the simulation in ``time_unit`` (``s``, ``min``, ``h``
        or ``d``).
        """
        try:
            import pandas as pd
        except ImportError:
            raise ImportError("to_dataframe() needs pandas (pip install pandas)") from None
        factor = _time_factor(time_unit)
        index = pd.Index([t / factor for t in self._times], name=f"time ({time_unit})")
        cols = list(range(len(self.ids))) if elements is None else [self.element_index(e) for e in elements]
        ids = [self.ids[c] for c in cols]
        if variable is not None:
            v = self.variable_index(variable)
            frame = pd.DataFrame(_np.asarray(self.values(v))[:, cols] if _np is not None else
                                 [[row[c] for c in cols] for row in self.values(v)],
                                 index=index, columns=pd.Index(ids, name=self.kind))
            frame.attrs.update(variable=self.variables[v].name, unit=self.variables[v].unit,
                               table=self.kind, time_unit=time_unit)
            return frame
        data = self.to_numpy()[:, :, cols].reshape(self.periods, -1)
        columns = pd.MultiIndex.from_product([self.variable_names, ids], names=["variable", self.kind])
        frame = pd.DataFrame(data, index=index, columns=columns)
        frame.attrs.update(units={v.name: v.unit for v in self.variables}, table=self.kind,
                           time_unit=time_unit)
        return frame


class Results:
    """The results of one run, read from an ``.anr`` file.

    Example::

        r = mirab.Results.load("model.anr")
        pressure = r.node.values("Pressure")          # (periods, nodes)
        j5 = r["node"].series("Pressure", "J5")
        df = r.node.to_dataframe("Pressure")          # needs pandas
    """

    def __init__(self, header: Dict[str, Any], tables: Dict[str, Table], path: Optional[Path] = None) -> None:
        self.path = path
        self.header = header
        self.engine_id: str = header.get("engineId", "")
        self.engine_name: str = header.get("engineName", "")
        self.title: str = header.get("title", "")
        self.flow_units: str = header.get("flowUnits", "")
        self.unit_system: str = header.get("unitSystem", "")
        self.report_start: float = float(header.get("reportStart", 0.0))
        self.report_step: float = float(header.get("reportStep", 0.0))
        self.times: List[float] = [float(t) for t in header.get("times", [])]
        self.metadata: Dict[str, str] = dict(header.get("metadata", {}))
        self.messages: List[str] = list(header.get("messages", []))
        self.tables: Dict[str, Table] = tables

    def __repr__(self) -> str:
        kinds = ", ".join(self.tables)
        return f"<Results {self.engine_name or self.engine_id}: {self.period_count} periods, tables {kinds}>"

    # -- reading ---------------------------------------------------------------
    @classmethod
    def load(cls, path: "PathLike", mmap: bool = False, cli: Union["Cli", "PathLike", None] = None) -> "Results":
        """Reads a results file.

        Args:
            path: an ``.anr`` file, or an EPANET/SWMM binary output file
                (converted with mirab-cli).
            mmap: map the values into memory instead of reading them (needs
                NumPy; the file stays open while the arrays are in use).
            cli: mirab-cli to use for the conversion of other formats.
        """
        p = Path(path)
        with open(p, "rb") as f:
            head = f.read(len(MAGIC))
        if head != MAGIC:
            return cls._convert(p, cli)
        return cls._read(p, mmap)

    @classmethod
    def _convert(cls, path: Path, cli: Union["Cli", "PathLike", None]) -> "Results":
        from ._cli import resolve_cli

        fd, tmp = tempfile.mkstemp(suffix=".anr", prefix="mirab-")
        os.close(fd)
        try:
            resolve_cli(cli).run(["results", path, "--format", "anr", "--out", tmp])
            r = cls._read(Path(tmp), mmap=False)
            r.path = path
            return r
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass

    @classmethod
    def _read(cls, path: Path, mmap: bool) -> "Results":
        with open(path, "rb") as f:
            pre = f.read(_PREAMBLE)
            if len(pre) < _PREAMBLE or pre[:len(MAGIC)] != MAGIC:
                raise ValueError(f"{path} is not a Mirab results file")
            version, header_len = struct.unpack_from("<II", pre, 16)
            if version > FORMAT_VERSION:
                raise ValueError(f"{path} uses results format {version}; this package reads up to "
                                 f"{FORMAT_VERSION}")
            header = json.loads(f.read(header_len).decode("utf-8"))
            periods = len(header.get("times", []))
            offset = _PREAMBLE + header_len
            tables: Dict[str, Table] = {}
            for t in header.get("tables", []):
                ids = list(t.get("ids", []))
                variables = [Variable(v.get("name", ""), v.get("unit", "")) for v in t.get("variables", [])]
                count = periods * len(variables) * len(ids)
                shape = (periods, len(variables), len(ids))
                if _np is not None and mmap and count:
                    data: Any = _np.memmap(path, dtype="<f4", mode="r", offset=offset, shape=shape)
                elif _np is not None:
                    f.seek(offset)
                    data = _np.fromfile(f, dtype="<f4", count=count).reshape(shape)
                    if data.size != count:
                        raise ValueError(f"{path} is truncated")
                else:
                    f.seek(offset)
                    raw = f.read(4 * count)
                    if len(raw) != 4 * count:
                        raise ValueError(f"{path} is truncated")
                    data = array("f")
                    data.frombytes(raw)
                    if sys.byteorder == "big":
                        data.byteswap()
                offset += 4 * count
                tables[t.get("kind", "")] = Table(t.get("kind", ""), ids, variables, periods, data,
                                                  header.get("times", []))
        return cls(header, tables, path)

    @classmethod
    def from_layout(cls, layout: Dict[str, Any], frames: Sequence[Any]) -> "Results":
        """Builds results from a live ``layout`` event and its ``frame`` events.

        Each frame must provide ``time`` and ``values`` (``{table kind:
        values of shape (variables, elements)}``) as :class:`mirab.FrameEvent` does.
        """
        header = {k: layout.get(k) for k in ("engineId", "engineName", "title", "flowUnits", "unitSystem",
                                             "reportStart", "reportStep")}
        header["tables"] = layout.get("tables", [])
        by_time: Dict[float, Any] = {}
        for fr in frames:
            by_time[float(fr.time)] = fr  # a repeated time replaces the earlier frame
        times = sorted(by_time)
        header["times"] = times
        tables: Dict[str, Table] = {}
        for t in header["tables"]:
            kind = t.get("kind", "")
            ids = list(t.get("ids", []))
            variables = [Variable(v.get("name", ""), v.get("unit", "")) for v in t.get("variables", [])]
            if _np is not None:
                data: Any = _np.stack([_np.asarray(by_time[s].values[kind], dtype="<f4") for s in times]) \
                    if times else _np.zeros((0, len(variables), len(ids)), dtype="<f4")
            else:
                data = array("f")
                for s in times:
                    for row in by_time[s].values[kind]:
                        data.extend(row)
            tables[kind] = Table(kind, ids, variables, len(times), data, times)
        return cls(header, tables, None)

    # -- access ----------------------------------------------------------------
    @property
    def period_count(self) -> int:
        return len(self.times)

    def __getitem__(self, kind: str) -> Table:
        try:
            return self.tables[kind]
        except KeyError:
            raise KeyError(f"no {kind!r} table (tables: {', '.join(self.tables)})") from None

    def __contains__(self, kind: object) -> bool:
        return kind in self.tables

    def __iter__(self) -> Iterator[Table]:
        return iter(self.tables.values())

    @property
    def node(self) -> Table:
        return self["node"]

    @property
    def link(self) -> Table:
        return self["link"]

    @property
    def subcatchment(self) -> Table:
        return self["subcatchment"]

    @property
    def system(self) -> Table:
        return self["system"]

    def time_index(self, time_unit: str = "h") -> List[float]:
        """Times of the periods from the start of the simulation."""
        factor = _time_factor(time_unit)
        return [t / factor for t in self.times]

    def period_at(self, seconds: float) -> int:
        """Period whose time is closest to ``seconds`` from the simulation start."""
        if not self.times:
            raise IndexError("the results have no periods")
        return min(range(len(self.times)), key=lambda i: abs(self.times[i] - seconds))

    def to_dataframe(self, kind: str, variable: Union[str, int, None] = None,
                     elements: Optional[Sequence[Union[str, int]]] = None, time_unit: str = "h") -> Any:
        """Shortcut for ``results[kind].to_dataframe(...)``."""
        return self[kind].to_dataframe(variable, elements, time_unit)

    def summary(self) -> Dict[str, Any]:
        """Header information without values (as ``mirab-cli results --json`` prints it)."""
        out = dict(self.header)
        out["periods"] = self.period_count
        return out


def load_results(path: "PathLike", mmap: bool = False) -> Results:
    """Reads a results file (see :meth:`Results.load`)."""
    return Results.load(path, mmap=mmap)
