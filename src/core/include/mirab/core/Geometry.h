/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Geometry.h
 * Purpose: Plane geometry of model elements: lengths, areas, regions, distances and measurement.
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

#include "mirab/core/Crs.h"
#include "mirab/core/Model.h"

#include <vector>

namespace mirab::core
{

double distance(const Point& a, const Point& b);
/// Distance from @p p to the segment a-b.
double segmentDistance(const Point& p, const Point& a, const Point& b);
/// Shortest distance between the segments a-b and c-d (0 when they cross).
double segmentsDistance(const Point& a, const Point& b, const Point& c, const Point& d);
bool segmentsIntersect(const Point& a, const Point& b, const Point& c, const Point& d);
double polylineLength(const std::vector<Point>& line);
/// Planar area of a polygon (absolute value; the ring need not be closed).
double polygonArea(const std::vector<Point>& ring);
bool pointInPolygon(const std::vector<Point>& ring, const Point& p);

/// Where a point projects onto a polyline.
struct PolylinePosition
{
    std::size_t segment = 0; ///< segment between line[segment] and line[segment + 1]
    double t = 0.0;          ///< position within that segment, 0..1
    double along = 0.0;      ///< distance from the start of the line
    double length = 0.0;     ///< total length of the line
    Point point;             ///< the projected point
    double offset = 0.0;     ///< distance from the given point to the line
    double fraction() const { return length > 0 ? along / length : 0.0; }
};
/// Closest position on a polyline of at least two points.
PolylinePosition locateOnPolyline(const std::vector<Point>& line, const Point& p);

/// Drawn geometry of a link: start node, vertices and end node. Empty when
/// an end node is missing or has no coordinates.
std::vector<Point> linkPolyline(const Model& model, const LinkItem& link);
/// Geometry of any element: one point for a node or rain gage, the line of a
/// link, the outline of a subcatchment. Empty when it has no coordinates.
std::vector<Point> elementGeometry(const Model& model, const ElementRef& element);

/// Elements inside a polygon. In window mode an element must lie completely
/// inside; in crossing mode touching the polygon is enough.
std::vector<ElementRef> elementsInPolygon(const Model& model, const std::vector<Point>& polygon,
                                          bool crossing);
/// Elements whose geometry comes within @p distance (map units) of the
/// geometry of any of @p from. The elements of @p from are included.
std::vector<ElementRef> elementsWithinDistance(const Model& model, const std::vector<ElementRef>& from,
                                               double distance);

/// Length or area measured on the map.
struct Measurement
{
    double length = 0.0; ///< metres, or map units when the coordinate system is unknown
    double area = 0.0;   ///< square metres, or square map units
    bool metres = false; ///< false when the values are in map units
    bool geodesic = false; ///< measured on the ellipsoid (geographic coordinates)
};

/// Measures a line (and, when @p closed, the area and perimeter of the
/// polygon) in coordinates of @p crs. Projected systems are measured in the
/// plane and converted to metres; geographic ones on the WGS 84 ellipsoid.
/// Without a valid system the values stay in map units.
Measurement measure(const std::vector<Point>& points, bool closed, const crs::Crs* crs);

/// Geodesic distance in metres between two longitude/latitude points on the
/// WGS 84 ellipsoid (Vincenty's inverse formula).
double geodesicDistance(double lon1, double lat1, double lon2, double lat2);
/// Area in square metres of a longitude/latitude polygon, on the sphere of
/// the same surface area as the WGS 84 ellipsoid.
double geodesicArea(const std::vector<Point>& lonLat);

} // namespace mirab::core
