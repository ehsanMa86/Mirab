/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/ControlRules.cpp
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
#include "mirab/core/ControlRules.h"

#include "mirab/core/Model.h"
#include "mirab/core/SwmmObjects.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace mirab::core
{

namespace
{
using Tokens = std::vector<std::string>;

bool contains(const std::vector<std::string>& words, std::string_view w)
{
    return std::any_of(words.begin(), words.end(),
                       [&](const std::string& x) { return text::iequals(x, w); });
}

std::string upper(std::string_view s)
{
    return text::toUpper(s);
}

std::string joinFrom(const Tokens& t, std::size_t from)
{
    std::string s;
    for (std::size_t i = from; i < t.size(); ++i)
    {
        if (!s.empty())
            s += ' ';
        s += t[i];
    }
    return s;
}

bool isTime(std::string_view v)
{
    return text::parseClockOrDuration(v).has_value();
}

/// A date as SWMM reads it (datetime_strToDate): month, day and year separated
/// by "/" or "-", the month as a number or a three-letter name.
bool isDate(std::string_view v)
{
    static const char* const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    std::vector<std::string> parts;
    std::string cur;
    for (char c : std::string(v) + "/")
    {
        if (c == '/' || c == '-')
        {
            if (cur.empty() || parts.size() >= 3)
                return false;
            parts.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (parts.size() != 3)
        return false;
    int month = 0;
    if (const auto m = text::toInt(parts[0]))
    {
        month = static_cast<int>(*m);
    }
    else
    {
        const std::string name = text::toUpper(parts[0]);
        for (int i = 0; i < 12; ++i)
        {
            if (name == months[i])
                month = i + 1;
        }
    }
    const auto day = text::toInt(parts[1]);
    return month >= 1 && month <= 12 && day && *day >= 1 && *day <= 31 && text::toInt(parts[2]).has_value();
}

/// Hours of a gage attribute like 6HR_PRECIP or 6-HR_PRECIP; 0 when it is not one.
int pastRainHours(std::string_view a)
{
    std::size_t i = 0;
    while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i])))
        ++i;
    if (i == 0)
        return 0;
    std::string rest = upper(a.substr(i));
    if (!rest.empty() && rest.front() == '-')
        rest.erase(0, 1);
    if (rest != "HR_PRECIP")
        return 0;
    return static_cast<int>(text::toInt(a.substr(0, i)).value_or(0));
}

const std::vector<std::string> kSwmmMathFunctions = {
    "COS",  "SIN",  "TAN",  "COT",  "ABS",  "SGN",  "SQRT",  "LOG",  "EXP", "ASIN",
    "ACOS", "ATAN", "ACOT", "SINH", "COSH", "TANH", "COTH", "LOG10", "STEP"};

/// Kind expected for a rule object type, empty when any kind is fine.
std::string expectedKind(RuleSyntax syntax, std::string_view object)
{
    const std::string o = upper(object);
    if (syntax == RuleSyntax::Swmm)
    {
        if (o == "CONDUIT")
            return "Conduit";
        if (o == "PUMP")
            return "Pump";
        if (o == "ORIFICE")
            return "Orifice";
        if (o == "WEIR")
            return "Weir";
        if (o == "OUTLET")
            return "Outlet";
        return {};
    }
    if (o == "JUNCTION")
        return "Junction";
    if (o == "RESERVOIR")
        return "Reservoir";
    if (o == "TANK")
        return "Tank";
    if (o == "PIPE")
        return "Pipe";
    if (o == "PUMP")
        return "Pump";
    if (o == "VALVE")
        return "Valve";
    return {};
}

bool isNodeObject(RuleSyntax syntax, std::string_view o)
{
    if (syntax == RuleSyntax::Swmm)
        return text::iequals(o, "NODE");
    return contains({"NODE", "JUNCTION", "RESERVOIR", "TANK"}, o);
}

bool isLinkObject(RuleSyntax syntax, std::string_view o)
{
    if (syntax == RuleSyntax::Swmm)
        return contains({"LINK", "CONDUIT", "PUMP", "ORIFICE", "WEIR", "OUTLET"}, o);
    return contains({"LINK", "PIPE", "PUMP", "VALVE"}, o);
}

class Checker
{
public:
    Checker(RuleSyntax syntax, const RuleContext* ctx) : syntax_(syntax), ctx_(ctx) {}

    std::vector<RuleMessage> messages;

    void err(int line, std::string m) { messages.push_back({line, true, std::move(m)}); }
    void warn(int line, std::string m) { messages.push_back({line, false, std::move(m)}); }

    /// Checks that an object ID exists with the right kind.
    void checkId(int line, std::string_view object, const std::string& id, bool strictKind)
    {
        if (!ctx_)
            return;
        const std::string o = upper(object);
        if (o == "GAGE")
        {
            if (!ctx_->hasGage(id))
                err(line, "Rain gage " + id + " does not exist.");
            return;
        }
        std::string kind;
        const bool node = isNodeObject(syntax_, o);
        kind = node ? ctx_->nodeKind(id) : ctx_->linkKind(id);
        if (kind.empty())
        {
            err(line, std::string(node ? "Node " : "Link ") + id + " does not exist.");
            return;
        }
        const std::string want = expectedKind(syntax_, o);
        if (!want.empty() && !text::iequals(kind, want))
        {
            const std::string m = id + " is a " + text::toLower(kind) + ", not a " +
                                  text::toLower(want) + ".";
            if (strictKind)
                err(line, m);
            else
                warn(line, m);
        }
    }

