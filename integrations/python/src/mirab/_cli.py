# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/_cli.py
# Purpose: Locating and calling mirab-cli from Python.
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
"""Locating and calling ``mirab-cli``.

Every model operation of the package goes through the command-line program
that ships with Mirab, so scripts see exactly what the desktop application
sees. Only reading ``.anr`` result files is done in Python.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable, List, Optional, Sequence, Union

PathLike = Union[str, "os.PathLike[str]"]

#: Environment variable that names the mirab-cli program (or its folder).
ENV_VAR = "MIRAB_CLI"

_EXE = "mirab-cli.exe" if os.name == "nt" else "mirab-cli"


class CliNotFoundError(FileNotFoundError):
    """mirab-cli could not be found (see :func:`find_cli`)."""


class CliError(RuntimeError):
    """mirab-cli ended with an error.

    Attributes:
        returncode: exit code (1 failure, 2 the project cannot run, 3 cancelled).
        stderr: the program's error output.
        command: the argument list that was run.
    """

    def __init__(self, message: str, returncode: int = 1, stderr: str = "",
                 command: Optional[Sequence[str]] = None) -> None:
        super().__init__(message)
        self.returncode = returncode
        self.stderr = stderr
        self.command = list(command or [])


def _standard_locations() -> List[Path]:
    """Places where installers put mirab-cli, most likely first."""
    home = Path.home()
    out: List[Path] = []
    if sys.platform.startswith("win"):
        for var in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)", "LOCALAPPDATA"):
            base = os.environ.get(var)
            if not base:
                continue
            root = Path(base) / ("Programs" if var == "LOCALAPPDATA" else "") / "Mirab"
            out += [root / "bin" / _EXE, root / _EXE]
    elif sys.platform == "darwin":
        for apps in (Path("/Applications"), home / "Applications"):
            out.append(apps / "Mirab.app" / "Contents" / "MacOS" / _EXE)
        out += [Path("/usr/local/bin") / _EXE, Path("/opt/homebrew/bin") / _EXE]
    else:
        out += [Path("/usr/bin") / _EXE, Path("/usr/local/bin") / _EXE,
                Path("/opt/mirab/bin") / _EXE, Path("/opt/Mirab/bin") / _EXE,
                home / ".local" / "bin" / _EXE]
    return out


def _as_program(candidate: Path) -> Optional[Path]:
    if candidate.is_dir():
        for sub in (candidate / _EXE, candidate / "bin" / _EXE):
            if sub.is_file():
                return sub
        return None
    return candidate if candidate.is_file() else None


def find_cli(required: bool = True) -> Optional[str]:
    """Returns the path of mirab-cli.

    The search order is the ``MIRAB_CLI`` environment variable (the program
    or the folder that holds it), the ``PATH``, then the standard install
    locations of each operating system (``C:\\Program Files\\Mirab\\bin`` on
    Windows, ``/Applications/Mirab.app/Contents/MacOS`` on macOS,
    ``/usr/bin``, ``/usr/local/bin``, ``/opt/mirab/bin`` and ``~/.local/bin``
    on Linux).

    Args:
        required: raise :class:`CliNotFoundError` when nothing is found
            (otherwise return ``None``).
    """
    env = os.environ.get(ENV_VAR)
    if env:
        found = _as_program(Path(env).expanduser())
        if found:
            return str(found)
        if required:
            raise CliNotFoundError(f"{ENV_VAR}={env} is not mirab-cli or a folder that holds it")
        return None
    on_path = shutil.which("mirab-cli")
    if on_path:
        return on_path
    for candidate in _standard_locations():
        if candidate.is_file():
            return str(candidate)
    if required:
        raise CliNotFoundError(
            "mirab-cli was not found; install Mirab, add its bin folder to PATH or set "
            f"{ENV_VAR} to the program")
    return None


class Cli:
    """A mirab-cli program: runs commands and decodes their JSON output."""

    def __init__(self, program: Optional[PathLike] = None) -> None:
        self.program = str(program) if program is not None else find_cli()

    def __repr__(self) -> str:
        return f"Cli({self.program!r})"

    def command(self, args: Iterable[Any]) -> List[str]:
        return [self.program] + [os.fspath(a) if isinstance(a, os.PathLike) else str(a) for a in args]

    def run(self, args: Iterable[Any], input: Optional[str] = None,
            ok_codes: Sequence[int] = (0,)) -> subprocess.CompletedProcess:
        """Runs a command and returns the completed process (text mode).

        Raises:
            CliError: the exit code is not in ``ok_codes``.
        """
        cmd = self.command(args)
        proc = subprocess.run(cmd, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              encoding="utf-8", errors="replace", **_hidden_window())
        if proc.returncode not in ok_codes:
            message = proc.stderr.strip() or proc.stdout.strip() or f"exit code {proc.returncode}"
            if message.startswith("error: "):
                message = message[len("error: "):]
            raise CliError(message, proc.returncode, proc.stderr, cmd)
        return proc

    def json(self, args: Iterable[Any], input: Optional[str] = None,
             ok_codes: Sequence[int] = (0,)) -> Any:
        """Runs a command that prints one JSON document and returns it decoded."""
        proc = self.run(args, input=input, ok_codes=ok_codes)
        try:
            return json.loads(proc.stdout)
        except ValueError as exc:
            raise CliError(f"mirab-cli did not print JSON: {exc}", proc.returncode, proc.stderr,
                           self.command(args)) from exc

    def version(self) -> str:
        """Mirab version, e.g. ``"0.2.0"``."""
        return str(self.json(["version", "--json"])["version"])


def _hidden_window() -> dict:
    """Keeps a console window from flashing up on Windows."""
    if os.name == "nt":
        return {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0)}
    return {}


_default: Optional[Cli] = None


def default_cli() -> Cli:
    """The :class:`Cli` used when none is given (found once, then reused)."""
    global _default
    if _default is None:
        _default = Cli()
    return _default


def set_cli(program: Optional[PathLike]) -> Cli:
    """Chooses the mirab-cli program used by default (``None`` searches again)."""
    global _default
    _default = Cli(program) if program is not None else Cli()
    return _default


def resolve_cli(cli: Union[Cli, PathLike, None]) -> Cli:
    if cli is None:
        return default_cli()
    if isinstance(cli, Cli):
        return cli
    return Cli(cli)
