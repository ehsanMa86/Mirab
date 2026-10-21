/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/ControlRules.h
 * Purpose: Parser and checker of SWMM and EPANET control rules.
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

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::core
{

class Model;

enum class RuleSyntax
{
    Swmm,  ///< SWMM 5.2 [CONTROLS]: rules, named variables and expressions
    Epanet ///< EPANET [RULES]
};

struct RuleMessage
{
    int line = 0; ///< 1-based line in the checked text; 0 for the whole text
    bool error = true; ///< false for warnings
    std::string message;
};

/// A part of a rules text: the lines before the first rule (name empty), or
/// one rule together with the comment lines directly above it.
struct RuleBlock
{
    std::string name;
    int firstLine = 0; ///< 0-based index of the first line
    int lineCount = 0;
};

/// Splits rule text into blocks. The first block is always the leading part
/// (possibly empty); the blocks cover every line, in order.
std::vector<RuleBlock> splitRuleBlocks(const std::vector<std::string>& lines);

/// Model objects that rules may refer to. Lookups ignore case, like SWMM.
class RuleContext
{
public:
    static RuleContext fromModel(const Model& model);

    void addNode(const std::string& id, const std::string& kind);
    void addLink(const std::string& id, const std::string& kind);
    void addGage(const std::string& id);
    void addCurve(const std::string& id);
    void addTimeSeries(const std::string& id);

    /// Kind of a node or link ("Storage", "Pump", ...); empty when unknown.
    std::string nodeKind(std::string_view id) const;
    std::string linkKind(std::string_view id) const;
    bool hasGage(std::string_view id) const;
    bool hasCurve(std::string_view id) const;
    bool hasTimeSeries(std::string_view id) const;

    /// IDs in model order, filtered by kind when @p kind is not empty.
    std::vector<std::string> nodes(std::string_view kind = {}) const;
    std::vector<std::string> links(std::string_view kind = {}) const;
    const std::vector<std::string>& gages() const { return gageList_; }
    const std::vector<std::string>& curves() const { return curveList_; }
    const std::vector<std::string>& timeSeries() const { return seriesList_; }

private:
    std::vector<std::pair<std::string, std::string>> nodeList_, linkList_;
    std::vector<std::string> gageList_, curveList_, seriesList_;
    std::map<std::string, std::string> nodeKinds_, linkKinds_; // upper-case ID -> kind
    std::set<std::string> gages_, curves_, series_;            // upper-case IDs
};

/// Checks a SWMM [CONTROLS] or EPANET [RULES] section. With a context, the
/// object IDs the rules refer to must exist and have the right type.
std::vector<RuleMessage> validateRules(const std::vector<std::string>& lines, RuleSyntax syntax,
                                       const RuleContext* context = nullptr);

/// Checks EPANET simple controls ([CONTROLS]).
std::vector<RuleMessage> validateSimpleControls(const std::vector<std::string>& lines,
                                                const RuleContext* context = nullptr);

// Grammar, for editors and highlighters.

/// Clause keywords: RULE, IF, AND, OR, THEN, ELSE, PRIORITY (and VARIABLE,
/// EXPRESSION for SWMM).
std::vector<std::string> ruleKeywords(RuleSyntax syntax);
/// Object types allowed in conditions (or actions when @p action is true).
std::vector<std::string> ruleObjectTypes(RuleSyntax syntax, bool action);
/// True for object types that are followed by an ID (all but SIMULATION / SYSTEM).
bool ruleObjectHasId(RuleSyntax syntax, std::string_view object);
std::vector<std::string> ruleAttributes(RuleSyntax syntax, std::string_view object, bool action);
std::vector<std::string> ruleRelations(RuleSyntax syntax);
/// Allowed words for an attribute value (ON/OFF, OPEN/CLOSED, ...); empty for numbers.
std::vector<std::string> ruleValueWords(RuleSyntax syntax, std::string_view object,
                                        std::string_view attribute);
/// Other words with a special meaning (CURVE, TIMESERIES, PID, IS, AM, ...).
std::vector<std::string> ruleOtherWords(RuleSyntax syntax);
/// IDs of the model objects of a rule object type.
std::vector<std::string> ruleObjectIds(const RuleContext& context, RuleSyntax syntax,
                                       std::string_view object);

struct RuleTemplate
{
    std::string title;
    std::string description;
    std::vector<std::string> variables; ///< VARIABLE / EXPRESSION lines for the leading block
    std::vector<std::string> lines;     ///< the rule (or rules)
};

/// Starting points for new rules, filled in with objects of the model when
/// possible. Rule names are made unique against @p usedNames.
std::vector<RuleTemplate> ruleTemplates(RuleSyntax syntax, const RuleContext& context,
                                        const std::set<std::string>& usedNames);
/// @p base, or base_2, base_3, ... whichever is not in @p usedNames (ignoring case).
std::string uniqueRuleName(const std::set<std::string>& usedNames, const std::string& base);
/// Names of the rules in a text.
std::set<std::string> ruleNames(const std::vector<std::string>& lines);

} // namespace mirab::core