    /// Parses "object id attribute" (or "SIMULATION attribute") at t[pos].
    /// Returns the attribute (upper case) or empty on error; advances pos past it.
    std::string variable(int line, const Tokens& t, std::size_t& pos, std::string* objectOut)
    {
        if (pos >= t.size())
        {
            err(line, "The condition is incomplete.");
            return {};
        }
        const std::string obj = upper(t[pos]);
        if (!contains(ruleObjectTypes(syntax_, false), obj))
        {
            err(line, "Unknown object type \"" + t[pos] + "\".");
            return {};
        }
        if (objectOut)
            *objectOut = obj;
        ++pos;
        if (ruleObjectHasId(syntax_, obj))
        {
            if (pos >= t.size())
            {
                err(line, "An ID is missing after " + obj + ".");
                return {};
            }
            checkId(line, obj, t[pos], false);
            ++pos;
        }
        if (pos >= t.size())
        {
            err(line, "An attribute is missing after " + obj + ".");
            return {};
        }
        const std::string attr = upper(t[pos]);
        ++pos;
        if (syntax_ == RuleSyntax::Swmm && obj == "GAGE")
        {
            if (attr == "INTENSITY")
                return attr;
            const int h = pastRainHours(attr);
            if (h >= 1 && h <= 48)
                return "PRECIP";
            err(line, "Rain gage attribute must be INTENSITY or nHR_PRECIP with n from 1 to 48.");
            return {};
        }
        const auto allowed = ruleAttributes(syntax_, obj, false);
        if (!contains(allowed, attr))
        {
            err(line, "\"" + t[pos - 1] + "\" is not an attribute of " + obj +
                          " (use " + text::join(allowed, ", ") + ").");
            return {};
        }
        return attr;
    }

    void value(int line, const std::string& obj, const std::string& attr, const std::string& v)
    {
        if (attr == "STATUS")
        {
            if (!contains(ruleValueWords(syntax_, obj, attr), v))
                err(line, "Status must be " + text::join(ruleValueWords(syntax_, obj, attr), ", ") +
                              "; found \"" + v + "\".");
        }
        else if (attr == "TIME" || attr == "CLOCKTIME" || attr == "TIMEOPEN" ||
                 attr == "TIMECLOSED" || attr == "FILLTIME" || attr == "DRAINTIME")
        {
            if (!isTime(v))
                err(line, "\"" + v + "\" is not a time (decimal hours or hh:mm).");
        }
        else if (attr == "DATE")
        {
            if (!isDate(v))
                err(line, "\"" + v + "\" is not a date (month/day/year).");
        }
        else if (attr == "DAY")
        {
            const auto d = text::toDouble(v);
            if (!d || *d < 1 || *d > 7)
                err(line, "Day of the week must be from 1 (Sunday) to 7.");
        }
        else if (attr == "MONTH")
        {
            const auto d = text::toDouble(v);
            if (!d || *d < 1 || *d > 12)
                err(line, "Month must be from 1 to 12.");
        }
        else if (attr == "DAYOFYEAR")
        {
            const auto d = text::toDouble(v);
            const bool md = v.find_first_of("/-") != std::string::npos && isDate(v + "/1947");
            if (!md && (!d || *d < 1 || *d > 365))
                err(line, "Day of the year must be from 1 to 365 or month/day.");
        }
        else if (!text::toDouble(v))
        {
            err(line, "\"" + v + "\" is not a number.");
        }
    }

    void swmmCondition(int line, const Tokens& t)
    {
        // t[0] is the clause keyword.
        std::size_t pos = 1;
        std::string obj, attr;
        bool numericLhs = false;
        const std::string first = pos < t.size() ? upper(t[pos]) : std::string();
        if (expressions_.count(first) || variables_.count(first))
        {
            const std::string name = upper(t[pos]);
            if (expressions_.count(name))
                numericLhs = true;
            else
            {
                obj = variables_[name].first;
                attr = variables_[name].second;
            }
            ++pos;
        }
        else
        {
            if (pos < t.size() && !contains(ruleObjectTypes(syntax_, false), t[pos]) &&
                laterNames_.count(upper(t[pos])))
            {
                err(line, "\"" + t[pos] + "\" is used before it is defined.");
                return;
            }
            attr = variable(line, t, pos, &obj);
            if (attr.empty())
                return;
        }
        if (pos >= t.size())
        {
            err(line, "A relational operator (=, <>, <, <=, >, >=) is missing.");
            return;
        }
        if (!contains(ruleRelations(syntax_), t[pos]))
        {
            err(line, "\"" + t[pos] + "\" is not a relational operator (=, <>, <, <=, >, >=).");
            return;
        }
        ++pos;
        if (pos >= t.size())
        {
            err(line, "The value to compare with is missing.");
            return;
        }
        const std::string rhs = upper(t[pos]);
        if (variables_.count(rhs) || expressions_.count(rhs))
        {
            ++pos;
        }
        else if (contains(ruleObjectTypes(syntax_, false), rhs))
        {
            std::string obj2;
            const std::string attr2 = variable(line, t, pos, &obj2);
            if (attr2.empty())
                return;
            if (!numericLhs && attr2 != attr)
                warn(line, "The condition compares different attributes (" + attr + " and " +
                               attr2 + ").");
        }
        else
        {
            if (numericLhs)
            {
                if (!text::toDouble(t[pos]))
                    err(line, "\"" + t[pos] + "\" is not a number.");
            }
            else
            {
                value(line, obj, attr, t[pos]);
            }
            ++pos;
        }
        if (pos < t.size())
            err(line, "Unexpected text after the condition: \"" + joinFrom(t, pos) + "\".");
    }

