# Changelog

## Unreleased

- SWMM editing without the text view: full schemas for inflows, dry weather flow, RDII, treatment, groundwater, coverages, loadings, inlet and LID usage; pollutants, land uses, aquifers, snow packs, streets and inlets as named objects with renames and deletions carried to every reference; dedicated editors for LID controls, LID usage, transects, unit hydrographs and control rules (SWMM 5.2 and EPANET, with line-numbered checks); a simulation options dialog for SWMM and EPANET.
- Native GeoTIFF and ESRI ASCII grid reader (own Deflate decoder, no GDAL); node elevations from a DEM; hillshade and colour-ramp raster layers; ground elevation under the cursor and in profiles; custom XYZ, WMS and WMTS basemaps.
- Live results while a simulation runs, with pause, resume and partial results after a stop, for every engine (`mirab-cli run --live`).
- Input preparation removes everything the selected engine ignores, as its capability manifest says (pollutants and LID for SWMM5+).
- Observed data, goodness-of-fit metrics (NSE, KGE, PBIAS, RMSE, MAE, R², peak, timing and volume errors), a calibration report and automated calibration with Dynamically Dimensioned Search or Latin hypercube screening (`mirab-cli calibrate`).
- Print layouts (PDF, PNG, printing), report documents (PDF with a table of contents, or HTML) and results animations (animated GIF or PNG images).
- `--json` output for every `mirab-cli` command, `model get`/`model set` for scripted edits, and the `mirab` Python package with a native results reader, batch runs and a Model Context Protocol server.
- Multi-selection (rectangle, polygon, query), upstream/downstream/connected tracing and shortest paths, editing many elements at once, splitting pipes and conduits, measuring distances and areas.
- Scenarios stored as changes to the base model, with inheritance, batch runs (`mirab-cli batch`), a comparison report and a difference map.
- Water and wastewater together: dry-weather flow of a SWMM model from the water use of a linked EPANET model (matching by distance, tags or service areas, return factors per demand category, patterns from demand patterns or results), a water network layer on the sewer map, `mirab-cli link dwf`, and a Long Beach sewer example linked to the water example.
- SWMM5+ through its interface version 2: live results equal to the final results, subcatchment and system results, and pausing between steps.
- The CARTO basemap, which now requires an API key, is replaced by the Esri light gray canvas.
- Fixes: crash when closing the window during a run; SWMM 5.2 runs with relative paths; ponded and LID unit areas shown in m²/ft²; long result units no longer squeeze values in *Properties*; release tags with a hyphen are published as pre-releases.

## 0.1.0 (2026-10-08)

First release.

- Desktop application (Qt 6) for Windows, macOS and Linux with map editing, property editor, input-file editor, results map coloring, time slider and charts.
- Engines: EPA EPANET 2.2, OWA EPANET 2.3, EPANET-MSX 2.0 on either EPANET engine, EPA SWMM 5.2, and SWMM5+ 1.0 through the separate swmm5plus-capi library.
- SWMM5+ installation from the application, `mirab-cli engines install|uninstall|verify|where`, and `mirab --install-engine`, with SHA-256 checks of every file.
- SWMM5+ on Windows, through the native Windows build of swmm5plus-capi.
- Redesigned interface: light theme on every platform, start page, line icons, map display bar, floating zoom buttons, scale bar, flow arrows, hover read-outs, readable panel widths and a Reset layout command.
- Web basemaps (OpenStreetMap, CARTO, Esri streets, imagery, topographic and gray, OpenTopoMap) with a disk tile cache, and a coordinate system library with about 1,950 EPSG systems, WKT/.prj and PROJ strings.
- Import of shapefiles, DXF drawings and CSV tables with field mapping, snapping and reprojection.
- Reports: network tables with filters, multi-element time series, profile, frequency and variability plots, system flow, pump energy audit and the status report.
- Pattern, curve and time-series editors with live charts.
- Large models: element and result lookups use hash indexes, and reading sections is linear; a 20,000-pipe import takes about 0.3 s.
- Capability manifests and compatibility checks; features an engine ignores are stripped automatically with a warning, features it cannot run block the run.
- Isolated solver process with progress reporting and cancellation.
- Resilience tools: Todini index, water service availability, topology, pipe criticality and fire flow.
- GeoPackage and GeoJSON export, WNTR JSON import and export, "Open in QGIS".
- `mirab-cli` for scripted runs, analyses and conversions.
- C++ plugin interface with an example plugin.
