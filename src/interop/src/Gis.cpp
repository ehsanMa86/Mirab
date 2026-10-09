/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/interop/src/Gis.cpp
 * Purpose: Builds GIS layers from a model and writes them as GeoJSON or GeoPackage.
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
#include "anahita/interop/Interop.h"

#include "anahita/core/Text.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <stdexcept>

#if defined(ANAHITA_HAVE_SQLITE)
#include <sqlite3.h>
#endif

namespace anahita::interop
{

using core::Category;
using core::Point;

namespace
{
std::string layerNameFor(const std::string& kind)
{
    std::string k = text::toLower(kind);
    if (k == "storage")
        return "storage_units";
    if (k == "raingage")
        return "rain_gages";
    if (k == "subcatchment")
        return "subcatchments";
    return k + "s";
}

std::string columnName(const std::string& s)
{
    std::string out;
    for (char c : text::toLower(s))
        out += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return out;
}

const char* geometryFor(Category c)
{
    switch (c)
    {
    case Category::Link:
        return "LINESTRING";
    case Category::Area:
        return "POLYGON";
    default:
        return "POINT";
    }
}
} // namespace

std::vector<Layer> buildLayers(const core::Model& model, const GisOptions& options)
{
    std::vector<Layer> layers;
    const core::ResultSet* results = options.results;
    auto resultTableFor = [&](Category c) -> const core::ResultTable* {
        if (!results)
            return nullptr;
        switch (c)
        {
        case Category::Node:
            return results->table("node");
        case Category::Link:
            return results->table("link");
        case Category::Area:
            return results->table("subcatchment");
        default:
            return nullptr;
        }
    };
    const int period = results ? (options.period >= 0 ? options.period : results->periodCount() - 1) : -1;

    for (const auto& def : core::sectionDefs(model.dialect()))
    {
        if (def.elementKind.empty())
            continue;
        Layer layer;
        layer.name = layerNameFor(def.elementKind);
        layer.geometry = geometryFor(def.keyCategory);
        std::set<std::string> used;
        for (const auto& c : def.columns)
        {
            std::string name = columnName(c.name);
            if (name == "id" || name == "name")
                name = "id";
            while (used.count(name))
                name += "_";
            used.insert(name);
            layer.columns.push_back(name);
            layer.numeric.push_back(c.type == core::ColumnType::Number);
        }
        const core::ResultTable* rt = resultTableFor(def.keyCategory);
        if (rt && period >= 0)
        {
            for (const auto& v : rt->variables)
            {
                std::string name = "r_" + columnName(v.name);
                while (used.count(name))
                    name += "_";
                used.insert(name);
                layer.columns.push_back(name);
                layer.numeric.push_back(true);
            }
        }
        const core::InpSection* s = model.document().find(def.name);
        if (!s)
            continue;
        for (int r = 0; r < s->rowCount(); ++r)
        {
            const auto tokens = s->rowTokens(r);
            if (tokens.empty())
                continue;
            Layer::Feature f;
            const std::string& id = tokens[0];
            if (def.keyCategory == Category::Node)
            {
                const auto* n = model.findNode(id);
                if (!n || !n->placed)
                    continue;
                f.coordinates.push_back(n->pos);
            }
            else if (def.keyCategory == Category::Link)
            {
                const auto* l = model.findLink(id);
                const auto* a = l ? model.findNode(l->from) : nullptr;
                const auto* b = l ? model.findNode(l->to) : nullptr;
                if (!a || !b || !a->placed || !b->placed)
                    continue;
                f.coordinates.push_back(a->pos);
                for (const auto& v : l->vertices)
                    f.coordinates.push_back(v);
                f.coordinates.push_back(b->pos);
            }
            else if (def.keyCategory == Category::Area)
            {
                const auto* a = model.findArea(id);
                if (!a || a->polygon.size() < 3)
                    continue;
                f.coordinates = a->polygon;
            }
            else if (def.keyCategory == Category::Gage)
            {
                const auto* g = model.findGage(id);
                if (!g || !g->placed)
                    continue;
                f.coordinates.push_back(g->pos);
            }
            for (std::size_t c = 0; c < def.columns.size(); ++c)
            {
                std::string v;
                if (c < tokens.size())
                {
                    v = text::unquote(tokens[c]);
                    if (c + 1 == def.columns.size() && def.variadic && tokens.size() > c + 1)
                    {
                        std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(c), tokens.end());
                        v = text::join(rest, " ");
                    }
                }
                if (v == "*")
                    v.clear();
                f.values.push_back(v);
            }
            if (rt && period >= 0)
            {
                const int e = rt->elementIndex(id);
                for (int v = 0; v < rt->variableCount(); ++v)
                    f.values.push_back(e >= 0 ? text::formatDouble(rt->value(period, v, e), 7) : std::string());
            }
            layer.features.push_back(std::move(f));
        }
        if (!layer.features.empty())
            layers.push_back(std::move(layer));
    }
    return layers;
}

