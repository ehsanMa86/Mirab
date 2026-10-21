/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Geometry.cpp
 * Purpose: Plane geometry of model elements, region selection and geodesic measurement.
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
#include "mirab/core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mirab::core
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
// WGS 84
constexpr double kA = 6378137.0;
constexpr double kF = 1.0 / 298.257223563;

double cross(const Point& o, const Point& a, const Point& b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool onSegment(const Point& a, const Point& b, const Point& p)
{
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
           p.y <= std::max(a.y, b.y);
}

struct Box
{
    double minX = std::numeric_limits<double>::max(), minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest(), maxY = std::numeric_limits<double>::lowest();
    explicit Box(const std::vector<Point>& pts)
    {
        for (const auto& p : pts)
        {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    }
    /// Gap between two boxes (0 when they overlap).
    double gap(const Box& o) const
    {
        const double dx = std::max({0.0, o.minX - maxX, minX - o.maxX});
        const double dy = std::max({0.0, o.minY - maxY, minY - o.maxY});
        return std::hypot(dx, dy);
    }
};

/// Shape of an element for distance and region tests.
struct Shape
{
    std::vector<Point> points;
    bool ring = false; ///< subcatchment outline (closed, has an inside)
};

Shape shapeOf(const Model& model, const ElementRef& e)
{
    Shape s;
    s.points = elementGeometry(model, e);
    s.ring = e.category == Category::Area && s.points.size() >= 3;
    return s;
}

/// Segments of a shape as index pairs; a single point is a zero-length segment.
template <typename Fn> void forSegments(const Shape& s, Fn&& fn)
{
    const std::size_t n = s.points.size();
    if (n == 1)
    {
        fn(s.points[0], s.points[0]);
        return;
    }
    for (std::size_t i = 0; i + 1 < n; ++i)
        fn(s.points[i], s.points[i + 1]);
    if (s.ring && n >= 3)
        fn(s.points[n - 1], s.points[0]);
}

double shapeDistance(const Shape& a, const Shape& b)
{
    if (a.points.empty() || b.points.empty())
        return std::numeric_limits<double>::infinity();
    if (a.ring && pointInPolygon(a.points, b.points[0]))
        return 0.0;
    if (b.ring && pointInPolygon(b.points, a.points[0]))
        return 0.0;
    double best = std::numeric_limits<double>::infinity();
    forSegments(a, [&](const Point& p, const Point& q) {
        forSegments(b, [&](const Point& r, const Point& s) { best = std::min(best, segmentsDistance(p, q, r, s)); });
    });
    return best;
}

/// Authalic latitude (radians) of a geodetic latitude on WGS 84, and the
/// radius of the sphere with the ellipsoid's surface area.
double qFunc(double sinPhi, double e)
{
    const double e2 = e * e;
    return (1 - e2) * (sinPhi / (1 - e2 * sinPhi * sinPhi) -
                       1.0 / (2 * e) * std::log((1 - e * sinPhi) / (1 + e * sinPhi)));
}
} // namespace

double distance(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

double segmentDistance(const Point& p, const Point& a, const Point& b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

bool segmentsIntersect(const Point& a, const Point& b, const Point& c, const Point& d)
{
    const double d1 = cross(c, d, a), d2 = cross(c, d, b);
    const double d3 = cross(a, b, c), d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return true;
    return (d1 == 0 && onSegment(c, d, a)) || (d2 == 0 && onSegment(c, d, b)) ||
           (d3 == 0 && onSegment(a, b, c)) || (d4 == 0 && onSegment(a, b, d));
}

double segmentsDistance(const Point& a, const Point& b, const Point& c, const Point& d)
{
    if (segmentsIntersect(a, b, c, d))
        return 0.0;
    return std::min({segmentDistance(a, c, d), segmentDistance(b, c, d), segmentDistance(c, a, b),
                     segmentDistance(d, a, b)});
}

double polylineLength(const std::vector<Point>& line)
{
    double total = 0.0;
    for (std::size_t i = 1; i < line.size(); ++i)
        total += distance(line[i - 1], line[i]);
    return total;
}

double polygonArea(const std::vector<Point>& ring)
{
    if (ring.size() < 3)
        return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        sum += (ring[j].x - ring[i].x) * (ring[j].y + ring[i].y);
    return std::fabs(sum) / 2.0;
}

bool pointInPolygon(const std::vector<Point>& ring, const Point& p)
{
    bool in = false;
    if (ring.size() < 3)
        return false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
    {
        if (((ring[i].y > p.y) != (ring[j].y > p.y)) &&
            (p.x < (ring[j].x - ring[i].x) * (p.y - ring[i].y) / (ring[j].y - ring[i].y) + ring[i].x))
            in = !in;
    }
    return in;
}

PolylinePosition locateOnPolyline(const std::vector<Point>& line, const Point& p)
{
    PolylinePosition best;
    best.offset = std::numeric_limits<double>::infinity();
    double start = 0.0;
    for (std::size_t i = 0; i + 1 < line.size(); ++i)
    {
        const Point& a = line[i];
        const Point& b = line[i + 1];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::hypot(dx, dy);
        double t = len > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / (len * len) : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const Point q{a.x + t * dx, a.y + t * dy};
        const double off = distance(p, q);
        if (off < best.offset)
        {
            best.offset = off;
            best.segment = i;
            best.t = t;
            best.along = start + t * len;
            best.point = q;
        }
        start += len;
    }
    best.length = start;
    if (line.size() < 2)
        best.offset = line.empty() ? std::numeric_limits<double>::infinity() : distance(p, line[0]);
    return best;
}

std::vector<Point> linkPolyline(const Model& model, const LinkItem& link)
{
    const NodeItem* a = model.findNode(link.from);
    const NodeItem* b = model.findNode(link.to);
    if (!a || !b || !a->placed || !b->placed)
        return {};
    std::vector<Point> line;
    line.reserve(link.vertices.size() + 2);
    line.push_back(a->pos);
    line.insert(line.end(), link.vertices.begin(), link.vertices.end());
    line.push_back(b->pos);
    return line;
}

std::vector<Point> elementGeometry(const Model& model, const ElementRef& e)
{
    switch (e.category)
    {
    case Category::Node:
        if (const auto* n = model.findNode(e.id); n && n->placed)
            return {n->pos};
        break;
    case Category::Link:
        if (const auto* l = model.findLink(e.id))
            return linkPolyline(model, *l);
        break;
    case Category::Area:
        if (const auto* a = model.findArea(e.id))
            return a->polygon;
        break;
    case Category::Gage:
        if (const auto* g = model.findGage(e.id); g && g->placed)
            return {g->pos};
        break;
    case Category::None:
        break;
    }
    return {};
}

namespace
{
std::vector<ElementRef> allElements(const Model& model)
{
    std::vector<ElementRef> all;
    all.reserve(model.nodes().size() + model.links().size() + model.areas().size() + model.gages().size());
    for (const auto& n : model.nodes())
        all.push_back({Category::Node, n.id});
    for (const auto& l : model.links())
        all.push_back({Category::Link, l.id});
    for (const auto& a : model.areas())
        all.push_back({Category::Area, a.id});
    for (const auto& g : model.gages())
        all.push_back({Category::Gage, g.id});
    return all;
}
} // namespace

std::vector<ElementRef> elementsInPolygon(const Model& model, const std::vector<Point>& polygon, bool crossing)
{
    std::vector<ElementRef> out;
    if (polygon.size() < 3)
        return out;
    const Box region(polygon);
    for (const auto& e : allElements(model))
    {
        const Shape s = shapeOf(model, e);
        if (s.points.empty())
            continue;
        const Box box(s.points);
        if (region.gap(box) > 0)
            continue;
        bool edgesCross = false;
        forSegments(s, [&](const Point& a, const Point& b) {
            for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size() && !edgesCross; j = i++)
                edgesCross = a.x != b.x || a.y != b.y ? segmentsIntersect(a, b, polygon[j], polygon[i]) : false;
        });
        std::size_t inside = 0;
        for (const auto& p : s.points)
            inside += pointInPolygon(polygon, p) ? 1 : 0;
        bool take = false;
        if (crossing)
            take = inside > 0 || edgesCross || (s.ring && pointInPolygon(s.points, polygon[0]));
        else
            take = inside == s.points.size() && !edgesCross;
        if (take)
            out.push_back(e);
    }
    return out;
}

std::vector<ElementRef> elementsWithinDistance(const Model& model, const std::vector<ElementRef>& from,
                                               double maxDistance)
{
    std::vector<ElementRef> out;
    std::vector<std::pair<Shape, Box>> sources;
    for (const auto& e : from)
    {
        Shape s = shapeOf(model, e);
        if (s.points.empty())
            continue;
        Box b(s.points);
        sources.emplace_back(std::move(s), b);
    }
    if (sources.empty())
        return out;
    for (const auto& e : allElements(model))
    {
        const Shape s = shapeOf(model, e);
        if (s.points.empty())
            continue;
        const Box box(s.points);
        for (const auto& [src, srcBox] : sources)
        {
            if (srcBox.gap(box) > maxDistance)
                continue;
            if (shapeDistance(src, s) <= maxDistance)
            {
                out.push_back(e);
                break;
            }
        }
    }
    return out;
}

double geodesicDistance(double lon1, double lat1, double lon2, double lat2)
{
    const double b = kA * (1 - kF);
    const double L = (lon2 - lon1) * kDeg;
    const double U1 = std::atan((1 - kF) * std::tan(lat1 * kDeg));
    const double U2 = std::atan((1 - kF) * std::tan(lat2 * kDeg));
    const double sinU1 = std::sin(U1), cosU1 = std::cos(U1);
    const double sinU2 = std::sin(U2), cosU2 = std::cos(U2);
    double lambda = L, sinSigma = 0, cosSigma = 0, sigma = 0, cos2Alpha = 0, cos2SigmaM = 0;
    bool converged = false;
    for (int i = 0; i < 200; ++i)
    {
        const double sinLambda = std::sin(lambda), cosLambda = std::cos(lambda);
        sinSigma = std::hypot(cosU2 * sinLambda, cosU1 * sinU2 - sinU1 * cosU2 * cosLambda);
        if (sinSigma == 0)
            return 0.0; // same point
        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = std::atan2(sinSigma, cosSigma);
        const double sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cos2Alpha = 1 - sinAlpha * sinAlpha;
        cos2SigmaM = cos2Alpha != 0 ? cosSigma - 2 * sinU1 * sinU2 / cos2Alpha : 0.0;
        const double C = kF / 16 * cos2Alpha * (4 + kF * (4 - 3 * cos2Alpha));
        const double prev = lambda;
        lambda = L + (1 - C) * kF * sinAlpha *
                         (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma * (-1 + 2 * cos2SigmaM * cos2SigmaM)));
        if (std::fabs(lambda - prev) < 1e-12)
        {
            converged = true;
            break;
        }
    }
    if (!converged)
    {
        // Nearly antipodal points: great-circle distance on the mean sphere.
        const double p1 = lat1 * kDeg, p2 = lat2 * kDeg;
        const double h = std::sin((p2 - p1) / 2) * std::sin((p2 - p1) / 2) +
                         std::cos(p1) * std::cos(p2) * std::sin(L / 2) * std::sin(L / 2);
        return 2 * 6371008.8 * std::asin(std::min(1.0, std::sqrt(h)));
    }
    const double u2 = cos2Alpha * (kA * kA - b * b) / (b * b);
    const double A = 1 + u2 / 16384 * (4096 + u2 * (-768 + u2 * (320 - 175 * u2)));
    const double B = u2 / 1024 * (256 + u2 * (-128 + u2 * (74 - 47 * u2)));
    const double deltaSigma =
        B * sinSigma *
        (cos2SigmaM + B / 4 *
                          (cosSigma * (-1 + 2 * cos2SigmaM * cos2SigmaM) -
                           B / 6 * cos2SigmaM * (-3 + 4 * sinSigma * sinSigma) * (-3 + 4 * cos2SigmaM * cos2SigmaM)));
    return b * A * (sigma - deltaSigma);
}

double geodesicArea(const std::vector<Point>& lonLat)
{
    if (lonLat.size() < 3)
        return 0.0;
    const double e = std::sqrt(kF * (2 - kF));
    const double qp = qFunc(1.0, e);
    const double rq = kA * std::sqrt(qp / 2.0);
    auto beta = [&](double latDeg) {
        const double q = qFunc(std::sin(latDeg * kDeg), e);
        return std::asin(std::clamp(q / qp, -1.0, 1.0));
    };
    double sum = 0.0;
    const std::size_t n = lonLat.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const Point& p1 = lonLat[i];
        const Point& p2 = lonLat[(i + 1) % n];
        double dl = (p2.x - p1.x) * kDeg;
        if (dl > kPi)
            dl -= 2 * kPi;
        else if (dl < -kPi)
            dl += 2 * kPi;
        sum += dl * (2 + std::sin(beta(p1.y)) + std::sin(beta(p2.y)));
    }
    return std::fabs(sum * rq * rq / 2.0);
}

Measurement measure(const std::vector<Point>& points, bool closed, const crs::Crs* crs)
{
    Measurement m;
    std::vector<Point> line = points;
    if (closed && line.size() >= 3)
        line.push_back(line.front());
    if (crs && crs->valid() && crs->isGeographic())
    {
        m.metres = true;
        m.geodesic = true;
        for (std::size_t i = 1; i < line.size(); ++i)
            m.length += geodesicDistance(line[i - 1].x, line[i - 1].y, line[i].x, line[i].y);
        if (closed)
            m.area = geodesicArea(points);
        return m;
    }
    const double unit = crs && crs->valid() ? crs->unitToMeters : 1.0;
    m.metres = crs && crs->valid();
    m.length = polylineLength(line) * unit;
    if (closed)
        m.area = polygonArea(points) * unit * unit;
    return m;
}

} // namespace mirab::core
