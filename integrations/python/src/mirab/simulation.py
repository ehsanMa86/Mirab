# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/simulation.py
# Purpose: Running simulations through the mirab-cli solver protocol: progress, live frames, pause and cancel.
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
"""Running simulations and analyses through the solver protocol.

``mirab-cli run ... --machine`` writes one JSON object per line (progress,
log, live ``layout`` and ``frame`` events, ``paused``, ``resumed`` and
``done``) and reads ``pause``, ``resume`` and ``cancel`` on standard input.
:class:`Simulation` wraps that protocol; ``docs/file-formats.md`` describes it.
"""

from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
import threading
from array import array
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, Iterator, List, Optional, Sequence, Union

from ._cli import Cli, CliError, PathLike, _hidden_window, resolve_cli
from .results import Results, _np

#: Exit code of a cancelled run.
EXIT_CANCELLED = 3


@dataclass
class Event:
    """One line of the solver protocol.

    ``type`` is ``progress``, ``log``, ``layout``, ``frame``, ``paused``,
    ``resumed`` or ``done``; the other members are in ``data`` and can also
    be read as attributes (``event.fraction``, ``event.message``, ...).
    """

    type: str
    data: Dict[str, Any] = field(default_factory=dict)

    def __getattr__(self, name: str) -> Any:
        data = self.__dict__.get("data", {})
        if name in data:
            return data[name]
        raise AttributeError(f"{self.__dict__.get('type', 'event')} event has no {name!r}")

    def get(self, name: str, default: Any = None) -> Any:
        return self.data.get(name, default)


class FrameEvent(Event):
    """A live ``frame``: the results of one reporting period during the run.

    ``values`` maps each table kind to its values with shape (variables,
    elements), decoded on first use; ``layout`` is the matching ``layout``
    event's data (tables, element IDs and variables).
    """

    def __init__(self, data: Dict[str, Any], layout: Optional[Dict[str, Any]]) -> None:
        super().__init__("frame", data)
        self.layout_data = layout or {}
        self._values: Optional[Dict[str, Any]] = None

    @property
    def period(self) -> int:
        return int(self.data.get("period", 0))

    @property
    def time(self) -> float:
        return float(self.data.get("time", 0.0))

    @property
    def values(self) -> Dict[str, Any]:
        if self._values is None:
            self._values = decode_frame(self.layout_data, self.data.get("data", ""))
        return self._values

    def table(self, kind: str) -> Any:
        return self.values[kind]

    def value(self, kind: str, variable: str, element: str) -> float:
        """One value of this period, e.g. ``frame.value("node", "Pressure", "J5")``."""
        for t in self.layout_data.get("tables", []):
            if t.get("kind") != kind:
                continue
            names = [v.get("name", "").lower() for v in t.get("variables", [])]
            v = names.index(variable.lower())
            e = list(t.get("ids", [])).index(element)
            return float(self.values[kind][v][e])
        raise KeyError(f"no {kind!r} table in the layout")


def decode_frame(layout: Dict[str, Any], data: str) -> Dict[str, Any]:
    """Decodes the base64 ``data`` of a frame into ``{kind: (variables, elements)}`` blocks."""
    raw = base64.b64decode(data) if data else b""
    out: Dict[str, Any] = {}
    offset = 0
    for t in layout.get("tables", []):
        nv, ne = len(t.get("variables", [])), len(t.get("ids", []))
        count = nv * ne
        chunk = raw[offset:offset + 4 * count]
        offset += 4 * count
        if _np is not None:
            out[t.get("kind", "")] = _np.frombuffer(chunk, dtype="<f4").reshape(nv, ne)
        else:
            a = array("f")
            a.frombytes(chunk)
            if sys.byteorder == "big":
                a.byteswap()
            out[t.get("kind", "")] = [list(a[i * ne:(i + 1) * ne]) for i in range(nv)]
    if offset != len(raw):
        raise ValueError("frame data does not match its layout")
    return out


