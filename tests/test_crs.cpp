/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_crs.cpp
 * Purpose: Tests for coordinate reference systems and map projections.
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
#include "TestFramework.h"

#include "mirab/core/Crs.h"

#include <cmath>
#include <set>
#include <string>

using namespace mirab;
using crs::Crs;
using crs::Method;

// Reference values: the worked examples of EPSG Guidance Note 7-2 (IOGP
// Publication 373-7-2) and Snyder (USGS PP 1395), plus points cross-checked
// against an independent implementation (PROJ 9.8.1 through pyproj 3.8),
// using the base geographic CRS of each system so that no datum shift applies.

namespace
{

Crs custom(Method m, double a, double invf)
{
    Crs c;
    c.method = m;
    c.a = a;
    c.invf = invf;
    return c;
}

void checkForward(const Crs& c, double lon, double lat, double x, double y, double tol)
{
    double px = 0, py = 0;
    CHECK(crs::fromLonLat(c, lon, lat, px, py));
    CHECK_NEAR(px, x, tol);
    CHECK_NEAR(py, y, tol);
}

void checkInverse(const Crs& c, double x, double y, double lon, double lat, double tolDeg)
{
    double plon = 0, plat = 0;
    CHECK(crs::toLonLat(c, x, y, plon, plat));
    CHECK_NEAR(plon, lon, tolDeg);
    CHECK_NEAR(plat, lat, tolDeg);
}

/// Forward and inverse against a reference point; tol in CRS units.
void checkBoth(const Crs& c, double lon, double lat, double x, double y, double tol)
{
    checkForward(c, lon, lat, x, y, tol);
    checkInverse(c, x, y, lon, lat, 1e-8);
}

/// Projects lon/lat, inverts and projects again; returns the drift in metres.
double roundTripMetres(const Crs& c, double lon, double lat)
{
    double x = 0, y = 0, lon2 = 0, lat2 = 0, x2 = 0, y2 = 0;
    if (!crs::fromLonLat(c, lon, lat, x, y) || !crs::toLonLat(c, x, y, lon2, lat2) ||
        !crs::fromLonLat(c, lon2, lat2, x2, y2))
        return 1e9;
    return std::hypot(x2 - x, y2 - y) * c.unitToMeters;
}

double dms(double d, double m, double s)
{
    return d < 0 ? d - m / 60.0 - s / 3600.0 : d + m / 60.0 + s / 3600.0;
}

const char* kEsriUtm14 =
    R"wkt(PROJCS["NAD_1983_UTM_Zone_14N",GEOGCS["GCS_North_American_1983",DATUM["D_North_American_1983",SPHEROID["GRS_1980",6378137.0,298.257222101]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Transverse_Mercator"],PARAMETER["False_Easting",500000.0],PARAMETER["False_Northing",0.0],PARAMETER["Central_Meridian",-99.0],PARAMETER["Scale_Factor",0.9996],PARAMETER["Latitude_Of_Origin",0.0],UNIT["Meter",1.0]])wkt";

const char* kEsriTexasCentralFeet =
    R"wkt(PROJCS["NAD_1983_StatePlane_Texas_Central_FIPS_4203_Feet",GEOGCS["GCS_North_American_1983",DATUM["D_North_American_1983",SPHEROID["GRS_1980",6378137.0,298.257222101]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Lambert_Conformal_Conic"],PARAMETER["False_Easting",2296583.333333333],PARAMETER["False_Northing",9842500.0],PARAMETER["Central_Meridian",-100.3333333333333],PARAMETER["Standard_Parallel_1",30.11666666666667],PARAMETER["Standard_Parallel_2",31.88333333333333],PARAMETER["Latitude_Of_Origin",29.66666666666667],UNIT["Foot_US",0.3048006096012192]])wkt";

} // namespace

TEST_CASE("crs: parse EPSG code strings")
{
    auto a = crs::parse("EPSG:32614");
    CHECK(a && a->epsg == 32614 && a->method == Method::TransverseMercator);
    auto b = crs::parse("32614");
    CHECK(b && b->epsg == 32614);
    auto c = crs::parse("  epsg:3857\n");
    CHECK(c && c->method == Method::WebMercator && c->epsg == 3857);
    auto d = crs::parse("urn:ogc:def:crs:EPSG::26914");
    CHECK(d && d->epsg == 26914);
    auto e = crs::parse("ESRI:102100");
    CHECK(e && e->method == Method::WebMercator);
    auto f = crs::parse("http://www.opengis.net/def/crs/EPSG/0/4326");
    CHECK(f && f->method == Method::Geographic);
    CHECK(!crs::parse("EPSG:99999"));
    CHECK(!crs::parse(""));
    CHECK(!crs::parse("not a crs"));
    CHECK(!crs::parse("PROJCS[\"broken\""));
}