json::Value toGeoJson(const core::Model& model, const GisOptions& options)
{
    json::Value fc = json::Value::object();
    fc.set("type", "FeatureCollection");
    fc.set("name", model.title().empty() ? "anahita" : model.title());
    if (!options.crs.empty())
    {
        // Legacy named CRS member, understood by QGIS and GDAL for projected data.
        std::string code = options.crs;
        if (text::istartsWith(code, "EPSG:"))
            code = "urn:ogc:def:crs:EPSG::" + code.substr(5);
        json::Value crs = json::Value::object();
        crs.set("type", "name");
        json::Value props = json::Value::object();
        props.set("name", code);
        crs.set("properties", props);
        fc.set("crs", crs);
    }
    json::Value features = json::Value::array();
    for (const auto& layer : buildLayers(model, options))
    {
        for (const auto& f : layer.features)
        {
            json::Value feature = json::Value::object();
            feature.set("type", "Feature");
            json::Value geom = json::Value::object();
            auto pt = [](const Point& p) {
                json::Value a = json::Value::array();
                a.push(p.x);
                a.push(p.y);
                return a;
            };
            if (layer.geometry == "POINT")
            {
                geom.set("type", "Point");
                geom.set("coordinates", pt(f.coordinates.front()));
            }
            else if (layer.geometry == "LINESTRING")
            {
                geom.set("type", "LineString");
                json::Value coords = json::Value::array();
                for (const auto& p : f.coordinates)
                    coords.push(pt(p));
                geom.set("coordinates", coords);
            }
            else
            {
                geom.set("type", "Polygon");
                json::Value ring = json::Value::array();
                for (const auto& p : f.coordinates)
                    ring.push(pt(p));
                ring.push(pt(f.coordinates.front()));
                json::Value rings = json::Value::array();
                rings.push(ring);
                geom.set("coordinates", rings);
            }
            feature.set("geometry", geom);
            json::Value props = json::Value::object();
            props.set("layer", layer.name);
            for (std::size_t c = 0; c < layer.columns.size() && c < f.values.size(); ++c)
            {
                const std::string& v = f.values[c];
                if (v.empty())
                    props.set(layer.columns[c], json::Value());
                else if (layer.numeric[c] && text::toDouble(v))
                    props.set(layer.columns[c], *text::toDouble(v));
                else
                    props.set(layer.columns[c], v);
            }
            feature.set("properties", props);
            features.push(feature);
        }
    }
    fc.set("features", features);
    return fc;
}

void writeGeoJson(const core::Model& model, const std::filesystem::path& path, const GisOptions& options)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + path.u8string());
    out << toGeoJson(model, options).dump(1) << "\n";
}

