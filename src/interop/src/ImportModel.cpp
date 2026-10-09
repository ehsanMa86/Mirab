/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/interop/src/ImportModel.cpp
 * Purpose: Turns imported GIS/CAD/CSV feature layers into nodes, links and subcatchments.
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
#include "anahita/interop/Import.h"

#include "ImportDetail.h"

#include "anahita/core/Text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace anahita::interop
{

namespace fs = std::filesystem;
using core::Category;
using core::ColumnDef;
using core::ColumnType;
using core::Dialect;
using core::Point;
using core::SectionDef;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

namespace detail
{

std::string readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad())
        throw std::runtime_error("cannot read " + path.string());
    return ss.str();
}

bool isValidUtf8(std::string_view s)
{
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n)
    {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        std::size_t len = 0;
        unsigned cp = 0;
        if ((c & 0xE0) == 0xC0)
        {
            len = 2;
            cp = c & 0x1Fu;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            len = 3;
            cp = c & 0x0Fu;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            len = 4;
            cp = c & 0x07u;
        }
        else
        {
            return false;
        }
        if (i + len > n)
            return false;
        for (std::size_t k = 1; k < len; ++k)
        {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

std::string cp1252ToUtf8(std::string_view s)
{
    // Windows-1252 code points for bytes 0x80..0x9F; the rest equals ISO-8859-1.
    static const unsigned short high[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (char ch : s)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80)
        {
            out += ch;
            continue;
        }
        const unsigned cp = (c >= 0x80 && c <= 0x9F) ? high[c - 0x80] : c;
        if (cp < 0x800)
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
    }
    return out;
}

std::string toUtf8Auto(std::string_view s)
{
    if (isValidUtf8(s))
        return std::string(s);
    return cp1252ToUtf8(s);
}

std::string normalizedName(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out += c;
        else if (c >= 'A' && c <= 'Z')
            out += static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

} // namespace detail

int ImportedLayer::fieldIndex(const std::string& field) const
{
    if (field.empty())
        return -1;
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (fields[i] == field)
            return static_cast<int>(i);
    }
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (text::iequals(fields[i], field))
            return static_cast<int>(i);
    }
    return -1;
}

namespace
{

const char* const kElementId = "ANAHITA_IMPORT_E";
const char* const kFromId = "ANAHITA_IMPORT_A";
const char* const kToId = "ANAHITA_IMPORT_B";

bool isGeometrySection(const std::string& name)
{
    return name == "COORDINATES" || name == "VERTICES" || name == "POLYGONS" || name == "SYMBOLS";
}

bool isReference(ColumnType t)
{
    switch (t)
    {
    case ColumnType::Id:
    case ColumnType::NodeRef:
    case ColumnType::LinkRef:
    case ColumnType::AreaRef:
    case ColumnType::NodeOrAreaRef:
    case ColumnType::GageRef:
        return true;
    default:
        return false;
    }
}

bool isNumeric(const ColumnDef& c)
{
    return c.type == ColumnType::Number || (c.type == ColumnType::Text && !c.quantity.empty());
}

const char* categoryName(Category c)
{
    switch (c)
    {
    case Category::Node:
        return "node";
    case Category::Link:
        return "link";
    case Category::Area:
        return "subcatchment";
    default:
        return "element";
    }
}

/// Rows written for one new element, as produced by Model::addNode, addLink or
/// addArea in a scratch model that shares the target's options and rain gages.
struct ElementTemplate
{
    struct Row
    {
        std::string section;
        const SectionDef* def = nullptr;
        std::vector<std::string> tokens;
    };
    std::vector<Row> rows;
};

ElementTemplate makeTemplate(const core::InpDocument& source, Dialect dialect, Category target,
                             const std::string& kind)
{
    if (dialect == Dialect::Msx)
        throw std::invalid_argument("network elements cannot be imported into an EPANET-MSX file");
    const SectionDef* def = core::sectionForKind(dialect, kind);
    if (!def || def->keyCategory != target)
        throw std::invalid_argument("'" + kind + "' is not a " + categoryName(target) + " type of " +
                                    core::dialectName(dialect) + " models");
    core::InpDocument doc;
    if (const auto* o = source.find("OPTIONS"))
        doc.sections.push_back(*o);
    if (target == Category::Area)
    {
        if (const auto* g = source.find("RAINGAGES"))
            doc.sections.push_back(*g);
    }
    core::Model m(std::move(doc), dialect);
    std::string error;
    bool ok = false;
    switch (target)
    {
    case Category::Node:
        ok = m.addNode(kind, kElementId, Point{}, &error);
        break;
    case Category::Link:
        ok = m.addNode("Junction", kFromId, Point{}, &error) &&
             m.addNode("Junction", kToId, Point{1, 0}, &error) &&
             m.addLink(kind, kElementId, kFromId, kToId, &error);
        break;
    case Category::Area:
        ok = m.addArea(kElementId, {}, {}, &error);
        break;
    default:
        error = "only nodes, links and subcatchments can be imported";
        break;
    }
    if (!ok)
        throw std::invalid_argument("cannot import " + kind + ": " + error);

    ElementTemplate t;
    for (const auto& s : m.document().sections)
    {
        if (isGeometrySection(s.name))
            continue;
        const SectionDef* sd = core::findSectionDef(dialect, s.name);
        if (!sd || sd->keyCategory != target)
            continue;
        const int rows = s.rowCount();
        for (int r = 0; r < rows; ++r)
        {
            auto tokens = s.rowTokens(r);
            if (!tokens.empty() && tokens[0] == kElementId)
                t.rows.push_back({s.name, sd, std::move(tokens)});
        }
    }
    if (t.rows.empty() || t.rows.front().section != def->name)
        throw std::invalid_argument("cannot import " + kind + ": no default row");
    return t;
}

/// Same token edit as Model::setToken for a non-empty value.
void setTokenValue(std::vector<std::string>& tokens, int column, const std::string& value)
{
    const std::string v = text::quoteIfNeeded(text::unquote(text::trim(value)));
    const auto col = static_cast<std::size_t>(column);
    while (tokens.size() < col)
        tokens.push_back("*");
    if (tokens.size() == col)
        tokens.push_back(v);
    else
        tokens[col] = v;
}

std::optional<double> parseNumber(const std::string& raw)
{
    std::string v = text::trim(raw);
    if (v.empty())
        return std::nullopt;
    if (auto d = text::toDouble(v))
        return d;
    // Decimal comma ("12,5") as written by some locales.
    if (v.find(',') != std::string::npos && v.find('.') == std::string::npos &&
        std::count(v.begin(), v.end(), ',') == 1)
        return text::toDouble(text::replaceAll(v, ",", "."));
    return std::nullopt;
}

double polylineLength(const std::vector<Point>& pts)
{
    double len = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i)
        len += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    return len;
}

