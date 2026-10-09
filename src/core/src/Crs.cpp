/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Crs.cpp
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
#include "mirab/core/Crs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <utility>

namespace mirab::crs
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kWebRadius = 6378137.0;
constexpr double kWebMaxLat = 85.051128779806592;
constexpr double kWgs84InvF = 298.257223563;
constexpr double kGrs80InvF = 298.257222101;

// ---------------------------------------------------------------------------
// Ellipsoid helpers
// ---------------------------------------------------------------------------

struct Ellipsoid
{
    double a = 0;  ///< semi-major axis (m)
    double e2 = 0; ///< first eccentricity squared
    double e = 0;  ///< first eccentricity
    double n = 0;  ///< third flattening (a-b)/(a+b)
};

Ellipsoid ellipsoidOf(const Crs& c)
{
    Ellipsoid el;
    el.a = c.a;
    const double f = c.invf > 0 ? 1.0 / c.invf : 0.0;
    el.e2 = f * (2.0 - f);
    el.e = std::sqrt(el.e2);
    el.n = f / (2.0 - f);
    return el;
}

double normalizeLon(double lon)
{
    if (lon >= -180.0 && lon <= 180.0)
        return lon;
    lon = std::fmod(lon + 180.0, 360.0);
    if (lon < 0)
        lon += 360.0;
    return lon - 180.0;
}

/// tan of the conformal latitude from tan of the geodetic latitude
/// (Karney 2011, eq. 7-9).
double tauPrime(double tau, double e)
{
    const double tau1 = std::hypot(1.0, tau);
    const double sig = std::sinh(e * std::atanh(e * tau / tau1));
    return std::hypot(1.0, sig) * tau - sig * tau1;
}

/// Inverse of tauPrime by Newton iteration (Karney 2011, eq. 19-21).
double tauFromTauPrime(double taup, double e)
{
    const double e2m = 1.0 - e * e;
    double tau = taup;
    for (int i = 0; i < 20; ++i)
    {
        const double taupa = tauPrime(tau, e);
        const double dtau = (taup - taupa) * (1.0 + e2m * tau * tau) /
                            (e2m * std::hypot(1.0, tau) * std::hypot(1.0, taupa));
        tau += dtau;
        if (!(std::fabs(dtau) >= 1e-15 * std::max(1.0, std::fabs(tau))))
            break;
    }
    return tau;
}

/// Isometric latitude psi = asinh(tan(conformal latitude)).
double isometricLat(double phi, double e)
{
    return std::asinh(tauPrime(std::tan(phi), e));
}

/// Geodetic latitude (radians) from the isometric latitude.
double latFromIsometric(double psi, double e)
{
    return std::atan(tauFromTauPrime(std::sinh(psi), e));
}

/// m = cos(phi) / sqrt(1 - e^2 sin^2 phi)  (Snyder eq. 14-15).
double mFn(double phi, double e2)
{
    const double s = std::sin(phi);
    return std::cos(phi) / std::sqrt(1.0 - e2 * s * s);
}

/// t = tan(pi/4 - phi/2) / ((1 - e sin phi)/(1 + e sin phi))^(e/2)  (Snyder eq. 15-9),
/// evaluated as exp(-psi) to stay accurate near the poles.
double tFn(double phi, double e)
{
    return std::exp(-isometricLat(phi, e));
}

double latFromT(double t, double e)
{
    return latFromIsometric(-std::log(t), e);
}

// ---------------------------------------------------------------------------
// Transverse Mercator: Kruger series to order n^6 (Karney 2011, eq. 35-36).
// ---------------------------------------------------------------------------

struct TmSeries
{
    double A;        ///< rectifying radius
    double alpha[6]; ///< forward coefficients
    double beta[6];  ///< inverse coefficients
};

TmSeries tmSeries(double n)
{
    const double n2 = n * n, n3 = n2 * n, n4 = n3 * n, n5 = n4 * n, n6 = n5 * n;
    TmSeries s{};
    s.A = 1.0 / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0 + n6 / 256.0);
    s.alpha[0] = n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0 + 41.0 * n4 / 180.0 -
                 127.0 * n5 / 288.0 + 7891.0 * n6 / 37800.0;
    s.alpha[1] = 13.0 * n2 / 48.0 - 3.0 * n3 / 5.0 + 557.0 * n4 / 1440.0 + 281.0 * n5 / 630.0 -
                 1983433.0 * n6 / 1935360.0;
    s.alpha[2] = 61.0 * n3 / 240.0 - 103.0 * n4 / 140.0 + 15061.0 * n5 / 26880.0 +
                 167603.0 * n6 / 181440.0;
    s.alpha[3] = 49561.0 * n4 / 161280.0 - 179.0 * n5 / 168.0 + 6601661.0 * n6 / 7257600.0;
    s.alpha[4] = 34729.0 * n5 / 80640.0 - 3418889.0 * n6 / 1995840.0;
    s.alpha[5] = 212378941.0 * n6 / 319334400.0;
    s.beta[0] = n / 2.0 - 2.0 * n2 / 3.0 + 37.0 * n3 / 96.0 - n4 / 360.0 - 81.0 * n5 / 512.0 +
                96199.0 * n6 / 604800.0;
    s.beta[1] = n2 / 48.0 + n3 / 15.0 - 437.0 * n4 / 1440.0 + 46.0 * n5 / 105.0 -
                1118711.0 * n6 / 3870720.0;
    s.beta[2] = 17.0 * n3 / 480.0 - 37.0 * n4 / 840.0 - 209.0 * n5 / 4480.0 + 5569.0 * n6 / 90720.0;
    s.beta[3] = 4397.0 * n4 / 161280.0 - 11.0 * n5 / 504.0 - 830251.0 * n6 / 7257600.0;
    s.beta[4] = 4583.0 * n5 / 161280.0 - 108847.0 * n6 / 3991680.0;
    s.beta[5] = 20648693.0 * n6 / 638668800.0;
    return s;
}

/// Rectifying latitude xi (radians) of the central meridian at latitude phi.
double tmXiOnMeridian(const TmSeries& s, double phi, double e)
{
    const double xip = std::atan(tauPrime(std::tan(phi), e));
    double xi = xip;
    for (int j = 1; j <= 6; ++j)
        xi += s.alpha[j - 1] * std::sin(2.0 * j * xip);
    return xi;
}

bool tmForward(const Crs& c, double lon, double lat, double& x, double& y)
{
    const Ellipsoid el = ellipsoidOf(c);
    const TmSeries s = tmSeries(el.n);
    const double lam = normalizeLon(lon - c.lon0) * kDeg;
    const double phi = lat * kDeg;
    const double taup = tauPrime(std::tan(phi), el.e);
    const double xip = std::atan2(taup, std::cos(lam));
    const double etap = std::asinh(std::sin(lam) / std::hypot(taup, std::cos(lam)));
    double xi = xip, eta = etap;
    for (int j = 1; j <= 6; ++j)
    {
        xi += s.alpha[j - 1] * std::sin(2.0 * j * xip) * std::cosh(2.0 * j * etap);
        eta += s.alpha[j - 1] * std::cos(2.0 * j * xip) * std::sinh(2.0 * j * etap);
    }
    const double xi0 = tmXiOnMeridian(s, c.lat0 * kDeg, el.e);
    const double kA = c.k0 * el.a * s.A;
    x = c.falseEasting + kA * eta;
    y = c.falseNorthing + kA * (xi - xi0);
    return true;
}

bool tmInverse(const Crs& c, double x, double y, double& lon, double& lat)
{
    const Ellipsoid el = ellipsoidOf(c);
    const TmSeries s = tmSeries(el.n);
    const double kA = c.k0 * el.a * s.A;
    const double xi = (y - c.falseNorthing) / kA + tmXiOnMeridian(s, c.lat0 * kDeg, el.e);
    const double eta = (x - c.falseEasting) / kA;
    double xip = xi, etap = eta;
    for (int j = 1; j <= 6; ++j)
    {
        xip -= s.beta[j - 1] * std::sin(2.0 * j * xi) * std::cosh(2.0 * j * eta);
        etap -= s.beta[j - 1] * std::cos(2.0 * j * xi) * std::sinh(2.0 * j * eta);
    }
    const double sh = std::sinh(etap);
    const double co = std::max(0.0, std::cos(xip));
    const double r = std::hypot(sh, co);
    double phi, lam;
    if (r != 0)
    {
        lam = std::atan2(sh, co);
        phi = std::atan(tauFromTauPrime(std::sin(xip) / r, el.e));
    }
    else
    {
        lam = 0;
        phi = xip < 0 ? -kPi / 2 : kPi / 2;
    }
    lon = normalizeLon(c.lon0 + lam / kDeg);
    lat = phi / kDeg;
    return true;
}

// ---------------------------------------------------------------------------
// Lambert Conformal Conic (Snyder ch. 15; EPSG GN7-2 section 3.2.1).
// One general form covers 1SP (lat1 = lat2 = lat0) and 2SP (k0 = 1).
// ---------------------------------------------------------------------------

struct LccConst
{
    double n, aF, rF;
};

LccConst lccConst(const Crs& c, const Ellipsoid& el)
{
    double phi1 = c.lat1 * kDeg, phi2 = c.lat2 * kDeg;
    if (c.method == Method::LambertConformalConic1SP)
        phi1 = phi2 = c.lat0 * kDeg;
    const double m1 = mFn(phi1, el.e2), t1 = tFn(phi1, el.e);
    double n;
    if (std::fabs(phi1 - phi2) > 1e-12)
    {
        const double m2 = mFn(phi2, el.e2), t2 = tFn(phi2, el.e);
        n = (std::log(m1) - std::log(m2)) / (std::log(t1) - std::log(t2));
    }
    else
    {
        n = std::sin(phi1);
    }
    LccConst k{};
    k.n = n;
    k.aF = el.a * c.k0 * m1 / (n * std::pow(t1, n));
    k.rF = k.aF * std::pow(tFn(c.lat0 * kDeg, el.e), n);
    return k;
}

bool lccForward(const Crs& c, double lon, double lat, double& x, double& y)
{
    const Ellipsoid el = ellipsoidOf(c);
    const LccConst k = lccConst(c, el);
    if (k.n == 0 || !std::isfinite(k.aF) || !std::isfinite(k.rF))
        return false;
    if ((k.n > 0 && lat <= -90.0) || (k.n < 0 && lat >= 90.0))
        return false; // the apex-opposite pole maps to infinity
    const double t = tFn(lat * kDeg, el.e);
    const double r = k.aF * std::pow(t, k.n);
    if (!std::isfinite(r))
        return false;
    const double theta = k.n * normalizeLon(lon - c.lon0) * kDeg;
    x = c.falseEasting + r * std::sin(theta);
    y = c.falseNorthing + k.rF - r * std::cos(theta);
    return true;
}

bool lccInverse(const Crs& c, double x, double y, double& lon, double& lat)
{
    const Ellipsoid el = ellipsoidOf(c);
    const LccConst k = lccConst(c, el);
    if (k.n == 0 || !std::isfinite(k.aF) || !std::isfinite(k.rF))
        return false;
    double dx = x - c.falseEasting;
    double dy = k.rF - (y - c.falseNorthing);
    if (k.n < 0)
    {
        dx = -dx;
        dy = -dy;
    }
    const double r = std::hypot(dx, dy);
    const double theta = std::atan2(dx, dy);
    double phi;
    if (r == 0)
        phi = k.n > 0 ? kPi / 2 : -kPi / 2;
    else
    {
        const double t = std::pow(r / std::fabs(k.aF), 1.0 / k.n);
        phi = latFromT(t, el.e);
    }
    lon = normalizeLon(c.lon0 + theta / k.n / kDeg);
    lat = phi / kDeg;
    return std::isfinite(lon) && std::isfinite(lat);
}

// ---------------------------------------------------------------------------
// Albers Equal Area (Snyder ch. 14; EPSG GN7-2 section 3.3.3).
// ---------------------------------------------------------------------------

double qFn(double phi, const Ellipsoid& el)
{
    const double s = std::sin(phi);
    if (el.e < 1e-12)
        return 2.0 * s;
    return (1.0 - el.e2) *
           (s / (1.0 - el.e2 * s * s) - std::log((1.0 - el.e * s) / (1.0 + el.e * s)) / (2.0 * el.e));
}

struct AeaConst
{
    double n, C, rho0;
};

AeaConst aeaConst(const Crs& c, const Ellipsoid& el)
{
    const double phi1 = c.lat1 * kDeg, phi2 = c.lat2 * kDeg;
    const double m1 = mFn(phi1, el.e2), q1 = qFn(phi1, el);
    double n;
    if (std::fabs(phi1 - phi2) > 1e-12)
    {
        const double m2 = mFn(phi2, el.e2), q2 = qFn(phi2, el);
        n = (m1 * m1 - m2 * m2) / (q2 - q1);
    }
    else
    {
        n = std::sin(phi1);
    }
    AeaConst k{};
    k.n = n;
    k.C = m1 * m1 + n * q1;
    k.rho0 = el.a * std::sqrt(std::max(0.0, k.C - n * qFn(c.lat0 * kDeg, el))) / n;
    return k;
}

bool aeaForward(const Crs& c, double lon, double lat, double& x, double& y)
{
    const Ellipsoid el = ellipsoidOf(c);
    const AeaConst k = aeaConst(c, el);
    if (k.n == 0)
        return false;
    const double rho = el.a * std::sqrt(std::max(0.0, k.C - k.n * qFn(lat * kDeg, el))) / k.n;
    const double theta = k.n * normalizeLon(lon - c.lon0) * kDeg;
    x = c.falseEasting + rho * std::sin(theta);
    y = c.falseNorthing + k.rho0 - rho * std::cos(theta);
    return std::isfinite(x) && std::isfinite(y);
}

bool aeaInverse(const Crs& c, double x, double y, double& lon, double& lat)
{
    const Ellipsoid el = ellipsoidOf(c);
    const AeaConst k = aeaConst(c, el);
    if (k.n == 0)
        return false;
    double dx = x - c.falseEasting;
    double dy = k.rho0 - (y - c.falseNorthing);
    if (k.n < 0)
    {
        dx = -dx;
        dy = -dy;
    }
    const double rho = std::hypot(dx, dy);
    const double theta = std::atan2(dx, dy);
    const double q = (k.C - rho * rho * k.n * k.n / (el.a * el.a)) / k.n;
    const double qp = qFn(kPi / 2, el);
    double phi;
    if (std::fabs(q) >= qp)
    {
        if (std::fabs(q) > qp * (1.0 + 1e-9))
            return false;
        phi = q < 0 ? -kPi / 2 : kPi / 2;
    }
    else
    {
        // Snyder eq. 3-16, Newton-like iteration from the authalic latitude.
        phi = std::asin(q / qp);
        for (int i = 0; i < 30; ++i)
        {
            const double s = std::sin(phi), co = std::cos(phi);
            if (std::fabs(co) < 1e-15)
                break;
            const double w = 1.0 - el.e2 * s * s;
            double dphi;
            if (el.e < 1e-12)
                dphi = (q / 2.0 - s) / co;
            else
                dphi = w * w / (2.0 * co) *
                       (q / (1.0 - el.e2) - s / w +
                        std::log((1.0 - el.e * s) / (1.0 + el.e * s)) / (2.0 * el.e));
            phi += dphi;
            if (std::fabs(dphi) < 1e-15)
                break;
        }
    }
    lon = normalizeLon(c.lon0 + theta / k.n / kDeg);
    lat = phi / kDeg;
    return std::isfinite(lon) && std::isfinite(lat);
}

// ---------------------------------------------------------------------------
// Mercator (variant A, EPSG GN7-2 section 3.5.1; Snyder ch. 7).
// ---------------------------------------------------------------------------

bool mercForward(const Crs& c, double lon, double lat, double& x, double& y)
{
    if (!(std::fabs(lat) < 90.0))
        return false;
    const Ellipsoid el = ellipsoidOf(c);
    x = c.falseEasting + el.a * c.k0 * normalizeLon(lon - c.lon0) * kDeg;
    y = c.falseNorthing + el.a * c.k0 * isometricLat(lat * kDeg, el.e);
    return true;
}

bool mercInverse(const Crs& c, double x, double y, double& lon, double& lat)
{
    const Ellipsoid el = ellipsoidOf(c);
    const double psi = (y - c.falseNorthing) / (el.a * c.k0);
    lon = normalizeLon(c.lon0 + (x - c.falseEasting) / (el.a * c.k0) / kDeg);
    lat = latFromIsometric(psi, el.e) / kDeg;
    return std::isfinite(lon) && std::isfinite(lat);
}

// ---------------------------------------------------------------------------
// Polar Stereographic variants A and B (EPSG GN7-2 section 3.3.2; Snyder ch. 21).
// ---------------------------------------------------------------------------

bool polarVariantB(const Crs& c)
{
    return c.lat1 != 0 && std::fabs(std::fabs(c.lat1) - 90.0) > 1e-12;
}

/// rho = scale * t where t = tan(pi/4 - chi/2) of the (pole-flipped) latitude.
double polarScale(const Crs& c, const Ellipsoid& el)
{
    if (polarVariantB(c))
    {
        const double phic = std::fabs(c.lat1) * kDeg;
        return el.a * mFn(phic, el.e2) / tFn(phic, el.e);
    }
    const double e = el.e;
    return 2.0 * el.a * c.k0 / std::sqrt(std::pow(1.0 + e, 1.0 + e) * std::pow(1.0 - e, 1.0 - e));
}

bool psForward(const Crs& c, double lon, double lat, double& x, double& y)
{
    const Ellipsoid el = ellipsoidOf(c);
    const bool north = c.lat0 > 0;
    const double phi = (north ? lat : -lat) * kDeg;
    if (phi <= -kPi / 2)
        return false;
    const double rho = polarScale(c, el) * tFn(phi, el.e);
    const double lam = normalizeLon(lon - c.lon0) * kDeg;
    x = c.falseEasting + rho * std::sin(lam);
    y = north ? c.falseNorthing - rho * std::cos(lam) : c.falseNorthing + rho * std::cos(lam);
    return std::isfinite(x) && std::isfinite(y);
}

bool psInverse(const Crs& c, double x, double y, double& lon, double& lat)
{
    const Ellipsoid el = ellipsoidOf(c);
    const bool north = c.lat0 > 0;
    const double dx = x - c.falseEasting, dy = y - c.falseNorthing;
    const double rho = std::hypot(dx, dy);
    double phi = kPi / 2;
    if (rho > 0)
        phi = latFromT(rho / polarScale(c, el), el.e);
    const double lam = north ? std::atan2(dx, -dy) : std::atan2(dx, dy);
    lon = normalizeLon(c.lon0 + lam / kDeg);
    lat = (north ? phi : -phi) / kDeg;
    return std::isfinite(lon) && std::isfinite(lat);
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& ch : out)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

/// Lower-case letters and digits only: "Standard_Parallel_1" -> "standardparallel1".
std::string squash(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char ch : s)
        if (std::isalnum(static_cast<unsigned char>(ch)))
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view s, std::string_view part)
{
    return s.find(part) != std::string_view::npos;
}

std::string_view trim(std::string_view s)
{
    if (startsWith(s, "\xEF\xBB\xBF"))
        s.remove_prefix(3);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool parseDouble(std::string_view s, double& out)
{
    const std::string tmp(trim(s));
    if (tmp.empty())
        return false;
    char* end = nullptr;
    out = std::strtod(tmp.c_str(), &end);
    return end == tmp.c_str() + tmp.size() && std::isfinite(out);
}

bool parseInt(std::string_view s, int& out)
{
    s = trim(s);
    if (s.empty() || s.size() > 9)
        return false;
    int v = 0;
    for (char ch : s)
    {
        if (ch < '0' || ch > '9')
            return false;
        v = v * 10 + (ch - '0');
    }
    out = v;
    return true;
}

/// Shortest decimal text that reads back to the same double.
std::string fmtNum(double v)
{
    if (v == 0)
        return "0";
    char buf[40];
    for (int prec = 15; prec <= 17; ++prec)
    {
        std::snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v)
            break;
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Datums
// ---------------------------------------------------------------------------

/// Canonical label of a datum name from WKT or PROJ; empty when unrecognised.
std::string canonicalDatum(std::string_view raw)
{
    std::string s = squash(raw);
    if (raw.size() > 2 && (raw[0] == 'D' || raw[0] == 'd') && raw[1] == '_' && !s.empty())
        s.erase(0, 1); // ESRI "D_North_American_1983"
    const bool nadFamily = contains(s, "nad") || contains(s, "northamerican");
    if (nadFamily && (contains(s, "1927") || contains(s, "nad27")))
        return "NAD27";
    if (nadFamily && (contains(s, "1983") || contains(s, "nad83")))
    {
        if (contains(s, "harn") || contains(s, "highaccuracy"))
            return "NAD83(HARN)";
        if (contains(s, "2011"))
            return "NAD83(2011)";
        if (contains(s, "csrs") || contains(s, "canadian"))
            return "NAD83(CSRS)";
        if (contains(s, "2007"))
            return "NAD83(NSRS2007)";
        if (contains(s, "cors96"))
            return "NAD83(CORS96)";
        if (contains(s, "pa11"))
            return "NAD83(PA11)";
        if (contains(s, "ma11"))
            return "NAD83(MA11)";
        return "NAD83";
    }
    if (contains(s, "wgs1984") || contains(s, "wgs84") || contains(s, "worldgeodeticsystem1984"))
        return "WGS84";
    if (contains(s, "etrs") || contains(s, "europeanterrestrialreferencesystem1989"))
        return "ETRS89";
    if (contains(s, "gda1994") || contains(s, "gda94") ||
        contains(s, "geocentricdatumofaustralia1994"))
        return "GDA94";
    if (contains(s, "gda2020") || contains(s, "geocentricdatumofaustralia2020"))
        return "GDA2020";
    if (contains(s, "sirgas2000") || contains(s, "sistemadereferenciageocentricoparaasamericas2000"))
        return "SIRGAS2000";
    if (contains(s, "nzgd2000") || contains(s, "newzealandgeodeticdatum2000"))
        return "NZGD2000";
    if (contains(s, "rgf1993") || contains(s, "rgf93") ||
        contains(s, "reseaugeodesiquefrancais1993"))
        return "RGF93";
    if (contains(s, "jgd2011") || contains(s, "japanesegeodeticdatum2011"))
        return "JGD2011";
    if (contains(s, "jgd2000") || contains(s, "japanesegeodeticdatum2000"))
        return "JGD2000";
    if (contains(s, "sweref99"))
        return "SWEREF99";
    return {};
}

bool compatibleDatumLabel(const std::string& d)
{
    static const char* const kCompatible[] = {"WGS84",    "ETRS89",   "GDA94",   "GDA2020",
                                              "SIRGAS2000", "NZGD2000", "RGF93", "JGD2011",
                                              "JGD2000",  "SWEREF99"};
    if (startsWith(d, "NAD83"))
        return true;
    for (const char* k : kCompatible)
        if (d == k)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Built-in EPSG definitions
// ---------------------------------------------------------------------------

enum DatumId : unsigned char
{
    D_WGS84,
    D_NAD83,
    D_NAD83_HARN,
    D_NAD83_2011,
    D_NAD83_CSRS,
    D_ETRS89,
    D_GDA94,
    D_GDA2020,
    D_SIRGAS2000,
    D_NZGD2000,
    D_RGF93,
    D_JGD2011,
    D_SWEREF99
};

const char* datumLabel(DatumId d)
{
    switch (d)
    {
    case D_WGS84: return "WGS84";
    case D_NAD83: return "NAD83";
    case D_NAD83_HARN: return "NAD83(HARN)";
    case D_NAD83_2011: return "NAD83(2011)";
    case D_NAD83_CSRS: return "NAD83(CSRS)";
    case D_ETRS89: return "ETRS89";
    case D_GDA94: return "GDA94";
    case D_GDA2020: return "GDA2020";
    case D_SIRGAS2000: return "SIRGAS2000";
    case D_NZGD2000: return "NZGD2000";
    case D_RGF93: return "RGF93";
    case D_JGD2011: return "JGD2011";
    case D_SWEREF99: return "SWEREF99";
    }
    return "";
}

enum EllipsoidId : unsigned char
{
    E_WGS84,
    E_GRS80
};

enum UnitId : unsigned char
{
    UM,
    UFTUS,
    UFT
};

enum Kind : unsigned char
{
    TM,
    LCC2,
    LCC1,
    AEA,
    MERC,
    MERC2,
    PS
};

/// Angle given in arc-seconds, so that repeating decimals stay exact.
constexpr double S(double arcSeconds)
{
    return arcSeconds / 3600.0;
}

struct Row
{
    int code;
    const char* name;
    Kind kind;
    DatumId datum;
    EllipsoidId ellipsoid;
    UnitId unit;
    double lat0, lon0, lat1, lat2, k0, fe, fn; ///< fe/fn in the CRS unit, as published
};

// Projected systems on WGS 84-compatible datums, extracted from the EPSG
// Geodetic Parameter Dataset (non-deprecated entries whose conversion method
// is supported here) and checked point by point against an independent
// implementation. UTM zones are generated in code (see utmFromEpsg()).
// clang-format off
const Row kRows[] = {
    {2105, "NZGD2000 / Mount Eden 2000", TM, D_NZGD2000, E_GRS80, UM, S(-132767), S(629151), 0, 0, 0.9999, 400000, 800000},
    {2106, "NZGD2000 / Bay of Plenty 2000", TM, D_NZGD2000, E_GRS80, UM, S(-135940), S(635278), 0, 0, 1, 400000, 800000},
    {2107, "NZGD2000 / Poverty Bay 2000", TM, D_NZGD2000, E_GRS80, UM, S(-139048), S(640388), 0, 0, 1, 400000, 800000},
    {2108, "NZGD2000 / Hawkes Bay 2000", TM, D_NZGD2000, E_GRS80, UM, S(-142743), S(636025), 0, 0, 1, 400000, 800000},
    {2109, "NZGD2000 / Taranaki 2000", TM, D_NZGD2000, E_GRS80, UM, S(-140888), S(627220), 0, 0, 1, 400000, 800000},
    {2110, "NZGD2000 / Tuhirangi 2000", TM, D_NZGD2000, E_GRS80, UM, S(-142244), 175.64, 0, 0, 1, 400000, 800000},
    {2111, "NZGD2000 / Wanganui 2000", TM, D_NZGD2000, E_GRS80, UM, S(-144871), S(631757), 0, 0, 1, 400000, 800000},
    {2112, "NZGD2000 / Wairarapa 2000", TM, D_NZGD2000, E_GRS80, UM, S(-147331), S(632330), 0, 0, 1, 400000, 800000},
    {2113, "NZGD2000 / Wellington 2000", TM, D_NZGD2000, E_GRS80, UM, S(-148684), S(629195), 0, 0, 1, 400000, 800000},
    {2114, "NZGD2000 / Collingwood 2000", TM, D_NZGD2000, E_GRS80, UM, S(-146573), S(621619), 0, 0, 1, 400000, 800000},
    {2115, "NZGD2000 / Nelson 2000", TM, D_NZGD2000, E_GRS80, UM, S(-148588), S(623877), 0, 0, 1, 400000, 800000},
    {2116, "NZGD2000 / Karamea 2000", TM, D_NZGD2000, E_GRS80, UM, S(-148643), S(619592), 0, 0, 1, 400000, 800000},
    {2117, "NZGD2000 / Buller 2000", TM, D_NZGD2000, E_GRS80, UM, S(-150518), S(617692), 0, 0, 1, 400000, 800000},
    {2118, "NZGD2000 / Grey 2000", TM, D_NZGD2000, E_GRS80, UM, S(-152401), S(617579), 0, 0, 1, 400000, 800000},
    {2119, "NZGD2000 / Amuri 2000", TM, D_NZGD2000, E_GRS80, UM, S(-153680), 173.01, 0, 0, 1, 400000, 800000},
    {2120, "NZGD2000 / Marlborough 2000", TM, D_NZGD2000, E_GRS80, UM, S(-149560), S(625687), 0, 0, 1, 400000, 800000},
    {2121, "NZGD2000 / Hokitika 2000", TM, D_NZGD2000, E_GRS80, UM, S(-154390), S(615527), 0, 0, 1, 400000, 800000},
    {2122, "NZGD2000 / Okarito 2000", TM, D_NZGD2000, E_GRS80, UM, -43.11, S(612939), 0, 0, 1, 400000, 800000},
    {2123, "NZGD2000 / Jacksons Bay 2000", TM, D_NZGD2000, E_GRS80, UM, S(-158320), S(606982), 0, 0, 1, 400000, 800000},
    {2124, "NZGD2000 / Mount Pleasant 2000", TM, D_NZGD2000, E_GRS80, UM, S(-156926), S(621817), 0, 0, 1, 400000, 800000},
    {2125, "NZGD2000 / Gawler 2000", TM, D_NZGD2000, E_GRS80, UM, S(-157495), S(616898), 0, 0, 1, 400000, 800000},
    {2126, "NZGD2000 / Timaru 2000", TM, D_NZGD2000, E_GRS80, UM, S(-159847), S(615806), 0, 0, 1, 400000, 800000},
    {2127, "NZGD2000 / Lindis Peak 2000", TM, D_NZGD2000, E_GRS80, UM, -44.735, 169.4675, 0, 0, 1, 400000, 800000},
    {2128, "NZGD2000 / Mount Nicholas 2000", TM, D_NZGD2000, E_GRS80, UM, S(-162478), S(606235), 0, 0, 1, 400000, 800000},
    {2129, "NZGD2000 / Mount York 2000", TM, D_NZGD2000, E_GRS80, UM, S(-164029), S(603859), 0, 0, 1, 400000, 800000},
    {2130, "NZGD2000 / Observation Point 2000", TM, D_NZGD2000, E_GRS80, UM, S(-164938), S(614262), 0, 0, 1, 400000, 800000},
    {2131, "NZGD2000 / North Taieri 2000", TM, D_NZGD2000, E_GRS80, UM, S(-165101), 170.2825, 0, 0, 0.99996, 400000, 800000},
    {2132, "NZGD2000 / Bluff 2000", TM, D_NZGD2000, E_GRS80, UM, -46.6, S(606034), 0, 0, 1, 400000, 800000},
    {2133, "NZGD2000 / UTM zone 58S", TM, D_NZGD2000, E_GRS80, UM, 0, 165, 0, 0, 0.9996, 500000, 10000000},
    {2134, "NZGD2000 / UTM zone 59S", TM, D_NZGD2000, E_GRS80, UM, 0, 171, 0, 0, 0.9996, 500000, 10000000},
    {2135, "NZGD2000 / UTM zone 60S", TM, D_NZGD2000, E_GRS80, UM, 0, 177, 0, 0, 0.9996, 500000, 10000000},
    {2154, "RGF93 v1 / Lambert-93", LCC2, D_RGF93, E_GRS80, UM, 46.5, 3, 49, 44, 1, 700000, 6600000},
    {2193, "NZGD2000 / New Zealand Transverse Mercator 2000", TM, D_NZGD2000, E_GRS80, UM, 0, 173, 0, 0, 0.9996, 1600000, 10000000},
    {2195, "NAD83(HARN) / UTM zone 2S", TM, D_NAD83_HARN, E_GRS80, UM, 0, -171, 0, 0, 0.9996, 500000, 10000000},
    {2196, "ETRS89 / Kp2000 Jutland", TM, D_ETRS89, E_GRS80, UM, 0, 9.5, 0, 0, 0.99995, 200000, 0},
    {2197, "ETRS89 / Kp2000 Zealand", TM, D_ETRS89, E_GRS80, UM, 0, 12, 0, 0, 0.99995, 500000, 0},
    {2198, "ETRS89 / Kp2000 Bornholm", TM, D_ETRS89, E_GRS80, UM, 0, 15, 0, 0, 1, 900000, 0},
    {2205, "NAD83 / Kentucky North", LCC2, D_NAD83, E_GRS80, UM, 37.5, -84.25, S(136680), S(140280), 1, 500000, 0},
    {2213, "ETRS89 / TM 30 NE", TM, D_ETRS89, E_GRS80, UM, 0, 30, 0, 0, 0.9996, 500000, 0},
    {2222, "NAD83 / Arizona East (ft)", TM, D_NAD83, E_GRS80, UFT, 31, S(-396600), 0, 0, 0.9999, 700000, 0},
    {2223, "NAD83 / Arizona Central (ft)", TM, D_NAD83, E_GRS80, UFT, 31, S(-402900), 0, 0, 0.9999, 700000, 0},
    {2224, "NAD83 / Arizona West (ft)", TM, D_NAD83, E_GRS80, UFT, 31, -113.75, 0, 0, 0.999933333, 700000, 0},
    {2225, "NAD83 / California zone 1 (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(141600), -122, S(150000), 40, 1, 6561666.667, 1640416.667},
    {2226, "NAD83 / California zone 2 (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(135600), -122, S(143400), S(138000), 1, 6561666.667, 1640416.667},
    {2227, "NAD83 / California zone 3 (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 36.5, -120.5, S(138360), S(133440), 1, 6561666.667, 1640416.667},
    {2228, "NAD83 / California zone 4 (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(127200), -119, 37.25, 36, 1, 6561666.667, 1640416.667},
    {2229, "NAD83 / California zone 5 (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 33.5, -118, S(127680), S(122520), 1, 6561666.667, 1640416.667},
    {2230, "NAD83 / California zone 6 (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(115800), -116.25, S(121980), S(118020), 1, 6561666.667, 1640416.667},
    {2231, "NAD83 / Colorado North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(141600), -105.5, S(146820), S(142980), 1, 3000000, 1000000},
    {2232, "NAD83 / Colorado Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(136200), -105.5, 39.75, 38.45, 1, 3000000, 1000000},
    {2233, "NAD83 / Colorado South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(132000), -105.5, S(138360), S(134040), 1, 3000000, 1000000},
    {2234, "NAD83 / Connecticut (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(147000), -72.75, S(150720), 41.2, 1, 1000000, 500000},
    {2235, "NAD83 / Delaware (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 38, S(-271500), 0, 0, 0.999995, 656166.667, 0},
    {2236, "NAD83 / Florida East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(87600), -81, 0, 0, 0.999941177, 656166.667, 0},
    {2237, "NAD83 / Florida West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(87600), -82, 0, 0, 0.999941177, 656166.667, 0},
    {2238, "NAD83 / Florida North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 29, -84.5, 30.75, S(106500), 1, 1968500, 0},
    {2239, "NAD83 / Georgia East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 30, S(-295800), 0, 0, 0.9999, 656166.667, 0},
    {2240, "NAD83 / Georgia West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 30, S(-303000), 0, 0, 0.9999, 2296583.333, 0},
    {2241, "NAD83 / Idaho East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(150000), S(-403800), 0, 0, 0.999947368, 656166.667, 0},
    {2242, "NAD83 / Idaho Central (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(150000), -114, 0, 0, 0.999947368, 1640416.667, 0},
    {2243, "NAD83 / Idaho West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(150000), -115.75, 0, 0, 0.999933333, 2624666.667, 0},
    {2246, "NAD83 / Kentucky North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 37.5, -84.25, S(136680), S(140280), 1, 1640416.667, 0},
    {2247, "NAD83 / Kentucky South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(130800), -85.75, S(136560), S(132240), 1, 1640416.667, 1640416.667},
    {2248, "NAD83 / Maryland (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(135600), -77, 39.45, 38.3, 1, 1312333.333, 0},
    {2249, "NAD83 / Massachusetts Mainland (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 41, -71.5, S(153660), S(150180), 1, 656166.667, 2460625},
    {2250, "NAD83 / Massachusetts Island (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 41, -70.5, S(149340), S(148620), 1, 1640416.667, 0},
    {2251, "NAD83 / Michigan North (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(161220), -87, S(169500), S(163740), 1, 26246719.16, 0},
    {2252, "NAD83 / Michigan Central (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(155940), S(-303720), 45.7, S(159060), 1, 19685039.37, 0},
    {2253, "NAD83 / Michigan South (ft)", LCC2, D_NAD83, E_GRS80, UFT, 41.5, S(-303720), S(157200), 42.1, 1, 13123359.58, 0},
    {2254, "NAD83 / Mississippi East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 29.5, S(-319800), 0, 0, 0.99995, 984250, 0},
    {2255, "NAD83 / Mississippi West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 29.5, S(-325200), 0, 0, 0.99995, 2296583.333, 0},
    {2256, "NAD83 / Montana (ft)", LCC2, D_NAD83, E_GRS80, UFT, 44.25, -109.5, 49, 45, 1, 1968503.937, 0},
    {2257, "NAD83 / New Mexico East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 31, S(-375600), 0, 0, 0.999909091, 541337.5, 0},
    {2258, "NAD83 / New Mexico Central (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 31, -106.25, 0, 0, 0.9999, 1640416.667, 0},
    {2259, "NAD83 / New Mexico West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 31, S(-388200), 0, 0, 0.999916667, 2723091.667, 0},
    {2260, "NAD83 / New York East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(139800), -74.5, 0, 0, 0.9999, 492125, 0},
    {2261, "NAD83 / New York Central (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 40, S(-275700), 0, 0, 0.9999375, 820208.333, 0},
    {2262, "NAD83 / New York West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 40, S(-282900), 0, 0, 0.9999375, 1148291.667, 0},
    {2263, "NAD83 / New York Long Island (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(144600), -74, S(147720), S(146400), 1, 984250, 0},
    {2264, "NAD83 / North Carolina (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 33.75, -79, S(130200), S(123600), 1, 2000000, 0},
    {2265, "NAD83 / North Dakota North (ft)", LCC2, D_NAD83, E_GRS80, UFT, 47, -100.5, S(175440), S(170760), 1, 1968503.937, 0},
    {2266, "NAD83 / North Dakota South (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(164400), -100.5, S(170940), S(166260), 1, 1968503.937, 0},
    {2267, "NAD83 / Oklahoma North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 35, -98, S(132360), S(128040), 1, 1968500, 0},
    {2268, "NAD83 / Oklahoma South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(120000), -98, S(126840), S(122160), 1, 1968500, 0},
    {2269, "NAD83 / Oregon North (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(157200), -120.5, 46, S(159600), 1, 8202099.738, 0},
    {2270, "NAD83 / Oregon South (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(150000), -120.5, 44, S(152400), 1, 4921259.843, 0},
    {2271, "NAD83 / Pennsylvania North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(144600), -77.75, 41.95, S(147180), 1, 1968500, 0},
    {2272, "NAD83 / Pennsylvania South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(141600), -77.75, S(147480), S(143760), 1, 1968500, 0},
    {2273, "NAD83 / South Carolina (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(114600), -81, S(125400), 32.5, 1, 2000000, 0},
    {2274, "NAD83 / Tennessee (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(123600), -86, S(131100), 35.25, 1, 1968500, 0},
    {2275, "NAD83 / Texas North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 34, -101.5, S(130260), 34.65, 1, 656166.667, 3280833.333},
    {2276, "NAD83 / Texas North Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(114000), -98.5, S(122280), S(115680), 1, 1968500, 6561666.667},
    {2277, "NAD83 / Texas Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(106800), S(-361200), S(114780), S(108420), 1, 2296583.333, 9842500},
    {2278, "NAD83 / Texas South Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(100200), -99, S(109020), S(102180), 1, 1968500, 13123333.333},
    {2279, "NAD83 / Texas South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(92400), -98.5, S(100200), S(94200), 1, 984250, 16404166.667},
    {2280, "NAD83 / Utah North (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(145200), -111.5, S(150420), S(146580), 1, 1640419.948, 3280839.895},
    {2281, "NAD83 / Utah Central (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(138000), -111.5, 40.65, S(140460), 1, 1640419.948, 6561679.79},
    {2282, "NAD83 / Utah South (ft)", LCC2, D_NAD83, E_GRS80, UFT, S(132000), -111.5, 38.35, S(133980), 1, 1640419.948, 9842519.685},
    {2283, "NAD83 / Virginia North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(135600), -78.5, 39.2, S(136920), 1, 11482916.667, 6561666.667},
    {2284, "NAD83 / Virginia South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(130800), -78.5, S(136680), S(132360), 1, 11482916.667, 3280833.333},
    {2285, "NAD83 / Washington North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 47, S(-435000), S(175440), 47.5, 1, 1640416.667, 0},
    {2286, "NAD83 / Washington South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(163200), -120.5, S(170400), S(165000), 1, 1640416.667, 0},
    {2287, "NAD83 / Wisconsin North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(162600), -90, S(168360), S(164040), 1, 1968500, 0},
    {2288, "NAD83 / Wisconsin Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(157800), -90, 45.5, 44.25, 1, 1968500, 0},
    {2289, "NAD83 / Wisconsin South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 42, -90, S(158640), S(153840), 1, 1968500, 0},
    {2309, "WGS 84 / TM 116 SE", TM, D_WGS84, E_WGS84, UM, 0, 116, 0, 0, 0.9996, 500000, 10000000},
    {2310, "WGS 84 / TM 132 SE", TM, D_WGS84, E_WGS84, UM, 0, 132, 0, 0, 0.9996, 500000, 10000000},
    {2311, "WGS 84 / TM 6 NE", TM, D_WGS84, E_WGS84, UM, 0, 6, 0, 0, 0.9996, 500000, 0},
    {2759, "NAD83(HARN) / Alabama East", TM, D_NAD83_HARN, E_GRS80, UM, 30.5, S(-309000), 0, 0, 0.99996, 200000, 0},
    {2760, "NAD83(HARN) / Alabama West", TM, D_NAD83_HARN, E_GRS80, UM, 30, -87.5, 0, 0, 0.999933333, 600000, 0},
    {2761, "NAD83(HARN) / Arizona East", TM, D_NAD83_HARN, E_GRS80, UM, 31, S(-396600), 0, 0, 0.9999, 213360, 0},
    {2762, "NAD83(HARN) / Arizona Central", TM, D_NAD83_HARN, E_GRS80, UM, 31, S(-402900), 0, 0, 0.9999, 213360, 0},
    {2763, "NAD83(HARN) / Arizona West", TM, D_NAD83_HARN, E_GRS80, UM, 31, -113.75, 0, 0, 0.999933333, 213360, 0},
    {2764, "NAD83(HARN) / Arkansas North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(123600), -92, S(130440), S(125760), 1, 400000, 0},
    {2765, "NAD83(HARN) / Arkansas South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(117600), -92, S(125160), 33.3, 1, 400000, 400000},
    {2766, "NAD83(HARN) / California zone 1", LCC2, D_NAD83_HARN, E_GRS80, UM, S(141600), -122, S(150000), 40, 1, 2000000, 500000},
    {2767, "NAD83(HARN) / California zone 2", LCC2, D_NAD83_HARN, E_GRS80, UM, S(135600), -122, S(143400), S(138000), 1, 2000000, 500000},
    {2768, "NAD83(HARN) / California zone 3", LCC2, D_NAD83_HARN, E_GRS80, UM, 36.5, -120.5, S(138360), S(133440), 1, 2000000, 500000},
    {2769, "NAD83(HARN) / California zone 4", LCC2, D_NAD83_HARN, E_GRS80, UM, S(127200), -119, 37.25, 36, 1, 2000000, 500000},
    {2770, "NAD83(HARN) / California zone 5", LCC2, D_NAD83_HARN, E_GRS80, UM, 33.5, -118, S(127680), S(122520), 1, 2000000, 500000},
    {2771, "NAD83(HARN) / California zone 6", LCC2, D_NAD83_HARN, E_GRS80, UM, S(115800), -116.25, S(121980), S(118020), 1, 2000000, 500000},
    {2772, "NAD83(HARN) / Colorado North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(141600), -105.5, S(146820), S(142980), 1, 914401.8289, 304800.6096},
    {2773, "NAD83(HARN) / Colorado Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(136200), -105.5, 39.75, 38.45, 1, 914401.8289, 304800.6096},
    {2774, "NAD83(HARN) / Colorado South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(132000), -105.5, S(138360), S(134040), 1, 914401.8289, 304800.6096},
    {2775, "NAD83(HARN) / Connecticut", LCC2, D_NAD83_HARN, E_GRS80, UM, S(147000), -72.75, S(150720), 41.2, 1, 304800.6096, 152400.3048},
    {2776, "NAD83(HARN) / Delaware", TM, D_NAD83_HARN, E_GRS80, UM, 38, S(-271500), 0, 0, 0.999995, 200000, 0},
    {2777, "NAD83(HARN) / Florida East", TM, D_NAD83_HARN, E_GRS80, UM, S(87600), -81, 0, 0, 0.999941177, 200000, 0},
    {2778, "NAD83(HARN) / Florida West", TM, D_NAD83_HARN, E_GRS80, UM, S(87600), -82, 0, 0, 0.999941177, 200000, 0},
    {2779, "NAD83(HARN) / Florida North", LCC2, D_NAD83_HARN, E_GRS80, UM, 29, -84.5, 30.75, S(106500), 1, 600000, 0},
    {2780, "NAD83(HARN) / Georgia East", TM, D_NAD83_HARN, E_GRS80, UM, 30, S(-295800), 0, 0, 0.9999, 200000, 0},
    {2781, "NAD83(HARN) / Georgia West", TM, D_NAD83_HARN, E_GRS80, UM, 30, S(-303000), 0, 0, 0.9999, 700000, 0},
    {2782, "NAD83(HARN) / Hawaii zone 1", TM, D_NAD83_HARN, E_GRS80, UM, S(67800), -155.5, 0, 0, 0.999966667, 500000, 0},
    {2783, "NAD83(HARN) / Hawaii zone 2", TM, D_NAD83_HARN, E_GRS80, UM, S(73200), S(-564000), 0, 0, 0.999966667, 500000, 0},
    {2784, "NAD83(HARN) / Hawaii zone 3", TM, D_NAD83_HARN, E_GRS80, UM, S(76200), -158, 0, 0, 0.99999, 500000, 0},
    {2785, "NAD83(HARN) / Hawaii zone 4", TM, D_NAD83_HARN, E_GRS80, UM, S(78600), -159.5, 0, 0, 0.99999, 500000, 0},
    {2786, "NAD83(HARN) / Hawaii zone 5", TM, D_NAD83_HARN, E_GRS80, UM, S(78000), S(-576600), 0, 0, 1, 500000, 0},
    {2787, "NAD83(HARN) / Idaho East", TM, D_NAD83_HARN, E_GRS80, UM, S(150000), S(-403800), 0, 0, 0.999947368, 200000, 0},
    {2788, "NAD83(HARN) / Idaho Central", TM, D_NAD83_HARN, E_GRS80, UM, S(150000), -114, 0, 0, 0.999947368, 500000, 0},
    {2789, "NAD83(HARN) / Idaho West", TM, D_NAD83_HARN, E_GRS80, UM, S(150000), -115.75, 0, 0, 0.999933333, 800000, 0},
    {2790, "NAD83(HARN) / Illinois East", TM, D_NAD83_HARN, E_GRS80, UM, S(132000), S(-318000), 0, 0, 0.999975, 300000, 0},
    {2791, "NAD83(HARN) / Illinois West", TM, D_NAD83_HARN, E_GRS80, UM, S(132000), S(-324600), 0, 0, 0.999941177, 700000, 0},
    {2792, "NAD83(HARN) / Indiana East", TM, D_NAD83_HARN, E_GRS80, UM, 37.5, S(-308400), 0, 0, 0.999966667, 100000, 250000},
    {2793, "NAD83(HARN) / Indiana West", TM, D_NAD83_HARN, E_GRS80, UM, 37.5, S(-313500), 0, 0, 0.999966667, 900000, 250000},
    {2794, "NAD83(HARN) / Iowa North", LCC2, D_NAD83_HARN, E_GRS80, UM, 41.5, -93.5, S(155760), S(151440), 1, 1500000, 1000000},
    {2795, "NAD83(HARN) / Iowa South", LCC2, D_NAD83_HARN, E_GRS80, UM, 40, -93.5, S(150420), S(146220), 1, 500000, 0},
    {2796, "NAD83(HARN) / Kansas North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(138000), -98, S(143220), S(139380), 1, 400000, 0},
    {2797, "NAD83(HARN) / Kansas South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(132000), -98.5, S(138840), S(134160), 1, 400000, 400000},
    {2798, "NAD83(HARN) / Kentucky North", LCC2, D_NAD83_HARN, E_GRS80, UM, 37.5, -84.25, S(136680), S(140280), 1, 500000, 0},
    {2799, "NAD83(HARN) / Kentucky South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(130800), -85.75, S(136560), S(132240), 1, 500000, 500000},
    {2800, "NAD83(HARN) / Louisiana North", LCC2, D_NAD83_HARN, E_GRS80, UM, 30.5, -92.5, S(117600), S(112200), 1, 1000000, 0},
    {2801, "NAD83(HARN) / Louisiana South", LCC2, D_NAD83_HARN, E_GRS80, UM, 28.5, S(-328800), 30.7, 29.3, 1, 1000000, 0},
    {2802, "NAD83(HARN) / Maine East", TM, D_NAD83_HARN, E_GRS80, UM, S(157200), -68.5, 0, 0, 0.9999, 300000, 0},
    {2803, "NAD83(HARN) / Maine West", TM, D_NAD83_HARN, E_GRS80, UM, S(154200), S(-252600), 0, 0, 0.999966667, 900000, 0},
    {2804, "NAD83(HARN) / Maryland", LCC2, D_NAD83_HARN, E_GRS80, UM, S(135600), -77, 39.45, 38.3, 1, 400000, 0},
    {2805, "NAD83(HARN) / Massachusetts Mainland", LCC2, D_NAD83_HARN, E_GRS80, UM, 41, -71.5, S(153660), S(150180), 1, 200000, 750000},
    {2806, "NAD83(HARN) / Massachusetts Island", LCC2, D_NAD83_HARN, E_GRS80, UM, 41, -70.5, S(149340), S(148620), 1, 500000, 0},
    {2807, "NAD83(HARN) / Michigan North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(161220), -87, S(169500), S(163740), 1, 8000000, 0},
    {2808, "NAD83(HARN) / Michigan Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(155940), S(-303720), 45.7, S(159060), 1, 6000000, 0},
    {2809, "NAD83(HARN) / Michigan South", LCC2, D_NAD83_HARN, E_GRS80, UM, 41.5, S(-303720), S(157200), 42.1, 1, 4000000, 0},
    {2810, "NAD83(HARN) / Minnesota North", LCC2, D_NAD83_HARN, E_GRS80, UM, 46.5, -93.1, S(175080), S(169320), 1, 800000, 100000},
    {2811, "NAD83(HARN) / Minnesota Central", LCC2, D_NAD83_HARN, E_GRS80, UM, 45, -94.25, 47.05, S(164220), 1, 800000, 100000},
    {2812, "NAD83(HARN) / Minnesota South", LCC2, D_NAD83_HARN, E_GRS80, UM, 43, -94, S(162780), S(157620), 1, 800000, 100000},
    {2813, "NAD83(HARN) / Mississippi East", TM, D_NAD83_HARN, E_GRS80, UM, 29.5, S(-319800), 0, 0, 0.99995, 300000, 0},
    {2814, "NAD83(HARN) / Mississippi West", TM, D_NAD83_HARN, E_GRS80, UM, 29.5, S(-325200), 0, 0, 0.99995, 700000, 0},
    {2815, "NAD83(HARN) / Missouri East", TM, D_NAD83_HARN, E_GRS80, UM, S(129000), -90.5, 0, 0, 0.999933333, 250000, 0},
    {2816, "NAD83(HARN) / Missouri Central", TM, D_NAD83_HARN, E_GRS80, UM, S(129000), -92.5, 0, 0, 0.999933333, 500000, 0},
    {2817, "NAD83(HARN) / Missouri West", TM, D_NAD83_HARN, E_GRS80, UM, S(130200), -94.5, 0, 0, 0.999941177, 850000, 0},
    {2818, "NAD83(HARN) / Montana", LCC2, D_NAD83_HARN, E_GRS80, UM, 44.25, -109.5, 49, 45, 1, 600000, 0},
    {2819, "NAD83(HARN) / Nebraska", LCC2, D_NAD83_HARN, E_GRS80, UM, S(143400), -100, 43, 40, 1, 500000, 0},
    {2820, "NAD83(HARN) / Nevada East", TM, D_NAD83_HARN, E_GRS80, UM, 34.75, S(-416100), 0, 0, 0.9999, 200000, 8000000},
    {2821, "NAD83(HARN) / Nevada Central", TM, D_NAD83_HARN, E_GRS80, UM, 34.75, S(-420000), 0, 0, 0.9999, 500000, 6000000},
    {2822, "NAD83(HARN) / Nevada West", TM, D_NAD83_HARN, E_GRS80, UM, 34.75, S(-426900), 0, 0, 0.9999, 800000, 4000000},
    {2823, "NAD83(HARN) / New Hampshire", TM, D_NAD83_HARN, E_GRS80, UM, 42.5, S(-258000), 0, 0, 0.999966667, 300000, 0},
    {2824, "NAD83(HARN) / New Jersey", TM, D_NAD83_HARN, E_GRS80, UM, S(139800), -74.5, 0, 0, 0.9999, 150000, 0},
    {2825, "NAD83(HARN) / New Mexico East", TM, D_NAD83_HARN, E_GRS80, UM, 31, S(-375600), 0, 0, 0.999909091, 165000, 0},
    {2826, "NAD83(HARN) / New Mexico Central", TM, D_NAD83_HARN, E_GRS80, UM, 31, -106.25, 0, 0, 0.9999, 500000, 0},
    {2827, "NAD83(HARN) / New Mexico West", TM, D_NAD83_HARN, E_GRS80, UM, 31, S(-388200), 0, 0, 0.999916667, 830000, 0},
    {2828, "NAD83(HARN) / New York East", TM, D_NAD83_HARN, E_GRS80, UM, S(139800), -74.5, 0, 0, 0.9999, 150000, 0},
    {2829, "NAD83(HARN) / New York Central", TM, D_NAD83_HARN, E_GRS80, UM, 40, S(-275700), 0, 0, 0.9999375, 250000, 0},
    {2830, "NAD83(HARN) / New York West", TM, D_NAD83_HARN, E_GRS80, UM, 40, S(-282900), 0, 0, 0.9999375, 350000, 0},
    {2831, "NAD83(HARN) / New York Long Island", LCC2, D_NAD83_HARN, E_GRS80, UM, S(144600), -74, S(147720), S(146400), 1, 300000, 0},
    {2832, "NAD83(HARN) / North Dakota North", LCC2, D_NAD83_HARN, E_GRS80, UM, 47, -100.5, S(175440), S(170760), 1, 600000, 0},
    {2833, "NAD83(HARN) / North Dakota South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(164400), -100.5, S(170940), S(166260), 1, 600000, 0},
    {2834, "NAD83(HARN) / Ohio North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(142800), -82.5, 41.7, S(145560), 1, 600000, 0},
    {2835, "NAD83(HARN) / Ohio South", LCC2, D_NAD83_HARN, E_GRS80, UM, 38, -82.5, S(144120), S(139440), 1, 600000, 0},
    {2836, "NAD83(HARN) / Oklahoma North", LCC2, D_NAD83_HARN, E_GRS80, UM, 35, -98, S(132360), S(128040), 1, 600000, 0},
    {2837, "NAD83(HARN) / Oklahoma South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(120000), -98, S(126840), S(122160), 1, 600000, 0},
    {2838, "NAD83(HARN) / Oregon North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(157200), -120.5, 46, S(159600), 1, 2500000, 0},
    {2839, "NAD83(HARN) / Oregon South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(150000), -120.5, 44, S(152400), 1, 1500000, 0},
    {2840, "NAD83(HARN) / Rhode Island", TM, D_NAD83_HARN, E_GRS80, UM, S(147900), -71.5, 0, 0, 0.99999375, 100000, 0},
    {2841, "NAD83(HARN) / South Dakota North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(157800), -100, S(164460), S(159900), 1, 600000, 0},
    {2842, "NAD83(HARN) / South Dakota South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(152400), S(-361200), 44.4, S(154200), 1, 600000, 0},
    {2843, "NAD83(HARN) / Tennessee", LCC2, D_NAD83_HARN, E_GRS80, UM, S(123600), -86, S(131100), 35.25, 1, 600000, 0},
    {2844, "NAD83(HARN) / Texas North", LCC2, D_NAD83_HARN, E_GRS80, UM, 34, -101.5, S(130260), 34.65, 1, 200000, 1000000},
    {2845, "NAD83(HARN) / Texas North Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(114000), -98.5, S(122280), S(115680), 1, 600000, 2000000},
    {2846, "NAD83(HARN) / Texas Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(106800), S(-361200), S(114780), S(108420), 1, 700000, 3000000},
    {2847, "NAD83(HARN) / Texas South Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(100200), -99, S(109020), S(102180), 1, 600000, 4000000},
    {2848, "NAD83(HARN) / Texas South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(92400), -98.5, S(100200), S(94200), 1, 300000, 5000000},
    {2849, "NAD83(HARN) / Utah North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(145200), -111.5, S(150420), S(146580), 1, 500000, 1000000},
    {2850, "NAD83(HARN) / Utah Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(138000), -111.5, 40.65, S(140460), 1, 500000, 2000000},
    {2851, "NAD83(HARN) / Utah South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(132000), -111.5, 38.35, S(133980), 1, 500000, 3000000},
    {2852, "NAD83(HARN) / Vermont", TM, D_NAD83_HARN, E_GRS80, UM, 42.5, -72.5, 0, 0, 0.999964286, 500000, 0},
    {2853, "NAD83(HARN) / Virginia North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(135600), -78.5, 39.2, S(136920), 1, 3500000, 2000000},
    {2854, "NAD83(HARN) / Virginia South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(130800), -78.5, S(136680), S(132360), 1, 3500000, 1000000},
    {2855, "NAD83(HARN) / Washington North", LCC2, D_NAD83_HARN, E_GRS80, UM, 47, S(-435000), S(175440), 47.5, 1, 500000, 0},
    {2856, "NAD83(HARN) / Washington South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(163200), -120.5, S(170400), S(165000), 1, 500000, 0},
    {2857, "NAD83(HARN) / West Virginia North", LCC2, D_NAD83_HARN, E_GRS80, UM, 38.5, -79.5, 40.25, 39, 1, 600000, 0},
    {2858, "NAD83(HARN) / West Virginia South", LCC2, D_NAD83_HARN, E_GRS80, UM, 37, -81, S(139980), S(134940), 1, 600000, 0},
    {2859, "NAD83(HARN) / Wisconsin North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(162600), -90, S(168360), S(164040), 1, 600000, 0},
    {2860, "NAD83(HARN) / Wisconsin Central", LCC2, D_NAD83_HARN, E_GRS80, UM, S(157800), -90, 45.5, 44.25, 1, 600000, 0},
    {2861, "NAD83(HARN) / Wisconsin South", LCC2, D_NAD83_HARN, E_GRS80, UM, 42, -90, S(158640), S(153840), 1, 600000, 0},
    {2862, "NAD83(HARN) / Wyoming East", TM, D_NAD83_HARN, E_GRS80, UM, 40.5, S(-378600), 0, 0, 0.9999375, 200000, 0},
    {2863, "NAD83(HARN) / Wyoming East Central", TM, D_NAD83_HARN, E_GRS80, UM, 40.5, S(-386400), 0, 0, 0.9999375, 400000, 100000},
    {2864, "NAD83(HARN) / Wyoming West Central", TM, D_NAD83_HARN, E_GRS80, UM, 40.5, -108.75, 0, 0, 0.9999375, 600000, 0},
    {2865, "NAD83(HARN) / Wyoming West", TM, D_NAD83_HARN, E_GRS80, UM, 40.5, S(-396300), 0, 0, 0.9999375, 800000, 100000},
    {2866, "NAD83(HARN) / Puerto Rico and Virgin Is.", LCC2, D_NAD83_HARN, E_GRS80, UM, S(64200), S(-239160), S(66360), S(64920), 1, 200000, 200000},
    {2867, "NAD83(HARN) / Arizona East (ft)", TM, D_NAD83_HARN, E_GRS80, UFT, 31, S(-396600), 0, 0, 0.9999, 700000, 0},
    {2868, "NAD83(HARN) / Arizona Central (ft)", TM, D_NAD83_HARN, E_GRS80, UFT, 31, S(-402900), 0, 0, 0.9999, 700000, 0},
    {2869, "NAD83(HARN) / Arizona West (ft)", TM, D_NAD83_HARN, E_GRS80, UFT, 31, -113.75, 0, 0, 0.999933333, 700000, 0},
    {2870, "NAD83(HARN) / California zone 1 (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(141600), -122, S(150000), 40, 1, 6561666.667, 1640416.667},
    {2871, "NAD83(HARN) / California zone 2 (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(135600), -122, S(143400), S(138000), 1, 6561666.667, 1640416.667},
    {2872, "NAD83(HARN) / California zone 3 (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 36.5, -120.5, S(138360), S(133440), 1, 6561666.667, 1640416.667},
    {2873, "NAD83(HARN) / California zone 4 (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(127200), -119, 37.25, 36, 1, 6561666.667, 1640416.667},
    {2874, "NAD83(HARN) / California zone 5 (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 33.5, -118, S(127680), S(122520), 1, 6561666.667, 1640416.667},
    {2875, "NAD83(HARN) / California zone 6 (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(115800), -116.25, S(121980), S(118020), 1, 6561666.667, 1640416.667},
    {2876, "NAD83(HARN) / Colorado North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(141600), -105.5, S(146820), S(142980), 1, 3000000, 1000000},
    {2877, "NAD83(HARN) / Colorado Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(136200), -105.5, 39.75, 38.45, 1, 3000000, 1000000},
    {2878, "NAD83(HARN) / Colorado South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(132000), -105.5, S(138360), S(134040), 1, 3000000, 1000000},
    {2879, "NAD83(HARN) / Connecticut (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(147000), -72.75, S(150720), 41.2, 1, 1000000, 500000},
    {2880, "NAD83(HARN) / Delaware (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 38, S(-271500), 0, 0, 0.999995, 656166.667, 0},
    {2881, "NAD83(HARN) / Florida East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(87600), -81, 0, 0, 0.999941177, 656166.667, 0},
    {2882, "NAD83(HARN) / Florida West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(87600), -82, 0, 0, 0.999941177, 656166.667, 0},
    {2883, "NAD83(HARN) / Florida North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 29, -84.5, 30.75, S(106500), 1, 1968500, 0},
    {2884, "NAD83(HARN) / Georgia East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 30, S(-295800), 0, 0, 0.9999, 656166.667, 0},
    {2885, "NAD83(HARN) / Georgia West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 30, S(-303000), 0, 0, 0.9999, 2296583.333, 0},
    {2886, "NAD83(HARN) / Idaho East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(150000), S(-403800), 0, 0, 0.999947368, 656166.667, 0},
    {2887, "NAD83(HARN) / Idaho Central (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(150000), -114, 0, 0, 0.999947368, 1640416.667, 0},
    {2888, "NAD83(HARN) / Idaho West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(150000), -115.75, 0, 0, 0.999933333, 2624666.667, 0},
    {2891, "NAD83(HARN) / Kentucky North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 37.5, -84.25, S(136680), S(140280), 1, 1640416.667, 0},
    {2892, "NAD83(HARN) / Kentucky South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(130800), -85.75, S(136560), S(132240), 1, 1640416.667, 1640416.667},
    {2893, "NAD83(HARN) / Maryland (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(135600), -77, 39.45, 38.3, 1, 1312333.333, 0},
    {2894, "NAD83(HARN) / Massachusetts Mainland (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 41, -71.5, S(153660), S(150180), 1, 656166.667, 2460625},
    {2895, "NAD83(HARN) / Massachusetts Island (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 41, -70.5, S(149340), S(148620), 1, 1640416.667, 0},
    {2896, "NAD83(HARN) / Michigan North (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(161220), -87, S(169500), S(163740), 1, 26246719.16, 0},
    {2897, "NAD83(HARN) / Michigan Central (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(155940), S(-303720), 45.7, S(159060), 1, 19685039.37, 0},
    {2898, "NAD83(HARN) / Michigan South (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, 41.5, S(-303720), S(157200), 42.1, 1, 13123359.58, 0},
    {2899, "NAD83(HARN) / Mississippi East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 29.5, S(-319800), 0, 0, 0.99995, 984250, 0},
    {2900, "NAD83(HARN) / Mississippi West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 29.5, S(-325200), 0, 0, 0.99995, 2296583.333, 0},
    {2901, "NAD83(HARN) / Montana (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, 44.25, -109.5, 49, 45, 1, 1968503.937, 0},
    {2902, "NAD83(HARN) / New Mexico East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 31, S(-375600), 0, 0, 0.999909091, 541337.5, 0},
    {2903, "NAD83(HARN) / New Mexico Central (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 31, -106.25, 0, 0, 0.9999, 1640416.667, 0},
    {2904, "NAD83(HARN) / New Mexico West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 31, S(-388200), 0, 0, 0.999916667, 2723091.667, 0},
    {2905, "NAD83(HARN) / New York East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(139800), -74.5, 0, 0, 0.9999, 492125, 0},
    {2906, "NAD83(HARN) / New York Central (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 40, S(-275700), 0, 0, 0.9999375, 820208.333, 0},
    {2907, "NAD83(HARN) / New York West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 40, S(-282900), 0, 0, 0.9999375, 1148291.667, 0},
    {2908, "NAD83(HARN) / New York Long Island (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(144600), -74, S(147720), S(146400), 1, 984250, 0},
    {2909, "NAD83(HARN) / North Dakota North (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, 47, -100.5, S(175440), S(170760), 1, 1968503.937, 0},
    {2910, "NAD83(HARN) / North Dakota South (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(164400), -100.5, S(170940), S(166260), 1, 1968503.937, 0},
    {2911, "NAD83(HARN) / Oklahoma North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 35, -98, S(132360), S(128040), 1, 1968500, 0},
    {2912, "NAD83(HARN) / Oklahoma South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(120000), -98, S(126840), S(122160), 1, 1968500, 0},
    {2913, "NAD83(HARN) / Oregon North (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(157200), -120.5, 46, S(159600), 1, 8202099.738, 0},
    {2914, "NAD83(HARN) / Oregon South (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(150000), -120.5, 44, S(152400), 1, 4921259.843, 0},
    {2915, "NAD83(HARN) / Tennessee (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(123600), -86, S(131100), 35.25, 1, 1968500, 0},
    {2916, "NAD83(HARN) / Texas North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 34, -101.5, S(130260), 34.65, 1, 656166.667, 3280833.333},
    {2917, "NAD83(HARN) / Texas North Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(114000), -98.5, S(122280), S(115680), 1, 1968500, 6561666.667},
    {2918, "NAD83(HARN) / Texas Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(106800), S(-361200), S(114780), S(108420), 1, 2296583.333, 9842500},
    {2919, "NAD83(HARN) / Texas South Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(100200), -99, S(109020), S(102180), 1, 1968500, 13123333.333},
    {2920, "NAD83(HARN) / Texas South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(92400), -98.5, S(100200), S(94200), 1, 984250, 16404166.667},
    {2921, "NAD83(HARN) / Utah North (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(145200), -111.5, S(150420), S(146580), 1, 1640419.948, 3280839.895},
    {2922, "NAD83(HARN) / Utah Central (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(138000), -111.5, 40.65, S(140460), 1, 1640419.948, 6561679.79},
    {2923, "NAD83(HARN) / Utah South (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(132000), -111.5, 38.35, S(133980), 1, 1640419.948, 9842519.685},
    {2924, "NAD83(HARN) / Virginia North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(135600), -78.5, 39.2, S(136920), 1, 11482916.667, 6561666.667},
    {2925, "NAD83(HARN) / Virginia South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(130800), -78.5, S(136680), S(132360), 1, 11482916.667, 3280833.333},
    {2926, "NAD83(HARN) / Washington North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 47, S(-435000), S(175440), 47.5, 1, 1640416.667, 0},
    {2927, "NAD83(HARN) / Washington South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(163200), -120.5, S(170400), S(165000), 1, 1640416.667, 0},
    {2928, "NAD83(HARN) / Wisconsin North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(162600), -90, S(168360), S(164040), 1, 1968500, 0},
    {2929, "NAD83(HARN) / Wisconsin Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(157800), -90, 45.5, 44.25, 1, 1968500, 0},
    {2930, "NAD83(HARN) / Wisconsin South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 42, -90, S(158640), S(153840), 1, 1968500, 0},
    {2945, "NAD83(CSRS) / MTM zone 3", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -58.5, 0, 0, 0.9999, 304800, 0},
    {2946, "NAD83(CSRS) / MTM zone 4", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -61.5, 0, 0, 0.9999, 304800, 0},
    {2947, "NAD83(CSRS) / MTM zone 5", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -64.5, 0, 0, 0.9999, 304800, 0},
    {2948, "NAD83(CSRS) / MTM zone 6", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -67.5, 0, 0, 0.9999, 304800, 0},
    {2949, "NAD83(CSRS) / MTM zone 7", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -70.5, 0, 0, 0.9999, 304800, 0},
    {2950, "NAD83(CSRS) / MTM zone 8", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -73.5, 0, 0, 0.9999, 304800, 0},
    {2951, "NAD83(CSRS) / MTM zone 9", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -76.5, 0, 0, 0.9999, 304800, 0},
    {2952, "NAD83(CSRS) / MTM zone 10", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -79.5, 0, 0, 0.9999, 304800, 0},
    {2955, "NAD83(CSRS) / UTM zone 11N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -117, 0, 0, 0.9996, 500000, 0},
    {2956, "NAD83(CSRS) / UTM zone 12N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -111, 0, 0, 0.9996, 500000, 0},
    {2957, "NAD83(CSRS) / UTM zone 13N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -105, 0, 0, 0.9996, 500000, 0},
    {2958, "NAD83(CSRS) / UTM zone 17N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -81, 0, 0, 0.9996, 500000, 0},
    {2959, "NAD83(CSRS) / UTM zone 18N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -75, 0, 0, 0.9996, 500000, 0},
    {2960, "NAD83(CSRS) / UTM zone 19N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -69, 0, 0, 0.9996, 500000, 0},
    {2961, "NAD83(CSRS) / UTM zone 20N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -63, 0, 0, 0.9996, 500000, 0},
    {2962, "NAD83(CSRS) / UTM zone 21N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -57, 0, 0, 0.9996, 500000, 0},
    {2965, "NAD83 / Indiana East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 37.5, S(-308400), 0, 0, 0.999966667, 328083.333, 820208.333},
    {2966, "NAD83 / Indiana West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 37.5, S(-313500), 0, 0, 0.999966667, 2952750, 820208.333},
    {2967, "NAD83(HARN) / Indiana East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 37.5, S(-308400), 0, 0, 0.999966667, 328083.333, 820208.333},
    {2968, "NAD83(HARN) / Indiana West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 37.5, S(-313500), 0, 0, 0.999966667, 2952750, 820208.333},
    {2991, "NAD83 / Oregon LCC (m)", LCC2, D_NAD83, E_GRS80, UM, 41.75, -120.5, 43, 45.5, 1, 400000, 0},
    {2992, "NAD83 / Oregon GIC Lambert (ft)", LCC2, D_NAD83, E_GRS80, UFT, 41.75, -120.5, 43, 45.5, 1, 1312335.958, 0},
    {2993, "NAD83(HARN) / Oregon LCC (m)", LCC2, D_NAD83_HARN, E_GRS80, UM, 41.75, -120.5, 43, 45.5, 1, 400000, 0},
    {2994, "NAD83(HARN) / Oregon GIC Lambert (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, 41.75, -120.5, 43, 45.5, 1, 1312335.958, 0},
    {3005, "NAD83 / BC Albers", AEA, D_NAD83, E_GRS80, UM, 45, -126, 50, 58.5, 1, 1000000, 0},
    {3006, "SWEREF99 TM", TM, D_SWEREF99, E_GRS80, UM, 0, 15, 0, 0, 0.9996, 500000, 0},
    {3007, "SWEREF99 12 00", TM, D_SWEREF99, E_GRS80, UM, 0, 12, 0, 0, 1, 150000, 0},
    {3008, "SWEREF99 13 30", TM, D_SWEREF99, E_GRS80, UM, 0, 13.5, 0, 0, 1, 150000, 0},
    {3009, "SWEREF99 15 00", TM, D_SWEREF99, E_GRS80, UM, 0, 15, 0, 0, 1, 150000, 0},
    {3010, "SWEREF99 16 30", TM, D_SWEREF99, E_GRS80, UM, 0, 16.5, 0, 0, 1, 150000, 0},
    {3011, "SWEREF99 18 00", TM, D_SWEREF99, E_GRS80, UM, 0, 18, 0, 0, 1, 150000, 0},
    {3012, "SWEREF99 14 15", TM, D_SWEREF99, E_GRS80, UM, 0, 14.25, 0, 0, 1, 150000, 0},
    {3013, "SWEREF99 15 45", TM, D_SWEREF99, E_GRS80, UM, 0, 15.75, 0, 0, 1, 150000, 0},
    {3014, "SWEREF99 17 15", TM, D_SWEREF99, E_GRS80, UM, 0, 17.25, 0, 0, 1, 150000, 0},
    {3015, "SWEREF99 18 45", TM, D_SWEREF99, E_GRS80, UM, 0, 18.75, 0, 0, 1, 150000, 0},
    {3016, "SWEREF99 20 15", TM, D_SWEREF99, E_GRS80, UM, 0, 20.25, 0, 0, 1, 150000, 0},
    {3017, "SWEREF99 21 45", TM, D_SWEREF99, E_GRS80, UM, 0, 21.75, 0, 0, 1, 150000, 0},
    {3018, "SWEREF99 23 15", TM, D_SWEREF99, E_GRS80, UM, 0, 23.25, 0, 0, 1, 150000, 0},
    {3031, "WGS 84 / Antarctic Polar Stereographic", PS, D_WGS84, E_WGS84, UM, -90, 0, -71, 0, 1, 0, 0},
    {3032, "WGS 84 / Australian Antarctic Polar Stereographic", PS, D_WGS84, E_WGS84, UM, -90, 70, -71, 0, 1, 6000000, 6000000},
    {3033, "WGS 84 / Australian Antarctic Lambert", LCC2, D_WGS84, E_WGS84, UM, -50, 70, -68.5, -74.5, 1, 6000000, 6000000},
    {3034, "ETRS89-extended / LCC Europe", LCC2, D_ETRS89, E_GRS80, UM, 52, 10, 35, 65, 1, 4000000, 2800000},
    {3040, "ETRS89 / UTM zone 28N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, -15, 0, 0, 0.9996, 500000, 0},
    {3041, "ETRS89 / UTM zone 29N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, -9, 0, 0, 0.9996, 500000, 0},
    {3042, "ETRS89 / UTM zone 30N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, -3, 0, 0, 0.9996, 500000, 0},
    {3043, "ETRS89 / UTM zone 31N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 3, 0, 0, 0.9996, 500000, 0},
    {3044, "ETRS89 / UTM zone 32N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 9, 0, 0, 0.9996, 500000, 0},
    {3045, "ETRS89 / UTM zone 33N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 15, 0, 0, 0.9996, 500000, 0},
    {3046, "ETRS89 / UTM zone 34N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 21, 0, 0, 0.9996, 500000, 0},
    {3047, "ETRS89 / UTM zone 35N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 27, 0, 0, 0.9996, 500000, 0},
    {3048, "ETRS89 / UTM zone 36N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 33, 0, 0, 0.9996, 500000, 0},
    {3049, "ETRS89 / UTM zone 37N (N-E)", TM, D_ETRS89, E_GRS80, UM, 0, 39, 0, 0, 0.9996, 500000, 0},
    {3070, "NAD83 / Wisconsin Transverse Mercator", TM, D_NAD83, E_GRS80, UM, 0, -90, 0, 0, 0.9996, 520000, -4480000},
    {3071, "NAD83(HARN) / Wisconsin Transverse Mercator", TM, D_NAD83_HARN, E_GRS80, UM, 0, -90, 0, 0, 0.9996, 520000, -4480000},
    {3072, "NAD83 / Maine CS2000 East", TM, D_NAD83, E_GRS80, UM, S(157800), -67.875, 0, 0, 0.99998, 700000, 0},
    {3074, "NAD83 / Maine CS2000 West", TM, D_NAD83, E_GRS80, UM, S(154200), -70.375, 0, 0, 0.99998, 300000, 0},
    {3075, "NAD83(HARN) / Maine CS2000 East", TM, D_NAD83_HARN, E_GRS80, UM, S(157800), -67.875, 0, 0, 0.99998, 700000, 0},
    {3077, "NAD83(HARN) / Maine CS2000 West", TM, D_NAD83_HARN, E_GRS80, UM, S(154200), -70.375, 0, 0, 0.99998, 300000, 0},
    {3081, "NAD83 / Texas State Mapping System", LCC2, D_NAD83, E_GRS80, UM, S(112200), -100, S(98700), S(125700), 1, 1000000, 1000000},
    {3082, "NAD83 / Texas Centric Lambert Conformal", LCC2, D_NAD83, E_GRS80, UM, 18, -100, 27.5, 35, 1, 1500000, 5000000},
    {3083, "NAD83 / Texas Centric Albers Equal Area", AEA, D_NAD83, E_GRS80, UM, 18, -100, 27.5, 35, 1, 1500000, 6000000},
    {3084, "NAD83(HARN) / Texas Centric Lambert Conformal", LCC2, D_NAD83_HARN, E_GRS80, UM, 18, -100, 27.5, 35, 1, 1500000, 5000000},
    {3085, "NAD83(HARN) / Texas Centric Albers Equal Area", AEA, D_NAD83_HARN, E_GRS80, UM, 18, -100, 27.5, 35, 1, 1500000, 6000000},
    {3086, "NAD83 / Florida GDL Albers", AEA, D_NAD83, E_GRS80, UM, 24, -84, 24, 31.5, 1, 400000, 0},
    {3087, "NAD83(HARN) / Florida GDL Albers", AEA, D_NAD83_HARN, E_GRS80, UM, 24, -84, 24, 31.5, 1, 400000, 0},
    {3088, "NAD83 / Kentucky Single Zone", LCC2, D_NAD83, E_GRS80, UM, S(130800), -85.75, S(133500), S(139200), 1, 1500000, 1000000},
    {3089, "NAD83 / Kentucky Single Zone (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(130800), -85.75, S(133500), S(139200), 1, 4921250, 3280833.333},
    {3090, "NAD83(HARN) / Kentucky Single Zone", LCC2, D_NAD83_HARN, E_GRS80, UM, S(130800), -85.75, S(133500), S(139200), 1, 1500000, 1000000},
    {3091, "NAD83(HARN) / Kentucky Single Zone (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(130800), -85.75, S(133500), S(139200), 1, 4921250, 3280833.333},
    {3107, "GDA94 / SA Lambert", LCC2, D_GDA94, E_GRS80, UM, -32, 135, -28, -36, 1, 1000000, 2000000},
    {3108, "ETRS89 / Guernsey Grid", TM, D_ETRS89, E_GRS80, UM, 49.5, S(-8700), 0, 0, 0.999997, 47000, 50000},
    {3109, "ETRS89 / Jersey Transverse Mercator", TM, D_ETRS89, E_GRS80, UM, 49.225, -2.135, 0, 0, 0.9999999, 40000, 70000},
    {3111, "GDA94 / Vicgrid", LCC2, D_GDA94, E_GRS80, UM, -37, 145, -36, -38, 1, 2500000, 2500000},
    {3112, "GDA94 / Geoscience Australia Lambert", LCC2, D_GDA94, E_GRS80, UM, 0, 134, -18, -36, 1, 0, 0},
    {3113, "GDA94 / BCSG02", TM, D_GDA94, E_GRS80, UM, -28, 153, 0, 0, 0.99999, 50000, 100000},
    {3152, "ST74", TM, D_SWEREF99, E_GRS80, UM, 0, 18.05779, 0, 0, 0.99999425, 100178.1808, -6500614.7836},
    {3153, "NAD83(CSRS) / BC Albers", AEA, D_NAD83_CSRS, E_GRS80, UM, 45, -126, 50, 58.5, 1, 1000000, 0},
    {3154, "NAD83(CSRS) / UTM zone 7N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -141, 0, 0, 0.9996, 500000, 0},
    {3155, "NAD83(CSRS) / UTM zone 8N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -135, 0, 0, 0.9996, 500000, 0},
    {3156, "NAD83(CSRS) / UTM zone 9N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -129, 0, 0, 0.9996, 500000, 0},
    {3157, "NAD83(CSRS) / UTM zone 10N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -123, 0, 0, 0.9996, 500000, 0},
    {3158, "NAD83(CSRS) / UTM zone 14N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -99, 0, 0, 0.9996, 500000, 0},
    {3159, "NAD83(CSRS) / UTM zone 15N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -93, 0, 0, 0.9996, 500000, 0},
    {3160, "NAD83(CSRS) / UTM zone 16N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -87, 0, 0, 0.9996, 500000, 0},
    {3161, "NAD83 / Ontario MNR Lambert", LCC2, D_NAD83, E_GRS80, UM, 0, -85, 44.5, 53.5, 1, 930000, 6430000},
    {3162, "NAD83(CSRS) / Ontario MNR Lambert", LCC2, D_NAD83_CSRS, E_GRS80, UM, 0, -85, 44.5, 53.5, 1, 930000, 6430000},
    {3174, "NAD83 / Great Lakes Albers", AEA, D_NAD83, E_GRS80, UM, 45.568977, -84.455955, 42.122774, 49.01518, 1, 1000000, 1000000},
    {3175, "NAD83 / Great Lakes and St Lawrence Albers", AEA, D_NAD83, E_GRS80, UM, 45.568977, -83.248627, 42.122774, 49.01518, 1, 1000000, 1000000},
    {3294, "WGS 84 / USGS Transantarctic Mountains", LCC2, D_WGS84, E_WGS84, UM, -78, 162, S(-276000), S(-285600), 1, 0, 0},
    {3308, "GDA94 / NSW Lambert", LCC2, D_GDA94, E_GRS80, UM, -33.25, 147, -30.75, -35.75, 1, 9300000, 4500000},
    {3310, "NAD83 / California Albers", AEA, D_NAD83, E_GRS80, UM, 0, -120, 34, 40.5, 1, 0, -4000000},
    {3311, "NAD83(HARN) / California Albers", AEA, D_NAD83_HARN, E_GRS80, UM, 0, -120, 34, 40.5, 1, 0, -4000000},
    {3338, "NAD83 / Alaska Albers", AEA, D_NAD83, E_GRS80, UM, 50, -154, 55, 65, 1, 0, 0},
    {3347, "NAD83 / Statistics Canada Lambert", LCC2, D_NAD83, E_GRS80, UM, 63.390675, S(-330720), 49, 77, 1, 6200000, 3000000},
    {3348, "NAD83(CSRS) / Statistics Canada Lambert", LCC2, D_NAD83_CSRS, E_GRS80, UM, 63.390675, S(-330720), 49, 77, 1, 6200000, 3000000},
    {3358, "NAD83(HARN) / North Carolina", LCC2, D_NAD83_HARN, E_GRS80, UM, 33.75, -79, S(130200), S(123600), 1, 609601.22, 0},
    {3360, "NAD83(HARN) / South Carolina", LCC2, D_NAD83_HARN, E_GRS80, UM, S(114600), -81, S(125400), 32.5, 1, 609600, 0},
    {3361, "NAD83(HARN) / South Carolina (ft)", LCC2, D_NAD83_HARN, E_GRS80, UFT, S(114600), -81, S(125400), 32.5, 1, 2000000, 0},
    {3362, "NAD83(HARN) / Pennsylvania North", LCC2, D_NAD83_HARN, E_GRS80, UM, S(144600), -77.75, 41.95, S(147180), 1, 600000, 0},
    {3363, "NAD83(HARN) / Pennsylvania North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(144600), -77.75, 41.95, S(147180), 1, 1968500, 0},
    {3364, "NAD83(HARN) / Pennsylvania South", LCC2, D_NAD83_HARN, E_GRS80, UM, S(141600), -77.75, S(147480), S(143760), 1, 600000, 0},
    {3365, "NAD83(HARN) / Pennsylvania South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(141600), -77.75, S(147480), S(143760), 1, 1968500, 0},
    {3372, "NAD83 / UTM zone 59N", TM, D_NAD83, E_GRS80, UM, 0, 171, 0, 0, 0.9996, 500000, 0},
    {3373, "NAD83 / UTM zone 60N", TM, D_NAD83, E_GRS80, UM, 0, 177, 0, 0, 0.9996, 500000, 0},
    {3395, "WGS 84 / World Mercator", MERC, D_WGS84, E_WGS84, UM, 0, 0, 0, 0, 1, 0, 0},
    {3400, "NAD83 / Alberta 10-TM (Forest)", TM, D_NAD83, E_GRS80, UM, 0, -115, 0, 0, 0.9992, 500000, 0},
    {3401, "NAD83 / Alberta 10-TM (Resource)", TM, D_NAD83, E_GRS80, UM, 0, -115, 0, 0, 0.9992, 0, 0},
    {3402, "NAD83(CSRS) / Alberta 10-TM (Forest)", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -115, 0, 0, 0.9992, 500000, 0},
    {3403, "NAD83(CSRS) / Alberta 10-TM (Resource)", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -115, 0, 0, 0.9992, 0, 0},
    {3404, "NAD83(HARN) / North Carolina (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 33.75, -79, S(130200), S(123600), 1, 2000000, 0},
    {3413, "WGS 84 / NSIDC Sea Ice Polar Stereographic North", PS, D_WGS84, E_WGS84, UM, 90, -45, 70, 0, 1, 0, 0},
    {3416, "ETRS89 / Austria Lambert", LCC2, D_ETRS89, E_GRS80, UM, 47.5, S(48000), 49, 46, 1, 400000, 400000},
    {3417, "NAD83 / Iowa North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 41.5, -93.5, S(155760), S(151440), 1, 4921250, 3280833.3333},
    {3418, "NAD83 / Iowa South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 40, -93.5, S(150420), S(146220), 1, 1640416.6667, 0},
    {3419, "NAD83 / Kansas North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(138000), -98, S(143220), S(139380), 1, 1312333.3333, 0},
    {3420, "NAD83 / Kansas South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(132000), -98.5, S(138840), S(134160), 1, 1312333.3333, 1312333.3333},
    {3421, "NAD83 / Nevada East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 34.75, S(-416100), 0, 0, 0.9999, 656166.6667, 26246666.6667},
    {3422, "NAD83 / Nevada Central (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 34.75, S(-420000), 0, 0, 0.9999, 1640416.6667, 19685000},
    {3423, "NAD83 / Nevada West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 34.75, S(-426900), 0, 0, 0.9999, 2624666.6667, 13123333.3333},
    {3424, "NAD83 / New Jersey (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(139800), -74.5, 0, 0, 0.9999, 492125, 0},
    {3425, "NAD83(HARN) / Iowa North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 41.5, -93.5, S(155760), S(151440), 1, 4921250, 3280833.3333},
    {3426, "NAD83(HARN) / Iowa South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 40, -93.5, S(150420), S(146220), 1, 1640416.6667, 0},
    {3427, "NAD83(HARN) / Kansas North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(138000), -98, S(143220), S(139380), 1, 1312333.3333, 0},
    {3428, "NAD83(HARN) / Kansas South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(132000), -98.5, S(138840), S(134160), 1, 1312333.3333, 1312333.3333},
    {3429, "NAD83(HARN) / Nevada East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 34.75, S(-416100), 0, 0, 0.9999, 656166.6667, 26246666.6667},
    {3430, "NAD83(HARN) / Nevada Central (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 34.75, S(-420000), 0, 0, 0.9999, 1640416.6667, 19685000},
    {3431, "NAD83(HARN) / Nevada West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 34.75, S(-426900), 0, 0, 0.9999, 2624666.6667, 13123333.3333},
    {3432, "NAD83(HARN) / New Jersey (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(139800), -74.5, 0, 0, 0.9999, 492125, 0},
    {3433, "NAD83 / Arkansas North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(123600), -92, S(130440), S(125760), 1, 1312333.3333, 0},
    {3434, "NAD83 / Arkansas South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(117600), -92, S(125160), 33.3, 1, 1312333.3333, 1312333.3333},
    {3435, "NAD83 / Illinois East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(132000), S(-318000), 0, 0, 0.999975, 984250, 0},
    {3436, "NAD83 / Illinois West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(132000), S(-324600), 0, 0, 0.999941177, 2296583.3333, 0},
    {3437, "NAD83 / New Hampshire (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 42.5, S(-258000), 0, 0, 0.999966667, 984250, 0},
    {3438, "NAD83 / Rhode Island (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(147900), -71.5, 0, 0, 0.99999375, 328083.3333, 0},
    {3441, "NAD83(HARN) / Arkansas North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(123600), -92, S(130440), S(125760), 1, 1312333.3333, 0},
    {3442, "NAD83(HARN) / Arkansas South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(117600), -92, S(125160), 33.3, 1, 1312333.3333, 1312333.3333},
    {3443, "NAD83(HARN) / Illinois East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(132000), S(-318000), 0, 0, 0.999975, 984250, 0},
    {3444, "NAD83(HARN) / Illinois West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(132000), S(-324600), 0, 0, 0.999941177, 2296583.3333, 0},
    {3445, "NAD83(HARN) / New Hampshire (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 42.5, S(-258000), 0, 0, 0.999966667, 984250, 0},
    {3446, "NAD83(HARN) / Rhode Island (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(147900), -71.5, 0, 0, 0.99999375, 328083.3333, 0},
    {3447, "ETRS89 / Belgian Lambert 2005", LCC2, D_ETRS89, E_GRS80, UM, 50.797815, S(15693.177), S(179400), S(184200), 1, 150328, 166262},
    {3451, "NAD83 / Louisiana North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 30.5, -92.5, S(117600), S(112200), 1, 3280833.3333, 0},
    {3452, "NAD83 / Louisiana South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 28.5, S(-328800), 30.7, 29.3, 1, 3280833.3333, 0},
    {3453, "NAD83 / Louisiana Offshore (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 25.5, S(-328800), S(100200), S(94200), 1, 3280833.3333, 0},
    {3455, "NAD83 / South Dakota South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(152400), S(-361200), 44.4, S(154200), 1, 1968500, 0},
    {3456, "NAD83(HARN) / Louisiana North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 30.5, -92.5, S(117600), S(112200), 1, 3280833.3333, 0},
    {3457, "NAD83(HARN) / Louisiana South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 28.5, S(-328800), 30.7, 29.3, 1, 3280833.3333, 0},
    {3458, "NAD83(HARN) / South Dakota North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(157800), -100, S(164460), S(159900), 1, 1968500, 0},
    {3459, "NAD83(HARN) / South Dakota South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(152400), S(-361200), 44.4, S(154200), 1, 1968500, 0},
    {3463, "NAD83 / Maine CS2000 Central", TM, D_NAD83, E_GRS80, UM, 43.5, -69.125, 0, 0, 0.99998, 500000, 0},
    {3464, "NAD83(HARN) / Maine CS2000 Central", TM, D_NAD83_HARN, E_GRS80, UM, 43.5, -69.125, 0, 0, 0.99998, 500000, 0},
    {3560, "NAD83 / Utah North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(145200), -111.5, S(150420), S(146580), 1, 1640416.6667, 3280833.3333},
    {3566, "NAD83 / Utah Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(138000), -111.5, 40.65, S(140460), 1, 1640416.6667, 6561666.6667},
    {3567, "NAD83 / Utah South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(132000), -111.5, 38.35, S(133980), 1, 1640416.6667, 9842500},
    {3568, "NAD83(HARN) / Utah North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(145200), -111.5, S(150420), S(146580), 1, 1640416.6667, 3280833.3333},
    {3569, "NAD83(HARN) / Utah Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(138000), -111.5, 40.65, S(140460), 1, 1640416.6667, 6561666.6667},
    {3570, "NAD83(HARN) / Utah South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(132000), -111.5, 38.35, S(133980), 1, 1640416.6667, 9842500},
    {3577, "GDA94 / Australian Albers", AEA, D_GDA94, E_GRS80, UM, 0, 132, -18, -36, 1, 0, 0},
    {3578, "NAD83 / Yukon Albers", AEA, D_NAD83, E_GRS80, UM, 59, -132.5, S(222000), 68, 1, 500000, 500000},
    {3579, "NAD83(CSRS) / Yukon Albers", AEA, D_NAD83_CSRS, E_GRS80, UM, 59, -132.5, S(222000), 68, 1, 500000, 500000},
    {3580, "NAD83 / NWT Lambert", LCC2, D_NAD83, E_GRS80, UM, 0, -112, 62, 70, 1, 0, 0},
    {3581, "NAD83(CSRS) / NWT Lambert", LCC2, D_NAD83_CSRS, E_GRS80, UM, 0, -112, 62, 70, 1, 0, 0},
    {3734, "NAD83 / Ohio North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(142800), -82.5, 41.7, S(145560), 1, 1968500, 0},
    {3735, "NAD83 / Ohio South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 38, -82.5, S(144120), S(139440), 1, 1968500, 0},
    {3736, "NAD83 / Wyoming East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 40.5, S(-378600), 0, 0, 0.9999375, 656166.6667, 0},
    {3737, "NAD83 / Wyoming East Central (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 40.5, S(-386400), 0, 0, 0.9999375, 1312333.3333, 328083.3333},
    {3738, "NAD83 / Wyoming West Central (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 40.5, -108.75, 0, 0, 0.9999375, 1968500, 0},
    {3739, "NAD83 / Wyoming West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 40.5, S(-396300), 0, 0, 0.9999375, 2624666.6667, 328083.3333},
    {3740, "NAD83(HARN) / UTM zone 10N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -123, 0, 0, 0.9996, 500000, 0},
    {3741, "NAD83(HARN) / UTM zone 11N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -117, 0, 0, 0.9996, 500000, 0},
    {3742, "NAD83(HARN) / UTM zone 12N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -111, 0, 0, 0.9996, 500000, 0},
    {3743, "NAD83(HARN) / UTM zone 13N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -105, 0, 0, 0.9996, 500000, 0},
    {3744, "NAD83(HARN) / UTM zone 14N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -99, 0, 0, 0.9996, 500000, 0},
    {3745, "NAD83(HARN) / UTM zone 15N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -93, 0, 0, 0.9996, 500000, 0},
    {3746, "NAD83(HARN) / UTM zone 16N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -87, 0, 0, 0.9996, 500000, 0},
    {3747, "NAD83(HARN) / UTM zone 17N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -81, 0, 0, 0.9996, 500000, 0},
    {3748, "NAD83(HARN) / UTM zone 18N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -75, 0, 0, 0.9996, 500000, 0},
    {3749, "NAD83(HARN) / UTM zone 19N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -69, 0, 0, 0.9996, 500000, 0},
    {3750, "NAD83(HARN) / UTM zone 4N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -159, 0, 0, 0.9996, 500000, 0},
    {3751, "NAD83(HARN) / UTM zone 5N", TM, D_NAD83_HARN, E_GRS80, UM, 0, -153, 0, 0, 0.9996, 500000, 0},
    {3753, "NAD83(HARN) / Ohio North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(142800), -82.5, 41.7, S(145560), 1, 1968500, 0},
    {3754, "NAD83(HARN) / Ohio South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 38, -82.5, S(144120), S(139440), 1, 1968500, 0},
    {3755, "NAD83(HARN) / Wyoming East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 40.5, S(-378600), 0, 0, 0.9999375, 656166.6667, 0},
    {3756, "NAD83(HARN) / Wyoming East Central (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 40.5, S(-386400), 0, 0, 0.9999375, 1312333.3333, 328083.3333},
    {3757, "NAD83(HARN) / Wyoming West Central (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 40.5, -108.75, 0, 0, 0.9999375, 1968500, 0},
    {3758, "NAD83(HARN) / Wyoming West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 40.5, S(-396300), 0, 0, 0.9999375, 2624666.6667, 328083.3333},
    {3759, "NAD83 / Hawaii zone 3 (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(76200), -158, 0, 0, 0.99999, 1640416.6667, 0},
    {3760, "NAD83(HARN) / Hawaii zone 3 (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(76200), -158, 0, 0, 0.99999, 1640416.6667, 0},
    {3761, "NAD83(CSRS) / UTM zone 22N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -51, 0, 0, 0.9996, 500000, 0},
    {3762, "WGS 84 / South Georgia Lambert", LCC2, D_WGS84, E_WGS84, UM, -55, -37, -54, -54.75, 1, 0, 0},
    {3763, "ETRS89 / Portugal TM06", TM, D_ETRS89, E_GRS80, UM, S(142805.73), S(-29279.19), 0, 0, 1, 0, 0},
    {3764, "NZGD2000 / Chatham Island Circuit 2000", TM, D_NZGD2000, E_GRS80, UM, -44, -176.5, 0, 0, 1, 400000, 800000},
    {3775, "NAD83 / Alberta 3TM ref merid 111 W", TM, D_NAD83, E_GRS80, UM, 0, -111, 0, 0, 0.9999, 0, 0},
    {3776, "NAD83 / Alberta 3TM ref merid 114 W", TM, D_NAD83, E_GRS80, UM, 0, -114, 0, 0, 0.9999, 0, 0},
    {3777, "NAD83 / Alberta 3TM ref merid 117 W", TM, D_NAD83, E_GRS80, UM, 0, -117, 0, 0, 0.9999, 0, 0},
    {3779, "NAD83(CSRS) / Alberta 3TM ref merid 111 W", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -111, 0, 0, 0.9999, 0, 0},
    {3780, "NAD83(CSRS) / Alberta 3TM ref merid 114 W", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -114, 0, 0, 0.9999, 0, 0},
    {3781, "NAD83(CSRS) / Alberta 3TM ref merid 117 W", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -117, 0, 0, 0.9999, 0, 0},
    {3788, "NZGD2000 / Auckland Islands TM 2000", TM, D_NZGD2000, E_GRS80, UM, 0, 166, 0, 0, 1, 3500000, 10000000},
    {3789, "NZGD2000 / Campbell Island TM 2000", TM, D_NZGD2000, E_GRS80, UM, 0, 169, 0, 0, 1, 3500000, 10000000},
    {3790, "NZGD2000 / Antipodes Islands TM 2000", TM, D_NZGD2000, E_GRS80, UM, 0, 179, 0, 0, 1, 3500000, 10000000},
    {3791, "NZGD2000 / Raoul Island TM 2000", TM, D_NZGD2000, E_GRS80, UM, 0, -178, 0, 0, 1, 3500000, 10000000},
    {3793, "NZGD2000 / Chatham Islands TM 2000", TM, D_NZGD2000, E_GRS80, UM, 0, -176.5, 0, 0, 1, 3500000, 10000000},
    {3798, "NAD83 / MTQ Lambert", LCC2, D_NAD83, E_GRS80, UM, 44, -70, 50, 46, 1, 800000, 0},
    {3799, "NAD83(CSRS) / MTQ Lambert", LCC2, D_NAD83_CSRS, E_GRS80, UM, 44, -70, 50, 46, 1, 800000, 0},
    {3801, "NAD83 / Alberta 3TM ref merid 120 W", TM, D_NAD83, E_GRS80, UM, 0, -120, 0, 0, 0.9999, 0, 0},
    {3802, "NAD83(CSRS) / Alberta 3TM ref merid 120 W", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -120, 0, 0, 0.9999, 0, 0},
    {3812, "ETRS89 / Belgian Lambert 2008", LCC2, D_ETRS89, E_GRS80, UM, 50.797815, S(15693.177), S(179400), S(184200), 1, 649328, 665262},
    {3814, "NAD83 / Mississippi TM", TM, D_NAD83, E_GRS80, UM, 32.5, -89.75, 0, 0, 0.9998335, 500000, 1300000},
    {3815, "NAD83(HARN) / Mississippi TM", TM, D_NAD83_HARN, E_GRS80, UM, 32.5, -89.75, 0, 0, 0.9998335, 500000, 1300000},
    {3832, "WGS 84 / PDC Mercator", MERC, D_WGS84, E_WGS84, UM, 0, 150, 0, 0, 1, 0, 0},
    {3845, "SWEREF99 / RT90 7.5 gon V emulation", TM, D_SWEREF99, E_GRS80, UM, 0, 11.30625, 0, 0, 1.000006, 1500025.141, -667.282},
    {3846, "SWEREF99 / RT90 5 gon V emulation", TM, D_SWEREF99, E_GRS80, UM, 0, S(48802.56), 0, 0, 1.0000058, 1500044.695, -667.13},
    {3847, "SWEREF99 / RT90 2.5 gon V emulation", TM, D_SWEREF99, E_GRS80, UM, 0, 15.8062845294444, 0, 0, 1.00000561024, 1500064.274, -667.711},
    {3848, "SWEREF99 / RT90 0 gon emulation", TM, D_SWEREF99, E_GRS80, UM, 0, 18.0563, 0, 0, 1.0000054, 1500083.521, -668.844},
    {3849, "SWEREF99 / RT90 2.5 gon O emulation", TM, D_SWEREF99, E_GRS80, UM, 0, S(73102.74), 0, 0, 1.0000052, 1500102.765, -670.706},
    {3850, "SWEREF99 / RT90 5 gon O emulation", TM, D_SWEREF99, E_GRS80, UM, 0, S(81202.8), 0, 0, 1.0000049, 1500121.846, -672.557},
    {3851, "NZGD2000 / NZCS2000", LCC2, D_NZGD2000, E_GRS80, UM, -41, 173, -37.5, -44.5, 1, 3000000, 7000000},
    {3854, "County ST74", TM, D_SWEREF99, E_GRS80, UM, 0, 18.05787, 0, 0, 0.99999506, 100182.7406, -6500620.1207},
    {3942, "RGF93 v1 / CC42", LCC2, D_RGF93, E_GRS80, UM, 42, 3, 41.25, 42.75, 1, 1700000, 1200000},
    {3943, "RGF93 v1 / CC43", LCC2, D_RGF93, E_GRS80, UM, 43, 3, 42.25, 43.75, 1, 1700000, 2200000},
    {3944, "RGF93 v1 / CC44", LCC2, D_RGF93, E_GRS80, UM, 44, 3, 43.25, 44.75, 1, 1700000, 3200000},
    {3945, "RGF93 v1 / CC45", LCC2, D_RGF93, E_GRS80, UM, 45, 3, 44.25, 45.75, 1, 1700000, 4200000},
    {3946, "RGF93 v1 / CC46", LCC2, D_RGF93, E_GRS80, UM, 46, 3, 45.25, 46.75, 1, 1700000, 5200000},
    {3947, "RGF93 v1 / CC47", LCC2, D_RGF93, E_GRS80, UM, 47, 3, 46.25, 47.75, 1, 1700000, 6200000},
    {3948, "RGF93 v1 / CC48", LCC2, D_RGF93, E_GRS80, UM, 48, 3, 47.25, 48.75, 1, 1700000, 7200000},
    {3949, "RGF93 v1 / CC49", LCC2, D_RGF93, E_GRS80, UM, 49, 3, 48.25, 49.75, 1, 1700000, 8200000},
    {3950, "RGF93 v1 / CC50", LCC2, D_RGF93, E_GRS80, UM, 50, 3, 49.25, 50.75, 1, 1700000, 9200000},
    {3968, "NAD83 / Virginia Lambert", LCC2, D_NAD83, E_GRS80, UM, 36, -79.5, 37, 39.5, 1, 0, 0},
    {3969, "NAD83(HARN) / Virginia Lambert", LCC2, D_NAD83_HARN, E_GRS80, UM, 36, -79.5, 37, 39.5, 1, 0, 0},
    {3976, "WGS 84 / NSIDC Sea Ice Polar Stereographic South", PS, D_WGS84, E_WGS84, UM, -90, 0, -70, 0, 1, 0, 0},
    {3978, "NAD83 / Canada Atlas Lambert", LCC2, D_NAD83, E_GRS80, UM, 49, -95, 49, 77, 1, 0, 0},
    {3979, "NAD83(CSRS) / Canada Atlas Lambert", LCC2, D_NAD83_CSRS, E_GRS80, UM, 49, -95, 49, 77, 1, 0, 0},
    {3994, "WGS 84 / Mercator 41", MERC2, D_WGS84, E_WGS84, UM, 0, 100, -41, 0, 1, 0, 0},
    {3995, "WGS 84 / Arctic Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, 0, 71, 0, 1, 0, 0},
    {3996, "WGS 84 / IBCAO Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, 0, 75, 0, 1, 0, 0},
    {3997, "WGS 84 / Dubai Local TM", TM, D_WGS84, E_WGS84, UM, 0, S(199200), 0, 0, 1, 500000, 0},
    {4037, "WGS 84 / TMzn35N", TM, D_WGS84, E_WGS84, UM, 0, 27, 0, 0, 0.9996, 500000, 0},
    {4038, "WGS 84 / TMzn36N", TM, D_WGS84, E_WGS84, UM, 0, 33, 0, 0, 0.9996, 500000, 0},
    {4093, "ETRS89 / DKTM1", TM, D_ETRS89, E_GRS80, UM, 0, 9, 0, 0, 0.99998, 200000, -5000000},
    {4094, "ETRS89 / DKTM2", TM, D_ETRS89, E_GRS80, UM, 0, 10, 0, 0, 0.99998, 400000, -5000000},
    {4095, "ETRS89 / DKTM3", TM, D_ETRS89, E_GRS80, UM, 0, 11.75, 0, 0, 0.99998, 600000, -5000000},
    {4096, "ETRS89 / DKTM4", TM, D_ETRS89, E_GRS80, UM, 0, 15, 0, 0, 1, 800000, -5000000},
    {4217, "NAD83 / BLM 59N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, 171, 0, 0, 0.9996, 1640416.67, 0},
    {4414, "NAD83(HARN) / Guam Map Grid", TM, D_NAD83_HARN, E_GRS80, UM, 13.5, 144.75, 0, 0, 1, 100000, 200000},
    {4420, "NAD83 / BLM 60N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, 177, 0, 0, 0.9996, 1640416.67, 0},
    {4421, "NAD83 / BLM 1N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -177, 0, 0, 0.9996, 1640416.67, 0},
    {4422, "NAD83 / BLM 2N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -171, 0, 0, 0.9996, 1640416.67, 0},
    {4423, "NAD83 / BLM 3N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -165, 0, 0, 0.9996, 1640416.67, 0},
    {4424, "NAD83 / BLM 4N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -159, 0, 0, 0.9996, 1640416.67, 0},
    {4425, "NAD83 / BLM 5N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -153, 0, 0, 0.9996, 1640416.67, 0},
    {4426, "NAD83 / BLM 6N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -147, 0, 0, 0.9996, 1640416.67, 0},
    {4427, "NAD83 / BLM 7N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -141, 0, 0, 0.9996, 1640416.67, 0},
    {4428, "NAD83 / BLM 8N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -135, 0, 0, 0.9996, 1640416.67, 0},
    {4429, "NAD83 / BLM 9N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -129, 0, 0, 0.9996, 1640416.67, 0},
    {4430, "NAD83 / BLM 10N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -123, 0, 0, 0.9996, 1640416.67, 0},
    {4431, "NAD83 / BLM 11N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -117, 0, 0, 0.9996, 1640416.67, 0},
    {4432, "NAD83 / BLM 12N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -111, 0, 0, 0.9996, 1640416.67, 0},
    {4433, "NAD83 / BLM 13N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -105, 0, 0, 0.9996, 1640416.67, 0},
    {4438, "NAD83 / BLM 18N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -75, 0, 0, 0.9996, 1640416.67, 0},
    {4439, "NAD83 / BLM 19N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -69, 0, 0, 0.9996, 1640416.67, 0},
    {4457, "NAD83 / South Dakota North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(157800), -100, S(164460), S(159900), 1, 1968500, 0},
    {4462, "WGS 84 / Australian Centre for Remote Sensing Lambert", LCC2, D_WGS84, E_WGS84, UM, -27, 132, -18, -36, 1, 0, 0},
    {4647, "ETRS89 / UTM zone 32N (zE-N)", TM, D_ETRS89, E_GRS80, UM, 0, 9, 0, 0, 0.9996, 32500000, 0},
    {4826, "WGS 84 / Cape Verde National", LCC2, D_WGS84, E_WGS84, UM, S(57000), -24, 15, S(60000), 1, 161587.83, 128511.202},
    {4839, "ETRS89 / LCC Germany (N-E)", LCC2, D_ETRS89, E_GRS80, UM, 51, 10.5, S(175200), S(193200), 1, 0, 0},
    {5041, "WGS 84 / UPS North (E,N)", PS, D_WGS84, E_WGS84, UM, 90, 0, 0, 0, 0.994, 2000000, 2000000},
    {5042, "WGS 84 / UPS South (E,N)", PS, D_WGS84, E_WGS84, UM, -90, 0, 0, 0, 0.994, 2000000, 2000000},
    {5070, "NAD83 / Conus Albers", AEA, D_NAD83, E_GRS80, UM, 23, -96, 29.5, 45.5, 1, 0, 0},
    {5071, "NAD83(HARN) / Conus Albers", AEA, D_NAD83_HARN, E_GRS80, UM, 23, -96, 29.5, 45.5, 1, 0, 0},
    {5223, "WGS 84 / Gabon TM", TM, D_WGS84, E_WGS84, UM, 0, 12, 0, 0, 0.9996, 500000, 500000},
    {5243, "ETRS89 / LCC Germany (E-N)", LCC2, D_ETRS89, E_GRS80, UM, 51, 10.5, S(175200), S(193200), 1, 0, 0},
    {5316, "ETRS89 / Faroe TM", TM, D_ETRS89, E_GRS80, UM, 0, -7, 0, 0, 0.999997, 200000, -6000000},
    {5320, "NAD83 / Teranet Ontario Lambert", LCC2, D_NAD83, E_GRS80, UM, 0, -84, 44.5, 54.5, 1, 1000000, 0},
    {5321, "NAD83(CSRS) / Teranet Ontario Lambert", LCC2, D_NAD83_CSRS, E_GRS80, UM, 0, -84, 44.5, 54.5, 1, 1000000, 0},
    {5396, "SIRGAS 2000 / UTM zone 26S", TM, D_SIRGAS2000, E_GRS80, UM, 0, -27, 0, 0, 0.9996, 500000, 10000000},
    {5523, "WGS 84 / Gabon TM 2011", TM, D_WGS84, E_WGS84, UM, 0, 11.5, 0, 0, 0.9996, 1500000, 5500000},
    {5641, "SIRGAS 2000 / Brazil Mercator", MERC2, D_SIRGAS2000, E_GRS80, UM, 0, -43, -2, 0, 1, 5000000, 10000000},
    {5646, "NAD83 / Vermont (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 42.5, -72.5, 0, 0, 0.999964286, 1640416.6667, 0},
    {5649, "ETRS89 / UTM zone 31N (zE-N)", TM, D_ETRS89, E_GRS80, UM, 0, 3, 0, 0, 0.9996, 31500000, 0},
    {5650, "ETRS89 / UTM zone 33N (zE-N)", TM, D_ETRS89, E_GRS80, UM, 0, 15, 0, 0, 0.9996, 33500000, 0},
    {5651, "ETRS89 / UTM zone 31N (N-zE)", TM, D_ETRS89, E_GRS80, UM, 0, 3, 0, 0, 0.9996, 31500000, 0},
    {5652, "ETRS89 / UTM zone 32N (N-zE)", TM, D_ETRS89, E_GRS80, UM, 0, 9, 0, 0, 0.9996, 32500000, 0},
    {5653, "ETRS89 / UTM zone 33N (N-zE)", TM, D_ETRS89, E_GRS80, UM, 0, 15, 0, 0, 0.9996, 33500000, 0},
    {5654, "NAD83(HARN) / Vermont (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 42.5, -72.5, 0, 0, 0.999964286, 1640416.6667, 0},
    {5700, "NZGD2000 / UTM zone 1S", TM, D_NZGD2000, E_GRS80, UM, 0, -177, 0, 0, 0.9996, 500000, 10000000},
    {5842, "WGS 84 / TM 12 SE", TM, D_WGS84, E_WGS84, UM, 0, 12, 0, 0, 0.9996, 500000, 10000000},
    {5921, "WGS 84 / EPSG Arctic Regional zone A1", LCC2, D_WGS84, E_WGS84, UM, 81.317226, -111, 85, 77, 1, 0, 0},
    {5922, "WGS 84 / EPSG Arctic Regional zone A2", LCC2, D_WGS84, E_WGS84, UM, 81.317226, -39, 85, 77, 1, 0, 0},
    {5923, "WGS 84 / EPSG Arctic Regional zone A3", LCC2, D_WGS84, E_WGS84, UM, 81.317226, 33, 85, 77, 1, 0, 0},
    {5924, "WGS 84 / EPSG Arctic Regional zone A4", LCC2, D_WGS84, E_WGS84, UM, 81.317226, 105, 85, 77, 1, 0, 0},
    {5925, "WGS 84 / EPSG Arctic Regional zone A5", LCC2, D_WGS84, E_WGS84, UM, 81.317226, 177, 85, 77, 1, 0, 0},
    {5926, "WGS 84 / EPSG Arctic Regional zone B1", LCC2, D_WGS84, E_WGS84, UM, S(263360.6671), -111, 77, 69, 1, 0, 0},
    {5927, "WGS 84 / EPSG Arctic Regional zone B2", LCC2, D_WGS84, E_WGS84, UM, S(263360.6671), -39, 77, 69, 1, 0, 0},
    {5928, "WGS 84 / EPSG Arctic Regional zone B3", LCC2, D_WGS84, E_WGS84, UM, S(263360.6671), 33, 77, 69, 1, 0, 0},
    {5929, "WGS 84 / EPSG Arctic Regional zone B4", LCC2, D_WGS84, E_WGS84, UM, S(263360.6671), 105, 77, 69, 1, 0, 0},
    {5930, "WGS 84 / EPSG Arctic Regional zone B5", LCC2, D_WGS84, E_WGS84, UM, S(263360.6671), 177, 77, 69, 1, 0, 0},
    {5931, "WGS 84 / EPSG Arctic Regional zone C1", LCC2, D_WGS84, E_WGS84, UM, S(234364.5752), -111, 69, 61, 1, 0, 0},
    {5932, "WGS 84 / EPSG Arctic Regional zone C2", LCC2, D_WGS84, E_WGS84, UM, S(234364.5752), -39, 69, 61, 1, 0, 0},
    {5933, "WGS 84 / EPSG Arctic Regional zone C3", LCC2, D_WGS84, E_WGS84, UM, S(234364.5752), 33, 69, 61, 1, 0, 0},
    {5934, "WGS 84 / EPSG Arctic Regional zone C4", LCC2, D_WGS84, E_WGS84, UM, S(234364.5752), 105, 69, 61, 1, 0, 0},
    {5935, "WGS 84 / EPSG Arctic Regional zone C5", LCC2, D_WGS84, E_WGS84, UM, S(234364.5752), 177, 69, 61, 1, 0, 0},
    {5936, "WGS 84 / EPSG Alaska Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, -150, 0, 0, 0.994, 2000000, 2000000},
    {5937, "WGS 84 / EPSG Canada Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, -100, 0, 0, 0.994, 2000000, 2000000},
    {5938, "WGS 84 / EPSG Greenland Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, -33, 0, 0, 0.994, 2000000, 2000000},
    {5939, "WGS 84 / EPSG Norway Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, 18, 0, 0, 0.994, 2000000, 2000000},
    {5940, "WGS 84 / EPSG Russia Polar Stereographic", PS, D_WGS84, E_WGS84, UM, 90, 105, 0, 0, 0.994, 2000000, 2000000},
    {6069, "ETRS89 / EPSG Arctic zone 2-22", LCC2, D_ETRS89, E_GRS80, UM, S(295410.3296), 16, S(301200), S(289200), 1, 22500000, 2500000},
    {6070, "ETRS89 / EPSG Arctic zone 3-11", LCC2, D_ETRS89, E_GRS80, UM, S(283346.4151), 21, S(289200), 77, 1, 11500000, 3500000},
    {6071, "ETRS89 / EPSG Arctic zone 4-26", LCC2, D_ETRS89, E_GRS80, UM, S(271311.8519), 10, 77, S(265200), 1, 26500000, 4500000},
    {6072, "ETRS89 / EPSG Arctic zone 4-28", LCC2, D_ETRS89, E_GRS80, UM, S(271311.8519), 34, 77, S(265200), 1, 28500000, 4500000},
    {6073, "ETRS89 / EPSG Arctic zone 5-11", LCC2, D_ETRS89, E_GRS80, UM, S(259290.0331), 14, S(265200), S(253200), 1, 11500000, 5500000},
    {6074, "ETRS89 / EPSG Arctic zone 5-13", LCC2, D_ETRS89, E_GRS80, UM, S(259290.0331), 34, S(265200), S(253200), 1, 13500000, 5500000},
    {6075, "WGS 84 / EPSG Arctic zone 2-24", LCC2, D_WGS84, E_WGS84, UM, S(295410.3296), 53, S(301200), S(289200), 1, 24500000, 2500000},
    {6076, "WGS 84 / EPSG Arctic zone 2-26", LCC2, D_WGS84, E_WGS84, UM, S(295410.3296), 93, S(301200), S(289200), 1, 26500000, 2500000},
    {6077, "WGS 84 / EPSG Arctic zone 3-13", LCC2, D_WGS84, E_WGS84, UM, S(283346.4151), 52, S(289200), 77, 1, 13500000, 3500000},
    {6078, "WGS 84 / EPSG Arctic zone 3-15", LCC2, D_WGS84, E_WGS84, UM, S(283346.4151), 83, S(289200), 77, 1, 15500000, 3500000},
    {6079, "WGS 84 / EPSG Arctic zone 3-17", LCC2, D_WGS84, E_WGS84, UM, S(283346.4151), 114, S(289200), 77, 1, 17500000, 3500000},
    {6080, "WGS 84 / EPSG Arctic zone 3-19", LCC2, D_WGS84, E_WGS84, UM, S(283346.4151), 145, S(289200), 77, 1, 19500000, 3500000},
    {6081, "WGS 84 / EPSG Arctic zone 4-30", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), 58, 77, S(265200), 1, 30500000, 4500000},
    {6082, "WGS 84 / EPSG Arctic zone 4-32", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), 82, 77, S(265200), 1, 32500000, 4500000},
    {6083, "WGS 84 / EPSG Arctic zone 4-34", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), 106, 77, S(265200), 1, 34500000, 4500000},
    {6084, "WGS 84 / EPSG Arctic zone 4-36", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), 130, 77, S(265200), 1, 36500000, 4500000},
    {6085, "WGS 84 / EPSG Arctic zone 4-38", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), 154, 77, S(265200), 1, 38500000, 4500000},
    {6086, "WGS 84 / EPSG Arctic zone 4-40", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), 179, 77, S(265200), 1, 40500000, 4500000},
    {6087, "WGS 84 / EPSG Arctic zone 5-15", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 54, S(265200), S(253200), 1, 15500000, 5500000},
    {6088, "WGS 84 / EPSG Arctic zone 5-17", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 74, S(265200), S(253200), 1, 17500000, 5500000},
    {6089, "WGS 84 / EPSG Arctic zone 5-19", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 95, S(265200), S(253200), 1, 19500000, 5500000},
    {6090, "WGS 84 / EPSG Arctic zone 5-21", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 116, S(265200), S(253200), 1, 21500000, 5500000},
    {6091, "WGS 84 / EPSG Arctic zone 5-23", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 137, S(265200), S(253200), 1, 23500000, 5500000},
    {6092, "WGS 84 / EPSG Arctic zone 5-25", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 158, S(265200), S(253200), 1, 25500000, 5500000},
    {6093, "WGS 84 / EPSG Arctic zone 5-27", LCC2, D_WGS84, E_WGS84, UM, S(259290.0331), 179, S(265200), S(253200), 1, 27500000, 5500000},
    {6098, "NAD83(CSRS) / EPSG Arctic zone 1-23", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(307573.626), -90, 87, S(301200), 1, 23500000, 1500000},
    {6099, "NAD83(CSRS) / EPSG Arctic zone 2-14", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(295410.3296), -115, S(301200), S(289200), 1, 14500000, 2500000},
    {6100, "NAD83(CSRS) / EPSG Arctic zone 2-16", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(295410.3296), -75, S(301200), S(289200), 1, 16500000, 2500000},
    {6101, "NAD83(CSRS) / EPSG Arctic zone 3-25", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(283346.4151), -129, S(289200), 77, 1, 25500000, 3500000},
    {6102, "NAD83(CSRS) / EPSG Arctic zone 3-27", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(283346.4151), -99, S(289200), 77, 1, 27500000, 3500000},
    {6103, "NAD83(CSRS) / EPSG Arctic zone 3-29", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(283346.4151), -69, S(289200), 77, 1, 29500000, 3500000},
    {6104, "NAD83(CSRS) / EPSG Arctic zone 4-14", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(271311.8519), -129, 77, S(265200), 1, 14500000, 4500000},
    {6105, "NAD83(CSRS) / EPSG Arctic zone 4-16", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(271311.8519), -104, 77, S(265200), 1, 16500000, 4500000},
    {6106, "NAD83(CSRS) / EPSG Arctic zone 4-18", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(271311.8519), -79, 77, S(265200), 1, 18500000, 4500000},
    {6107, "NAD83(CSRS) / EPSG Arctic zone 5-33", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(259290.0331), -131, S(265200), S(253200), 1, 33500000, 5500000},
    {6108, "NAD83(CSRS) / EPSG Arctic zone 5-35", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(259290.0331), -111, S(265200), S(253200), 1, 35500000, 5500000},
    {6109, "NAD83(CSRS) / EPSG Arctic zone 5-37", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(259290.0331), -91, S(265200), S(253200), 1, 37500000, 5500000},
    {6110, "NAD83(CSRS) / EPSG Arctic zone 5-39", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(259290.0331), -71, S(265200), S(253200), 1, 39500000, 5500000},
    {6111, "NAD83(CSRS) / EPSG Arctic zone 6-18", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(247274.912), -132, S(253200), 67, 1, 18500000, 6500000},
    {6112, "NAD83(CSRS) / EPSG Arctic zone 6-20", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(247274.912), -113, S(253200), 67, 1, 20500000, 6500000},
    {6113, "NAD83(CSRS) / EPSG Arctic zone 6-22", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(247274.912), -94, S(253200), 67, 1, 22500000, 6500000},
    {6114, "NAD83(CSRS) / EPSG Arctic zone 6-24", LCC2, D_NAD83_CSRS, E_GRS80, UM, S(247274.912), -75, S(253200), 67, 1, 24500000, 6500000},
    {6115, "WGS 84 / EPSG Arctic zone 1-27", LCC2, D_WGS84, E_WGS84, UM, S(307573.626), 30, 87, S(301200), 1, 27500000, 1500000},
    {6116, "WGS 84 / EPSG Arctic zone 1-29", LCC2, D_WGS84, E_WGS84, UM, S(307573.626), 90, 87, S(301200), 1, 29500000, 1500000},
    {6117, "WGS 84 / EPSG Arctic zone 1-31", LCC2, D_WGS84, E_WGS84, UM, S(307573.626), 150, 87, S(301200), 1, 31500000, 1500000},
    {6118, "WGS 84 / EPSG Arctic zone 1-21", LCC2, D_WGS84, E_WGS84, UM, S(307573.626), -150, 87, S(301200), 1, 21500000, 1500000},
    {6119, "WGS 84 / EPSG Arctic zone 2-28", LCC2, D_WGS84, E_WGS84, UM, S(295410.3296), 133, S(301200), S(289200), 1, 28500000, 2500000},
    {6120, "WGS 84 / EPSG Arctic zone 2-10", LCC2, D_WGS84, E_WGS84, UM, S(295410.3296), 166, S(301200), S(289200), 1, 10500000, 2500000},
    {6121, "WGS 84 / EPSG Arctic zone 2-12", LCC2, D_WGS84, E_WGS84, UM, S(295410.3296), -154, S(301200), S(289200), 1, 12500000, 2500000},
    {6122, "WGS 84 / EPSG Arctic zone 3-21", LCC2, D_WGS84, E_WGS84, UM, S(283346.4151), 176, S(289200), 77, 1, 21500000, 3500000},
    {6123, "WGS 84 / EPSG Arctic zone 3-23", LCC2, D_WGS84, E_WGS84, UM, S(283346.4151), -153, S(289200), 77, 1, 23500000, 3500000},
    {6124, "WGS 84 / EPSG Arctic zone 4-12", LCC2, D_WGS84, E_WGS84, UM, S(271311.8519), -155, 77, S(265200), 1, 12500000, 4500000},
    {6125, "ETRS89 / EPSG Arctic zone 5-47", LCC2, D_ETRS89, E_GRS80, UM, S(259290.0331), -5, S(265200), S(253200), 1, 47500000, 5500000},
    {6210, "SIRGAS 2000 / UTM zone 23N", TM, D_SIRGAS2000, E_GRS80, UM, 0, -45, 0, 0, 0.9996, 500000, 0},
    {6211, "SIRGAS 2000 / UTM zone 24N", TM, D_SIRGAS2000, E_GRS80, UM, 0, -39, 0, 0, 0.9996, 500000, 0},
    {6328, "NAD83(2011) / UTM zone 59N", TM, D_NAD83_2011, E_GRS80, UM, 0, 171, 0, 0, 0.9996, 500000, 0},
    {6329, "NAD83(2011) / UTM zone 60N", TM, D_NAD83_2011, E_GRS80, UM, 0, 177, 0, 0, 0.9996, 500000, 0},
    {6350, "NAD83(2011) / Conus Albers", AEA, D_NAD83_2011, E_GRS80, UM, 23, -96, 29.5, 45.5, 1, 0, 0},
    {6351, "NAD83(2011) / EPSG Arctic zone 5-29", LCC2, D_NAD83_2011, E_GRS80, UM, S(259290.0331), -163, S(265200), S(253200), 1, 29500000, 5500000},
    {6352, "NAD83(2011) / EPSG Arctic zone 5-31", LCC2, D_NAD83_2011, E_GRS80, UM, S(259290.0331), -147, S(265200), S(253200), 1, 31500000, 5500000},
    {6353, "NAD83(2011) / EPSG Arctic zone 6-14", LCC2, D_NAD83_2011, E_GRS80, UM, S(247274.912), -165, S(253200), 67, 1, 14500000, 6500000},
    {6354, "NAD83(2011) / EPSG Arctic zone 6-16", LCC2, D_NAD83_2011, E_GRS80, UM, S(247274.912), -147, S(253200), 67, 1, 16500000, 6500000},
    {6355, "NAD83(2011) / Alabama East", TM, D_NAD83_2011, E_GRS80, UM, 30.5, S(-309000), 0, 0, 0.99996, 200000, 0},
    {6356, "NAD83(2011) / Alabama West", TM, D_NAD83_2011, E_GRS80, UM, 30, -87.5, 0, 0, 0.999933333, 600000, 0},
    {6393, "NAD83(2011) / Alaska Albers", AEA, D_NAD83_2011, E_GRS80, UM, 50, -154, 55, 65, 1, 0, 0},
    {6395, "NAD83(2011) / Alaska zone 2", TM, D_NAD83_2011, E_GRS80, UM, 54, -142, 0, 0, 0.9999, 500000, 0},
    {6396, "NAD83(2011) / Alaska zone 3", TM, D_NAD83_2011, E_GRS80, UM, 54, -146, 0, 0, 0.9999, 500000, 0},
    {6397, "NAD83(2011) / Alaska zone 4", TM, D_NAD83_2011, E_GRS80, UM, 54, -150, 0, 0, 0.9999, 500000, 0},
    {6398, "NAD83(2011) / Alaska zone 5", TM, D_NAD83_2011, E_GRS80, UM, 54, -154, 0, 0, 0.9999, 500000, 0},
    {6399, "NAD83(2011) / Alaska zone 6", TM, D_NAD83_2011, E_GRS80, UM, 54, -158, 0, 0, 0.9999, 500000, 0},
    {6400, "NAD83(2011) / Alaska zone 7", TM, D_NAD83_2011, E_GRS80, UM, 54, -162, 0, 0, 0.9999, 500000, 0},
    {6401, "NAD83(2011) / Alaska zone 8", TM, D_NAD83_2011, E_GRS80, UM, 54, -166, 0, 0, 0.9999, 500000, 0},
    {6402, "NAD83(2011) / Alaska zone 9", TM, D_NAD83_2011, E_GRS80, UM, 54, -170, 0, 0, 0.9999, 500000, 0},
    {6403, "NAD83(2011) / Alaska zone 10", LCC2, D_NAD83_2011, E_GRS80, UM, 51, -176, S(193800), S(186600), 1, 1000000, 0},
    {6404, "NAD83(2011) / Arizona Central", TM, D_NAD83_2011, E_GRS80, UM, 31, S(-402900), 0, 0, 0.9999, 213360, 0},
    {6405, "NAD83(2011) / Arizona Central (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 31, S(-402900), 0, 0, 0.9999, 700000, 0},
    {6406, "NAD83(2011) / Arizona East", TM, D_NAD83_2011, E_GRS80, UM, 31, S(-396600), 0, 0, 0.9999, 213360, 0},
    {6407, "NAD83(2011) / Arizona East (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 31, S(-396600), 0, 0, 0.9999, 700000, 0},
    {6408, "NAD83(2011) / Arizona West", TM, D_NAD83_2011, E_GRS80, UM, 31, -113.75, 0, 0, 0.999933333, 213360, 0},
    {6409, "NAD83(2011) / Arizona West (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 31, -113.75, 0, 0, 0.999933333, 700000, 0},
    {6410, "NAD83(2011) / Arkansas North", LCC2, D_NAD83_2011, E_GRS80, UM, S(123600), -92, S(130440), S(125760), 1, 400000, 0},
    {6411, "NAD83(2011) / Arkansas North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(123600), -92, S(130440), S(125760), 1, 1312333.3333, 0},
    {6412, "NAD83(2011) / Arkansas South", LCC2, D_NAD83_2011, E_GRS80, UM, S(117600), -92, S(125160), 33.3, 1, 400000, 400000},
    {6413, "NAD83(2011) / Arkansas South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(117600), -92, S(125160), 33.3, 1, 1312333.3333, 1312333.3333},
    {6414, "NAD83(2011) / California Albers", AEA, D_NAD83_2011, E_GRS80, UM, 0, -120, 34, 40.5, 1, 0, -4000000},
    {6415, "NAD83(2011) / California zone 1", LCC2, D_NAD83_2011, E_GRS80, UM, S(141600), -122, S(150000), 40, 1, 2000000, 500000},
    {6416, "NAD83(2011) / California zone 1 (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(141600), -122, S(150000), 40, 1, 6561666.667, 1640416.667},
    {6417, "NAD83(2011) / California zone 2", LCC2, D_NAD83_2011, E_GRS80, UM, S(135600), -122, S(143400), S(138000), 1, 2000000, 500000},
    {6418, "NAD83(2011) / California zone 2 (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(135600), -122, S(143400), S(138000), 1, 6561666.667, 1640416.667},
    {6419, "NAD83(2011) / California zone 3", LCC2, D_NAD83_2011, E_GRS80, UM, 36.5, -120.5, S(138360), S(133440), 1, 2000000, 500000},
    {6420, "NAD83(2011) / California zone 3 (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 36.5, -120.5, S(138360), S(133440), 1, 6561666.667, 1640416.667},
    {6421, "NAD83(2011) / California zone 4", LCC2, D_NAD83_2011, E_GRS80, UM, S(127200), -119, 37.25, 36, 1, 2000000, 500000},
    {6422, "NAD83(2011) / California zone 4 (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(127200), -119, 37.25, 36, 1, 6561666.667, 1640416.667},
    {6423, "NAD83(2011) / California zone 5", LCC2, D_NAD83_2011, E_GRS80, UM, 33.5, -118, S(127680), S(122520), 1, 2000000, 500000},
    {6424, "NAD83(2011) / California zone 5 (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 33.5, -118, S(127680), S(122520), 1, 6561666.667, 1640416.667},
    {6425, "NAD83(2011) / California zone 6", LCC2, D_NAD83_2011, E_GRS80, UM, S(115800), -116.25, S(121980), S(118020), 1, 2000000, 500000},
    {6426, "NAD83(2011) / California zone 6 (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(115800), -116.25, S(121980), S(118020), 1, 6561666.667, 1640416.667},
    {6427, "NAD83(2011) / Colorado Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(136200), -105.5, 39.75, 38.45, 1, 914401.8289, 304800.6096},
    {6428, "NAD83(2011) / Colorado Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(136200), -105.5, 39.75, 38.45, 1, 3000000, 1000000},
    {6429, "NAD83(2011) / Colorado North", LCC2, D_NAD83_2011, E_GRS80, UM, S(141600), -105.5, S(146820), S(142980), 1, 914401.8289, 304800.6096},
    {6430, "NAD83(2011) / Colorado North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(141600), -105.5, S(146820), S(142980), 1, 3000000, 1000000},
    {6431, "NAD83(2011) / Colorado South", LCC2, D_NAD83_2011, E_GRS80, UM, S(132000), -105.5, S(138360), S(134040), 1, 914401.8289, 304800.6096},
    {6432, "NAD83(2011) / Colorado South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(132000), -105.5, S(138360), S(134040), 1, 3000000, 1000000},
    {6433, "NAD83(2011) / Connecticut", LCC2, D_NAD83_2011, E_GRS80, UM, S(147000), -72.75, S(150720), 41.2, 1, 304800.6096, 152400.3048},
    {6434, "NAD83(2011) / Connecticut (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(147000), -72.75, S(150720), 41.2, 1, 1000000, 500000},
    {6435, "NAD83(2011) / Delaware", TM, D_NAD83_2011, E_GRS80, UM, 38, S(-271500), 0, 0, 0.999995, 200000, 0},
    {6436, "NAD83(2011) / Delaware (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38, S(-271500), 0, 0, 0.999995, 656166.667, 0},
    {6437, "NAD83(2011) / Florida East", TM, D_NAD83_2011, E_GRS80, UM, S(87600), -81, 0, 0, 0.999941177, 200000, 0},
    {6438, "NAD83(2011) / Florida East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(87600), -81, 0, 0, 0.999941177, 656166.667, 0},
    {6439, "NAD83(2011) / Florida GDL Albers", AEA, D_NAD83_2011, E_GRS80, UM, 24, -84, 24, 31.5, 1, 400000, 0},
    {6440, "NAD83(2011) / Florida North", LCC2, D_NAD83_2011, E_GRS80, UM, 29, -84.5, 30.75, S(106500), 1, 600000, 0},
    {6441, "NAD83(2011) / Florida North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 29, -84.5, 30.75, S(106500), 1, 1968500, 0},
    {6442, "NAD83(2011) / Florida West", TM, D_NAD83_2011, E_GRS80, UM, S(87600), -82, 0, 0, 0.999941177, 200000, 0},
    {6443, "NAD83(2011) / Florida West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(87600), -82, 0, 0, 0.999941177, 656166.667, 0},
    {6444, "NAD83(2011) / Georgia East", TM, D_NAD83_2011, E_GRS80, UM, 30, S(-295800), 0, 0, 0.9999, 200000, 0},
    {6445, "NAD83(2011) / Georgia East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 30, S(-295800), 0, 0, 0.9999, 656166.667, 0},
    {6446, "NAD83(2011) / Georgia West", TM, D_NAD83_2011, E_GRS80, UM, 30, S(-303000), 0, 0, 0.9999, 700000, 0},
    {6447, "NAD83(2011) / Georgia West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 30, S(-303000), 0, 0, 0.9999, 2296583.333, 0},
    {6448, "NAD83(2011) / Idaho Central", TM, D_NAD83_2011, E_GRS80, UM, S(150000), -114, 0, 0, 0.999947368, 500000, 0},
    {6449, "NAD83(2011) / Idaho Central (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(150000), -114, 0, 0, 0.999947368, 1640416.667, 0},
    {6450, "NAD83(2011) / Idaho East", TM, D_NAD83_2011, E_GRS80, UM, S(150000), S(-403800), 0, 0, 0.999947368, 200000, 0},
    {6451, "NAD83(2011) / Idaho East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(150000), S(-403800), 0, 0, 0.999947368, 656166.667, 0},
    {6452, "NAD83(2011) / Idaho West", TM, D_NAD83_2011, E_GRS80, UM, S(150000), -115.75, 0, 0, 0.999933333, 800000, 0},
    {6453, "NAD83(2011) / Idaho West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(150000), -115.75, 0, 0, 0.999933333, 2624666.667, 0},
    {6454, "NAD83(2011) / Illinois East", TM, D_NAD83_2011, E_GRS80, UM, S(132000), S(-318000), 0, 0, 0.999975, 300000, 0},
    {6455, "NAD83(2011) / Illinois East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(132000), S(-318000), 0, 0, 0.999975, 984250, 0},
    {6456, "NAD83(2011) / Illinois West", TM, D_NAD83_2011, E_GRS80, UM, S(132000), S(-324600), 0, 0, 0.999941177, 700000, 0},
    {6457, "NAD83(2011) / Illinois West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(132000), S(-324600), 0, 0, 0.999941177, 2296583.3333, 0},
    {6458, "NAD83(2011) / Indiana East", TM, D_NAD83_2011, E_GRS80, UM, 37.5, S(-308400), 0, 0, 0.999966667, 100000, 250000},
    {6459, "NAD83(2011) / Indiana East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, S(-308400), 0, 0, 0.999966667, 328083.333, 820208.333},
    {6460, "NAD83(2011) / Indiana West", TM, D_NAD83_2011, E_GRS80, UM, 37.5, S(-313500), 0, 0, 0.999966667, 900000, 250000},
    {6461, "NAD83(2011) / Indiana West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, S(-313500), 0, 0, 0.999966667, 2952750, 820208.333},
    {6462, "NAD83(2011) / Iowa North", LCC2, D_NAD83_2011, E_GRS80, UM, 41.5, -93.5, S(155760), S(151440), 1, 1500000, 1000000},
    {6463, "NAD83(2011) / Iowa North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 41.5, -93.5, S(155760), S(151440), 1, 4921250, 3280833.3333},
    {6464, "NAD83(2011) / Iowa South", LCC2, D_NAD83_2011, E_GRS80, UM, 40, -93.5, S(150420), S(146220), 1, 500000, 0},
    {6465, "NAD83(2011) / Iowa South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 40, -93.5, S(150420), S(146220), 1, 1640416.6667, 0},
    {6466, "NAD83(2011) / Kansas North", LCC2, D_NAD83_2011, E_GRS80, UM, S(138000), -98, S(143220), S(139380), 1, 400000, 0},
    {6467, "NAD83(2011) / Kansas North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(138000), -98, S(143220), S(139380), 1, 1312333.3333, 0},
    {6468, "NAD83(2011) / Kansas South", LCC2, D_NAD83_2011, E_GRS80, UM, S(132000), -98.5, S(138840), S(134160), 1, 400000, 400000},
    {6469, "NAD83(2011) / Kansas South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(132000), -98.5, S(138840), S(134160), 1, 1312333.3333, 1312333.3333},
    {6470, "NAD83(2011) / Kentucky North", LCC2, D_NAD83_2011, E_GRS80, UM, 37.5, -84.25, S(136680), S(140280), 1, 500000, 0},
    {6471, "NAD83(2011) / Kentucky North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 37.5, -84.25, S(136680), S(140280), 1, 1640416.667, 0},
    {6472, "NAD83(2011) / Kentucky Single Zone", LCC2, D_NAD83_2011, E_GRS80, UM, S(130800), -85.75, S(133500), S(139200), 1, 1500000, 1000000},
    {6473, "NAD83(2011) / Kentucky Single Zone (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(130800), -85.75, S(133500), S(139200), 1, 4921250, 3280833.333},
    {6474, "NAD83(2011) / Kentucky South", LCC2, D_NAD83_2011, E_GRS80, UM, S(130800), -85.75, S(136560), S(132240), 1, 500000, 500000},
    {6475, "NAD83(2011) / Kentucky South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(130800), -85.75, S(136560), S(132240), 1, 1640416.667, 1640416.667},
    {6476, "NAD83(2011) / Louisiana North", LCC2, D_NAD83_2011, E_GRS80, UM, 30.5, -92.5, S(117600), S(112200), 1, 1000000, 0},
    {6477, "NAD83(2011) / Louisiana North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 30.5, -92.5, S(117600), S(112200), 1, 3280833.3333, 0},
    {6478, "NAD83(2011) / Louisiana South", LCC2, D_NAD83_2011, E_GRS80, UM, 28.5, S(-328800), 30.7, 29.3, 1, 1000000, 0},
    {6479, "NAD83(2011) / Louisiana South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 28.5, S(-328800), 30.7, 29.3, 1, 3280833.3333, 0},
    {6480, "NAD83(2011) / Maine CS2000 Central", TM, D_NAD83_2011, E_GRS80, UM, 43.5, -69.125, 0, 0, 0.99998, 500000, 0},
    {6481, "NAD83(2011) / Maine CS2000 East", TM, D_NAD83_2011, E_GRS80, UM, S(157800), -67.875, 0, 0, 0.99998, 700000, 0},
    {6482, "NAD83(2011) / Maine CS2000 West", TM, D_NAD83_2011, E_GRS80, UM, S(154200), -70.375, 0, 0, 0.99998, 300000, 0},
    {6483, "NAD83(2011) / Maine East", TM, D_NAD83_2011, E_GRS80, UM, S(157200), -68.5, 0, 0, 0.9999, 300000, 0},
    {6484, "NAD83(2011) / Maine East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(157200), -68.5, 0, 0, 0.9999, 984250, 0},
    {6485, "NAD83(2011) / Maine West", TM, D_NAD83_2011, E_GRS80, UM, S(154200), S(-252600), 0, 0, 0.999966667, 900000, 0},
    {6486, "NAD83(2011) / Maine West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(154200), S(-252600), 0, 0, 0.999966667, 2952750, 0},
    {6487, "NAD83(2011) / Maryland", LCC2, D_NAD83_2011, E_GRS80, UM, S(135600), -77, 39.45, 38.3, 1, 400000, 0},
    {6488, "NAD83(2011) / Maryland (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(135600), -77, 39.45, 38.3, 1, 1312333.333, 0},
    {6489, "NAD83(2011) / Massachusetts Island", LCC2, D_NAD83_2011, E_GRS80, UM, 41, -70.5, S(149340), S(148620), 1, 500000, 0},
    {6490, "NAD83(2011) / Massachusetts Island (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 41, -70.5, S(149340), S(148620), 1, 1640416.667, 0},
    {6491, "NAD83(2011) / Massachusetts Mainland", LCC2, D_NAD83_2011, E_GRS80, UM, 41, -71.5, S(153660), S(150180), 1, 200000, 750000},
    {6492, "NAD83(2011) / Massachusetts Mainland (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 41, -71.5, S(153660), S(150180), 1, 656166.667, 2460625},
    {6493, "NAD83(2011) / Michigan Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(155940), S(-303720), 45.7, S(159060), 1, 6000000, 0},
    {6494, "NAD83(2011) / Michigan Central (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, S(155940), S(-303720), 45.7, S(159060), 1, 19685039.37, 0},
    {6495, "NAD83(2011) / Michigan North", LCC2, D_NAD83_2011, E_GRS80, UM, S(161220), -87, S(169500), S(163740), 1, 8000000, 0},
    {6496, "NAD83(2011) / Michigan North (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, S(161220), -87, S(169500), S(163740), 1, 26246719.16, 0},
    {6498, "NAD83(2011) / Michigan South", LCC2, D_NAD83_2011, E_GRS80, UM, 41.5, S(-303720), S(157200), 42.1, 1, 4000000, 0},
    {6499, "NAD83(2011) / Michigan South (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, 41.5, S(-303720), S(157200), 42.1, 1, 13123359.58, 0},
    {6500, "NAD83(2011) / Minnesota Central", LCC2, D_NAD83_2011, E_GRS80, UM, 45, -94.25, 47.05, S(164220), 1, 800000, 100000},
    {6501, "NAD83(2011) / Minnesota Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 45, -94.25, 47.05, S(164220), 1, 2624666.6667, 328083.3333},
    {6502, "NAD83(2011) / Minnesota North", LCC2, D_NAD83_2011, E_GRS80, UM, 46.5, -93.1, S(175080), S(169320), 1, 800000, 100000},
    {6503, "NAD83(2011) / Minnesota North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 46.5, -93.1, S(175080), S(169320), 1, 2624666.6667, 328083.3333},
    {6504, "NAD83(2011) / Minnesota South", LCC2, D_NAD83_2011, E_GRS80, UM, 43, -94, S(162780), S(157620), 1, 800000, 100000},
    {6505, "NAD83(2011) / Minnesota South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 43, -94, S(162780), S(157620), 1, 2624666.6667, 328083.3333},
    {6506, "NAD83(2011) / Mississippi East", TM, D_NAD83_2011, E_GRS80, UM, 29.5, S(-319800), 0, 0, 0.99995, 300000, 0},
    {6507, "NAD83(2011) / Mississippi East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 29.5, S(-319800), 0, 0, 0.99995, 984250, 0},
    {6508, "NAD83(2011) / Mississippi TM", TM, D_NAD83_2011, E_GRS80, UM, 32.5, -89.75, 0, 0, 0.9998335, 500000, 1300000},
    {6509, "NAD83(2011) / Mississippi West", TM, D_NAD83_2011, E_GRS80, UM, 29.5, S(-325200), 0, 0, 0.99995, 700000, 0},
    {6510, "NAD83(2011) / Mississippi West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 29.5, S(-325200), 0, 0, 0.99995, 2296583.333, 0},
    {6511, "NAD83(2011) / Missouri Central", TM, D_NAD83_2011, E_GRS80, UM, S(129000), -92.5, 0, 0, 0.999933333, 500000, 0},
    {6512, "NAD83(2011) / Missouri East", TM, D_NAD83_2011, E_GRS80, UM, S(129000), -90.5, 0, 0, 0.999933333, 250000, 0},
    {6513, "NAD83(2011) / Missouri West", TM, D_NAD83_2011, E_GRS80, UM, S(130200), -94.5, 0, 0, 0.999941177, 850000, 0},
    {6514, "NAD83(2011) / Montana", LCC2, D_NAD83_2011, E_GRS80, UM, 44.25, -109.5, 49, 45, 1, 600000, 0},
    {6515, "NAD83(2011) / Montana (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, 44.25, -109.5, 49, 45, 1, 1968503.937, 0},
    {6516, "NAD83(2011) / Nebraska", LCC2, D_NAD83_2011, E_GRS80, UM, S(143400), -100, 43, 40, 1, 500000, 0},
    {6518, "NAD83(2011) / Nevada Central", TM, D_NAD83_2011, E_GRS80, UM, 34.75, S(-420000), 0, 0, 0.9999, 500000, 6000000},
    {6519, "NAD83(2011) / Nevada Central (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 34.75, S(-420000), 0, 0, 0.9999, 1640416.6667, 19685000},
    {6520, "NAD83(2011) / Nevada East", TM, D_NAD83_2011, E_GRS80, UM, 34.75, S(-416100), 0, 0, 0.9999, 200000, 8000000},
    {6521, "NAD83(2011) / Nevada East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 34.75, S(-416100), 0, 0, 0.9999, 656166.6667, 26246666.6667},
    {6522, "NAD83(2011) / Nevada West", TM, D_NAD83_2011, E_GRS80, UM, 34.75, S(-426900), 0, 0, 0.9999, 800000, 4000000},
    {6523, "NAD83(2011) / Nevada West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 34.75, S(-426900), 0, 0, 0.9999, 2624666.6667, 13123333.3333},
    {6524, "NAD83(2011) / New Hampshire", TM, D_NAD83_2011, E_GRS80, UM, 42.5, S(-258000), 0, 0, 0.999966667, 300000, 0},
    {6525, "NAD83(2011) / New Hampshire (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 42.5, S(-258000), 0, 0, 0.999966667, 984250, 0},
    {6526, "NAD83(2011) / New Jersey", TM, D_NAD83_2011, E_GRS80, UM, S(139800), -74.5, 0, 0, 0.9999, 150000, 0},
    {6527, "NAD83(2011) / New Jersey (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(139800), -74.5, 0, 0, 0.9999, 492125, 0},
    {6528, "NAD83(2011) / New Mexico Central", TM, D_NAD83_2011, E_GRS80, UM, 31, -106.25, 0, 0, 0.9999, 500000, 0},
    {6529, "NAD83(2011) / New Mexico Central (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 31, -106.25, 0, 0, 0.9999, 1640416.667, 0},
    {6530, "NAD83(2011) / New Mexico East", TM, D_NAD83_2011, E_GRS80, UM, 31, S(-375600), 0, 0, 0.999909091, 165000, 0},
    {6531, "NAD83(2011) / New Mexico East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 31, S(-375600), 0, 0, 0.999909091, 541337.5, 0},
    {6532, "NAD83(2011) / New Mexico West", TM, D_NAD83_2011, E_GRS80, UM, 31, S(-388200), 0, 0, 0.999916667, 830000, 0},
    {6533, "NAD83(2011) / New Mexico West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 31, S(-388200), 0, 0, 0.999916667, 2723091.667, 0},
    {6534, "NAD83(2011) / New York Central", TM, D_NAD83_2011, E_GRS80, UM, 40, S(-275700), 0, 0, 0.9999375, 250000, 0},
    {6535, "NAD83(2011) / New York Central (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40, S(-275700), 0, 0, 0.9999375, 820208.333, 0},
    {6536, "NAD83(2011) / New York East", TM, D_NAD83_2011, E_GRS80, UM, S(139800), -74.5, 0, 0, 0.9999, 150000, 0},
    {6537, "NAD83(2011) / New York East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(139800), -74.5, 0, 0, 0.9999, 492125, 0},
    {6538, "NAD83(2011) / New York Long Island", LCC2, D_NAD83_2011, E_GRS80, UM, S(144600), -74, S(147720), S(146400), 1, 300000, 0},
    {6539, "NAD83(2011) / New York Long Island (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(144600), -74, S(147720), S(146400), 1, 984250, 0},
    {6540, "NAD83(2011) / New York West", TM, D_NAD83_2011, E_GRS80, UM, 40, S(-282900), 0, 0, 0.9999375, 350000, 0},
    {6541, "NAD83(2011) / New York West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40, S(-282900), 0, 0, 0.9999375, 1148291.667, 0},
    {6542, "NAD83(2011) / North Carolina", LCC2, D_NAD83_2011, E_GRS80, UM, 33.75, -79, S(130200), S(123600), 1, 609601.22, 0},
    {6543, "NAD83(2011) / North Carolina (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 33.75, -79, S(130200), S(123600), 1, 2000000, 0},
    {6544, "NAD83(2011) / North Dakota North", LCC2, D_NAD83_2011, E_GRS80, UM, 47, -100.5, S(175440), S(170760), 1, 600000, 0},
    {6545, "NAD83(2011) / North Dakota North (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, 47, -100.5, S(175440), S(170760), 1, 1968503.937, 0},
    {6546, "NAD83(2011) / North Dakota South", LCC2, D_NAD83_2011, E_GRS80, UM, S(164400), -100.5, S(170940), S(166260), 1, 600000, 0},
    {6547, "NAD83(2011) / North Dakota South (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, S(164400), -100.5, S(170940), S(166260), 1, 1968503.937, 0},
    {6548, "NAD83(2011) / Ohio North", LCC2, D_NAD83_2011, E_GRS80, UM, S(142800), -82.5, 41.7, S(145560), 1, 600000, 0},
    {6549, "NAD83(2011) / Ohio North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(142800), -82.5, 41.7, S(145560), 1, 1968500, 0},
    {6550, "NAD83(2011) / Ohio South", LCC2, D_NAD83_2011, E_GRS80, UM, 38, -82.5, S(144120), S(139440), 1, 600000, 0},
    {6551, "NAD83(2011) / Ohio South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 38, -82.5, S(144120), S(139440), 1, 1968500, 0},
    {6552, "NAD83(2011) / Oklahoma North", LCC2, D_NAD83_2011, E_GRS80, UM, 35, -98, S(132360), S(128040), 1, 600000, 0},
    {6553, "NAD83(2011) / Oklahoma North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 35, -98, S(132360), S(128040), 1, 1968500, 0},
    {6554, "NAD83(2011) / Oklahoma South", LCC2, D_NAD83_2011, E_GRS80, UM, S(120000), -98, S(126840), S(122160), 1, 600000, 0},
    {6555, "NAD83(2011) / Oklahoma South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(120000), -98, S(126840), S(122160), 1, 1968500, 0},
    {6556, "NAD83(2011) / Oregon LCC (m)", LCC2, D_NAD83_2011, E_GRS80, UM, 41.75, -120.5, 43, 45.5, 1, 400000, 0},
    {6557, "NAD83(2011) / Oregon GIC Lambert (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, 41.75, -120.5, 43, 45.5, 1, 1312335.958, 0},
    {6558, "NAD83(2011) / Oregon North", LCC2, D_NAD83_2011, E_GRS80, UM, S(157200), -120.5, 46, S(159600), 1, 2500000, 0},
    {6559, "NAD83(2011) / Oregon North (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, S(157200), -120.5, 46, S(159600), 1, 8202099.738, 0},
    {6560, "NAD83(2011) / Oregon South", LCC2, D_NAD83_2011, E_GRS80, UM, S(150000), -120.5, 44, S(152400), 1, 1500000, 0},
    {6561, "NAD83(2011) / Oregon South (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, S(150000), -120.5, 44, S(152400), 1, 4921259.843, 0},
    {6562, "NAD83(2011) / Pennsylvania North", LCC2, D_NAD83_2011, E_GRS80, UM, S(144600), -77.75, 41.95, S(147180), 1, 600000, 0},
    {6563, "NAD83(2011) / Pennsylvania North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(144600), -77.75, 41.95, S(147180), 1, 1968500, 0},
    {6564, "NAD83(2011) / Pennsylvania South", LCC2, D_NAD83_2011, E_GRS80, UM, S(141600), -77.75, S(147480), S(143760), 1, 600000, 0},
    {6565, "NAD83(2011) / Pennsylvania South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(141600), -77.75, S(147480), S(143760), 1, 1968500, 0},
    {6566, "NAD83(2011) / Puerto Rico and Virgin Is.", LCC2, D_NAD83_2011, E_GRS80, UM, S(64200), S(-239160), S(66360), S(64920), 1, 200000, 200000},
    {6567, "NAD83(2011) / Rhode Island", TM, D_NAD83_2011, E_GRS80, UM, S(147900), -71.5, 0, 0, 0.99999375, 100000, 0},
    {6568, "NAD83(2011) / Rhode Island (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(147900), -71.5, 0, 0, 0.99999375, 328083.3333, 0},
    {6569, "NAD83(2011) / South Carolina", LCC2, D_NAD83_2011, E_GRS80, UM, S(114600), -81, S(125400), 32.5, 1, 609600, 0},
    {6570, "NAD83(2011) / South Carolina (ft)", LCC2, D_NAD83_2011, E_GRS80, UFT, S(114600), -81, S(125400), 32.5, 1, 2000000, 0},
    {6571, "NAD83(2011) / South Dakota North", LCC2, D_NAD83_2011, E_GRS80, UM, S(157800), -100, S(164460), S(159900), 1, 600000, 0},
    {6572, "NAD83(2011) / South Dakota North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(157800), -100, S(164460), S(159900), 1, 1968500, 0},
    {6573, "NAD83(2011) / South Dakota South", LCC2, D_NAD83_2011, E_GRS80, UM, S(152400), S(-361200), 44.4, S(154200), 1, 600000, 0},
    {6574, "NAD83(2011) / South Dakota South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(152400), S(-361200), 44.4, S(154200), 1, 1968500, 0},
    {6575, "NAD83(2011) / Tennessee", LCC2, D_NAD83_2011, E_GRS80, UM, S(123600), -86, S(131100), 35.25, 1, 600000, 0},
    {6576, "NAD83(2011) / Tennessee (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(123600), -86, S(131100), 35.25, 1, 1968500, 0},
    {6577, "NAD83(2011) / Texas Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(106800), S(-361200), S(114780), S(108420), 1, 700000, 3000000},
    {6578, "NAD83(2011) / Texas Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(106800), S(-361200), S(114780), S(108420), 1, 2296583.333, 9842500},
    {6579, "NAD83(2011) / Texas Centric Albers Equal Area", AEA, D_NAD83_2011, E_GRS80, UM, 18, -100, 27.5, 35, 1, 1500000, 6000000},
    {6580, "NAD83(2011) / Texas Centric Lambert Conformal", LCC2, D_NAD83_2011, E_GRS80, UM, 18, -100, 27.5, 35, 1, 1500000, 5000000},
    {6581, "NAD83(2011) / Texas North", LCC2, D_NAD83_2011, E_GRS80, UM, 34, -101.5, S(130260), 34.65, 1, 200000, 1000000},
    {6582, "NAD83(2011) / Texas North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 34, -101.5, S(130260), 34.65, 1, 656166.667, 3280833.333},
    {6583, "NAD83(2011) / Texas North Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(114000), -98.5, S(122280), S(115680), 1, 600000, 2000000},
    {6584, "NAD83(2011) / Texas North Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(114000), -98.5, S(122280), S(115680), 1, 1968500, 6561666.667},
    {6585, "NAD83(2011) / Texas South", LCC2, D_NAD83_2011, E_GRS80, UM, S(92400), -98.5, S(100200), S(94200), 1, 300000, 5000000},
    {6586, "NAD83(2011) / Texas South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(92400), -98.5, S(100200), S(94200), 1, 984250, 16404166.667},
    {6587, "NAD83(2011) / Texas South Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(100200), -99, S(109020), S(102180), 1, 600000, 4000000},
    {6588, "NAD83(2011) / Texas South Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(100200), -99, S(109020), S(102180), 1, 1968500, 13123333.333},
    {6589, "NAD83(2011) / Vermont", TM, D_NAD83_2011, E_GRS80, UM, 42.5, -72.5, 0, 0, 0.999964286, 500000, 0},
    {6590, "NAD83(2011) / Vermont (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 42.5, -72.5, 0, 0, 0.999964286, 1640416.6667, 0},
    {6591, "NAD83(2011) / Virginia Lambert", LCC2, D_NAD83_2011, E_GRS80, UM, 36, -79.5, 37, 39.5, 1, 0, 0},
    {6592, "NAD83(2011) / Virginia North", LCC2, D_NAD83_2011, E_GRS80, UM, S(135600), -78.5, 39.2, S(136920), 1, 3500000, 2000000},
    {6593, "NAD83(2011) / Virginia North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(135600), -78.5, 39.2, S(136920), 1, 11482916.667, 6561666.667},
    {6594, "NAD83(2011) / Virginia South", LCC2, D_NAD83_2011, E_GRS80, UM, S(130800), -78.5, S(136680), S(132360), 1, 3500000, 1000000},
    {6595, "NAD83(2011) / Virginia South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(130800), -78.5, S(136680), S(132360), 1, 11482916.667, 3280833.333},
    {6596, "NAD83(2011) / Washington North", LCC2, D_NAD83_2011, E_GRS80, UM, 47, S(-435000), S(175440), 47.5, 1, 500000, 0},
    {6597, "NAD83(2011) / Washington North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 47, S(-435000), S(175440), 47.5, 1, 1640416.667, 0},
    {6598, "NAD83(2011) / Washington South", LCC2, D_NAD83_2011, E_GRS80, UM, S(163200), -120.5, S(170400), S(165000), 1, 500000, 0},
    {6599, "NAD83(2011) / Washington South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(163200), -120.5, S(170400), S(165000), 1, 1640416.667, 0},
    {6600, "NAD83(2011) / West Virginia North", LCC2, D_NAD83_2011, E_GRS80, UM, 38.5, -79.5, 40.25, 39, 1, 600000, 0},
    {6601, "NAD83(2011) / West Virginia North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 38.5, -79.5, 40.25, 39, 1, 1968500, 0},
    {6602, "NAD83(2011) / West Virginia South", LCC2, D_NAD83_2011, E_GRS80, UM, 37, -81, S(139980), S(134940), 1, 600000, 0},
    {6603, "NAD83(2011) / West Virginia South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 37, -81, S(139980), S(134940), 1, 1968500, 0},
    {6605, "NAD83(2011) / Wisconsin Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(157800), -90, 45.5, 44.25, 1, 1968500, 0},
    {6606, "NAD83(2011) / Wisconsin North", LCC2, D_NAD83_2011, E_GRS80, UM, S(162600), -90, S(168360), S(164040), 1, 600000, 0},
    {6607, "NAD83(2011) / Wisconsin North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(162600), -90, S(168360), S(164040), 1, 1968500, 0},
    {6608, "NAD83(2011) / Wisconsin South", LCC2, D_NAD83_2011, E_GRS80, UM, 42, -90, S(158640), S(153840), 1, 600000, 0},
    {6609, "NAD83(2011) / Wisconsin South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 42, -90, S(158640), S(153840), 1, 1968500, 0},
    {6610, "NAD83(2011) / Wisconsin Transverse Mercator", TM, D_NAD83_2011, E_GRS80, UM, 0, -90, 0, 0, 0.9996, 520000, -4480000},
    {6611, "NAD83(2011) / Wyoming East", TM, D_NAD83_2011, E_GRS80, UM, 40.5, S(-378600), 0, 0, 0.9999375, 200000, 0},
    {6612, "NAD83(2011) / Wyoming East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.5, S(-378600), 0, 0, 0.9999375, 656166.6667, 0},
    {6613, "NAD83(2011) / Wyoming East Central", TM, D_NAD83_2011, E_GRS80, UM, 40.5, S(-386400), 0, 0, 0.9999375, 400000, 100000},
    {6614, "NAD83(2011) / Wyoming East Central (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.5, S(-386400), 0, 0, 0.9999375, 1312333.3333, 328083.3333},
    {6615, "NAD83(2011) / Wyoming West", TM, D_NAD83_2011, E_GRS80, UM, 40.5, S(-396300), 0, 0, 0.9999375, 800000, 100000},
    {6616, "NAD83(2011) / Wyoming West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.5, S(-396300), 0, 0, 0.9999375, 2624666.6667, 328083.3333},
    {6617, "NAD83(2011) / Wyoming West Central", TM, D_NAD83_2011, E_GRS80, UM, 40.5, -108.75, 0, 0, 0.9999375, 600000, 0},
    {6618, "NAD83(2011) / Wyoming West Central (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.5, -108.75, 0, 0, 0.9999375, 1968500, 0},
    {6619, "NAD83(2011) / Utah Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(138000), -111.5, 40.65, S(140460), 1, 500000, 2000000},
    {6620, "NAD83(2011) / Utah North", LCC2, D_NAD83_2011, E_GRS80, UM, S(145200), -111.5, S(150420), S(146580), 1, 500000, 1000000},
    {6621, "NAD83(2011) / Utah South", LCC2, D_NAD83_2011, E_GRS80, UM, S(132000), -111.5, 38.35, S(133980), 1, 500000, 3000000},
    {6623, "NAD83 / Quebec Albers", AEA, D_NAD83, E_GRS80, UM, 44, -68.5, 60, 46, 1, 0, 0},
    {6625, "NAD83(2011) / Utah Central (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(138000), -111.5, 40.65, S(140460), 1, 1640416.6667, 6561666.6667},
    {6626, "NAD83(2011) / Utah North (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(145200), -111.5, S(150420), S(146580), 1, 1640416.6667, 3280833.3333},
    {6627, "NAD83(2011) / Utah South (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(132000), -111.5, 38.35, S(133980), 1, 1640416.6667, 9842500},
    {6669, "JGD2011 / Japan Plane Rectangular CS I", TM, D_JGD2011, E_GRS80, UM, 33, 129.5, 0, 0, 0.9999, 0, 0},
    {6670, "JGD2011 / Japan Plane Rectangular CS II", TM, D_JGD2011, E_GRS80, UM, 33, 131, 0, 0, 0.9999, 0, 0},
    {6671, "JGD2011 / Japan Plane Rectangular CS III", TM, D_JGD2011, E_GRS80, UM, 36, S(475800), 0, 0, 0.9999, 0, 0},
    {6672, "JGD2011 / Japan Plane Rectangular CS IV", TM, D_JGD2011, E_GRS80, UM, 33, 133.5, 0, 0, 0.9999, 0, 0},
    {6673, "JGD2011 / Japan Plane Rectangular CS V", TM, D_JGD2011, E_GRS80, UM, 36, S(483600), 0, 0, 0.9999, 0, 0},
    {6674, "JGD2011 / Japan Plane Rectangular CS VI", TM, D_JGD2011, E_GRS80, UM, 36, 136, 0, 0, 0.9999, 0, 0},
    {6675, "JGD2011 / Japan Plane Rectangular CS VII", TM, D_JGD2011, E_GRS80, UM, 36, S(493800), 0, 0, 0.9999, 0, 0},
    {6676, "JGD2011 / Japan Plane Rectangular CS VIII", TM, D_JGD2011, E_GRS80, UM, 36, 138.5, 0, 0, 0.9999, 0, 0},
    {6677, "JGD2011 / Japan Plane Rectangular CS IX", TM, D_JGD2011, E_GRS80, UM, 36, S(503400), 0, 0, 0.9999, 0, 0},
    {6678, "JGD2011 / Japan Plane Rectangular CS X", TM, D_JGD2011, E_GRS80, UM, 40, S(507000), 0, 0, 0.9999, 0, 0},
    {6679, "JGD2011 / Japan Plane Rectangular CS XI", TM, D_JGD2011, E_GRS80, UM, 44, 140.25, 0, 0, 0.9999, 0, 0},
    {6680, "JGD2011 / Japan Plane Rectangular CS XII", TM, D_JGD2011, E_GRS80, UM, 44, 142.25, 0, 0, 0.9999, 0, 0},
    {6681, "JGD2011 / Japan Plane Rectangular CS XIII", TM, D_JGD2011, E_GRS80, UM, 44, 144.25, 0, 0, 0.9999, 0, 0},
    {6682, "JGD2011 / Japan Plane Rectangular CS XIV", TM, D_JGD2011, E_GRS80, UM, 26, 142, 0, 0, 0.9999, 0, 0},
    {6683, "JGD2011 / Japan Plane Rectangular CS XV", TM, D_JGD2011, E_GRS80, UM, 26, 127.5, 0, 0, 0.9999, 0, 0},
    {6684, "JGD2011 / Japan Plane Rectangular CS XVI", TM, D_JGD2011, E_GRS80, UM, 26, 124, 0, 0, 0.9999, 0, 0},
    {6685, "JGD2011 / Japan Plane Rectangular CS XVII", TM, D_JGD2011, E_GRS80, UM, 26, 131, 0, 0, 0.9999, 0, 0},
    {6686, "JGD2011 / Japan Plane Rectangular CS XVIII", TM, D_JGD2011, E_GRS80, UM, 20, 136, 0, 0, 0.9999, 0, 0},
    {6687, "JGD2011 / Japan Plane Rectangular CS XIX", TM, D_JGD2011, E_GRS80, UM, 26, 154, 0, 0, 0.9999, 0, 0},
    {6688, "JGD2011 / UTM zone 51N", TM, D_JGD2011, E_GRS80, UM, 0, 123, 0, 0, 0.9996, 500000, 0},
    {6689, "JGD2011 / UTM zone 52N", TM, D_JGD2011, E_GRS80, UM, 0, 129, 0, 0, 0.9996, 500000, 0},
    {6690, "JGD2011 / UTM zone 53N", TM, D_JGD2011, E_GRS80, UM, 0, 135, 0, 0, 0.9996, 500000, 0},
    {6691, "JGD2011 / UTM zone 54N", TM, D_JGD2011, E_GRS80, UM, 0, 141, 0, 0, 0.9996, 500000, 0},
    {6692, "JGD2011 / UTM zone 55N", TM, D_JGD2011, E_GRS80, UM, 0, 147, 0, 0, 0.9996, 500000, 0},
    {6703, "WGS 84 / TM 60 SW", TM, D_WGS84, E_WGS84, UM, 0, -60, 0, 0, 0.9996, 500000, 10000000},
    {6720, "WGS 84 / CIG92", TM, D_WGS84, E_WGS84, UM, 0, 105.625, 0, 0, 1.000024, 50000, 1300000},
    {6721, "GDA94 / CIG94", TM, D_GDA94, E_GRS80, UM, 0, 105.625, 0, 0, 1.00002514, 50000, 1300000},
    {6722, "WGS 84 / CKIG92", TM, D_WGS84, E_WGS84, UM, 0, 96.875, 0, 0, 1, 50000, 1400000},
    {6723, "GDA94 / CKIG94", TM, D_GDA94, E_GRS80, UM, 0, 96.875, 0, 0, 0.99999387, 50000, 1500000},
    {6736, "GDA94 / MGA zone 46", TM, D_GDA94, E_GRS80, UM, 0, 93, 0, 0, 0.9996, 500000, 10000000},
    {6737, "GDA94 / MGA zone 47", TM, D_GDA94, E_GRS80, UM, 0, 99, 0, 0, 0.9996, 500000, 10000000},
    {6738, "GDA94 / MGA zone 59", TM, D_GDA94, E_GRS80, UM, 0, 171, 0, 0, 0.9996, 500000, 10000000},
    {6786, "NAD83(2011) / Oregon Baker zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 44.5, S(-424200), 0, 0, 1.00016, 40000, 0},
    {6787, "NAD83(2011) / Oregon Baker zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 44.5, S(-424200), 0, 0, 1.00016, 131233.5958, 0},
    {6790, "NAD83(2011) / Oregon Bend-Klamath Falls zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.75, -121.75, 0, 0, 1.0002, 80000, 0},
    {6791, "NAD83(2011) / Oregon Bend-Klamath Falls zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 41.75, -121.75, 0, 0, 1.0002, 262467.1916, 0},
    {6794, "NAD83(2011) / Oregon Bend-Redmond-Prineville zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(160800), -121.25, S(160800), S(160800), 1.00012, 80000, 130000},
    {6795, "NAD83(2011) / Oregon Bend-Redmond-Prineville zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(160800), -121.25, S(160800), S(160800), 1.00012, 262467.1916, 426509.1864},
    {6798, "NAD83(2011) / Oregon Bend-Burns zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(157200), -119.75, S(157200), S(157200), 1.0002, 120000, 60000},
    {6799, "NAD83(2011) / Oregon Bend-Burns zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(157200), -119.75, S(157200), S(157200), 1.0002, 393700.7874, 196850.3937},
    {6802, "NAD83(2011) / Oregon Canyonville-Grants Pass zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 42.5, S(-444000), 0, 0, 1.00007, 40000, 0},
    {6803, "NAD83(2011) / Oregon Canyonville-Grants Pass zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 42.5, S(-444000), 0, 0, 1.00007, 131233.5958, 0},
    {6806, "NAD83(2011) / Oregon Columbia River East zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(164400), -120.5, S(164400), S(164400), 1.000008, 150000, 30000},
    {6807, "NAD83(2011) / Oregon Columbia River East zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(164400), -120.5, S(164400), S(164400), 1.000008, 492125.9843, 98425.1969},
    {6814, "NAD83(2011) / Oregon Cottage Grove-Canyonville zone (m)", TM, D_NAD83_2011, E_GRS80, UM, S(154200), S(-444000), 0, 0, 1.000023, 50000, 0},
    {6815, "NAD83(2011) / Oregon Cottage Grove-Canyonville zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, S(154200), S(-444000), 0, 0, 1.000023, 164041.9948, 0},
    {6818, "NAD83(2011) / Oregon Dufur-Madras zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 44.5, -121, 0, 0, 1.00011, 80000, 0},
    {6819, "NAD83(2011) / Oregon Dufur-Madras zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 44.5, -121, 0, 0, 1.00011, 262467.1916, 0},
    {6822, "NAD83(2011) / Oregon Eugene zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 43.75, S(-443400), 0, 0, 1.000015, 50000, 0},
    {6823, "NAD83(2011) / Oregon Eugene zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 43.75, S(-443400), 0, 0, 1.000015, 164041.9948, 0},
    {6826, "NAD83(2011) / Oregon Grants Pass-Ashland zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.75, S(-444000), 0, 0, 1.000043, 50000, 0},
    {6827, "NAD83(2011) / Oregon Grants Pass-Ashland zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 41.75, S(-444000), 0, 0, 1.000043, 164041.9948, 0},
    {6830, "NAD83(2011) / Oregon Gresham-Warm Springs zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 45, S(-440400), 0, 0, 1.00005, 10000, 0},
    {6831, "NAD83(2011) / Oregon Gresham-Warm Springs zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 45, S(-440400), 0, 0, 1.00005, 32808.399, 0},
    {6834, "NAD83(2011) / Oregon La Grande zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 45, -118, 0, 0, 1.00013, 40000, 0},
    {6835, "NAD83(2011) / Oregon La Grande zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 45, -118, 0, 0, 1.00013, 131233.5958, 0},
    {6838, "NAD83(2011) / Oregon Ontario zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 43.25, -117, 0, 0, 1.0001, 80000, 0},
    {6839, "NAD83(2011) / Oregon Ontario zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 43.25, -117, 0, 0, 1.0001, 262467.1916, 0},
    {6846, "NAD83(2011) / Oregon Pendleton zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 45.25, S(-429000), 0, 0, 1.000045, 60000, 0},
    {6847, "NAD83(2011) / Oregon Pendleton zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 45.25, S(-429000), 0, 0, 1.000045, 196850.3937, 0},
    {6850, "NAD83(2011) / Oregon Pendleton-La Grande zone (m)", TM, D_NAD83_2011, E_GRS80, UM, S(162300), S(-426000), 0, 0, 1.000175, 30000, 0},
    {6851, "NAD83(2011) / Oregon Pendleton-La Grande zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, S(162300), S(-426000), 0, 0, 1.000175, 98425.1969, 0},
    {6854, "NAD83(2011) / Oregon Portland zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.5, -122.75, 45.5, 45.5, 1.000002, 100000, 50000},
    {6855, "NAD83(2011) / Oregon Portland zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 45.5, -122.75, 45.5, 45.5, 1.000002, 328083.9895, 164041.9948},
    {6858, "NAD83(2011) / Oregon Salem zone (m)", TM, D_NAD83_2011, E_GRS80, UM, S(159600), S(-443100), 0, 0, 1.00001, 50000, 0},
    {6859, "NAD83(2011) / Oregon Salem zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, S(159600), S(-443100), 0, 0, 1.00001, 164041.9948, 0},
    {6862, "NAD83(2011) / Oregon Santiam Pass zone (m)", TM, D_NAD83_2011, E_GRS80, UM, S(158700), -122.5, 0, 0, 1.000155, 0, 0},
    {6863, "NAD83(2011) / Oregon Santiam Pass zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, S(158700), -122.5, 0, 0, 1.000155, 0, 0},
    {6870, "ETRS89 / Albania TM 2010", TM, D_ETRS89, E_GRS80, UM, 0, 20, 0, 0, 1, 500000, 0},
    {6879, "NAD83(2011) / Wisconsin Central", LCC2, D_NAD83_2011, E_GRS80, UM, S(157800), -90, 45.5, 44.25, 1, 600000, 0},
    {6880, "NAD83(2011) / Nebraska (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, S(143400), -100, 43, 40, 1, 1640416.6667, 0},
    {6922, "NAD83 / Kansas LCC", LCC2, D_NAD83, E_GRS80, UM, 36, -98.25, 39.5, 37.5, 1, 400000, 0},
    {6923, "NAD83 / Kansas LCC (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 36, -98.25, 39.5, 37.5, 1, 1312333.3333, 0},
    {6924, "NAD83(2011) / Kansas LCC", LCC2, D_NAD83_2011, E_GRS80, UM, 36, -98.25, 39.5, 37.5, 1, 400000, 0},
    {6925, "NAD83(2011) / Kansas LCC (ftUS)", LCC2, D_NAD83_2011, E_GRS80, UFTUS, 36, -98.25, 39.5, 37.5, 1, 1312333.3333, 0},
    {6962, "ETRS89 / Albania LCC 2010", LCC2, D_ETRS89, E_GRS80, UM, 41, 20, 39, 43, 1, 0, 0},
    {7057, "NAD83(2011) / IaRCS zone 1", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.2, -95.25, 43.2, 43.2, 1.000052, 11500000, 9600000},
    {7058, "NAD83(2011) / IaRCS zone 2", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(155400), -92.75, S(155400), S(155400), 1.000043, 12500000, 9800000},
    {7059, "NAD83(2011) / IaRCS zone 3", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, -91.2, 0, 0, 1.000035, 13500000, 8300000},
    {7060, "NAD83(2011) / IaRCS zone 4", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(153120), S(-341400), S(153120), S(153120), 1.000045, 14500000, 8600000},
    {7061, "NAD83(2011) / IaRCS zone 5", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 42.65, -92.25, 42.65, 42.65, 1.000032, 15500000, 8900000},
    {7062, "NAD83(2011) / IaRCS zone 6", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, S(-344640), 0, 0, 1.000039, 16500000, 6600000},
    {7063, "NAD83(2011) / IaRCS zone 7", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, S(-340680), 0, 0, 1.000045, 17500000, 6800000},
    {7064, "NAD83(2011) / IaRCS zone 8", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, S(-337380), 0, 0, 1.000033, 18500000, 7000000},
    {7065, "NAD83(2011) / IaRCS zone 9", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, S(-334140), 0, 0, 1.000027, 19500000, 7200000},
    {7066, "NAD83(2011) / IaRCS zone 10", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(150600), S(-330000), S(150600), S(150600), 1.00002, 20500000, 8000000},
    {7067, "NAD83(2011) / IaRCS zone 11", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, S(-325920), 0, 0, 1.000027, 21500000, 7600000},
    {7068, "NAD83(2011) / IaRCS zone 12", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(147300), -93.75, S(147300), S(147300), 1.000037, 22500000, 6200000},
    {7069, "NAD83(2011) / IaRCS zone 13", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, S(-330900), 0, 0, 1.00002, 23500000, 6400000},
    {7070, "NAD83(2011) / IaRCS zone 14", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.25, -91.25, 0, 0, 1.000018, 24500000, 6200000},
    {7109, "NAD83(2011) / RMTCRS St Mary (m)", TM, D_NAD83_2011, E_GRS80, UM, 48.5, -112.5, 0, 0, 1.00016, 150000, 0},
    {7110, "NAD83(2011) / RMTCRS Blackfeet (m)", TM, D_NAD83_2011, E_GRS80, UM, 48, -112.5, 0, 0, 1.00019, 100000, 0},
    {7111, "NAD83(2011) / RMTCRS Milk River (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 48.5, -111, 48.5, 48.5, 1.000145, 150000, 200000},
    {7112, "NAD83(2011) / RMTCRS Fort Belknap (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 48.5, -108.5, 48.5, 48.5, 1.00012, 200000, 150000},
    {7113, "NAD83(2011) / RMTCRS Fort Peck Assiniboine (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(174000), -105.5, S(174000), S(174000), 1.00012, 200000, 100000},
    {7114, "NAD83(2011) / RMTCRS Fort Peck Sioux (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(174000), -105.5, S(174000), S(174000), 1.00009, 100000, 50000},
    {7115, "NAD83(2011) / RMTCRS Crow (m)", TM, D_NAD83_2011, E_GRS80, UM, 44.75, -107.75, 0, 0, 1.000148, 200000, 0},
    {7116, "NAD83(2011) / RMTCRS Bobcat (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 46.25, -111.25, 46.25, 46.25, 1.000185, 100000, 100000},
    {7117, "NAD83(2011) / RMTCRS Billings (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(164820), S(-390300), S(164820), S(164820), 1.0001515, 200000, 50000},
    {7118, "NAD83(2011) / RMTCRS Wind River (m)", TM, D_NAD83_2011, E_GRS80, UM, S(153600), S(-390000), 0, 0, 1.00024, 100000, 0},
    {7119, "NAD83(2011) / RMTCRS St Mary (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 48.5, -112.5, 0, 0, 1.00016, 492125.9843, 0},
    {7120, "NAD83(2011) / RMTCRS Blackfeet (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 48, -112.5, 0, 0, 1.00019, 328083.9895, 0},
    {7121, "NAD83(2011) / RMTCRS Milk River (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 48.5, -111, 48.5, 48.5, 1.000145, 492125.9843, 656167.979},
    {7122, "NAD83(2011) / RMTCRS Fort Belknap (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 48.5, -108.5, 48.5, 48.5, 1.00012, 656167.979, 492125.9843},
    {7123, "NAD83(2011) / RMTCRS Fort Peck Assiniboine (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(174000), -105.5, S(174000), S(174000), 1.00012, 656167.979, 328083.9895},
    {7124, "NAD83(2011) / RMTCRS Fort Peck Sioux (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(174000), -105.5, S(174000), S(174000), 1.00009, 328083.9895, 164041.9938},
    {7125, "NAD83(2011) / RMTCRS Crow (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 44.75, -107.75, 0, 0, 1.000148, 656167.979, 0},
    {7126, "NAD83(2011) / RMTCRS Bobcat (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 46.25, -111.25, 46.25, 46.25, 1.000185, 328083.9895, 328083.9895},
    {7127, "NAD83(2011) / RMTCRS Billings (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(164820), S(-390300), S(164820), S(164820), 1.0001515, 656167.979, 164041.9948},
    {7128, "NAD83(2011) / RMTCRS Wind River (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(153600), S(-390000), 0, 0, 1.00024, 328083.3333, 0},
    {7131, "NAD83(2011) / San Francisco CS13", TM, D_NAD83_2011, E_GRS80, UM, 37.75, -122.45, 0, 0, 1.000007, 48000, 24000},
    {7132, "NAD83(2011) / San Francisco CS13 (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.75, -122.45, 0, 0, 1.000007, 157480, 78740},
    {7257, "NAD83(2011) / InGCS Adams (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.55, -84.95, 0, 0, 1.000034, 240000, 36000},
    {7258, "NAD83(2011) / InGCS Adams (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.55, -84.95, 0, 0, 1.000034, 787400, 118110},
    {7259, "NAD83(2011) / InGCS Allen (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.9, -85.05, 0, 0, 1.000031, 240000, 36000},
    {7260, "NAD83(2011) / InGCS Allen (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.9, -85.05, 0, 0, 1.000031, 787400, 118110},
    {7261, "NAD83(2011) / InGCS Bartholomew (m)", TM, D_NAD83_2011, E_GRS80, UM, 39, -85.85, 0, 0, 1.000026, 240000, 36000},
    {7262, "NAD83(2011) / InGCS Bartholomew (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39, -85.85, 0, 0, 1.000026, 787400, 118110},
    {7263, "NAD83(2011) / InGCS Benton (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.45, -87.3, 0, 0, 1.000029, 240000, 36000},
    {7264, "NAD83(2011) / InGCS Benton (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.45, -87.3, 0, 0, 1.000029, 787400, 118110},
    {7265, "NAD83(2011) / InGCS Blackford-Delaware (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.05, -85.4, 0, 0, 1.000038, 240000, 36000},
    {7266, "NAD83(2011) / InGCS Blackford-Delaware (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.05, -85.4, 0, 0, 1.000038, 787400, 118110},
    {7267, "NAD83(2011) / InGCS Boone-Hendricks (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.6, -86.5, 0, 0, 1.000036, 240000, 36000},
    {7268, "NAD83(2011) / InGCS Boone-Hendricks (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.6, -86.5, 0, 0, 1.000036, 787400, 118110},
    {7269, "NAD83(2011) / InGCS Brown (m)", TM, D_NAD83_2011, E_GRS80, UM, 39, -86.3, 0, 0, 1.00003, 240000, 36000},
    {7270, "NAD83(2011) / InGCS Brown (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39, -86.3, 0, 0, 1.00003, 787400, 118110},
    {7271, "NAD83(2011) / InGCS Carroll (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.4, -86.65, 0, 0, 1.000026, 240000, 36000},
    {7272, "NAD83(2011) / InGCS Carroll (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.4, -86.65, 0, 0, 1.000026, 787400, 118110},
    {7273, "NAD83(2011) / InGCS Cass (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.55, -86.4, 0, 0, 1.000028, 240000, 36000},
    {7274, "NAD83(2011) / InGCS Cass (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.55, -86.4, 0, 0, 1.000028, 787400, 118110},
    {7275, "NAD83(2011) / InGCS Clark-Floyd-Scott (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.15, -85.6, 0, 0, 1.000021, 240000, 36000},
    {7276, "NAD83(2011) / InGCS Clark-Floyd-Scott (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.15, -85.6, 0, 0, 1.000021, 787400, 118110},
    {7277, "NAD83(2011) / InGCS Clay (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.15, -87.15, 0, 0, 1.000024, 240000, 36000},
    {7278, "NAD83(2011) / InGCS Clay (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.15, -87.15, 0, 0, 1.000024, 787400, 118110},
    {7279, "NAD83(2011) / InGCS Clinton (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.15, -86.6, 0, 0, 1.000032, 240000, 36000},
    {7280, "NAD83(2011) / InGCS Clinton (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.15, -86.6, 0, 0, 1.000032, 787400, 118110},
    {7281, "NAD83(2011) / InGCS Crawford-Lawrence-Orange (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.1, -86.5, 0, 0, 1.000025, 240000, 36000},
    {7282, "NAD83(2011) / InGCS Crawford-Lawrence-Orange (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.1, -86.5, 0, 0, 1.000025, 787400, 118110},
    {7283, "NAD83(2011) / InGCS Daviess-Greene (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.45, -87.1, 0, 0, 1.000018, 240000, 36000},
    {7284, "NAD83(2011) / InGCS Daviess-Greene (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.45, -87.1, 0, 0, 1.000018, 787400, 118110},
    {7285, "NAD83(2011) / InGCS Dearborn-Ohio-Switzerland (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.65, -84.9, 0, 0, 1.000029, 240000, 36000},
    {7286, "NAD83(2011) / InGCS Dearborn-Ohio-Switzerland (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.65, -84.9, 0, 0, 1.000029, 787400, 118110},
    {7287, "NAD83(2011) / InGCS Decatur-Rush (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.1, -85.65, 0, 0, 1.000036, 240000, 36000},
    {7288, "NAD83(2011) / InGCS Decatur-Rush (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.1, -85.65, 0, 0, 1.000036, 787400, 118110},
    {7289, "NAD83(2011) / InGCS DeKalb (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.25, -84.95, 0, 0, 1.000036, 240000, 36000},
    {7290, "NAD83(2011) / InGCS DeKalb (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 41.25, -84.95, 0, 0, 1.000036, 787400, 118110},
    {7291, "NAD83(2011) / InGCS Dubois-Martin (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.2, -86.95, 0, 0, 1.00002, 240000, 36000},
    {7292, "NAD83(2011) / InGCS Dubois-Martin (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.2, -86.95, 0, 0, 1.00002, 787400, 118110},
    {7293, "NAD83(2011) / InGCS Elkhart-Kosciusko-Wabash (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.65, -85.85, 0, 0, 1.000033, 240000, 36000},
    {7294, "NAD83(2011) / InGCS Elkhart-Kosciusko-Wabash (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.65, -85.85, 0, 0, 1.000033, 787400, 118110},
    {7295, "NAD83(2011) / InGCS Fayette-Franklin-Union (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.25, -85.05, 0, 0, 1.000038, 240000, 36000},
    {7296, "NAD83(2011) / InGCS Fayette-Franklin-Union (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.25, -85.05, 0, 0, 1.000038, 787400, 118110},
    {7297, "NAD83(2011) / InGCS Fountain-Warren (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.95, -87.3, 0, 0, 1.000025, 240000, 36000},
    {7298, "NAD83(2011) / InGCS Fountain-Warren (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.95, -87.3, 0, 0, 1.000025, 787400, 118110},
    {7299, "NAD83(2011) / InGCS Fulton-Marshall-St. Joseph (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.9, -86.3, 0, 0, 1.000031, 240000, 36000},
    {7300, "NAD83(2011) / InGCS Fulton-Marshall-St. Joseph (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.9, -86.3, 0, 0, 1.000031, 787400, 118110},
    {7301, "NAD83(2011) / InGCS Gibson (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.15, -87.65, 0, 0, 1.000013, 240000, 36000},
    {7302, "NAD83(2011) / InGCS Gibson (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.15, -87.65, 0, 0, 1.000013, 787400, 118110},
    {7303, "NAD83(2011) / InGCS Grant (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.35, -85.7, 0, 0, 1.000034, 240000, 36000},
    {7304, "NAD83(2011) / InGCS Grant (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.35, -85.7, 0, 0, 1.000034, 787400, 118110},
    {7305, "NAD83(2011) / InGCS Hamilton-Tipton (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.9, -86, 0, 0, 1.000034, 240000, 36000},
    {7306, "NAD83(2011) / InGCS Hamilton-Tipton (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.9, -86, 0, 0, 1.000034, 787400, 118110},
    {7307, "NAD83(2011) / InGCS Hancock-Madison (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.65, -85.8, 0, 0, 1.000036, 240000, 36000},
    {7308, "NAD83(2011) / InGCS Hancock-Madison (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.65, -85.8, 0, 0, 1.000036, 787400, 118110},
    {7309, "NAD83(2011) / InGCS Harrison-Washington (m)", TM, D_NAD83_2011, E_GRS80, UM, 37.95, -86.15, 0, 0, 1.000027, 240000, 36000},
    {7310, "NAD83(2011) / InGCS Harrison-Washington (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.95, -86.15, 0, 0, 1.000027, 787400, 118110},
    {7311, "NAD83(2011) / InGCS Henry (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.75, -85.45, 0, 0, 1.000043, 240000, 36000},
    {7312, "NAD83(2011) / InGCS Henry (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.75, -85.45, 0, 0, 1.000043, 787400, 118110},
    {7313, "NAD83(2011) / InGCS Howard-Miami (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.35, -86.15, 0, 0, 1.000031, 240000, 36000},
    {7314, "NAD83(2011) / InGCS Howard-Miami (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.35, -86.15, 0, 0, 1.000031, 787400, 118110},
    {7315, "NAD83(2011) / InGCS Huntington-Whitley (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.65, -85.5, 0, 0, 1.000034, 240000, 36000},
    {7316, "NAD83(2011) / InGCS Huntington-Whitley (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.65, -85.5, 0, 0, 1.000034, 787400, 118110},
    {7317, "NAD83(2011) / InGCS Jackson (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.7, -85.95, 0, 0, 1.000022, 240000, 36000},
    {7318, "NAD83(2011) / InGCS Jackson (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.7, -85.95, 0, 0, 1.000022, 787400, 118110},
    {7319, "NAD83(2011) / InGCS Jasper-Porter (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.7, -87.1, 0, 0, 1.000027, 240000, 36000},
    {7320, "NAD83(2011) / InGCS Jasper-Porter (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.7, -87.1, 0, 0, 1.000027, 787400, 118110},
    {7321, "NAD83(2011) / InGCS Jay (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.3, -85, 0, 0, 1.000038, 240000, 36000},
    {7322, "NAD83(2011) / InGCS Jay (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.3, -85, 0, 0, 1.000038, 787400, 118110},
    {7323, "NAD83(2011) / InGCS Jefferson (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.55, -85.35, 0, 0, 1.000028, 240000, 36000},
    {7324, "NAD83(2011) / InGCS Jefferson (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.55, -85.35, 0, 0, 1.000028, 787400, 118110},
    {7325, "NAD83(2011) / InGCS Jennings (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.8, -85.8, 0, 0, 1.000025, 240000, 36000},
    {7326, "NAD83(2011) / InGCS Jennings (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.8, -85.8, 0, 0, 1.000025, 787400, 118110},
    {7327, "NAD83(2011) / InGCS Johnson-Marion (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.3, -86.15, 0, 0, 1.000031, 240000, 36000},
    {7328, "NAD83(2011) / InGCS Johnson-Marion (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.3, -86.15, 0, 0, 1.000031, 787400, 118110},
    {7329, "NAD83(2011) / InGCS Knox (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.4, -87.45, 0, 0, 1.000015, 240000, 36000},
    {7330, "NAD83(2011) / InGCS Knox (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.4, -87.45, 0, 0, 1.000015, 787400, 118110},
    {7331, "NAD83(2011) / InGCS LaGrange-Noble (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.25, -85.45, 0, 0, 1.000037, 240000, 36000},
    {7332, "NAD83(2011) / InGCS LaGrange-Noble (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 41.25, -85.45, 0, 0, 1.000037, 787400, 118110},
    {7333, "NAD83(2011) / InGCS Lake-Newton (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.7, -87.4, 0, 0, 1.000026, 240000, 36000},
    {7334, "NAD83(2011) / InGCS Lake-Newton (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.7, -87.4, 0, 0, 1.000026, 787400, 118110},
    {7335, "NAD83(2011) / InGCS LaPorte-Pulaski-Starke (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.9, -86.75, 0, 0, 1.000027, 240000, 36000},
    {7336, "NAD83(2011) / InGCS LaPorte-Pulaski-Starke (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.9, -86.75, 0, 0, 1.000027, 787400, 118110},
    {7337, "NAD83(2011) / InGCS Monroe-Morgan (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.95, -86.5, 0, 0, 1.000028, 240000, 36000},
    {7338, "NAD83(2011) / InGCS Monroe-Morgan (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.95, -86.5, 0, 0, 1.000028, 787400, 118110},
    {7339, "NAD83(2011) / InGCS Montgomery-Putnam (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.45, -86.95, 0, 0, 1.000031, 240000, 36000},
    {7340, "NAD83(2011) / InGCS Montgomery-Putnam (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.45, -86.95, 0, 0, 1.000031, 787400, 118110},
    {7341, "NAD83(2011) / InGCS Owen (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.15, -86.9, 0, 0, 1.000026, 240000, 36000},
    {7342, "NAD83(2011) / InGCS Owen (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.15, -86.9, 0, 0, 1.000026, 787400, 118110},
    {7343, "NAD83(2011) / InGCS Parke-Vermillion (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.6, -87.35, 0, 0, 1.000022, 240000, 36000},
    {7344, "NAD83(2011) / InGCS Parke-Vermillion (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.6, -87.35, 0, 0, 1.000022, 787400, 118110},
    {7345, "NAD83(2011) / InGCS Perry (m)", TM, D_NAD83_2011, E_GRS80, UM, 37.8, -86.7, 0, 0, 1.00002, 240000, 36000},
    {7346, "NAD83(2011) / InGCS Perry (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.8, -86.7, 0, 0, 1.00002, 787400, 118110},
    {7347, "NAD83(2011) / InGCS Pike-Warrick (m)", TM, D_NAD83_2011, E_GRS80, UM, 37.85, -87.3, 0, 0, 1.000015, 240000, 36000},
    {7348, "NAD83(2011) / InGCS Pike-Warrick (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.85, -87.3, 0, 0, 1.000015, 787400, 118110},
    {7349, "NAD83(2011) / InGCS Posey (m)", TM, D_NAD83_2011, E_GRS80, UM, 37.75, -87.95, 0, 0, 1.000013, 240000, 36000},
    {7350, "NAD83(2011) / InGCS Posey (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.75, -87.95, 0, 0, 1.000013, 787400, 118110},
    {7351, "NAD83(2011) / InGCS Randolph-Wayne (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.7, -85.05, 0, 0, 1.000044, 240000, 36000},
    {7352, "NAD83(2011) / InGCS Randolph-Wayne (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.7, -85.05, 0, 0, 1.000044, 787400, 118110},
    {7353, "NAD83(2011) / InGCS Ripley (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.9, -85.3, 0, 0, 1.000038, 240000, 36000},
    {7354, "NAD83(2011) / InGCS Ripley (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.9, -85.3, 0, 0, 1.000038, 787400, 118110},
    {7355, "NAD83(2011) / InGCS Shelby (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.3, -85.9, 0, 0, 1.00003, 240000, 36000},
    {7356, "NAD83(2011) / InGCS Shelby (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.3, -85.9, 0, 0, 1.00003, 787400, 118110},
    {7357, "NAD83(2011) / InGCS Spencer (m)", TM, D_NAD83_2011, E_GRS80, UM, 37.75, -87.05, 0, 0, 1.000014, 240000, 36000},
    {7358, "NAD83(2011) / InGCS Spencer (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.75, -87.05, 0, 0, 1.000014, 787400, 118110},
    {7359, "NAD83(2011) / InGCS Steuben (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.5, -85, 0, 0, 1.000041, 240000, 36000},
    {7360, "NAD83(2011) / InGCS Steuben (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 41.5, -85, 0, 0, 1.000041, 787400, 118110},
    {7361, "NAD83(2011) / InGCS Sullivan (m)", TM, D_NAD83_2011, E_GRS80, UM, 38.9, -87.5, 0, 0, 1.000017, 240000, 36000},
    {7362, "NAD83(2011) / InGCS Sullivan (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 38.9, -87.5, 0, 0, 1.000017, 787400, 118110},
    {7363, "NAD83(2011) / InGCS Tippecanoe-White (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.2, -86.9, 0, 0, 1.000026, 240000, 36000},
    {7364, "NAD83(2011) / InGCS Tippecanoe-White (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.2, -86.9, 0, 0, 1.000026, 787400, 118110},
    {7365, "NAD83(2011) / InGCS Vanderburgh (m)", TM, D_NAD83_2011, E_GRS80, UM, 37.8, -87.55, 0, 0, 1.000015, 240000, 36000},
    {7366, "NAD83(2011) / InGCS Vanderburgh (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.8, -87.55, 0, 0, 1.000015, 787400, 118110},
    {7367, "NAD83(2011) / InGCS Vigo (m)", TM, D_NAD83_2011, E_GRS80, UM, 39.25, -87.45, 0, 0, 1.00002, 240000, 36000},
    {7368, "NAD83(2011) / InGCS Vigo (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 39.25, -87.45, 0, 0, 1.00002, 787400, 118110},
    {7369, "NAD83(2011) / InGCS Wells (m)", TM, D_NAD83_2011, E_GRS80, UM, 40.55, -85.25, 0, 0, 1.000034, 240000, 36000},
    {7370, "NAD83(2011) / InGCS Wells (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.55, -85.25, 0, 0, 1.000034, 787400, 118110},
    {7528, "NAD83(2011) / WISCRS Adams and Juneau (m)", TM, D_NAD83_2011, E_GRS80, UM, S(156120), -90, 0, 0, 1.0000365285, 147218.6942, 0.0037},
    {7529, "NAD83(2011) / WISCRS Ashland (m)", TM, D_NAD83_2011, E_GRS80, UM, S(164542), S(-326240), 0, 0, 1.0000495683, 172821.9461, 0.0017},
    {7530, "NAD83(2011) / WISCRS Barron (m)", TM, D_NAD83_2011, E_GRS80, UM, S(162480), -91.85, 0, 0, 1.0000486665, 93150, 0.0029},
    {7531, "NAD83(2011) / WISCRS Bayfield (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 46.6696483772222, S(-328150), 46.6696483772222, 46.6696483772222, 1.0000331195, 228600.4575, 148551.4837},
    {7532, "NAD83(2011) / WISCRS Brown (m)", TM, D_NAD83_2011, E_GRS80, UM, 43, -88, 0, 0, 1.00002, 31600, 4600},
    {7533, "NAD83(2011) / WISCRS Buffalo (m)", TM, D_NAD83_2011, E_GRS80, UM, S(156533), S(-330470), 0, 0, 1.0000382778, 175260.3502, 0.0048},
    {7534, "NAD83(2011) / WISCRS Burnett (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.8987148658333, S(-332848), 45.8987148658333, 45.8987148658333, 1.0000383841, 64008.1276, 59445.9043},
    {7535, "NAD83(2011) / WISCRS Calumet, Fond du Lac, Outagamie and Winnebago (m)", TM, D_NAD83_2011, E_GRS80, UM, S(153790), -88.5, 0, 0, 1.0000286569, 244754.8893, 0.0049},
    {7536, "NAD83(2011) / WISCRS Chippewa (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.9778568986111, S(-328660), 44.9778568986111, 44.9778568986111, 1.0000391127, 60045.72, 44091.4346},
    {7537, "NAD83(2011) / WISCRS Clark (m)", TM, D_NAD83_2011, E_GRS80, UM, 43.6, S(-326550), 0, 0, 1.0000463003, 199949.1989, 0.0086},
    {7538, "NAD83(2011) / WISCRS Columbia (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.4625466458333, S(-321820), 43.4625466458333, 43.4625466458333, 1.00003498, 169164.3381, 111569.6134},
    {7539, "NAD83(2011) / WISCRS Crawford (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.200055605, S(-327380), 43.200055605, 43.200055605, 1.0000349151, 113690.6274, 53703.1201},
    {7540, "NAD83(2011) / WISCRS Dane (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.0695160375, S(-321920), 43.0695160375, 43.0695160375, 1.0000384786, 247193.2944, 146591.9896},
    {7541, "NAD83(2011) / WISCRS Dodge and Jefferson (m)", TM, D_NAD83_2011, E_GRS80, UM, S(149300), -88.775, 0, 0, 1.0000346418, 263347.7263, 0.0076},
    {7542, "NAD83(2011) / WISCRS Door (m)", TM, D_NAD83_2011, E_GRS80, UM, 44.4, S(-314180), 0, 0, 1.0000187521, 158801.1176, 0.0023},
    {7543, "NAD83(2011) / WISCRS Douglas (m)", TM, D_NAD83_2011, E_GRS80, UM, S(165180), S(-330900), 0, 0, 1.0000385418, 59131.3183, 0.0041},
    {7544, "NAD83(2011) / WISCRS Dunn (m)", TM, D_NAD83_2011, E_GRS80, UM, S(159870), S(-330820), 0, 0, 1.0000410324, 51816.104, 0.003},
    {7545, "NAD83(2011) / WISCRS Eau Claire (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.8722811263889, S(-328640), 44.8722811263889, 44.8722811263889, 1.000035079, 120091.4402, 91687.9239},
    {7546, "NAD83(2011) / WISCRS Florence (m)", TM, D_NAD83_2011, E_GRS80, UM, S(163580), S(-317310), 0, 0, 1.0000552095, 133502.6683, 0.0063},
    {7547, "NAD83(2011) / WISCRS Forest (m)", TM, D_NAD83_2011, E_GRS80, UM, S(158420), S(-319080), 0, 0, 1.0000673004, 275844.5533, 0.0157},
    {7548, "NAD83(2011) / WISCRS Grant (m)", TM, D_NAD83_2011, E_GRS80, UM, S(149080), -90.8, 0, 0, 1.0000349452, 242316.4841, 0.01},
    {7549, "NAD83(2011) / WISCRS Green and Lafayette (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 42.6375622769444, S(-323420), 42.6375622769444, 42.6375622769444, 1.0000390487, 170078.7403, 45830.2947},
    {7550, "NAD83(2011) / WISCRS Green Lake and Marquette (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.8070001177778, S(-321270), 43.8070001177778, 43.8070001177778, 1.0000344057, 150876.3018, 79170.7795},
    {7551, "NAD83(2011) / WISCRS Iowa (m)", TM, D_NAD83_2011, E_GRS80, UM, S(153140), S(-324580), 0, 0, 1.0000394961, 113081.0261, 0.0045},
    {7552, "NAD83(2011) / WISCRS Iron (m)", TM, D_NAD83_2011, E_GRS80, UM, S(163560), S(-324920), 0, 0, 1.0000677153, 220980.4419, 0.0085},
    {7553, "NAD83(2011) / WISCRS Jackson (m)", TM, D_NAD83_2011, E_GRS80, UM, 44.2533351277778, -90.8442965194444, 0, 0, 1.0000353, 27000, 25000},
    {7554, "NAD83(2011) / WISCRS Kenosha, Milwaukee, Ozaukee and Racine (m)", TM, D_NAD83_2011, E_GRS80, UM, S(151980), S(-316420), 0, 0, 1.0000260649, 185928.3728, 0.0009},
    {7555, "NAD83(2011) / WISCRS Kewaunee, Manitowoc and Sheboygan (m)", TM, D_NAD83_2011, E_GRS80, UM, S(155760), -87.55, 0, 0, 1.0000233704, 79857.7614, 0.0012},
    {7556, "NAD83(2011) / WISCRS La Crosse (m)", TM, D_NAD83_2011, E_GRS80, UM, S(156424), S(-328740), 0, 0, 1.0000319985, 130454.6598, 0.0033},
    {7557, "NAD83(2011) / WISCRS Langlade (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.1542371052778, S(-320520), 45.1542371052778, 45.1542371052778, 1.0000627024, 198425.197, 105279.7829},
    {7558, "NAD83(2011) / WISCRS Lincoln (m)", TM, D_NAD83_2011, E_GRS80, UM, S(161440), S(-323040), 0, 0, 1.0000599003, 116129.0323, 0.0058},
    {7559, "NAD83(2011) / WISCRS Marathon (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.9009044236111, -89.77, 44.9009044236111, 44.9009044236111, 1.000053289, 74676.1493, 55049.2669},
    {7560, "NAD83(2011) / WISCRS Marinette (m)", TM, D_NAD83_2011, E_GRS80, UM, S(160890), S(-315760), 0, 0, 1.0000234982, 238658.8794, 0.0032},
    {7561, "NAD83(2011) / WISCRS Menominee (m)", TM, D_NAD83_2011, E_GRS80, UM, S(160980), S(-318300), 0, 0, 1.0000362499, 105461.0121, 0.0029},
    {7562, "NAD83(2011) / WISCRS Monroe (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.0000739286111, S(-326310), 44.0000739286111, 44.0000739286111, 1.0000434122, 204521.209, 121923.9861},
    {7563, "NAD83(2011) / WISCRS Oconto (m)", TM, D_NAD83_2011, E_GRS80, UM, S(159830), S(-316470), 0, 0, 1.0000236869, 182880.3676, 0.0033},
    {7564, "NAD83(2011) / WISCRS Oneida (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.7042237702778, S(-322360), 45.7042237702778, 45.7042237702778, 1.0000686968, 70104.1401, 57588.0346},
    {7565, "NAD83(2011) / WISCRS Pepin and Pierce (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.6361488719444, S(-332020), 44.6361488719444, 44.6361488719444, 1.0000362977, 167640.3354, 86033.0876},
    {7566, "NAD83(2011) / WISCRS Polk (m)", TM, D_NAD83_2011, E_GRS80, UM, S(160780), S(-333480), 0, 0, 1.0000433849, 141732.2823, 0.0059},
    {7567, "NAD83(2011) / WISCRS Portage (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.4168239752778, -89.5, 44.4168239752778, 44.4168239752778, 1.000039936, 56388.1128, 50022.1874},
    {7568, "NAD83(2011) / WISCRS Price (m)", TM, D_NAD83_2011, E_GRS80, UM, S(160400), S(-325760), 0, 0, 1.0000649554, 227990.8546, 0.0109},
    {7569, "NAD83(2011) / WISCRS Richland (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.3223129275, S(-325550), 43.3223129275, 43.3223129275, 1.0000375653, 202387.6048, 134255.4253},
    {7570, "NAD83(2011) / WISCRS Rock (m)", TM, D_NAD83_2011, E_GRS80, UM, S(151000), S(-320660), 0, 0, 1.0000337311, 146304.2926, 0.0068},
    {7571, "NAD83(2011) / WISCRS Rusk (m)", TM, D_NAD83_2011, E_GRS80, UM, S(158110), S(-327840), 0, 0, 1.0000495976, 250546.1013, 0.0234},
    {7572, "NAD83(2011) / WISCRS Sauk (m)", TM, D_NAD83_2011, E_GRS80, UM, S(154150), -89.9, 0, 0, 1.0000373868, 185623.5716, 0.0051},
    {7573, "NAD83(2011) / WISCRS Sawyer (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.9000991313889, S(-328020), 45.9000991313889, 45.9000991313889, 1.0000573461, 216713.2336, 120734.1631},
    {7574, "NAD83(2011) / WISCRS Shawano (m)", TM, D_NAD83_2011, E_GRS80, UM, S(158530), S(-318980), 0, 0, 1.000032144, 262433.3253, 0.0096},
    {7575, "NAD83(2011) / WISCRS St. Croix (m)", TM, D_NAD83_2011, E_GRS80, UM, S(158530), S(-333480), 0, 0, 1.0000381803, 165506.7302, 0.0103},
    {7576, "NAD83(2011) / WISCRS Taylor (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.1778220858333, S(-325740), 45.1778220858333, 45.1778220858333, 1.0000597566, 187147.5744, 107746.7522},
    {7577, "NAD83(2011) / WISCRS Trempealeau (m)", TM, D_NAD83_2011, E_GRS80, UM, S(155380), S(-328920), 0, 0, 1.0000361538, 256946.9138, 0.0041},
    {7578, "NAD83(2011) / WISCRS Vernon (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.5750329397222, S(-326820), 43.5750329397222, 43.5750329397222, 1.0000408158, 222504.4451, 47532.0602},
    {7579, "NAD83(2011) / WISCRS Vilas (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 46.0778440905556, S(-322160), 46.0778440905556, 46.0778440905556, 1.0000730142, 134417.0689, 50337.1092},
    {7580, "NAD83(2011) / WISCRS Walworth (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 42.6694620969444, S(-318750), 42.6694620969444, 42.6694620969444, 1.0000367192, 232562.8651, 111088.2224},
    {7581, "NAD83(2011) / WISCRS Washburn (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(165460.3914), S(-330420), S(165460.3914), S(165460.3914), 1.0000475376, 234086.8682, 188358.6058},
    {7582, "NAD83(2011) / WISCRS Washington (m)", TM, D_NAD83_2011, E_GRS80, UM, S(154505), S(-317030), 0, 0, 1.00003738, 120091.4415, 0.003},
    {7583, "NAD83(2011) / WISCRS Waukesha (m)", TM, D_NAD83_2011, E_GRS80, UM, S(153250), -88.225, 0, 0, 1.0000346179, 208788.418, 0.0034},
    {7584, "NAD83(2011) / WISCRS Waupaca (m)", TM, D_NAD83_2011, E_GRS80, UM, S(156313), S(-319740), 0, 0, 1.0000333645, 185013.9709, 0.007},
    {7585, "NAD83(2011) / WISCRS Waushara (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.1139440458333, S(-321270), 44.1139440458333, 44.1139440458333, 1.0000392096, 120091.4402, 45069.7587},
    {7586, "NAD83(2011) / WISCRS Wood (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44.3625954694444, -90, 44.3625954694444, 44.3625954694444, 1.0000421209, 208483.6173, 134589.754},
    {7587, "NAD83(2011) / WISCRS Adams and Juneau (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(156120), -90, 0, 0, 1.0000365285, 482999.999, 0.012},
    {7588, "NAD83(2011) / WISCRS Ashland (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(164542), S(-326240), 0, 0, 1.0000495683, 567000.001, 0.006},
    {7589, "NAD83(2011) / WISCRS Barron (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(162480), -91.85, 0, 0, 1.0000486665, 305609.625, 0.01},
    {7590, "NAD83(2011) / WISCRS Bayfield (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 46.6696483772222, S(-328150), 46.6696483772222, 46.6696483772222, 1.0000331195, 750000.001, 487372.659},
    {7591, "NAD83(2011) / WISCRS Brown (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 43, -88, 0, 0, 1.00002, 103674.333, 15091.833},
    {7592, "NAD83(2011) / WISCRS Buffalo (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(156533), S(-330470), 0, 0, 1.0000382778, 574999.999, 0.016},
    {7593, "NAD83(2011) / WISCRS Burnett (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 45.8987148658333, S(-332848), 45.8987148658333, 45.8987148658333, 1.0000383841, 209999.999, 195032.104},
    {7594, "NAD83(2011) / WISCRS Calumet, Fond du Lac, Outagamie and Winnebago (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(153790), -88.5, 0, 0, 1.0000286569, 802999.999, 0.016},
    {7595, "NAD83(2011) / WISCRS Chippewa (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.9778568986111, S(-328660), 44.9778568986111, 44.9778568986111, 1.0000391127, 197000, 144656.648},
    {7596, "NAD83(2011) / WISCRS Clark (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 43.6, S(-326550), 0, 0, 1.0000463003, 655999.997, 0.028},
    {7597, "NAD83(2011) / WISCRS Columbia (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.4625466458333, S(-321820), 43.4625466458333, 43.4625466458333, 1.00003498, 554999.999, 366041.307},
    {7598, "NAD83(2011) / WISCRS Crawford (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.200055605, S(-327380), 43.200055605, 43.200055605, 1.0000349151, 373000, 176190.987},
    {7599, "NAD83(2011) / WISCRS Dane (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.0695160375, S(-321920), 43.0695160375, 43.0695160375, 1.0000384786, 811000, 480943.886},
    {7600, "NAD83(2011) / WISCRS Dodge and Jefferson (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(149300), -88.775, 0, 0, 1.0000346418, 863999.999, 0.025},
    {7601, "NAD83(2011) / WISCRS Door (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 44.4, S(-314180), 0, 0, 1.0000187521, 521000, 0.008},
    {7602, "NAD83(2011) / WISCRS Douglas (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(165180), S(-330900), 0, 0, 1.0000385418, 194000, 0.013},
    {7603, "NAD83(2011) / WISCRS Dunn (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(159870), S(-330820), 0, 0, 1.0000410324, 170000.001, 0.01},
    {7604, "NAD83(2011) / WISCRS Eau Claire (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.8722811263889, S(-328640), 44.8722811263889, 44.8722811263889, 1.000035079, 394000, 300812.797},
    {7605, "NAD83(2011) / WISCRS Florence (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(163580), S(-317310), 0, 0, 1.0000552095, 438000.004, 0.021},
    {7606, "NAD83(2011) / WISCRS Forest (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(158420), S(-319080), 0, 0, 1.0000673004, 905000.005, 0.052},
    {7607, "NAD83(2011) / WISCRS Grant (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(149080), -90.8, 0, 0, 1.0000349452, 794999.998, 0.033},
    {7608, "NAD83(2011) / WISCRS Green and Lafayette (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 42.6375622769444, S(-323420), 42.6375622769444, 42.6375622769444, 1.0000390487, 558000, 150361.559},
    {7609, "NAD83(2011) / WISCRS Green Lake and Marquette (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.8070001177778, S(-321270), 43.8070001177778, 43.8070001177778, 1.0000344057, 495000, 259746.132},
    {7610, "NAD83(2011) / WISCRS Iowa (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(153140), S(-324580), 0, 0, 1.0000394961, 371000, 0.015},
    {7611, "NAD83(2011) / WISCRS Iron (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(163560), S(-324920), 0, 0, 1.0000677153, 725000, 0.028},
    {7612, "NAD83(2011) / WISCRS Jackson (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 44.2533351277778, -90.8442965194444, 0, 0, 1.0000353, 88582.5, 82020.833},
    {7613, "NAD83(2011) / WISCRS Kenosha, Milwaukee, Ozaukee and Racine (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(151980), S(-316420), 0, 0, 1.0000260649, 610000.003, 0.003},
    {7614, "NAD83(2011) / WISCRS Kewaunee, Manitowoc and Sheboygan (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(155760), -87.55, 0, 0, 1.0000233704, 262000.006, 0.004},
    {7615, "NAD83(2011) / WISCRS La Crosse (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(156424), S(-328740), 0, 0, 1.0000319985, 427999.996, 0.011},
    {7616, "NAD83(2011) / WISCRS Langlade (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 45.1542371052778, S(-320520), 45.1542371052778, 45.1542371052778, 1.0000627024, 651000, 345405.421},
    {7617, "NAD83(2011) / WISCRS Lincoln (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(161440), S(-323040), 0, 0, 1.0000599003, 381000, 0.019},
    {7618, "NAD83(2011) / WISCRS Marathon (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.9009044236111, -89.77, 44.9009044236111, 44.9009044236111, 1.000053289, 245000, 180607.47},
    {7619, "NAD83(2011) / WISCRS Marinette (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(160890), S(-315760), 0, 0, 1.0000234982, 783000.007, 0.01},
    {7620, "NAD83(2011) / WISCRS Menominee (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(160980), S(-318300), 0, 0, 1.0000362499, 346000.004, 0.01},
    {7621, "NAD83(2011) / WISCRS Monroe (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.0000739286111, S(-326310), 44.0000739286111, 44.0000739286111, 1.0000434122, 671000, 400012.278},
    {7622, "NAD83(2011) / WISCRS Oconto (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(159830), S(-316470), 0, 0, 1.0000236869, 600000.006, 0.011},
    {7623, "NAD83(2011) / WISCRS Oneida (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 45.7042237702778, S(-322360), 45.7042237702778, 45.7042237702778, 1.0000686968, 230000, 188936.744},
    {7624, "NAD83(2011) / WISCRS Pepin and Pierce (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.6361488719444, S(-332020), 44.6361488719444, 44.6361488719444, 1.0000362977, 550000, 282260.222},
    {7625, "NAD83(2011) / WISCRS Polk (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(160780), S(-333480), 0, 0, 1.0000433849, 464999.996, 0.019},
    {7626, "NAD83(2011) / WISCRS Portage (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.4168239752778, -89.5, 44.4168239752778, 44.4168239752778, 1.000039936, 185000, 164114.46},
    {7627, "NAD83(2011) / WISCRS Price (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(160400), S(-325760), 0, 0, 1.0000649554, 747999.995, 0.036},
    {7628, "NAD83(2011) / WISCRS Richland (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.3223129275, S(-325550), 43.3223129275, 43.3223129275, 1.0000375653, 664000, 440469.675},
    {7629, "NAD83(2011) / WISCRS Rock (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(151000), S(-320660), 0, 0, 1.0000337311, 480000, 0.022},
    {7630, "NAD83(2011) / WISCRS Rusk (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(158110), S(-327840), 0, 0, 1.0000495976, 822000.001, 0.077},
    {7631, "NAD83(2011) / WISCRS Sauk (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(154150), -89.9, 0, 0, 1.0000373868, 609000.001, 0.017},
    {7632, "NAD83(2011) / WISCRS Sawyer (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 45.9000991313889, S(-328020), 45.9000991313889, 45.9000991313889, 1.0000573461, 711000.001, 396108.667},
    {7633, "NAD83(2011) / WISCRS Shawano (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(158530), S(-318980), 0, 0, 1.000032144, 861000.001, 0.031},
    {7634, "NAD83(2011) / WISCRS St. Croix (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(158530), S(-333480), 0, 0, 1.0000381803, 542999.997, 0.034},
    {7635, "NAD83(2011) / WISCRS Taylor (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 45.1778220858333, S(-325740), 45.1778220858333, 45.1778220858333, 1.0000597566, 614000, 353499.136},
    {7636, "NAD83(2011) / WISCRS Trempealeau (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(155380), S(-328920), 0, 0, 1.0000361538, 843000, 0.013},
    {7637, "NAD83(2011) / WISCRS Vernon (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 43.5750329397222, S(-326820), 43.5750329397222, 43.5750329397222, 1.0000408158, 730000, 155944.768},
    {7638, "NAD83(2011) / WISCRS Vilas (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 46.0778440905556, S(-322160), 46.0778440905556, 46.0778440905556, 1.0000730142, 441000, 165147.666},
    {7639, "NAD83(2011) / WISCRS Walworth (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 42.6694620969444, S(-318750), 42.6694620969444, 42.6694620969444, 1.0000367192, 763000, 364461.943},
    {7640, "NAD83(2011) / WISCRS Washburn (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(165460.3914), S(-330420), S(165460.3914), S(165460.3914), 1.0000475376, 768000, 617973.193},
    {7641, "NAD83(2011) / WISCRS Washington (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(154505), S(-317030), 0, 0, 1.00003738, 394000.004, 0.01},
    {7642, "NAD83(2011) / WISCRS Waukesha (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(153250), -88.225, 0, 0, 1.0000346179, 685000.001, 0.011},
    {7643, "NAD83(2011) / WISCRS Waupaca (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, S(156313), S(-319740), 0, 0, 1.0000333645, 607000.003, 0.023},
    {7644, "NAD83(2011) / WISCRS Waushara (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.1139440458333, S(-321270), 44.1139440458333, 44.1139440458333, 1.0000392096, 394000, 147866.367},
    {7645, "NAD83(2011) / WISCRS Wood (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 44.3625954694444, -90, 44.3625954694444, 44.3625954694444, 1.0000421209, 684000.001, 441566.551},
    {7755, "WGS 84 / India NSF LCC", LCC2, D_WGS84, E_WGS84, UM, 24, 80, 12.472955, S(126622.096), 1, 4000000, 4000000},
    {7756, "WGS 84 / Andhra Pradesh", LCC2, D_WGS84, E_WGS84, UM, 16.25543298, 80.875, 13.75, 18.75, 1, 1000000, 1000000},
    {7757, "WGS 84 / Arunachal Pradesh", LCC2, D_WGS84, E_WGS84, UM, 28.00157897, 94.5, 27, 29, 1, 1000000, 1000000},
    {7758, "WGS 84 / Assam", LCC2, D_WGS84, E_WGS84, UM, 26.00257703, 92.75, S(88800), S(98400), 1, 1000000, 1000000},
    {7759, "WGS 84 / Bihar", LCC2, D_WGS84, E_WGS84, UM, 25.87725247, 85.875, 24.625, 27.125, 1, 1000000, 1000000},
    {7760, "WGS 84 / Delhi", LCC2, D_WGS84, E_WGS84, UM, 28.62510126, 77, 28.375, 28.875, 1, 1000000, 1000000},
    {7761, "WGS 84 / Gujarat", LCC2, D_WGS84, E_WGS84, UM, 22.37807121, 71.375, S(74850), S(86250), 1, 1000000, 1000000},
    {7762, "WGS 84 / Haryana", LCC2, D_WGS84, E_WGS84, UM, 29.25226266, 76, S(101100), S(109500), 1, 1000000, 1000000},
    {7763, "WGS 84 / Himachal Pradesh", LCC2, D_WGS84, E_WGS84, UM, 31.75183497, 77.375, 30.75, 32.75, 1, 1000000, 1000000},
    {7764, "WGS 84 / Jammu and Kashmir", LCC2, D_WGS84, E_WGS84, UM, 34.75570874, 76.5, S(119100), S(131100), 1, 1000000, 1000000},
    {7765, "WGS 84 / Jharkhand", LCC2, D_WGS84, E_WGS84, UM, 23.62652682, 85.625, S(81150), S(88950), 1, 1000000, 1000000},
    {7766, "WGS 84 / Madhya Pradesh", LCC2, D_WGS84, E_WGS84, UM, 24.00529821, 78.375, 22, 26, 1, 1000000, 1000000},
    {7767, "WGS 84 / Maharashtra", LCC2, D_WGS84, E_WGS84, UM, 18.88015774, 76.75, 16.625, 21.125, 1, 1000000, 1000000},
    {7768, "WGS 84 / Manipur", LCC2, D_WGS84, E_WGS84, UM, 24.75060911, 94, S(86700), S(91500), 1, 1000000, 1000000},
    {7769, "WGS 84 / Meghalaya", LCC2, D_WGS84, E_WGS84, UM, 25.62524747, 91.375, S(90750), S(93750), 1, 1000000, 1000000},
    {7770, "WGS 84 / Nagaland", LCC2, D_WGS84, E_WGS84, UM, 26.12581974, 94.375, 25.375, 26.875, 1, 1000000, 1000000},
    {7771, "WGS 84 / India Northeast", LCC2, D_WGS84, E_WGS84, UM, 25.63452135, 93.5, S(82950), S(101550), 1, 1000000, 1000000},
    {7772, "WGS 84 / Orissa", LCC2, D_WGS84, E_WGS84, UM, 20.25305174, 84.375, S(66900), S(78900), 1, 1000000, 1000000},
    {7773, "WGS 84 / Punjab", LCC2, D_WGS84, E_WGS84, UM, 31.00178226, 75.375, 30, 32, 1, 1000000, 1000000},
    {7774, "WGS 84 / Rajasthan", LCC2, D_WGS84, E_WGS84, UM, 26.88505546, 73.875, S(87450), S(106050), 1, 1000000, 1000000},
    {7775, "WGS 84 / Uttar Pradesh", LCC2, D_WGS84, E_WGS84, UM, 27.13270823, 80.875, 24.875, 29.375, 1, 1000000, 1000000},
    {7776, "WGS 84 / Uttaranchal", LCC2, D_WGS84, E_WGS84, UM, 30.0017132, 79.375, 29, 31, 1, 1000000, 1000000},
    {7777, "WGS 84 / Andaman and Nicobar", TM, D_WGS84, E_WGS84, UM, 10.25, 93.25, 0, 0, 0.9999428, 1000000, 1000000},
    {7778, "WGS 84 / Chhattisgarh", TM, D_WGS84, E_WGS84, UM, 21, 82.25, 0, 0, 0.9998332, 1000000, 1000000},
    {7779, "WGS 84 / Goa", TM, D_WGS84, E_WGS84, UM, 15.375, 74, 0, 0, 0.9999913, 1000000, 1000000},
    {7780, "WGS 84 / Karnataka", TM, D_WGS84, E_WGS84, UM, 15.125, 76.375, 0, 0, 0.9998012, 1000000, 1000000},
    {7781, "WGS 84 / Kerala", TM, D_WGS84, E_WGS84, UM, 10.5, 76, 0, 0, 0.9999177, 1000000, 1000000},
    {7782, "WGS 84 / Lakshadweep", TM, D_WGS84, E_WGS84, UM, 10, 73.125, 0, 0, 0.9999536, 1000000, 1000000},
    {7783, "WGS 84 / Mizoram", TM, D_WGS84, E_WGS84, UM, 23.125, 92.75, 0, 0, 0.9999821, 1000000, 1000000},
    {7784, "WGS 84 / Sikkim", TM, D_WGS84, E_WGS84, UM, 27.625, 88.5, 0, 0, 0.9999926, 1000000, 1000000},
    {7785, "WGS 84 / Tamil Nadu", TM, D_WGS84, E_WGS84, UM, 10.875, 78.375, 0, 0, 0.9997942, 1000000, 1000000},
    {7786, "WGS 84 / Tripura", TM, D_WGS84, E_WGS84, UM, 23.75, 91.75, 0, 0, 0.9999822, 1000000, 1000000},
    {7787, "WGS 84 / West Bengal", TM, D_WGS84, E_WGS84, UM, 24.375, 87.875, 0, 0, 0.9998584, 1000000, 1000000},
    {7845, "GDA2020 / GA LCC", LCC2, D_GDA2020, E_GRS80, UM, 0, 134, -18, -36, 1, 0, 0},
    {7899, "GDA2020 / Vicgrid", LCC2, D_GDA2020, E_GRS80, UM, -37, 145, -36, -38, 1, 2500000, 2500000},
    {8013, "GDA2020 / ALB2020", TM, D_GDA2020, E_GRS80, UM, 0, S(424380), 0, 0, 1.0000044, 50000, 4100000},
    {8014, "GDA2020 / BIO2020", TM, D_GDA2020, E_GRS80, UM, 0, 115.25, 0, 0, 1.0000022, 60000, 2700000},
    {8015, "GDA2020 / BRO2020", TM, D_GDA2020, E_GRS80, UM, 0, S(440400), 0, 0, 1.00000298, 50000, 2300000},
    {8016, "GDA2020 / BCG2020", TM, D_GDA2020, E_GRS80, UM, 0, S(415560), 0, 0, 0.99999592, 50000, 4000000},
    {8017, "GDA2020 / CARN2020", TM, D_GDA2020, E_GRS80, UM, 0, S(409200), 0, 0, 0.99999796, 50000, 3050000},
    {8018, "GDA2020 / CIG2020", TM, D_GDA2020, E_GRS80, UM, 0, 105.625, 0, 0, 1.00002514, 50000, 1400000},
    {8019, "GDA2020 / CKIG2020", TM, D_GDA2020, E_GRS80, UM, 0, 96.875, 0, 0, 0.99999387, 50000, 1600000},
    {8020, "GDA2020 / COL2020", TM, D_GDA2020, E_GRS80, UM, 0, S(417360), 0, 0, 1.000019, 40000, 4100000},
    {8021, "GDA2020 / ESP2020", TM, D_GDA2020, E_GRS80, UM, 0, S(438780), 0, 0, 1.0000055, 50000, 4050000},
    {8022, "GDA2020 / EXM2020", TM, D_GDA2020, E_GRS80, UM, 0, S(410640), 0, 0, 1.00000236, 50000, 2750000},
    {8023, "GDA2020 / GCG2020", TM, D_GDA2020, E_GRS80, UM, 0, S(412500), 0, 0, 1.00000628, 50000, 3450000},
    {8024, "GDA2020 / GOLD2020", TM, D_GDA2020, E_GRS80, UM, 0, 121.5, 0, 0, 1.00004949, 60000, 3800000},
    {8025, "GDA2020 / JCG2020", TM, D_GDA2020, E_GRS80, UM, 0, S(413940), 0, 0, 1.00000314, 50000, 3650000},
    {8026, "GDA2020 / KALB2020", TM, D_GDA2020, E_GRS80, UM, 0, S(411535), 0, 0, 1.000014, 55000, 3700000},
    {8027, "GDA2020 / KAR2020", TM, D_GDA2020, E_GRS80, UM, 0, S(420960), 0, 0, 0.9999989, 50000, 2550000},
    {8028, "GDA2020 / KUN2020", TM, D_GDA2020, E_GRS80, UM, 0, 128.75, 0, 0, 1.0000165, 50000, 2100000},
    {8029, "GDA2020 / LCG2020", TM, D_GDA2020, E_GRS80, UM, 0, S(415320), 0, 0, 1.00000157, 50000, 3750000},
    {8030, "GDA2020 / MRCG2020", TM, D_GDA2020, E_GRS80, UM, 0, S(414600), 0, 0, 1.0000055, 50000, 4050000},
    {8031, "GDA2020 / PCG2020", TM, D_GDA2020, E_GRS80, UM, 0, S(416940), 0, 0, 0.99999906, 50000, 3900000},
    {8032, "GDA2020 / PHG2020", TM, D_GDA2020, E_GRS80, UM, 0, 118.6, 0, 0, 1.00000135, 50000, 2500000},
    {8035, "WGS 84 / TM Zone 20N (ftUS)", TM, D_WGS84, E_WGS84, UFTUS, 0, -63, 0, 0, 0.9996, 1640416.667, 0},
    {8036, "WGS 84 / TM Zone 21N (ftUS)", TM, D_WGS84, E_WGS84, UFTUS, 0, -57, 0, 0, 0.9996, 1640416.667, 0},
    {8058, "GDA2020 / NSW Lambert", LCC2, D_GDA2020, E_GRS80, UM, -33.25, 147, -30.75, -35.75, 1, 9300000, 4500000},
    {8059, "GDA2020 / SA Lambert", LCC2, D_GDA2020, E_GRS80, UM, -32, 135, -28, -36, 1, 1000000, 2000000},
    {8066, "NAD83(2011) / PCCS zone 2 (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 31.25, S(-403800), 0, 0, 1.00009, 1800000, 1000000},
    {8067, "NAD83(2011) / PCCS zone 3 (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 31.5, S(-407400), 0, 0, 1.000055, 600000, 0},
    {8068, "NAD83(2011) / PCCS zone 4 (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 30.5, -110.75, 30.5, 30.5, 0.9998, 30000, -620000},
    {8090, "NAD83(HARN) / WISCRS Florence (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(163580), S(-317310), 0, 0, 1.0000552095, 133502.6683, 0.0063},
    {8091, "NAD83(HARN) / WISCRS Florence (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(163580), S(-317310), 0, 0, 1.0000552095, 438000.004, 0.021},
    {8092, "NAD83(HARN) / WISCRS Eau Claire (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.8722811263889, S(-328640), 44.8722811263889, 44.8722811263889, 1.000035079, 120091.4402, 91687.9239},
    {8093, "NAD83(HARN) / WISCRS Eau Claire (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.8722811263889, S(-328640), 44.8722811263889, 44.8722811263889, 1.000035079, 394000, 300812.797},
    {8095, "NAD83(HARN) / WISCRS Wood (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.3625954694444, -90, 44.3625954694444, 44.3625954694444, 1.0000421209, 208483.6173, 134589.754},
    {8096, "NAD83(HARN) / WISCRS Wood (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.3625954694444, -90, 44.3625954694444, 44.3625954694444, 1.0000421209, 684000.001, 441566.551},
    {8097, "NAD83(HARN) / WISCRS Waushara (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.1139440458333, S(-321270), 44.1139440458333, 44.1139440458333, 1.0000392096, 120091.4402, 45069.7587},
    {8098, "NAD83(HARN) / WISCRS Waushara (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.1139440458333, S(-321270), 44.1139440458333, 44.1139440458333, 1.0000392096, 394000, 147866.367},
    {8099, "NAD83(HARN) / WISCRS Waupaca (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(156313), S(-319740), 0, 0, 1.0000333645, 185013.9709, 0.007},
    {8100, "NAD83(HARN) / WISCRS Waupaca (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(156313), S(-319740), 0, 0, 1.0000333645, 607000.003, 0.023},
    {8101, "NAD83(HARN) / WISCRS Waukesha (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(153250), -88.225, 0, 0, 1.0000346179, 208788.418, 0.0034},
    {8102, "NAD83(HARN) / WISCRS Waukesha (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(153250), -88.225, 0, 0, 1.0000346179, 685000.001, 0.011},
    {8103, "NAD83(HARN) / WISCRS Washington (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(154505), S(-317030), 0, 0, 1.00003738, 120091.4415, 0.003},
    {8104, "NAD83(HARN) / WISCRS Washington (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(154505), S(-317030), 0, 0, 1.00003738, 394000.004, 0.01},
    {8105, "NAD83(HARN) / WISCRS Washburn (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, S(165460.3914), S(-330420), S(165460.3914), S(165460.3914), 1.0000475376, 234086.8682, 188358.6058},
    {8106, "NAD83(HARN) / WISCRS Washburn (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, S(165460.3914), S(-330420), S(165460.3914), S(165460.3914), 1.0000475376, 768000, 617973.193},
    {8107, "NAD83(HARN) / WISCRS Walworth (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 42.6694620969444, S(-318750), 42.6694620969444, 42.6694620969444, 1.0000367192, 232562.8651, 111088.2224},
    {8108, "NAD83(HARN) / WISCRS Walworth (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 42.6694620969444, S(-318750), 42.6694620969444, 42.6694620969444, 1.0000367192, 763000, 364461.943},
    {8109, "NAD83(HARN) / WISCRS Vilas (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 46.0778440905556, S(-322160), 46.0778440905556, 46.0778440905556, 1.0000730142, 134417.0689, 50337.1092},
    {8110, "NAD83(HARN) / WISCRS Vilas (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 46.0778440905556, S(-322160), 46.0778440905556, 46.0778440905556, 1.0000730142, 441000, 165147.666},
    {8111, "NAD83(HARN) / WISCRS Vernon (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 43.5750329397222, S(-326820), 43.5750329397222, 43.5750329397222, 1.0000408158, 222504.4451, 47532.0602},
    {8112, "NAD83(HARN) / WISCRS Vernon (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 43.5750329397222, S(-326820), 43.5750329397222, 43.5750329397222, 1.0000408158, 730000, 155944.768},
    {8113, "NAD83(HARN) / WISCRS Trempealeau (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(155380), S(-328920), 0, 0, 1.0000361538, 256946.9138, 0.0041},
    {8114, "NAD83(HARN) / WISCRS Trempealeau (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(155380), S(-328920), 0, 0, 1.0000361538, 843000, 0.013},
    {8115, "NAD83(HARN) / WISCRS Taylor (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 45.1778220858333, S(-325740), 45.1778220858333, 45.1778220858333, 1.0000597566, 187147.5744, 107746.7522},
    {8116, "NAD83(HARN) / WISCRS Taylor (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 45.1778220858333, S(-325740), 45.1778220858333, 45.1778220858333, 1.0000597566, 614000, 353499.136},
    {8117, "NAD83(HARN) / WISCRS St. Croix (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(158530), S(-333480), 0, 0, 1.0000381803, 165506.7302, 0.0103},
    {8118, "NAD83(HARN) / WISCRS St. Croix (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(158530), S(-333480), 0, 0, 1.0000381803, 542999.997, 0.034},
    {8119, "NAD83(HARN) / WISCRS Shawano (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(158530), S(-318980), 0, 0, 1.000032144, 262433.3253, 0.0096},
    {8120, "NAD83(HARN) / WISCRS Shawano (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(158530), S(-318980), 0, 0, 1.000032144, 861000.001, 0.031},
    {8121, "NAD83(HARN) / WISCRS Sawyer (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 45.9000991313889, S(-328020), 45.9000991313889, 45.9000991313889, 1.0000573461, 216713.2336, 120734.1631},
    {8122, "NAD83(HARN) / WISCRS Sawyer (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 45.9000991313889, S(-328020), 45.9000991313889, 45.9000991313889, 1.0000573461, 711000.001, 396108.667},
    {8123, "NAD83(HARN) / WISCRS Sauk (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(154150), -89.9, 0, 0, 1.0000373868, 185623.5716, 0.0051},
    {8124, "NAD83(HARN) / WISCRS Sauk (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(154150), -89.9, 0, 0, 1.0000373868, 609000.001, 0.017},
    {8125, "NAD83(HARN) / WISCRS Rusk (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(158110), S(-327840), 0, 0, 1.0000495976, 250546.1013, 0.0234},
    {8126, "NAD83(HARN) / WISCRS Rusk (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(158110), S(-327840), 0, 0, 1.0000495976, 822000.001, 0.077},
    {8127, "NAD83(HARN) / WISCRS Rock (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(151000), S(-320660), 0, 0, 1.0000337311, 146304.2926, 0.0068},
    {8128, "NAD83(HARN) / WISCRS Rock (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(151000), S(-320660), 0, 0, 1.0000337311, 480000, 0.022},
    {8129, "NAD83(HARN) / WISCRS Richland (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 43.3223129275, S(-325550), 43.3223129275, 43.3223129275, 1.0000375653, 202387.6048, 134255.4253},
    {8130, "NAD83(HARN) / WISCRS Richland (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 43.3223129275, S(-325550), 43.3223129275, 43.3223129275, 1.0000375653, 664000, 440469.675},
    {8131, "NAD83(HARN) / WISCRS Price (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(160400), S(-325760), 0, 0, 1.0000649554, 227990.8546, 0.0109},
    {8132, "NAD83(HARN) / WISCRS Price (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(160400), S(-325760), 0, 0, 1.0000649554, 747999.995, 0.036},
    {8133, "NAD83(HARN) / WISCRS Portage (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.4168239752778, -89.5, 44.4168239752778, 44.4168239752778, 1.000039936, 56388.1128, 50022.1874},
    {8134, "NAD83(HARN) / WISCRS Portage (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.4168239752778, -89.5, 44.4168239752778, 44.4168239752778, 1.000039936, 185000, 164114.46},
    {8135, "NAD83(HARN) / WISCRS Polk (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(160780), S(-333480), 0, 0, 1.0000433849, 141732.2823, 0.0059},
    {8136, "NAD83(HARN) / WISCRS Polk (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(160780), S(-333480), 0, 0, 1.0000433849, 464999.996, 0.019},
    {8137, "NAD83(HARN) / WISCRS Pepin and Pierce (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.6361488719444, S(-332020), 44.6361488719444, 44.6361488719444, 1.0000362977, 167640.3354, 86033.0876},
    {8138, "NAD83(HARN) / WISCRS Pepin and Pierce (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.6361488719444, S(-332020), 44.6361488719444, 44.6361488719444, 1.0000362977, 550000, 282260.222},
    {8139, "NAD83(HARN) / WISCRS Oneida (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 45.7042237702778, S(-322360), 45.7042237702778, 45.7042237702778, 1.0000686968, 70104.1401, 57588.0346},
    {8140, "NAD83(HARN) / WISCRS Oneida (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 45.7042237702778, S(-322360), 45.7042237702778, 45.7042237702778, 1.0000686968, 230000, 188936.744},
    {8141, "NAD83(HARN) / WISCRS Oconto (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(159830), S(-316470), 0, 0, 1.0000236869, 182880.3676, 0.0033},
    {8142, "NAD83(HARN) / WISCRS Oconto (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(159830), S(-316470), 0, 0, 1.0000236869, 600000.006, 0.011},
    {8143, "NAD83(HARN) / WISCRS Monroe (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.0000739286111, S(-326310), 44.0000739286111, 44.0000739286111, 1.0000434122, 204521.209, 121923.9861},
    {8144, "NAD83(HARN) / WISCRS Monroe (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.0000739286111, S(-326310), 44.0000739286111, 44.0000739286111, 1.0000434122, 671000, 400012.278},
    {8145, "NAD83(HARN) / WISCRS Menominee (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(160980), S(-318300), 0, 0, 1.0000362499, 105461.0121, 0.0029},
    {8146, "NAD83(HARN) / WISCRS Menominee (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(160980), S(-318300), 0, 0, 1.0000362499, 346000.004, 0.01},
    {8147, "NAD83(HARN) / WISCRS Marinette (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(160890), S(-315760), 0, 0, 1.0000234982, 238658.8794, 0.0032},
    {8148, "NAD83(HARN) / WISCRS Marinette (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(160890), S(-315760), 0, 0, 1.0000234982, 783000.007, 0.01},
    {8149, "NAD83(HARN) / WISCRS Marathon (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.9009044236111, -89.77, 44.9009044236111, 44.9009044236111, 1.000053289, 74676.1493, 55049.2669},
    {8150, "NAD83(HARN) / WISCRS Marathon (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.9009044236111, -89.77, 44.9009044236111, 44.9009044236111, 1.000053289, 245000, 180607.47},
    {8151, "NAD83(HARN) / WISCRS Lincoln (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(161440), S(-323040), 0, 0, 1.0000599003, 116129.0323, 0.0058},
    {8152, "NAD83(HARN) / WISCRS Lincoln (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(161440), S(-323040), 0, 0, 1.0000599003, 381000, 0.019},
    {8153, "NAD83(HARN) / WISCRS Langlade (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 45.1542371052778, S(-320520), 45.1542371052778, 45.1542371052778, 1.0000627024, 198425.197, 105279.7829},
    {8154, "NAD83(HARN) / WISCRS Langlade (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 45.1542371052778, S(-320520), 45.1542371052778, 45.1542371052778, 1.0000627024, 651000, 345405.421},
    {8155, "NAD83(HARN) / WISCRS La Crosse (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(156424), S(-328740), 0, 0, 1.0000319985, 130454.6598, 0.0033},
    {8156, "NAD83(HARN) / WISCRS La Crosse (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(156424), S(-328740), 0, 0, 1.0000319985, 427999.996, 0.011},
    {8157, "NAD83(HARN) / WISCRS Kewaunee, Manitowoc and Sheboygan (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(155760), -87.55, 0, 0, 1.0000233704, 79857.7614, 0.0012},
    {8158, "NAD83(HARN) / WISCRS Kewaunee, Manitowoc and Sheboygan (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(155760), -87.55, 0, 0, 1.0000233704, 262000.006, 0.004},
    {8159, "NAD83(HARN) / WISCRS Kenosha, Milwaukee, Ozaukee and Racine (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(151980), S(-316420), 0, 0, 1.0000260649, 185928.3728, 0.0009},
    {8160, "NAD83(HARN) / WISCRS Kenosha, Milwaukee, Ozaukee and Racine (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(151980), S(-316420), 0, 0, 1.0000260649, 610000.003, 0.003},
    {8161, "NAD83(HARN) / WISCRS Jackson (m)", TM, D_NAD83_HARN, E_GRS80, UM, 44.2533351277778, -90.8442965194444, 0, 0, 1.0000353, 27000, 25000},
    {8162, "NAD83(HARN) / WISCRS Jackson (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 44.2533351277778, -90.8442965194444, 0, 0, 1.0000353, 88582.5, 82020.833},
    {8163, "NAD83(HARN) / WISCRS Iron (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(163560), S(-324920), 0, 0, 1.0000677153, 220980.4419, 0.0085},
    {8164, "NAD83(HARN) / WISCRS Iron (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(163560), S(-324920), 0, 0, 1.0000677153, 725000, 0.028},
    {8165, "NAD83(HARN) / WISCRS Iowa (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(153140), S(-324580), 0, 0, 1.0000394961, 113081.0261, 0.0045},
    {8166, "NAD83(HARN) / WISCRS Iowa (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(153140), S(-324580), 0, 0, 1.0000394961, 371000, 0.015},
    {8167, "NAD83(HARN) / WISCRS Green Lake and Marquette (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 43.8070001177778, S(-321270), 43.8070001177778, 43.8070001177778, 1.0000344057, 150876.3018, 79170.7795},
    {8168, "NAD83(HARN) / WISCRS Green Lake and Marquette (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 43.8070001177778, S(-321270), 43.8070001177778, 43.8070001177778, 1.0000344057, 495000, 259746.132},
    {8169, "NAD83(HARN) / WISCRS Green and Lafayette (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 42.6375622769444, S(-323420), 42.6375622769444, 42.6375622769444, 1.0000390487, 170078.7403, 45830.2947},
    {8170, "NAD83(HARN) / WISCRS Green and Lafayette (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 42.6375622769444, S(-323420), 42.6375622769444, 42.6375622769444, 1.0000390487, 558000, 150361.559},
    {8171, "NAD83(HARN) / WISCRS Grant (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(149080), -90.8, 0, 0, 1.0000349452, 242316.4841, 0.01},
    {8172, "NAD83(HARN) / WISCRS Grant (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(149080), -90.8, 0, 0, 1.0000349452, 794999.998, 0.033},
    {8173, "NAD83(HARN) / WISCRS Forest (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(158420), S(-319080), 0, 0, 1.0000673004, 275844.5533, 0.0157},
    {8177, "NAD83(HARN) / WISCRS Forest (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(158420), S(-319080), 0, 0, 1.0000673004, 905000.005, 0.052},
    {8179, "NAD83(HARN) / WISCRS Dunn (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(159870), S(-330820), 0, 0, 1.0000410324, 51816.104, 0.003},
    {8180, "NAD83(HARN) / WISCRS Dunn (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(159870), S(-330820), 0, 0, 1.0000410324, 170000.001, 0.01},
    {8181, "NAD83(HARN) / WISCRS Douglas (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(165180), S(-330900), 0, 0, 1.0000385418, 59131.3183, 0.0041},
    {8182, "NAD83(HARN) / WISCRS Douglas (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(165180), S(-330900), 0, 0, 1.0000385418, 194000, 0.013},
    {8184, "NAD83(HARN) / WISCRS Door (m)", TM, D_NAD83_HARN, E_GRS80, UM, 44.4, S(-314180), 0, 0, 1.0000187521, 158801.1176, 0.0023},
    {8185, "NAD83(HARN) / WISCRS Door (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 44.4, S(-314180), 0, 0, 1.0000187521, 521000, 0.008},
    {8187, "NAD83(HARN) / WISCRS Dodge and Jefferson (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(149300), -88.775, 0, 0, 1.0000346418, 263347.7263, 0.0076},
    {8189, "NAD83(HARN) / WISCRS Dodge and Jefferson (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(149300), -88.775, 0, 0, 1.0000346418, 863999.999, 0.025},
    {8191, "NAD83(HARN) / WISCRS Dane (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 43.0695160375, S(-321920), 43.0695160375, 43.0695160375, 1.0000384786, 247193.2944, 146591.9896},
    {8193, "NAD83(HARN) / WISCRS Dane (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 43.0695160375, S(-321920), 43.0695160375, 43.0695160375, 1.0000384786, 811000, 480943.886},
    {8196, "NAD83(HARN) / WISCRS Crawford (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 43.200055605, S(-327380), 43.200055605, 43.200055605, 1.0000349151, 113690.6274, 53703.1201},
    {8197, "NAD83(HARN) / WISCRS Crawford (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 43.200055605, S(-327380), 43.200055605, 43.200055605, 1.0000349151, 373000, 176190.987},
    {8198, "NAD83(HARN) / WISCRS Columbia (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 43.4625466458333, S(-321820), 43.4625466458333, 43.4625466458333, 1.00003498, 169164.3381, 111569.6134},
    {8200, "NAD83(HARN) / WISCRS Columbia (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 43.4625466458333, S(-321820), 43.4625466458333, 43.4625466458333, 1.00003498, 554999.999, 366041.307},
    {8201, "NAD83(HARN) / WISCRS Clark (m)", TM, D_NAD83_HARN, E_GRS80, UM, 43.6, S(-326550), 0, 0, 1.0000463003, 199949.1989, 0.0086},
    {8202, "NAD83(HARN) / WISCRS Clark (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 43.6, S(-326550), 0, 0, 1.0000463003, 655999.997, 0.028},
    {8203, "NAD83(HARN) / WISCRS Chippewa (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 44.9778568986111, S(-328660), 44.9778568986111, 44.9778568986111, 1.0000391127, 60045.72, 44091.4346},
    {8204, "NAD83(HARN) / WISCRS Chippewa (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 44.9778568986111, S(-328660), 44.9778568986111, 44.9778568986111, 1.0000391127, 197000, 144656.648},
    {8205, "NAD83(HARN) / WISCRS Calumet, Fond du Lac, Outagamie and Winnebago (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(153790), -88.5, 0, 0, 1.0000286569, 244754.8893, 0.0049},
    {8206, "NAD83(HARN) / WISCRS Calumet, Fond du Lac, Outagamie and Winnebago (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(153790), -88.5, 0, 0, 1.0000286569, 802999.999, 0.016},
    {8207, "NAD83(HARN) / WISCRS Burnett (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 45.8987148658333, S(-332848), 45.8987148658333, 45.8987148658333, 1.0000383841, 64008.1276, 59445.9043},
    {8208, "NAD83(HARN) / WISCRS Burnett (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 45.8987148658333, S(-332848), 45.8987148658333, 45.8987148658333, 1.0000383841, 209999.999, 195032.104},
    {8209, "NAD83(HARN) / WISCRS Buffalo (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(156533), S(-330470), 0, 0, 1.0000382778, 175260.3502, 0.0048},
    {8210, "NAD83(HARN) / WISCRS Buffalo (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(156533), S(-330470), 0, 0, 1.0000382778, 574999.999, 0.016},
    {8212, "NAD83(HARN) / WISCRS Brown (m)", TM, D_NAD83_HARN, E_GRS80, UM, 43, -88, 0, 0, 1.00002, 31600, 4600},
    {8213, "NAD83(HARN) / WISCRS Brown (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, 43, -88, 0, 0, 1.00002, 103674.333, 15091.833},
    {8214, "NAD83(HARN) / WISCRS Bayfield (m)", LCC1, D_NAD83_HARN, E_GRS80, UM, 46.6696483772222, S(-328150), 46.6696483772222, 46.6696483772222, 1.0000331195, 228600.4575, 148551.4837},
    {8216, "NAD83(HARN) / WISCRS Bayfield (ftUS)", LCC1, D_NAD83_HARN, E_GRS80, UFTUS, 46.6696483772222, S(-328150), 46.6696483772222, 46.6696483772222, 1.0000331195, 750000.001, 487372.659},
    {8218, "NAD83(HARN) / WISCRS Barron (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(162480), -91.85, 0, 0, 1.0000486665, 93150, 0.0029},
    {8220, "NAD83(HARN) / WISCRS Barron (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(162480), -91.85, 0, 0, 1.0000486665, 305609.625, 0.01},
    {8222, "NAD83(HARN) / WISCRS Ashland (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(164542), S(-326240), 0, 0, 1.0000495683, 172821.9461, 0.0017},
    {8224, "NAD83(HARN) / WISCRS Ashland (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(164542), S(-326240), 0, 0, 1.0000495683, 567000.001, 0.006},
    {8225, "NAD83(HARN) / WISCRS Adams and Juneau (m)", TM, D_NAD83_HARN, E_GRS80, UM, S(156120), -90, 0, 0, 1.0000365285, 147218.6942, 0.0037},
    {8226, "NAD83(HARN) / WISCRS Adams and Juneau (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(156120), -90, 0, 0, 1.0000365285, 482999.999, 0.012},
    {8311, "NAD83(2011) / Oregon Burns-Harper zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 43.5, S(-423600), 0, 0, 1.00014, 90000, 0},
    {8312, "NAD83(2011) / Oregon Burns-Harper zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 43.5, S(-423600), 0, 0, 1.00014, 295275.5906, 0},
    {8313, "NAD83(2011) / Oregon Canyon City-Burns zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 43.5, -119, 0, 0, 1.00022, 20000, 0},
    {8314, "NAD83(2011) / Oregon Canyon City-Burns zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 43.5, -119, 0, 0, 1.00022, 65616.7979, 0},
    {8315, "NAD83(2011) / Oregon Coast Range North zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(164100), S(-444300), S(164100), S(164100), 1.000045, 30000, 20000},
    {8316, "NAD83(2011) / Oregon Coast Range North zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(164100), S(-444300), S(164100), S(164100), 1.000045, 98425.1969, 65616.7979},
    {8317, "NAD83(2011) / Oregon Dayville-Prairie City zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 44.25, S(-430680), 0, 0, 1.00012, 20000, 0},
    {8318, "NAD83(2011) / Oregon Dayville-Prairie City zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 44.25, S(-430680), 0, 0, 1.00012, 65616.7979, 0},
    {8319, "NAD83(2011) / Oregon Denio-Burns zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.75, S(-426300), 0, 0, 1.00019, 80000, 0},
    {8320, "NAD83(2011) / Oregon Denio-Burns zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 41.75, S(-426300), 0, 0, 1.00019, 262467.1916, 0},
    {8321, "NAD83(2011) / Oregon Halfway zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.25, -117.25, 45.25, 45.25, 1.000085, 40000, 70000},
    {8322, "NAD83(2011) / Oregon Halfway zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 45.25, -117.25, 45.25, 45.25, 1.000085, 131233.5958, 229658.7927},
    {8323, "NAD83(2011) / Oregon Medford-Diamond Lake zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 42, -122.25, 42, 42, 1.00004, 60000, -60000},
    {8324, "NAD83(2011) / Oregon Medford-Diamond Lake zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 42, -122.25, 42, 42, 1.00004, 196850.3937, -196850.3937},
    {8325, "NAD83(2011) / Oregon Mitchell zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 47, -120.25, 47, 47, 0.99927, 30000, 290000},
    {8326, "NAD83(2011) / Oregon Mitchell zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 47, -120.25, 47, 47, 0.99927, 98425.1969, 951443.5696},
    {8327, "NAD83(2011) / Oregon North Central zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(166200), -120.5, S(166200), S(166200), 1, 100000, 140000},
    {8328, "NAD83(2011) / Oregon North Central zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(166200), -120.5, S(166200), S(166200), 1, 328083.9895, 459317.5853},
    {8329, "NAD83(2011) / Oregon Ochoco Summit zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 43.5, -120.5, 43.5, 43.5, 1.00006, 40000, -80000},
    {8330, "NAD83(2011) / Oregon Ochoco Summit zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 43.5, -120.5, 43.5, 43.5, 1.00006, 131233.5958, -262467.1916},
    {8331, "NAD83(2011) / Oregon Owyhee zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.75, S(-423300), 0, 0, 1.00018, 70000, 0},
    {8332, "NAD83(2011) / Oregon Owyhee zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 41.75, S(-423300), 0, 0, 1.00018, 229658.7927, 0},
    {8333, "NAD83(2011) / Oregon Pilot Rock-Ukiah zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, S(166200), -119, S(166200), S(166200), 1.000025, 50000, 130000},
    {8334, "NAD83(2011) / Oregon Pilot Rock-Ukiah zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, S(166200), -119, S(166200), S(166200), 1.000025, 164041.9948, 426509.1864},
    {8335, "NAD83(2011) / Oregon Prairie City-Brogan zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 44, -118, 44, 44, 1.00017, 60000, 0},
    {8336, "NAD83(2011) / Oregon Prairie City-Brogan zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 44, -118, 44, 44, 1.00017, 196850.3937, 0},
    {8337, "NAD83(2011) / Oregon Riley-Lakeview zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 41.75, S(-433200), 0, 0, 1.000215, 70000, 0},
    {8338, "NAD83(2011) / Oregon Riley-Lakeview zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 41.75, S(-433200), 0, 0, 1.000215, 229658.7927, 0},
    {8339, "NAD83(2011) / Oregon Siskiyou Pass zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 42.5, S(-441300), 42.5, 42.5, 1.00015, 10000, 60000},
    {8340, "NAD83(2011) / Oregon Siskiyou Pass zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 42.5, S(-441300), 42.5, 42.5, 1.00015, 32808.399, 196850.3937},
    {8341, "NAD83(2011) / Oregon Ukiah-Fox zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 45.25, -119, 45.25, 45.25, 1.00014, 30000, 90000},
    {8342, "NAD83(2011) / Oregon Ukiah-Fox zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 45.25, -119, 45.25, 45.25, 1.00014, 98425.1969, 295275.5906},
    {8343, "NAD83(2011) / Oregon Wallowa zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 45.25, -117.5, 0, 0, 1.000195, 60000, 0},
    {8344, "NAD83(2011) / Oregon Wallowa zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 45.25, -117.5, 0, 0, 1.000195, 196850.3937, 0},
    {8345, "NAD83(2011) / Oregon Warner Highway zone (m)", LCC1, D_NAD83_2011, E_GRS80, UM, 42.5, -120, 42.5, 42.5, 1.000245, 40000, 60000},
    {8346, "NAD83(2011) / Oregon Warner Highway zone (ft)", LCC1, D_NAD83_2011, E_GRS80, UFT, 42.5, -120, 42.5, 42.5, 1.000245, 131233.5958, 196850.3937},
    {8347, "NAD83(2011) / Oregon Willamette Pass zone (m)", TM, D_NAD83_2011, E_GRS80, UM, 43, -122, 0, 0, 1.000223, 20000, 0},
    {8348, "NAD83(2011) / Oregon Willamette Pass zone (ft)", TM, D_NAD83_2011, E_GRS80, UFT, 43, -122, 0, 0, 1.000223, 65616.7979, 0},
    {8379, "NAD83 / NCRS Las Vegas (m)", TM, D_NAD83, E_GRS80, UM, 36.25, S(-413880), 0, 0, 1.0001, 100000, 200000},
    {8380, "NAD83 / NCRS Las Vegas (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 36.25, S(-413880), 0, 0, 1.0001, 328083.3333, 656166.6667},
    {8381, "NAD83 / NCRS Las Vegas high (m)", TM, D_NAD83, E_GRS80, UM, 36.25, S(-413880), 0, 0, 1.000135, 300000, 400000},
    {8382, "NAD83 / NCRS Las Vegas high (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 36.25, S(-413880), 0, 0, 1.000135, 984250, 1312333.3333},
    {8383, "NAD83(2011) / NCRS Las Vegas (m)", TM, D_NAD83_2011, E_GRS80, UM, 36.25, S(-413880), 0, 0, 1.0001, 100000, 200000},
    {8384, "NAD83(2011) / NCRS Las Vegas (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.25, S(-413880), 0, 0, 1.0001, 328083.3333, 656166.6667},
    {8385, "NAD83(2011) / NCRS Las Vegas high (m)", TM, D_NAD83_2011, E_GRS80, UM, 36.25, S(-413880), 0, 0, 1.000135, 300000, 400000},
    {8387, "NAD83(2011) / NCRS Las Vegas high (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.25, S(-413880), 0, 0, 1.000135, 984250, 1312333.3333},
    {8391, "GDA94 / WEIPA94", TM, D_GDA94, E_GRS80, UM, 0, 141, 0, 0, 0.999929, 300000, 2000000},
    {8395, "ETRS89 / Gauss-Kruger CM 9E", TM, D_ETRS89, E_GRS80, UM, 0, 9, 0, 0, 1, 500000, 0},
    {8518, "NAD83(2011) / KS RCS zone 1", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, -101.6, 0, 0, 1.000156, 1500000, 0},
    {8519, "NAD83(2011) / KS RCS zone 2", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, -100.95, 0, 0, 1.000134, 2500000, 0},
    {8520, "NAD83(2011) / KS RCS zone 3", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, -100.35, 0, 0, 1.000116, 3500000, 0},
    {8521, "NAD83(2011) / KS RCS zone 4", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, -99.45, 0, 0, 1.000082, 4500000, 0},
    {8522, "NAD83(2011) / KS RCS zone 5", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, S(-355200), 0, 0, 1.000078, 5500000, 0},
    {8523, "NAD83(2011) / KS RCS zone 6", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, -98.15, 0, 0, 1.000068, 6500000, 0},
    {8524, "NAD83(2011) / KS RCS zone 7", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.5, S(-350400), 0, 0, 1.000049, 7500000, 0},
    {8525, "NAD83(2011) / KS RCS zone 8", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(141000), -96.5, S(141000), S(141000), 1.000044, 8500000, 600000},
    {8526, "NAD83(2011) / KS RCS zone 9", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 38.5, -96.5, 38.5, 38.5, 1.00005, 9500000, 300000},
    {8527, "NAD83(2011) / KS RCS zone 10", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(142680), -95.75, S(142680), S(142680), 1.00004, 10500000, 700000},
    {8528, "NAD83(2011) / KS RCS zone 11", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 39.1, -95.25, 39.1, 39.1, 1.000033, 11500000, 600000},
    {8529, "NAD83(2011) / KS RCS zone 12", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, S(-365100), 0, 0, 1.00014, 12500000, 0},
    {8531, "NAD83(2011) / KS RCS zone 13", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, -100.4, 0, 0, 1.000109, 13500000, 0},
    {8533, "NAD83(2011) / KS RCS zone 14", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, S(-358800), 0, 0, 1.000097, 14500000, 0},
    {8534, "NAD83(2011) / KS RCS zone 15", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, -99.2, 0, 0, 1.000087, 15500000, 0},
    {8535, "NAD83(2011) / KS RCS zone 16", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, -98.55, 0, 0, 1.000069, 16500000, 0},
    {8536, "NAD83(2011) / KS RCS zone 17", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(135960), -97.5, S(135960), S(135960), 1.000059, 17500000, 400000},
    {8538, "NAD83(2011) / KS RCS zone 18", LCC1, D_NAD83_2011, E_GRS80, UFTUS, S(133860), -97.5, S(133860), S(133860), 1.000055, 18500000, 200000},
    {8539, "NAD83(2011) / KS RCS zone 19", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, S(-345480), 0, 0, 1.000034, 19500000, 0},
    {8540, "NAD83(2011) / KS RCS zone 20", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, S(-342300), 0, 0, 1.000031, 20500000, 0},
    {8826, "NAD83 / Idaho Transverse Mercator", TM, D_NAD83, E_GRS80, UM, 42, -114, 0, 0, 0.9996, 2500000, 1200000},
    {9191, "WGS 84 / NIWA Albers", AEA, D_WGS84, E_WGS84, UM, -40, 175, -30, -50, 1, 0, 0},
    {9354, "WGS 84 / IBCSO Polar Stereographic", PS, D_WGS84, E_WGS84, UM, -90, 0, -65, 0, 1, 0, 0},
    {9473, "GDA2020 / Australian Albers", AEA, D_GDA2020, E_GRS80, UM, 0, 132, -18, -36, 1, 0, 0},
    {9674, "NAD83 / USFS R6 Albers", AEA, D_NAD83, E_GRS80, UM, 34, -120, 43, 48, 1, 600000, 0},
    {9680, "WGS 84 / TM 90 NE", TM, D_WGS84, E_WGS84, UM, 0, 90, 0, 0, 0.9996, 500000, 0},
    {9709, "NAD83(CSRS) / UTM zone 23N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -45, 0, 0, 0.9996, 500000, 0},
    {9712, "NAD83 / UTM zone 24N", TM, D_NAD83, E_GRS80, UM, 0, -39, 0, 0, 0.9996, 500000, 0},
    {9713, "NAD83(CSRS) / UTM zone 24N", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -39, 0, 0, 0.9996, 500000, 0},
    {9748, "NAD83(2011) / Alabama East (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 30.5, S(-309000), 0, 0, 0.99996, 656166.667, 0},
    {9749, "NAD83(2011) / Alabama West (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 30, -87.5, 0, 0, 0.999933333, 1968500, 0},
    {10448, "GDA94 / ALB94", TM, D_GDA94, E_GRS80, UM, 0, S(424380), 0, 0, 1.0000044, 50000, 4000000},
    {10449, "GDA94 / BIO94", TM, D_GDA94, E_GRS80, UM, 0, 115.25, 0, 0, 1.0000022, 60000, 2600000},
    {10450, "GDA94 / BRO94", TM, D_GDA94, E_GRS80, UM, 0, S(440400), 0, 0, 1.00000298, 50000, 2200000},
    {10451, "GDA94 / BCG94", TM, D_GDA94, E_GRS80, UM, 0, S(415560), 0, 0, 0.99999592, 50000, 3900000},
    {10452, "GDA94 / CARN94", TM, D_GDA94, E_GRS80, UM, 0, S(409200), 0, 0, 0.99999796, 50000, 2950000},
    {10453, "GDA94 / COL94", TM, D_GDA94, E_GRS80, UM, 0, S(417360), 0, 0, 1.000019, 40000, 4000000},
    {10454, "GDA94 / ESP94", TM, D_GDA94, E_GRS80, UM, 0, S(438780), 0, 0, 1.0000055, 50000, 3950000},
    {10455, "GDA94 / EXM94", TM, D_GDA94, E_GRS80, UM, 0, S(410640), 0, 0, 1.00000236, 50000, 2650000},
    {10456, "GDA94 / GCG94", TM, D_GDA94, E_GRS80, UM, 0, S(412500), 0, 0, 1.00000628, 50000, 3350000},
    {10457, "GDA94 / GOLD94", TM, D_GDA94, E_GRS80, UM, 0, 121.5, 0, 0, 1.00004949, 60000, 3700000},
    {10458, "GDA94 / JCG94", TM, D_GDA94, E_GRS80, UM, 0, S(413940), 0, 0, 1.00000314, 50000, 3550000},
    {10459, "GDA94 / KALB94", TM, D_GDA94, E_GRS80, UM, 0, S(411535), 0, 0, 1.000014, 55000, 3600000},
    {10460, "GDA94 / KAR94", TM, D_GDA94, E_GRS80, UM, 0, S(420960), 0, 0, 0.9999989, 50000, 2450000},
    {10461, "GDA94 / KUN94", TM, D_GDA94, E_GRS80, UM, 0, 128.75, 0, 0, 1.0000165, 50000, 2000000},
    {10462, "GDA94 / LCG94", TM, D_GDA94, E_GRS80, UM, 0, S(415320), 0, 0, 1.00000157, 50000, 3650000},
    {10463, "GDA94 / MRCG94", TM, D_GDA94, E_GRS80, UM, 0, S(414600), 0, 0, 1.0000055, 50000, 3950000},
    {10464, "GDA94 / PCG94", TM, D_GDA94, E_GRS80, UM, 0, S(416940), 0, 0, 0.99999906, 50000, 3800000},
    {10465, "GDA94 / PHG94", TM, D_GDA94, E_GRS80, UM, 0, 118.6, 0, 0, 1.00000135, 50000, 2400000},
    {10481, "NAD83 / TWDB GM", AEA, D_NAD83, E_GRS80, UFTUS, 31.25, -100, 27.5, 35, 1, 4921250, 19685000},
    {10516, "NAD83(2011) / Adjusted Jackson (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 44.2533353222222, -90.8442965138889, 0, 0, 1.0000353, 88582.5, 82020.833},
    {10665, "SIRGAS 2000 / Porto Alegre TM", TM, D_SIRGAS2000, E_GRS80, UM, 0, -51, 0, 0, 0.999995, 300000, 5000000},
    {10773, "SIRGAS 2000 / Ribeirao Preto Local TM", TM, D_SIRGAS2000, E_GRS80, UM, -21.2, S(-172020), 0, 0, 1.000092, 15000, 25000},
    {10820, "WGS 84 / Agriculture Canada Albers", AEA, D_WGS84, E_WGS84, UM, 40, -96, 44.75, 55.75, 1, 0, 0},
    {10857, "SIRGAS 2000 / Brazil Albers", AEA, D_SIRGAS2000, E_GRS80, UM, -12, -54, -2, -22, 1, 5000000, 10000000},
    {20047, "GDA2020 / BCSG2020", TM, D_GDA2020, E_GRS80, UM, -28, 153, 0, 0, 0.99999, 150000, 200000},
    {23301, "NAD83(2011) / ICS83-Freeport (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 42.2, -89.95, 42.2, 42.2, 1.000029, 1804000, 755000},
    {23302, "NAD83(2011) / ICS83-Rockford (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 41.25, -89.25, 0, 0, 1.000029, 2822000, 492000},
    {23303, "NAD83(2011) / ICS83-Aurora (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 41.25, -88.5, 0, 0, 1.00003, 3773000, 492000},
    {23304, "NAD83(2011) / ICS83-Chicago (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 41.25, -87.8, 0, 0, 1.000023, 4757000, 492000},
    {23305, "NAD83(2011) / ICS83-Moline (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 41.55, -90.6, 41.55, 41.55, 1.000024, 5741000, 755000},
    {23306, "NAD83(2011) / ICS83-Sterling (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 41.55, -90.05, 41.55, 41.55, 1.00002, 6726000, 755000},
    {23307, "NAD83(2011) / ICS83-Ottawa (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 41.3, -89.05, 41.3, 41.3, 1.000023, 7743000, 755000},
    {23308, "NAD83(2011) / ICS83-Joliet (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.55, -88, 0, 0, 1.000022, 8694000, 492000},
    {23309, "NAD83(2011) / ICS83-Monmouth (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 40.2, -90.85, 0, 0, 1.000024, 9678000, 492000},
    {23310, "NAD83(2011) / ICS83-Galesburg (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.25, -90.1, 0, 0, 1.000023, 230000, 492000},
    {23311, "NAD83(2011) / ICS83-Peoria (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.4, -89.65, 0, 0, 1.000023, 1378000, 622000},
    {23312, "NAD83(2011) / ICS83-Eureka (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 37.45, -89.3, 0, 0, 1.000025, 2756000, 427000},
    {23313, "NAD83(2011) / ICS83-Bloomington (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 40.5, -88.85, 40.5, 40.5, 1.000031, 3773000, 1739000},
    {23314, "NAD83(2011) / ICS83-Pontiac (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 40.9, -88.55, 40.9, 40.9, 1.000025, 4757000, 1739000},
    {23315, "NAD83(2011) / ICS83-Watseka (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 40.75, -87.95, 40.75, 40.75, 1.000024, 5741000, 1739000},
    {23316, "NAD83(2011) / ICS83-Quincy (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.75, -91.25, 0, 0, 1.000023, 6726000, 492000},
    {23317, "NAD83(2011) / ICS83-Macomb (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.8, -90.6, 0, 0, 1.000024, 7710000, 492000},
    {23318, "NAD83(2011) / ICS83-Lincoln (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 40.15, -89.8, 40.15, 40.15, 1.000018, 8760000, 1739000},
    {23319, "NAD83(2011) / ICS83-Decatur (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 36.3, -88.8, 0, 0, 1.000024, 9678000, 492000},
    {23320, "NAD83(2011) / ICS83-Champaign (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 40.15, -88, 40.15, 40.15, 1.000026, 328000, 2822000},
    {23321, "NAD83(2011) / ICS83-Jacksonville (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 39.65, -90.6, 39.65, 39.65, 1.000023, 1247000, 2822000},
    {23322, "NAD83(2011) / ICS83-Springfield (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 39.75, -89.65, 39.75, 39.75, 1.000022, 2329000, 2887000},
    {23323, "NAD83(2011) / ICS83-Charleston (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 39.65, -88, 39.65, 39.65, 1.000024, 3773000, 2756000},
    {23324, "NAD83(2011) / ICS83-Jerseyville (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 32.85, -90.5, 0, 0, 1.000019, 4757000, 492000},
    {23325, "NAD83(2011) / ICS83-Carlinville (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 39.3, -90.15, 39.3, 39.3, 1.00002, 5741000, 2756000},
    {23326, "NAD83(2011) / ICS83-Taylorville (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 33.15, -89.4, 0, 0, 1.000023, 6726000, 492000},
    {23327, "NAD83(2011) / ICS83-Effingham (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 38.95, -89, 38.95, 38.95, 1.000019, 7710000, 2756000},
    {23328, "NAD83(2011) / ICS83-Robinson (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 39.1, -88, 39.1, 39.1, 1.000017, 8694000, 2756000},
    {23329, "NAD83(2011) / ICS83-Belleville (ftUS)", TM, D_NAD83_2011, E_GRS80, UFTUS, 32.35, -90, 0, 0, 1.000016, 9678000, 492000},
    {23330, "NAD83(2011) / ICS83-Mount Vernon (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 38.45, -89.15, 38.45, 38.45, 1.000015, 328000, 3773000},
    {23331, "NAD83(2011) / ICS83-Olney (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 38.55, -88.15, 38.55, 38.55, 1.000013, 1247000, 3773000},
    {23332, "NAD83(2011) / ICS83-Carbondale (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 37.9, -88.95, 37.9, 37.9, 1.000012, 2395000, 3773000},
    {23333, "NAD83(2011) / ICS83-Metropolis (ftUS)", LCC1, D_NAD83_2011, E_GRS80, UFTUS, 37.2, -88.9, 37.2, 37.2, 1.00001, 3642000, 3839000},
    {25884, "ETRS89 / TM Baltic93", TM, D_ETRS89, E_GRS80, UM, 0, 24, 0, 0, 0.9996, 500000, 0},
    {26847, "NAD83 / Maine East (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(157200), -68.5, 0, 0, 0.9999, 984250, 0},
    {26848, "NAD83 / Maine West (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, S(154200), S(-252600), 0, 0, 0.999966667, 2952750, 0},
    {26849, "NAD83 / Minnesota North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 46.5, -93.1, S(175080), S(169320), 1, 2624666.6667, 328083.3333},
    {26850, "NAD83 / Minnesota Central (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 45, -94.25, 47.05, S(164220), 1, 2624666.6667, 328083.3333},
    {26851, "NAD83 / Minnesota South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 43, -94, S(162780), S(157620), 1, 2624666.6667, 328083.3333},
    {26852, "NAD83 / Nebraska (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, S(143400), -100, 43, 40, 1, 1640416.6667, 0},
    {26853, "NAD83 / West Virginia North (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 38.5, -79.5, 40.25, 39, 1, 1968500, 0},
    {26854, "NAD83 / West Virginia South (ftUS)", LCC2, D_NAD83, E_GRS80, UFTUS, 37, -81, S(139980), S(134940), 1, 1968500, 0},
    {26855, "NAD83(HARN) / Maine East (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(157200), -68.5, 0, 0, 0.9999, 984250, 0},
    {26856, "NAD83(HARN) / Maine West (ftUS)", TM, D_NAD83_HARN, E_GRS80, UFTUS, S(154200), S(-252600), 0, 0, 0.999966667, 2952750, 0},
    {26857, "NAD83(HARN) / Minnesota North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 46.5, -93.1, S(175080), S(169320), 1, 2624666.6667, 328083.3333},
    {26858, "NAD83(HARN) / Minnesota Central (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 45, -94.25, 47.05, S(164220), 1, 2624666.6667, 328083.3333},
    {26859, "NAD83(HARN) / Minnesota South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 43, -94, S(162780), S(157620), 1, 2624666.6667, 328083.3333},
    {26860, "NAD83(HARN) / Nebraska (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, S(143400), -100, 43, 40, 1, 1640416.6667, 0},
    {26861, "NAD83(HARN) / West Virginia North (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 38.5, -79.5, 40.25, 39, 1, 1968500, 0},
    {26862, "NAD83(HARN) / West Virginia South (ftUS)", LCC2, D_NAD83_HARN, E_GRS80, UFTUS, 37, -81, S(139980), S(134940), 1, 1968500, 0},
    {26891, "NAD83(CSRS) / MTM zone 11", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -82.5, 0, 0, 0.9999, 304800, 0},
    {26892, "NAD83(CSRS) / MTM zone 12", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -81, 0, 0, 0.9999, 304800, 0},
    {26893, "NAD83(CSRS) / MTM zone 13", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -84, 0, 0, 0.9999, 304800, 0},
    {26894, "NAD83(CSRS) / MTM zone 14", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -87, 0, 0, 0.9999, 304800, 0},
    {26895, "NAD83(CSRS) / MTM zone 15", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -90, 0, 0, 0.9999, 304800, 0},
    {26896, "NAD83(CSRS) / MTM zone 16", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -93, 0, 0, 0.9999, 304800, 0},
    {26897, "NAD83(CSRS) / MTM zone 17", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -96, 0, 0, 0.9999, 304800, 0},
    {26898, "NAD83(CSRS) / MTM zone 1", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -53, 0, 0, 0.9999, 304800, 0},
    {26899, "NAD83(CSRS) / MTM zone 2", TM, D_NAD83_CSRS, E_GRS80, UM, 0, -56, 0, 0, 0.9999, 304800, 0},
    {26929, "NAD83 / Alabama East", TM, D_NAD83, E_GRS80, UM, 30.5, S(-309000), 0, 0, 0.99996, 200000, 0},
    {26930, "NAD83 / Alabama West", TM, D_NAD83, E_GRS80, UM, 30, -87.5, 0, 0, 0.999933333, 600000, 0},
    {26932, "NAD83 / Alaska zone 2", TM, D_NAD83, E_GRS80, UM, 54, -142, 0, 0, 0.9999, 500000, 0},
    {26933, "NAD83 / Alaska zone 3", TM, D_NAD83, E_GRS80, UM, 54, -146, 0, 0, 0.9999, 500000, 0},
    {26934, "NAD83 / Alaska zone 4", TM, D_NAD83, E_GRS80, UM, 54, -150, 0, 0, 0.9999, 500000, 0},
    {26935, "NAD83 / Alaska zone 5", TM, D_NAD83, E_GRS80, UM, 54, -154, 0, 0, 0.9999, 500000, 0},
    {26936, "NAD83 / Alaska zone 6", TM, D_NAD83, E_GRS80, UM, 54, -158, 0, 0, 0.9999, 500000, 0},
    {26937, "NAD83 / Alaska zone 7", TM, D_NAD83, E_GRS80, UM, 54, -162, 0, 0, 0.9999, 500000, 0},
    {26938, "NAD83 / Alaska zone 8", TM, D_NAD83, E_GRS80, UM, 54, -166, 0, 0, 0.9999, 500000, 0},
    {26939, "NAD83 / Alaska zone 9", TM, D_NAD83, E_GRS80, UM, 54, -170, 0, 0, 0.9999, 500000, 0},
    {26940, "NAD83 / Alaska zone 10", LCC2, D_NAD83, E_GRS80, UM, 51, -176, S(193800), S(186600), 1, 1000000, 0},
    {26941, "NAD83 / California zone 1", LCC2, D_NAD83, E_GRS80, UM, S(141600), -122, S(150000), 40, 1, 2000000, 500000},
    {26942, "NAD83 / California zone 2", LCC2, D_NAD83, E_GRS80, UM, S(135600), -122, S(143400), S(138000), 1, 2000000, 500000},
    {26943, "NAD83 / California zone 3", LCC2, D_NAD83, E_GRS80, UM, 36.5, -120.5, S(138360), S(133440), 1, 2000000, 500000},
    {26944, "NAD83 / California zone 4", LCC2, D_NAD83, E_GRS80, UM, S(127200), -119, 37.25, 36, 1, 2000000, 500000},
    {26945, "NAD83 / California zone 5", LCC2, D_NAD83, E_GRS80, UM, 33.5, -118, S(127680), S(122520), 1, 2000000, 500000},
    {26946, "NAD83 / California zone 6", LCC2, D_NAD83, E_GRS80, UM, S(115800), -116.25, S(121980), S(118020), 1, 2000000, 500000},
    {26948, "NAD83 / Arizona East", TM, D_NAD83, E_GRS80, UM, 31, S(-396600), 0, 0, 0.9999, 213360, 0},
    {26949, "NAD83 / Arizona Central", TM, D_NAD83, E_GRS80, UM, 31, S(-402900), 0, 0, 0.9999, 213360, 0},
    {26950, "NAD83 / Arizona West", TM, D_NAD83, E_GRS80, UM, 31, -113.75, 0, 0, 0.999933333, 213360, 0},
    {26951, "NAD83 / Arkansas North", LCC2, D_NAD83, E_GRS80, UM, S(123600), -92, S(130440), S(125760), 1, 400000, 0},
    {26952, "NAD83 / Arkansas South", LCC2, D_NAD83, E_GRS80, UM, S(117600), -92, S(125160), 33.3, 1, 400000, 400000},
    {26953, "NAD83 / Colorado North", LCC2, D_NAD83, E_GRS80, UM, S(141600), -105.5, S(146820), S(142980), 1, 914401.8289, 304800.6096},
    {26954, "NAD83 / Colorado Central", LCC2, D_NAD83, E_GRS80, UM, S(136200), -105.5, 39.75, 38.45, 1, 914401.8289, 304800.6096},
    {26955, "NAD83 / Colorado South", LCC2, D_NAD83, E_GRS80, UM, S(132000), -105.5, S(138360), S(134040), 1, 914401.8289, 304800.6096},
    {26956, "NAD83 / Connecticut", LCC2, D_NAD83, E_GRS80, UM, S(147000), -72.75, S(150720), 41.2, 1, 304800.6096, 152400.3048},
    {26957, "NAD83 / Delaware", TM, D_NAD83, E_GRS80, UM, 38, S(-271500), 0, 0, 0.999995, 200000, 0},
    {26958, "NAD83 / Florida East", TM, D_NAD83, E_GRS80, UM, S(87600), -81, 0, 0, 0.999941177, 200000, 0},
    {26959, "NAD83 / Florida West", TM, D_NAD83, E_GRS80, UM, S(87600), -82, 0, 0, 0.999941177, 200000, 0},
    {26960, "NAD83 / Florida North", LCC2, D_NAD83, E_GRS80, UM, 29, -84.5, 30.75, S(106500), 1, 600000, 0},
    {26961, "NAD83 / Hawaii zone 1", TM, D_NAD83, E_GRS80, UM, S(67800), -155.5, 0, 0, 0.999966667, 500000, 0},
    {26962, "NAD83 / Hawaii zone 2", TM, D_NAD83, E_GRS80, UM, S(73200), S(-564000), 0, 0, 0.999966667, 500000, 0},
    {26963, "NAD83 / Hawaii zone 3", TM, D_NAD83, E_GRS80, UM, S(76200), -158, 0, 0, 0.99999, 500000, 0},
    {26964, "NAD83 / Hawaii zone 4", TM, D_NAD83, E_GRS80, UM, S(78600), -159.5, 0, 0, 0.99999, 500000, 0},
    {26965, "NAD83 / Hawaii zone 5", TM, D_NAD83, E_GRS80, UM, S(78000), S(-576600), 0, 0, 1, 500000, 0},
    {26966, "NAD83 / Georgia East", TM, D_NAD83, E_GRS80, UM, 30, S(-295800), 0, 0, 0.9999, 200000, 0},
    {26967, "NAD83 / Georgia West", TM, D_NAD83, E_GRS80, UM, 30, S(-303000), 0, 0, 0.9999, 700000, 0},
    {26968, "NAD83 / Idaho East", TM, D_NAD83, E_GRS80, UM, S(150000), S(-403800), 0, 0, 0.999947368, 200000, 0},
    {26969, "NAD83 / Idaho Central", TM, D_NAD83, E_GRS80, UM, S(150000), -114, 0, 0, 0.999947368, 500000, 0},
    {26970, "NAD83 / Idaho West", TM, D_NAD83, E_GRS80, UM, S(150000), -115.75, 0, 0, 0.999933333, 800000, 0},
    {26971, "NAD83 / Illinois East", TM, D_NAD83, E_GRS80, UM, S(132000), S(-318000), 0, 0, 0.999975, 300000, 0},
    {26972, "NAD83 / Illinois West", TM, D_NAD83, E_GRS80, UM, S(132000), S(-324600), 0, 0, 0.999941177, 700000, 0},
    {26973, "NAD83 / Indiana East", TM, D_NAD83, E_GRS80, UM, 37.5, S(-308400), 0, 0, 0.999966667, 100000, 250000},
    {26974, "NAD83 / Indiana West", TM, D_NAD83, E_GRS80, UM, 37.5, S(-313500), 0, 0, 0.999966667, 900000, 250000},
    {26975, "NAD83 / Iowa North", LCC2, D_NAD83, E_GRS80, UM, 41.5, -93.5, S(155760), S(151440), 1, 1500000, 1000000},
    {26976, "NAD83 / Iowa South", LCC2, D_NAD83, E_GRS80, UM, 40, -93.5, S(150420), S(146220), 1, 500000, 0},
    {26977, "NAD83 / Kansas North", LCC2, D_NAD83, E_GRS80, UM, S(138000), -98, S(143220), S(139380), 1, 400000, 0},
    {26978, "NAD83 / Kansas South", LCC2, D_NAD83, E_GRS80, UM, S(132000), -98.5, S(138840), S(134160), 1, 400000, 400000},
    {26980, "NAD83 / Kentucky South", LCC2, D_NAD83, E_GRS80, UM, S(130800), -85.75, S(136560), S(132240), 1, 500000, 500000},
    {26981, "NAD83 / Louisiana North", LCC2, D_NAD83, E_GRS80, UM, 30.5, -92.5, S(117600), S(112200), 1, 1000000, 0},
    {26982, "NAD83 / Louisiana South", LCC2, D_NAD83, E_GRS80, UM, 28.5, S(-328800), 30.7, 29.3, 1, 1000000, 0},
    {26983, "NAD83 / Maine East", TM, D_NAD83, E_GRS80, UM, S(157200), -68.5, 0, 0, 0.9999, 300000, 0},
    {26984, "NAD83 / Maine West", TM, D_NAD83, E_GRS80, UM, S(154200), S(-252600), 0, 0, 0.999966667, 900000, 0},
    {26985, "NAD83 / Maryland", LCC2, D_NAD83, E_GRS80, UM, S(135600), -77, 39.45, 38.3, 1, 400000, 0},
    {26986, "NAD83 / Massachusetts Mainland", LCC2, D_NAD83, E_GRS80, UM, 41, -71.5, S(153660), S(150180), 1, 200000, 750000},
    {26987, "NAD83 / Massachusetts Island", LCC2, D_NAD83, E_GRS80, UM, 41, -70.5, S(149340), S(148620), 1, 500000, 0},
    {26988, "NAD83 / Michigan North", LCC2, D_NAD83, E_GRS80, UM, S(161220), -87, S(169500), S(163740), 1, 8000000, 0},
    {26989, "NAD83 / Michigan Central", LCC2, D_NAD83, E_GRS80, UM, S(155940), S(-303720), 45.7, S(159060), 1, 6000000, 0},
    {26990, "NAD83 / Michigan South", LCC2, D_NAD83, E_GRS80, UM, 41.5, S(-303720), S(157200), 42.1, 1, 4000000, 0},
    {26991, "NAD83 / Minnesota North", LCC2, D_NAD83, E_GRS80, UM, 46.5, -93.1, S(175080), S(169320), 1, 800000, 100000},
    {26992, "NAD83 / Minnesota Central", LCC2, D_NAD83, E_GRS80, UM, 45, -94.25, 47.05, S(164220), 1, 800000, 100000},
    {26993, "NAD83 / Minnesota South", LCC2, D_NAD83, E_GRS80, UM, 43, -94, S(162780), S(157620), 1, 800000, 100000},
    {26994, "NAD83 / Mississippi East", TM, D_NAD83, E_GRS80, UM, 29.5, S(-319800), 0, 0, 0.99995, 300000, 0},
    {26995, "NAD83 / Mississippi West", TM, D_NAD83, E_GRS80, UM, 29.5, S(-325200), 0, 0, 0.99995, 700000, 0},
    {26996, "NAD83 / Missouri East", TM, D_NAD83, E_GRS80, UM, S(129000), -90.5, 0, 0, 0.999933333, 250000, 0},
    {26997, "NAD83 / Missouri Central", TM, D_NAD83, E_GRS80, UM, S(129000), -92.5, 0, 0, 0.999933333, 500000, 0},
    {26998, "NAD83 / Missouri West", TM, D_NAD83, E_GRS80, UM, S(130200), -94.5, 0, 0, 0.999941177, 850000, 0},
    {32100, "NAD83 / Montana", LCC2, D_NAD83, E_GRS80, UM, 44.25, -109.5, 49, 45, 1, 600000, 0},
    {32104, "NAD83 / Nebraska", LCC2, D_NAD83, E_GRS80, UM, S(143400), -100, 43, 40, 1, 500000, 0},
    {32107, "NAD83 / Nevada East", TM, D_NAD83, E_GRS80, UM, 34.75, S(-416100), 0, 0, 0.9999, 200000, 8000000},
    {32108, "NAD83 / Nevada Central", TM, D_NAD83, E_GRS80, UM, 34.75, S(-420000), 0, 0, 0.9999, 500000, 6000000},
    {32109, "NAD83 / Nevada West", TM, D_NAD83, E_GRS80, UM, 34.75, S(-426900), 0, 0, 0.9999, 800000, 4000000},
    {32110, "NAD83 / New Hampshire", TM, D_NAD83, E_GRS80, UM, 42.5, S(-258000), 0, 0, 0.999966667, 300000, 0},
    {32111, "NAD83 / New Jersey", TM, D_NAD83, E_GRS80, UM, S(139800), -74.5, 0, 0, 0.9999, 150000, 0},
    {32112, "NAD83 / New Mexico East", TM, D_NAD83, E_GRS80, UM, 31, S(-375600), 0, 0, 0.999909091, 165000, 0},
    {32113, "NAD83 / New Mexico Central", TM, D_NAD83, E_GRS80, UM, 31, -106.25, 0, 0, 0.9999, 500000, 0},
    {32114, "NAD83 / New Mexico West", TM, D_NAD83, E_GRS80, UM, 31, S(-388200), 0, 0, 0.999916667, 830000, 0},
    {32115, "NAD83 / New York East", TM, D_NAD83, E_GRS80, UM, S(139800), -74.5, 0, 0, 0.9999, 150000, 0},
    {32116, "NAD83 / New York Central", TM, D_NAD83, E_GRS80, UM, 40, S(-275700), 0, 0, 0.9999375, 250000, 0},
    {32117, "NAD83 / New York West", TM, D_NAD83, E_GRS80, UM, 40, S(-282900), 0, 0, 0.9999375, 350000, 0},
    {32118, "NAD83 / New York Long Island", LCC2, D_NAD83, E_GRS80, UM, S(144600), -74, S(147720), S(146400), 1, 300000, 0},
    {32119, "NAD83 / North Carolina", LCC2, D_NAD83, E_GRS80, UM, 33.75, -79, S(130200), S(123600), 1, 609601.22, 0},
    {32120, "NAD83 / North Dakota North", LCC2, D_NAD83, E_GRS80, UM, 47, -100.5, S(175440), S(170760), 1, 600000, 0},
    {32121, "NAD83 / North Dakota South", LCC2, D_NAD83, E_GRS80, UM, S(164400), -100.5, S(170940), S(166260), 1, 600000, 0},
    {32122, "NAD83 / Ohio North", LCC2, D_NAD83, E_GRS80, UM, S(142800), -82.5, 41.7, S(145560), 1, 600000, 0},
    {32123, "NAD83 / Ohio South", LCC2, D_NAD83, E_GRS80, UM, 38, -82.5, S(144120), S(139440), 1, 600000, 0},
    {32124, "NAD83 / Oklahoma North", LCC2, D_NAD83, E_GRS80, UM, 35, -98, S(132360), S(128040), 1, 600000, 0},
    {32125, "NAD83 / Oklahoma South", LCC2, D_NAD83, E_GRS80, UM, S(120000), -98, S(126840), S(122160), 1, 600000, 0},
    {32126, "NAD83 / Oregon North", LCC2, D_NAD83, E_GRS80, UM, S(157200), -120.5, 46, S(159600), 1, 2500000, 0},
    {32127, "NAD83 / Oregon South", LCC2, D_NAD83, E_GRS80, UM, S(150000), -120.5, 44, S(152400), 1, 1500000, 0},
    {32128, "NAD83 / Pennsylvania North", LCC2, D_NAD83, E_GRS80, UM, S(144600), -77.75, 41.95, S(147180), 1, 600000, 0},
    {32129, "NAD83 / Pennsylvania South", LCC2, D_NAD83, E_GRS80, UM, S(141600), -77.75, S(147480), S(143760), 1, 600000, 0},
    {32130, "NAD83 / Rhode Island", TM, D_NAD83, E_GRS80, UM, S(147900), -71.5, 0, 0, 0.99999375, 100000, 0},
    {32133, "NAD83 / South Carolina", LCC2, D_NAD83, E_GRS80, UM, S(114600), -81, S(125400), 32.5, 1, 609600, 0},
    {32134, "NAD83 / South Dakota North", LCC2, D_NAD83, E_GRS80, UM, S(157800), -100, S(164460), S(159900), 1, 600000, 0},
    {32135, "NAD83 / South Dakota South", LCC2, D_NAD83, E_GRS80, UM, S(152400), S(-361200), 44.4, S(154200), 1, 600000, 0},
    {32136, "NAD83 / Tennessee", LCC2, D_NAD83, E_GRS80, UM, S(123600), -86, S(131100), 35.25, 1, 600000, 0},
    {32137, "NAD83 / Texas North", LCC2, D_NAD83, E_GRS80, UM, 34, -101.5, S(130260), 34.65, 1, 200000, 1000000},
    {32138, "NAD83 / Texas North Central", LCC2, D_NAD83, E_GRS80, UM, S(114000), -98.5, S(122280), S(115680), 1, 600000, 2000000},
    {32139, "NAD83 / Texas Central", LCC2, D_NAD83, E_GRS80, UM, S(106800), S(-361200), S(114780), S(108420), 1, 700000, 3000000},
    {32140, "NAD83 / Texas South Central", LCC2, D_NAD83, E_GRS80, UM, S(100200), -99, S(109020), S(102180), 1, 600000, 4000000},
    {32141, "NAD83 / Texas South", LCC2, D_NAD83, E_GRS80, UM, S(92400), -98.5, S(100200), S(94200), 1, 300000, 5000000},
    {32142, "NAD83 / Utah North", LCC2, D_NAD83, E_GRS80, UM, S(145200), -111.5, S(150420), S(146580), 1, 500000, 1000000},
    {32143, "NAD83 / Utah Central", LCC2, D_NAD83, E_GRS80, UM, S(138000), -111.5, 40.65, S(140460), 1, 500000, 2000000},
    {32144, "NAD83 / Utah South", LCC2, D_NAD83, E_GRS80, UM, S(132000), -111.5, 38.35, S(133980), 1, 500000, 3000000},
    {32145, "NAD83 / Vermont", TM, D_NAD83, E_GRS80, UM, 42.5, -72.5, 0, 0, 0.999964286, 500000, 0},
    {32146, "NAD83 / Virginia North", LCC2, D_NAD83, E_GRS80, UM, S(135600), -78.5, 39.2, S(136920), 1, 3500000, 2000000},
    {32147, "NAD83 / Virginia South", LCC2, D_NAD83, E_GRS80, UM, S(130800), -78.5, S(136680), S(132360), 1, 3500000, 1000000},
    {32148, "NAD83 / Washington North", LCC2, D_NAD83, E_GRS80, UM, 47, S(-435000), S(175440), 47.5, 1, 500000, 0},
    {32149, "NAD83 / Washington South", LCC2, D_NAD83, E_GRS80, UM, S(163200), -120.5, S(170400), S(165000), 1, 500000, 0},
    {32150, "NAD83 / West Virginia North", LCC2, D_NAD83, E_GRS80, UM, 38.5, -79.5, 40.25, 39, 1, 600000, 0},
    {32151, "NAD83 / West Virginia South", LCC2, D_NAD83, E_GRS80, UM, 37, -81, S(139980), S(134940), 1, 600000, 0},
    {32152, "NAD83 / Wisconsin North", LCC2, D_NAD83, E_GRS80, UM, S(162600), -90, S(168360), S(164040), 1, 600000, 0},
    {32153, "NAD83 / Wisconsin Central", LCC2, D_NAD83, E_GRS80, UM, S(157800), -90, 45.5, 44.25, 1, 600000, 0},
    {32154, "NAD83 / Wisconsin South", LCC2, D_NAD83, E_GRS80, UM, 42, -90, S(158640), S(153840), 1, 600000, 0},
    {32155, "NAD83 / Wyoming East", TM, D_NAD83, E_GRS80, UM, 40.5, S(-378600), 0, 0, 0.9999375, 200000, 0},
    {32156, "NAD83 / Wyoming East Central", TM, D_NAD83, E_GRS80, UM, 40.5, S(-386400), 0, 0, 0.9999375, 400000, 100000},
    {32157, "NAD83 / Wyoming West Central", TM, D_NAD83, E_GRS80, UM, 40.5, -108.75, 0, 0, 0.9999375, 600000, 0},
    {32158, "NAD83 / Wyoming West", TM, D_NAD83, E_GRS80, UM, 40.5, S(-396300), 0, 0, 0.9999375, 800000, 100000},
    {32159, "NAD83 / Wyoming Lambert", LCC2, D_NAD83, E_GRS80, UM, 41, -107.5, 41, 45, 1, 500000, 200000},
    {32161, "NAD83 / Puerto Rico & Virgin Is.", LCC2, D_NAD83, E_GRS80, UM, S(64200), S(-239160), S(66360), S(64920), 1, 200000, 200000},
    {32164, "NAD83 / BLM 14N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -99, 0, 0, 0.9996, 1640416.67, 0},
    {32165, "NAD83 / BLM 15N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -93, 0, 0, 0.9996, 1640416.67, 0},
    {32166, "NAD83 / BLM 16N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -87, 0, 0, 0.9996, 1640416.67, 0},
    {32167, "NAD83 / BLM 17N (ftUS)", TM, D_NAD83, E_GRS80, UFTUS, 0, -81, 0, 0, 0.9996, 1640416.67, 0},
    {32181, "NAD83 / MTM zone 1", TM, D_NAD83, E_GRS80, UM, 0, -53, 0, 0, 0.9999, 304800, 0},
    {32182, "NAD83 / MTM zone 2", TM, D_NAD83, E_GRS80, UM, 0, -56, 0, 0, 0.9999, 304800, 0},
    {32183, "NAD83 / MTM zone 3", TM, D_NAD83, E_GRS80, UM, 0, -58.5, 0, 0, 0.9999, 304800, 0},
    {32184, "NAD83 / MTM zone 4", TM, D_NAD83, E_GRS80, UM, 0, -61.5, 0, 0, 0.9999, 304800, 0},
    {32185, "NAD83 / MTM zone 5", TM, D_NAD83, E_GRS80, UM, 0, -64.5, 0, 0, 0.9999, 304800, 0},
    {32186, "NAD83 / MTM zone 6", TM, D_NAD83, E_GRS80, UM, 0, -67.5, 0, 0, 0.9999, 304800, 0},
    {32187, "NAD83 / MTM zone 7", TM, D_NAD83, E_GRS80, UM, 0, -70.5, 0, 0, 0.9999, 304800, 0},
    {32188, "NAD83 / MTM zone 8", TM, D_NAD83, E_GRS80, UM, 0, -73.5, 0, 0, 0.9999, 304800, 0},
    {32189, "NAD83 / MTM zone 9", TM, D_NAD83, E_GRS80, UM, 0, -76.5, 0, 0, 0.9999, 304800, 0},
    {32190, "NAD83 / MTM zone 10", TM, D_NAD83, E_GRS80, UM, 0, -79.5, 0, 0, 0.9999, 304800, 0},
    {32191, "NAD83 / MTM zone 11", TM, D_NAD83, E_GRS80, UM, 0, -82.5, 0, 0, 0.9999, 304800, 0},
    {32192, "NAD83 / MTM zone 12", TM, D_NAD83, E_GRS80, UM, 0, -81, 0, 0, 0.9999, 304800, 0},
    {32193, "NAD83 / MTM zone 13", TM, D_NAD83, E_GRS80, UM, 0, -84, 0, 0, 0.9999, 304800, 0},
    {32194, "NAD83 / MTM zone 14", TM, D_NAD83, E_GRS80, UM, 0, -87, 0, 0, 0.9999, 304800, 0},
    {32195, "NAD83 / MTM zone 15", TM, D_NAD83, E_GRS80, UM, 0, -90, 0, 0, 0.9999, 304800, 0},
    {32196, "NAD83 / MTM zone 16", TM, D_NAD83, E_GRS80, UM, 0, -93, 0, 0, 0.9999, 304800, 0},
    {32197, "NAD83 / MTM zone 17", TM, D_NAD83, E_GRS80, UM, 0, -96, 0, 0, 0.9999, 304800, 0},
    {32198, "NAD83 / Quebec Lambert", LCC2, D_NAD83, E_GRS80, UM, 44, -68.5, 60, 46, 1, 0, 0},
    {32199, "NAD83 / Louisiana Offshore", LCC2, D_NAD83, E_GRS80, UM, 25.5, S(-328800), S(100200), S(94200), 1, 1000000, 0},
    {32661, "WGS 84 / UPS North (N,E)", PS, D_WGS84, E_WGS84, UM, 90, 0, 0, 0, 0.994, 2000000, 2000000},
    {32664, "WGS 84 / BLM 14N (ftUS)", TM, D_WGS84, E_WGS84, UFTUS, 0, -99, 0, 0, 0.9996, 1640416.67, 0},
    {32665, "WGS 84 / BLM 15N (ftUS)", TM, D_WGS84, E_WGS84, UFTUS, 0, -93, 0, 0, 0.9996, 1640416.67, 0},
    {32666, "WGS 84 / BLM 16N (ftUS)", TM, D_WGS84, E_WGS84, UFTUS, 0, -87, 0, 0, 0.9996, 1640416.67, 0},
    {32667, "WGS 84 / BLM 17N (ftUS)", TM, D_WGS84, E_WGS84, UFTUS, 0, -81, 0, 0, 0.9996, 1640416.67, 0},
    {32761, "WGS 84 / UPS South (N,E)", PS, D_WGS84, E_WGS84, UM, -90, 0, 0, 0, 0.994, 2000000, 2000000},
    {32766, "WGS 84 / TM 36 SE", TM, D_WGS84, E_WGS84, UM, 0, 36, 0, 0, 0.9996, 500000, 10000000},
};
// clang-format on

struct GeoDef
{
    int code;
    const char* name;
    DatumId datum;
    EllipsoidId ellipsoid;
};

const GeoDef kGeographic[] = {
    {4326, "WGS 84", D_WGS84, E_WGS84},
    {4269, "NAD83", D_NAD83, E_GRS80},
    {4152, "NAD83(HARN)", D_NAD83_HARN, E_GRS80},
    {6318, "NAD83(2011)", D_NAD83_2011, E_GRS80},
    {4617, "NAD83(CSRS)", D_NAD83_CSRS, E_GRS80},
    {4258, "ETRS89", D_ETRS89, E_GRS80},
    {4283, "GDA94", D_GDA94, E_GRS80},
    {7844, "GDA2020", D_GDA2020, E_GRS80},
    {4674, "SIRGAS 2000", D_SIRGAS2000, E_GRS80},
    {4167, "NZGD2000", D_NZGD2000, E_GRS80},
    {4171, "RGF93 v1", D_RGF93, E_GRS80},
    {6668, "JGD2011", D_JGD2011, E_GRS80},
    {4619, "SWEREF99", D_SWEREF99, E_GRS80},
};

/// UTM-style ranges: codes [first, last] map to zones first+zoneOffset.
struct UtmRange
{
    int first, last, firstZone;
    bool south;
    DatumId datum;
    EllipsoidId ellipsoid;
    const char* prefix; ///< name before the zone number
    const char* suffixN;
};

const UtmRange kUtmRanges[] = {
    {32601, 32660, 1, false, D_WGS84, E_WGS84, "WGS 84 / UTM zone ", "N"},
    {32701, 32760, 1, true, D_WGS84, E_WGS84, "WGS 84 / UTM zone ", "S"},
    {26901, 26923, 1, false, D_NAD83, E_GRS80, "NAD83 / UTM zone ", "N"},
    {6330, 6348, 1, false, D_NAD83_2011, E_GRS80, "NAD83(2011) / UTM zone ", "N"},
    {25828, 25838, 28, false, D_ETRS89, E_GRS80, "ETRS89 / UTM zone ", "N"},
    {28348, 28358, 48, true, D_GDA94, E_GRS80, "GDA94 / MGA zone ", ""},
    {7846, 7859, 46, true, D_GDA2020, E_GRS80, "GDA2020 / MGA zone ", ""},
    {31965, 31976, 11, false, D_SIRGAS2000, E_GRS80, "SIRGAS 2000 / UTM zone ", "N"},
    {31977, 31985, 17, true, D_SIRGAS2000, E_GRS80, "SIRGAS 2000 / UTM zone ", "S"},
};

void setEllipsoid(Crs& c, EllipsoidId e)
{
    c.a = 6378137.0;
    c.invf = e == E_WGS84 ? kWgs84InvF : kGrs80InvF;
}

double unitFactor(UnitId u)
{
    switch (u)
    {
    case UFTUS: return kUsSurveyFoot;
    case UFT: return kInternationalFoot;
    case UM: break;
    }
    return 1.0;
}

Crs webMercator()
{
    Crs c;
    c.method = Method::WebMercator;
    c.epsg = 3857;
    c.name = "WGS 84 / Pseudo-Mercator";
    c.datum = "WGS84";
    return c;
}

Crs utmCrs(int zone, bool south)
{
    Crs c;
    c.method = Method::TransverseMercator;
    c.lon0 = -183.0 + 6.0 * zone;
    c.k0 = 0.9996;
    c.falseEasting = 500000.0;
    c.falseNorthing = south ? 10000000.0 : 0.0;
    return c;
}

std::optional<Crs> utmFromEpsg(int code)
{
    for (const UtmRange& r : kUtmRanges)
    {
        if (code < r.first || code > r.last)
            continue;
        const int zone = r.firstZone + (code - r.first);
        Crs c = utmCrs(zone, r.south);
        c.epsg = code;
        c.name = r.prefix + std::to_string(zone) + r.suffixN;
        c.datum = datumLabel(r.datum);
        setEllipsoid(c, r.ellipsoid);
        return c;
    }
    return std::nullopt;
}

Crs fromRow(const Row& r)
{
    Crs c;
    c.epsg = r.code;
    c.name = r.name;
    c.datum = datumLabel(r.datum);
    setEllipsoid(c, r.ellipsoid);
    c.unitToMeters = unitFactor(r.unit);
    c.lat0 = r.lat0;
    c.lon0 = r.lon0;
    c.lat1 = r.lat1;
    c.lat2 = r.lat2;
    c.k0 = r.k0;
    c.falseEasting = r.fe * c.unitToMeters;
    c.falseNorthing = r.fn * c.unitToMeters;
    switch (r.kind)
    {
    case TM: c.method = Method::TransverseMercator; break;
    case LCC2: c.method = Method::LambertConformalConic2SP; break;
    case LCC1: c.method = Method::LambertConformalConic1SP; break;
    case AEA: c.method = Method::AlbersEqualArea; break;
    case MERC: c.method = Method::Mercator1SP; break;
    case MERC2:
        c.method = Method::Mercator1SP;
        c.k0 = mFn(r.lat1 * kDeg, ellipsoidOf(c).e2);
        c.lat1 = 0;
        break;
    case PS: c.method = Method::PolarStereographic; break;
    }
    return c;
}

const Row* findRow(int code)
{
    const Row* begin = std::begin(kRows);
    const Row* end = std::end(kRows);
    const Row* it =
        std::lower_bound(begin, end, code, [](const Row& r, int v) { return r.code < v; });
    return it != end && it->code == code ? it : nullptr;
}

/// Every built-in system, built once.
const std::vector<Crs>& allBuiltIn()
{
    static const std::vector<Crs> all = [] {
        std::vector<Crs> v;
        for (const GeoDef& g : kGeographic)
            if (auto c = fromEpsg(g.code))
                v.push_back(*c);
        v.push_back(webMercator());
        for (const UtmRange& r : kUtmRanges)
            for (int code = r.first; code <= r.last; ++code)
                v.push_back(*utmFromEpsg(code));
        for (const Row& r : kRows)
            v.push_back(fromRow(r));
        return v;
    }();
    return all;
}

// ---------------------------------------------------------------------------
// Equivalence and identification
// ---------------------------------------------------------------------------

bool nearAngle(double a, double b)
{
    return std::fabs(normalizeLon(a - b)) < 1e-8;
}

bool sameParallels(const Crs& a, const Crs& b)
{
    return (nearAngle(a.lat1, b.lat1) && nearAngle(a.lat2, b.lat2)) ||
           (nearAngle(a.lat1, b.lat2) && nearAngle(a.lat2, b.lat1));
}

/// Same projection and parameters (to well under a millimetre), ignoring names.
bool equivalentProjection(const Crs& a, const Crs& b)
{
    if (a.method != b.method)
        return false;
    if (std::fabs(a.a - b.a) > 1e-4)
        return false;
    const double fa = a.invf > 0 ? 1.0 / a.invf : 0.0, fb = b.invf > 0 ? 1.0 / b.invf : 0.0;
    if (std::fabs(fa - fb) > 1e-12)
        return false;
    if (a.method == Method::Geographic || a.method == Method::WebMercator)
        return a.method == Method::Geographic ||
               (nearAngle(a.lon0, b.lon0) && std::fabs(a.falseEasting - b.falseEasting) < 2e-3 &&
                std::fabs(a.falseNorthing - b.falseNorthing) < 2e-3);
    if (std::fabs(a.unitToMeters - b.unitToMeters) > 1e-12 * b.unitToMeters)
        return false;
    if (std::fabs(a.falseEasting - b.falseEasting) > 2e-3 ||
        std::fabs(a.falseNorthing - b.falseNorthing) > 2e-3)
        return false;
    if (!nearAngle(a.lon0, b.lon0))
        return false;
    const bool sameK = std::fabs(a.k0 - b.k0) < 1e-10;
    switch (a.method)
    {
    case Method::TransverseMercator:
    case Method::LambertConformalConic1SP: return nearAngle(a.lat0, b.lat0) && sameK;
    case Method::LambertConformalConic2SP:
        return nearAngle(a.lat0, b.lat0) && sameParallels(a, b) && sameK;
    case Method::AlbersEqualArea: return nearAngle(a.lat0, b.lat0) && sameParallels(a, b);
    case Method::Mercator1SP: return sameK;
    case Method::PolarStereographic:
        if (!nearAngle(a.lat0, b.lat0) || polarVariantB(a) != polarVariantB(b))
            return false;
        return polarVariantB(a) ? nearAngle(a.lat1, b.lat1) : sameK;
    default: return false;
    }
}

/// Fills epsg (and an empty name) when the definition matches a built-in system
/// on the same datum.
void identify(Crs& c)
{
    if (c.epsg != 0 || c.datum.empty() || !c.valid())
        return;
    for (const Crs& k : allBuiltIn())
    {
        if (k.datum == c.datum && equivalentProjection(c, k))
        {
            c.epsg = k.epsg;
            if (c.name.empty())
                c.name = k.name;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Building a projected Crs from a method name and named parameters
// ---------------------------------------------------------------------------

/// Parameters keyed by squashed name; angles in degrees, lengths in metres.
using ParamMap = std::map<std::string, double>;

std::optional<double> param(const ParamMap& p, std::initializer_list<const char*> names)
{
    for (const char* n : names)
    {
        auto it = p.find(n);
        if (it != p.end())
            return it->second;
    }
    return std::nullopt;
}

double paramOr(const ParamMap& p, std::initializer_list<const char*> names, double fallback)
{
    return param(p, names).value_or(fallback);
}

bool isLengthParam(const std::string& key)
{
    return contains(key, "easting") || contains(key, "northing");
}

/// methodKey is the squashed projection/method name.
std::optional<Crs> buildProjected(const std::string& methodKey, const ParamMap& p, Crs base)
{
    Crs c = std::move(base);
    const auto lat0 = param(p, {"latitudeoforigin", "latitudeofnaturalorigin",
                                "latitudeoffalseorigin", "latitudeofcenter", "latitudeofcentre",
                                "latitudeofprojectioncentre", "latitudeofprojectioncenter"});
    const double lon0 = paramOr(p,
                                {"centralmeridian", "longitudeoforigin", "longitudeofnaturalorigin",
                                 "longitudeoffalseorigin", "longitudeofcenter",
                                 "longitudeofcentre", "longitudeofprojectioncentre",
                                 "straightverticallongitudefrompole"},
                                0.0);
    const auto k0 = param(p, {"scalefactor", "scalefactoratnaturalorigin", "scalefactoratorigin"});
    const auto sp1 = param(p, {"standardparallel1", "latitudeof1ststandardparallel",
                               "latitudeoffirststandardparallel", "latitudeofstandardparallel",
                               "standardparallel"});
    const auto sp2 = param(p, {"standardparallel2", "latitudeof2ndstandardparallel",
                               "latitudeofsecondstandardparallel"});
    c.lon0 = lon0;
    c.falseEasting = paramOr(p, {"falseeasting", "eastingatfalseorigin", "eastingatprojectioncentre"}, 0.0);
    c.falseNorthing =
        paramOr(p, {"falsenorthing", "northingatfalseorigin", "northingatprojectioncentre"}, 0.0);
    std::string m = methodKey;
    if ((m == "stereographic" || m == "obliquestereographic") && lat0 &&
        std::fabs(std::fabs(*lat0) - 90.0) < 1e-10)
        m = "polarstereographicvarianta"; // ESRI form of UPS and other polar grids

    if (m == "transversemercator" || m == "gausskruger" || m == "transversemercatorgausskruger")
    {
        c.method = Method::TransverseMercator;
        c.lat0 = lat0.value_or(0.0);
        c.k0 = k0.value_or(1.0);
        return c;
    }
    if (startsWith(m, "lambertconformalconic") || startsWith(m, "lambertconicconformal"))
    {
        const std::string rest = m.substr(21);
        if (!(rest.empty() || rest == "1sp" || rest == "2sp"))
            return std::nullopt; // Belgium, Michigan, west orientated ...
        c.lat0 = lat0.value_or(sp1.value_or(0.0));
        c.k0 = k0.value_or(1.0);
        if (rest == "1sp")
        {
            c.method = Method::LambertConformalConic1SP;
            c.lat1 = c.lat2 = c.lat0;
            return c;
        }
        c.lat1 = sp1.value_or(c.lat0);
        c.lat2 = sp2.value_or(c.lat1);
        if (rest.empty() && std::fabs(c.lat1 - c.lat2) < 1e-10 && std::fabs(c.lat1 - c.lat0) < 1e-10)
        {
            c.method = Method::LambertConformalConic1SP; // ESRI form of a 1SP definition
            return c;
        }
        c.method = Method::LambertConformalConic2SP;
        return c;
    }
    if (m == "mercatorauxiliarysphere" || m == "popularvisualisationpseudomercator" ||
        m == "popularvisualizationpseudomercator" || m == "webmercator" || m == "pseudomercator")
    {
        Crs w = webMercator();
        w.epsg = 0;
        w.name = c.name;
        w.lon0 = c.lon0;
        w.falseEasting = c.falseEasting;
        w.falseNorthing = c.falseNorthing;
        w.unitToMeters = c.unitToMeters;
        return w;
    }
    if (m == "mercator" || m == "mercator1sp" || m == "mercator2sp" || m == "mercatorvarianta" ||
        m == "mercatorvariantb")
    {
        c.method = Method::Mercator1SP;
        if ((m == "mercator" || m == "mercator2sp" || m == "mercatorvariantb") && sp1)
            c.k0 = mFn(*sp1 * kDeg, ellipsoidOf(c).e2);
        else
            c.k0 = k0.value_or(1.0);
        return c;
    }
    if (m == "albers" || m == "albersconicequalarea" || m == "albersequalarea")
    {
        c.method = Method::AlbersEqualArea;
        c.lat0 = lat0.value_or(0.0);
        c.lat1 = sp1.value_or(c.lat0);
        c.lat2 = sp2.value_or(c.lat1);
        return c;
    }
    if (startsWith(m, "polarstereographic") || m == "stereographicnorthpole" ||
        m == "stereographicsouthpole")
    {
        c.method = Method::PolarStereographic;
        const double ts = sp1.value_or(lat0.value_or(m == "stereographicsouthpole" ? -90.0 : 90.0));
        bool north = ts > 0;
        if (m == "stereographicnorthpole")
            north = true;
        else if (m == "stereographicsouthpole")
            north = false;
        c.lat0 = north ? 90.0 : -90.0;
        if (m != "polarstereographicvarianta" && std::fabs(std::fabs(ts) - 90.0) > 1e-10)
        {
            c.lat1 = ts; // variant B: latitude of true scale
            c.k0 = 1.0;
        }
        else
        {
            c.lat1 = 0;
            c.k0 = k0.value_or(1.0);
        }
        return c;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// WKT (OGC WKT1, ESRI .prj and WKT2)
// ---------------------------------------------------------------------------

struct WktNode
{
    std::string key;
    std::vector<std::string> strs; ///< quoted strings and bare words, in order
    std::vector<double> nums;
    std::vector<WktNode> kids;

    const WktNode* kid(std::initializer_list<const char*> keys) const
    {
        for (const WktNode& k : kids)
            for (const char* wanted : keys)
                if (k.key == wanted)
                    return &k;
        return nullptr;
    }
    const WktNode* find(std::initializer_list<const char*> keys) const
    {
        if (const WktNode* k = kid(keys))
            return k;
        for (const WktNode& k : kids)
            if (const WktNode* f = k.find(keys))
                return f;
        return nullptr;
    }
    std::string str(size_t i) const { return i < strs.size() ? strs[i] : std::string(); }
};

class WktParser
{
public:
    explicit WktParser(std::string_view text) : s_(text) {}

    bool parse(WktNode& out)
    {
        if (!node(out, 0))
            return false;
        ws();
        return pos_ == s_.size();
    }

private:
    std::string_view s_;
    size_t pos_ = 0;

    void ws()
    {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_])))
            ++pos_;
    }
    bool identifier(std::string& out)
    {
        ws();
        const size_t start = pos_;
        while (pos_ < s_.size() &&
               (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
            ++pos_;
        out.assign(s_.substr(start, pos_ - start));
        return !out.empty();
    }
    bool node(WktNode& n, int depth)
    {
        if (depth > 64 || !identifier(n.key))
            return false;
        for (char& ch : n.key)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        ws();
        if (pos_ >= s_.size() || (s_[pos_] != '[' && s_[pos_] != '('))
            return false;
        const char close = s_[pos_] == '[' ? ']' : ')';
        ++pos_;
        for (;;)
        {
            ws();
            if (pos_ >= s_.size())
                return false;
            const char ch = s_[pos_];
            if (ch == close)
            {
                ++pos_;
                return true;
            }
            if (ch == '"')
            {
                std::string v;
                ++pos_;
                for (;;)
                {
                    if (pos_ >= s_.size())
                        return false;
                    if (s_[pos_] == '"')
                    {
                        if (pos_ + 1 < s_.size() && s_[pos_ + 1] == '"')
                        {
                            v.push_back('"');
                            pos_ += 2;
                            continue;
                        }
                        ++pos_;
                        break;
                    }
                    v.push_back(s_[pos_++]);
                }
                n.strs.push_back(std::move(v));
            }
            else if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' ||
                     ch == '.')
            {
                const size_t start = pos_;
                while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) ||
                                            s_[pos_] == '-' || s_[pos_] == '+' || s_[pos_] == '.'))
                    ++pos_;
                double v = 0;
                if (!parseDouble(s_.substr(start, pos_ - start), v))
                    return false;
                n.nums.push_back(v);
            }
            else
            {
                const size_t save = pos_;
                std::string word;
                if (!identifier(word))
                    return false;
                ws();
                if (pos_ < s_.size() && (s_[pos_] == '[' || s_[pos_] == '('))
                {
                    pos_ = save;
                    WktNode k;
                    if (!node(k, depth + 1))
                        return false;
                    n.kids.push_back(std::move(k));
                }
                else
                {
                    n.strs.push_back(std::move(word));
                }
            }
            ws();
            if (pos_ < s_.size() && s_[pos_] == ',')
                ++pos_;
        }
    }
};

/// EPSG (or ESRI) code from an AUTHORITY / ID child of a node.
int authorityCode(const WktNode& n, bool& esri)
{
    esri = false;
    const WktNode* a = n.kid({"AUTHORITY", "ID"});
    if (!a || a->strs.empty())
        return 0;
    const std::string auth = lower(a->strs[0]);
    if (auth != "epsg" && auth != "esri")
        return 0;
    esri = auth == "esri";
    int code = 0;
    if (!a->nums.empty())
        code = static_cast<int>(a->nums[0]);
    else if (a->strs.size() > 1)
        parseInt(a->strs[1], code);
    return code;
}

/// Unit factor of a UNIT / LENGTHUNIT / ANGLEUNIT node, or fallback.
double unitOf(const WktNode* u, double fallback)
{
    if (u && !u->nums.empty() && u->nums[0] > 0)
        return u->nums[0];
    return fallback;
}

/// Exact factor for US survey and international feet written with 15 digits.
double snapUnit(double f)
{
    for (double exact : {kUsSurveyFoot, kInternationalFoot, 1.0})
        if (std::fabs(f - exact) < 1e-12 * exact)
            return exact;
    return f;
}

/// Ellipsoid and datum from a GEOGCS / BASEGEOGCRS / GEOGCRS node.
void readGeodetic(const WktNode& g, Crs& c)
{
    if (const WktNode* d = g.find({"DATUM", "ENSEMBLE", "TRF", "GEODETICDATUM"}))
    {
        const std::string raw = d->str(0);
        const std::string label = canonicalDatum(raw);
        c.datum = label.empty() ? raw : label;
    }
    if (const WktNode* sph = g.find({"SPHEROID", "ELLIPSOID"}))
    {
        if (sph->nums.size() >= 2 && sph->nums[0] > 0)
        {
            c.a = sph->nums[0] * unitOf(sph->kid({"LENGTHUNIT", "UNIT"}), 1.0);
            c.invf = sph->nums[1];
        }
    }
}

std::optional<Crs> fromWktNode(const WktNode& root)
{
    const std::string& key = root.key;
    if (key == "COMPD_CS" || key == "COMPOUNDCRS")
    {
        for (const WktNode& k : root.kids)
            if (auto c = fromWktNode(k))
                return c;
        return std::nullopt;
    }
    if (key == "BOUNDCRS")
    {
        if (const WktNode* s = root.kid({"SOURCECRS"}))
            if (!s->kids.empty())
                return fromWktNode(s->kids.front());
        return std::nullopt;
    }
    const bool projected = key == "PROJCS" || key == "PROJCRS" || key == "PROJECTEDCRS";
    const bool geographic = key == "GEOGCS" || key == "GEOGCRS" || key == "GEOGRAPHICCRS" ||
                            key == "GEODCRS" || key == "GEODETICCRS";
    if (!projected && !geographic)
        return std::nullopt;

    bool esri = false;
    const int code = authorityCode(root, esri);
    if (esri && (code == 102100 || code == 102113 || code == 3857))
        return webMercator();

    Crs c;
    c.name = root.str(0);
    c.datum.clear();
    if (geographic)
    {
        c.method = Method::Geographic;
        readGeodetic(root, c);
    }
    else
    {
        const bool wkt2 = key != "PROJCS";
        const WktNode* g = root.kid({"GEOGCS", "BASEGEOGCRS", "BASEGEODCRS", "GEOGCRS"});
        if (g)
            readGeodetic(*g, c);
        double primeMeridian = 0;
        if (g)
            if (const WktNode* pm = g->kid({"PRIMEM", "PRIMEMERIDIAN"}))
                if (!pm->nums.empty())
                    primeMeridian = pm->nums[0];

        // Linear unit of the coordinates.
        const WktNode* unitNode = root.kid({"UNIT", "LENGTHUNIT"});
        if (!unitNode)
            if (const WktNode* axis = root.kid({"AXIS"}))
                unitNode = axis->kid({"LENGTHUNIT", "UNIT"});
        c.unitToMeters = snapUnit(unitOf(unitNode, 1.0));

        std::string methodName;
        ParamMap params;
        const WktNode* holder = &root;
        if (wkt2)
        {
            holder = root.kid({"CONVERSION", "DERIVINGCONVERSION"});
            if (!holder)
                return std::nullopt;
            if (const WktNode* m = holder->kid({"METHOD", "PROJECTION"}))
                methodName = m->str(0);
        }
        else if (const WktNode* m = root.kid({"PROJECTION"}))
        {
            methodName = m->str(0);
        }
        for (const WktNode& p : holder->kids)
        {
            if (p.key != "PARAMETER" || p.nums.empty())
                continue;
            const std::string pk = squash(p.str(0));
            double v = p.nums[0];
            if (wkt2)
            {
                if (const WktNode* u = p.kid({"ANGLEUNIT"}))
                    v = v * unitOf(u, kDeg) / kDeg;
                else if (const WktNode* lu = p.kid({"LENGTHUNIT"}))
                    v = v * snapUnit(unitOf(lu, 1.0));
                else if (isLengthParam(pk))
                    v = v * c.unitToMeters;
            }
            else if (isLengthParam(pk))
            {
                v = v * c.unitToMeters; // WKT1 false easting/northing use the CRS unit
            }
            params[pk] = v;
        }

        std::string mk = squash(methodName);
        const std::string nameKey = squash(c.name);
        bool web = contains(nameKey, "webmercator") || contains(nameKey, "pseudomercator") ||
                   contains(nameKey, "googlemapsglobalmercator");
        if (const WktNode* ext = root.kid({"EXTENSION"}))
            if (ext->strs.size() > 1 && contains(ext->strs[1], "+proj=merc") &&
                contains(ext->strs[1], "+a=6378137") && contains(ext->strs[1], "+b=6378137"))
                web = true;
        if (startsWith(mk, "mercator") && web)
            mk = "popularvisualisationpseudomercator";
        auto built = buildProjected(mk, params, c);
        if (!built)
        {
            if (code > 0 && !esri)
                if (auto k = fromEpsg(code))
                {
                    if (!c.name.empty())
                        k->name = c.name;
                    return k;
                }
            return std::nullopt;
        }
        c = *built;
        if (primeMeridian != 0 && c.method != Method::WebMercator)
            c.lon0 = normalizeLon(c.lon0 + primeMeridian);
    }

    if (code > 0 && !esri)
    {
        if (auto k = fromEpsg(code))
        {
            // The authority code is definitive; keep the file's own name.
            if (!c.name.empty())
                k->name = c.name;
            if (!c.datum.empty() && k->datum.empty())
                k->datum = c.datum;
            return k;
        }
        c.epsg = code;
        return c;
    }
    identify(c);
    return c;
}

std::optional<Crs> fromWkt(std::string_view text)
{
    WktNode root;
    WktParser parser(text);
    if (!parser.parse(root))
        return std::nullopt;
    return fromWktNode(root);
}

// ---------------------------------------------------------------------------
// PROJ strings
// ---------------------------------------------------------------------------

bool namedEllipsoid(const std::string& name, double& a, double& invf)
{
    struct E
    {
        const char* name;
        double a, invf;
    };
    static const E kEllipsoids[] = {
        {"wgs84", 6378137.0, kWgs84InvF},     {"grs80", 6378137.0, kGrs80InvF},
        {"clrk66", 6378206.4, 294.978698213898}, {"intl", 6378388.0, 297.0},
        {"bessel", 6377397.155, 299.1528128}, {"airy", 6377563.396, 299.3249646},
        {"krass", 6378245.0, 298.3},          {"wgs72", 6378135.0, 298.26},
        {"clrk80ign", 6378249.2, 293.4660212936269}, {"sphere", 6370997.0, 0.0},
    };
    for (const E& e : kEllipsoids)
        if (name == e.name)
        {
            a = e.a;
            invf = e.invf;
            return true;
        }
    return false;
}

bool linearUnit(const std::string& u, double& f)
{
    static const std::pair<const char*, double> kUnits[] = {
        {"m", 1.0},          {"km", 1000.0},           {"dm", 0.1},
        {"cm", 0.01},        {"mm", 0.001},            {"ft", kInternationalFoot},
        {"us-ft", kUsSurveyFoot}, {"yd", 0.9144},       {"us-yd", 3600.0 / 3937.0},
        {"mi", 1609.344},    {"us-mi", 6336000.0 / 3937.0}, {"in", 0.0254},
        {"us-in", 100.0 / 3937.0}, {"ch", 20.1168},     {"link", 0.201168},
    };
    for (const auto& kv : kUnits)
        if (u == kv.first)
        {
            f = kv.second;
            return true;
        }
    return false;
}

std::optional<Crs> fromProjString(std::string_view text)
{
    std::map<std::string, std::string> kv;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        const size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::string_view tok = text.substr(start, i - start);
        if (tok.empty())
            continue;
        if (tok.front() == '+')
            tok.remove_prefix(1);
        const size_t eq = tok.find('=');
        const std::string k = lower(tok.substr(0, eq));
        kv[k] = eq == std::string_view::npos ? std::string() : std::string(tok.substr(eq + 1));
    }
    auto has = [&](const char* k) { return kv.count(k) != 0; };
    auto num = [&](const char* k, double& out) {
        auto it = kv.find(k);
        if (it == kv.end())
            return false;
        // Accept DMS-free decimal degrees only.
        return parseDouble(it->second, out);
    };

    if (has("init"))
    {
        const std::string v = lower(kv["init"]);
        int code = 0;
        if (startsWith(v, "epsg:") && parseInt(std::string_view(v).substr(5), code))
            return fromEpsg(code);
        return std::nullopt;
    }
    if (!has("proj"))
        return std::nullopt;
    const std::string proj = lower(kv["proj"]);

    Crs c;
    c.invf = kGrs80InvF; // PROJ's default ellipsoid
    if (has("datum"))
    {
        const std::string d = lower(kv["datum"]);
        if (d == "wgs84")
        {
            c.datum = "WGS84";
            c.invf = kWgs84InvF;
        }
        else if (d == "nad83")
            c.datum = "NAD83";
        else if (d == "nad27")
        {
            c.datum = "NAD27";
            namedEllipsoid("clrk66", c.a, c.invf);
        }
        else
        {
            const std::string label = canonicalDatum(kv["datum"]);
            c.datum = label.empty() ? kv["datum"] : label;
        }
    }
    if (has("ellps"))
    {
        if (!namedEllipsoid(lower(kv["ellps"]), c.a, c.invf))
            return std::nullopt;
    }
    double v = 0;
    if (num("r", v) && v > 0)
    {
        c.a = v;
        c.invf = 0;
    }
    if (num("a", v) && v > 0)
    {
        c.a = v;
        double b = 0;
        if (num("rf", v))
            c.invf = v;
        else if (num("f", v))
            c.invf = v > 0 ? 1.0 / v : 0.0;
        else if (num("b", b) && b > 0)
            c.invf = b < c.a ? c.a / (c.a - b) : 0.0;
        else if (num("es", v))
            c.invf = v > 0 ? 1.0 / (1.0 - std::sqrt(1.0 - v)) : 0.0;
    }
    else if (num("rf", v))
    {
        c.invf = v;
    }
    if (has("units"))
    {
        if (!linearUnit(lower(kv["units"]), c.unitToMeters))
            return std::nullopt;
    }
    if (num("to_meter", v) && v > 0)
        c.unitToMeters = v;

    double lat0 = 0, lon0 = 0, lat1 = 0, lat2 = 0, latTs = 0, k = 1, x0 = 0, y0 = 0;
    const bool hasLat0 = num("lat_0", lat0);
    num("lon_0", lon0);
    const bool hasLat1 = num("lat_1", lat1);
    const bool hasLat2 = num("lat_2", lat2);
    const bool hasLatTs = num("lat_ts", latTs);
    if (!num("k_0", k))
        num("k", k);
    num("x_0", x0);
    num("y_0", y0);
    c.lat0 = lat0;
    c.lon0 = lon0;
    c.k0 = k;
    c.falseEasting = x0; // PROJ false easting/northing are always metres
    c.falseNorthing = y0;

    if (proj == "longlat" || proj == "latlong" || proj == "lonlat" || proj == "latlon")
    {
        c.method = Method::Geographic;
        c.lat0 = c.lon0 = 0;
        c.k0 = 1;
        c.falseEasting = c.falseNorthing = 0;
        c.unitToMeters = 1;
    }
    else if (proj == "webmerc" || (proj == "merc" && c.a == kWebRadius && c.invf == 0 &&
                                   latTs == 0 && c.k0 == 1.0))
    {
        Crs w = webMercator();
        w.epsg = 0;
        w.name.clear();
        w.lon0 = c.lon0;
        w.falseEasting = c.falseEasting;
        w.falseNorthing = c.falseNorthing;
        w.unitToMeters = c.unitToMeters;
        if (w.lon0 == 0 && w.falseEasting == 0 && w.falseNorthing == 0 && w.unitToMeters == 1)
            return webMercator();
        return w;
    }
    else if (proj == "merc")
    {
        c.method = Method::Mercator1SP;
        c.lat0 = 0;
        if (hasLatTs)
            c.k0 = mFn(latTs * kDeg, ellipsoidOf(c).e2);
    }
    else if (proj == "tmerc" || proj == "etmerc")
    {
        c.method = Method::TransverseMercator;
    }
    else if (proj == "utm")
    {
        int zone = 0;
        if (!has("zone") || !parseInt(kv["zone"], zone) || zone < 1 || zone > 60)
            return std::nullopt;
        const Crs u = utmCrs(zone, has("south"));
        c.method = u.method;
        c.lat0 = 0;
        c.lon0 = u.lon0;
        c.k0 = u.k0;
        c.falseEasting = u.falseEasting;
        c.falseNorthing = u.falseNorthing;
    }
    else if (proj == "lcc")
    {
        if (!hasLat1)
            return std::nullopt;
        if (!hasLat2)
        {
            lat2 = lat1;
            if (!hasLat0)
                c.lat0 = lat1; // as PROJ does for a one-parallel cone
        }
        c.lat1 = lat1;
        c.lat2 = lat2;
        if (std::fabs(lat1 - lat2) < 1e-10 && std::fabs(lat1 - c.lat0) < 1e-10)
            c.method = Method::LambertConformalConic1SP;
        else
            c.method = Method::LambertConformalConic2SP;
    }
    else if (proj == "aea")
    {
        if (!hasLat1)
            return std::nullopt;
        c.method = Method::AlbersEqualArea;
        c.lat1 = lat1;
        c.lat2 = hasLat2 ? lat2 : lat1;
        c.k0 = 1;
    }
    else if (proj == "stere" || proj == "ups")
    {
        if (proj == "ups")
        {
            c.lat0 = has("south") ? -90.0 : 90.0;
            c.lon0 = 0;
            c.k0 = 0.994;
            c.falseEasting = c.falseNorthing = 2000000.0;
        }
        if (std::fabs(std::fabs(c.lat0) - 90.0) > 1e-10)
            return std::nullopt; // oblique stereographic is not supported
        c.method = Method::PolarStereographic;
        if (hasLatTs && std::fabs(std::fabs(latTs) - 90.0) > 1e-10)
        {
            c.lat1 = latTs;
            c.k0 = 1;
        }
    }
    else
    {
        return std::nullopt;
    }
    identify(c);
    return c;
}

std::optional<Crs> fromCodeText(std::string_view text)
{
    const std::string t = lower(text);
    auto digitsAfterLast = [&](char sep) {
        int code = 0;
        const size_t p = t.find_last_of(sep);
        if (p != std::string::npos && parseInt(std::string_view(t).substr(p + 1), code))
            return code;
        return 0;
    };
    if (t == "crs84" || t == "ogc:crs84" || t == "urn:ogc:def:crs:ogc:1.3:crs84" ||
        t == "urn:ogc:def:crs:ogc::crs84")
        return fromEpsg(4326);
    if (startsWith(t, "esri:"))
    {
        const int code = digitsAfterLast(':');
        if (code == 102100 || code == 102113)
            return webMercator();
        return std::nullopt;
    }
    if (startsWith(t, "epsg:") || startsWith(t, "urn:ogc:def:crs:epsg:"))
        return fromEpsg(digitsAfterLast(':'));
    if (contains(t, "opengis.net/def/crs/epsg/"))
        return fromEpsg(digitsAfterLast('/'));
    int code = 0;
    if (parseInt(t, code))
        return fromEpsg(code);
    return std::nullopt;
}

bool sane(const Crs& c)
{
    return c.valid() && c.a > 0 && std::isfinite(c.a) && c.invf >= 0 && c.unitToMeters > 0 &&
           c.k0 > 0 && std::isfinite(c.k0);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<Crs> parse(std::string_view text)
{
    text = trim(text);
    if (text.empty())
        return std::nullopt;
    if (text.front() == '+' || contains(lower(text.substr(0, 512)), "proj="))
        return fromProjString(text);
    if (auto c = fromCodeText(text))
        return c;
    if (std::isalpha(static_cast<unsigned char>(text.front())) &&
        text.find_first_of("[(") != std::string_view::npos)
        return fromWkt(text);
    return std::nullopt;
}

std::optional<Crs> fromEpsg(int code)
{
    if (code == 3857 || code == 900913)
        return webMercator();
    for (const GeoDef& g : kGeographic)
    {
        if (g.code != code)
            continue;
        Crs c;
        c.method = Method::Geographic;
        c.epsg = code;
        c.name = g.name;
        c.datum = datumLabel(g.datum);
        setEllipsoid(c, g.ellipsoid);
        return c;
    }
    if (auto u = utmFromEpsg(code))
        return u;
    if (const Row* r = findRow(code))
        return fromRow(*r);
    return std::nullopt;
}

bool toLonLat(const Crs& crs, double x, double y, double& lon, double& lat)
{
    if (!sane(crs) || !std::isfinite(x) || !std::isfinite(y))
        return false;
    if (crs.method == Method::Geographic)
    {
        lon = x;
        lat = y;
        return std::fabs(lat) <= 90.0;
    }
    const double xm = x * crs.unitToMeters, ym = y * crs.unitToMeters;
    bool ok = false;
    switch (crs.method)
    {
    case Method::WebMercator:
        webMercatorToLonLat(xm - crs.falseEasting, ym - crs.falseNorthing, lon, lat);
        lon = normalizeLon(lon + crs.lon0);
        ok = true;
        break;
    case Method::TransverseMercator: ok = tmInverse(crs, xm, ym, lon, lat); break;
    case Method::LambertConformalConic1SP:
    case Method::LambertConformalConic2SP: ok = lccInverse(crs, xm, ym, lon, lat); break;
    case Method::Mercator1SP: ok = mercInverse(crs, xm, ym, lon, lat); break;
    case Method::AlbersEqualArea: ok = aeaInverse(crs, xm, ym, lon, lat); break;
    case Method::PolarStereographic: ok = psInverse(crs, xm, ym, lon, lat); break;
    default: return false;
    }
    return ok && std::isfinite(lon) && std::isfinite(lat);
}

bool fromLonLat(const Crs& crs, double lon, double lat, double& x, double& y)
{
    if (!sane(crs) || !std::isfinite(lon) || !std::isfinite(lat) || std::fabs(lat) > 90.0)
        return false;
    if (crs.method == Method::Geographic)
    {
        x = lon;
        y = lat;
        return true;
    }
    double xm = 0, ym = 0;
    bool ok = false;
    switch (crs.method)
    {
    case Method::WebMercator:
        if (std::fabs(lat) >= 90.0)
            return false;
        xm = crs.falseEasting + kWebRadius * normalizeLon(lon - crs.lon0) * kDeg;
        ym = crs.falseNorthing + kWebRadius * std::asinh(std::tan(lat * kDeg));
        ok = true;
        break;
    case Method::TransverseMercator: ok = tmForward(crs, lon, lat, xm, ym); break;
    case Method::LambertConformalConic1SP:
    case Method::LambertConformalConic2SP: ok = lccForward(crs, lon, lat, xm, ym); break;
    case Method::Mercator1SP: ok = mercForward(crs, lon, lat, xm, ym); break;
    case Method::AlbersEqualArea: ok = aeaForward(crs, lon, lat, xm, ym); break;
    case Method::PolarStereographic: ok = psForward(crs, lon, lat, xm, ym); break;
    default: return false;
    }
    if (!ok || !std::isfinite(xm) || !std::isfinite(ym))
        return false;
    x = xm / crs.unitToMeters;
    y = ym / crs.unitToMeters;
    return true;
}

bool transform(const Crs& from, const Crs& to, double x, double y, double& outX, double& outY)
{
    double lon = 0, lat = 0;
    return toLonLat(from, x, y, lon, lat) && fromLonLat(to, lon, lat, outX, outY);
}

std::string toString(const Crs& crs)
{
    if (!crs.valid())
        return {};
    if (crs.epsg > 0)
        if (auto k = fromEpsg(crs.epsg); k && equivalentProjection(crs, *k))
            return "EPSG:" + std::to_string(crs.epsg);

    std::string s;
    auto add = [&](const char* key, double v) {
        s += " +";
        s += key;
        s += '=';
        s += fmtNum(v);
    };
    switch (crs.method)
    {
    case Method::Geographic: s = "+proj=longlat"; break;
    case Method::WebMercator:
        s = "+proj=webmerc +lat_0=0";
        add("lon_0", crs.lon0);
        break;
    case Method::TransverseMercator:
        s = "+proj=tmerc";
        add("lat_0", crs.lat0);
        add("lon_0", crs.lon0);
        add("k_0", crs.k0);
        break;
    case Method::LambertConformalConic1SP:
        s = "+proj=lcc";
        add("lat_1", crs.lat0);
        add("lat_0", crs.lat0);
        add("lon_0", crs.lon0);
        add("k_0", crs.k0);
        break;
    case Method::LambertConformalConic2SP:
        s = "+proj=lcc";
        add("lat_0", crs.lat0);
        add("lon_0", crs.lon0);
        add("lat_1", crs.lat1);
        add("lat_2", crs.lat2);
        if (crs.k0 != 1.0)
            add("k_0", crs.k0);
        break;
    case Method::Mercator1SP:
        s = "+proj=merc";
        add("lon_0", crs.lon0);
        add("k_0", crs.k0);
        break;
    case Method::AlbersEqualArea:
        s = "+proj=aea";
        add("lat_0", crs.lat0);
        add("lon_0", crs.lon0);
        add("lat_1", crs.lat1);
        add("lat_2", crs.lat2);
        break;
    case Method::PolarStereographic:
        s = "+proj=stere";
        add("lat_0", crs.lat0 > 0 ? 90.0 : -90.0);
        add("lon_0", crs.lon0);
        if (polarVariantB(crs))
            add("lat_ts", crs.lat1);
        else
            add("k_0", crs.k0);
        break;
    default: return {};
    }
    if (crs.method != Method::Geographic)
    {
        add("x_0", crs.falseEasting);
        add("y_0", crs.falseNorthing);
    }
    if (crs.datum == "WGS84" && crs.a == 6378137.0 && crs.invf == kWgs84InvF)
        s += " +datum=WGS84";
    else if (crs.datum == "NAD83" && crs.a == 6378137.0 && crs.invf == kGrs80InvF)
        s += " +datum=NAD83";
    else if (crs.a == 6378137.0 && crs.invf == kWgs84InvF)
        s += " +ellps=WGS84";
    else if (crs.a == 6378137.0 && crs.invf == kGrs80InvF)
        s += " +ellps=GRS80";
    else if (crs.invf == 0)
        add("R", crs.a);
    else
    {
        add("a", crs.a);
        add("rf", crs.invf);
    }
    if (crs.method != Method::Geographic)
    {
        if (crs.unitToMeters == 1.0)
            s += " +units=m";
        else if (crs.unitToMeters == kUsSurveyFoot)
            s += " +units=us-ft";
        else if (crs.unitToMeters == kInternationalFoot)
            s += " +units=ft";
        else
            add("to_meter", crs.unitToMeters);
    }
    s += " +no_defs";
    return s;
}

bool isWgs84Compatible(const Crs& crs)
{
    if (!crs.valid())
        return false;
    if (crs.method == Method::WebMercator)
        return true;
    if (!crs.datum.empty())
        return compatibleDatumLabel(crs.datum);
    // Unknown datum: WGS 84 and GRS 1980 ellipsoids are used by geocentric datums.
    return crs.a == 6378137.0 && (std::fabs(crs.invf - kWgs84InvF) < 1e-6 ||
                                  std::fabs(crs.invf - kGrs80InvF) < 1e-6);
}

std::vector<Entry> commonSystems()
{
    std::vector<Entry> head, rest;
    for (const Crs& c : allBuiltIn())
    {
        if (c.epsg == 4326 || c.epsg == 3857)
            head.push_back({c.epsg, c.name});
        else
            rest.push_back({c.epsg, c.name});
    }
    std::sort(head.begin(), head.end(), [](const Entry& a, const Entry& b) {
        return a.epsg == 4326 && b.epsg != 4326;
    });
    std::sort(rest.begin(), rest.end(),
              [](const Entry& a, const Entry& b) { return a.epsg < b.epsg; });
    head.insert(head.end(), rest.begin(), rest.end());
    return head;
}

std::optional<Crs> guessFromExtent(double minX, double minY, double maxX, double maxY)
{
    const bool finite = std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) &&
                        std::isfinite(maxY);
    if (!finite || minX > maxX || minY > maxY)
        return std::nullopt;
    if (minX < -180.0 || maxX > 180.0 || minY < -90.0 || maxY > 90.0)
        return std::nullopt;
    if (maxX - minX >= 20.0 || maxY - minY >= 20.0)
        return std::nullopt;
    return fromEpsg(4326);
}

void lonLatToWebMercator(double lon, double lat, double& x, double& y)
{
    lat = std::max(-kWebMaxLat, std::min(kWebMaxLat, lat));
    x = kWebRadius * lon * kDeg;
    y = kWebRadius * std::asinh(std::tan(lat * kDeg));
}

void webMercatorToLonLat(double x, double y, double& lon, double& lat)
{
    lon = x / kWebRadius / kDeg;
    lat = std::atan(std::sinh(y / kWebRadius)) / kDeg;
}

} // namespace mirab::crs
