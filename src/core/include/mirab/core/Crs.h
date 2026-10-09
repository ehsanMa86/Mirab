/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Crs.h
 * Purpose: Coordinate reference systems and map projections.
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Coordinate reference systems (CRS) and the map projections needed to place
/// network coordinates on a web basemap. Plain C++17, no external libraries.
///
/// Conventions:
///  - x is always easting (or longitude) and y northing (or latitude),
///    whatever the axis order declared by the authority.
///  - Angles are in degrees.
///  - Datum shifts are ignored: every supported datum (NAD83 and its
///    realizations, ETRS89, GDA94/2020, SIRGAS 2000, ...) is treated as
///    WGS 84, which is within about two metres and good enough for display.
///
/// References: J.P. Snyder, "Map Projections - A Working Manual", USGS
/// Professional Paper 1395 (1987); IOGP Publication 373-7-2, "Geomatics
/// Guidance Note 7, part 2: Coordinate Conversions and Transformations"
/// (EPSG Guidance Note 7-2); C.F.F. Karney, "Transverse Mercator with an
/// accuracy of a few nanometers", J. Geodesy 85 (2011) 475-485.
namespace mirab::crs
{

enum class Method
{
    None,
    Geographic,               ///< x = longitude, y = latitude
    WebMercator,              ///< EPSG:3857 spherical "Popular Visualisation Pseudo Mercator"
    TransverseMercator,       ///< lat0, lon0, k0
    LambertConformalConic2SP, ///< lat0 (false origin), lon0, lat1, lat2 (k0 honoured, normally 1)
    LambertConformalConic1SP, ///< lat0 (natural origin), lon0, k0
    Mercator1SP,              ///< lon0, k0 (Mercator 2SP is stored with the equivalent k0)
    AlbersEqualArea,          ///< lat0 (false origin), lon0, lat1, lat2
    PolarStereographic        ///< lat0 = +90 or -90; lat1 = latitude of true scale (variant B)
                              ///< or 0 for variant A (scale k0 at the pole)
};

struct Crs
{
    Method method = Method::None;
    int epsg = 0;     ///< 0 when unknown
    std::string name; ///< human-readable, e.g. "WGS 84 / UTM zone 14N"
    /// Canonical datum label ("WGS84", "NAD83", "NAD83(2011)", "ETRS89", "NAD27", ...)
    /// or the datum name found in WKT; empty when unknown.
    std::string datum;
    // ellipsoid (invf = 0 means a sphere)
    double a = 6378137.0, invf = 298.257223563;
    // projection parameters (degrees for angles)
    double lat0 = 0, lon0 = 0, lat1 = 0, lat2 = 0, k0 = 1;
    /// False easting / northing, always in metres (WKT values are converted on parse).
    double falseEasting = 0, falseNorthing = 0;
    /// Length of one x/y unit in metres: 0.3048006096012192 for US survey
    /// feet, 0.3048 for international feet.
    double unitToMeters = 1.0;

    bool valid() const { return method != Method::None; }
    bool isGeographic() const { return method == Method::Geographic; }
};

/// Parses "EPSG:32614", "32614", "epsg:3857", "ESRI:102100", OGC/ESRI WKT1
/// (the contents of a .prj file), WKT2, or a PROJ string such as
/// "+proj=utm +zone=14 +datum=WGS84 +units=m". When the text carries no EPSG
/// code but its parameters and datum match a known system, epsg is filled in.
std::optional<Crs> parse(std::string_view text);

/// Built-in EPSG definitions (see commonSystems() for the full list).
/// 900913 is accepted as an alias of 3857.
std::optional<Crs> fromEpsg(int code);

/// Projected (or geographic) coordinates to WGS 84 longitude/latitude in degrees.
bool toLonLat(const Crs& crs, double x, double y, double& lon, double& lat);
/// WGS 84 longitude/latitude in degrees to projected (or geographic) coordinates.
bool fromLonLat(const Crs& crs, double lon, double lat, double& x, double& y);
/// Converts a point between two systems through longitude/latitude.
bool transform(const Crs& from, const Crs& to, double x, double y, double& outX, double& outY);

/// "EPSG:32614" when the code is one of the built-in definitions, otherwise a
/// PROJ-style string that parse() accepts back. Empty for an invalid Crs.
std::string toString(const Crs& crs);

/// True when the datum is one that is treated as WGS 84 within about two
/// metres (WGS84, NAD83 family, ETRS89, GDA94, GDA2020, SIRGAS 2000, ...).
/// False for other or unknown datums (for example NAD27), whose coordinates
/// will appear shifted, by up to a few hundred metres, over a basemap.
bool isWgs84Compatible(const Crs& crs);

struct Entry
{
    int epsg;
    std::string name;
};
/// Every built-in system, geographic and Web Mercator first, then by code.
std::vector<Entry> commonSystems();

/// Guess for unknown data: returns Geographic WGS84 when every point lies in
/// lon [-180,180], lat [-90,90] and the extent is small (< 20 degrees in both
/// directions), else nullopt.
std::optional<Crs> guessFromExtent(double minX, double minY, double maxX, double maxY);

/// Spherical Web Mercator (EPSG:3857) used by slippy-map tiles. Latitude is
/// clamped to +/-85.0511287798 degrees, the limit of the square tile pyramid.
void lonLatToWebMercator(double lon, double lat, double& x, double& y);
void webMercatorToLonLat(double x, double y, double& lon, double& lat);

/// Half the side of the Web Mercator square, pi * 6378137 metres.
constexpr double kWebMercatorHalfWorld = 20037508.342789244;

/// Common linear units in metres.
constexpr double kUsSurveyFoot = 1200.0 / 3937.0;
constexpr double kInternationalFoot = 0.3048;

} // namespace mirab::crs