TEST_CASE("crs: built-in EPSG definitions")
{
    auto g = crs::fromEpsg(4326);
    CHECK(g && g->method == Method::Geographic && g->name == "WGS 84" && g->datum == "WGS84");
    auto nad = crs::fromEpsg(4269);
    CHECK(nad && nad->method == Method::Geographic);
    CHECK_NEAR(nad->invf, 298.257222101, 1e-9);
    CHECK(crs::fromEpsg(4258));
    CHECK(crs::fromEpsg(900913) && crs::fromEpsg(900913)->method == Method::WebMercator);

    auto u = crs::fromEpsg(32614);
    CHECK(u && u->name == "WGS 84 / UTM zone 14N");
    CHECK_NEAR(u->lon0, -99.0, 0);
    CHECK_NEAR(u->k0, 0.9996, 0);
    CHECK_NEAR(u->falseEasting, 500000.0, 0);
    auto s = crs::fromEpsg(32760);
    CHECK(s && s->name == "WGS 84 / UTM zone 60S");
    CHECK_NEAR(s->lon0, 177.0, 0);
    CHECK_NEAR(s->falseNorthing, 10000000.0, 0);
    CHECK(crs::fromEpsg(26923)->name == "NAD83 / UTM zone 23N");
    CHECK_NEAR(crs::fromEpsg(6331)->lon0, -171.0, 0); // NAD83(2011) / UTM zone 2N
    CHECK(crs::fromEpsg(6348)->name == "NAD83(2011) / UTM zone 19N");
    CHECK_NEAR(crs::fromEpsg(25832)->lon0, 9.0, 0);
    auto mga = crs::fromEpsg(28355);
    CHECK(mga && mga->name == "GDA94 / MGA zone 55");
    CHECK_NEAR(mga->lon0, 147.0, 0);
    CHECK_NEAR(mga->falseNorthing, 10000000.0, 0);
    CHECK(crs::fromEpsg(7856)->name == "GDA2020 / MGA zone 56");
    CHECK(crs::fromEpsg(31983)->name == "SIRGAS 2000 / UTM zone 23S");
    CHECK(crs::fromEpsg(31965)->name == "SIRGAS 2000 / UTM zone 11N");

    // NAD83 / Texas Central (ftUS)
    auto tx = crs::fromEpsg(2277);
    CHECK(tx && tx->method == Method::LambertConformalConic2SP);
    CHECK(tx->name == "NAD83 / Texas Central (ftUS)");
    CHECK_NEAR(tx->lat0, dms(29, 40, 0), 1e-12);
    CHECK_NEAR(tx->lon0, dms(-100, 20, 0), 1e-12);
    CHECK_NEAR(tx->lat1, dms(31, 53, 0), 1e-12);
    CHECK_NEAR(tx->lat2, dms(30, 7, 0), 1e-12);
    CHECK_NEAR(tx->unitToMeters, 1200.0 / 3937.0, 1e-16);
    CHECK_NEAR(tx->falseEasting, 2296583.333 * 1200.0 / 3937.0, 1e-6);
    CHECK_NEAR(tx->falseNorthing, 3000000.0, 1e-6);

    CHECK(crs::fromEpsg(5070)->method == Method::AlbersEqualArea);
    CHECK(crs::fromEpsg(3031)->method == Method::PolarStereographic);
    CHECK(crs::fromEpsg(3395)->method == Method::Mercator1SP);
    CHECK(!crs::fromEpsg(26714)); // NAD27 is deliberately not built in
    CHECK(!crs::fromEpsg(27700)); // OSGB 1936 needs a datum shift
    CHECK(!crs::fromEpsg(0));
}

TEST_CASE("crs: common systems list")
{
    const auto list = crs::commonSystems();
    CHECK(list.size() > 1500);
    CHECK_EQ(list[0].epsg, 4326);
    CHECK_EQ(list[1].epsg, 3857);
    std::set<int> codes;
    for (const auto& e : list)
    {
        CHECK(codes.insert(e.epsg).second);
        auto c = crs::fromEpsg(e.epsg);
        CHECK(c && c->name == e.name && c->valid());
    }
    CHECK(codes.count(32614) && codes.count(2277) && codes.count(26914) && codes.count(6539));
}