    void swmmAction(int line, const Tokens& t)
    {
        if (t.size() < 6)
        {
            err(line, "An action has the form: object ID attribute = value.");
            return;
        }
        const std::string obj = upper(t[1]);
        if (!contains(ruleObjectTypes(syntax_, true), obj))
        {
            err(line, "Actions apply to CONDUIT, PUMP, ORIFICE, WEIR or OUTLET, not \"" + t[1] +
                          "\".");
            return;
        }
        checkId(line, obj, t[2], true);
        const std::string attr = upper(t[3]);
        const auto allowed = ruleAttributes(syntax_, obj, true);
        if (!contains(allowed, attr))
        {
            err(line, obj + " actions set " + text::join(allowed, " or ") + ", not \"" + t[3] +
                          "\".");
            return;
        }
        if (t[4] != "=")
            err(line, "\"=\" is expected after " + attr + ".");
        std::size_t used = 6;
        if (attr == "STATUS")
        {
            if (!contains(ruleValueWords(syntax_, obj, attr), t[5]))
                err(line, obj + " status must be " +
                              text::join(ruleValueWords(syntax_, obj, attr), " or ") + ".");
        }
        else
        {
            const std::string kind = upper(t[5]);
            if (kind == "CURVE" || kind == "TIMESERIES")
            {
                if (t.size() < 7)
                {
                    err(line, "The name of the " + text::toLower(kind) + " is missing.");
                    return;
                }
                used = 7;
                if (ctx_ && kind == "CURVE" && !ctx_->hasCurve(t[6]))
                    err(line, "Curve " + t[6] + " does not exist.");
                if (ctx_ && kind == "TIMESERIES" && !ctx_->hasTimeSeries(t[6]))
                    err(line, "Time series " + t[6] + " does not exist.");
            }
            else if (kind == "PID")
            {
                used = 9;
                if (t.size() < 9)
                {
                    err(line, "A PID setting needs three coefficients: PID Kp Ki Kd.");
                    return;
                }
                for (std::size_t i = 6; i < 9; ++i)
                {
                    if (!text::toDouble(t[i]))
                        err(line, "PID coefficient \"" + t[i] + "\" is not a number.");
                }
            }
            else
            {
                const auto v = text::toDouble(t[5]);
                if (!v)
                    err(line, "\"" + t[5] + "\" is not a number, CURVE, TIMESERIES or PID.");
                else if (obj != "PUMP" && (*v < 0.0 || *v > 1.0))
                    err(line, obj + " settings are fractions from 0 to 1.");
                else if (*v < 0.0)
                    err(line, "A pump setting cannot be negative.");
            }
        }
        if (t.size() > used)
            err(line, "Unexpected text after the action: \"" + joinFrom(t, used) + "\".");
    }

    void epanetCondition(int line, const Tokens& t)
    {
        std::size_t pos = 1;
        std::string obj;
        const std::string attr = variable(line, t, pos, &obj);
        if (attr.empty())
            return;
        if (pos >= t.size())
        {
            err(line, "A relational operator is missing.");
            return;
        }
        const std::string rel = upper(t[pos]);
        if (!contains(ruleRelations(syntax_), rel))
        {
            err(line, "\"" + t[pos] + "\" is not a relational operator.");
            return;
        }
        ++pos;
        if (pos >= t.size())
        {
            err(line, "The value to compare with is missing.");
            return;
        }
        value(line, obj, attr, t[pos]);
        ++pos;
        if (attr == "CLOCKTIME" && pos < t.size() &&
            (text::iequals(t[pos], "AM") || text::iequals(t[pos], "PM")))
            ++pos;
        if (pos < t.size())
            err(line, "Unexpected text after the condition: \"" + joinFrom(t, pos) + "\".");
    }

    void epanetAction(int line, const Tokens& t)
    {
        if (t.size() < 5)
        {
            err(line, "An action has the form: object ID STATUS/SETTING IS value.");
            return;
        }
        const std::string obj = upper(t[1]);
        if (!contains(ruleObjectTypes(syntax_, true), obj))
        {
            err(line, "Actions apply to LINK, PIPE, PUMP or VALVE, not \"" + t[1] + "\".");
            return;
        }
        checkId(line, obj, t[2], true);
        const std::string attr = upper(t[3]);
        if (attr != "STATUS" && attr != "SETTING")
        {
            err(line, "Actions set STATUS or SETTING, not \"" + t[3] + "\".");
            return;
        }
        if (!text::iequals(t[4], "IS") && t[4] != "=")
            err(line, "\"IS\" is expected after " + attr + ".");
        if (t.size() < 6)
        {
            err(line, "The value of the action is missing.");
            return;
        }
        if (attr == "STATUS" && !contains({"OPEN", "CLOSED", "ACTIVE"}, t[5]))
            err(line, "Status must be OPEN, CLOSED or ACTIVE.");
        if (attr == "SETTING" && !text::toDouble(t[5]))
            err(line, "\"" + t[5] + "\" is not a number.");
        if (t.size() > 6)
            err(line, "Unexpected text after the action: \"" + joinFrom(t, 6) + "\".");
    }

    void swmmVariable(int line, const Tokens& t)
    {
        if (t.size() < 5 || t[2] != "=")
        {
            err(line, "A named variable has the form: VARIABLE name = object ID attribute.");
            return;
        }
        const std::string name = upper(t[1]);
        if (contains(allAttributes(), name) || contains(ruleObjectTypes(syntax_, false), name))
        {
            err(line, "\"" + t[1] + "\" is a keyword and cannot name a variable.");
            return;
        }
        if (variables_.count(name) || expressions_.count(name))
            err(line, "\"" + t[1] + "\" is defined twice.");
        std::size_t pos = 3;
        std::string obj;
        const std::string attr = variable(line, t, pos, &obj);
        if (!attr.empty() && pos < t.size())
            err(line, "Unexpected text after the variable: \"" + joinFrom(t, pos) + "\".");
        variables_[name] = {obj, attr};
    }

