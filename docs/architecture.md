# Architecture

```
            +---------------------------- Mirab (Qt 6 desktop) ----------------------------+
            | MapView  PropertyPanel  ProjectBrowser  InpEditor  ResultsPanel  Dialogs       |
            | ProjectDocument (model + undo + results)        PluginInterface (C++ add-ons) |
            +-----------------------------------------+---------------------------------------+
                                                      | QProcess, JSON lines
                                                      v
            +------------------------------- mirab-cli ------------------------------------+
            | run, check, results, resilience, export, import, elevations                   |
            +----------+--------------------+--------------------+---------------------------+
                       |                    |                    |
             mirab_engine         mirab_resilience     mirab_interop
             (adapters, registry,   (Todini, service,      (GeoJSON, GeoPackage,
              compatibility)          topology, criticality,  WNTR JSON)
                       |              fire flow)
                       v
             engine libraries loaded at run time
             EPANET 2.2 | EPANET 2.3 | MSX on 2.2 | MSX on 2.3 | SWMM 5.2 | SWMM5+ (C API)

            mirab_core: InpDocument, Schema, Model, Features, Units, Results, Json, ProjectSettings,
                        Crs, Raster, Inflate, Elevation
```

## Libraries

- **core** (`src/core`, no Qt): the section-preserving input document, the column schemas of EPANET and SWMM sections, the network model used for drawing and editing, feature detection, units, the results model and the `.anr` results format, readers for EPANET and SWMM binary output, coordinate reference systems (`Crs.h`: EPSG table, WKT and PROJ parsing, Transverse Mercator, Lambert conformal conic, Albers, Mercator and polar stereographic projections), rasters (`Raster.h`: GeoTIFF and ESRI ASCII grid readers with lazy, cached block access and bilinear sampling; `Inflate.h`: the Deflate decoder they use), and node elevations from a DEM (`Elevation.h`).
- **engine** (`src/engine`, no Qt): the `Engine` interface, the registry with each engine's capability manifest, discovery of engine libraries, compatibility checking and input preparation, and one adapter per engine family.
- **resilience** (`src/resilience`, no Qt): the five analyses.
- **interop** (`src/interop`, no Qt): GIS layers, GeoJSON and GeoPackage writers (on the bundled SQLite), WNTR conversion, and importers for shapefiles, DXF and CSV (`Import.h`) that build network elements with field mapping and snapping.
- **cli** (`src/cli`): the `mirab-cli` program. It is both the user's command-line tool and the solver process used by the desktop application.
- **app** (`src/app`): the Qt 6 Widgets application: main window and docks, map (`MapView`, with the `Basemap` tile layer for XYZ, WMS and WMTS services, `MapServices` for their capabilities, and `RasterLayers` for DEM and image layers), reports, data editors, import dialog, and the light theme and icon set drawn in code (`Theme`, `Icons`).

## Design decisions

- **The input file is the model.** Mirab edits `.inp` files through a document that keeps every line, comment and section order. Only rows the user edits change. Anything Mirab does not model in detail can still be edited in the text view. Settings that engines do not read live in a sidecar JSON file.
- **Schemas instead of hand-written forms.** Each section is described once (columns, units, references, row variants such as inlet types, repeated column groups such as land use coverages). Sections that define named objects (SWMM pollutants, land uses, aquifers, ...) say so in the schema. The property editor, the object editor, renaming, deletion, GIS export and validation all use the same description.
- **Engines are plug-in libraries.** They are loaded with `LoadLibraryEx`/`dlopen` and bound by symbol name. EPANET uses the thread-safe `EN_` API; EPANET-MSX uses its toolkit plus the legacy API of its companion EPANET library; SWMM uses its stepping API and the binary output file; SWMM5+ uses the swmm5plus-capi interface with a progress callback.
- **Process isolation.** The desktop process never loads an engine; see [engines.md](engines.md).
- **Live results.** Engines hand each reporting period to a paced frame callback (`RunCallbacks::frame`) and wait between time steps while paused (`RunCallbacks::checkpoint`). `mirab-cli` streams the frames as compact binary events (`core/LiveResults.h`, [file-formats.md](file-formats.md#live-results)); `SolverProcess` grows an in-memory result set from them that `ProjectDocument` shows until the saved results replace it.
- **No Python on the user side.** WNTR interoperability is by file format; resilience metrics are native.
- **QGIS is a neighbour, not a dependency.** Mirab writes GeoPackage files and starts QGIS as a separate program.
