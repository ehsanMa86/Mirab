# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/batch.py
# Purpose: Running many variants of a model in parallel mirab-cli processes.
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
"""Running many variants of a model in parallel.

Each case is a list of edits (see :func:`mirab.edit`, :func:`mirab.option`
and :func:`mirab.rows`). Its model is written by ``mirab-cli model set``
next to the original (so relative paths in the project settings still
work) and run by its own ``mirab-cli`` process; up to ``workers`` cases run
at a time.
"""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Sequence, Union

from ._cli import Cli, CliError, PathLike, resolve_cli
from .project import sidecar_of, write_model
from .results import Results


@dataclass
class Case:
    """One variant: a name and the edits that make it."""

    name: str
    edits: List[Dict[str, Any]] = field(default_factory=list)
    engine: Optional[str] = None
    #: Free data for the caller (the sampled parameters, ...), returned unchanged.
    params: Dict[str, Any] = field(default_factory=dict)


@dataclass
class CaseResult:
    """Outcome of one case.

    ``value`` is what ``reduce`` returned; ``results_path`` is kept when
    the results files are kept.
    """

    case: Case
    success: bool
    message: str = ""
    seconds: float = 0.0
    value: Any = None
    results_path: Optional[Path] = None

    @property
    def name(self) -> str:
        return self.case.name

    def results(self) -> Results:
        if not self.results_path:
            raise RuntimeError(f"case {self.name}: no results were kept ({self.message})")
        return Results.load(self.results_path)


CaseSpec = Union[Case, Sequence[Mapping[str, Any]]]


def _cases(cases: Union[Iterable[CaseSpec], Mapping[str, Sequence[Mapping[str, Any]]]]) -> List[Case]:
    if isinstance(cases, Mapping):
        return [Case(str(name), [dict(e) for e in edits]) for name, edits in cases.items()]
    out: List[Case] = []
    for i, c in enumerate(cases):
        out.append(c if isinstance(c, Case) else Case(f"case-{i + 1}", [dict(e) for e in c]))
    names = [c.name for c in out]
    if len(set(names)) != len(names):
        raise ValueError("case names must be unique")
    return out


def _safe(name: str) -> str:
    return "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in name) or "case"


def run_batch(model: PathLike, cases: Union[Iterable[CaseSpec], Mapping[str, Sequence[Mapping[str, Any]]]],
              engine: Optional[str] = None, reduce: Optional[Callable[[Results], Any]] = None,
              workers: Optional[int] = None, out_dir: Optional[PathLike] = None,
              keep_results: Optional[bool] = None, cli: Union[Cli, PathLike, None] = None,
              on_done: Optional[Callable[[CaseResult], Any]] = None) -> List[CaseResult]:
    """Runs every case and returns their outcomes in the order given.

    Args:
        model: the base ``.inp`` file (left unchanged).
        cases: :class:`Case` objects, lists of edits, or ``{name: edits}``.
        engine: engine ID for every case (a :class:`Case` may override it).
        reduce: called with the :class:`Results` of each successful case, in
            the worker thread; its return value becomes ``CaseResult.value``.
        workers: cases run at a time (default: number of CPUs).
        out_dir: folder for results and reports (default: a new temporary folder).
        keep_results: keep the ``.anr`` files (default: when ``reduce`` is not given).
        on_done: called with each :class:`CaseResult` as soon as it is known.
    """
    c = resolve_cli(cli)
    base = Path(model)
    if not base.is_file():
        raise FileNotFoundError(f"no such model: {base}")
    items = _cases(cases)
    keep = reduce is None if keep_results is None else keep_results
    folder = Path(out_dir) if out_dir is not None else Path(tempfile.mkdtemp(prefix="mirab-batch-"))
    folder.mkdir(parents=True, exist_ok=True)
    tag = uuid.uuid4().hex[:8]

    def one(index: int, case: Case) -> CaseResult:
        stem = f"{index + 1:04d}-{_safe(case.name)}"
        inp = base.with_name(f".{base.stem}.batch-{tag}-{stem}{base.suffix}")
        anr = folder / f"{stem}.anr"
        rpt = folder / f"{stem}.rpt"
        try:
            write_model(base, inp, case.edits, c)
            args: List[Any] = ["run", inp, "--out", anr, "--report", rpt, "--json"]
            eng = case.engine or engine
            if eng:
                args += ["--engine", eng]
            proc = c.run(args, ok_codes=(0, 1, 3))
            try:
                done = json.loads(proc.stdout)
            except ValueError:
                message = proc.stderr.strip() or f"exit code {proc.returncode}"
                return CaseResult(case, False, message[7:] if message.startswith("error: ") else message)
            result = CaseResult(case, bool(done.get("success")), str(done.get("message", "")),
                                float(done.get("seconds", 0.0)))
            if result.success:
                if reduce is not None:
                    try:
                        result.value = reduce(Results.load(anr))
                    except Exception as exc:  # reported per case, the other cases go on
                        result.success = False
                        result.message = f"reduce failed: {exc!r}"
                if keep:
                    result.results_path = anr
            return result
        except CliError as exc:
            return CaseResult(case, False, str(exc))
        finally:
            for f in (inp, sidecar_of(inp)) + (() if keep else (anr, rpt)):
                try:
                    f.unlink()
                except OSError:
                    pass

    results: List[Optional[CaseResult]] = [None] * len(items)
    with ThreadPoolExecutor(max_workers=workers or os.cpu_count() or 2) as pool:
        futures = {pool.submit(one, i, case): i for i, case in enumerate(items)}
        for fut in as_completed(futures):
            r = fut.result()
            results[futures[fut]] = r
            if on_done is not None:
                on_done(r)
    if not keep and out_dir is None:
        shutil.rmtree(folder, ignore_errors=True)
    return [r for r in results if r is not None]
