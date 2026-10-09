# Anahita user guide

![The map with an OpenStreetMap basemap, pressure colors and a time series](images/map-basemap.jpg)

## The window

- **Start page.** It appears when no project is open, with buttons for a new water network, a new stormwater model and opening a file. It also lists recent projects and the examples. *Help > Start page* shows it again.
- **Tool bar.** It holds file commands, undo and redo, and the map tools: select, pan and zoom to a rectangle. Next come one button per element type, delete and find, the *Reports* menu, the engine selector and the *Run* button.
- **Map tab.** A bar above the map chooses the results that color nodes, links and subcatchments. It also holds the time controls (step, play, slider), the basemap and the label, flow-arrow and legend switches. Zoom buttons float at the top right of the map; a scale bar and the legend sit on the map itself.
- **Input file tab.** The `.inp` text exactly as the engines read it. Edit it and press *Apply changes*; the change can be undone as one step.
- **Report, pattern and curve tabs.** They open next to the map and close with the cross on the tab.
- **Project panel** (left). It shows a summary of the model and a search box. Below them is a tree with the simulation options, every element by type, and the patterns, curves and time series. Double-click an element to zoom to it, or a pattern or curve to edit it. Right-click for more.
- **Properties panel** (right). It shows the selected element, or the simulation options when nothing is selected. Click a value to edit it, and type in the filter box to find a property quickly.
- **Messages and Time series** (bottom). The Messages tab shows compatibility notes and the run log. The Time series tab plots the selected element over time.

Panels can be resized, moved, closed and floated. *View > Panels* shows a closed panel again and *View > Reset layout* restores the default arrangement. Anahita always uses a light theme.

## Creating a model

1. Choose *New water network* or *New stormwater model* on the start page (or in *File*), then choose flow units. SI units give metres, millimetres and metres of pressure head; US units give feet, inches and psi.
2. Pick an element on the tool bar and click on the map:
   - node tools place one element per click,
   - link tools need a start node, optional bend points, and an end node,
   - the subcatchment tool takes the outline corners; double-click or press Enter to finish. The nearest node becomes its outlet.

   You can also right-click an empty spot of the map and choose *Add here*.
3. Press `Esc` or right-click to return to the selection tool. Drag nodes to move them. `Delete` removes the selection and any links attached to a removed node.
4. Edit values in *Properties*. Renaming the ID updates every reference to it, including controls and rules.
5. *Add data* attaches extra rows to the selected element, such as demand categories, emitters, FAVAD leakage, water quality sources, losses or cross sections.

Anahita writes standard EPANET and SWMM input files, so models stay usable in any other tool.

## Importing GIS and CAD data

*File > Import* reads ESRI shapefiles (`.shp` with `.dbf` and `.prj`), DXF drawings and CSV tables. You can also drop these files on the window.

![Import dialog](images/import-dialog.png)

1. Choose a layer. A shapefile has one layer. A DXF drawing has one layer per drawing layer and geometry type.
2. Choose what its features become:
   - **Points** become nodes, such as junctions, tanks or outfalls.
   - **Lines** become links, such as pipes or conduits.
   - **Polygons** become subcatchments in SWMM models.
3. Choose the ID field, or let Anahita generate IDs with a prefix.
4. For links, line ends within the *snap tolerance* connect to existing nodes, and new nodes are created for the others. Alternatively, choose the fields that hold the start and end node IDs; a CSV table of pipes needs no coordinates this way.
5. Map attribute fields to properties such as elevation, diameter, roughness or length. Anahita suggests fields with common names, and computes lengths and subcatchment areas from the geometry when no field is chosen.
6. When the file has a `.prj`, its coordinate system is shown:
   - If the project already has a coordinate system, the points are reprojected to it.
   - If not, *Use it as the coordinate system of the project* sets it, which also enables basemaps.

Import several layers in a row, for example junctions, then pipes, then tanks. Every import is one undoable step, and notes about skipped or invalid features appear in *Messages*.

## Basemaps and coordinate systems

The *Basemap* button on the map bar shows a web map under the network. The choices are OpenStreetMap, CARTO Light, Esri streets, satellite imagery, topographic and light-gray maps, and OpenTopoMap. Tiles are cached on disk, so places you have visited load quickly and work offline. The attribution of the map provider is shown at the bottom right.

Basemaps need to know where the coordinates are on Earth. Use *View > Coordinate system* (or the basemap menu) to choose the coordinate reference system of the node coordinates:

- **Searching:** search about 1,950 built-in systems by name or EPSG code. They include WGS 84 and Web Mercator, every UTM zone, all NAD83 State Plane and county systems in metres and feet, ETRS89, GDA, SIRGAS and others.
- **Other definitions:** type an EPSG code, PROJ string or WKT, or load the `.prj` file of a shapefile.
- **Checking:** the dialog shows where the first node would be in latitude and longitude, so a wrong choice is easy to spot.

Coordinates that are plain longitude and latitude are recognised automatically. Datum shifts are not applied, so data on older datums such as NAD27 can appear tens of metres away from the basemap. The coordinate system is also written into GeoPackage and GeoJSON exports.

The example `examples/epanet/anahita-demo-tabriz.inp` is placed in Tabriz in UTM zone 38N and opens with an OpenStreetMap basemap.

## Choosing an engine

The engine box on the tool bar lists the engines for the model type. Engines that are not installed are disabled; *Project > Simulation engines* explains why.