double ringArea(const std::vector<Point>& ring)
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

Point centroid(const std::vector<Point>& ring)
{
    const double a = ringArea(ring);
    Point c;
    if (std::fabs(a) > 1e-12)
    {
        const std::size_t n = ring.size();
        // Relative to the first vertex for numerical stability with projected coordinates.
        const Point o = ring.front();
        double cx = 0, cy = 0, area = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x0 = ring[i].x - o.x, y0 = ring[i].y - o.y;
            const double x1 = ring[(i + 1) % n].x - o.x, y1 = ring[(i + 1) % n].y - o.y;
            const double cross = x0 * y1 - x1 * y0;
            area += cross;
            cx += (x0 + x1) * cross;
            cy += (y0 + y1) * cross;
        }
        if (std::fabs(area) > 1e-12)
            return {o.x + cx / (3.0 * area), o.y + cy / (3.0 * area)};
    }
    for (const auto& p : ring)
    {
        c.x += p.x;
        c.y += p.y;
    }
    if (!ring.empty())
    {
        c.x /= static_cast<double>(ring.size());
        c.y /= static_cast<double>(ring.size());
    }
    return c;
}

std::string formatRounded(double v, double step)
{
    return text::formatDouble(std::round(v / step) * step);
}

class Notes
{
public:
    explicit Notes(std::vector<std::string>& out) : out_(out) {}
    void add(std::string msg)
    {
        if (count_ < kMax)
            out_.push_back(std::move(msg));
        ++count_;
    }
    void finish()
    {
        if (count_ > kMax)
            out_.push_back("... and " + std::to_string(count_ - kMax) + " more notes");
    }

private:
    static constexpr int kMax = 50;
    std::vector<std::string>& out_;
    int count_ = 0;
};

/// Buffers new rows per section and inserts them the way InpSection::appendRow
/// does (before trailing blank lines), so that thousands of rows cost O(n).
class RowWriter
{
public:
    explicit RowWriter(core::InpDocument& doc) : doc_(doc) {}

    void append(const std::string& section, const std::vector<std::string>& tokens)
    {
        auto it = pending_.find(section);
        if (it == pending_.end())
        {
            doc_.ensure(section); // creates missing sections in first-use order
            order_.push_back(section);
            it = pending_.emplace(section, std::vector<std::string>{}).first;
        }
        it->second.push_back(core::InpSection::formatRow(tokens));
    }

    bool empty() const { return order_.empty(); }

    void flush()
    {
        for (const auto& name : order_)
        {
            core::InpSection* s = doc_.find(name);
            auto& rows = pending_[name];
            if (!s || rows.empty())
                continue;
            std::size_t insertAt = s->lines.size();
            while (insertAt > 0 && text::trim(s->lines[insertAt - 1]).empty())
                --insertAt;
            s->lines.insert(s->lines.begin() + static_cast<std::ptrdiff_t>(insertAt),
                            std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
        }
        order_.clear();
        pending_.clear();
    }

private:
    core::InpDocument& doc_;
    std::vector<std::string> order_;
    std::unordered_map<std::string, std::vector<std::string>> pending_;
};

/// Uniform grid over node positions for snapping and nearest-node queries.
class NodeGrid
{
public:
    void configure(double minX, double minY, double cell)
    {
        minX_ = minX;
        minY_ = minY;
        cell_ = cell > 0 && std::isfinite(cell) ? cell : 1.0;
    }

    void insert(int node, const Point& p)
    {
        cells_[key(cellX(p.x), cellY(p.y))].push_back(node);
        ++count_;
    }

    /// Nearest node within @p tol (inclusive), or -1. Requires cell >= tol.
    int nearestWithin(const Point& p, double tol, const std::vector<Point>& pos) const
    {
        const long long cx = cellX(p.x), cy = cellY(p.y);
        int best = -1;
        double bestD2 = tol * tol;
        for (long long dx = -1; dx <= 1; ++dx)
        {
            for (long long dy = -1; dy <= 1; ++dy)
                scan(cx + dx, cy + dy, p, pos, best, bestD2, true);
        }
        return best;
    }

