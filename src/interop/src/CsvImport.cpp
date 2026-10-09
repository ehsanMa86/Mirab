/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/src/CsvImport.cpp
 * Purpose: Reads delimited text tables (CSV/TSV) as point layers or attribute tables.
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
// Format reference: RFC 4180, Common Format and MIME Type for Comma-Separated
// Values (CSV) Files: quoted fields, doubled quotes inside quoted fields and
// line breaks inside quoted fields. Semicolon and tab delimiters and a UTF-8
// byte order mark are accepted as written by common spreadsheet programs.
#include "mirab/interop/Import.h"

#include "ImportDetail.h"

#include "mirab/core/Text.h"

#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>

namespace mirab::interop
{

namespace fs = std::filesystem;

namespace
{

char detectDelimiter(const std::string& s)
{
    int comma = 0, semicolon = 0, tab = 0;
    bool quoted = false;
    for (char c : s)
    {
        if (c == '"')
            quoted = !quoted;
        else if (!quoted && (c == '\n' || c == '\r'))
            break;
        else if (!quoted && c == ',')
            ++comma;
        else if (!quoted && c == ';')
            ++semicolon;
        else if (!quoted && c == '\t')
            ++tab;
    }
    if (tab > comma && tab > semicolon)
        return '\t';
    if (semicolon > comma)
        return ';';
    return ',';
}

std::vector<std::vector<std::string>> parseRecords(const std::string& s, char delim)
{
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string field;
    bool quoted = false;     // inside a quoted section
    bool wasQuoted = false;  // the current field had a quoted section
    auto endField = [&]() {
        record.push_back(wasQuoted ? field : text::trim(field));
        field.clear();
        wasQuoted = false;
    };
    auto endRecord = [&]() {
        endField();
        const bool blank = record.size() == 1 && record[0].empty();
        if (!blank)
            records.push_back(std::move(record));
        record.clear();
    };
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const char c = s[i];
        if (quoted)
        {
            if (c == '"')
            {
                if (i + 1 < n && s[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                {
                    quoted = false;
                }
            }
            else
            {
                field += c;
            }
            continue;
        }
        if (c == '"' && !wasQuoted && text::trim(field).empty())
        {
            field.clear();
            quoted = true;
            wasQuoted = true;
        }
        else if (c == delim)
        {
            endField();
        }
        else if (c == '\n' || c == '\r')
        {
            if (c == '\r' && i + 1 < n && s[i + 1] == '\n')
                ++i;
            endRecord();
        }
        else if (!(wasQuoted && (c == ' ' || c == '\t')))
        {
            field += c;
        }
    }
    if (!field.empty() || wasQuoted || !record.empty())
        endRecord();
    return records;
}

int findColumn(const std::vector<std::string>& fields, const std::string& wanted)
{
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (text::iequals(fields[i], wanted))
            return static_cast<int>(i);
    }
    return -1;
}

int detectColumn(const std::vector<std::string>& fields, std::initializer_list<const char*> names)
{
    for (const char* name : names)
    {
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (detail::normalizedName(fields[i]) == name)
                return static_cast<int>(i);
        }
    }
    return -1;
}

std::optional<double> parseCoordinate(std::string v, char delim)
{
    v = text::trim(v);
    if (delim != ',' && v.find(',') != std::string::npos && v.find('.') == std::string::npos)
        v = text::replaceAll(v, ",", ".");
    return text::toDouble(v);
}

} // namespace

ImportedLayer readCsv(const fs::path& path, const CsvOptions& options)
{
    std::string content = detail::readFileBytes(path);
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF)
        content.erase(0, 3);
    content = detail::toUtf8Auto(content);
    const char delim = options.delimiter ? options.delimiter : detectDelimiter(content);
    auto records = parseRecords(content, delim);
    if (records.empty())
        throw std::runtime_error(path.string() + " has no header line");

    ImportedLayer layer;
    layer.name = path.stem().string();
    layer.geometry = GeometryType::Point;
    std::set<std::string> used;
    for (std::size_t i = 0; i < records[0].size(); ++i)
    {
        std::string name = text::trim(records[0][i]);
        if (name.empty())
            name = "Field" + std::to_string(i + 1);
        std::string unique = name;
        for (int k = 2; used.count(text::toLower(unique)); ++k)
            unique = name + "_" + std::to_string(k);
        used.insert(text::toLower(unique));
        layer.fields.push_back(unique);
    }

    int xi = -1, yi = -1;
    if (!options.xField.empty() || !options.yField.empty())
    {
        xi = findColumn(layer.fields, options.xField);
        yi = findColumn(layer.fields, options.yField);
        if (xi < 0 || yi < 0)
            throw std::runtime_error(path.string() + ": coordinate column '" +
                                     (xi < 0 ? options.xField : options.yField) + "' not found");
    }
    else
    {
        xi = detectColumn(layer.fields, {"x", "easting", "east", "xcoord", "xcoordinate", "pointx",
                                         "lon", "long", "longitude", "lng"});
        yi = detectColumn(layer.fields, {"y", "northing", "north", "ycoord", "ycoordinate", "pointy",
                                         "lat", "latitude"});
        if (xi < 0 || yi < 0)
            xi = yi = -1;
    }

    const std::size_t fieldCount = layer.fields.size();
    layer.features.reserve(records.size() - 1);
    for (std::size_t r = 1; r < records.size(); ++r)
    {
        ImportedFeature f;
        f.values = std::move(records[r]);
        f.values.resize(fieldCount);
        if (xi >= 0)
        {
            const auto x = parseCoordinate(f.values[static_cast<std::size_t>(xi)], delim);
            const auto y = parseCoordinate(f.values[static_cast<std::size_t>(yi)], delim);
            if (x && y)
                f.points.push_back({*x, *y});
        }
        layer.features.push_back(std::move(f));
    }
    return layer;
}

} // namespace mirab::interop
