# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/src/mirab/mcp.py
# Purpose: Model Context Protocol server (JSON-RPC over standard input and output) for Mirab models.
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
"""A Model Context Protocol server for Mirab models.

Run ``python -m mirab.mcp`` (or ``mirab-mcp``). The server reads JSON-RPC
2.0 messages, one per line, on standard input and answers on standard
output, as the protocol's stdio transport specifies. It offers tools to
summarise a model, list and read elements, change values, check engine
compatibility, run simulations and query results. Only the standard
library and this package are used.
"""

from __future__ import annotations

import json
import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, TextIO, Tuple

from . import __version__, analysis
from ._cli import Cli, CliError, resolve_cli
from .project import Project, write_model
from .results import Results
from .simulation import run as run_model

#: Protocol revisions this server implements, newest first.
PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603

_MAX_VALUES = 5000


class ToolError(Exception):
    """A tool failed in a way the client should see (``isError`` result)."""


def _schema(properties: Dict[str, Any], required: Tuple[str, ...] = ()) -> Dict[str, Any]:
    return {"type": "object", "properties": properties, "required": list(required), "additionalProperties": False}


_PATH = {"type": "string", "description": "Path of the EPANET or SWMM .inp file"}
_ENGINE = {"type": "string", "description": "Engine ID, e.g. epanet-2.3-owa or swmm-5.2-epa (default: the "
                                            "project's engine)"}

TOOLS: List[Dict[str, Any]] = [
    {"name": "model_summary",
     "title": "Model summary",
     "description": "Summarise a water distribution (EPANET) or stormwater (SWMM) model: format, units, "
                    "times, element counts by kind, options, detected features and validation messages.",
     "inputSchema": _schema({"path": _PATH}, ("path",))},
    {"name": "list_elements",
     "title": "List elements",
     "description": "List the elements of a model with their main values. category is node, link, "
                    "subcatchment, gage or a named object kind (Pollutant, Landuse, ...); kind filters by "
                    "element type (Junction, Pipe, Conduit, ...).",
     "inputSchema": _schema({"path": _PATH,
                             "category": {"type": "string", "default": "node"},
                             "kind": {"type": "string"},
                             "offset": {"type": "integer", "minimum": 0, "default": 0},
                             "limit": {"type": "integer", "minimum": 1, "maximum": 2000, "default": 200}},
                            ("path",))},
    {"name": "get_element",
     "title": "Get element",
     "description": "Every value of one element or named object: its own row by column name and the rows "
                    "of other sections keyed by it.",
     "inputSchema": _schema({"path": _PATH, "id": {"type": "string"},
                             "category": {"type": "string", "description": "node, link, subcatchment, gage or "
                                                                           "an object kind, when IDs repeat"}},
                            ("path", "id"))},
    {"name": "set_values",
     "title": "Set values",
     "description": "Change values of a model through Mirab, keeping the rest of the file as written. Each "
                    "edit is {id, column, value[, section, row, category]} for an element or object value, "
                    "{option, value[, section]} for an option (null removes it) or {section, id, rows} to "
                    "replace the rows of a pattern, curve or time series. Writes the model in place unless "
                    "out is given.",
     "inputSchema": _schema({"path": _PATH,
                             "edits": {"type": "array", "items": {"type": "object"}, "minItems": 1},
                             "out": {"type": "string", "description": "Write the changed model here"}},
                            ("path", "edits"))},
    {"name": "check_compatibility",
     "title": "Check compatibility",
     "description": "Tell whether an engine can run the model and list what it ignores or cannot run.",
     "inputSchema": _schema({"path": _PATH, "engine": _ENGINE}, ("path",))},
    {"name": "run_simulation",
     "title": "Run simulation",
     "description": "Run the model and return the outcome, the warnings and the tables and variables of "
                    "the results file (default: next to the model with the .anr extension).",
     "inputSchema": _schema({"path": _PATH, "engine": _ENGINE,
                             "out": {"type": "string", "description": "Results file to write"}}, ("path",))},
    {"name": "query_results",
     "title": "Query results",
     "description": "Read values from a results file (.anr). Give elements for their time series, period "
                    "for every element at one reporting period, or neither for the minimum, mean and "
                    "maximum of each element.",
     "inputSchema": _schema({"results": {"type": "string", "description": "Path of the .anr file"},
                             "table": {"type": "string", "description": "node, link, subcatchment or system"},
                             "variable": {"type": "string", "description": "e.g. Pressure, Flow, Depth"},
                             "elements": {"type": "array", "items": {"type": "string"}},
                             "period": {"type": "integer", "minimum": 0}},
                            ("results", "table", "variable"))},
    {"name": "list_engines",
     "title": "List engines",
     "description": "List the simulation engines Mirab knows and whether each is installed.",
     "inputSchema": _schema({})},
]