    /// Nearest node at any distance, or -1 when the grid is empty.
    int nearest(const Point& p, const std::vector<Point>& pos) const
    {
        if (count_ == 0)
            return -1;
        const long long cx = cellX(p.x), cy = cellY(p.y);
        int best = -1;
        double bestD2 = std::numeric_limits<double>::infinity();
        const long long maxRing = maxRing_ + std::max(std::llabs(cx), std::llabs(cy)) + 2;
        for (long long r = 0; r <= maxRing; ++r)
        {
            for (long long dx = -r; dx <= r; ++dx)
            {
                scan(cx + dx, cy - r, p, pos, best, bestD2, false);
                if (r > 0)
                    scan(cx + dx, cy + r, p, pos, best, bestD2, false);
            }
            for (long long dy = -r + 1; dy <= r - 1; ++dy)
            {
                scan(cx - r, cy + dy, p, pos, best, bestD2, false);
                scan(cx + r, cy + dy, p, pos, best, bestD2, false);
            }
            const double reach = static_cast<double>(r) * cell_;
            if (best >= 0 && bestD2 <= reach * reach)
                break;
        }
        return best;
    }

    void setExtentCells(long long cells) { maxRing_ = cells; }

private:
    long long cellX(double x) const { return clampCell(std::floor((x - minX_) / cell_)); }
    long long cellY(double y) const { return clampCell(std::floor((y - minY_) / cell_)); }
    static long long clampCell(double v)
    {
        if (!(v > -1e9))
            return -1000000000LL;
        if (!(v < 1e9))
            return 1000000000LL;
        return static_cast<long long>(v);
    }
    static std::uint64_t key(long long cx, long long cy)
    {
        return (static_cast<std::uint64_t>(cx) << 32) ^ (static_cast<std::uint64_t>(cy) & 0xFFFFFFFFull);
    }
    void scan(long long cx, long long cy, const Point& p, const std::vector<Point>& pos, int& best,
              double& bestD2, bool inclusive) const
    {
        auto it = cells_.find(key(cx, cy));
        if (it == cells_.end())
            return;
        for (int n : it->second)
        {
            const Point& q = pos[static_cast<std::size_t>(n)];
            const double d2 = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
            if (d2 < bestD2 || (d2 == bestD2 && (best < 0 ? inclusive : n < best)))
            {
                best = n;
                bestD2 = d2;
            }
        }
    }

    double minX_ = 0, minY_ = 0, cell_ = 1.0;
    long long maxRing_ = 0;
    std::size_t count_ = 0;
    std::unordered_map<std::uint64_t, std::vector<int>> cells_;
};

/// Generates "prefix + counter" IDs like Model::uniqueId, in O(1) amortised.
class IdGenerator
{
public:
    std::string make(Category category, const std::string& prefix,
                     const std::unordered_set<std::string>& used,
                     const std::unordered_set<std::string>& reserved)
    {
        int& i = next_[std::string(1, static_cast<char>('0' + static_cast<int>(category))) + prefix];
        for (;;)
        {
            ++i;
            std::string candidate = prefix + std::to_string(i);
            if (!used.count(candidate) && !reserved.count(candidate))
                return candidate;
        }
    }

private:
    std::unordered_map<std::string, int> next_;
};

std::string sanitizeId(const std::string& raw)
{
    std::string id = text::trim(raw);
    for (char& c : id)
    {
        if (c == ' ' || c == '\t' || c == ';' || c == '"' || c == '\r' || c == '\n')
            c = '_';
    }
    return id;
}

/// A mapped property resolved to a template row and column.
struct PropertyTarget
{
    std::string column;
    int row = -1;
    int columnIndex = -1;
    const ColumnDef* def = nullptr;
    int field = -1;
};

struct ResolvedColumn
{
    int row = -1;
    int column = -1;
    const ColumnDef* def = nullptr;
};

ResolvedColumn findColumn(const ElementTemplate& t, const std::string& name)
{
    const std::string want = detail::normalizedName(name);
    for (std::size_t r = 0; r < t.rows.size(); ++r)
    {
        const auto& cols = t.rows[r].def->columns;
        for (std::size_t c = 0; c < cols.size(); ++c)
        {
            if (!isReference(cols[c].type) && detail::normalizedName(cols[c].name) == want)
                return {static_cast<int>(r), static_cast<int>(c), &cols[c]};
        }
    }
    return {};
}

int findColumnOfType(const SectionDef& def, ColumnType type)
{
    for (std::size_t c = 0; c < def.columns.size(); ++c)
    {
        if (def.columns[c].type == type)
            return static_cast<int>(c);
    }
    return -1;
}

/// Converts a source value for a column; returns nullopt for empty values and
/// sets @p bad when the value does not fit the column.
std::optional<std::string> convertValue(const ColumnDef& c, const std::string& raw, bool& bad)
{
    bad = false;
    const std::string v = text::trim(raw);
    if (v.empty())
        return std::nullopt;
    if (isNumeric(c))
    {
        const auto d = parseNumber(v);
        if (!d)
        {
            bad = true;
            return std::nullopt;
        }
        return text::formatDouble(*d);
    }
    if (c.type == ColumnType::Choice && !c.choices.empty())
    {
        for (const auto& choice : c.choices)
        {
            if (text::iequals(choice, v))
                return choice;
        }
        bad = true;
        return std::nullopt;
    }
    return v;
}

class Importer
{
public:
    Importer(core::Model& model, const ImportedLayer& layer, const ImportMapping& mapping,
             ImportReport& report)
        : model_(model), layer_(layer), map_(mapping), report_(report), notes_(report.notes),
          writer_(model.document())
    {
    }

