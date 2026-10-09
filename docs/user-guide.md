# Anahita user guide

## The window

- **Map** (center): the network. Scroll to zoom, drag empty space or hold the middle button to pan, press `F` to fit the network.
- **Input file** (center tab): the `.inp` text exactly as the engines read it. Edit it and press *Apply changes*; the change can be undone as one step.
- **MSX file** (center tab): appears when multi-species quality is on.
- **Project** (left): every element grouped by type, with a filter box.
- **Properties** (right): the selected element. With nothing selected it shows the project options.
- **Results** and **Messages** (bottom): map coloring, the time slider, a chart of the selected element, and compatibility or run messages.

## Creating a model

1. *File > New EPANET project* or *New SWMM project*, then choose flow units. SI units give metres, millimetres and metres of pressure head; US units give feet, inches and psi.
2. Pick a tool on the edit toolbar and click on the map:
   - node tools place one element per click,
   - link tools need a start node, optional bend points, and an end node,
   - the subcatchment tool takes the outline corners; double-click or press Enter to finish. The nearest node becomes its outlet.
3. Press `Esc` or right-click to return to the selection tool. Drag nodes to move them. `Delete` removes the selection and any links attached to a removed node.
4. Edit values in *Properties*. Renaming the ID updates every reference to it, including controls and rules.
5. *Add data* attaches extra rows to the selected element, such as demand categories, emitters, FAVAD leakage, water quality sources, losses or cross sections.

Anahita writes standard EPANET and SWMM input files, so models stay usable in any other tool.

## Choosing an engine

The engine box on the simulation toolbar lists the engines for the model type. Engines that are not installed are disabled; *Tools > Simulation engines* explains why.

SWMM5+ is an optional engine. On Linux and macOS, open *Tools > Simulation engines* and click *Install SWMM5+*: Anahita downloads it, checks every file and adds it to the engine box. Without internet access, download the files for your computer from the swmm5plus-capi release page and use *Install from file...*. See [engines.md](engines.md#installing-swmm5) for details.

- For EPANET models, tick *Multi-species (MSX)* to run EPANET-MSX on top of the selected EPANET engine and choose the `.msx` file.
- The *Messages* panel always shows how the project fits the selected engine:
  - **information**: the project is fully supported,
  - **warning**: a feature is ignored by this engine; Anahita removes it from the copy sent to the engine and tells you,
  - **error**: the engine cannot run the project; the Run button is disabled.
- Data the selected engine does not model is greyed out in *Properties*. For example, the `[LEAKAGE]` rows of a pipe are greyed out under EPA EPANET 2.2, and PCV is not offered as a valve type.

The choice of engine, the MSX file, the coordinate system and the backdrop are saved next to the model in `<model>.inp.anahita.json`.

## Running and viewing results

Press `F5` or the Run button. The engine runs in a separate process; you can keep working and press Stop at any time. When it finishes:

- the map colors nodes, links and subcatchments by the variables chosen in *Results*,
- the slider and the play button step through reporting periods,
- the chart shows the selected element over time; *File > Export > Selected element time series* saves it as CSV,
- the engine's text report opens with *Simulation > Open report*,
- results are saved as `<model>.anr` and reload when the model is opened again.

*Simulation > Load results* also opens binary output files (`.out`) written by EPANET or SWMM elsewhere.

## Resilience tools

*Analysis > Resilience tools* offers five analyses for EPANET models. See [resilience.md](resilience.md) for the methods. Selecting a row in the result table highlights the element on the map.

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
| `S` | Selection tool |
| `Esc` | Cancel the current tool or clear the selection |
| `Delete` | Delete the selection |
| `Ctrl+Z`, `Ctrl+Y` | Undo, redo |
| `Ctrl+F` | Find an element by ID |
