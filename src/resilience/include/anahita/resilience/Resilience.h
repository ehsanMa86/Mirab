/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/resilience/include/anahita/resilience/Resilience.h
 * Purpose: Native resilience metrics for water distribution networks.
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

#include "anahita/core/Model.h"
#include "anahita/core/Results.h"
#include "anahita/engine/EpanetToolkit.h"

#include <functional>
#include <string>
#include <vector>

namespace anahita::resilience
{

using Progress = std::function<bool(double fraction, const std::string& stage)>; ///< return false to cancel

// ---------------------------------------------------------------------------
// Todini resilience index (Todini 2000, Urban Water 2(2):115-122)
//
//   Ir = sum_i q_i (h_i - h*_i) / (sum_k Q_k H_k + sum_p Q_p dH_p - sum_i q_i h*_i)
//
// q_i demand and h_i head at junction i, h*_i = elevation + required pressure,
// Q_k and H_k outflow and head of reservoirs and tanks that supply water,
// Q_p and dH_p flow and head gain of pumps.
// ---------------------------------------------------------------------------
struct TodiniResult
{
    std::vector<double> times;
    std::vector<double> index; ///< one value per reporting period
    double minimum = 0.0;
    double mean = 0.0;
    std::string note;
};

/// @param requiredPressure in the pressure units of the results
TodiniResult todiniIndex(const core::Model& model, const core::ResultSet& results,
                         double requiredPressure);

// ---------------------------------------------------------------------------
// Water service availability: delivered / expected demand.
// Expected demand comes from "Full Demand" (EPANET 2.3) or from
// "Demand" + "Demand Deficit" (EPANET 2.2); both require a pressure-driven run.
// ---------------------------------------------------------------------------
struct ServiceResult
{
    std::vector<double> times;
    std::vector<double> system;                    ///< per period
    std::vector<std::pair<std::string, double>> nodes; ///< over the whole run
    double overall = 1.0;
    std::string note;
};

ServiceResult waterServiceAvailability(const core::ResultSet& results);

// ---------------------------------------------------------------------------
// Graph topology
// ---------------------------------------------------------------------------
struct TopologyResult
{
    int nodes = 0;
    int links = 0;
    int components = 0;
    double averageDegree = 0.0;
    double linkDensity = 0.0;  ///< 2m / (n (n-1))
    double meshedness = 0.0;   ///< (m - n + 1) / (2n - 5), Buhl et al. 2004
    std::vector<std::string> articulationNodes; ///< nodes whose loss splits the network
    std::vector<std::string> bridgeLinks;       ///< links whose loss splits the network
    std::vector<std::string> deadEnds;          ///< nodes with a single connection
    std::vector<std::string> isolatedNodes;
};

TopologyResult topology(const core::Model& model);

// ---------------------------------------------------------------------------
// Pipe criticality: close each pipe in turn and re-run hydraulics.
// ---------------------------------------------------------------------------
struct CriticalityOptions
{
    double pressureThreshold = 0.0; ///< project pressure units
    bool pressureDriven = true;
    double minimumPressure = 0.0;
    double requiredPressure = 0.0; ///< 0 means "use pressureThreshold"
    std::vector<std::string> links; ///< empty: every pipe
};

struct CriticalityEntry
{
    std::string link;
    int nodesBelowThreshold = 0;  ///< junctions newly below the threshold
    double unmetDemandFraction = 0.0; ///< share of demand not delivered (PDA runs)
    double minimumPressure = 0.0;
    std::string worstNode;
    bool failed = false;
    std::string error;
};

std::vector<CriticalityEntry> pipeCriticality(const engine::EpanetApi& api,
                                              const std::filesystem::path& input,
                                              const CriticalityOptions& options,
                                              const Progress& progress = {});

// ---------------------------------------------------------------------------
// Fire flow: add a fire demand at each node and check residual pressures.
// ---------------------------------------------------------------------------
struct FireFlowOptions
{
    double fireFlow = 0.0;          ///< project flow units
    double residualPressure = 0.0;  ///< project pressure units
    double timeOfDay = 0.0;         ///< seconds; the pattern time used for the snapshot
    bool computeAvailableFlow = true;
    double maximumFlow = 0.0;       ///< upper bound of the search (0: 4 x fireFlow)
    std::vector<std::string> nodes; ///< empty: every junction
};

struct FireFlowEntry
{
    std::string node;
    double residualPressure = 0.0;   ///< at the hydrant node with the fire flow applied
    double minimumPressure = 0.0;    ///< lowest junction pressure anywhere
    std::string criticalNode;
    bool adequate = false;
    double availableFlow = 0.0;      ///< largest flow keeping every junction above the residual
    bool failed = false;
    std::string error;
};

std::vector<FireFlowEntry> fireFlow(const engine::EpanetApi& api, const std::filesystem::path& input,
                                    const FireFlowOptions& options, const Progress& progress = {});

} // namespace anahita::resilience