    void run()
    {
        tmpl_ = makeTemplate(model_.document(), model_.dialect(), map_.target, map_.kind);
        loadExisting();
        resolveFields();
        transformAll();
        switch (map_.target)
        {
        case Category::Node:
            importNodes();
            break;
        case Category::Link:
            importLinks();
            break;
        case Category::Area:
            importAreas();
            break;
        default:
            break;
        }
        if (!writer_.empty())
        {
            writer_.flush();
            model_.rebuild();
        }
        notes_.finish();
    }

private:
    // ----- setup ---------------------------------------------------------
    void loadExisting()
    {
        for (const auto& n : model_.nodes())
        {
            const int idx = static_cast<int>(nodePos_.size());
            nodeIndex_.emplace(n.id, idx);
            nodeIds_.insert(n.id);
            nodeNames_.push_back(n.id);
            nodePos_.push_back(n.pos);
            nodePlaced_.push_back(n.placed);
        }
        for (const auto& l : model_.links())
            linkIds_.insert(l.id);
        for (const auto& a : model_.areas())
            areaIds_.insert(a.id);
    }

    void resolveFields()
    {
        idField_ = layer_.fieldIndex(map_.idField);
        if (!map_.idField.empty() && idField_ < 0)
            notes_.add("ID field '" + map_.idField + "' not found; IDs are generated");
        for (const auto& [column, field] : map_.properties)
        {
            if (field.empty())
                continue;
            const ResolvedColumn rc = findColumn(tmpl_, column);
            if (!rc.def)
            {
                notes_.add("'" + column + "' is not a property of " + map_.kind + "; ignored");
                continue;
            }
            const int fi = layer_.fieldIndex(field);
            if (fi < 0)
            {
                notes_.add("field '" + field + "' not found; " + column + " not imported");
                continue;
            }
            props_.push_back({column, rc.row, rc.column, rc.def, fi});
        }
        const ResolvedColumn len = findColumn(tmpl_, "Length");
        if (len.def && len.row == 0)
            lengthColumn_ = len.column;
        if (map_.target == Category::Area)
        {
            const ResolvedColumn area = findColumn(tmpl_, "Area");
            if (area.def && area.row == 0)
                areaColumn_ = area.column;
            outletColumn_ = findColumnOfType(*tmpl_.rows.front().def, ColumnType::NodeOrAreaRef);
        }
        if (map_.target == Category::Link)
        {
            fromField_ = layer_.fieldIndex(map_.fromField);
            toField_ = layer_.fieldIndex(map_.toField);
            if (!map_.fromField.empty() && fromField_ < 0)
                notes_.add("from-node field '" + map_.fromField + "' not found");
            if (!map_.toField.empty() && toField_ < 0)
                notes_.add("to-node field '" + map_.toField + "' not found");
            useEndFields_ = fromField_ >= 0 && toField_ >= 0;
        }
        if (map_.target == Category::Area)
        {
            outletField_ = layer_.fieldIndex(map_.outletField);
            if (!map_.outletField.empty() && outletField_ < 0)
                notes_.add("outlet field '" + map_.outletField + "' not found; nearest nodes are used");
        }
        // Reserve explicit IDs so that generated IDs never take them.
        if (idField_ >= 0)
        {
            for (const auto& f : layer_.features)
            {
                if (static_cast<std::size_t>(idField_) < f.values.size())
                {
                    const std::string id = sanitizeId(f.values[static_cast<std::size_t>(idField_)]);
                    if (!id.empty())
                        reserved_.insert(id);
                }
            }
        }
    }

    void transformAll()
    {
        geometry_.resize(layer_.features.size());
        valid_.assign(layer_.features.size(), true);
        for (std::size_t i = 0; i < layer_.features.size(); ++i)
        {
            auto pts = layer_.features[i].points;
            if (map_.transform)
            {
                for (auto& p : pts)
                {
                    if (!map_.transform(p.x, p.y) || !std::isfinite(p.x) || !std::isfinite(p.y))
                    {
                        valid_[i] = false;
                        break;
                    }
                }
            }
            geometry_[i] = std::move(pts);
        }
    }

    void buildGrid()
    {
        double minX = std::numeric_limits<double>::infinity(), minY = minX;
        double maxX = -minX, maxY = -minX;
        std::size_t count = 0;
        auto include = [&](const Point& p) {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
            ++count;
        };
        for (std::size_t i = 0; i < nodePos_.size(); ++i)
        {
            if (nodePlaced_[i])
                include(nodePos_[i]);
        }
        for (std::size_t i = 0; i < geometry_.size(); ++i)
        {
            if (!valid_[i])
                continue;
            for (const auto& p : geometry_[i])
                include(p);
        }
        if (count == 0)
        {
            minX = minY = 0;
            maxX = maxY = 1;
        }
        const double extent = std::max(maxX - minX, maxY - minY);
        double cell = extent / std::max(1.0, std::sqrt(static_cast<double>(count)));
        cell = std::max(cell, map_.snapTolerance);
        if (!(cell > 0))
            cell = std::max(1.0, map_.snapTolerance);
        grid_.configure(minX, minY, cell);
        grid_.setExtentCells(static_cast<long long>(std::ceil(extent / cell)) + 1);
        for (std::size_t i = 0; i < nodePos_.size(); ++i)
        {
            if (nodePlaced_[i])
                grid_.insert(static_cast<int>(i), nodePos_[i]);
        }
        gridReady_ = true;
    }

