# File formats and the solver protocol

## Project files

| File | Content |
|---|---|
| `model.inp` | EPANET or SWMM input file, the master copy of the model |
| `model.msx` | EPANET-MSX input file (optional) |
| `model.inp.mirab.json` | Mirab settings that engines do not read |
| `model.anr` | results of the last run |
| `model.rpt` | the engine's text report |
| `model.<scenario>.anr`, `.rpt` | results and report of a scenario (the name with characters other than letters, digits, `-` and `_` replaced by `-`) |
| `model.gpkg` | GeoPackage written by "Open in QGIS" |
| `model-calibration/` | log (`evaluations.csv`), best values (`best.json`) and calibrated model of the last calibration |

### Settings sidecar

```json
{
  "format": "mirab-project-settings/1",
  "engine": "epanet-2.3-owa",
  "useMsx": false,
  "msxFile": "model.msx",
  "crs": "EPSG:32614",
  "backdropImage": "aerial.png",
  "pressureThreshold": 0,
  "basemap": "custom:County imagery",
  "mapService": {"id": "custom:County imagery", "name": "County imagery", "type": "wms",
                 "url": "https://maps.example.org/wms", "layer": "ortho", "format": "image/png",
                 "crs": "EPSG:3857", "version": "1.3.0"},
  "rasters": [
    {"path": "terrain/dem.tif", "style": "hillshade-color", "opacity": 0.6, "visible": true,
     "exaggeration": 2, "verticalUnit": 0.3048006096012192},
    {"path": "photo.tif", "style": "image", "opacity": 0.85, "visible": true}
  ]
}
```

