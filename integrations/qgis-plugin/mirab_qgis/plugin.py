# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    integrations/qgis-plugin/mirab_qgis/plugin.py
# Purpose: Loads Mirab GeoPackages and input files into QGIS with styling.
#
# Copyright (C) 2026 Ehsan Madadi
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""QGIS side of the Mirab bridge.

The desktop application writes a GeoPackage and starts QGIS. This plugin adds
one menu with two commands:

* Load Mirab GeoPackage: adds every layer of the file with styling,
* Open EPANET or SWMM input file: converts it with mirab-cli and loads it.
"""

import os
import shutil
import subprocess
import tempfile

from qgis.PyQt.QtGui import QIcon
from qgis.PyQt.QtWidgets import QAction, QFileDialog, QMessageBox
from qgis.core import QgsFillSymbol, QgsLineSymbol, QgsMarkerSymbol, QgsProject, QgsVectorLayer

STYLES = {
    "junctions": ("marker", "circle", "#5a6473", 2.2),
    "reservoirs": ("marker", "trapezoid", "#1f6fbf", 4.0),
    "tanks": ("marker", "square", "#1f6fbf", 3.6),
    "outfalls": ("marker", "triangle", "#2a9d8f", 3.6),
    "storage_units": ("marker", "square", "#2a9d8f", 3.6),
    "dividers": ("marker", "diamond", "#e9c46a", 3.2),
    "rain_gages": ("marker", "star", "#3a86ff", 4.0),
    "pipes": ("line", None, "#28303c", 0.6),
    "conduits": ("line", None, "#28303c", 0.7),
    "pumps": ("line", None, "#d1495b", 0.9),
    "valves": ("line", None, "#edae49", 0.9),
    "orifices": ("line", None, "#edae49", 0.8),
    "weirs": ("line", None, "#edae49", 0.8),
    "outlets": ("line", None, "#edae49", 0.8),
    "subcatchments": ("fill", None, "#96cd96", 0.4),
}


def _find_cli():
    for name in ("mirab-cli", "mirab-cli.exe"):
        found = shutil.which(name)
        if found:
            return found
    candidates = [
        os.path.join(os.environ.get("PROGRAMFILES", r"C:\Program Files"), "Mirab", "bin", "mirab-cli.exe"),
        "/Applications/Mirab.app/Contents/MacOS/mirab-cli",
        "/usr/bin/mirab-cli",
        "/usr/local/bin/mirab-cli",
    ]
    return next((c for c in candidates if os.path.exists(c)), None)


class MirabBridge:
    def __init__(self, iface):
        self.iface = iface
        self.actions = []

    def initGui(self):  # noqa: N802 (QGIS naming)
        icon = QIcon(os.path.join(os.path.dirname(__file__), "icon.png"))
        for text, slot in (
            ("Load Mirab GeoPackage...", self.load_geopackage),
            ("Open EPANET or SWMM input file...", self.open_inp),
        ):
            action = QAction(icon, text, self.iface.mainWindow())
            action.triggered.connect(slot)
            self.iface.addPluginToMenu("&Mirab", action)
            self.actions.append(action)

    def unload(self):
        for action in self.actions:
            self.iface.removePluginMenu("&Mirab", action)
        self.actions = []

    def load_geopackage(self, path=None):
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self.iface.mainWindow(), "Mirab GeoPackage", "", "GeoPackage (*.gpkg)")
        if not path:
            return
        probe = QgsVectorLayer(path, "probe", "ogr")
        names = [s.split("!!::!!")[1] for s in probe.dataProvider().subLayers()]
        group_name = os.path.splitext(os.path.basename(path))[0]
        group = QgsProject.instance().layerTreeRoot().insertGroup(0, group_name)
        # Draw areas first, then links, then nodes on top.
        def rank(name):
            kind = STYLES.get(name, ("marker",))[0]
            return {"fill": 0, "line": 1}.get(kind, 2)
        for name in sorted(names, key=rank):
            layer = QgsVectorLayer(f"{path}|layername={name}", name.replace("_", " "), "ogr")
            if not layer.isValid():
                continue
            self._style(layer, name)
            QgsProject.instance().addMapLayer(layer, False)
            group.insertLayer(0, layer)
        self.iface.mapCanvas().refreshAllLayers()

    def open_inp(self):
        inp, _ = QFileDialog.getOpenFileName(
            self.iface.mainWindow(), "EPANET or SWMM input file", "", "Input files (*.inp)")
        if not inp:
            return
        cli = _find_cli()
        if not cli:
            QMessageBox.warning(self.iface.mainWindow(), "Mirab",
                                "mirab-cli was not found. Install Mirab or add it to the PATH.")
            return
        out = os.path.join(tempfile.gettempdir(), os.path.splitext(os.path.basename(inp))[0] + ".gpkg")
        result = subprocess.run([cli, "export", "gpkg", inp, "--out", out], capture_output=True, text=True)
        if result.returncode != 0:
            QMessageBox.critical(self.iface.mainWindow(), "Mirab", result.stderr or result.stdout)
            return
        self.load_geopackage(out)

    @staticmethod
    def _style(layer, name):
        kind, shape, color, size = STYLES.get(name, ("marker", "circle", "#5a6473", 2.0))
        if kind == "marker":
            symbol = QgsMarkerSymbol.createSimple(
                {"name": shape, "color": color, "size": str(size), "outline_color": "#28303c"})
        elif kind == "line":
            symbol = QgsLineSymbol.createSimple({"color": color, "width": str(size)})
        else:
            symbol = QgsFillSymbol.createSimple(
                {"color": color, "outline_color": "#3c8c46", "outline_width": str(size)})
            symbol.setOpacity(0.6)
        layer.renderer().setSymbol(symbol)
        layer.triggerRepaint()
