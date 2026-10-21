# File formats and the solver protocol

## Project files

| File | Content |
|---|---|
| `model.inp` | EPANET or SWMM input file, the master copy of the model |
| `model.msx` | EPANET-MSX input file (optional) |
| `model.inp.mirab.json` | Mirab settings that engines do not read |
| `model.anr` | results of the last run |
| `model.rpt` | the engine's text report |
| `model.gpkg` | GeoPackage written by "Open in QGIS" |

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

Paths are relative to the `.inp` file. `basemap` is a built-in provider (`osm`, `esri-imagery`, ...) or `custom:<name>`; `mapService` describes a custom one (`type` `xyz`, `wms` or `wmts`; WMTS also has `matrixSet`, `style` and `format`). Raster `style` is `hillshade`, `color`, `hillshade-color` or `image`; `verticalUnit` is metres per elevation unit and is left out when the file's own units are used.

## Results format (`.anr`)

Little-endian binary:

1. 16 bytes: the ASCII text `MIRAB-RESULTS` padded with zero bytes,
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

The desktop application runs `mirab-cli <command> ... --machine`. Every line on standard output is one JSON object with an `event` member:

| Event | Members |
|---|---|
| `progress` | `fraction` (0 to 1), `stage` |
| `log` | `level` (`info`, `warning`, `error`), `message` |
| `layout` | runs with `--live`: `layout` (a number), `engineId`, `engineName`, `title`, `flowUnits`, `unitSystem`, `reportStart`, `reportStep`, `tables` (as in the `.anr` header, without values) |
| `frame` | runs with `--live`: `layout`, `period`, `time` (seconds from the simulation start), `data` |
| `paused` | `time`: simulation seconds reached when the engine stopped between two time steps |
| `resumed` | `time` |
| `done` | `success`, `message`, and for runs `results`, `report`, `periods`, `seconds`, `engine`, `cancelled`; for analyses `payload` |

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