    void swmmExpression(int line, const Tokens& t)
    {
        if (t.size() < 4 || t[2] != "=")
        {
            err(line, "An expression has the form: EXPRESSION name = formula.");
            return;
        }
        const std::string name = upper(t[1]);
        if (variables_.count(name) || expressions_.count(name))
            err(line, "\"" + t[1] + "\" is defined twice.");
        const std::string f = joinFrom(t, 3);
        int depth = 0;
        for (std::size_t i = 0; i < f.size();)
        {
            const char c = f[i];
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++i;
            }
            else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                std::size_t j = i;
                while (j < f.size() && (std::isalnum(static_cast<unsigned char>(f[j])) ||
                                        f[j] == '_' || f[j] == '.'))
                    ++j;
                const std::string id = upper(f.substr(i, j - i));
                if (!variables_.count(id) && !contains(kSwmmMathFunctions, id))
                    err(line, "\"" + f.substr(i, j - i) +
                                  "\" in the expression is not a named variable or function.");
                i = j;
            }
            else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
            {
                std::size_t j = i;
                while (j < f.size() && (std::isdigit(static_cast<unsigned char>(f[j])) ||
                                        f[j] == '.' || f[j] == 'e' || f[j] == 'E'))
                    ++j;
                i = j;
            }
            else if (c == '(' || c == ')')
            {
                depth += c == '(' ? 1 : -1;
                if (depth < 0)
                    break;
                ++i;
            }
            else if (std::string("+-*/^,").find(c) != std::string::npos)
            {
                ++i;
            }
            else
            {
                err(line, std::string("Unexpected character \"") + c + "\" in the expression.");
                ++i;
            }
        }
        if (depth != 0)
            err(line, "The parentheses of the expression do not match.");
        expressions_.insert(name);
    }

    void run(const std::vector<std::string>& lines)
    {
        enum State
        {
            None,
            InRule,
            InIf,
            InThen,
            InElse,
            InPriority
        } state = None;
        std::set<std::string> ruleNames;
        int ruleLine = 0;
        std::string ruleName;
        // Names defined anywhere, to tell "used before defined" from "unknown".
        if (syntax_ == RuleSyntax::Swmm)
        {
            for (const auto& l : lines)
            {
                const auto t = text::splitInpLine(l).tokens;
                if (t.size() > 1 &&
                    (text::iequals(t[0], "VARIABLE") || text::iequals(t[0], "EXPRESSION")))
                    laterNames_.insert(upper(t[1]));
            }
        }
        auto endRule = [&] {
            if (state == InRule || state == InIf)
                err(ruleLine, "Rule " + ruleName + " has no THEN action.");
        };
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const int ln = static_cast<int>(i) + 1;
            const auto t = text::splitInpLine(lines[i]).tokens;
            if (t.empty())
                continue;
            const std::string kw = upper(t[0]);
            if (kw == "RULE")
            {
                endRule();
                state = InRule;
                ruleLine = ln;
                ruleName = t.size() > 1 ? t[1] : std::string();
                if (t.size() < 2)
                    err(ln, "The rule needs a name.");
                else if (t.size() > 2)
                    err(ln, "A rule name cannot contain blanks.");
                else if (!ruleNames.insert(upper(t[1])).second)
                    err(ln, "Rule " + t[1] + " is defined twice.");
                continue;
            }
            if (syntax_ == RuleSyntax::Swmm && kw == "VARIABLE")
            {
                swmmVariable(ln, t);
                continue;
            }
            if (syntax_ == RuleSyntax::Swmm && kw == "EXPRESSION")
            {
                swmmExpression(ln, t);
                continue;
            }
            if (!contains({"IF", "AND", "OR", "THEN", "ELSE", "PRIORITY"}, kw))
            {
                const std::string extra =
                    syntax_ == RuleSyntax::Swmm ? ", VARIABLE, EXPRESSION" : "";
                err(ln, "\"" + t[0] +
                            "\" is not a rule keyword (RULE, IF, AND, OR, THEN, ELSE, PRIORITY" +
                            extra + ").");
                continue;
            }
            if (state == None)
            {
                err(ln, kw + " appears outside a rule; start the rule with RULE name.");
                continue;
            }
            const bool swmm = syntax_ == RuleSyntax::Swmm;
            auto condition = [&] { swmm ? swmmCondition(ln, t) : epanetCondition(ln, t); };
            auto action = [&] { swmm ? swmmAction(ln, t) : epanetAction(ln, t); };
            if (kw == "IF")
            {
                if (state != InRule)
                    err(ln, "IF must come right after the RULE line.");
                state = InIf;
                condition();
            }
            else if (kw == "AND")
            {
                if (state == InIf)
                    condition();
                else if (state == InThen || state == InElse)
                    action();
                else
                    err(ln, "AND must follow an IF, THEN or ELSE clause.");
            }
            else if (kw == "OR")
            {
                if (state != InIf)
                    err(ln, "OR can only join conditions (after IF).");
                else
                    condition();
            }
            else if (kw == "THEN")
            {
                if (state != InIf)
                    err(ln, state == InRule ? "THEN needs an IF condition before it."
                                            : "A rule has only one THEN clause.");
                state = InThen;
                action();
            }
            else if (kw == "ELSE")
            {
                if (state != InThen)
                    err(ln, "ELSE must follow the THEN actions.");
                state = InElse;
                action();
            }
            else if (kw == "PRIORITY")
            {
                if (state != InThen && state != InElse)
                    err(ln, "PRIORITY must be the last clause of a rule, after the actions.");
                state = InPriority;
                if (t.size() != 2 || !text::toDouble(t[1]))
                    err(ln, "PRIORITY needs one number.");
            }
        }
        endRule();
        std::stable_sort(messages.begin(), messages.end(),
                         [](const RuleMessage& a, const RuleMessage& b) {
                             return a.line < b.line;
                         });
    }