TEST_CASE("crs: transverse mercator worked example (GN7-2, OSGB 1936 grid)")
{
    Crs c = custom(Method::TransverseMercator, 6377563.396, 299.3249646);
    c.lat0 = 49;
    c.lon0 = -2;
    c.k0 = 0.9996012717;
    c.falseEasting = 400000;
    c.falseNorthing = -100000;
    checkBoth(c, 0.5, 50.5, 577274.9838, 69740.4923, 1e-3);
}

TEST_CASE("crs: UTM reference points")
{
    const Crs u14 = *crs::fromEpsg(32614);
    checkBoth(u14, -99.0, 0.0, 500000.0, 0.0, 1e-6);
    checkBoth(u14, -97.7431, 30.2672, 620903.830065, 3349062.364102, 1e-4);
    checkBoth(u14, -95.0, 45.0, 815261.427154, 4990738.261611, 1e-4);
    checkBoth(u14, -99.0, 84.0, 500000.0, 9328093.830561, 1e-4);
    // Well outside the zone the Kruger series stays accurate.
    checkBoth(u14, -90.0, 30.0, 1369825.158094, 3353121.873786, 1e-3);
    checkBoth(u14, -109.0, 10.0, -601255.226892, 1122228.282913, 1e-3);
    const Crs u56s = *crs::fromEpsg(32756);
    checkBoth(u56s, 151.2093, -33.8688, 334368.633648, 6250948.345385, 1e-4);
}

TEST_CASE("crs: transverse mercator round trips within the zone")
{
    const Crs u = *crs::fromEpsg(32633);
    double worst = 0;
    for (double lat = -80; lat <= 84; lat += 2.5)
        for (double dl = -4; dl <= 4; dl += 0.5)
            worst = std::max(worst, roundTripMetres(u, 15.0 + dl, lat));
    CHECK(worst < 1e-6);
    const Crs ft = *crs::fromEpsg(2236); // NAD83 / Florida East (ftUS), TM in US feet
    checkBoth(ft, -80.593251279140, 28.764752921799, 786482.677653162, 1610974.763958271, 1e-4);
}

TEST_CASE("crs: lambert conic 2SP worked example (GN7-2, Texas South Central)")
{
    // NAD27 / Texas South Central on Clarke 1866, coordinates in US survey feet.
    Crs c = custom(Method::LambertConformalConic2SP, 6378206.4, 294.97869821);
    c.lat0 = dms(27, 50, 0);
    c.lon0 = -99;
    c.lat1 = dms(28, 23, 0);
    c.lat2 = dms(30, 17, 0);
    c.unitToMeters = crs::kUsSurveyFoot;
    c.falseEasting = 2000000.0 * crs::kUsSurveyFoot;
    checkBoth(c, -96.0, 28.5, 2963503.9128, 254759.8006, 1e-3);
}

TEST_CASE("crs: lambert conic 1SP worked example (GN7-2, Jamaica)")
{
    Crs c = custom(Method::LambertConformalConic1SP, 6378206.4, 294.97869821);
    c.lat0 = 18;
    c.lon0 = -77;
    c.k0 = 1;
    c.falseEasting = 250000;
    c.falseNorthing = 150000;
    checkBoth(c, dms(-76, 56, 37.26), dms(17, 55, 55.80), 255966.5818, 142493.5110, 1e-3);
}

TEST_CASE("crs: lambert conic state plane references")
{
    checkBoth(*crs::fromEpsg(2277), -97.7431, 30.2672, 3114226.214204, 10070436.131252, 1e-4);
    checkBoth(*crs::fromEpsg(2229), -118.2437, 34.0522, 6487847.002983, 1841468.251763, 1e-4);
    checkBoth(*crs::fromEpsg(6539), -73.9857, 40.7484, 988212.237183, 211939.278568, 1e-4);
    checkBoth(*crs::fromEpsg(2154), 2.3522, 48.8566, 652469.022709, 6862035.259420, 1e-4);
    // County and regional systems.
    checkBoth(*crs::fromEpsg(7599), -89.596515287324, 43.077948745054, 764429.939779799,
              484065.940558670, 1e-4); // WISCRS Dane (ftUS)
    checkBoth(*crs::fromEpsg(6794), -121.259207433818, 44.095038547237, 79262.561699804,
              66472.237834560, 1e-4); // Oregon Bend-Redmond-Prineville, LCC 1SP
    checkBoth(*crs::fromEpsg(2992), -119.977075271167, 45.172027646248, 1447187.165070936,
              1247787.740338830, 1e-4); // Oregon GIC Lambert, international feet
    checkBoth(*crs::fromEpsg(3112), 142.229028481879, -24.492043429320, 824150.562664689,
              -2813226.597071202, 1e-4); // southern-hemisphere cone
}

