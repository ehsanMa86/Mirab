/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/src/Shapefile.cpp
 * Purpose: Reads ESRI shapefiles (.shp/.dbf/.prj/.cpg) into imported feature layers.
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
// Format references:
//   ESRI Shapefile Technical Description, ESRI White Paper, July 1998
//     (main file header, record headers, shape types 0, 1, 3, 5, 8 and their
//     Z (11, 13, 15, 18) and M (21, 23, 25, 28) variants).
//   dBASE III/IV file structure (table header, 32-byte field descriptors
//     terminated by 0x0D, fixed-length records with a deletion flag byte).
//   ESRI code page file (.cpg) convention for the attribute text encoding.
#include "mirab/interop/Import.h"

#include "ImportDetail.h"

#include "mirab/core/Text.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace mirab::interop
{

namespace fs = std::filesystem;
using core::Point;

namespace
{

enum class Encoding
{
    Auto,
    Utf8,
    Latin1
};

std::uint32_t readBE32(const std::string& b, std::size_t at)
{
    const auto* p = reinterpret_cast<const unsigned char*>(b.data() + at);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
           std::uint32_t(p[3]);
}

std::uint32_t readLE32(const std::string& b, std::size_t at)
{
    const auto* p = reinterpret_cast<const unsigned char*>(b.data() + at);
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

std::uint16_t readLE16(const std::string& b, std::size_t at)
{
    const auto* p = reinterpret_cast<const unsigned char*>(b.data() + at);
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

double readLEDouble(const std::string& b, std::size_t at)
{
    std::uint64_t bits = 0;
    for (int i = 7; i >= 0; --i)
        bits = (bits << 8) | static_cast<unsigned char>(b[at + static_cast<std::size_t>(i)]);
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

/// Finds a sibling file with the same stem and the given extension, in any letter case.
fs::path sibling(const fs::path& shp, const char* ext)
{
    fs::path candidate = shp;
    candidate.replace_extension(ext);
    std::error_code ec;
    if (fs::exists(candidate, ec))
        return candidate;
    candidate.replace_extension(text::toUpper(ext));
    if (fs::exists(candidate, ec))
        return candidate;
    const fs::path dir = shp.has_parent_path() ? shp.parent_path() : fs::path(".");
    const std::string stem = shp.stem().string();
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        const fs::path& p = entry.path();
        if (text::iequals(p.stem().string(), stem) && text::iequals(p.extension().string(), ext))
            return p;
    }
    return {};
}

Encoding encodingFromCpg(const std::string& cpg)
{
    const std::string c = text::toUpper(text::trim(cpg));
    if (c.find("UTF-8") != std::string::npos || c.find("UTF8") != std::string::npos || c == "65001")
        return Encoding::Utf8;
    static const char* latin[] = {"8859-1", "88591", "8859_1", "LATIN1", "LATIN-1", "1252", "ANSI"};
    for (const char* l : latin)
    {
        if (c.find(l) != std::string::npos)
            return Encoding::Latin1;
    }
    return Encoding::Auto;
}

std::string decodeText(std::string_view raw, Encoding enc)
{
    switch (enc)
    {
    case Encoding::Latin1:
        return detail::cp1252ToUtf8(raw);
    case Encoding::Utf8:
    case Encoding::Auto:
        break;
    }
    return detail::toUtf8Auto(raw);
}

struct DbfTable
{
    std::vector<std::string> fields;
    std::vector<std::vector<std::string>> records;
};

std::string dbfValue(char type, std::string_view raw, Encoding enc)
{
    switch (type)
    {
    case 'C':
        return text::trim(decodeText(raw, enc));
    case 'N':
    case 'F':
    {
        std::string t = text::trim(raw);
        if (t.find_first_not_of('*') == std::string::npos)
            return {};
        return t;
    }
    case 'L':
    {
        const std::string t = text::trim(raw);
        if (t.empty())
            return {};
        const char c = t[0];
        if (c == 'T' || c == 't' || c == 'Y' || c == 'y')
            return "true";
        if (c == 'F' || c == 'f' || c == 'N' || c == 'n')
            return "false";
        return {};
    }
    case 'D':
    {
        const std::string t = text::trim(raw);
        if (t.size() == 8 && t.find_first_not_of("0123456789") == std::string::npos)
            return t.substr(0, 4) + "-" + t.substr(4, 2) + "-" + t.substr(6, 2);
        return t;
    }
    case 'I':
        if (raw.size() == 4)
        {
            const std::string b(raw);
            return std::to_string(static_cast<std::int32_t>(readLE32(b, 0)));
        }
        return {};
    case 'O':
        if (raw.size() == 8)
            return text::formatDouble(readLEDouble(std::string(raw), 0), 15);
        return {};
    case 'M':
        return {}; // memo block number; memo files are not read
    default:
        return text::trim(decodeText(raw, enc));
    }
}

DbfTable readDbf(const fs::path& path, Encoding enc)
{
    DbfTable table;
    const std::string b = detail::readFileBytes(path);
    if (b.size() < 32)
        throw std::runtime_error("invalid dBASE file " + path.string());
    const std::uint32_t recordCount = readLE32(b, 4);
    const std::size_t headerLen = readLE16(b, 8);
    const std::size_t recordLen = readLE16(b, 10);
    struct Field
    {
        char type;
        std::size_t offset;
        std::size_t length;
    };
    std::vector<Field> fields;
    std::size_t offset = 1; // deletion flag
    for (std::size_t at = 32; at + 32 <= b.size() && at + 1 <= headerLen; at += 32)
    {
        if (static_cast<unsigned char>(b[at]) == 0x0D)
            break;
        std::size_t nameLen = 0;
        while (nameLen < 11 && b[at + nameLen] != '\0')
            ++nameLen;
        const char type = static_cast<char>(std::toupper(static_cast<unsigned char>(b[at + 11])));
        std::size_t length = static_cast<unsigned char>(b[at + 16]);
        const std::size_t decimals = static_cast<unsigned char>(b[at + 17]);
        if (type == 'C' && decimals > 0 && offset + length + decimals * 256 < recordLen + 1)
            length += decimals * 256; // long character fields (Clipper/FoxPro extension)
        table.fields.push_back(text::trim(detail::toUtf8Auto(std::string_view(b).substr(at, nameLen))));
        fields.push_back({type, offset, length});
        offset += length;
    }
    if (recordLen == 0 || headerLen == 0)
        return table;
    table.records.reserve(recordCount);
    for (std::uint32_t r = 0; r < recordCount; ++r)
    {
        const std::size_t start = headerLen + static_cast<std::size_t>(r) * recordLen;
        if (start + recordLen > b.size())
            break;
        std::vector<std::string> values;
        values.reserve(fields.size());
        for (const auto& f : fields)
        {
            if (f.offset + f.length > recordLen)
            {
                values.emplace_back();
                continue;
            }
            values.push_back(dbfValue(f.type, std::string_view(b).substr(start + f.offset, f.length), enc));
        }
        table.records.push_back(std::move(values));
    }
    return table;
}

bool samePoint(const Point& a, const Point& b)
{
    return a.x == b.x && a.y == b.y;
}

double signedArea(const std::vector<Point>& ring)
{
    double a = 0.0;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * a;
}

GeometryType geometryOf(int shapeType)
{
    switch (shapeType % 10)
    {
    case 3:
        return GeometryType::Line;
    case 5:
        return GeometryType::Polygon;
    default:
        return GeometryType::Point;
    }
}

bool supportedShape(int shapeType)
{
    switch (shapeType)
    {
    case 1:
    case 3:
    case 5:
    case 8:
    case 11:
    case 13:
    case 15:
    case 18:
    case 21:
    case 23:
    case 25:
    case 28:
        return true;
    default:
        return false;
    }
}

} // namespace

ImportedLayer readShapefile(const fs::path& shp)
{
    const std::string b = detail::readFileBytes(shp);
    if (b.size() < 100 || readBE32(b, 0) != 9994)
        throw std::runtime_error(shp.string() + " is not an ESRI shapefile");
    const int layerType = static_cast<int>(readLE32(b, 32));
    if (layerType == 31)
        throw std::runtime_error("MultiPatch shapefiles are not supported: " + shp.string());
    if (layerType != 0 && !supportedShape(layerType))
        throw std::runtime_error("unsupported shape type " + std::to_string(layerType) + " in " +
                                 shp.string());

    ImportedLayer layer;
    layer.name = shp.stem().string();
    layer.geometry = geometryOf(layerType);

    Encoding enc = Encoding::Auto;
    if (const fs::path cpg = sibling(shp, ".cpg"); !cpg.empty())
        enc = encodingFromCpg(detail::readFileBytes(cpg));
    if (const fs::path prj = sibling(shp, ".prj"); !prj.empty())
        layer.crsText = text::trim(detail::readFileBytes(prj));
    DbfTable dbf;
    if (const fs::path dbfPath = sibling(shp, ".dbf"); !dbfPath.empty())
        dbf = readDbf(dbfPath, enc);
    layer.fields = dbf.fields;

    const bool hasZ = layerType == 11 || layerType == 18;
    if (hasZ)
    {
        std::string zName = "Z";
        for (const auto& f : layer.fields)
        {
            if (text::iequals(f, "Z"))
                zName = "SHP_Z";
        }
        layer.fields.push_back(zName);
    }
    const std::size_t fieldCount = layer.fields.size();

    // The file length in the header counts 16-bit words; trust the real size when shorter.
    std::size_t end = static_cast<std::size_t>(readBE32(b, 24)) * 2;
    if (end > b.size() || end < 100)
        end = b.size();

    std::size_t pos = 100;
    std::size_t recordIndex = 0;
    for (; pos + 8 <= end; ++recordIndex)
    {
        const std::size_t contentLen = static_cast<std::size_t>(readBE32(b, pos + 4)) * 2;
        const std::size_t c = pos + 8;
        pos = c + contentLen;
        if (pos > end || contentLen < 4)
            break;
        const int type = static_cast<int>(readLE32(b, c));
        if (type == 0 || !supportedShape(type) || geometryOf(type) != layer.geometry)
            continue; // null shape or a shape that does not fit the layer

        std::vector<std::string> values;
        if (recordIndex < dbf.records.size())
            values = dbf.records[recordIndex];
        values.resize(dbf.fields.size());
        if (hasZ)
            values.emplace_back();
        values.resize(fieldCount);

        auto addFeature = [&](std::vector<Point> pts, const std::string& z) {
            ImportedFeature f;
            f.points = std::move(pts);
            f.values = values;
            if (hasZ)
                f.values.back() = z;
            layer.features.push_back(std::move(f));
        };

        const int base = type % 10;
        if (base == 1)
        {
            if (contentLen < 20)
                continue;
            const Point p{readLEDouble(b, c + 4), readLEDouble(b, c + 12)};
            std::string z;
            if (type == 11 && contentLen >= 28)
                z = text::formatDouble(readLEDouble(b, c + 20), 15);
            if (std::isfinite(p.x) && std::isfinite(p.y))
                addFeature({p}, z);
            continue;
        }
        if (base == 8)
        {
            if (contentLen < 40)
                continue;
            const std::size_t n = readLE32(b, c + 36);
            const std::size_t ptsAt = c + 40;
            if (ptsAt + n * 16 > pos)
                continue;
            const std::size_t zAt = ptsAt + n * 16 + 16;
            const bool zOk = type == 18 && zAt + n * 8 <= pos;
            for (std::size_t i = 0; i < n; ++i)
            {
                const Point p{readLEDouble(b, ptsAt + i * 16), readLEDouble(b, ptsAt + i * 16 + 8)};
                std::string z;
                if (zOk)
                    z = text::formatDouble(readLEDouble(b, zAt + i * 8), 15);
                if (std::isfinite(p.x) && std::isfinite(p.y))
                    addFeature({p}, z);
            }
            continue;
        }

        // PolyLine / Polygon (and Z/M variants): box, parts, points.
        if (contentLen < 44)
            continue;
        const std::size_t numParts = readLE32(b, c + 36);
        const std::size_t numPoints = readLE32(b, c + 40);
        const std::size_t partsAt = c + 44;
        const std::size_t ptsAt = partsAt + numParts * 4;
        if (numParts == 0 || numParts > contentLen || numPoints > contentLen ||
            ptsAt + numPoints * 16 > pos)
            continue;
        std::vector<std::vector<Point>> parts;
        for (std::size_t k = 0; k < numParts; ++k)
        {
            const std::size_t first = readLE32(b, partsAt + k * 4);
            const std::size_t last = k + 1 < numParts ? readLE32(b, partsAt + (k + 1) * 4) : numPoints;
            if (first > last || last > numPoints)
                continue;
            std::vector<Point> part;
            part.reserve(last - first);
            for (std::size_t i = first; i < last; ++i)
            {
                const Point p{readLEDouble(b, ptsAt + i * 16), readLEDouble(b, ptsAt + i * 16 + 8)};
                if (!std::isfinite(p.x) || !std::isfinite(p.y))
                    continue;
                if (!part.empty() && samePoint(part.back(), p))
                    continue;
                part.push_back(p);
            }
            parts.push_back(std::move(part));
        }
        if (layer.geometry == GeometryType::Line)
        {
            for (auto& part : parts)
            {
                if (part.size() >= 2)
                    addFeature(std::move(part), {});
            }
            continue;
        }
        // Polygons: outer rings are clockwise, holes counter-clockwise.
        std::vector<std::vector<Point>> rings;
        std::vector<double> areas;
        for (auto& part : parts)
        {
            if (part.size() > 1 && samePoint(part.front(), part.back()))
                part.pop_back();
            if (part.size() < 3)
                continue;
            areas.push_back(signedArea(part));
            rings.push_back(std::move(part));
        }
        bool anyClockwise = false;
        for (double a : areas)
            anyClockwise = anyClockwise || a < 0.0;
        for (std::size_t k = 0; k < rings.size(); ++k)
        {
            if (!anyClockwise || areas[k] < 0.0)
                addFeature(std::move(rings[k]), {});
        }
    }
    return layer;
}

} // namespace mirab::interop
