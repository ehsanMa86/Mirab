# Command line and JSON output

`mirab-cli` ships next to the desktop application (`bin/` on Windows and Linux,
`Mirab.app/Contents/MacOS` on macOS). It runs every engine and analysis Mirab has, edits models
and reads results. `mirab-cli --help` lists the commands and their options.

For scripts, every command that prints information has a `--json` form. This page describes those
documents; the [Python package](python.md) is built on them.

## Conventions

- With `--json` the command prints one JSON document on standard output. Messages for people
  (notes, "Wrote ...") go to standard error.
- Exit codes: 0 success, 1 failure (the message is on standard error, after `error: `), 2 when
  `check` finds the project cannot run, 3 when a run was cancelled.
- Numbers are in the model's units. Times are seconds from the start of the simulation.
  Values a result file does not hold (NaN) are `null`.
- Objects keep their members in the order shown. New members may be added in later versions;
  existing members keep their name and meaning.
- `--compact` prints JSON on one line where a document can be large (`model get`, `results`).
- `--machine` turns `run` and `resilience` into the line-by-line solver protocol described in
  [file-formats.md](file-formats.md#solver-protocol).

| Command | `--json` document |
|---|---|
| `version` | `name`, `version`, `copyright`, `license`, `homepage` |
| `engines` | array of engines (below) |
| `engines install`, `uninstall`, `verify`, `where` | the engine, folder or library concerned |
| `info <model>` | counts, kinds, features and validation messages |
| `check <model>` | whether an engine can run the model |
| `model get <model>` | the whole model (always JSON) |
| `model set <model>` | what an edit changed |
| `run <model>` | the outcome of the run |
| `results <file>` | header, or values of a table |
| `resilience ...` | the analysis results ([resilience.md](resilience.md)) |
| `elevations <model>` | the planned changes of each node |
| `calibrate metrics <model>` | goodness of fit of each observed series |
| `scenarios list <model>` | the scenarios of the project ([below](#scenarios)) |
| `scenarios export <model>` | the file written and the conflicts |
| `batch <model>` | the outcome of each run |
| `export ...`, `import wntr ...` | the file written and notes |
| `link dwf <sewer>` | the dry-weather inflows from a water model ([below](#water-and-wastewater-link-dwf)) |

## Engines and checks

```sh
mirab-cli engines --json
```

```json
[{"id": "epanet-2.3-owa", "name": "OWA EPANET 2.3", "version": "2.3.5", "dialect": "EPANET",
  "multiSpecies": false, "baseEngine": "", "provider": "...", "license": "MIT", "url": "...",
  "description": "...", "optional": false, "available": true, "library": "/.../libepanet2.so",
  "limitations": {"Pressure-driven demand": "supported", "...": "ignored"}}]
```

An engine that is not installed has `"available": false` and a `reason` instead of `library`.
`engines where --json` prints `{"path"}` (the per-user engine folder); `engines verify ID --json`
prints `{"engine", "name", "version", "library"}`.

```sh
mirab-cli info model.inp --json
mirab-cli check model.inp --engine swmm-5.2-epa --json
```

```json
{"path": "model.inp", "dialect": "EPANET", "title": "...", "units": {"flow": "LPS", "system": "SI"},
 "duration": 86400, "reportStep": 3600,
 "counts": {"nodes": 11, "links": 14, "subcatchments": 0, "gages": 0},
 "kinds": {"Junction": 9, "Pipe": 13, "Pump": 1, "Reservoir": 1, "Tank": 1},
 "features": [{"feature": "Pressure-driven demand", "detail": "DEMAND MODEL PDA", "section": "OPTIONS", "row": -1}],
 "issues": [{"severity": "warning", "message": "...", "section": "...", "row": 3}]}

{"engine": "swmm-5.2-epa", "engineName": "EPA SWMM 5.2", "runnable": true,
 "issues": [{"level": "ignored", "message": "...", "section": "...", "row": 0}]}
```

`level` is `ignored` (the engine skips the data) or `unsupported` (it cannot run the model).

## Reading a model: `model get`

```sh
mirab-cli model get model.inp [--compact] [--out model.json]
```

Prints every element with its values named by Mirab's schema of the input format (the names the
property panel shows), the options, the named objects and the remaining sections:

```json
{
  "format": "mirab-model/1",
  "dialect": "EPANET",
  "title": "Mirab demonstration network",
  "units": {"flow": "LPS", "system": "SI", "pressure": "METERS"},
  "times": {"duration": 86400, "hydraulicStep": 3600, "reportStep": 3600, "reportStart": 0},
  "options": {
    "OPTIONS": {"UNITS": "LPS", "HEADLOSS": "H-W", "DEMAND MULTIPLIER": "1.0", "...": "..."},
    "TIMES": {"DURATION": "24:00", "HYDRAULIC TIMESTEP": "1:00", "...": "..."},
    "REACTIONS": {"ORDER BULK": "1", "GLOBAL BULK": "-0.5"}
  },
  "nodes": [
    {"id": "J1", "kind": "Junction", "section": "JUNCTIONS",
     "properties": {"Elevation": 20, "Demand": 0, "Pattern": "PAT1"},
     "x": 1000, "y": 3000,
     "data": {"QUALITY": [{"InitQual": 1}]}}
  ],
  "links": [
    {"id": "P1", "kind": "Pipe", "section": "PIPES",
     "properties": {"Node1": "J1", "Node2": "J2", "Length": 300, "Diameter": 200, "Roughness": 120,
                    "MinorLoss": 0, "Status": "Open"},
     "from": "J1", "to": "J2", "vertices": [], "data": {}}
  ],
  "subcatchments": [],
  "gages": [],
  "objects": {},
  "sections": {"PATTERNS": [["PAT1", "0.6", "0.5", "..."]], "CURVES": [["C1", "60", "40"]]},
  "path": "model.inp"
}
```

- `properties` holds the element's own row: every column of the schema, `null` when the file
  leaves an optional value out. The ID column is not repeated.
- `data` holds the rows of other sections keyed by the element, by section: SWMM `XSECTIONS`,
  `SUBAREAS`, `INFILTRATION`, `INFLOWS`, ...; EPANET `DEMANDS`, `EMITTERS`, `QUALITY`, `SOURCES`,
  ... Each row is an object of named values.
- Values of numeric and free-text columns are numbers when they read as numbers; IDs and choices
  are strings.
- Nodes and rain gages have `x` and `y` (`null` without coordinates), links `from`, `to` and
  `vertices`, subcatchments `outlet`, `gage` and `polygon`.
- `options` holds the key/value sections (EPANET `OPTIONS`, `TIMES`, `REPORT`, `REACTIONS`,
  `ENERGY`; SWMM `OPTIONS`, `REPORT`, `EVAPORATION`, `TEMPERATURE`, `ADJUSTMENTS`). Keys are upper
  case and may have several words; rows that start alike get the words that tell them apart
  (`"PUMP PU1 PRICE"`). Every key can be passed back to `model set --option`.
- `objects` (SWMM) lists pollutants, land uses, aquifers, snow packs, unit hydrographs, transects,
  streets, inlets, LID controls and control rules by kind:
  `{"Pollutant": [{"id": "TSS", "section": "POLLUTANTS", "rows": [{"section": "POLLUTANTS",
  "tokens": [...], "values": {...}}]}]}`. A land use's rows include its `BUILDUP` and `WASHOFF`
  rows.
- `sections` holds every other section as rows of tokens: patterns, curves, time series, controls,
  labels, ...

## Changing a model: `model set`

```sh
mirab-cli model set model.inp --id J1 --column Elevation --value 21.5
mirab-cli model set model.inp --option "DEMAND MODEL" --value PDA
mirab-cli model set model.inp --section EMITTERS --id J3 --column Coefficient --value 0.4
mirab-cli model set model.inp --edits edits.json --out variant.inp --json
```

Edits go through Mirab's model, as in the desktop application: only the edited values change and
every other line, comment and the order of the file stay as written. Without `--out` the file is
changed in place (and only when something changed). `--edits -` reads the edits from standard
input. The edits file is a JSON array, or an object with an `edits` array, of:

| Edit | Meaning |
|---|---|
| `{"id", "column", "value"}` | Sets one value of an element or named object. `column` is a schema name, matched ignoring case, blanks and punctuation (`"InitLevel"`, `"init_level"`), or a token index (`0` renames). Without `section` the element's own row is tried first, then the other sections that hold a row for it (`"Geom1"` reaches `[XSECTIONS]`). |
| `+ "section"` | The row in that section. When the element has no row there yet, one is added with default values (`EMITTERS`, `INFLOWS`, `LOSSES`, ...). |
| `+ "row"` | The n-th row of the element in `section` (default 0), for sections with several rows per element such as `DEMANDS`. |
| `+ "category"` | `node`, `link`, `subcatchment`, `gage` or an object kind, when an ID names several things. |
| `{"option", "value"}` | Sets a key/value option; `section` defaults to `OPTIONS` (`TIMES`, `REPORT`, ...). A `null` value removes the option. |
| `{"section", "id", "rows"}` | Replaces the rows of a pattern, curve, time series or other keyed data. Rows are arrays of values without the key, e.g. `[["0:00", 0], ["0:15", 6]]`. Without `id` every row of the section is replaced. |

Values may be strings, numbers, booleans (`YES`/`NO`) or `null` (clears an optional value). The
edits are applied in order; an edit that cannot be applied stops the command with
`edit N: <reason>` and nothing is written.

```json
{"edits": [
  {"id": "P1", "column": "Diameter", "value": 250},
  {"id": "C1", "column": "Geom1", "value": 0.75},
  {"option": "Duration", "section": "TIMES", "value": "12:00"},
  {"section": "PATTERNS", "id": "PAT1", "rows": [[1.0, 1.2, 1.4], [1.1, 0.9]]}
]}
```

`--json` prints `{"path", "applied", "changed", "written", "issues"}`, where `issues` are the
structural checks of the changed model (as in `info`).

## Running: `run --json`

```sh
mirab-cli run model.inp [--engine ID] [--out results.anr] [--report model.rpt] --json
```

```json
{"success": true, "cancelled": false, "code": 0, "message": "simulation completed",
 "engine": "epanet-2.3-owa", "seconds": 0.0025, "results": "model.anr", "report": "model.rpt",
 "periods": 25}
```

Progress and engine messages go to standard error. With `--machine` instead, the same object is the
`done` event of the solver protocol, preceded by progress, log and (with `--live`) frame events.

## Results: `results`

```sh
mirab-cli results model.anr --json
mirab-cli results model.anr --table node --variable Pressure --element J1,J5 [--period 3] --json
mirab-cli results model.anr --table node [--variable Pressure] --format npy --out pressure.npy
mirab-cli results model.anr --table link --variable Flow [--out flow.csv]
mirab-cli results model.out --format anr --out model.anr
```

Without `--table`, `--json` prints the header of the file (the same members as the `.anr` header in
[file-formats.md](file-formats.md#results-format-anr), plus `periods`):

```json
{"engineId": "epanet-2.3-owa", "engineName": "OWA EPANET 2.3 (2.3.5)", "title": "...",
 "flowUnits": "LPS", "unitSystem": "SI", "reportStart": 0, "reportStep": 3600, "periods": 25,
 "times": [0, 3600, ...], "metadata": {"input": "model.inp", "mirabVersion": "0.2.0"},
 "messages": [], "tables": [{"kind": "node", "ids": ["J1", ...],
                             "variables": [{"name": "Pressure", "unit": "m"}, ...]}]}
```

With `--table`, the values of the chosen variables (every variable without `--variable`), elements
(all without `--element`) and periods (all without `--period`):

```json
{"table": "node", "periods": [3], "times": [10800], "elements": ["J1", "J5"],
 "variables": [{"name": "Pressure", "unit": "m", "values": [[48.22, 51.12]]}]}
```

`values` is indexed by period, then element.

- `--format npy` writes a NumPy array of little-endian `float32`: shape (periods, elements) with
  one `--variable`, otherwise (periods, variables, elements).
- `--format csv` (the default with `--table`) writes `time_s` and one column per element for one
  variable, to standard output or `--out`.
- `--format anr` converts any results file Mirab reads (EPANET and SWMM binary output too) to the
  Mirab format.

## Analyses, elevations, export

`resilience ... --json` prints the payload of each analysis (see [resilience.md](resilience.md));
with `--machine` it is the `payload` of the `done` event, after progress events.

```sh
mirab-cli elevations model.inp --dem dem.tif --json [--out model.inp]
```

```json
{"dem": {"path": "dem.tif", "format": "GeoTIFF", "width": 400, "height": 300, "crs": "EPSG:2229"},
 "counts": {"update": 9, "unchanged": 2},
 "nodes": [{"id": "J1", "kind": "Junction", "field": "Elevation", "status": "update",
            "ground": 25.0, "old": 20, "new": 25}],
 "notes": [], "written": "model.inp", "changed": 9}
```

`status` is `update`, `unchanged`, `outside the DEM`, `no data`, `no coordinates` or `skipped`;
`counts` has the statuses that occur. `written` and `changed` appear with `--out`.

`export geojson|gpkg|wntr ... --json` prints `{"format", "path", "notes"}`; `import wntr ...
--json` prints `{"path", "nodes", "links", "notes"}`.

## Scenarios

Scenarios are alternatives of a model stored as changes in the project sidecar (member
`scenarios`, see [file-formats.md](file-formats.md#scenarios) and the
[user guide](user-guide.md#scenarios)). `--scenario NAME` applies one before a command:
`info`, `check`, `model get`, `model set`, `run`, `calibrate`, `export` and the resilience analyses
accept it (`Base` is the base model). A scenario's results go to `model.<scenario>.anr` and
`.rpt`, the name with characters other than letters, digits, `-` and `_` replaced by `-`.

```sh
mirab-cli scenarios list model.inp --json
mirab-cli scenarios export model.inp --scenario "Pipe upsizing" --out upsized.inp
mirab-cli run model.inp --scenario "Pipe upsizing" --json
mirab-cli batch model.inp --all --jobs 4 --json          # the base model and every scenario
mirab-cli batch model.inp --scenarios "Base,Pipe upsizing" --engine epanet-2.2-epa
mirab-cli model set model.inp --scenario "Pipe upsizing" --id P7 --column Diameter --value 200
```

`scenarios list --json` prints one object per scenario; `conflicts` counts the changes that no
longer apply cleanly to the base model and `results` is empty until the scenario has been run:

```json
[{"name": "Growth with upsizing", "parent": "Demand growth", "description": "...",
  "changes": 4, "conflicts": 0, "engine": "", "results": "/data/model.Growth-with-upsizing.anr"}]
```

`batch --json` runs the scenarios in separate `mirab-cli run` processes, `--jobs` at a time
(default: up to 4), and prints:

```json
{"model": "/data/model.inp", "failed": 0,
 "runs": [{"scenario": "", "success": true, "seconds": 0.21, "results": "/data/model.anr",
           "report": "/data/model.rpt"},
          {"scenario": "Pipe upsizing", "success": true, "seconds": 0.23,
           "results": "/data/model.Pipe-upsizing.anr", "report": "/data/model.Pipe-upsizing.rpt"}]}
```

A failed run also has `log`, the output of its process. `scenarios export --json` prints `scenario`,
`path` and `conflicts` (each with `scenario`, `section`, `key`, `missing` and `message`).
`run --json` adds `scenario`, and `model get` adds `scenario` to the model document.
`model set --scenario` records the edits as changes of the scenario in the sidecar and leaves the
input file as it is; with `--out` it writes the scenario's edited model to that file instead.

## Calibration

The observed data and the calibration set-up live in the project sidecar
(`model.inp.mirab.json`, member `calibration`) or in a `--config` file; see the
[user guide](user-guide.md#calibration).

```sh
mirab-cli calibrate metrics model.inp --json            # compare the saved results
mirab-cli calibrate model.inp --jobs 4 --budget 100     # search the parameters
mirab-cli calibrate model.inp --scenario "Pipe upsizing" # calibrate a scenario's model
```

`calibrate metrics --json` prints one entry per observed series:

```json
{"series": [{"series": "J5 Pressure (m)", "key": "node:J5:Pressure", "ok": true, "count": 48,
             "unit": "m", "NSE": 0.724, "KGE": 0.894, "PBIAS": 1.52, "RMSE": 1.77, "MAE": 1.07,
             "R2": 0.807, "PeakError": 0.762, "PeakTiming": 0, "VolumeError": null}]}
```

Metrics that do not apply to a series (such as the volume error of a pressure) are `null`.
`calibrate` writes `evaluations.csv`, `best.json` and `<model>-best.inp` to `--out-dir`; with
`--machine` it reports each run as an `evaluation` event and ends with a `done` event whose
`payload` holds the best parameters (see [file-formats.md](file-formats.md#solver-protocol)).

## Water and wastewater: `link dwf`

`link dwf` computes the dry-weather inflow of a SWMM sewer model from the water use of an EPANET
model of the same area (see the [user guide](user-guide.md#water-and-wastewater-together)).
Without `--out` it only previews.

```sh
mirab-cli link dwf sewer.inp --water water.inp --distance 300 --json
mirab-cli link dwf sewer.inp --water water.inp --return 0.8,Irrigation=0,none=0.9 --lag 0.5 --out sewer.inp
mirab-cli link dwf sewer.inp --use-results water.anr --weekend --week-start Saturday --out sewer.inp
```

| Option | Meaning |
|---|---|
| `--water FILE` | the EPANET model; its coordinate system is read from its sidecar |
| `--distance D` | largest distance from a water junction to its SWMM node, in SWMM map units (0: no limit) |
| `--return F[,CAT=F...]` | return factor, and factors of demand categories (`none` is the demand without a category) |
| `--categories CAT,CAT` | only these demand categories return flow |
| `--use-results FILE` | the delivered demand of an EPANET run (`.anr` or EPANET binary output) instead of the base demands and patterns |
| `--tag TAG` | only SWMM nodes with this tag receive flow |
| `--areas subcatchments\|FILE` | service areas: the SWMM subcatchments or the polygons of a shapefile or DXF file |
| `--override W=S,...` | water junction `W` drains to SWMM node `S`; `W=` leaves it out |
| `--lag HOURS` | travel time added to the patterns |
| `--weekend`, `--week-start DAY` | weekly sources: `WEEKEND` and `DAILY` patterns too; the day of the week at EPANET time zero (default Monday) |
| `--prefix P` | prefix of the pattern names (default `WW_`) |
| `--no-storage` | storage units do not receive flow |
| `--crs`, `--water-crs` | coordinate systems, when the sidecars have none |
| `--out FILE` | writes the model with the `[DWF]` flow rows and patterns, and the link in its sidecar |

Options not given come from the link in the SWMM project's sidecar (`waterLink`, see
[file-formats.md](file-formats.md#settings-sidecar)). With `--json`:

```json
{"model": "sewer.inp", "water": "/data/water.inp", "flowUnits": "CFS", "source": "demands", "days": 1,
 "totals": {"waterDemand": 1.547, "includedDemand": 1.547, "returned": 1.237, "unassignedFlow": 0,
            "demandNodes": 8, "unassigned": 0, "tooFar": 0, "swmmNodes": 8},
 "inflows": [{"node": "MH-A2", "average": 0.1384, "hourly": "WW_PAT1", "sources": ["J4"]}],
 "patterns": [{"name": "WW_PAT1", "type": "HOURLY", "factors": [0.6695, 0.5665, "..."]}],
 "assignments": [{"water": "J4", "swmm": "MH-A2", "status": "matched", "distance": 246,
                  "demand": 0.173, "returned": 0.1384}],
 "notes": [], "written": "sewer.inp", "changed": 12}
```

Flows are averages in the SWMM flow units. `inflows` may also have `weekend` and `daily` pattern
names. `status` is `matched`, `manual`, `too-far` (with `nearest` and its `distance`),
`no-coordinates`, `outside-areas`, `no-candidate` or `excluded`; `swmm` is `null` for a junction that
is not connected, and `area` names its service area. `written` and `changed` (rows written or
removed) appear with `--out`.