TEST_CASE("crs: lambert conic round trips")
{
    for (int code : {2277, 6539, 3034, 6794})
    {
        const Crs c = *crs::fromEpsg(code);
        double worst = 0;
        for (double lat = c.lat0 - 6; lat <= c.lat0 + 6; lat += 0.75)
            for (double dl = -6; dl <= 6; dl += 0.75)
                worst = std::max(worst, roundTripMetres(c, c.lon0 + dl, lat));
        CHECK(worst < 1e-6);
    }
}

TEST_CASE("crs: albers equal area (Snyder example and Conus Albers)")
{
    // Snyder, Map Projections - A Working Manual, numerical example for Albers.
    Crs c = custom(Method::AlbersEqualArea, 6378206.4, 294.978698213898);
    c.lat0 = 23;
    c.lon0 = -96;
    c.lat1 = 29.5;
    c.lat2 = 45.5;
    checkBoth(c, -75.0, 35.0, 1885472.7258, 1535925.0050, 1e-3);
    checkBoth(*crs::fromEpsg(5070), -97.7431, 30.2672, -167451.371088, 800570.852984, 1e-4);
    checkBoth(*crs::fromEpsg(3338), 177.916138884147, 66.865231829254, -1200549.975461832,
              2136323.487482868, 1e-4); // Alaska Albers across the antimeridian
    CHECK(roundTripMetres(*crs::fromEpsg(5070), -120, 48) < 1e-6);
}

TEST_CASE("crs: mercator worked examples (GN7-2)")
{
    Crs m1 = custom(Method::Mercator1SP, 6377397.155, 299.15281);
    m1.lon0 = 110;
    m1.k0 = 0.997;
    m1.falseEasting = 3900000;
    m1.falseNorthing = 900000;
    checkBoth(m1, 120.0, -3.0, 5009726.5833, 569150.8186, 1e-3);

    // Mercator 2SP (Pulkovo 1942 / Caspian Sea) through a PROJ string with lat_ts.
    auto m2 = crs::parse("+proj=merc +lat_ts=42 +lon_0=51 +x_0=0 +y_0=0 +a=6378245 +rf=298.3 +units=m");
    CHECK(m2 && m2->method == Method::Mercator1SP);
    checkBoth(*m2, 53.0, 53.0, 165704.2933, 5171848.0729, 1e-3);

    checkBoth(*crs::fromEpsg(3395), -118.275773735398, -10.149816773737, -13166398.905404968,
              -1128303.885428174, 1e-4);
}

TEST_CASE("crs: polar stereographic worked examples (GN7-2)")
{
    // Variant A: WGS 84 / UPS North.
    checkBoth(*crs::fromEpsg(5041), 44.0, 73.0, 3320416.7474, 632668.4313, 1e-3);
    // Variant B: WGS 84 / Australian Antarctic Polar Stereographic.
    checkBoth(*crs::fromEpsg(3032), 120.0, -75.0, 7255380.7933, 7053389.5606, 1e-3);
    checkBoth(*crs::fromEpsg(3413), 41.646555931122, 83.398928872293, 714610.644768238,
              -41873.013715990, 1e-4);
    checkBoth(*crs::fromEpsg(32761), -17.299417205002, -72.923258114750, 1432175.778840222,
              3823138.999500694, 1e-4);
}

TEST_CASE("crs: web mercator")
{
    const Crs w = *crs::fromEpsg(3857);
    checkBoth(w, dms(-100, 20, 0), dms(24, 22, 54.433), -11169055.5763, 2800000.0031, 1e-3);
    double x = 0, y = 0;
    crs::lonLatToWebMercator(180.0, 89.9, x, y); // latitude clamps to the tile limit
    CHECK_NEAR(x, crs::kWebMercatorHalfWorld, 1e-6);
    CHECK_NEAR(y, crs::kWebMercatorHalfWorld, 1e-3);
    double lon = 0, lat = 0;
    crs::webMercatorToLonLat(-crs::kWebMercatorHalfWorld, -crs::kWebMercatorHalfWorld, lon, lat);
    CHECK_NEAR(lon, -180.0, 1e-9);
    CHECK_NEAR(lat, -85.0511287798066, 1e-9);
    crs::lonLatToWebMercator(-97.7431, 30.2672, x, y);
    crs::webMercatorToLonLat(x, y, lon, lat);
    CHECK_NEAR(lon, -97.7431, 1e-12);
    CHECK_NEAR(lat, 30.2672, 1e-12);
}