private:
    static const std::vector<std::string>& allAttributes()
    {
        static const std::vector<std::string> a = {
            "DEPTH",    "MAXDEPTH", "HEAD",       "VOLUME", "INFLOW", "FLOW",      "FULLFLOW",
            "FULLDEPTH", "STATUS",  "SETTING",    "LENGTH", "SLOPE",  "VELOCITY",  "TIMEOPEN",
            "TIMECLOSED", "TIME",   "DATE",       "CLOCKTIME", "DAYOFYEAR", "DAY", "MONTH",
            "INTENSITY"};
        return a;
    }

    RuleSyntax syntax_;
    const RuleContext* ctx_;
    // Named variable -> (object, attribute).
    std::map<std::string, std::pair<std::string, std::string>> variables_;
    std::set<std::string> expressions_;
    std::set<std::string> laterNames_;
};

} // namespace

// ---------------------------------------------------------------------------

std::vector<RuleBlock> splitRuleBlocks(const std::vector<std::string>& lines)
{
    std::vector<RuleBlock> blocks;
    blocks.push_back({});
    int lowerBound = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        const auto t = text::splitInpLine(lines[static_cast<std::size_t>(i)]).tokens;
        if (t.empty() || !text::iequals(t[0], "RULE"))
            continue;
        // Comment lines directly above the RULE line describe the rule.
        int start = i;
        while (start > lowerBound)
        {
            const std::string l = text::trim(lines[static_cast<std::size_t>(start - 1)]);
            if (l.empty() || l.front() != ';')
                break;
            --start;
        }
        RuleBlock b;
        b.name = t.size() > 1 ? t[1] : std::string();
        b.firstLine = start;
        blocks.push_back(b);
        lowerBound = i + 1;
    }
    for (std::size_t k = 0; k < blocks.size(); ++k)
    {
        const int end = k + 1 < blocks.size() ? blocks[k + 1].firstLine
                                              : static_cast<int>(lines.size());
        blocks[k].lineCount = end - blocks[k].firstLine;
    }
    return blocks;
}

RuleContext RuleContext::fromModel(const Model& model)
{
    RuleContext c;
    for (const auto& n : model.nodes())
        c.addNode(n.id, n.kind);
    for (const auto& l : model.links())
        c.addLink(l.id, l.kind);
    for (const auto& g : model.gages())
        c.addGage(g.id);
    for (const auto& id : keyedObjectNames(model.document(), "CURVES"))
        c.addCurve(id);
    for (const auto& id : keyedObjectNames(model.document(), "TIMESERIES"))
        c.addTimeSeries(id);
    return c;
}

void RuleContext::addNode(const std::string& id, const std::string& kind)
{
    nodeList_.emplace_back(id, kind);
    nodeKinds_[upper(id)] = kind;
}

void RuleContext::addLink(const std::string& id, const std::string& kind)
{
    linkList_.emplace_back(id, kind);
    linkKinds_[upper(id)] = kind;
}

void RuleContext::addGage(const std::string& id)
{
    gageList_.push_back(id);
    gages_.insert(upper(id));
}

void RuleContext::addCurve(const std::string& id)
{
    curveList_.push_back(id);
    curves_.insert(upper(id));
}

void RuleContext::addTimeSeries(const std::string& id)
{
    seriesList_.push_back(id);
    series_.insert(upper(id));
}

std::string RuleContext::nodeKind(std::string_view id) const
{
    auto it = nodeKinds_.find(upper(id));
    return it == nodeKinds_.end() ? std::string() : it->second;
}

std::string RuleContext::linkKind(std::string_view id) const
{
    auto it = linkKinds_.find(upper(id));
    return it == linkKinds_.end() ? std::string() : it->second;
}

bool RuleContext::hasGage(std::string_view id) const
{
    return gages_.count(upper(id)) > 0;
}

bool RuleContext::hasCurve(std::string_view id) const
{
    return curves_.count(upper(id)) > 0;
}

bool RuleContext::hasTimeSeries(std::string_view id) const
{
    return series_.count(upper(id)) > 0;
}

std::vector<std::string> RuleContext::nodes(std::string_view kind) const
{
    std::vector<std::string> out;
    for (const auto& [id, k] : nodeList_)
    {
        if (kind.empty() || text::iequals(k, kind))
            out.push_back(id);
    }
    return out;
}

std::vector<std::string> RuleContext::links(std::string_view kind) const
{
    std::vector<std::string> out;
    for (const auto& [id, k] : linkList_)
    {
        if (kind.empty() || text::iequals(k, kind))
            out.push_back(id);
    }
    return out;
}

std::vector<RuleMessage> validateRules(const std::vector<std::string>& lines, RuleSyntax syntax,
                                       const RuleContext* context)
{
    Checker c(syntax, context);
    c.run(lines);
    return std::move(c.messages);
}

