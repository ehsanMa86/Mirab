# Python scripting

The `mirab` package (in [integrations/python](../integrations/python)) scripts Mirab from Python 3.9
or newer. It drives `mirab-cli`, so scripts use the same engines, compatibility checks and file
handling as the desktop application, and reads result files directly. It is pure Python with no
required dependencies; NumPy is used when installed and pandas adds `to_dataframe()`.

The desktop application does not need Python. The package is only for people who script.

## Installation

```sh
pip install integrations/python              # from a Mirab source checkout
pip install "integrations/python[pandas]"    # with NumPy and pandas
pip install "integrations/python[plot]"      # also matplotlib, for the examples
```

The package finds `mirab-cli` through, in this order:

1. the `MIRAB_CLI` environment variable (the program or the folder that holds it),
2. the `PATH`,
3. the standard install locations: `C:\Program Files\Mirab\bin` and
   `%LOCALAPPDATA%\Programs\Mirab` on Windows, `/Applications/Mirab.app/Contents/MacOS` on macOS,
   `/usr/bin`, `/usr/local/bin`, `/opt/mirab/bin` and `~/.local/bin` on Linux.

`mirab.find_cli()` returns the program it uses; `mirab.set_cli(path)` chooses another one.

## Reading a model

```python
import mirab

p = mirab.Project.open("examples/epanet/mirab-demo.inp")
p.dialect, p.title, p.units        # 'EPANET', '...', {'flow': 'LPS', 'system': 'SI', ...}
p.times["duration"]                # seconds
p.option("demand multiplier")      # '1.0' (keys in any case)

j1 = p.nodes["J1"]
j1.kind, j1.x, j1.y                # 'Junction', 1000, 3000
j1.elevation, j1["Elevation"]      # 20, 20
p.nodes["T1"].init_level           # column "InitLevel"
p.links["P13"].to_node             # 'T1'
[n.id for n in p.nodes.of_kind("Tank")]
p.pattern("PAT1"), p.curve("C1")   # multipliers, (x, y) points
p.nodes.to_dataframe()             # one row per node (pandas)
```

`p.nodes`, `p.links`, `p.subcatchments` and `p.gages` map IDs to elements in file order;
`p.objects["Pollutant"]` and the other SWMM object kinds work the same way. `p.element(id)` finds
any of them.