TEST_CASE("crs: other projected systems cross-checked")
{
    checkBoth(*crs::fromEpsg(2105), 172.757471126202, -36.119931683992, 219349.977744247,
              882438.980337728, 1e-4); // NZGD2000 Mount Eden, TM with non-zero origin
    checkBoth(*crs::fromEpsg(2193), 167.214691938252, -42.977783917692, 1128222.316178840,
              5225386.149804974, 1e-4);
    checkBoth(*crs::fromEpsg(3034), 36.170891640483, 66.341327416064, 5156909.921269003,
              4572614.230391756, 1e-4);
    checkBoth(*crs::fromEpsg(7131), -122.461866733229, 36.950197415857, 46943.026332384,
              -64766.021743181, 1e-4);
}

TEST_CASE("crs: ESRI .prj for UTM")
{
    auto c = crs::parse(kEsriUtm14);
    CHECK(c && c->method == Method::TransverseMercator);
    CHECK_EQ(c->epsg, 26914);
    CHECK_EQ(c->name, std::string("NAD_1983_UTM_Zone_14N"));
    CHECK_EQ(c->datum, std::string("NAD83"));
    CHECK_NEAR(c->k0, 0.9996, 0);
    CHECK_EQ(crs::toString(*c), std::string("EPSG:26914"));
    checkBoth(*c, -97.7431, 30.2672, 620903.830065, 3349062.364102, 1e-3); // GRS80 vs WGS 84

    // Pretty-printed, lower-case keywords and parentheses are accepted too.
    auto p = crs::parse("projcs ( \"WGS_1984_UTM_Zone_33N\" ,\n  geogcs(\"GCS_WGS_1984\", "
                        "datum(\"D_WGS_1984\", spheroid(\"WGS_1984\", 6378137.0, 298.257223563))),\n"
                        "  projection(\"Transverse_Mercator\"), parameter(\"false_easting\", 500000),\n"
                        "  parameter(\"central_meridian\", 15), parameter(\"scale_factor\", 0.9996),\n"
                        "  unit(\"Meter\", 1) )");
    CHECK(p && p->epsg == 32633);
}

TEST_CASE("crs: ESRI .prj for a state plane zone in US feet")
{
    auto c = crs::parse(kEsriTexasCentralFeet);
    CHECK(c && c->method == Method::LambertConformalConic2SP);
    CHECK_EQ(c->epsg, 2277);
    CHECK_EQ(c->name, std::string("NAD_1983_StatePlane_Texas_Central_FIPS_4203_Feet"));
    CHECK_NEAR(c->unitToMeters, 1200.0 / 3937.0, 1e-16);
    CHECK_NEAR(c->falseEasting, 700000.0, 1e-6); // stored in metres
    CHECK_NEAR(c->falseNorthing, 3000000.0, 1e-6);
    checkBoth(*c, -97.7431, 30.2672, 3114226.214204, 10070436.131252, 1e-3);
}