std::vector<RuleMessage> validateSimpleControls(const std::vector<std::string>& lines,
                                                const RuleContext* context)
{
    Checker c(RuleSyntax::Epanet, context);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const int ln = static_cast<int>(i) + 1;
        const auto t = text::splitInpLine(lines[i]).tokens;
        if (t.empty())
            continue;
        if (!text::iequals(t[0], "LINK") || t.size() < 5)
        {
            c.err(ln, "A control has the form: LINK ID status-or-setting AT TIME t, AT "
                      "CLOCKTIME t AM/PM, or IF NODE ID ABOVE/BELOW value.");
            continue;
        }
        c.checkId(ln, "LINK", t[1], true);
        if (!contains({"OPEN", "CLOSED"}, t[2]) && !text::toDouble(t[2]))
            c.err(ln, "\"" + t[2] + "\" must be OPEN, CLOSED or a numeric setting.");
        const std::string how = upper(t[3]);
        if (how == "AT")
        {
            const std::string when = upper(t[4]);
            if (when != "TIME" && when != "CLOCKTIME")
            {
                c.err(ln, "AT must be followed by TIME or CLOCKTIME.");
                continue;
            }
            if (t.size() < 6 || !isTime(t[5]))
            {
                c.err(ln, "A time (hours or hh:mm) is expected after " + when + ".");
                continue;
            }
            std::size_t used = 6;
            if (t.size() > 6 && (text::iequals(t[6], "AM") || text::iequals(t[6], "PM")))
                used = 7;
            if (t.size() > used)
                c.err(ln, "Unexpected text after the control: \"" + joinFrom(t, used) + "\".");
        }
        else if (how == "IF")
        {
            if (t.size() < 8 || !text::iequals(t[4], "NODE"))
            {
                c.err(ln, "IF must be followed by NODE ID ABOVE/BELOW value.");
                continue;
            }
            c.checkId(ln, "NODE", t[5], true);
            if (!contains({"ABOVE", "BELOW"}, t[6]))
                c.err(ln, "\"" + t[6] + "\" must be ABOVE or BELOW.");
            if (!text::toDouble(t[7]))
                c.err(ln, "\"" + t[7] + "\" is not a number.");
            if (t.size() > 8)
                c.err(ln, "Unexpected text after the control: \"" + joinFrom(t, 8) + "\".");
        }
        else
        {
            c.err(ln, "\"" + t[3] + "\" must be AT or IF.");
        }
    }
    return std::move(c.messages);
}

int fileLineOf(const InpDocument& doc, const InpSection& section, int index)
{
    int line = static_cast<int>(doc.preamble.size());
    for (const auto& s : doc.sections)
    {
        ++line; // header
        if (&s == &section)
            return line + index + 1;
        line += static_cast<int>(s.lines.size());
    }
    return 0;
}

std::vector<ModelIssue> validateObjectSections(const Model& model)
{
    std::vector<ModelIssue> out;
    const auto& doc = model.document();
    auto add = [&](const std::vector<ObjectIssue>& issues, const std::string& what,
                   const char* section) {
        for (const auto& i : issues)
        {
            const bool err = i.severity == ObjectIssue::Severity::Error;
            out.push_back({err ? ModelIssue::Severity::Error : ModelIssue::Severity::Warning,
                           what + ": " + i.message, section, -1});
        }
    };
    auto rules = [&](const char* name, bool simple, RuleSyntax syntax) {
        const auto* s = doc.find(name);
        if (!s)
            return;
        const auto ctx = RuleContext::fromModel(model);
        const auto msgs = simple ? validateSimpleControls(s->lines, &ctx)
                                 : validateRules(s->lines, syntax, &ctx);
        for (const auto& m : msgs)
            out.push_back({m.error ? ModelIssue::Severity::Error : ModelIssue::Severity::Warning,
                           "[" + std::string(name) + "] line " +
                               std::to_string(fileLineOf(doc, *s, m.line - 1)) + ": " + m.message,
                           name, -1});
    };
    if (model.dialect() == Dialect::Epanet)
    {
        rules("CONTROLS", true, RuleSyntax::Epanet);
        rules("RULES", false, RuleSyntax::Epanet);
        return out;
    }
    if (model.dialect() != Dialect::Swmm)
        return out;
    for (const auto& name : lidControlNames(doc))
        add(validateLidControl(*readLidControl(doc, name), &doc), "LID control " + name,
            "LID_CONTROLS");
    for (const auto& sub : keyedObjectNames(doc, "LID_USAGE"))
        add(validateLidUsage(model, sub, readLidUsage(doc, sub)), "LID usage of " + sub,
            "LID_USAGE");
    for (const auto& t : readTransects(doc))
        add(validateTransect(t), "transect " + t.name, "TRANSECTS");
    for (const auto& name : unitHydrographNames(doc))
        add(validateUnitHydrograph(*readUnitHydrograph(doc, name), &model),
            "unit hydrographs " + name, "HYDROGRAPHS");
    rules("CONTROLS", false, RuleSyntax::Swmm);
    return out;
}

std::vector<std::string> ruleKeywords(RuleSyntax syntax)
{
    std::vector<std::string> k = {"RULE", "IF", "AND", "OR", "THEN", "ELSE", "PRIORITY"};
    if (syntax == RuleSyntax::Swmm)
    {
        k.push_back("VARIABLE");
        k.push_back("EXPRESSION");
    }
    return k;
}

std::vector<std::string> ruleObjectTypes(RuleSyntax syntax, bool action)
{
    if (syntax == RuleSyntax::Swmm)
    {
        if (action)
            return {"CONDUIT", "PUMP", "ORIFICE", "WEIR", "OUTLET"};
        return {"NODE", "LINK", "CONDUIT", "PUMP", "ORIFICE", "WEIR", "OUTLET", "GAGE",
                "SIMULATION"};
    }
    if (action)
        return {"LINK", "PIPE", "PUMP", "VALVE"};
    return {"NODE", "JUNCTION", "RESERVOIR", "TANK", "LINK", "PIPE", "PUMP", "VALVE", "SYSTEM"};
}

