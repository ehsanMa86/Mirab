/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/src/Dxf.cpp
 * Purpose: Reads point, line and polygon entities from ASCII DXF drawings.
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
//
// Format reference: Autodesk DXF Reference (AutoCAD 2012 and 2018 editions,
// compatible with R12): group code/value pairs, the ENTITIES section, the LINE,
// LWPOLYLINE, POLYLINE/VERTEX/SEQEND, POINT, CIRCLE and INSERT entities, and the
// "Arbitrary Axis Algorithm" that maps object coordinate systems (OCS) to world
// coordinates. Binary DXF ("AutoCAD Binary DXF" sentinel) is rejected.
#include "mirab/interop/Import.h"

#include "ImportDetail.h"

#include "mirab/core/Text.h"

#include <cmath>
#include <map>
#include <stdexcept>

namespace mirab::interop
{

namespace fs = std::filesystem;
using core::Point;

namespace
{

struct Pair
{
    int code = 0;
    std::string value;
};

struct Vec3
{
    double x = 0, y = 0, z = 0;
};

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalized(const Vec3& v)
{
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0)
        return {0, 0, 1};
    return {v.x / len, v.y / len, v.z / len};
}

/// Object coordinate system of an entity (DXF Reference, Arbitrary Axis Algorithm).
struct Ocs
{
    Vec3 ax{1, 0, 0}, ay{0, 1, 0}, n{0, 0, 1};
    bool identity = true;

    explicit Ocs(Vec3 extrusion)
    {
        n = normalized(extrusion);
        if (std::fabs(n.x) < 1e-12 && std::fabs(n.y) < 1e-12 && n.z > 0)
            return;
        identity = false;
        const double limit = 1.0 / 64.0;
        if (std::fabs(n.x) < limit && std::fabs(n.y) < limit)
            ax = normalized(cross({0, 1, 0}, n));
        else
            ax = normalized(cross({0, 0, 1}, n));
        ay = normalized(cross(n, ax));
    }