    // ----- helpers ---------------------------------------------------------
    std::string value(const ImportedFeature& f, int field) const
    {
        if (field < 0 || static_cast<std::size_t>(field) >= f.values.size())
            return {};
        return f.values[static_cast<std::size_t>(field)];
    }

    void skip(std::string why)
    {
        ++report_.skipped;
        notes_.add(std::move(why));
    }

    /// Resolves the ID of feature @p i; empty when the feature must be skipped.
    std::string featureId(std::size_t i, const std::unordered_set<std::string>& used,
                          const std::string& prefix)
    {
        const std::string raw = text::trim(value(layer_.features[i], idField_));
        if (raw.empty())
            return ids_.make(map_.target, prefix, used, reserved_);
        const std::string id = sanitizeId(raw);
        if (id.size() > 31)
        {
            skip("feature " + std::to_string(i + 1) + ": ID '" + raw + "' is longer than 31 characters");
            return {};
        }
        if (used.count(id))
        {
            skip("duplicate " + std::string(categoryName(map_.target)) + " ID " + id + " skipped");
            return {};
        }
        if (id != raw)
            notes_.add("ID '" + raw + "' imported as " + id);
        return id;
    }

    /// Copies the template rows for @p id and applies the mapped properties.
    std::vector<std::vector<std::string>> rowsFor(const ElementTemplate& t, const std::string& id,
                                                  const std::string& from, const std::string& to,
                                                  const ImportedFeature* f, bool& lengthSet,
                                                  bool& areaSet)
    {
        std::vector<std::vector<std::string>> rows;
        rows.reserve(t.rows.size());
        for (const auto& r : t.rows)
        {
            auto tokens = r.tokens;
            for (auto& tok : tokens)
            {
                if (tok == kElementId)
                    tok = id;
                else if (tok == kFromId)
                    tok = from;
                else if (tok == kToId)
                    tok = to;
            }
            rows.push_back(std::move(tokens));
        }
        lengthSet = areaSet = false;
        if (!f)
            return rows;
        for (const auto& p : props_)
        {
            bool bad = false;
            const auto v = convertValue(*p.def, value(*f, p.field), bad);
            if (bad)
            {
                notes_.add(id + ": '" + value(*f, p.field) + "' is not a valid " + p.column +
                           "; default kept");
                continue;
            }
            if (!v)
                continue;
            setTokenValue(rows[static_cast<std::size_t>(p.row)], p.columnIndex, *v);
            if (p.row == 0 && p.columnIndex == lengthColumn_)
                lengthSet = true;
            if (p.row == 0 && p.columnIndex == areaColumn_)
                areaSet = true;
        }
        return rows;
    }

    void writeRows(const ElementTemplate& t, const std::vector<std::vector<std::string>>& rows)
    {
        for (std::size_t r = 0; r < rows.size(); ++r)
            writer_.append(t.rows[r].section, rows[r]);
    }

    int addNodeRecord(const std::string& id, const Point& p)
    {
        const int idx = static_cast<int>(nodePos_.size());
        nodeIndex_.emplace(id, idx);
        nodeIds_.insert(id);
        nodeNames_.push_back(id);
        nodePos_.push_back(p);
        nodePlaced_.push_back(true);
        if (gridReady_)
            grid_.insert(idx, p);
        ++report_.nodesAdded;
        return idx;
    }

    std::vector<std::string> coordinateRow(const std::string& id, const Point& p) const
    {
        return {id, text::formatDouble(p.x), text::formatDouble(p.y)};
    }

    // ----- nodes -----------------------------------------------------------
    void importNodes()
    {
        for (std::size_t i = 0; i < layer_.features.size(); ++i)
        {
            const auto& f = layer_.features[i];
            if (!valid_[i])
            {
                skip("feature " + std::to_string(i + 1) + ": coordinates could not be transformed");
                continue;
            }
            const auto& pts = geometry_[i];
            Point p;
            if (layer_.geometry == GeometryType::Line)
            {
                skip("feature " + std::to_string(i + 1) + ": a line cannot become a node");
                continue;
            }
            if (pts.empty())
            {
                skip("feature " + std::to_string(i + 1) + ": no coordinates");
                continue;
            }
            p = layer_.geometry == GeometryType::Polygon ? centroid(pts) : pts.front();
            const std::string id = featureId(i, nodeIds_, map_.idPrefix);
            if (id.empty())
                continue;
            bool lengthSet = false, areaSet = false;
            const auto rows = rowsFor(tmpl_, id, {}, {}, &f, lengthSet, areaSet);
            writeRows(tmpl_, rows);
            writer_.append("COORDINATES", coordinateRow(id, p));
            addNodeRecord(id, p);
        }
    }

    // ----- links -----------------------------------------------------------
    int endpointNode(const Point& p)
    {
        if (!endpointTmpl_)
            endpointTmpl_ = makeTemplate(model_.document(), model_.dialect(), Category::Node,
                                         map_.endpointKind);
        const std::string id = ids_.make(Category::Node, map_.endpointPrefix, nodeIds_, {});
        bool lengthSet = false, areaSet = false;
        const auto rows = rowsFor(*endpointTmpl_, id, {}, {}, nullptr, lengthSet, areaSet);
        writeRows(*endpointTmpl_, rows);
        writer_.append("COORDINATES", coordinateRow(id, p));
        return addNodeRecord(id, p);
    }