bool ruleObjectHasId(RuleSyntax syntax, std::string_view object)
{
    return !text::iequals(object, syntax == RuleSyntax::Swmm ? "SIMULATION" : "SYSTEM");
}

std::vector<std::string> ruleAttributes(RuleSyntax syntax, std::string_view object, bool action)
{
    const std::string o = upper(object);
    if (syntax == RuleSyntax::Swmm)
    {
        if (action)
        {
            if (o == "CONDUIT")
                return {"STATUS"};
            if (o == "PUMP")
                return {"STATUS", "SETTING"};
            if (o == "ORIFICE" || o == "WEIR" || o == "OUTLET")
                return {"SETTING"};
            return {};
        }
        if (o == "NODE")
            return {"DEPTH", "MAXDEPTH", "HEAD", "VOLUME", "INFLOW"};
        if (o == "LINK" || o == "CONDUIT")
            return {"FLOW",   "FULLFLOW", "DEPTH",    "FULLDEPTH", "VELOCITY",
                    "LENGTH", "SLOPE",    "STATUS",   "TIMEOPEN",  "TIMECLOSED"};
        if (o == "PUMP")
            return {"STATUS", "SETTING", "FLOW", "TIMEOPEN", "TIMECLOSED"};
        if (o == "ORIFICE" || o == "WEIR" || o == "OUTLET")
            return {"SETTING", "FLOW", "TIMEOPEN", "TIMECLOSED"};
        if (o == "GAGE")
            return {"INTENSITY", "1HR_PRECIP", "3HR_PRECIP", "6HR_PRECIP", "12HR_PRECIP",
                    "24HR_PRECIP"};
        if (o == "SIMULATION")
            return {"TIME", "DATE", "CLOCKTIME", "DAY", "MONTH", "DAYOFYEAR"};
        return {};
    }
    if (action)
        return contains({"LINK", "PIPE", "PUMP", "VALVE"}, o)
                   ? std::vector<std::string>{"STATUS", "SETTING"}
                   : std::vector<std::string>{};
    if (o == "NODE" || o == "JUNCTION" || o == "RESERVOIR")
        return {"DEMAND", "HEAD", "PRESSURE"};
    if (o == "TANK")
        return {"LEVEL", "HEAD", "PRESSURE", "DEMAND", "FILLTIME", "DRAINTIME"};
    if (o == "LINK" || o == "PIPE" || o == "PUMP" || o == "VALVE")
        return {"FLOW", "STATUS", "SETTING"};
    if (o == "SYSTEM")
        return {"DEMAND", "TIME", "CLOCKTIME"};
    return {};
}

std::vector<std::string> ruleRelations(RuleSyntax syntax)
{
    if (syntax == RuleSyntax::Swmm)
        return {"=", "<>", "<", "<=", ">", ">="};
    return {"=", "<>", "<", "<=", ">", ">=", "IS", "NOT", "BELOW", "ABOVE"};
}

std::vector<std::string> ruleValueWords(RuleSyntax syntax, std::string_view object,
                                        std::string_view attribute)
{
    if (!text::iequals(attribute, "STATUS"))
        return {};
    if (syntax == RuleSyntax::Epanet)
        return {"OPEN", "CLOSED", "ACTIVE"};
    const std::string o = upper(object);
    if (o == "PUMP")
        return {"ON", "OFF"};
    if (o == "CONDUIT")
        return {"OPEN", "CLOSED"};
    return {"ON", "OFF", "OPEN", "CLOSED"};
}

std::vector<std::string> ruleOtherWords(RuleSyntax syntax)
{
    if (syntax == RuleSyntax::Swmm)
        return {"CURVE", "TIMESERIES", "PID", "ON", "OFF", "OPEN", "CLOSED"};
    return {"IS", "NOT", "ABOVE", "BELOW", "OPEN", "CLOSED", "ACTIVE", "AM", "PM", "AT", "TIME",
            "CLOCKTIME"};
}

std::vector<std::string> ruleObjectIds(const RuleContext& context, RuleSyntax syntax,
                                       std::string_view object)
{
    const std::string o = upper(object);
    if (o == "GAGE")
        return context.gages();
    const std::string kind = expectedKind(syntax, o);
    if (isNodeObject(syntax, o))
        return context.nodes(kind);
    if (isLinkObject(syntax, o))
        return context.links(kind);
    return {};
}

std::string uniqueRuleName(const std::set<std::string>& usedNames, const std::string& base)
{
    std::set<std::string> used;
    for (const auto& n : usedNames)
        used.insert(upper(n));
    if (!used.count(upper(base)))
        return base;
    for (int i = 2;; ++i)
    {
        const std::string n = base + "_" + std::to_string(i);
        if (!used.count(upper(n)))
            return n;
    }
}

std::set<std::string> ruleNames(const std::vector<std::string>& lines)
{
    std::set<std::string> names;
    for (const auto& l : lines)
    {
        const auto t = text::splitInpLine(l).tokens;
        if (t.size() > 1 && text::iequals(t[0], "RULE"))
            names.insert(t[1]);
    }
    return names;
}