TEST_CASE("crs: OGC WKT1 and WKT2 with authority")
{
    const char* gdal =
        R"wkt(PROJCS["NAD83 / Texas Central (ftUS)",GEOGCS["NAD83",DATUM["North_American_Datum_1983",SPHEROID["GRS 1980",6378137,298.257222101,AUTHORITY["EPSG","7019"]],AUTHORITY["EPSG","6269"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4269"]],PROJECTION["Lambert_Conformal_Conic_2SP"],PARAMETER["latitude_of_origin",29.6666666666667],PARAMETER["central_meridian",-100.333333333333],PARAMETER["standard_parallel_1",31.8833333333333],PARAMETER["standard_parallel_2",30.1166666666667],PARAMETER["false_easting",2296583.333],PARAMETER["false_northing",9842500],UNIT["US survey foot",0.304800609601219,AUTHORITY["EPSG","9003"]],AXIS["Easting",EAST],AXIS["Northing",NORTH],AUTHORITY["EPSG","2277"]])wkt";
    auto a = crs::parse(gdal);
    CHECK(a && a->epsg == 2277 && a->method == Method::LambertConformalConic2SP);

    const char* wkt2 =
        R"wkt(PROJCRS["NAD83 / Texas Central (ftUS)",BASEGEOGCRS["NAD83",DATUM["North American Datum 1983",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]]],CONVERSION["SPCS83 Texas Central zone (US survey foot)",METHOD["Lambert Conic Conformal (2SP)",ID["EPSG",9802]],PARAMETER["Latitude of false origin",29.6666666666667,ANGLEUNIT["degree",0.0174532925199433]],PARAMETER["Longitude of false origin",-100.333333333333,ANGLEUNIT["degree",0.0174532925199433]],PARAMETER["Latitude of 1st standard parallel",31.8833333333333,ANGLEUNIT["degree",0.0174532925199433]],PARAMETER["Latitude of 2nd standard parallel",30.1166666666667,ANGLEUNIT["degree",0.0174532925199433]],PARAMETER["Easting at false origin",2296583.333,LENGTHUNIT["US survey foot",0.304800609601219]],PARAMETER["Northing at false origin",9842500,LENGTHUNIT["US survey foot",0.304800609601219]]],CS[Cartesian,2],AXIS["easting (X)",east,ORDER[1],LENGTHUNIT["US survey foot",0.304800609601219]],AXIS["northing (Y)",north,ORDER[2],LENGTHUNIT["US survey foot",0.304800609601219]]])wkt";
    auto b = crs::parse(wkt2); // no ID: identified from its parameters
    CHECK(b && b->epsg == 2277);
    CHECK_NEAR(b->falseEasting, 2296583.333 * crs::kUsSurveyFoot, 1e-6);
    checkBoth(*b, -97.7431, 30.2672, 3114226.214204, 10070436.131252, 1e-4);

    const char* esriWeb =
        R"wkt(PROJCS["WGS_1984_Web_Mercator_Auxiliary_Sphere",GEOGCS["GCS_WGS_1984",DATUM["D_WGS_1984",SPHEROID["WGS_1984",6378137.0,298.257223563]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Mercator_Auxiliary_Sphere"],PARAMETER["False_Easting",0.0],PARAMETER["False_Northing",0.0],PARAMETER["Central_Meridian",0.0],PARAMETER["Standard_Parallel_1",0.0],PARAMETER["Auxiliary_Sphere_Type",0.0],UNIT["Meter",1.0]])wkt";
    auto w = crs::parse(esriWeb);
    CHECK(w && w->method == Method::WebMercator && w->epsg == 3857);

    auto geo = crs::parse(
        R"wkt(GEOGCS["GCS_WGS_1984",DATUM["D_WGS_1984",SPHEROID["WGS_1984",6378137.0,298.257223563]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]])wkt");
    CHECK(geo && geo->method == Method::Geographic && geo->epsg == 4326);
}