#if defined(ANAHITA_HAVE_SQLITE)
namespace
{
class Db
{
public:
    explicit Db(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (sqlite3_open(path.u8string().c_str(), &db_) != SQLITE_OK)
            throw std::runtime_error("cannot create " + path.u8string());
    }
    ~Db() { sqlite3_close(db_); }
    void exec(const std::string& sql)
    {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK)
        {
            std::string msg = err ? err : "unknown SQLite error";
            sqlite3_free(err);
            throw std::runtime_error("GeoPackage: " + msg);
        }
    }
    sqlite3_stmt* prepare(const std::string& sql)
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("GeoPackage: ") + sqlite3_errmsg(db_));
        return st;
    }
    void step(sqlite3_stmt* st)
    {
        if (sqlite3_step(st) != SQLITE_DONE)
            throw std::runtime_error(std::string("GeoPackage: ") + sqlite3_errmsg(db_));
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
    }

private:
    sqlite3* db_ = nullptr;
};

void put32(std::vector<unsigned char>& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}

void putDouble(std::vector<unsigned char>& b, double d)
{
    unsigned char raw[8];
    std::memcpy(raw, &d, 8);
    b.insert(b.end(), raw, raw + 8);
}

std::vector<unsigned char> gpkgGeometry(const std::string& type, const std::vector<Point>& pts, int srs)
{
    std::vector<unsigned char> b = {'G', 'P', 0, 0x01}; // version 0, little endian, no envelope
    put32(b, static_cast<std::uint32_t>(srs));
    b.push_back(1); // WKB little endian
    if (type == "POINT")
    {
        put32(b, 1);
        putDouble(b, pts.front().x);
        putDouble(b, pts.front().y);
    }
    else if (type == "LINESTRING")
    {
        put32(b, 2);
        put32(b, static_cast<std::uint32_t>(pts.size()));
        for (const auto& p : pts)
        {
            putDouble(b, p.x);
            putDouble(b, p.y);
        }
    }
    else
    {
        put32(b, 3);
        put32(b, 1);
        put32(b, static_cast<std::uint32_t>(pts.size() + 1));
        for (const auto& p : pts)
        {
            putDouble(b, p.x);
            putDouble(b, p.y);
        }
        putDouble(b, pts.front().x);
        putDouble(b, pts.front().y);
    }
    return b;
}

std::string quoteSql(const std::string& s)
{
    return "'" + text::replaceAll(s, "'", "''") + "'";
}
} // namespace

bool geoPackageAvailable()
{
    return true;
}

