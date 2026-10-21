# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tests/cli_protocol.py
# Purpose: Checks the solver protocol of mirab-cli: live result frames, pause, resume and cancel.
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
"""Usage: cli_protocol.py <mirab-cli> <examples-dir> <scratch-dir>

Runs mirab-cli in machine mode the way the desktop application does and
checks the JSON-lines events it writes.
"""

import base64
import json
import os
import queue
import re
import shutil
import struct
import subprocess
import sys
import threading
import time

CLI, EXAMPLES, SCRATCH = sys.argv[1], sys.argv[2], sys.argv[3]
FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
        print("FAIL:", message)


def events_of(output):
    return [json.loads(line) for line in output.splitlines() if line.strip()]


def table_sizes(layout):
    return [len(t["ids"]) * len(t["variables"]) for t in layout["tables"]]


def read_anr(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:13] == b"MIRAB-RESULTS"
    header_len = struct.unpack_from("<I", data, 20)[0]
    header = json.loads(data[24:24 + header_len].decode("utf-8"))
    offset = 24 + header_len
    periods = len(header["times"])
    blocks = []
    for t in header["tables"]:
        n = len(t["ids"]) * len(t["variables"])
        blocks.append(data[offset:offset + 4 * n * periods])
        offset += 4 * n * periods
    return header, blocks


def period_bytes(header, blocks, period):
    out = b""
    for t, block in zip(header["tables"], blocks):
        n = len(t["ids"]) * len(t["variables"])
        out += block[4 * n * period:4 * n * (period + 1)]
    return out


def copy_with_times(name, duration, hydraulic):
    src = os.path.join(EXAMPLES, "epanet", "mirab-demo.inp")
    with open(src, encoding="utf-8") as f:
        text = f.read()
    text = re.sub(r"(?m)^\s*Duration\s+\S+", " Duration           " + duration, text)
    text = re.sub(r"(?m)^\s*Hydraulic Timestep\s+\S+", " Hydraulic Timestep " + hydraulic, text)
    path = os.path.join(SCRATCH, name)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def complete_run():
    """A full run streams every period and ends on the saved results."""
    inp = copy_with_times("protocol-demo.inp", "24:00", "1:00")
    anr = os.path.join(SCRATCH, "protocol-demo.anr")
    run = subprocess.run([CLI, "run", inp, "--engine", "epanet-2.3-owa", "--out", anr, "--machine", "--live",
                          "--frame-interval", "0"], stdin=subprocess.DEVNULL, capture_output=True, text=True,
                         timeout=120)
    check(run.returncode == 0, "complete run exits with 0")
    events = events_of(run.stdout)
    kinds = [e["event"] for e in events]
    check(kinds.count("layout") == 1, "one layout event")
    check(kinds.index("layout") < kinds.index("frame"), "the layout comes before the first frame")
    layout = next(e for e in events if e["event"] == "layout")
    frames = [e for e in events if e["event"] == "frame"]
    done = events[-1]
    check(done["event"] == "done" and done["success"], "the last event is a successful done")
    check(len(frames) == done["periods"], "one frame per reporting period")
    check([f["period"] for f in frames] == list(range(len(frames))), "frames follow the periods in order")
    size = 4 * sum(table_sizes(layout))
    for f in frames:
        check(f["layout"] == layout["layout"], "frames name the announced layout")
        check(len(base64.b64decode(f["data"])) == size, "frame %d has the layout's size" % f["period"])
    header, blocks = read_anr(anr)
    check([t["ids"] for t in header["tables"]] == [t["ids"] for t in layout["tables"]],
          "the layout lists the elements of the saved results")
    check([[v["name"] for v in t["variables"]] for t in header["tables"]] ==
          [[v["name"] for v in t["variables"]] for t in layout["tables"]],
          "the layout lists the variables of the saved results")
    last = frames[-1]
    check(abs(last["time"] - header["times"][-1]) < 1e-6, "the last frame is the last reporting time")
    check(base64.b64decode(last["data"]) == period_bytes(header, blocks, len(header["times"]) - 1),
          "the last frame equals the saved results")

    # Without --live the protocol is unchanged.
    plain = subprocess.run([CLI, "run", inp, "--engine", "epanet-2.3-owa", "--out", anr, "--machine"],
                           stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
    kinds = {e["event"] for e in events_of(plain.stdout)}
    check(kinds <= {"progress", "log", "done"}, "without --live only progress, log and done are written")


class Solver:
    def __init__(self, args):
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.events = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            if line.strip():
                self.events.put(json.loads(line))
        self.events.put(None)

    def send(self, command):
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()

    def wait_for(self, kind, timeout=30.0):
        """Returns the first event of this kind and the events before it."""
        seen = []
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                e = self.events.get(timeout=max(0.01, end - time.monotonic()))
            except queue.Empty:
                break
            if e is None:
                break
            if e["event"] == kind:
                return e, seen
            seen.append(e)
        return None, seen

    def quiet_for(self, seconds):
        """Events that arrive within @p seconds."""
        got = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                e = self.events.get(timeout=max(0.01, end - time.monotonic()))
            except queue.Empty:
                break
            if e is None:
                break
            got.append(e)
        return got

    def finish(self):
        try:
            return self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return None


def pause_resume_cancel():
    """Pause holds the engine between steps; resume and cancel release it."""
    inp = copy_with_times("protocol-long.inp", "20000:00", "0:01")
    out = os.path.join(SCRATCH, "protocol-long.anr")
    args = [CLI, "run", inp, "--engine", "epanet-2.3-owa", "--out", out, "--machine", "--live",
            "--frame-interval", "0.05"]

    s = Solver(args)
    first, _ = s.wait_for("frame")
    check(first is not None, "a long run sends frames")
    s.send("pause")
    paused, _ = s.wait_for("paused", 10)
    check(paused is not None and paused["time"] >= 0, "pause is acknowledged with the simulation time")
    during = s.quiet_for(1.0)
    check(not [e for e in during if e["event"] in ("frame", "progress")], "nothing is computed while paused")
    s.send("resume")
    resumed, _ = s.wait_for("resumed", 10)
    check(resumed is not None and resumed["time"] == paused["time"], "resume continues from the paused time")
    later, _ = s.wait_for("frame", 10)
    check(later is not None and later["time"] >= paused["time"], "frames continue after resume")
    s.send("cancel")
    done, before = s.wait_for("done", 30)
    check(done is not None and done.get("cancelled") and not done["success"], "cancel ends the run")
    frames = [e for e in before if e["event"] == "frame"]
    check(not frames or frames[-1]["time"] >= later["time"], "the state reached is sent before done")
    check(s.finish() == 3, "a cancelled run exits with 3")

    # Cancel while paused.
    s = Solver(args)
    s.wait_for("frame")
    s.send("pause")
    paused, _ = s.wait_for("paused", 10)
    check(paused is not None, "pause is acknowledged")
    s.send("cancel")
    done, before = s.wait_for("done", 10)
    check(done is not None and done.get("cancelled"), "cancel releases a paused run")
    check("resumed" not in [e["event"] for e in before], "a cancelled pause does not resume")
    check(s.finish() == 3, "a run cancelled while paused exits with 3")
    if os.path.exists(out):
        check(False, "a cancelled run writes no results file")


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH, exist_ok=True)
    complete_run()
    pause_resume_cancel()
    print("%d failure(s)" % len(FAILURES))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