std::vector<RuleTemplate> ruleTemplates(RuleSyntax syntax, const RuleContext& ctx,
                                        const std::set<std::string>& usedNames)
{
    std::set<std::string> used = usedNames;
    auto name = [&](const std::string& base) {
        const std::string n = uniqueRuleName(used, base);
        used.insert(n);
        return n;
    };
    auto first = [](const std::vector<std::string>& v, const std::string& fallback) {
        return v.empty() ? fallback : v.front();
    };
    std::vector<RuleTemplate> out;
    if (syntax == RuleSyntax::Swmm)
    {
        const std::string well = first(ctx.nodes("Storage"), first(ctx.nodes(), "WetWell"));
        const std::string pump = first(ctx.links("Pump"), "Pump1");
        const std::string orifice = first(ctx.links("Orifice"), "Orifice1");
        const std::string weir = first(ctx.links("Weir"), "Weir1");
        const std::string conduit = first(ctx.links("Conduit"), first(ctx.links(), "Conduit1"));
        const std::string curve = first(ctx.curves(), "Curve1");
        {
            const std::string on = name("PumpOn"), off = name("PumpOff");
            out.push_back({"Pump on/off by wet well depth",
                           "Starts the pump above a high level and stops it below a low level.",
                           {},
                           {"RULE " + on, "IF NODE " + well + " DEPTH > 5",
                            "THEN PUMP " + pump + " STATUS = ON", "PRIORITY 1", "",
                            "RULE " + off, "IF NODE " + well + " DEPTH < 1",
                            "THEN PUMP " + pump + " STATUS = OFF", "PRIORITY 1"}});
        }
        {
            const std::string n = name("OrificeByTime");
            out.push_back({"Orifice opening by time of day",
                           "Half-closes an orifice during the day and opens it fully at night.",
                           {},
                           {"RULE " + n, "IF SIMULATION CLOCKTIME >= 08:00",
                            "AND SIMULATION CLOCKTIME < 18:00",
                            "THEN ORIFICE " + orifice + " SETTING = 0.5",
                            "ELSE ORIFICE " + orifice + " SETTING = 1"}});
        }
        {
            const std::string n = name("WeirByFlow");
            out.push_back({"Weir setting by flow",
                           "Raises the weir opening when the flow in a conduit is high.",
                           {},
                           {"RULE " + n, "IF LINK " + conduit + " FLOW > 10",
                            "THEN WEIR " + weir + " SETTING = 1",
                            "ELSE WEIR " + weir + " SETTING = 0.5"}});
        }
        {
            const std::string n = name("OrificeByCurve");
            out.push_back({"Orifice setting from a control curve",
                           "Sets the orifice opening from a control curve of the controlling "
                           "variable (the first variable of the rule's conditions).",
                           {},
                           {"RULE " + n, "IF NODE " + well + " DEPTH >= 0",
                            "THEN ORIFICE " + orifice + " SETTING = CURVE " + curve}});
        }
        {
            const std::string n = name("OrificePID");
            out.push_back({"Orifice setting by PID controller",
                           "Keeps a depth near a set point with a PID controller; the set point "
                           "is the value in the condition.",
                           {},
                           {"RULE " + n, "IF NODE " + well + " DEPTH <> 3",
                            "THEN ORIFICE " + orifice + " SETTING = PID 0.5 0.1 0"}});
        }
        {
            const std::string n = name("ByExpression");
            const std::string v = uniqueRuleName(used, "Depth1");
            out.push_back({"Named variable and expression",
                           "Defines a named variable and an expression and uses the expression "
                           "in a condition.",
                           {"VARIABLE " + v + " = NODE " + well + " DEPTH",
                            "EXPRESSION " + v + "_ft = " + v + " * 3.281"},
                           {"RULE " + n, "IF " + v + "_ft > 10",
                            "THEN PUMP " + pump + " STATUS = ON",
                            "ELSE PUMP " + pump + " STATUS = OFF"}});
        }
        {
            const std::string n = name("Rule");
            out.push_back({"Empty rule", "A rule skeleton to fill in.", {},
                           {"RULE " + n, "IF SIMULATION TIME > 0",
                            "THEN PUMP " + pump + " STATUS = ON"}});
        }
        return out;
    }
    const std::string tank = first(ctx.nodes("Tank"), first(ctx.nodes(), "Tank1"));
    const std::string pump = first(ctx.links("Pump"), "Pump1");
    const std::string valve = first(ctx.links("Valve"), first(ctx.links(), "Valve1"));
    {
        const std::string on = name("PumpOn"), off = name("PumpOff");
        out.push_back({"Pump on/off by tank level",
                       "Starts the pump when the tank is low and stops it when it is full.",
                       {},
                       {"RULE " + on, "IF TANK " + tank + " LEVEL BELOW 2",
                        "THEN PUMP " + pump + " STATUS IS OPEN", "PRIORITY 1", "",
                        "RULE " + off, "IF TANK " + tank + " LEVEL ABOVE 5",
                        "THEN PUMP " + pump + " STATUS IS CLOSED", "PRIORITY 1"}});
    }
    {
        const std::string n = name("ValveByTime");
        out.push_back({"Valve setting by time of day",
                       "Changes a valve setting during the day.",
                       {},
                       {"RULE " + n, "IF SYSTEM CLOCKTIME >= 7 AM",
                        "AND SYSTEM CLOCKTIME < 7 PM",
                        "THEN VALVE " + valve + " SETTING IS 40",
                        "ELSE VALVE " + valve + " SETTING IS 60"}});
    }
    {
        const std::string n = name("Rule");
        out.push_back({"Empty rule", "A rule skeleton to fill in.", {},
                       {"RULE " + n, "IF SYSTEM TIME > 0",
                        "THEN LINK " + valve + " STATUS IS OPEN"}});
    }
    return out;
}

} // namespace mirab::core
