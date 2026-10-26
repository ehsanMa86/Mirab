# Changelog

## 0.2.5 (2026-10-26)

- SWMM5+ engine (swmm5plus-capi 0.2.3, fetched by *Install SWMM5+*): the total inflow of a junction between two conduits and of an outfall no longer counts its inflow or runoff twice, so the system outflow agrees with the runoff; a missing input or settings file is reported instead of ending the solver.
- Fix: SWMM results whose reporting starts after the simulation start (REPORT_START_DATE and REPORT_START_TIME later than START) were placed too early, by the report start less one report step. The live view, the exports and calibration now agree with SWMM's report.
- Fix: `mirab-cli resilience criticality` and `fireflow` solved the base model when `--scenario` was given; they now solve the scenario's model.
- Fix: the WNTR import kept closed pipes, pumps and valves open, wrote `*` as the pattern of a demand category without one (EPANET error 205) and dropped the demand multiplier, emitter exponent, trials, accuracy, pattern start, quality time step, report start and start clock time. The export records whether a valve is open or closed instead of always active.
- Fix: water service availability counted emitter and leakage flow as delivered demand with OWA EPANET 2.3; it now uses the consumer demand delivered (the full demand less the deficit).
- Fix: results of EPA EPANET 2.2 were labelled with the PRESSURE option where 2.2 overrides it; 2.2 reports psi with US flow units and never psi with SI units.
- Fix: the head loss of pipes from an EPANET run was the loss across the pipe but labelled per 1000 ft or per km; it is now per 1000 length units, as in EPANET's report and binary output. Pumps and valves keep their head gain or loss.
- Fix: the link status read from an EPANET binary output held EPANET's status codes, so a closed pipe counted as open in network tracing; it now holds 0 for closed and 1 for open, like the results of a run.
- Fix: the control rule checker rejected SWMM dates with "-" between the parts or a month name (`01-01-2020`, `JAN/01/2020`), which SWMM accepts, so the rule editor could not keep such rules.
- Fix: a section header that appears twice in an input file hid the elements after the second one; the lines of both now belong to the section, as in EPANET and SWMM.
- Fix: `mirab-cli export --results R --period N` with N out of range wrote zeros for every result; it now reports the valid range.
- Fix: the fire-flow time of day ignored the model's START CLOCKTIME and replaced its PATTERN START; the snapshot is now solved at that clock time of the model.
- Engine manifests whose engine id is a path are rejected, so the install record cannot be written outside the engine folder.

## 0.2.4 (2026-10-23)

- Several results in one chart: the *Time series* report plots every result checked in its list, for each element; results in a second unit use a y axis on the right. The chart under the map takes a second result too, for example the depth and the total inflow of a junction.
- System graphs for SWMM: *System* in the *Time series* report plots the totals of the whole model (rainfall, runoff, flooding, outflow, storage, evaporation and the others), with their units.
- Fix: the evaporation loss of subcatchments is labelled in in/day or mm/day, as SWMM reports it, instead of a rainfall rate.
- Interface in Hebrew (right to left), Brazilian Portuguese, French, Italian, German and Traditional Chinese.
- Fix: a SWMM model no longer lists the EPANET-only analyses (resilience tools, fire flow, pipe criticality) or the MSX file choice; *Analysis* offers network topology only.
- Until a model is created or opened, the engine list reads "Open or create a model" and the MSX option and drawing tools are hidden, instead of offering EPANET engines before a model type is chosen.
- Fix: SWMM5+ stopped on models whose upstream end junctions receive only runoff, such as the urban and water quality examples, and `mirab-cli` reported success. Mirab now adds a zero inflow at those junctions for SWMM5+, refuses terminal junctions before the run, and exits with code 3 when SWMM5+ stops the program.
- Fix: SWMM5+ stopped when an inflow time series ended before the simulation, as in the water quality example. Mirab continues such series with zero for SWMM5+, the inflow EPA SWMM uses after their last point, and SWMM5+'s messages are no longer lost when it stops the program on Linux and macOS.

The release tagged v0.2.3 was published from the changes from Hebrew to the inflow time series above, with its packages and version still named 0.2.2; 0.2.4 is the first release that carries them under its own version.

## 0.2.2 (2026-10-23)

- Installers for Intel Macs (`Mirab-<version>-macos-x64.dmg`) next to the Apple silicon ones.
- Every release carries SHA-256 checksums and a signed build provenance attestation for each file (`gh attestation verify <file> --repo ehsanMa86/Mirab`).
- The desktop packages include the full LGPL-3.0 text of Qt, and the notices name the Qt version shipped (6.8.3) and its source archive.
- The source code is public: issue forms for bug reports and feature requests, a security policy and contribution guidelines.

## 0.2.1 (2026-10-22)

- Fix: SWMM5+ runs started from the application never finished on Windows. The solver process waited for standard input while SWMM5+ inspected it; commands are now read only when they arrive.
- The Long Beach sewer example runs with SWMM5+: its upstream end MH-A1 has a small infiltration flow.
- Documentation: SWMM5+ applies the average dry-weather flow without its time patterns.

## 0.2.0 (2026-10-21)

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
- Interface in Persian, Arabic (right to left), Spanish and Simplified Chinese, chosen with the globe button in the status bar or in *Tools > Settings*, with every language also named in English and an optional immediate restart; Persian and Arabic use the bundled Vazirmatn font. Model files stay locale-independent.
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