void writeGeoPackage(const core::Model& model, const std::filesystem::path& path, const GisOptions& options)
{
    Db db(path);
    db.exec("PRAGMA application_id = 1196444487; PRAGMA user_version = 10300;");
    db.exec("CREATE TABLE gpkg_spatial_ref_sys (srs_name TEXT NOT NULL, srs_id INTEGER PRIMARY KEY, "
            "organization TEXT NOT NULL, organization_coordsys_id INTEGER NOT NULL, definition TEXT NOT NULL, "
            "description TEXT)");
    db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('Undefined cartesian SRS', -1, 'NONE', -1, 'undefined', "
            "'undefined cartesian coordinate reference system')");
    db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('Undefined geographic SRS', 0, 'NONE', 0, 'undefined', "
            "'undefined geographic coordinate reference system')");
    db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('WGS 84 geodetic', 4326, 'EPSG', 4326, "
            "'GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
            "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433],AUTHORITY[\"EPSG\",\"4326\"]]', "
            "'longitude/latitude coordinates in decimal degrees on the WGS 84 spheroid')");
    int srs = -1;
    if (text::istartsWith(options.crs, "EPSG:"))
    {
        if (auto code = text::toInt(options.crs.substr(5)))
        {
            srs = static_cast<int>(*code);
            if (srs != 4326)
                db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES (" + quoteSql(options.crs) + ", " +
                        std::to_string(srs) + ", 'EPSG', " + std::to_string(srs) +
                        ", 'undefined', 'coordinate system declared by the Anahita project')");
        }
    }
    db.exec("CREATE TABLE gpkg_contents (table_name TEXT NOT NULL PRIMARY KEY, data_type TEXT NOT NULL, "
            "identifier TEXT UNIQUE, description TEXT DEFAULT '', last_change DATETIME NOT NULL DEFAULT "
            "(strftime('%Y-%m-%dT%H:%M:%fZ','now')), min_x DOUBLE, min_y DOUBLE, max_x DOUBLE, max_y DOUBLE, "
            "srs_id INTEGER, CONSTRAINT fk_gc_r_srs_id FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys(srs_id))");
    db.exec("CREATE TABLE gpkg_geometry_columns (table_name TEXT NOT NULL, column_name TEXT NOT NULL, "
            "geometry_type_name TEXT NOT NULL, srs_id INTEGER NOT NULL, z TINYINT NOT NULL, m TINYINT NOT NULL, "
            "CONSTRAINT pk_geom_cols PRIMARY KEY (table_name, column_name), "
            "CONSTRAINT fk_gc_tn FOREIGN KEY (table_name) REFERENCES gpkg_contents(table_name), "
            "CONSTRAINT fk_gc_srs FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys (srs_id))");

    db.exec("BEGIN");
    for (const auto& layer : buildLayers(model, options))
    {
        std::string create = "CREATE TABLE \"" + layer.name + "\" (fid INTEGER PRIMARY KEY AUTOINCREMENT, geom " +
                             layer.geometry;
        for (std::size_t c = 0; c < layer.columns.size(); ++c)
            create += ", \"" + layer.columns[c] + "\" " + (layer.numeric[c] ? "REAL" : "TEXT");
        create += ")";
        db.exec(create);
        core::Bounds b;
        for (const auto& f : layer.features)
        {
            for (const auto& p : f.coordinates)
                b.include(p);
        }
        db.exec("INSERT INTO gpkg_contents (table_name, data_type, identifier, description, min_x, min_y, max_x, "
                "max_y, srs_id) VALUES (" +
                quoteSql(layer.name) + ", 'features', " + quoteSql(layer.name) + ", " +
                quoteSql("Anahita " + layer.name) + ", " + text::formatDouble(b.minX, 15) + ", " +
                text::formatDouble(b.minY, 15) + ", " + text::formatDouble(b.maxX, 15) + ", " +
                text::formatDouble(b.maxY, 15) + ", " + std::to_string(srs) + ")");
        db.exec("INSERT INTO gpkg_geometry_columns VALUES (" + quoteSql(layer.name) + ", 'geom', " +
                quoteSql(layer.geometry) + ", " + std::to_string(srs) + ", 0, 0)");

        std::string insert = "INSERT INTO \"" + layer.name + "\" (geom";
        for (const auto& c : layer.columns)
            insert += ", \"" + c + "\"";
        insert += ") VALUES (?";
        for (std::size_t c = 0; c < layer.columns.size(); ++c)
            insert += ", ?";
        insert += ")";
        sqlite3_stmt* st = db.prepare(insert);
        for (const auto& f : layer.features)
        {
            const auto blob = gpkgGeometry(layer.geometry, f.coordinates, srs);
            sqlite3_bind_blob(st, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
            for (std::size_t c = 0; c < layer.columns.size(); ++c)
            {
                const int slot = static_cast<int>(c) + 2;
                const std::string v = c < f.values.size() ? f.values[c] : std::string();
                if (v.empty())
                    sqlite3_bind_null(st, slot);
                else if (layer.numeric[c] && text::toDouble(v))
                    sqlite3_bind_double(st, slot, *text::toDouble(v));
                else
                    sqlite3_bind_text(st, slot, v.c_str(), -1, SQLITE_TRANSIENT);
            }
            db.step(st);
        }
        sqlite3_finalize(st);
    }
    db.exec("COMMIT");
}
#else
bool geoPackageAvailable()
{
    return false;
}

void writeGeoPackage(const core::Model&, const std::filesystem::path&, const GisOptions&)
{
    throw std::runtime_error("this build of Anahita was made without GeoPackage support");
}
#endif

} // namespace anahita::interop