    void importLinks()
    {
        if (!useEndFields_)
        {
            if (layer_.geometry != GeometryType::Line)
            {
                for (std::size_t i = 0; i < layer_.features.size(); ++i)
                    skip("feature " + std::to_string(i + 1) +
                         ": links need line geometry or from/to node fields");
                return;
            }
            buildGrid();
        }
        const double tol = std::max(0.0, map_.snapTolerance);
        for (std::size_t i = 0; i < layer_.features.size(); ++i)
        {
            const auto& f = layer_.features[i];
            if (!valid_[i])
            {
                skip("feature " + std::to_string(i + 1) + ": coordinates could not be transformed");
                continue;
            }
            std::vector<Point> pts =
                layer_.geometry == GeometryType::Line ? geometry_[i] : std::vector<Point>{};
            if (!useEndFields_ && pts.size() < 2)
            {
                skip("feature " + std::to_string(i + 1) + ": line has fewer than two points");
                continue;
            }
            const std::string id = featureId(i, linkIds_, map_.idPrefix);
            if (id.empty())
                continue;

            int a = -1, b = -1;
            if (useEndFields_)
            {
                const std::string from = text::trim(value(f, fromField_));
                const std::string to = text::trim(value(f, toField_));
                auto ia = nodeIndex_.find(from);
                auto ib = nodeIndex_.find(to);
                if (ia == nodeIndex_.end() || ib == nodeIndex_.end())
                {
                    skip(id + ": unknown end node " + (ia == nodeIndex_.end() ? from : to));
                    continue;
                }
                a = ia->second;
                b = ib->second;
                if (a == b)
                {
                    skip(id + ": both ends are node " + from);
                    continue;
                }
                const auto ua = static_cast<std::size_t>(a), ub = static_cast<std::size_t>(b);
                if (pts.size() >= 2 && nodePlaced_[ua] && nodePlaced_[ub])
                {
                    // Orient the drawn line from the first to the second node.
                    auto d = [](const Point& p, const Point& q) { return std::hypot(p.x - q.x, p.y - q.y); };
                    if (d(pts.front(), nodePos_[ub]) + d(pts.back(), nodePos_[ua]) <
                        d(pts.front(), nodePos_[ua]) + d(pts.back(), nodePos_[ub]))
                        std::reverse(pts.begin(), pts.end());
                }
                if (pts.size() >= 2)
                {
                    if (nodePlaced_[ua])
                        pts.front() = nodePos_[ua];
                    if (nodePlaced_[ub])
                        pts.back() = nodePos_[ub];
                }
                else if (nodePlaced_[ua] && nodePlaced_[ub])
                {
                    pts = {nodePos_[ua], nodePos_[ub]};
                }
            }
            else
            {
                a = grid_.nearestWithin(pts.front(), tol, nodePos_);
                b = grid_.nearestWithin(pts.back(), tol, nodePos_);
                const Point& p0 = pts.front();
                const Point& p1 = pts.back();
                const bool closeEnds = std::hypot(p0.x - p1.x, p0.y - p1.y) <= tol;
                if ((a >= 0 && a == b) || (a < 0 && b < 0 && closeEnds))
                {
                    skip(id + ": both ends snap to the same node");
                    continue;
                }
                if (a < 0)
                    a = endpointNode(p0);
                if (b < 0)
                    b = endpointNode(p1);
                pts.front() = nodePos_[static_cast<std::size_t>(a)];
                pts.back() = nodePos_[static_cast<std::size_t>(b)];
            }

            const std::string& fromId = idOfNode(a);
            const std::string& toId = idOfNode(b);
            bool lengthSet = false, areaSet = false;
            auto rows = rowsFor(tmpl_, id, fromId, toId, &f, lengthSet, areaSet);
            if (map_.computeLength && !lengthSet && lengthColumn_ >= 0 && pts.size() >= 2)
            {
                const double len = polylineLength(pts);
                if (len >= 0.0005)
                    setTokenValue(rows.front(), lengthColumn_, formatRounded(len, 0.001));
            }
            writeRows(tmpl_, rows);
            if (map_.keepVertices && pts.size() > 2)
            {
                for (std::size_t k = 1; k + 1 < pts.size(); ++k)
                    writer_.append("VERTICES", coordinateRow(id, pts[k]));
            }
            linkIds_.insert(id);
            ++report_.linksAdded;
        }
    }

    const std::string& idOfNode(int idx) const { return nodeNames_[static_cast<std::size_t>(idx)]; }