SWMM5+ is an optional engine. Open *Project > Simulation engines* and click *Install SWMM5+*: Anahita downloads it, checks every file and adds it to the engine box. Without internet access, download the files for your computer from the swmm5plus-capi release page and use *Install from file...*. See [engines.md](engines.md#installing-swmm5) for details.

- **MSX:** for EPANET models, tick *MSX* to run EPANET-MSX on top of the selected EPANET engine, and choose the `.msx` file.
- **Messages:** the panel always shows how the project fits the selected engine.
  - **Information:** the project is fully supported.
  - **Warning:** this engine ignores a feature. Anahita removes it from the copy sent to the engine and tells you.
  - **Error:** the engine cannot run the project, and the Run button is disabled.
- **Greyed-out data:** data the selected engine does not model is greyed out in *Properties*. For example, the `[LEAKAGE]` rows of a pipe are greyed out under EPA EPANET 2.2, and PCV is not offered as a valve type.

The choice of engine, the MSX file, the coordinate system, the basemap and the backdrop are saved next to the model in `<model>.inp.anahita.json`.

## Running and viewing results

Press `F5` or *Run*. The engine runs in a separate process; you can keep working and press Stop at any time. When it finishes:

- **Map colors:** the map colors nodes, links and subcatchments by the results chosen on the map bar, with a legend and flow-direction arrows.
- **Time controls:** the time controls step through the reporting periods or animate them.
- **Hover:** hovering over an element shows its results at the current time.
- **Properties:** *Properties* lists the selected element's results under its input data.
- **Time series:** the Time series panel plots the selected element over time. Right-click the chart to copy it, save it as an image or copy its data.
- **Saved results:** results are saved as `<model>.anr` and reload when the model is opened again.

*Simulation > Load results* also opens binary output files (`.out`) written by EPANET or SWMM elsewhere.

## Reports

The *Reports* menu and tool bar button open report tabs. Each report follows new results, and most follow the time slider. Every report can export CSV.

| Report | What it shows |
|---|---|
| Network tables | Properties and results of every node, link or subcatchment at the current time. Sort by any column and filter with expressions such as `Pressure < 20` or `Diameter >= 300`. Selecting a row selects the element on the map. |
| Time series | One result for several elements on one chart. |
| Profile plot | Ground or invert elevation and the hydraulic grade along the shortest path between two nodes, with the minimum-maximum range over the simulation. |
| Frequency plot | The distribution of a result over all elements at one time, with minimum, median, mean and maximum. |
| Variability plot | The minimum, 10th-90th percentile band, median, mean and maximum of a result over all elements (or one type) at every time. |
| System flow | EPANET: water supplied by reservoirs, consumed by demands and stored in tanks. SWMM: runoff, outfall discharge and flooding. |
| Energy report | EPANET pumps: utilization, average efficiency, energy per volume pumped, average and peak power, energy and cost per day. |
| Status report | The engine's own report file. |

*Reports > Fire flow analysis* opens the fire-flow capacity analysis of the resilience tools.

![Profile plot](images/report-profile.png)

## Patterns, curves and time series

Double-click a pattern, curve or time series in the *Project* panel to edit it in a tab. The tab shows a table of values next to a chart that updates while you type. *Apply* writes the changes as one undoable step, and *Revert* discards them.

![Pattern editor](images/pattern-editor.jpg)

## Resilience tools

*Analysis* offers five analyses for EPANET models: the Todini index, water service availability, network topology, pipe criticality and fire-flow capacity. See [resilience.md](resilience.md) for the methods. Selecting a row in the result table highlights the element on the map.

## GIS and QGIS

- *File > Export > GeoPackage* writes one layer per element type (junctions, pipes, conduits, subcatchments, ...) with every input value and, when results are loaded, the values of the current period.
- *File > Open in QGIS* writes `<model>.gpkg` and starts QGIS with it. QGIS is never loaded inside Anahita, so a QGIS installation problem cannot affect it. Set the QGIS program in *Tools > Settings* if it is not found.
- *View > Backdrop image* shows a PNG, JPEG or TIFF behind the network. With a world file (`.pgw`, `.jgw`, `.tfw` or `.wld`) the image is georeferenced; without one it is stretched over the network.

## WNTR

*File > Export > WNTR network* writes the JSON layout read by `wntr.network.read_json`, in SI units. *File > Import > WNTR network* reads a file written by `wntr.network.write_json` and saves it as an EPANET input file in LPS. Controls, rules and quality sources are reported as not transferred.

## Keyboard shortcuts

| Key | Action |
|---|---|
| `F5` | Run simulation |
| `F` | Zoom to fit |
| `+`, `-`, arrow keys | Zoom and pan the map |
| `S` | Selection tool |
| `H` | Pan tool |
| `Z` | Zoom to a rectangle |
| `Esc` | Cancel the current tool or clear the selection |
| `Delete` | Delete the selection |
| `Ctrl+Z`, `Ctrl+Y` | Undo, redo |
| `Ctrl+F` | Find an element by ID and zoom to it |

## Command-line options

`anahita [file.inp]` opens a project. Other options:

| Option | Effect |
|---|---|
| `--import FILE` | Opens the import dialog for a shapefile, DXF drawing or CSV table. |
| `--show-data SECTION:ID` | Opens the editor of a pattern, curve or time series, for example `PATTERNS:PAT1`. |
| `--select ID` | Selects an element. |
| `--install-engine swmm5plus` | Installs SWMM5+ without opening a window (also a manifest URL or file). |
| `--screenshot FILE.png` | Saves a picture of the window and quits; with `--run` (run first), `--report KIND` (open a report: tables, timeseries, profile, frequency, variability, systemflow, energy, status) and `--delay MS`. |

Every analysis is also available without a window through `anahita-cli`; run `anahita-cli` without arguments for its commands.