Columns are named as in Mirab's schema of the input format (the names the property panel shows)
and can be written in any case and spelling: `InitLevel`, `init_level` and `initlevel` are the
same column. An element also reaches the columns of other sections that hold one row for it, so a
SWMM conduit has `shape` and `geom1` from `[XSECTIONS]` and a subcatchment `n_perv` from
`[SUBAREAS]`. `element.properties` is its own row, `element.sections` the other rows, and
`element.raw` the element as `mirab-cli model get` prints it ([cli.md](cli.md#reading-a-model-model-get)).

## Changing and saving

```python
p.nodes["J1"].elevation = 21.5               # recorded edit
p.links["P1"].diameter = 250
p.set("J3", "Coefficient", 0.4, section="EMITTERS")   # adds the [EMITTERS] row
p.set("J2", "Demand", 3.2, section="DEMANDS", row=1)  # second demand category
p.set_option("DEMAND MODEL", "PDA")
p.set_option("Duration", "48:00", section="TIMES")
p.set_pattern("PAT1", [0.8] * 6 + [1.2] * 18)
p.set_rows("TIMESERIES", [["0:00", 0], ["0:30", 25], ["1:00", 0]], id="Storm1")  # SWMM

p.edits          # the recorded edits, as model set --edits takes them
p.save()         # in place; p.save("variant.inp") writes a copy and switches to it
p.write_copy("variant.inp")   # writes a copy, keeps the project and its edits
```

Edits are written by `mirab-cli model set`, which changes only the edited values: other lines,
comments and the order of the file stay as they are. Setting an unknown column raises
`AttributeError`; an edit that `mirab-cli` refuses raises `mirab.CliError` with its message.
`mirab.edit()`, `mirab.option()` and `mirab.rows()` build the same edits as dictionaries, for
batch runs or `p.apply([...])`.

## Running

```python
result = p.run()                          # the project's engine
result = p.run(engine="epanet-2.2-epa", out="runs/base.anr")
result.success, result.seconds, result.periods, result.warnings
r = result.results                        # mirab.Results
```

Unsaved edits are run from a scratch copy next to the model, which is removed afterwards; their
results are read into memory unless `out` is given, so the model file stays untouched.
`p.check(engine)` tells whether an engine can run the project before trying.

Progress, live results and run control use the solver protocol of the desktop application:

```python
def watch(event, sim):
    if event.type == "frame":                       # one reporting period, during the run
        if event.value("node", "Pressure", "J5") < 15:
            sim.cancel()
    elif event.type == "log":
        print(event.level, event.message)

result = p.run(callback=watch, progress=lambda f, stage: print(f"{f:4.0%} {stage}"))
result.cancelled
```

The callback receives every event (`progress`, `log`, `layout`, `frame`, `paused`, `resumed`,
`done`) and may call `sim.pause()`, `sim.resume()` and `sim.cancel()`. Frames are paced
(`frame_interval`, default 0.25 s; 0 sends every period). `event.values` holds the period as
`{table: array (variables, elements)}`, and `mirab.Results.from_layout(layout, frames)` turns
streamed frames back into a results object.

For control from another thread, start the run and read its events yourself:

```python
with p.start(live=True) as sim:
    for event in sim.events():
        ...
print(sim.result)
```

`mirab.run("model.inp", ...)` runs a file without opening a project.

## Results

```python
r = mirab.Results.load("model.anr")       # mmap=True maps the values instead (NumPy)
r.times, r.flow_units, r.engine_name
r.node.variable_names                     # ['Demand', 'Head', 'Pressure', ...]
r.node.values("Pressure")                 # (periods, nodes)
r.node.series("Pressure", "J5")           # (periods,)
r.node.snapshot("Pressure", period=12)    # (nodes,)
r.link.value(3, "Flow", "P1")
r.node.to_numpy()                         # (periods, variables, nodes)
r.node.to_dataframe("Pressure")           # pandas, index: time in hours
r.to_dataframe("link")                    # columns: (variable, link)
```

The `.anr` format is read in Python from its description in [file-formats.md](file-formats.md);
with NumPy the values are arrays, without it lists. EPANET and SWMM binary output files are
converted with `mirab-cli results --format anr` first. Tables are `node`, `link`,
`subcatchment` (SWMM) and `system` (SWMM); variables are matched without regard to case.

## Analyses

```python
p.info()                                    # counts, features, validation messages
p.topology()                                # graph measures, critical nodes and links
p.todini(result.results_path)               # Todini resilience index
p.criticality(links=["P1", "P2"], progress=print)
p.fireflow(30, residual=14, nodes=["J5"])
p.elevations("terrain/dem.tif", mode="missing", apply=True)
p.export("gpkg", "network.gpkg")
mirab.engines()                             # engines and whether they are installed
```

They return the JSON documents of the matching `mirab-cli` commands ([cli.md](cli.md),
[resilience.md](resilience.md)). Analyses read the file on disk, so save edits first.

## Many variants in parallel

```python
cases = [mirab.Case(f"x{m}", [mirab.option("DEMAND MULTIPLIER", m)], params={"m": m})
         for m in (0.8, 1.0, 1.2, 1.5)]

def lowest(results):
    return float(results.node.values("Pressure").min())

for case in mirab.run_batch("network.inp", cases, reduce=lowest, workers=4):
    print(case.name, case.success, case.value)
```

Each case is written by `mirab-cli model set` next to the original (so the project settings and
their relative paths apply) and run by its own `mirab-cli` process; `workers` cases run at a
time. `reduce` turns each case's results into the value to keep, so large studies need not keep
every results file (`keep_results=True` and `out_dir` keep them). Cases may also be given as
`{name: edits}` or a list of edit lists. A failed case is reported with its message and does not
stop the others.

## Examples

[integrations/python/examples](../integrations/python/examples):

- `monte_carlo_demand.py`: random junction demands on the EPANET demonstration network; the
  spread of each junction's lowest pressure and the probability of falling below a threshold.
- `swmm_return_periods.py`: alternating-block design storms from an IDF relation for several
  return periods on the SWMM demonstration catchment; peak conduit flow and flooded volume.
- `results_to_pandas.py`: node pressures in a pandas DataFrame, summary statistics, CSV, and a
  plot of the lowest nodes when matplotlib is installed.

## Model Context Protocol server

```sh
python -m mirab.mcp [--cli path/to/mirab-cli]     # or: mirab-mcp
```

The module is a [Model Context Protocol](https://modelcontextprotocol.io) server over standard
input and output (JSON-RPC 2.0, one message per line; protocol revisions 2024-11-05 to
2025-06-18), written with the standard library only. Any client that speaks the protocol
can use it to work with Mirab models. Its tools:

| Tool | Does |
|---|---|
| `model_summary` | format, units, times, counts by kind, options, features, validation messages |
| `list_elements` | elements of a category (and kind) with their values, paged |
| `get_element` | every value of one element or object |
| `set_values` | applies edits in the `model set` format, in place or to `out` |
| `check_compatibility` | whether an engine can run the model, with the reasons |
| `run_simulation` | runs the model; outcome, warnings, tables and variables of the results |
| `query_results` | time series of elements, one period of all elements, or per-element statistics |
| `list_engines` | engines and whether they are installed |

Paths are files on the computer that runs the server, and `set_values` and `run_simulation`
write files there. A client is usually configured with the command and its arguments, for
example `{"command": "python", "args": ["-m", "mirab.mcp"], "env": {"MIRAB_CLI": "..."}}`.

## Tests

```sh
pip install "integrations/python[test,pandas]"
MIRAB_CLI=build/release/bin/mirab-cli pytest integrations/python/tests
```

The tests run the real `mirab-cli` against the example models and skip themselves when it cannot
be found. When CMake finds Python 3 with pytest, `ctest` runs them as the `python-package` test
against the build's own `mirab-cli`; the build itself never needs Python.