    Vec3 toWorld(const Vec3& p) const
    {
        if (identity)
            return p;
        return {p.x * ax.x + p.y * ay.x + p.z * n.x, p.x * ax.y + p.y * ay.y + p.z * n.y,
                p.x * ax.z + p.y * ay.z + p.z * n.z};
    }
};

/// Decodes "\U+XXXX" escapes used by DXF for characters outside the code page.
std::string decodeDxfText(const std::string& raw)
{
    std::string s = detail::toUtf8Auto(raw);
    if (s.find("\\U+") == std::string::npos && s.find("\\u+") == std::string::npos)
        return s;
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 6 < s.size() && (s[i + 1] == 'U' || s[i + 1] == 'u') &&
            s[i + 2] == '+')
        {
            const std::string hex = s.substr(i + 3, 4);
            if (hex.size() == 4 && hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
            {
                const unsigned cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                if (cp < 0x80)
                {
                    out += static_cast<char>(cp);
                }
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                i += 6;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::vector<Pair> readPairs(const fs::path& path)
{
    const std::string b = detail::readFileBytes(path);
    if (b.rfind("AutoCAD Binary DXF", 0) == 0)
        throw std::runtime_error(path.string() +
                                 " is a binary DXF file; save it as ASCII DXF and import it again");
    std::vector<Pair> pairs;
    std::size_t pos = 0;
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xEF &&
        static_cast<unsigned char>(b[1]) == 0xBB && static_cast<unsigned char>(b[2]) == 0xBF)
        pos = 3;
    auto nextLine = [&](std::string& line) {
        if (pos >= b.size())
            return false;
        std::size_t e = b.find('\n', pos);
        if (e == std::string::npos)
            e = b.size();
        line.assign(b, pos, e - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        pos = e + 1;
        return true;
    };
    std::string codeLine, valueLine;
    std::size_t lineNo = 0;
    while (nextLine(codeLine))
    {
        ++lineNo;
        const std::string trimmed = text::trim(codeLine);
        if (trimmed.empty() && pos >= b.size())
            break;
        const auto code = text::toInt(trimmed);
        if (!code)
            throw std::runtime_error(path.string() + ": not a valid ASCII DXF file (line " +
                                     std::to_string(lineNo) + ")");
        if (!nextLine(valueLine))
            break;
        ++lineNo;
        pairs.push_back({static_cast<int>(*code), text::trim(valueLine)});
        if (*code == 0 && pairs.back().value == "EOF")
            break;
    }
    return pairs;
}

double number(const std::string& v)
{
    return text::toDouble(v).value_or(0.0);
}

struct Collected
{
    GeometryType geometry = GeometryType::Point;
    std::string layer;
    std::string handle;
    std::string elevation;
    std::string block;
    std::vector<Point> points;
};

class Builder
{
public:
    void add(Collected c)
    {
        if (c.points.empty())
            return;
        if (c.geometry == GeometryType::Polygon)
        {
            if (c.points.size() > 1 && c.points.front().x == c.points.back().x &&
                c.points.front().y == c.points.back().y)
                c.points.pop_back();
            if (c.points.size() < 3)
                c.geometry = GeometryType::Line;
        }
        if (c.geometry == GeometryType::Line && c.points.size() < 2)
            return;
        const std::string layerName = c.layer.empty() ? std::string("0") : c.layer;
        const auto key = std::make_pair(text::toUpper(layerName), static_cast<int>(c.geometry));
        auto it = index_.find(key);
        if (it == index_.end())
        {
            ImportedLayer l;
            const char* suffix = c.geometry == GeometryType::Point  ? " (points)"
                                 : c.geometry == GeometryType::Line ? " (lines)"
                                                                    : " (polygons)";
            l.name = layerName + suffix;
            l.geometry = c.geometry;
            l.fields = {"Layer", "Handle", "Elevation"};
            if (c.geometry == GeometryType::Point)
                l.fields.push_back("Block");
            layers_.push_back(std::move(l));
            it = index_.emplace(key, layers_.size() - 1).first;
        }
        ImportedLayer& l = layers_[it->second];
        ImportedFeature f;
        f.points = std::move(c.points);
        f.values = {layerName, c.handle, c.elevation};
        if (c.geometry == GeometryType::Point)
            f.values.push_back(c.block);
        l.features.push_back(std::move(f));
    }

    std::vector<ImportedLayer> take() { return std::move(layers_); }

private:
    std::vector<ImportedLayer> layers_;
    std::map<std::pair<std::string, int>, std::size_t> index_;
};

std::string formatZ(double z)
{
    return text::formatDouble(z, 12);
}

} // namespace

std::vector<ImportedLayer> readDxf(const fs::path& path)
{
    const std::vector<Pair> pairs = readPairs(path);
    Builder builder;

    // Locate the ENTITIES section.
    std::size_t i = 0;
    bool found = false;
    for (; i + 1 < pairs.size(); ++i)
    {
        if (pairs[i].code == 0 && pairs[i].value == "SECTION" && pairs[i + 1].code == 2 &&
            text::iequals(pairs[i + 1].value, "ENTITIES"))
        {
            i += 2;
            found = true;
            break;
        }
    }
    if (!found)
        return {};

    // Open POLYLINE waiting for its VERTEX entities.
    bool inPolyline = false;
    Collected poly;
    int polyFlags = 0;
    Ocs polyOcs({0, 0, 1});
    double polyElevation = 0.0;

    auto finishPolyline = [&]() {
        if (!inPolyline)
            return;
        inPolyline = false;
        builder.add(std::move(poly));
        poly = Collected{};
    };

    while (i < pairs.size())
    {
        if (pairs[i].code != 0)
        {
            ++i;
            continue;
        }
        const std::string type = text::toUpper(pairs[i].value);
        if (type == "ENDSEC" || type == "EOF")
            break;
        std::size_t j = i + 1;
        while (j < pairs.size() && pairs[j].code != 0)
            ++j;
        // Entity body: pairs (i, j).
        std::string layer, handle, block;
        Vec3 p10, p11, extrusion{0, 0, 1};
        bool has30 = false, has38 = false;
        double elevation38 = 0.0;
        int flags = 0;
        std::vector<Point> lwPoints;
        for (std::size_t k = i + 1; k < j; ++k)
        {
            const Pair& pr = pairs[k];
            switch (pr.code)
            {
            case 8:
                layer = decodeDxfText(pr.value);
                break;
            case 5:
                handle = pr.value;
                break;
            case 2:
                block = decodeDxfText(pr.value);
                break;
            case 10:
                p10.x = number(pr.value);
                if (type == "LWPOLYLINE")
                    lwPoints.push_back({p10.x, 0.0});
                break;
            case 20:
                p10.y = number(pr.value);
                if (type == "LWPOLYLINE" && !lwPoints.empty())
                    lwPoints.back().y = p10.y;
                break;
            case 30:
                p10.z = number(pr.value);
                has30 = true;
                break;
            case 11:
                p11.x = number(pr.value);
                break;
            case 21:
                p11.y = number(pr.value);
                break;
            case 31:
                p11.z = number(pr.value);
                break;
            case 38:
                elevation38 = number(pr.value);
                has38 = true;
                break;
            case 70:
                flags = static_cast<int>(text::toInt(pr.value).value_or(0));
                break;
            case 210:
                extrusion.x = number(pr.value);
                break;
            case 220:
                extrusion.y = number(pr.value);
                break;
            case 230:
                extrusion.z = number(pr.value);
                break;
            default:
                break;
            }
        }

        if (type == "VERTEX")
        {
            // Spline frame control points (16) and polyface face records (128) carry no position.
            if (inPolyline && !(flags & 16) && !(flags & 128))
            {
                Vec3 w = p10;
                if (!(polyFlags & 8))
                {
                    w.z = polyElevation;
                    w = polyOcs.toWorld(w);
                }
                if (poly.points.empty() && (polyFlags & 8))
                    poly.elevation = formatZ(w.z);
                if (poly.points.empty() || poly.points.back().x != w.x || poly.points.back().y != w.y)
                    poly.points.push_back({w.x, w.y});
            }
        }
        else
        {
            finishPolyline();
            if (type == "LINE")
            {
                Collected c;
                c.geometry = GeometryType::Line;
                c.layer = layer;
                c.handle = handle;
                if (has30)
                    c.elevation = formatZ(p10.z);
                c.points = {{p10.x, p10.y}, {p11.x, p11.y}};
                if (!(p10.x == p11.x && p10.y == p11.y))
                    builder.add(std::move(c));
            }
            else if (type == "POINT" || type == "CIRCLE" || type == "INSERT")
            {
                Vec3 w = p10;
                if (type != "POINT")
                    w = Ocs(extrusion).toWorld(p10);
                Collected c;
                c.geometry = GeometryType::Point;
                c.layer = layer;
                c.handle = handle;
                if (has30 || !Ocs(extrusion).identity)
                    c.elevation = formatZ(w.z);
                if (type == "INSERT")
                    c.block = block;
                c.points = {{w.x, w.y}};
                builder.add(std::move(c));
            }
            else if (type == "LWPOLYLINE")
            {
                const Ocs ocs(extrusion);
                Collected c;
                c.geometry = (flags & 1) ? GeometryType::Polygon : GeometryType::Line;
                c.layer = layer;
                c.handle = handle;
                double z = elevation38;
                for (const auto& pt : lwPoints)
                {
                    const Vec3 w = ocs.toWorld({pt.x, pt.y, elevation38});
                    z = w.z;
                    if (c.points.empty() || c.points.back().x != w.x || c.points.back().y != w.y)
                        c.points.push_back({w.x, w.y});
                }
                if (has38 || !ocs.identity)
                    c.elevation = formatZ(z);
                builder.add(std::move(c));
            }
            else if (type == "POLYLINE")
            {
                // Polygon meshes (16) and polyface meshes (64) are not networks.
                if (!(flags & 16) && !(flags & 64))
                {
                    inPolyline = true;
                    poly = Collected{};
                    poly.geometry = (flags & 1) ? GeometryType::Polygon : GeometryType::Line;
                    poly.layer = layer;
                    poly.handle = handle;
                    polyFlags = flags;
                    polyOcs = Ocs(extrusion);
                    polyElevation = p10.z;
                    if (!(flags & 8) && (has30 || !polyOcs.identity))
                        poly.elevation = formatZ(polyOcs.toWorld({0, 0, p10.z}).z);
                }
            }
            // SEQEND closes the polyline (done above); TEXT, MTEXT, ARC, HATCH,
            // DIMENSION, ATTRIB and other entities are ignored.
        }
        i = j;
    }
    finishPolyline();
    return builder.take();
}

} // namespace mirab::interop
