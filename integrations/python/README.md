# mirab for Python

Scripting for [Mirab](https://github.com/ehsanMa86/Mirab) water distribution (EPANET) and
stormwater (SWMM) models.

The package drives `mirab-cli`, the command-line program installed with Mirab, so a script sees
the same engines, checks and file handling as the desktop application. Result files (`.anr`) are
read directly in Python. The package itself is pure Python 3.9+ with no required dependencies:
NumPy is used when it is installed, pandas adds `to_dataframe()`.

The desktop application does not need Python; this package is only for scripts.

## Installation

```sh
pip install integrations/python            # from a Mirab source checkout
pip install "integrations/python[pandas]"  # with NumPy and pandas
```

`mirab-cli` is found through the `MIRAB_CLI` environment variable (the program or its folder),
then `PATH`, then the standard install locations (`C:\Program Files\Mirab\bin`,
`/Applications/Mirab.app/Contents/MacOS`, `/usr/bin`, `/usr/local/bin`, `/opt/mirab/bin`,
`~/.local/bin`).

## Quick start

```python
import mirab

project = mirab.Project.open("network.inp")
print(project.nodes["J1"].elevation)        # columns by name, in any spelling
project.nodes["J1"].elevation = 12.5        # recorded edit
project.set_option("DEMAND MODEL", "PDA")
project.save()                              # only the edited values change in the file

result = project.run(progress=lambda f, stage: print(f"{f:5.0%} {stage}"))
pressure = result.results.node.values("Pressure")       # (periods, nodes)
df = result.results.node.to_dataframe("Pressure")       # pandas, time in hours
```

Live results, pause and cancel:

```python
def watch(event, sim):
    if event.type == "frame" and event.value("node", "Pressure", "J5") < 15:
        sim.cancel()

project.run(callback=watch)
```

Many variants in parallel:

```python
cases = [mirab.Case(f"x{m}", [mirab.option("DEMAND MULTIPLIER", m)]) for m in (0.8, 1.0, 1.2)]
low = lambda r: float(r.node.values("Pressure").min())
for case in mirab.run_batch("network.inp", cases, reduce=low):
    print(case.name, case.value)
```

The examples folder holds a Monte Carlo demand study, a SWMM design storm sweep and a pandas and
matplotlib example. `docs/python.md` in the Mirab repository is the full guide; `docs/cli.md`
describes the JSON that `mirab-cli` prints.

## Model Context Protocol server

`python -m mirab.mcp` (or the `mirab-mcp` command) serves the model tools of this package over
standard input and output with the Model Context Protocol, for any client that
speaks it. See `docs/python.md`.

## Tests

```sh
pip install "integrations/python[test,pandas]"
MIRAB_CLI=build/bin/mirab-cli pytest integrations/python/tests
```

The tests skip themselves when `mirab-cli` cannot be found. A Mirab build registers them as the
`python-package` ctest when Python 3 and pytest are available.

## License

GPL-3.0-or-later, like Mirab.