Scenarios, when the project has any, are stored under `scenarios` (see [Scenarios](#scenarios)), the calibration set-up under `calibration` and, in a SWMM project linked to a water distribution model, the link under `waterLink` (see [Water link](#water-link)). Members a version of Mirab does not know are ignored when the file is read.

Paths are relative to the `.inp` file. `basemap` is a built-in provider (`osm`, `esri-imagery`, ...) or `custom:<name>`; `mapService` describes a custom one (`type` `xyz`, `wms` or `wmts`; WMTS also has `matrixSet`, `style` and `format`). Raster `style` is `hillshade`, `color`, `hillshade-color` or `image`; `verticalUnit` is metres per elevation unit and is left out when the file's own units are used. An optional `calibration` member holds the observed-data files, how their columns link to results, and the calibration parameters and objective; its layout is described in the [user guide](user-guide.md#from-the-command-line).

### Scenarios

Each scenario is stored as its differences from its parent: the base model (the `.inp` file) or the scenario named by `parent`. The scenarios are an addition to format version 1; files without them read as before, and older versions of Mirab ignore them.

```json
"scenarios": [
  {"name": "Demand growth", "description": "30 percent more demand",
   "changes": [{"op": "set", "section": "OPTIONS", "key": "DEMAND MULTIPLIER", "column": 2, "from": "1.0", "to": "1.3"}]},
  {"name": "Growth with upsizing", "parent": "Demand growth", "engine": "epanet-2.3-owa",
   "changes": [
     {"op": "set", "section": "PIPES", "key": "P5", "column": 4, "from": "100", "to": "150"},
     {"op": "add", "section": "DEMANDS", "key": "J8", "row": 1, "to": ["J8", "2.5", "PAT2"]},
     {"op": "remove", "section": "PIPES", "key": "P12", "from": ["P12", "J8", "J9", "300", "100", "120", "0", "Open"]},
     {"op": "row", "section": "PATTERNS", "key": "PAT1", "row": 3, "from": ["PAT1", "1.4", "1.3"], "to": ["PAT1", "1.5"]},
     {"op": "text", "section": "TITLE", "from": ["Demo network"], "to": ["Demo network, alternative B"]}]}
]
```

| Member | Meaning |
|---|---|
| `name` | unique; not `Base`, no commas, quotes or semicolons |
| `description` | optional text |
| `parent` | optional: the scenario this one is based on; its changes apply first |
| `engine`, `useMsx`, `msxFile` | optional: engine, EPANET-MSX use and MSX file (relative to the `.inp`) when they differ from the parent |
| `changes` | the differences, applied in order |

A change finds its row by `section` (upper case, without brackets), `key` and `row`, never by line number:

- `key` is the first value of the row (element, pattern, curve or object ID). In option sections (`[OPTIONS]`, `[TIMES]`, `[REPORT]`, ...) it is the option name in upper case with its words (`DEMAND MULTIPLIER`); EPANET `[ENERGY]` rows for a pump use `PUMP <id> <parameter>` and `[REACTIONS]` rows the first two words; EPANET `[CONTROLS]` and `[TAGS]` use the first two words; SWMM `[INFLOWS]`, `[DWF]`, `[TREATMENT]`, `[BUILDUP]`, `[WASHOFF]`, `[LID_USAGE]` and `[TAGS]` use the first two values; rows of a control rule use the rule's name, and the `NC`, `X1` and `GR` rows of a SWMM transect its name.
- `row` is the ordinal among the rows of the section with that key, from 0 (left out when 0).
- `op` is `set` (one value: `column` counts from 0 over the row's values, `from` and `to` are strings), `row` (all values, when their number changes), `add` (a new row, inserted after the last row with the key), `remove`, or `text` (all lines of a free-text section such as `[TITLE]`).

`from` holds the parent's value when the change was recorded. When it no longer matches, the change is reported as a conflict and the scenario's value is used; a change whose row no longer exists is reported and skipped, and so is an added row of an element ([DEMANDS], [COORDINATES], ...) that no longer exists.

### Water link

A SWMM project whose dry-weather inflow comes from an EPANET model of the same area
([user guide](user-guide.md#water-and-wastewater-together)) keeps the link in its sidecar:

```json
"waterLink": {
  "waterModel": "../epanet/mirab-demo-longbeach.inp",
  "maxDistance": 300,
  "nodeTag": "Sanitary",
  "serviceAreas": "subcatchments",
  "returnFactor": 0.8,
  "categoryFactors": {"Irrigation": 0, "Commercial": 0.9},
  "categories": ["", "Commercial"],
  "patternPrefix": "WW_",
  "weekend": true, "weekStart": "Monday",
  "lagHours": 0.5,
  "results": "../epanet/mirab-demo-longbeach.anr",
  "overrides": {"J5": "MH-B1S", "J9": ""},
  "links": {"J2": "MH-B1", "J3": "MH-C1", "J5": "MH-B1S"},
  "showReference": true
}
```

| Member | Meaning |
|---|---|
| `waterModel` | the EPANET input file, relative to the SWMM file |
| `maxDistance` | largest distance from a water junction to its SWMM node in SWMM map units; 0 for none |
| `nodeTag`, `serviceAreas`, `storage` | optional limits on the receiving SWMM nodes: a `[TAGS]` tag, `subcatchments` or a polygon file (relative path), and `false` when storage units do not receive flow |
| `returnFactor`, `categoryFactors` | share of the water that returns, and per demand category (`""` is the demand without a category) |
| `categories` | optional: only these demand categories return flow |
| `patternPrefix`, `weekend`, `weekStart`, `lagHours` | names of the patterns written, weekly patterns, the day of the week at EPANET time zero, travel time in hours |
| `results` | optional: EPANET results used instead of the base demands |
| `overrides` | manual choices: water junction to SWMM node, `""` to leave it out |
| `links` | the SWMM node of each water junction at the last apply, drawn on the map |
| `showReference` | `false` hides the water network on the SWMM map |

Optional members are left out when not used. The dry-weather inflow itself is part of the input
file (`[DWF]` rows with constituent `FLOW` and patterns named with `patternPrefix`).

## Results format (`.anr`)

Little-endian binary:

1. 16 bytes: the ASCII text `MIRAB-RESULTS` padded with zero bytes,
2. `uint32` format version (currently 1),
3. `uint32` length of the header in bytes,
4. the header, UTF-8 JSON (results of a scenario have `"scenario"` in `metadata`):
   ```json
   {"engineId": "...", "engineName": "...", "title": "...", "flowUnits": "LPS", "unitSystem": "SI",
    "reportStart": 0, "reportStep": 3600, "times": [0, 3600, ...],
    "metadata": {...}, "messages": [...],
    "tables": [{"kind": "node", "ids": ["J1", ...], "variables": [{"name": "Pressure", "unit": "m"}, ...]}, ...]}
   ```
5. for each table in header order, `float32` values ordered by period, then variable, then element:
   `value(p, v, e) = data[(p * nVariables + v) * nElements + e]`.

## Solver protocol

The desktop application runs `mirab-cli <command> ... --machine`. Every line on standard output is one JSON object with an `event` member:

| Event | Members |
|---|---|
| `progress` | `fraction` (0 to 1), `stage` |
| `log` | `level` (`info`, `warning`, `error`), `message` |
| `layout` | runs with `--live`: `layout` (a number), `engineId`, `engineName`, `title`, `flowUnits`, `unitSystem`, `reportStart`, `reportStep`, `tables` (as in the `.anr` header, without values) |
| `frame` | runs with `--live`: `layout`, `period`, `time` (seconds from the simulation start), `data` |
| `paused` | `time`: simulation seconds reached when the engine stopped between two time steps |
| `resumed` | `time` |
| `evaluation` | calibration only: `index`, `objective`, `best` (best objective so far), `values`, `success` |
| `done` | `success`, `message`, and for runs `results`, `report`, `periods`, `seconds`, `engine`, `cancelled`; for analyses and calibration `payload` |

Standard input accepts one command per line:

- `cancel` stops the run at the next time step (also while paused),
- `pause` holds the engine between two time steps until `resume`; every engine supports it.

The exit code is 0 on success, 1 on failure, 2 when a check finds the project cannot run, and 3 when cancelled.

### Live results

`mirab-cli run ... --machine --live` streams the results of each reporting period while the engine computes them, so the desktop application can color the map during the run.

- A `layout` event announces the tables, element IDs and variables once. They are the same as in the final results, so any variable can be shown during the run. A new `layout` (with a new number) is sent only when the tables change: EPANET-MSX first streams the hydraulic pass, then the species pass with the species added.
- Each `frame` carries one period: `data` is base64 of little-endian `float32` values, table by table in layout order, each table variable by variable, element by element, the same block that period has in an `.anr` file. Values an engine cannot read during the run are NaN (EPA SWMM 5.2: link volume, snow depth, groundwater, soil moisture and pollutants, and most system values; link capacity is approximated from the depth). `data` is the last member, so a reader can parse the short header and decode the block in place.
- Frames are paced: at most one every `--frame-interval` seconds (default 0.25; 0 sends every period). Skipped periods appear only in the final results. The last period computed is always sent, whether the run completes or is cancelled.
- A frame may repeat the latest `time` with corrected values; it replaces that period. EPA SWMM 5.2 does this at the end: its live values are the state at the routing step that reaches each reporting time, and the last frame is read from its binary output file, so the live view ends exactly on the saved results.
- The `.anr` file written at the end stays the complete, authoritative result. Cancelled runs write no results file.

Base64 of raw `float32` keeps the values exact and needs no compression library on either side; it adds a third to the size. A network of 19,000 elements (EPANET, 11 node and link variables) sends 0.6 MB per frame, which the desktop application reads, decodes and stores in 2 to 5 ms.