    // ----- subcatchments ---------------------------------------------------
    void importAreas()
    {
        if (layer_.geometry != GeometryType::Polygon)
        {
            for (std::size_t i = 0; i < layer_.features.size(); ++i)
                skip("feature " + std::to_string(i + 1) + ": subcatchments need polygon geometry");
            return;
        }
        buildGrid();
        const bool si = model_.units().system == core::UnitSystem::SI;
        bool warnedNoNodes = false;
        // IDs of the new subcatchments, to accept outlets that are other new subcatchments.
        std::unordered_set<std::string> layerAreaIds;
        if (outletField_ >= 0)
            layerAreaIds = reserved_;
        for (std::size_t i = 0; i < layer_.features.size(); ++i)
        {
            const auto& f = layer_.features[i];
            if (!valid_[i])
            {
                skip("feature " + std::to_string(i + 1) + ": coordinates could not be transformed");
                continue;
            }
            const auto& ring = geometry_[i];
            if (ring.size() < 3)
            {
                skip("feature " + std::to_string(i + 1) + ": polygon has fewer than three points");
                continue;
            }
            const std::string id = featureId(i, areaIds_, map_.idPrefix);
            if (id.empty())
                continue;
            std::string outlet;
            if (outletField_ >= 0)
            {
                outlet = text::trim(value(f, outletField_));
                if (!outlet.empty() && !nodeIndex_.count(outlet) && !areaIds_.count(outlet) &&
                    !layerAreaIds.count(outlet))
                {
                    notes_.add(id + ": outlet " + outlet + " does not exist; nearest node used");
                    outlet.clear();
                }
                if (outlet == id)
                    outlet.clear();
            }
            if (outlet.empty())
            {
                const int n = grid_.nearest(centroid(ring), nodePos_);
                if (n >= 0)
                {
                    outlet = idOfNode(n);
                }
                else if (!warnedNoNodes)
                {
                    notes_.add("the model has no placed nodes; subcatchment outlets left empty");
                    warnedNoNodes = true;
                }
            }
            bool lengthSet = false, areaSet = false;
            auto rows = rowsFor(tmpl_, id, {}, {}, &f, lengthSet, areaSet);
            if (!outlet.empty() && outletColumn_ >= 0)
                setTokenValue(rows.front(), outletColumn_, outlet);
            if (map_.computeArea && !areaSet && areaColumn_ >= 0)
            {
                const double a = std::fabs(ringArea(ring)) / (si ? 10000.0 : 43560.0);
                if (a >= 0.00005)
                    setTokenValue(rows.front(), areaColumn_, formatRounded(a, 0.0001));
            }
            writeRows(tmpl_, rows);
            for (const auto& p : ring)
                writer_.append("POLYGONS", coordinateRow(id, p));
            areaIds_.insert(id);
            ++report_.areasAdded;
        }
    }

    core::Model& model_;
    const ImportedLayer& layer_;
    const ImportMapping& map_;
    ImportReport& report_;
    Notes notes_;
    RowWriter writer_;
    ElementTemplate tmpl_;
    std::optional<ElementTemplate> endpointTmpl_;
    std::vector<PropertyTarget> props_;
    int idField_ = -1, fromField_ = -1, toField_ = -1, outletField_ = -1;
    int lengthColumn_ = -1, areaColumn_ = -1, outletColumn_ = -1;
    bool useEndFields_ = false;
    std::vector<std::vector<Point>> geometry_;
    std::vector<bool> valid_;
    std::unordered_map<std::string, int> nodeIndex_;
    std::unordered_set<std::string> nodeIds_, linkIds_, areaIds_, reserved_;
    std::vector<Point> nodePos_;
    std::vector<bool> nodePlaced_;
    std::vector<std::string> nodeNames_;
    NodeGrid grid_;
    bool gridReady_ = false;
    IdGenerator ids_;
};

// ---------------------------------------------------------------------------
// Mapping suggestions
// ---------------------------------------------------------------------------

std::string defaultPrefix(const std::string& kind)
{
    static const std::pair<const char*, const char*> prefixes[] = {
        {"junction", "J"},  {"reservoir", "R"}, {"tank", "T"},     {"outfall", "OF"},
        {"storage", "SU"},  {"divider", "D"},   {"pipe", "P"},     {"pump", "PU"},
        {"valve", "V"},     {"conduit", "C"},   {"orifice", "OR"}, {"weir", "W"},
        {"outlet", "OL"},   {"subcatchment", "S"}};
    const std::string k = text::toLower(kind);
    for (const auto& [name, prefix] : prefixes)
    {
        if (k == name)
            return prefix;
    }
    return kind.empty() ? std::string() : text::toUpper(kind.substr(0, 1));
}

/// Normalized field names that commonly carry a column, most specific first.
std::vector<std::string> synonyms(Dialect dialect, const std::string& kind, const std::string& column)
{
    const std::string c = detail::normalizedName(column);
    const std::string k = text::toLower(kind);
    const std::vector<std::string> elevation = {"elevation", "elev", "z", "shpz", "elevm", "elevft",
                                                "invert", "invertel", "invertelev", "invelev", "inv",
                                                "groundelev", "grndelev"};
    const std::vector<std::string> diameter = {"diameter", "diam", "dia", "pipedia", "pipediam",
                                               "pipesize", "nomdia", "size", "diametermm", "diamm"};
    if (c == "elevation")
        return elevation;
    if (c == "head")
    {
        auto v = std::vector<std::string>{"head", "totalhead", "hgl", "waterlevel"};
        v.insert(v.end(), elevation.begin(), elevation.end());
        return v;
    }
    if (c == "diameter")
        return diameter;
    if (c == "geom1")
    {
        auto v = std::vector<std::string>{"geom1", "height", "depth", "rise", "maxdepth"};
        v.insert(v.end(), diameter.begin(), diameter.end());
        return v;
    }
    if (c == "geom2")
        return {"geom2", "width", "span"};
    if (c == "roughness")
    {
        if (dialect == Dialect::Swmm)
            return {"roughness", "rough", "manningn", "mannings", "manning", "n", "nvalue"};
        return {"roughness", "rough", "hazen", "hazenc", "hwc", "chw", "cfactor", "c", "ks"};
    }
    if (c == "length")
        return {"length", "len", "pipelen", "pipelength", "lengthm", "lengthft", "shapeleng",
                "shapelength"};
    if (c == "demand")
        return {"demand", "basedemand", "basedem", "dem", "q"};
    if (c == "minorloss")
        return {"minorloss", "kloss", "k", "lossk"};
    if (c == "status")
        return {"status"};
    if (c == "pattern")
        return {"pattern", "demandpattern", "pat"};
    if (c == "maxdepth")
        return {"maxdepth", "depth", "ymax", "dmax", "rimdepth", "mhdepth"};
    if (c == "initdepth" || c == "initlevel")
        return {"initdepth", "initlevel", "initiallevel", "initialdepth", "level"};
    if (c == "minlevel")
        return {"minlevel", "minimumlevel"};
    if (c == "maxlevel")
        return {"maxlevel", "maximumlevel"};
    if (c == "surdepth")
        return {"surdepth", "surcharge", "surchargedepth"};
    if (c == "aponded")
        return {"aponded", "pondedarea"};
    if (c == "inoffset")
        return {"inoffset", "usoffset", "upoffset", "offset1", "inletoffset"};
    if (c == "outoffset")
        return {"outoffset", "dsoffset", "downoffset", "offset2", "outletoffset"};
    if (c == "barrels")
        return {"barrels", "nbarrels"};
    if (c == "area" && k == "subcatchment")
        return {"area", "areaha", "areaac", "hectares", "acres"};
    if (c == "pctimperv")
        return {"pctimperv", "imperv", "impervious", "pctimp", "percimp", "imp"};
    if (c == "width")
        return {"width"};
    if (c == "pctslope")
        return {"pctslope", "slope", "pslope"};
    if (c == "curblen")
        return {"curblen", "curblength"};
    return {c};
}

int findField(const ImportedLayer& layer, const std::vector<std::string>& names,
              const std::set<int>& used)
{
    for (const auto& name : names)
    {
        for (std::size_t i = 0; i < layer.fields.size(); ++i)
        {
            if (!used.count(static_cast<int>(i)) && detail::normalizedName(layer.fields[i]) == name)
                return static_cast<int>(i);
        }
    }
    return -1;
}

ElementTemplate templateFor(Dialect dialect, Category target, const std::string& kind)
{
    const core::Model scratch = core::Model::newProject(dialect, {});
    return makeTemplate(scratch.document(), dialect, target, kind);
}

} // namespace

