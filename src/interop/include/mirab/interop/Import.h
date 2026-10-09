/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/include/mirab/interop/Import.h
 * Purpose: Importers that turn shapefiles, DXF drawings and CSV tables into network elements.
 *
 * Copyright (C) 2026 Ehsan Madadi
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "mirab/core/Model.h"
#include "mirab/core/Schema.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mirab::interop
{

enum class GeometryType
{
    Point,
    Line,
    Polygon
};

struct ImportedFeature
{
    /// Point: one point (none for a pure attribute row); Line: polyline vertices;
    /// Polygon: outer ring without the repeated closing point.
    std::vector<core::Point> points;
    std::vector<std::string> values; ///< one per field, as UTF-8 text
};

struct ImportedLayer
{
    std::string name; ///< file stem, or "<DXF layer> (points|lines|polygons)"
    GeometryType geometry = GeometryType::Point;
    /// Attribute names: DBF fields, DXF "Layer", "Handle", "Elevation" (+ "Block"
    /// for points), or CSV headers.
    std::vector<std::string> fields;
    std::vector<ImportedFeature> features;
    std::string crsText; ///< contents of the .prj file (WKT) when present

    /// Index of a field (case-insensitive), or -1.
    int fieldIndex(const std::string& field) const;
};

/// Reads an ESRI shapefile (.shp with its .dbf, .prj and .cpg siblings).
/// Point, MultiPoint, PolyLine and Polygon shapes, including their Z and M
/// variants, are supported. Multi-part lines give one feature per part and
/// multi-part polygons one feature per outer ring; holes and null shapes are
/// dropped. PointZ layers get an extra "Z" field. Text attributes are decoded
/// from UTF-8 or Latin-1/Windows-1252 (per .cpg, else detected). Throws
/// std::runtime_error on unreadable or malformed files.
ImportedLayer readShapefile(const std::filesystem::path& shp);

/// Reads the ENTITIES section of an ASCII DXF file (R12 through 2018): LINE,
/// LWPOLYLINE, POLYLINE/VERTEX/SEQEND, POINT, CIRCLE (centre point) and INSERT
/// (insertion point, field "Block"). Closed polylines become polygons. One
/// layer is returned per (DXF layer, geometry type). Throws std::runtime_error
/// for binary DXF and unreadable files.
std::vector<ImportedLayer> readDxf(const std::filesystem::path& dxf);

struct CsvOptions
{
    char delimiter = 0;  ///< 0: detect from the header line (',', ';' or tab)
    std::string xField;  ///< empty: detect x, easting, lon, longitude, ...
    std::string yField;  ///< empty: detect y, northing, lat, latitude, ...
};

/// Reads a delimited text table (RFC 4180 quoting, UTF-8 BOM, CRLF). Gives a
/// point layer when coordinate columns are found, otherwise features without
/// points (an attribute table, e.g. links with from/to node columns). Throws
/// std::runtime_error when the file cannot be read or a requested coordinate
/// column does not exist.
ImportedLayer readCsv(const std::filesystem::path& csv, const CsvOptions& options = {});

struct ImportMapping
{
    core::Category target = core::Category::Node; ///< Node, Link or Area
    std::string kind;     ///< "Junction", "Pipe", "Conduit", "Subcatchment", ...
    std::string idField;  ///< empty: generated IDs (idPrefix + counter)
    std::string idPrefix; ///< e.g. "J", "P"
    /// Schema column name (e.g. "Elevation", "Diameter", "Geom1") -> source field.
    std::map<std::string, std::string> properties;
    /// Links: node IDs from attributes; when empty, endpoints are snapped or created.
    std::string fromField, toField;
    /// Links: endpoints within this distance (model units) reuse the nearest
    /// existing node; otherwise a new node is created.
    double snapTolerance = 0.0;
    std::string endpointKind = "Junction"; ///< kind of nodes created at link endpoints
    std::string endpointPrefix = "J";
    bool computeLength = true; ///< set Length from the geometry when not mapped
    bool computeArea = true;   ///< subcatchments: set Area (ac or ha) from the outline when not mapped
    bool keepVertices = true;  ///< interior polyline points -> [VERTICES]
    /// Subcatchments: outlet node ID field; when empty, the node nearest to the
    /// centroid is used.
    std::string outletField;
    /// Optional transform applied to every point (e.g. a reprojection); returns
    /// false to reject the point, which skips its feature.
    std::function<bool(double& x, double& y)> transform;
};

struct ImportReport
{
    int nodesAdded = 0; ///< includes nodes created at link endpoints
    int linksAdded = 0;
    int areasAdded = 0;
    int skipped = 0; ///< features not imported
    std::vector<std::string> notes;
};

/// Adds the features of @p layer to @p model as elements of mapping.kind. Rows
/// are written exactly as Model::addNode/addLink/addArea would write them,
/// mapped properties are then applied to the element's rows, and the model is
/// rebuilt once. Features with duplicate or invalid IDs, missing geometry or
/// unknown end nodes are skipped and listed in the report notes. Throws
/// std::invalid_argument when the kind does not exist in the model's dialect.
ImportReport importLayer(core::Model& model, const ImportedLayer& layer,
                         const ImportMapping& mapping);

/// Suggests a mapping from field names (DIAMETER/DIA/DIAM -> Diameter,
/// ELEV/ELEVATION/Z -> Elevation, ROUGH/C/HAZEN -> Roughness, ID/NAME/FACILITYID
/// -> id, FROM_NODE/TO_NODE -> end nodes, OUTLET -> outlet, ...).
ImportMapping suggestMapping(const core::Model& model, const ImportedLayer& layer,
                             core::Category target, const std::string& kind);

/// Column names that a mapping can set for an element kind, in schema order,
/// from the element's main row first and then its secondary rows (e.g.
/// [XSECTIONS] for conduits, [SUBAREAS] and [INFILTRATION] for subcatchments).
/// IDs, node/link/area/gage references and coordinates are excluded.
std::vector<std::string> mappableProperties(core::Dialect dialect, core::Category target,
                                            const std::string& kind);

} // namespace mirab::interop
