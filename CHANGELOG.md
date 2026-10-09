# Changelog

## 0.1.0 (2026-10-08)

First release.

- Desktop application (Qt 6) for Windows, macOS and Linux with map editing, property editor, input-file editor, results map coloring, time slider and charts.
- Engines: EPA EPANET 2.2, OWA EPANET 2.3, EPANET-MSX 2.0 on either EPANET engine, EPA SWMM 5.2, and SWMM5+ 1.0 through the separate swmm5plus-capi library.
- SWMM5+ installation from the application, `anahita-cli engines install|uninstall|verify|where`, and `anahita --install-engine`, with SHA-256 checks of every file.
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
- `anahita-cli` for scripted runs, analyses and conversions.
- C++ plugin interface with an example plugin.
