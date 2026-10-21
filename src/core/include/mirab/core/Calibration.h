/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Calibration.h
 * Purpose: Observed data, goodness-of-fit metrics and calibration parameters.
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

#include "mirab/core/Json.h"
#include "mirab/core/Model.h"
#include "mirab/core/Results.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::core
{

// ---------------------------------------------------------------------------
// Dates and times (no time zones: every time is local "wall clock" time)
// ---------------------------------------------------------------------------

/// Order of day and month in dates such as 03/04/2024.
enum class DateOrder
{
    MonthFirst, ///< MM/DD/YYYY (EPANET and SWMM convention)
    DayFirst    ///< DD/MM/YYYY
};

/// Parses a calendar date with an optional time of day into seconds since
/// 1970-01-01 00:00. Accepts ISO 8601 ("2024-05-01T13:30:00", optional
/// fraction and "Z" or "+02:00" suffix, which is ignored), "2024/05/01 13:30",
/// "05/01/2024 1:30 PM" and "01-May-2024 13:30".
std::optional<double> parseDateTime(std::string_view text, DateOrder order = DateOrder::MonthFirst);
/// Formats seconds since 1970-01-01 as "YYYY-MM-DD HH:MM[:SS]".
std::string formatDateTime(double epochSeconds);
/// Parses a time of day ("13:30", "1:30 PM", "12 AM", "6") into seconds.
std::optional<double> parseTimeOfDay(std::string_view text);

/// Where the simulation clock starts on the calendar.
struct TimeBase
{
    std::optional<double> startEpoch; ///< calendar start of the simulation, when known
    double startClock = 0.0;          ///< time of day of the start, in seconds
};

/// Calendar start of a simulation. @p simulationStart (a date-time text)
/// overrides the model; otherwise SWMM models use START_DATE and START_TIME
/// and EPANET models only know their START CLOCKTIME.
TimeBase timeBaseFor(const Model* model, const ResultSet* results, const std::string& simulationStart = {});

// ---------------------------------------------------------------------------
// Observed data
// ---------------------------------------------------------------------------

/// One measured time series linked to a simulated result.
struct ObservedSeries
{
    std::string name;     ///< label, e.g. "J5 Pressure"
    std::string table;    ///< result table: "node", "link" or "subcatchment"
    std::string element;  ///< element ID
    std::string variable; ///< result variable, e.g. "Pressure", "Flow", "Depth"
    std::string unit;     ///< unit of the values; empty when they are in model units
    double weight = 1.0;  ///< weight in the calibration objective
    bool calendar = false; ///< times are seconds since 1970 (else seconds from the simulation start)
    std::vector<double> times;
    std::vector<double> values; ///< NaN for missing values
    std::string source;         ///< file the data came from

    /// "table:element:variable"
    std::string key() const;
};

/// Links one column (CSV) or location (calibration file) to a result.
struct ObservedLink
{
    std::string column;   ///< CSV column header (or 1-based number); location ID in calibration files
    std::string table;    ///< "node", "link" or "subcatchment"
    std::string element;  ///< defaults to the location ID in calibration files
    std::string variable;
    std::string unit;
    double weight = 1.0;
    bool enabled = true;
};

/// A file of measurements and how its data map to model results. Stored in
/// the project sidecar with the path relative to the input file.
struct ObservedSource
{
    std::string file;
    std::string format = "auto"; ///< "csv", "epanet" (.dat), "swmm" (.dat) or "auto"

    // Delimited text options
    std::string timeColumn;          ///< header or 1-based number; empty: the first column
    std::string timeFormat = "auto"; ///< "auto", "datetime" or "elapsed"
    std::string timeUnit = "hours";  ///< unit of elapsed numbers: seconds, minutes, hours or days
    std::string dateOrder = "MDY";   ///< "MDY" or "DMY"
    std::string noData;              ///< extra missing-value marker, e.g. "-9999"

    // Calibration files: defaults for every location
    std::string table;
    std::string variable;
    std::string unit;

    std::vector<ObservedLink> links; ///< empty: every column or location, mapped by guessing

    static ObservedSource fromJson(const json::Value& v);
    json::Value toJson() const;
    /// The format to read, resolving "auto" from the extension and the content.
    std::string resolvedFormat(std::string_view content) const;
};

/// A delimited text file split into cells.
struct DelimitedTable
{
    std::vector<std::string> header; ///< column names ("Column 1", ... when the file has none)
    std::vector<std::vector<std::string>> rows;
    char delimiter = ',';
    bool hasHeader = true;
};

/// Splits CSV, TSV, semicolon or blank-separated text. Lines starting with
/// '#' or ';' are comments. A first row that is not mostly numbers is the header.
DelimitedTable readDelimited(std::string_view text);

/// Reads the series of a delimited text source. Times may be one date-time
/// column, a date column followed by a time column, or elapsed time.
std::vector<ObservedSeries> readObservedTable(std::string_view text, const ObservedSource& source,
                                              std::vector<std::string>* warnings = nullptr);

/// Reads an EPANET calibration file (location, time, value; rows without a
/// location continue the previous one) or a SWMM calibration file (a location
/// on its own line, then date, time, value or elapsed time, value rows).
std::vector<ObservedSeries> readCalibrationFile(std::string_view text, const ObservedSource& source,
                                                std::vector<std::string>* warnings = nullptr);

/// Reads the file of a source. Relative paths are resolved against @p baseDir.
/// With a model, series without an element or variable are linked by
/// guessing from the column header. Throws std::runtime_error when the file
/// cannot be read.
std::vector<ObservedSeries> loadObservedSource(const ObservedSource& source,
                                               const std::filesystem::path& baseDir,
                                               const Model* model = nullptr,
                                               std::vector<std::string>* warnings = nullptr);

/// Guesses the element and result of a column header such as "J5 Pressure",
/// "P12_flow" or "Outfall O1". Leaves fields empty when nothing matches.
ObservedLink guessLink(const std::string& header, const Model& model);

/// Default result variable for a table of a model type.
std::string defaultVariable(Dialect dialect, const std::string& table);

/// Observation times in seconds from the simulation start.
std::vector<double> simulationTimes(const ObservedSeries& series, const TimeBase& base);

/// Factor that converts values in unit @p from to unit @p to (pressure, head,
/// length and flow units). Returns 1 when either is empty and nullopt when
/// the units cannot be converted.
std::optional<double> unitFactor(std::string_view from, std::string_view to);

/// True for flow-like variables (volume error is reported for these).
bool isFlowVariable(std::string_view variable);

// ---------------------------------------------------------------------------
// Alignment and metrics
// ---------------------------------------------------------------------------

struct SimulatedSeries
{
    std::vector<double> times; ///< seconds from the simulation start
    std::vector<double> values;
    std::string unit;
    std::string variable; ///< name as found in the results
};

/// Simulated series of an element. Variable names match without regard to
/// case, and "Inflow" finds "Total Inflow". Returns nullopt with an error.
std::optional<SimulatedSeries> simulatedSeries(const ResultSet& results, const std::string& table,
                                               const std::string& element, const std::string& variable,
                                               std::string* error = nullptr);

/// Linear interpolation of a series at time @p t (no extrapolation: NaN outside).
double interpolate(const std::vector<double>& times, const std::vector<double>& values, double t);

/// Observed values paired with simulated values at the same times.
struct AlignedSeries
{
    std::vector<double> times; ///< seconds from the simulation start
    std::vector<double> observed;
    std::vector<double> simulated;
    int missing = 0; ///< observations without a value
    int outside = 0; ///< observations outside the simulated period
};

/// Interpolates the simulated series linearly at the observation times.
AlignedSeries align(const SimulatedSeries& simulated, const std::vector<double>& obsTimes,
                    const std::vector<double>& obsValues, double obsScale = 1.0);

struct FitMetrics
{
    int count = 0;
    double nse = 0.0;          ///< Nash-Sutcliffe efficiency
    double kge = 0.0;          ///< Kling-Gupta efficiency (2012)
    double pbias = 0.0;        ///< percent bias, 100 * sum(sim - obs) / sum(obs)
    double rmse = 0.0;
    double mae = 0.0;
    double r2 = 0.0;           ///< squared Pearson correlation
    double peakError = 0.0;    ///< simulated peak minus observed peak
    double peakErrorPct = 0.0; ///< peak error in percent of the observed peak
    double peakTiming = 0.0;   ///< time of simulated peak minus time of observed peak, seconds
    double volumeError = 0.0;  ///< percent error of the volume (trapezoidal integral)
    double meanObserved = 0.0;
    double meanSimulated = 0.0;
};

/// Goodness of fit of aligned series. Undefined metrics are NaN.
FitMetrics computeMetrics(const AlignedSeries& aligned);

enum class Metric
{
    Nse,
    Kge,
    Pbias,
    Rmse,
    Mae,
    R2,
    PeakError,
    PeakTiming,
    VolumeError
};

const std::vector<Metric>& allMetrics();
const char* metricName(Metric m); ///< "NSE", "KGE", "PBIAS", "RMSE", ...
std::optional<Metric> metricFromName(std::string_view name);
double metricValue(Metric m, const FitMetrics& f);
/// Value to minimise: 1 - NSE, 1 - KGE, 1 - R2, |PBIAS| / 100, RMSE, MAE,
/// |peak error| / 100 (percent), |peak timing| in hours, |volume error| / 100.
double metricLoss(Metric m, const FitMetrics& f);

/// The fit of one observed series.
struct SeriesFit
{
    std::string key;
    std::string name;
    std::string unit; ///< unit of the simulated values
    bool ok = false;
    std::string error;
    AlignedSeries aligned;
    FitMetrics metrics;
};

/// Aligns every observed series with the results and computes its metrics.
std::vector<SeriesFit> evaluateFit(const std::vector<ObservedSeries>& observed, const ResultSet& results,
                                   const TimeBase& base);

// ---------------------------------------------------------------------------
// Calibration set-up
// ---------------------------------------------------------------------------

/// A model input adjusted by the optimiser.
struct CalibrationParameter
{
    enum class Mode
    {
        Multiply, ///< value = original value * x
        Replace   ///< value = x
    };
    std::string name;
    std::string section;               ///< e.g. "PIPES"
    std::string column;                ///< schema column name, or token position (the ID is 0)
    std::vector<std::string> elements; ///< empty: every row of the section
    Mode mode = Mode::Multiply;
    double min = 0.5;
    double max = 1.5;

    static CalibrationParameter fromJson(const json::Value& v);
    json::Value toJson() const;
    std::string label() const; ///< name, or "SECTION column"
};

struct ObjectiveTerm
{
    Metric metric = Metric::Nse;
    double weight = 1.0;
};

/// Everything the calibration needs; stored under "calibration" in the sidecar.
struct CalibrationConfig
{
    std::vector<ObservedSource> observed;
    std::string simulationStart; ///< calendar start of EPANET runs, e.g. "2024-05-01 00:00"
    std::vector<CalibrationParameter> parameters;
    std::vector<ObjectiveTerm> objective; ///< empty: NSE with weight 1
    int budget = 100;
    int jobs = 0; ///< parallel engine runs; 0 picks the processor count
    unsigned seed = 1;
    std::string engine; ///< empty: the project engine

    static CalibrationConfig fromJson(const json::Value& v);
    json::Value toJson() const;
    bool empty() const { return observed.empty() && parameters.empty(); }
};

/// Loads every source; problems are reported as warnings.
std::vector<ObservedSeries> loadObserved(const CalibrationConfig& config,
                                         const std::filesystem::path& baseDir,
                                         const Model* model = nullptr,
                                         std::vector<std::string>* warnings = nullptr);

/// Weighted sum of metric losses over the series (each series has a weight).
/// Metrics that are undefined for a series (volume error of a pressure) are
/// left out. Series that cannot be evaluated make the result +inf.
double objectiveValue(const std::vector<ObjectiveTerm>& terms, const std::vector<ObservedSeries>& observed,
                      const std::vector<SeriesFit>& fits);

/// Schema column index of a parameter, or -1.
int parameterColumn(const Model& model, const CalibrationParameter& p);
/// Number of rows a parameter changes.
int parameterRowCount(const Model& model, const CalibrationParameter& p);
/// Starting point: 1 for multipliers, the mean current value for replacements (clamped to the range).
double parameterInitialValue(const Model& model, const CalibrationParameter& p);
/// Writes parameter values into the model. Returns false with an error when a
/// parameter matches nothing.
bool applyParameters(Model& model, const std::vector<CalibrationParameter>& params,
                     const std::vector<double>& values, std::string* error = nullptr);

} // namespace mirab::core
