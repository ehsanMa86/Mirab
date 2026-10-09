# File formats and the solver protocol

## Project files

| File | Content |
|---|---|
| `model.inp` | EPANET or SWMM input file, the master copy of the model |
| `model.msx` | EPANET-MSX input file (optional) |
| `model.inp.anahita.json` | Anahita settings that engines do not read |
| `model.anr` | results of the last run |
| `model.rpt` | the engine's text report |
| `model.gpkg` | GeoPackage written by "Open in QGIS" |

### Settings sidecar

```json
{
  "format": "anahita-project-settings/1",
  "engine": "epanet-2.3-owa",
  "useMsx": false,
  "msxFile": "model.msx",
  "crs": "EPSG:32614",
  "backdropImage": "aerial.png",
  "pressureThreshold": 0
}
```

Paths are relative to the `.inp` file.

## Results format (`.anr`)

Little-endian binary:

1. 16 bytes: the ASCII text `ANAHITA-RESULTS` followed by a zero byte,
2. `uint32` format version (currently 1),
3. `uint32` length of the header in bytes,
4. the header, UTF-8 JSON:
   ```json
   {"engineId": "...", "engineName": "...", "title": "...", "flowUnits": "LPS", "unitSystem": "SI",
    "reportStart": 0, "reportStep": 3600, "times": [0, 3600, ...],
    "metadata": {...}, "messages": [...],
    "tables": [{"kind": "node", "ids": ["J1", ...], "variables": [{"name": "Pressure", "unit": "m"}, ...]}, ...]}
   ```
5. for each table in header order, `float32` values ordered by period, then variable, then element:
   `value(p, v, e) = data[(p * nVariables + v) * nElements + e]`.

## Solver protocol

The desktop application runs `anahita-cli <command> ... --machine`. Every line on standard output is one JSON object with an `event` member:

| Event | Members |
|---|---|
| `progress` | `fraction` (0 to 1), `stage` |
| `log` | `level` (`info`, `warning`, `error`), `message` |
| `done` | `success`, `message`, and for runs `results`, `report`, `periods`, `seconds`, `engine`; for analyses `payload` |

Writing the line `cancel` to standard input asks the solver to stop at the next time step. The exit code is 0 on success, 1 on failure, 2 when a check finds the project cannot run, and 3 when cancelled.