@dataclass
class RunResult:
    """Outcome of a simulation.

    ``results_path`` and ``report_path`` are set when the run succeeded;
    :attr:`results` reads the results file on first use.
    """

    success: bool
    cancelled: bool = False
    message: str = ""
    engine: str = ""
    seconds: float = 0.0
    periods: int = 0
    results_path: Optional[Path] = None
    report_path: Optional[Path] = None
    returncode: int = 0
    log: List[Dict[str, Any]] = field(default_factory=list)
    _results: Optional[Results] = field(default=None, repr=False)

    @property
    def results(self) -> Results:
        if self._results is None:
            if not self.results_path:
                raise RuntimeError(f"the run wrote no results: {self.message}")
            self._results = Results.load(self.results_path)
        return self._results

    @property
    def warnings(self) -> List[str]:
        return [e.get("message", "") for e in self.log if e.get("level") in ("warning", "error")]

    def raise_for_status(self) -> "RunResult":
        """Raises :class:`CliError` when the run failed or was cancelled."""
        if not self.success:
            raise CliError(self.message or ("cancelled" if self.cancelled else "the run failed"),
                           self.returncode)
        return self

    @classmethod
    def from_done(cls, done: Dict[str, Any], returncode: int, log: List[Dict[str, Any]]) -> "RunResult":
        return cls(success=bool(done.get("success")), cancelled=bool(done.get("cancelled")),
                   message=str(done.get("message", "")), engine=str(done.get("engine", "")),
                   seconds=float(done.get("seconds", 0.0)), periods=int(done.get("periods", 0)),
                   results_path=Path(done["results"]) if done.get("results") else None,
                   report_path=Path(done["report"]) if done.get("report") else None,
                   returncode=returncode, log=log)


class Simulation:
    """A running ``mirab-cli`` process in machine mode.

    Iterate over :meth:`events` (or call :meth:`wait`) and control the run
    with :meth:`pause`, :meth:`resume` and :meth:`cancel`, from the loop or
    from another thread::

        with project.start(live=True) as sim:
            for event in sim.events():
                if event.type == "frame" and event.value("node", "Pressure", "J5") < 20:
                    sim.cancel()
        print(sim.result)
    """

    def __init__(self, args: Sequence[Any], cli: Union[Cli, PathLike, None] = None,
                 on_finish: Optional[Callable[[], None]] = None) -> None:
        self.cli = resolve_cli(cli)
        self.command = self.cli.command(list(args) + ["--machine"])
        self._on_finish = on_finish
        self._proc = subprocess.Popen(self.command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, encoding="utf-8", errors="replace",
                                      bufsize=1, **_hidden_window())
        self._stderr: List[str] = []
        self._stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self._stderr_thread.start()
        self._lock = threading.Lock()
        self.layout: Optional[Dict[str, Any]] = None
        self.log: List[Dict[str, Any]] = []
        self.done: Optional[Dict[str, Any]] = None
        self.result: Optional[RunResult] = None
        self.paused = False
        self.progress = 0.0
        self.stage = ""
        self._iterating = False

    def _drain_stderr(self) -> None:
        assert self._proc.stderr is not None
        for line in self._proc.stderr:
            self._stderr.append(line)

    # -- control -----------------------------------------------------------------
    def _send(self, command: str) -> None:
        with self._lock:
            stdin = self._proc.stdin
            if stdin is None or stdin.closed or self._proc.poll() is not None:
                return
            try:
                stdin.write(command + "\n")
                stdin.flush()
            except (BrokenPipeError, OSError, ValueError):
                pass

    def pause(self) -> None:
        """Holds the engine between two time steps until :meth:`resume`."""
        self._send("pause")

    def resume(self) -> None:
        self._send("resume")

    def cancel(self) -> None:
        """Stops the run at the next time step; no results file is written."""
        self._send("cancel")

    @property
    def running(self) -> bool:
        return self._proc.poll() is None

    # -- events ------------------------------------------------------------------
    def events(self) -> Iterator[Event]:
        """Yields protocol events until the process ends (``done`` is the last)."""
        if self._iterating:
            raise RuntimeError("events() is already being read")
        self._iterating = True
        assert self._proc.stdout is not None
        try:
            for line in self._proc.stdout:
                line = line.strip()
                if not line.startswith("{"):
                    continue
                try:
                    data = json.loads(line)
                except ValueError:
                    continue
                kind = str(data.pop("event", ""))
                event: Event
                if kind == "frame":
                    event = FrameEvent(data, self.layout)
                else:
                    event = Event(kind, data)
                    if kind == "layout":
                        self.layout = data
                    elif kind == "progress":
                        self.progress = float(data.get("fraction", 0.0))
                        self.stage = str(data.get("stage", ""))
                    elif kind == "log":
                        self.log.append(data)
                    elif kind == "paused":
                        self.paused = True
                    elif kind == "resumed":
                        self.paused = False
                    elif kind == "done":
                        self.done = data
                yield event
        finally:
            self._finish()

    def _finish(self) -> None:
        if self.result is not None:
            return
        if self._proc.stdin and not self._proc.stdin.closed:
            try:
                self._proc.stdin.close()
            except OSError:
                pass
        returncode = self._proc.wait()
        self._stderr_thread.join(timeout=5)
        done = self.done
        if done is None:
            message = "".join(self._stderr).strip() or f"mirab-cli ended with exit code {returncode}"
            done = {"success": False, "message": message, "cancelled": returncode == EXIT_CANCELLED}
        self.result = RunResult.from_done(done, returncode, self.log)
        if self._on_finish:
            try:
                self._on_finish()
            finally:
                self._on_finish = None

    def wait(self) -> RunResult:
        """Reads the remaining events and returns the outcome."""
        if not self._iterating:
            for _ in self.events():
                pass
        else:
            self._finish()
        assert self.result is not None
        return self.result

    @property
    def stderr(self) -> str:
        return "".join(self._stderr)

    def __enter__(self) -> "Simulation":
        return self

    def __exit__(self, *exc: Any) -> None:
        if self.running:
            self.cancel()
        if self.result is None:
            if self._iterating:
                self._finish()
            else:
                self.wait()