class Server:
    """Handles protocol messages; :meth:`serve` runs the stdio loop."""

    def __init__(self, cli: Optional[Cli] = None) -> None:
        self._cli = cli
        self.protocol_version: Optional[str] = None
        self.initialized = False
        self._projects: Dict[str, Tuple[float, Project]] = {}
        self._tools: Dict[str, Callable[[Dict[str, Any]], Any]] = {
            "model_summary": self.model_summary, "list_elements": self.list_elements,
            "get_element": self.get_element, "set_values": self.set_values,
            "check_compatibility": self.check_compatibility, "run_simulation": self.run_simulation,
            "query_results": self.query_results, "list_engines": self.list_engines}

    @property
    def cli(self) -> Cli:
        if self._cli is None:
            self._cli = resolve_cli(None)
        return self._cli

    # -- JSON-RPC ------------------------------------------------------------------
    def handle(self, message: Any) -> Optional[Any]:
        """Answers one decoded message (or a batch); ``None`` for notifications."""
        if isinstance(message, list):
            replies = [r for r in (self.handle(m) for m in message) if r is not None]
            return replies or None
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
            return _error(None, INVALID_REQUEST, "not a JSON-RPC 2.0 message")
        method = message.get("method")
        msg_id = message.get("id")
        if not isinstance(method, str):
            # A response to a request of ours (none are sent) or garbage.
            return None if "result" in message or "error" in message else \
                _error(msg_id, INVALID_REQUEST, "missing method")
        is_request = "id" in message
        params = message.get("params") or {}
        try:
            result = self.dispatch(method, params)
        except _RpcError as exc:
            return _error(msg_id, exc.code, str(exc)) if is_request else None
        except Exception as exc:  # pragma: no cover - defensive
            traceback.print_exc(file=sys.stderr)
            return _error(msg_id, INTERNAL_ERROR, str(exc)) if is_request else None
        if not is_request:
            return None
        return {"jsonrpc": "2.0", "id": msg_id, "result": result}

    def dispatch(self, method: str, params: Dict[str, Any]) -> Any:
        if method == "initialize":
            requested = str(params.get("protocolVersion", ""))
            self.protocol_version = requested if requested in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0]
            return {"protocolVersion": self.protocol_version,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "mirab", "title": "Mirab water models", "version": __version__},
                    "instructions": "Tools for EPANET and SWMM models through Mirab. Paths are files on this "
                                    "computer; set_values and run_simulation write files."}
        if method == "notifications/initialized":
            self.initialized = True
            return None
        if method == "ping":
            return {}
        if method.startswith("notifications/"):
            return None
        if method == "tools/list":
            return {"tools": TOOLS}
        if method == "tools/call":
            name = params.get("name")
            if name not in self._tools:
                raise _RpcError(INVALID_PARAMS, f"unknown tool: {name}")
            args = params.get("arguments") or {}
            if not isinstance(args, dict):
                raise _RpcError(INVALID_PARAMS, "arguments must be an object")
            return self.call(name, args)
        raise _RpcError(METHOD_NOT_FOUND, f"method not found: {method}")

    def call(self, name: str, args: Dict[str, Any]) -> Dict[str, Any]:
        """Runs a tool; failures become ``isError`` results the client can read."""
        try:
            for key in next(t for t in TOOLS if t["name"] == name)["inputSchema"]["required"]:
                if key not in args:
                    raise ToolError(f"missing argument: {key}")
            value = self._tools[name](args)
        except (ToolError, CliError, KeyError, ValueError, FileNotFoundError, IndexError) as exc:
            message = exc.args[0] if isinstance(exc, KeyError) and exc.args else str(exc)
            return {"content": [{"type": "text", "text": f"Error: {message}"}], "isError": True}
        structured = value if isinstance(value, dict) else {"result": value}
        return {"content": [{"type": "text", "text": json.dumps(structured, ensure_ascii=False)}],
                "structuredContent": structured, "isError": False}

    def serve(self, stdin: Optional[TextIO] = None, stdout: Optional[TextIO] = None) -> None:
        """Reads messages until standard input closes."""
        inp = stdin or sys.stdin
        out = stdout or sys.stdout
        for line in inp:
            line = line.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
            except ValueError:
                reply: Any = _error(None, PARSE_ERROR, "invalid JSON")
            else:
                reply = self.handle(message)
            if reply is not None:
                out.write(json.dumps(reply, ensure_ascii=False, separators=(",", ":")) + "\n")
                out.flush()

    # -- tools -----------------------------------------------------------------------
    def _project(self, path: str) -> Project:
        p = Path(path).expanduser()
        if not p.is_file():
            raise ToolError(f"no such model: {p}")
        key = str(p.resolve())
        stamp = p.stat().st_mtime
        cached = self._projects.get(key)
        if cached and cached[0] == stamp:
            return cached[1]
        project = Project.open(p, self.cli)
        self._projects[key] = (stamp, project)
        return project

    def model_summary(self, args: Dict[str, Any]) -> Dict[str, Any]:
        project = self._project(args["path"])
        info = analysis.info(project.path, cli=self.cli)
        info["times"] = project.times
        info["options"] = project.options
        info["objects"] = {kind: len(items) for kind, items in project.objects.items() if items}
        return info

    def list_elements(self, args: Dict[str, Any]) -> Dict[str, Any]:
        project = self._project(args["path"])
        category = str(args.get("category", "node")).lower()
        groups = {"node": project.nodes, "link": project.links, "subcatchment": project.subcatchments,
                  "gage": project.gages}
        groups.update({k.lower(): v for k, v in project.objects.items()})
        if category not in groups:
            raise ToolError(f"unknown category {category}; use one of {', '.join(groups)}")
        items = list(groups[category].values())
        if args.get("kind"):
            items = [e for e in items if e.kind.lower() == str(args["kind"]).lower()]
        offset, limit = int(args.get("offset", 0)), int(args.get("limit", 200))
        page = items[offset:offset + limit]
        rows = []
        for e in page:
            row: Dict[str, Any] = {"id": e.id, "kind": e.kind}
            row.update(e.properties if e.properties else {})
            rows.append(row)
        return {"total": len(items), "offset": offset, "count": len(rows), "elements": rows}

    def get_element(self, args: Dict[str, Any]) -> Dict[str, Any]:
        project = self._project(args["path"])
        return project.element(str(args["id"]), args.get("category")).raw

    def set_values(self, args: Dict[str, Any]) -> Dict[str, Any]:
        project = self._project(args["path"])
        edits = args["edits"]
        if not isinstance(edits, list) or not all(isinstance(e, dict) for e in edits):
            raise ToolError("edits must be a list of objects")
        out = Path(args.get("out") or project.path)
        summary = write_model(project.path, out, edits, self.cli)
        return summary

    def check_compatibility(self, args: Dict[str, Any]) -> Dict[str, Any]:
        project = self._project(args["path"])
        return analysis.check(project.path, engine=args.get("engine"), cli=self.cli)

    def run_simulation(self, args: Dict[str, Any]) -> Dict[str, Any]:
        project = self._project(args["path"])
        result = run_model(project.path, engine=args.get("engine"), out=args.get("out"), cli=self.cli)
        out: Dict[str, Any] = {"success": result.success, "message": result.message, "engine": result.engine,
                               "seconds": result.seconds, "periods": result.periods,
                               "warnings": result.warnings}
        if result.success and result.results_path:
            r = result.results
            out["results"] = str(result.results_path)
            out["report"] = str(result.report_path) if result.report_path else None
            out["reportStep"] = r.report_step
            out["tables"] = [{"table": t.kind, "elements": len(t.ids),
                              "variables": [f"{v.name} ({v.unit})" if v.unit else v.name for v in t.variables]}
                             for t in r]
        return out

    def query_results(self, args: Dict[str, Any]) -> Dict[str, Any]:
        r = Results.load(str(args["results"]), cli=self.cli)
        table = r[str(args["table"])]
        variable = str(args["variable"])
        unit = table.unit(variable)
        out: Dict[str, Any] = {"table": table.kind, "variable": table.variables[table.variable_index(variable)].name,
                               "unit": unit}
        elements = args.get("elements")
        if args.get("period") is not None:
            p = int(args["period"])
            ids = list(elements) if elements else table.ids
            snap = table.snapshot(variable, p)
            out.update(period=p, time=r.times[p],
                       values={e: _num(snap[table.element_index(e)]) for e in ids[:_MAX_VALUES]})
        elif elements:
            out["times"] = r.times
            out["series"] = {e: [_num(x) for x in table.series(variable, e)] for e in list(elements)[:50]}
        else:
            stats = {}
            for i, e in enumerate(table.ids[:_MAX_VALUES]):
                s = [float(x) for x in table.series(variable, i) if x == x]
                stats[e] = {"min": min(s), "mean": sum(s) / len(s), "max": max(s)} if s else None
            out["statistics"] = stats
            out["periods"] = r.period_count
        return out

    def list_engines(self, args: Dict[str, Any]) -> Dict[str, Any]:
        return {"engines": [{k: e.get(k) for k in ("id", "name", "dialect", "available", "reason")}
                            for e in analysis.engines(self.cli)]}


class _RpcError(Exception):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code


def _error(msg_id: Any, code: int, message: str) -> Dict[str, Any]:
    return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}


def _num(x: Any) -> Optional[float]:
    v = float(x)
    return v if v == v else None


def main(argv: Optional[List[str]] = None) -> int:
    """Entry point of ``python -m mirab.mcp``: ``[--cli PATH]``."""
    import argparse

    parser = argparse.ArgumentParser(prog="python -m mirab.mcp", description=__doc__.splitlines()[0])
    parser.add_argument("--cli", help="path of mirab-cli (default: MIRAB_CLI, PATH, install locations)")
    opts = parser.parse_args(argv)
    if hasattr(sys.stdin, "reconfigure"):
        sys.stdin.reconfigure(encoding="utf-8")  # type: ignore[union-attr]
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")  # type: ignore[union-attr]
    Server(Cli(opts.cli) if opts.cli else None).serve()
    return 0


if __name__ == "__main__":
    sys.exit(main())
