# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/python/tests/test_mcp.py
# Purpose: Tests of the Model Context Protocol server: lifecycle, tool listing and tool calls.
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
"""The JSON-RPC server of ``python -m mirab.mcp``."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from mirab import mcp


def request(server, method, params=None, id=1):
    msg = {"jsonrpc": "2.0", "id": id, "method": method}
    if params is not None:
        msg["params"] = params
    return server.handle(msg)


def call(server, name, **arguments):
    reply = request(server, "tools/call", {"name": name, "arguments": arguments})
    assert "result" in reply, reply
    result = reply["result"]
    assert result["content"][0]["type"] == "text"
    return result


def test_lifecycle_and_errors():
    s = mcp.Server()
    init = request(s, "initialize", {"protocolVersion": "2025-03-26", "capabilities": {},
                                     "clientInfo": {"name": "test", "version": "1"}})
    assert init["result"]["protocolVersion"] == "2025-03-26"
    assert init["result"]["capabilities"]["tools"] == {"listChanged": False}
    assert init["result"]["serverInfo"]["name"] == "mirab"
    newer = request(s, "initialize", {"protocolVersion": "2099-01-01"})
    assert newer["result"]["protocolVersion"] == mcp.PROTOCOL_VERSIONS[0]
    assert s.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}) is None
    assert s.initialized
    assert request(s, "ping") == {"jsonrpc": "2.0", "id": 1, "result": {}}
    assert request(s, "nope/method")["error"]["code"] == mcp.METHOD_NOT_FOUND
    assert request(s, "tools/call", {"name": "nope"})["error"]["code"] == mcp.INVALID_PARAMS
    assert s.handle({"id": 3, "method": "ping"})["error"]["code"] == mcp.INVALID_REQUEST
    tools = request(s, "tools/list")["result"]["tools"]
    names = {t["name"] for t in tools}
    assert {"model_summary", "list_elements", "get_element", "set_values", "check_compatibility",
            "run_simulation", "query_results", "list_engines"} <= names
    for t in tools:
        assert t["inputSchema"]["type"] == "object" and t["description"]
    missing = call(s, "get_element", path="x.inp")
    assert missing["isError"] and "id" in missing["content"][0]["text"]


def test_tools_on_a_model(cli, epanet_demo, epanet_engine, tmp_path):
    s = mcp.Server(cli)
    summary = call(s, "model_summary", path=str(epanet_demo))["structuredContent"]
    assert summary["counts"]["nodes"] == 11 and summary["options"]["TIMES"]["DURATION"] == "24:00"

    page = call(s, "list_elements", path=str(epanet_demo), category="node", kind="Junction", limit=3)
    page = page["structuredContent"]
    assert page["total"] == 9 and [e["id"] for e in page["elements"]] == ["J1", "J2", "J3"]
    assert page["elements"][0]["Elevation"] == 20

    j1 = call(s, "get_element", path=str(epanet_demo), id="J1")["structuredContent"]
    assert j1["properties"]["Pattern"] == "PAT1"
    assert call(s, "get_element", path=str(epanet_demo), id="J99")["isError"]

    changed = call(s, "set_values", path=str(epanet_demo), out=str(tmp_path / "edited.inp"),
                   edits=[{"id": "J1", "column": "Elevation", "value": 25}])["structuredContent"]
    assert changed["changed"] == 1
    edited = call(s, "get_element", path=str(tmp_path / "edited.inp"), id="J1")["structuredContent"]
    assert edited["properties"]["Elevation"] == 25
    bad = call(s, "set_values", path=str(epanet_demo), edits=[{"id": "J1", "column": "Nope", "value": 1}])
    assert bad["isError"] and "Nope" in bad["content"][0]["text"]

    check = call(s, "check_compatibility", path=str(epanet_demo), engine=epanet_engine)["structuredContent"]
    assert check["runnable"]
    run = call(s, "run_simulation", path=str(epanet_demo), engine=epanet_engine)["structuredContent"]
    assert run["success"] and run["periods"] == 25
    assert any(t["table"] == "node" for t in run["tables"])

    stats = call(s, "query_results", results=run["results"], table="node", variable="pressure")
    stats = stats["structuredContent"]
    assert stats["unit"] == "m" and stats["statistics"]["J5"]["min"] <= stats["statistics"]["J5"]["max"]
    series = call(s, "query_results", results=run["results"], table="link", variable="Flow",
                  elements=["P1"])["structuredContent"]
    assert len(series["series"]["P1"]) == 25
    snap = call(s, "query_results", results=run["results"], table="node", variable="Pressure",
                period=3)["structuredContent"]
    assert set(snap["values"]) >= {"J1", "J9"}
    engines = call(s, "list_engines")["structuredContent"]["engines"]
    assert any(e["id"] == epanet_engine and e["available"] for e in engines)


def test_stdio_transport(cli, epanet_demo):
    src = Path(__file__).resolve().parents[1] / "src"
    env = dict(os.environ, PYTHONPATH=str(src) + os.pathsep + os.environ.get("PYTHONPATH", ""))
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "0"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
         "params": {"name": "model_summary", "arguments": {"path": str(epanet_demo)}}},
    ]
    text = "\n".join(json.dumps(m) for m in messages) + "\nnot json\n"
    proc = subprocess.run([sys.executable, "-m", "mirab.mcp", "--cli", cli.program], input=text,
                          capture_output=True, text=True, encoding="utf-8", env=env, timeout=60)
    assert proc.returncode == 0, proc.stderr
    replies = [json.loads(line) for line in proc.stdout.splitlines()]
    assert [r.get("id") for r in replies] == [1, 2, None]
    assert replies[0]["result"]["protocolVersion"] == "2025-06-18"
    assert replies[1]["result"]["structuredContent"]["dialect"] == "EPANET"
    assert replies[2]["error"]["code"] == mcp.PARSE_ERROR


@pytest.mark.parametrize("args", [["--help"]])
def test_command_line_help(args):
    src = Path(__file__).resolve().parents[1] / "src"
    env = dict(os.environ, PYTHONPATH=str(src))
    proc = subprocess.run([sys.executable, "-m", "mirab.mcp"] + args, capture_output=True, text=True, env=env)
    assert proc.returncode == 0 and "--cli" in proc.stdout