TEST_CASE("crs: WKT for systems that are not built in")
{
    // NAD27 state plane: projected correctly on Clarke 1866, but flagged as
    // a datum that is not WGS 84-compatible.
    auto n27 = crs::parse(
        R"wkt(PROJCS["NAD_1927_StatePlane_Texas_Central_FIPS_4203",GEOGCS["GCS_North_American_1927",DATUM["D_North_American_1927",SPHEROID["Clarke_1866",6378206.4,294.9786982]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Lambert_Conformal_Conic"],PARAMETER["False_Easting",2000000.0],PARAMETER["False_Northing",0.0],PARAMETER["Central_Meridian",-100.3333333333333],PARAMETER["Standard_Parallel_1",30.11666666666667],PARAMETER["Standard_Parallel_2",31.88333333333333],PARAMETER["Latitude_Of_Origin",29.66666666666667],UNIT["Foot_US",0.3048006096012192]])wkt");
    CHECK(n27 && n27->method == Method::LambertConformalConic2SP);
    CHECK_EQ(n27->epsg, 0);
    CHECK_EQ(n27->datum, std::string("NAD27"));
    CHECK(!crs::isWgs84Compatible(*n27));
    CHECK_NEAR(n27->a, 6378206.4, 1e-9);
    CHECK(crs::isWgs84Compatible(*crs::fromEpsg(2277)));

    // A custom Gauss-Kruger grid: no EPSG code, written back as a PROJ string.
    auto gk = crs::parse(
        R"wkt(PROJCS["Local_Grid",GEOGCS["GCS_WGS_1984",DATUM["D_WGS_1984",SPHEROID["WGS_1984",6378137.0,298.257223563]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Gauss_Kruger"],PARAMETER["False_Easting",1000.0],PARAMETER["False_Northing",2000.0],PARAMETER["Central_Meridian",51.25],PARAMETER["Scale_Factor",1.0],PARAMETER["Latitude_Of_Origin",35.5],UNIT["Foot",0.3048]])wkt");
    CHECK(gk && gk->method == Method::TransverseMercator && gk->epsg == 0);
    CHECK_NEAR(gk->falseEasting, 304.8, 1e-9);
    const std::string text = crs::toString(*gk);
    CHECK(text.find("+proj=tmerc") != std::string::npos);
    CHECK(text.find("+units=ft") != std::string::npos);
    auto back = crs::parse(text);
    CHECK(back && back->method == Method::TransverseMercator);
    CHECK_NEAR(back->lat0, 35.5, 0);
    CHECK_NEAR(back->lon0, 51.25, 0);
    CHECK_NEAR(back->falseEasting, gk->falseEasting, 1e-9);
    CHECK_NEAR(back->unitToMeters, 0.3048, 0);
    double x = 0, y = 0, x2 = 0, y2 = 0;
    CHECK(crs::fromLonLat(*gk, 51.3, 35.6, x, y));
    CHECK(crs::fromLonLat(*back, 51.3, 35.6, x2, y2));
    CHECK_NEAR(x, x2, 1e-9);
    CHECK_NEAR(y, y2, 1e-9);

    CHECK(!crs::parse(
        R"wkt(PROJCS["x",GEOGCS["GCS_WGS_1984",DATUM["D_WGS_1984",SPHEROID["WGS_1984",6378137.0,298.257223563]]],PROJECTION["Robinson"],UNIT["Meter",1.0]])wkt"));
}

TEST_CASE("crs: PROJ strings")
{
    auto a = crs::parse("+proj=utm +zone=14 +datum=WGS84 +units=m +no_defs");
    CHECK(a && a->epsg == 32614);
    auto b = crs::parse("+proj=utm +zone=56 +south +datum=WGS84 +units=m");
    CHECK(b && b->epsg == 32756);
    auto n = crs::parse("+proj=utm +zone=15 +datum=NAD83 +units=m");
    CHECK(n && n->epsg == 26915);
    auto e = crs::parse("+proj=utm +zone=14 +ellps=GRS80 +units=m"); // no datum: no code
    CHECK(e && e->epsg == 0 && e->method == Method::TransverseMercator);
    CHECK(crs::isWgs84Compatible(*e));
    auto g = crs::parse("+proj=longlat +datum=WGS84 +no_defs");
    CHECK(g && g->epsg == 4326);
    auto w = crs::parse("+proj=merc +a=6378137 +b=6378137 +lat_ts=0 +lon_0=0 +x_0=0 +y_0=0 +k=1 "
                        "+units=m +nadgrids=@null +wktext +no_defs");
    CHECK(w && w->method == Method::WebMercator && w->epsg == 3857);
    auto l = crs::parse("+proj=lcc +lat_0=29.6666666666667 +lon_0=-100.333333333333 "
                        "+lat_1=31.8833333333333 +lat_2=30.1166666666667 +x_0=699999.999898399 "
                        "+y_0=3000000 +datum=NAD83 +units=us-ft +no_defs");
    CHECK(l && l->epsg == 2277);
    auto one = crs::parse("+proj=lcc +lat_1=18 +lon_0=-77 +k_0=1 +x_0=250000 +y_0=150000 +ellps=clrk66");
    CHECK(one && one->method == Method::LambertConformalConic1SP);
    CHECK_NEAR(one->lat0, 18.0, 0);
    auto ups = crs::parse("+proj=ups +datum=WGS84");
    CHECK(ups && ups->method == Method::PolarStereographic && ups->epsg == 5041);
    auto aea = crs::parse("+proj=aea +lat_0=23 +lon_0=-96 +lat_1=29.5 +lat_2=45.5 +x_0=0 +y_0=0 "
                          "+datum=NAD83 +units=m");
    CHECK(aea && aea->epsg == 5070);
    auto init = crs::parse("+init=epsg:26914");
    CHECK(init && init->epsg == 26914);
    CHECK(!crs::parse("+proj=robin +datum=WGS84"));
    CHECK(!crs::parse("+proj=utm +zone=61"));
    CHECK(!crs::parse("+proj=sterea +lat_0=52 +lon_0=5"));
}

