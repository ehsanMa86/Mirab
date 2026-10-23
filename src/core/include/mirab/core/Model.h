/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Model.h
 * Purpose: Network model: graph view of an input document plus schema-aware editing.
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

#include "mirab/core/InpDocument.h"
#include "mirab/core/Schema.h"
#include "mirab/core/Units.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mirab::core
{

struct Point
{
    double x = 0.0;
    double y = 0.0;
};

struct Bounds
{
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool valid = false;
    void include(const Point& p);
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
};

struct NodeItem
{
    std::string id;
    std::string kind;    ///< Junction, Reservoir, Tank, Outfall, Storage, Divider
    std::string section; ///< defining section
    int row = -1;        ///< data row in the defining section
    Point pos;
    bool placed = false; ///< has coordinates
};

struct LinkItem
{
    std::string id;
    std::string kind; ///< Pipe, Pump, Valve, Conduit, Orifice, Weir, Outlet
    std::string section;
    int row = -1;
    std::string from;
    std::string to;
    std::vector<Point> vertices;
};

struct AreaItem
{
    std::string id;
    std::string kind = "Subcatchment";
    std::string section;
    int row = -1;
    std::string outlet;
    std::string gage;
    std::vector<Point> polygon;
};

struct GageItem
{
    std::string id;
    std::string section;
    int row = -1;
    Point pos;
    bool placed = false;
};

/// A row that belongs to an element, in any of the sections keyed by it.
struct ElementRow
{
    std::string section;
    int row = -1;
    const SectionDef* def = nullptr;
    std::vector<std::string> tokens;
};

/// Reference to a drawn element: a node, link, subcatchment or rain gage.
struct ElementRef
{
    Category category = Category::None;
    std::string id;
    bool operator==(const ElementRef& o) const { return category == o.category && id == o.id; }
    bool operator!=(const ElementRef& o) const { return !(*this == o); }
    bool operator<(const ElementRef& o) const
    {
        return category != o.category ? category < o.category : id < o.id;
    }
};

struct ModelIssue
{
    enum class Severity
    {
        Warning,
        Error
    } severity = Severity::Warning;
    std::string message;
    std::string section;
    int row = -1;
};

class Model
{
public:
    Model();
    Model(InpDocument document, Dialect dialect);

    static Model fromFile(const std::filesystem::path& path);
    static Model fromText(std::string_view text);
    static Dialect detectDialect(const InpDocument& document);
    /// Creates an empty project with sensible default options.
    static Model newProject(Dialect dialect, std::string_view flowUnits);

    Dialect dialect() const { return dialect_; }
    const InpDocument& document() const { return doc_; }
    /// Direct document access; call rebuild() after changes.
    InpDocument& document() { return doc_; }
    void rebuild();

    std::string toText() const { return doc_.toString(); }
    void save(const std::filesystem::path& path) const { doc_.save(path); }

    const std::vector<NodeItem>& nodes() const { return nodes_; }
    const std::vector<LinkItem>& links() const { return links_; }
    const std::vector<AreaItem>& areas() const { return areas_; }
    const std::vector<GageItem>& gages() const { return gages_; }

    const NodeItem* findNode(std::string_view id) const;
    const LinkItem* findLink(std::string_view id) const;
    const AreaItem* findArea(std::string_view id) const;
    const GageItem* findGage(std::string_view id) const;
    int nodeIndex(std::string_view id) const;
    int linkIndex(std::string_view id) const;

    Bounds bounds() const;

    // Options and timing
    ProjectUnits units() const;
    std::string title() const;
    std::string option(std::string_view key) const;
    void setOption(std::string_view key, const std::string& value);
    double durationSeconds() const;
    double reportStepSeconds() const;
    /// Seconds from the simulation start to the first reported time.
    double reportStartSeconds() const;
    double hydraulicStepSeconds() const;
    /// EPANET: demand model "DDA" or "PDA".
    std::string demandModel() const;

    // Element data
    std::vector<ElementRow> rowsFor(Category category, std::string_view id) const;
    /// rowsFor() of many elements of one category in a single pass over the
    /// sections; elements without rows are absent from the map.
    std::unordered_map<std::string, std::vector<ElementRow>>
    rowsForMany(Category category, const std::vector<std::string>& ids) const;
    /// Sets one token of a data row. With @p rebuildModel false, call
    /// rebuild() after the last of several changes.
    bool setToken(std::string_view section, int row, int column, const std::string& value,
                  bool rebuildModel = true);

    // Editing
    bool idInUse(Category category, std::string_view id) const;
    std::string uniqueId(Category category, std::string_view prefix) const;
    bool addNode(std::string_view kind, std::string_view id, Point pos, std::string* error = nullptr);
    bool addLink(std::string_view kind, std::string_view id, std::string_view from,
                 std::string_view to, std::string* error = nullptr);
    bool addArea(std::string_view id, const std::vector<Point>& polygon, std::string_view outlet,
                 std::string* error = nullptr);
    bool addGage(std::string_view id, Point pos, std::string* error = nullptr);
    bool moveNode(std::string_view id, Point pos);
    bool moveGage(std::string_view id, Point pos);
    bool setVertices(std::string_view linkId, const std::vector<Point>& vertices);
    /// Removes the element and every row keyed by it. Removing a node also
    /// removes the links connected to it.
    bool removeElement(Category category, std::string_view id);
    /// Removes several elements at once (as removeElement() does for each);
    /// returns the number of elements removed.
    int removeElements(const std::vector<ElementRef>& elements);
    bool renameElement(Category category, std::string_view oldId, std::string_view newId,
                       std::string* error = nullptr);

    // Named objects that are not drawn on the map: SWMM pollutants, land uses,
    // aquifers, snow packs, unit hydrographs, transects, streets, inlets, LID
    // controls and control rules (see objectKinds()).
    std::vector<std::string> objectNames(std::string_view kind) const;
    bool objectExists(std::string_view kind, std::string_view name) const;
    std::string uniqueObjectName(std::string_view kind, std::string_view prefix) const;
    /// Adds an object with default parameters.
    bool addObject(std::string_view kind, std::string_view name, std::string* error = nullptr);
    /// Renames an object and every reference to it.
    bool renameObject(std::string_view kind, std::string_view oldName, std::string_view newName,
                      std::string* error = nullptr);
    /// Removes an object and the data that cannot exist without it (inflows
    /// of a pollutant, buildup of a land use, ...); optional references are cleared.
    bool removeObject(std::string_view kind, std::string_view name);
    /// Rows that define an object, followed by rows of other sections keyed
    /// by it (a land use's [BUILDUP] and [WASHOFF] rows).
    std::vector<ElementRow> objectRows(std::string_view kind, std::string_view name) const;

    /// Data rows of @p section that belong to @p key: rows whose first token is
    /// the key, the X1 and GR rows of a transect, or the rows of a control rule.
    std::vector<int> rowsOfKey(std::string_view section, std::string_view key) const;
    /// Replaces the given data rows of a section by @p rows. Rows whose tokens
    /// do not change keep their text and comments; new rows go after the last
    /// replaced row (or at the end of the section).
    void replaceRows(std::string_view section, const std::vector<int>& rowIndices,
                     const std::vector<std::vector<std::string>>& rows);
    /// A new row for a data section keyed by an element or object, with
    /// default values. Returns an empty row and sets @p missing (e.g.
    /// "Pollutant") when the row needs an object that does not exist yet.
    std::vector<std::string> newDataRow(const SectionDef& def, std::string_view key,
                                        std::string* missing = nullptr) const;

    /// Structural checks: dangling references, duplicate IDs, unplaced nodes.
    std::vector<ModelIssue> validate() const;

private:
    void removeKeyedRows(Category category, std::string_view id);
    void removeKeyedRows(Category category, const std::unordered_set<std::string>& ids);
    void appendReferenceIssues(std::vector<ModelIssue>& issues) const;
    std::string coordinateSection(Category category) const;

    Dialect dialect_ = Dialect::Epanet;
    InpDocument doc_;
    std::vector<NodeItem> nodes_;
    std::vector<LinkItem> links_;
    std::vector<AreaItem> areas_;
    std::vector<GageItem> gages_;
    // ID lookup tables, rebuilt with the element lists.
    std::unordered_map<std::string, std::size_t> nodeIdx_, linkIdx_, areaIdx_, gageIdx_;
};

/// Days since 1970-01-01 for a civil date (proleptic Gregorian).
long long daysFromCivil(int year, int month, int day);
/// The civil date of a number of days since 1970-01-01.
void civilFromDays(long long days, int& year, int& month, int& day);
/// Parses "MM/DD/YYYY" (SWMM) into days since 1970-01-01.
bool parseSwmmDate(std::string_view text, long long& days);

} // namespace mirab::core