EventCallback = Callable[[Event, Simulation], Any]
ProgressCallback = Callable[[float, str], Any]


def run_args(inp: PathLike, engine: Optional[str] = None, out: Optional[PathLike] = None,
             report: Optional[PathLike] = None, msx: Optional[PathLike] = None,
             live: bool = False, frame_interval: Optional[float] = None,
             scenario: Optional[str] = None) -> List[Any]:
    """Arguments of ``mirab-cli run`` (without ``--machine``)."""
    args: List[Any] = ["run", os.fspath(inp)]
    if scenario:
        args += ["--scenario", scenario]
    if engine:
        args += ["--engine", engine]
    if msx:
        args += ["--msx", os.fspath(msx)]
    if out:
        args += ["--out", os.fspath(out)]
    if report:
        args += ["--report", os.fspath(report)]
    if live:
        args.append("--live")
        if frame_interval is not None:
            args += ["--frame-interval", repr(float(frame_interval))]
    return args


def drive(sim: Simulation, callback: Optional[EventCallback] = None,
          progress: Optional[ProgressCallback] = None) -> RunResult:
    """Feeds every event of a simulation to the callbacks and returns the outcome."""
    for event in sim.events():
        if progress is not None and event.type == "progress":
            progress(float(event.get("fraction", 0.0)), str(event.get("stage", "")))
        if callback is not None:
            callback(event, sim)
    assert sim.result is not None
    return sim.result


def run(inp: PathLike, engine: Optional[str] = None, out: Optional[PathLike] = None,
        report: Optional[PathLike] = None, msx: Optional[PathLike] = None,
        callback: Optional[EventCallback] = None, progress: Optional[ProgressCallback] = None,
        live: Optional[bool] = None, frame_interval: Optional[float] = None,
        cli: Union[Cli, PathLike, None] = None, check: bool = False,
        scenario: Optional[str] = None) -> RunResult:
    """Runs a model file with mirab-cli.

    Args:
        inp: the ``.inp`` file.
        scenario: run this scenario of the project (see :func:`mirab.scenarios`);
            its results go to ``<model>.<scenario>.anr`` unless ``out`` is given.
        engine: engine ID (``epanet-2.3-owa``, ``swmm-5.2-epa``, ...); the
            project's engine by default.
        out: results file (default: next to the model with ``.anr``).
        callback: called as ``callback(event, simulation)`` for every
            protocol event; it may call ``simulation.pause()`` or ``cancel()``.
        progress: called as ``progress(fraction, stage)``.
        live: stream ``layout``/``frame`` events (default: when a callback is given).
        frame_interval: minimum seconds between frames (0 sends every period).
        check: raise :class:`CliError` when the run fails.
    """
    live = callback is not None if live is None else live
    sim = Simulation(run_args(inp, engine, out, report, msx, live, frame_interval, scenario), cli)
    result = drive(sim, callback, progress)
    return result.raise_for_status() if check else result


def machine_payload(args: Sequence[Any], cli: Union[Cli, PathLike, None] = None,
                    progress: Optional[ProgressCallback] = None) -> Any:
    """Runs an analysis in machine mode and returns the ``payload`` of its ``done`` event."""
    sim = Simulation(args, cli)
    drive(sim, None, progress)
    done = sim.done or {}
    if not done.get("success"):
        message = done.get("message") or sim.stderr.strip() or "the analysis failed"
        raise CliError(str(message), sim.result.returncode if sim.result else 1, sim.stderr, sim.command)
    return done.get("payload")
