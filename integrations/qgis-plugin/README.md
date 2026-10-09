# Mirab bridge for QGIS

This QGIS plugin is the QGIS side of Mirab's "Open in QGIS" command. It runs inside QGIS's own Python; Mirab users do not install Python.

- **Load Mirab GeoPackage** adds every layer written by Mirab (junctions, pipes, conduits, subcatchments, ...) in one group, with styling.
- **Open EPANET or SWMM input file** converts an `.inp` file with `mirab-cli export gpkg` and loads it.

Install: zip the `mirab_qgis` folder and use *Plugins > Manage and Install Plugins > Install from ZIP*, or copy the folder into the `python/plugins` directory of your QGIS profile.

Copyright (C) 2026 Ehsan Madadi. GPL-3.0-or-later.
