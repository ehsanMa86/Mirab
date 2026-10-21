/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/SwmmObjects.h
 * Purpose: SWMM named objects: LID controls and usage, transects and unit hydrographs.
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
#include "mirab/core/Model.h"
#include "mirab/core/Units.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mirab::core
{

/// A message from the validation of one object.
struct ObjectIssue
{
    enum class Severity
    {
        Warning,
        Error
    } severity = Severity::Error;
    std::string field; ///< parameter, layer or row the message refers to (may be empty)
    std::string message;
};

bool hasErrors(const std::vector<ObjectIssue>& issues);

/// Checks the LID controls and usage, transects, unit hydrographs and control
/// rules of a model (EPANET: [CONTROLS] and [RULES]). Rule messages give the
/// line number in the file.
std::vector<ModelIssue> validateObjectSections(const Model& model);

/// 1-based line number in the file of line @p index of a section.
int fileLineOf(const InpDocument& doc, const InpSection& section, int index);

/// After renaming an object defined in @p section (LID_CONTROLS, TRANSECTS or
/// HYDROGRAPHS), updates the rows that refer to it ([LID_USAGE], irregular
/// [XSECTIONS], [RDII]). Returns the number of rows changed.
int renameSwmmObjectReferences(InpDocument& doc, std::string_view section, std::string_view oldName,
                               std::string_view newName);

/// IDs in order of first appearance of the first token of the data rows of a
/// section (for [LID_CONTROLS] and [HYDROGRAPHS]).
std::vector<std::string> keyedObjectNames(const InpDocument& doc, std::string_view section);

// ---------------------------------------------------------------------------
// LID controls ([LID_CONTROLS])

enum class LidLayer
{
    Surface,
    Soil,
    Pavement,
    Storage,
    Drain,
    DrainMat,
    Removals
};

const char* lidLayerKeyword(LidLayer layer);
/// Display name, e.g. "Drainage mat".
const char* lidLayerName(LidLayer layer);
std::optional<LidLayer> lidLayerFromKeyword(std::string_view keyword);

struct LidTypeInfo
{
    std::string code; ///< BC, RG, GR, IT, PP, RB, RD, VS
    std::string name; ///< "Bio-retention cell", ...
    std::vector<LidLayer> layers;   ///< layers the type uses, top to bottom
    std::vector<LidLayer> required; ///< layers SWMM requires
    std::string help;
};

const std::vector<LidTypeInfo>& lidTypes();
const LidTypeInfo* findLidType(std::string_view code);

/// One parameter of a LID layer row, in file order.
struct LidParamDef
{
    std::string key;      ///< name used in the SWMM manual, e.g. "StorHt"
    std::string label;    ///< display label, e.g. "Berm height"
    std::string quantity; ///< depth, rate, recovery, fraction, percent, hours, days, ratio,
                          ///< roughness, slope, sideslope, coefficient, exponent, yesno, curve
    double min = 0.0;
    double max = 1e30;
    bool minExclusive = false;
    bool maxExclusive = false;
    std::string defaultUS; ///< default in US customary units
    std::string defaultSI; ///< default in SI units
    bool optional = false;
    std::string help;
};

const std::vector<LidParamDef>& lidLayerParams(LidLayer layer);
/// Unit label of a LID parameter quantity ("in", "mm/hr", "fraction", ...); empty when unitless.
std::string lidUnitLabel(std::string_view quantity, UnitSystem system);

struct LidControl
{
    std::string name;
    std::string type; ///< type code, e.g. "BC"
    /// Values after the layer keyword, as written in the file. For REMOVALS the
    /// values are pollutant/percent pairs.
    std::map<LidLayer, std::vector<std::string>> layers;

    bool hasLayer(LidLayer layer) const { return layers.count(layer) > 0; }
    /// Value of a parameter, empty when missing.
    std::string value(LidLayer layer, int index) const;
    std::optional<double> number(LidLayer layer, int index) const;
    /// Layer rows in canonical order, each starting with the control name.
    std::vector<std::vector<std::string>> toRows() const;
};

std::vector<std::string> lidControlNames(const InpDocument& doc);
std::optional<LidControl> readLidControl(const InpDocument& doc, std::string_view name);
/// A new control of a type with the default values of its layers.
LidControl defaultLidControl(std::string_view name, std::string_view type,
                             UnitSystem system = UnitSystem::US);
/// Default values of a layer for a LID type.
std::vector<std::string> defaultLidLayer(LidLayer layer, std::string_view type, UnitSystem system);
/// Unit system of a SWMM document, from its FLOW_UNITS option.
UnitSystem swmmUnitSystem(const InpDocument& doc);
/// Writes the control, replacing the rows of @p oldName (or of control.name).
/// Rows that keep their layer are rewritten in place with their comment;
/// other lines of the section are left untouched.
void writeLidControl(InpDocument& doc, const LidControl& control, std::string_view oldName = {});
/// Checks the layers and parameter ranges. @p doc (optional) is used to check
/// curve and pollutant names.
std::vector<ObjectIssue> validateLidControl(const LidControl& control,
                                            const InpDocument* doc = nullptr);

// ---------------------------------------------------------------------------
// LID usage ([LID_USAGE], rows keyed by subcatchment)

struct LidUsage
{
    std::string lid;
    int number = 1;
    double area = 0.0;      ///< area of each unit, ft2 or m2
    double width = 0.0;     ///< outflow width of each unit, ft or m
    double initSat = 0.0;   ///< % initially saturated
    double fromImp = 0.0;   ///< % of impervious area treated
    bool toPervious = false;
    std::string reportFile; ///< empty for none
    std::string drainTo;    ///< empty for the subcatchment outlet
    double fromPerv = 0.0;  ///< % of pervious area treated
};

std::vector<LidUsage> readLidUsage(const InpDocument& doc, std::string_view subcatchment);
/// Replaces the rows of the subcatchment, keeping comments and other rows.
void writeLidUsage(InpDocument& doc, std::string_view subcatchment,
                   const std::vector<LidUsage>& units);
std::vector<std::string> lidUsageRow(std::string_view subcatchment, const LidUsage& unit);

struct LidAreaSummary
{
    double lidArea = 0.0;         ///< total LID area in subcatchment area units (ac or ha)
    double subcatchmentArea = 0.0; ///< ac or ha; 0 when unknown
    double percent = 0.0;         ///< lidArea / subcatchmentArea * 100
    double impervTreated = 0.0;   ///< sum of % impervious area treated
    double pervTreated = 0.0;     ///< sum of % pervious area treated
};

/// Unit areas (ft2 or m2) per subcatchment area unit (ac or ha).
double lidAreaPerSubcatchmentUnit(UnitSystem system);
LidAreaSummary lidAreaSummary(const Model& model, std::string_view subcatchment,
                              const std::vector<LidUsage>& units);
std::vector<ObjectIssue> validateLidUsage(const Model& model, std::string_view subcatchment,
                                          const std::vector<LidUsage>& units);

// ---------------------------------------------------------------------------
// Transects ([TRANSECTS], HEC-2 style NC / X1 / GR records)

struct Transect
{
    std::string name;
    double nLeft = 0.0;    ///< Manning's n of the left overbank (effective value)
    double nRight = 0.0;   ///< Manning's n of the right overbank
    double nChannel = 0.0; ///< Manning's n of the main channel
    double leftBank = 0.0; ///< station of the left bank
    double rightBank = 0.0;
    double meander = 0.0;         ///< Lfactor: channel length / overbank length (0 = none)
    double stationFactor = 0.0;   ///< Wfactor: multiplier of stations (0 = none)
    double elevationOffset = 0.0; ///< Eoffset: added to every elevation
    std::vector<std::pair<double, double>> points; ///< (station, elevation), left to right
    bool ownRoughness = false; ///< the block has its own NC line
    int firstLine = -1;        ///< first line of the block in the section (-1 for new)
    int lastLine = -1;

    /// Stations and elevations with the station factor and elevation offset applied.
    std::vector<std::pair<double, double>> adjustedPoints() const;
    double adjustedLeftBank() const;
    double adjustedRightBank() const;
};

struct TransectGeometry
{
    double area = 0.0;
    double topWidth = 0.0;
    double wettedPerimeter = 0.0;
    double hydraulicRadius = 0.0;
};

std::vector<Transect> readTransects(const InpDocument& doc);
std::optional<Transect> readTransect(const InpDocument& doc, std::string_view name);
std::vector<std::string> transectNames(const InpDocument& doc);
/// Rewrites the block of the transect named @p oldName (or transect.name), or
/// appends a new block. The roughness of the following transect is kept when
/// it inherited its n values from this one.
void writeTransect(InpDocument& doc, const Transect& transect, std::string_view oldName = {});
/// Lines of one transect block (NC, X1 and GR lines; GR with five pairs per line).
std::vector<std::string> formatTransect(const Transect& transect, bool withRoughness = true);
std::vector<ObjectIssue> validateTransect(const Transect& transect);
/// Flow area, top width and wetted perimeter below a water surface elevation
/// (adjusted coordinates; the ends of the section are extended vertically).
TransectGeometry transectGeometry(const Transect& transect, double waterElevation);

// ---------------------------------------------------------------------------
// RDII unit hydrographs ([HYDROGRAPHS])

struct UnitHydrographResponse
{
    bool defined = false;
    double r = 0.0; ///< fraction of rainfall that becomes RDII
    double t = 0.0; ///< time to peak, hours
    double k = 0.0; ///< recession limb duration / time to peak
    std::optional<double> iaMax;      ///< maximum initial abstraction depth (in or mm)
    std::optional<double> iaRecovery; ///< recovery rate (in/day or mm/day)
    std::optional<double> iaInitial;  ///< initial abstraction already used (in or mm)
};

struct UnitHydrographGroup
{
    static constexpr int kMonths = 13; ///< ALL, JAN .. DEC
    static constexpr int kResponses = 3; ///< short, medium, long term
    std::string name;
    std::string gage;
    std::array<std::array<UnitHydrographResponse, kResponses>, kMonths> params{};

    bool monthDefined(int month) const;
};

/// "ALL", "JAN", ... "DEC" for 0..12.
const char* uhMonthKeyword(int month);
/// "SHORT", "MEDIUM", "LONG" for 0..2.
const char* uhResponseKeyword(int response);

std::vector<std::string> unitHydrographNames(const InpDocument& doc);
std::optional<UnitHydrographGroup> readUnitHydrograph(const InpDocument& doc,
                                                      std::string_view name);
void writeUnitHydrograph(InpDocument& doc, const UnitHydrographGroup& group,
                         std::string_view oldName = {});
std::vector<std::vector<std::string>> unitHydrographRows(const UnitHydrographGroup& group);
/// @p model (optional) is used to check the rain gage.
std::vector<ObjectIssue> validateUnitHydrograph(const UnitHydrographGroup& group,
                                                const Model* model = nullptr);
/// Ordinates of the triangular unit hydrograph: (hours, fraction of rain per hour).
/// The area under the triangle equals R.
std::vector<std::pair<double, double>> triangularHydrograph(const UnitHydrographResponse& p);

} // namespace mirab::core