ImportReport importLayer(core::Model& model, const ImportedLayer& layer, const ImportMapping& mapping)
{
    ImportReport report;
    Importer importer(model, layer, mapping, report);
    importer.run();
    return report;
}

std::vector<std::string> mappableProperties(Dialect dialect, Category target, const std::string& kind)
{
    std::vector<std::string> out;
    ElementTemplate t;
    try
    {
        t = templateFor(dialect, target, kind);
    }
    catch (const std::exception&)
    {
        return out;
    }
    std::set<std::string> seen;
    for (const auto& row : t.rows)
    {
        for (const auto& c : row.def->columns)
        {
            if (isReference(c.type))
                continue;
            if (seen.insert(detail::normalizedName(c.name)).second)
                out.push_back(c.name);
        }
    }
    return out;
}

ImportMapping suggestMapping(const core::Model& model, const ImportedLayer& layer, Category target,
                             const std::string& kind)
{
    ImportMapping m;
    m.target = target;
    m.kind = kind;
    m.idPrefix = defaultPrefix(kind);
    m.endpointKind = "Junction";
    m.endpointPrefix = "J";
    std::set<int> used;
    auto take = [&](const std::vector<std::string>& names) -> std::string {
        const int i = findField(layer, names, used);
        if (i < 0)
            return {};
        used.insert(i);
        return layer.fields[static_cast<std::size_t>(i)];
    };

    std::vector<std::string> idNames;
    switch (target)
    {
    case Category::Node:
        idNames = {"nodeid", "junctionid", "junction", "node", "mhid", "manholeid", "manhole",
                   "structid", "nodename"};
        break;
    case Category::Link:
        idNames = {"linkid", "pipeid", "conduitid", "pipe", "conduit", "link", "pipename",
                   "linkname"};
        break;
    case Category::Area:
        idNames = {"subcatchid", "subcatchment", "subcatch", "subid", "catchment", "catchid",
                   "basinid", "basin"};
        break;
    default:
        break;
    }
    for (const char* generic : {"id", "name", "facilityid", "assetid", "uid", "gid", "label", "code"})
        idNames.push_back(generic);
    m.idField = take(idNames);

    if (target == Category::Link)
    {
        const int from = findField(layer, {"fromnode", "from", "node1", "startnode", "usnode",
                                           "upstreamnode", "upnode", "inletnode", "fromid", "fnodeid"},
                                   used);
        const int to = findField(layer, {"tonode", "to", "node2", "endnode", "dsnode",
                                         "downstreamnode", "downnode", "outletnode", "toid", "tnodeid"},
                                 used);
        if (from >= 0 && to >= 0 && from != to)
        {
            used.insert(from);
            used.insert(to);
            m.fromField = layer.fields[static_cast<std::size_t>(from)];
            m.toField = layer.fields[static_cast<std::size_t>(to)];
        }
    }
    if (target == Category::Area)
        m.outletField = take({"outlet", "outletnode", "outletid", "outfall", "outnode"});

    for (const auto& column : mappableProperties(model.dialect(), target, kind))
    {
        const std::string field = take(synonyms(model.dialect(), kind, column));
        if (!field.empty())
            m.properties[column] = field;
    }
    return m;
}

} // namespace anahita::interop