TEST_CASE("crs: toString round trips")
{
    CHECK_EQ(crs::toString(*crs::fromEpsg(32614)), std::string("EPSG:32614"));
    CHECK_EQ(crs::toString(*crs::fromEpsg(900913)), std::string("EPSG:3857"));
    CHECK_EQ(crs::toString(Crs{}), std::string());

    Crs c = custom(Method::LambertConformalConic2SP, 6378137.0, 298.257222101);
    c.lat0 = 41.5;
    c.lon0 = -87.25;
    c.lat1 = 42.1;
    c.lat2 = 43.3;
    c.falseEasting = 123456.789;
    c.falseNorthing = -9876.5;
    c.unitToMeters = crs::kUsSurveyFoot;
    for (Method m : {Method::TransverseMercator, Method::LambertConformalConic1SP,
                     Method::LambertConformalConic2SP, Method::AlbersEqualArea,
                     Method::Mercator1SP, Method::PolarStereographic, Method::Geographic})
    {
        Crs v = c;
        v.method = m;
        if (m == Method::TransverseMercator || m == Method::LambertConformalConic1SP)
            v.k0 = 0.99993;
        if (m == Method::PolarStereographic)
        {
            v.lat0 = -90;
            v.lat1 = -71;
        }
        const std::string text = crs::toString(v);
        auto back = crs::parse(text);
        CHECK(back && back->method == m);
        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        const double lat = m == Method::PolarStereographic ? -75.0 : 42.0;
        CHECK(crs::fromLonLat(v, -88.0, lat, x1, y1));
        CHECK(crs::fromLonLat(*back, -88.0, lat, x2, y2));
        CHECK_NEAR(x1, x2, 1e-6);
        CHECK_NEAR(y1, y2, 1e-6);
    }
}

TEST_CASE("crs: transform between systems")
{
    const Crs tx = *crs::fromEpsg(2277);
    const Crs web = *crs::fromEpsg(3857);
    double x = 0, y = 0, wx = 0, wy = 0;
    CHECK(crs::transform(tx, web, 3114226.214204, 10070436.131252, x, y));
    crs::lonLatToWebMercator(-97.7431, 30.2672, wx, wy);
    CHECK_NEAR(x, wx, 1e-3);
    CHECK_NEAR(y, wy, 1e-3);
    // UTM zone 14N to 15N and back.
    const Crs u14 = *crs::fromEpsg(32614), u15 = *crs::fromEpsg(32615);
    double a = 0, b = 0, c = 0, d = 0;
    CHECK(crs::transform(u14, u15, 620903.830065, 3349062.364102, a, b));
    CHECK(crs::transform(u15, u14, a, b, c, d));
    CHECK_NEAR(c, 620903.830065, 1e-6);
    CHECK_NEAR(d, 3349062.364102, 1e-6);
}

TEST_CASE("crs: guess from extent")
{
    auto g = crs::guessFromExtent(-97.9, 30.1, -97.5, 30.5);
    CHECK(g && g->epsg == 4326);
    CHECK(!crs::guessFromExtent(600000, 3300000, 640000, 3360000)); // projected metres
    CHECK(!crs::guessFromExtent(10, 10, 90, 90));                   // EPANET-style sketch
    CHECK(!crs::guessFromExtent(-100, 30, -60, 35));                // too wide
    CHECK(!crs::guessFromExtent(170, 0, 185, 5));                   // longitude out of range
    CHECK(!crs::guessFromExtent(0, 0, NAN, 1));
}

TEST_CASE("crs: invalid input is rejected")
{
    double x = 0, y = 0;
    CHECK(!crs::fromLonLat(Crs{}, 0, 0, x, y));
    CHECK(!crs::toLonLat(Crs{}, 0, 0, x, y));
    const Crs u = *crs::fromEpsg(32614);
    CHECK(!crs::fromLonLat(u, -99, 91, x, y));
    CHECK(!crs::toLonLat(u, NAN, 0, x, y));
    CHECK(!crs::fromLonLat(*crs::fromEpsg(3395), 0, 90, x, y));
    CHECK(!crs::fromLonLat(*crs::fromEpsg(2277), -99, -90, x, y)); // opposite pole of the cone
    Crs bad = u;
    bad.unitToMeters = 0;
    CHECK(!crs::fromLonLat(bad, -99, 30, x, y));
}

