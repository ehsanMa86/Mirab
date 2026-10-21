/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Query.cpp
 * Purpose: Parser and evaluator of element queries, element fields and CSV tables of elements.
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
#include "mirab/core/Query.h"

#include "mirab/core/ModelJson.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <unordered_set>

namespace mirab::core
{

struct Query::Node
{
    enum class Kind
    {
        And,
        Or,
        Not,
        Compare
    } kind = Kind::Compare;
    std::vector<std::shared_ptr<const Node>> children;
    std::string field;
    std::string op; ///< "=", "!=", "<", "<=", ">", ">=", "contains"
    std::string value;
};

namespace
{
struct Token
{
    enum class Type
    {
        Word,
        Text, ///< quoted
        Op,
        Open,
        Close,
        End
    } type = Type::End;
    std::string text;
};

bool isWordChar(char c)
{
    return !std::isspace(static_cast<unsigned char>(c)) && c != '(' && c != ')' && c != '<' && c != '>' &&
           c != '=' && c != '!' && c != '"' && c != '\'';
}

bool tokenize(std::string_view s, std::vector<Token>& out, std::string* error)
{
    std::size_t i = 0;
    while (i < s.size())
    {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            ++i;
            continue;
        }
        if (c == '(' || c == ')')
        {
            out.push_back({c == '(' ? Token::Type::Open : Token::Type::Close, std::string(1, c)});
            ++i;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            const std::size_t end = s.find(c, i + 1);
            if (end == std::string_view::npos)
            {
                if (error)
                    *error = "a quote is not closed";
                return false;
            }
            out.push_back({Token::Type::Text, std::string(s.substr(i + 1, end - i - 1))});
            i = end + 1;
            continue;
        }
        if (c == '<' || c == '>' || c == '=' || c == '!')
        {
            std::string op(1, c);
            if (i + 1 < s.size() && (s[i + 1] == '=' || (c == '<' && s[i + 1] == '>')))
                op += s[i + 1];
            if (op == "!")
            {
                if (error)
                    *error = "\"!\" must be followed by \"=\"";
                return false;
            }
            i += op.size();
            if (op == "==")
                op = "=";
            else if (op == "<>")
                op = "!=";
            out.push_back({Token::Type::Op, op});
            continue;
        }
        std::size_t end = i;
        while (end < s.size() && isWordChar(s[end]))
            ++end;
        out.push_back({Token::Type::Word, std::string(s.substr(i, end - i))});
        i = end;
    }
    out.push_back({Token::Type::End, {}});
    return true;
}

class Parser
{
public:
    explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

    std::shared_ptr<const Query::Node> parse(std::string* error)
    {
        auto n = orExpr();
        if (n && peek().type != Token::Type::End)
            fail("unexpected \"" + peek().text + "\"");
        if (!error_.empty())
        {
            if (error)
                *error = error_;
            return nullptr;
        }
        return n;
    }

private:
    using NodePtr = std::shared_ptr<const Query::Node>;

    const Token& peek() const { return t_[pos_]; }
    Token next() { return t_[pos_ < t_.size() - 1 ? pos_++ : pos_]; }
    bool keyword(const char* k) const { return peek().type == Token::Type::Word && text::iequals(peek().text, k); }
    NodePtr fail(const std::string& message)
    {
        if (error_.empty())
            error_ = message;
        return nullptr;
    }

    NodePtr combine(Query::Node::Kind kind, NodePtr a, NodePtr b)
    {
        auto n = std::make_shared<Query::Node>();
        n->kind = kind;
        n->children = {std::move(a), std::move(b)};
        return n;
    }

    NodePtr orExpr()
    {
        NodePtr left = andExpr();
        while (left && keyword("OR"))
        {
            next();
            NodePtr right = andExpr();
            if (!right)
                return nullptr;
            left = combine(Query::Node::Kind::Or, left, right);
        }
        return left;
    }

    NodePtr andExpr()
    {
        NodePtr left = unary();
        while (left && keyword("AND"))
        {
            next();
            NodePtr right = unary();
            if (!right)
                return nullptr;
            left = combine(Query::Node::Kind::And, left, right);
        }
        return left;
    }

    NodePtr unary()
    {
        if (keyword("NOT"))
        {
            next();
            NodePtr inner = unary();
            if (!inner)
                return nullptr;
            auto n = std::make_shared<Query::Node>();
            n->kind = Query::Node::Kind::Not;
            n->children = {inner};
            return n;
        }
        if (peek().type == Token::Type::Open)
        {
            next();
            NodePtr inner = orExpr();
            if (!inner)
                return nullptr;
            if (peek().type != Token::Type::Close)
                return fail("a closing parenthesis is missing");
            next();
            return inner;
        }
        return comparison();
    }

    NodePtr comparison()
    {
        std::vector<std::string> name;
        while ((peek().type == Token::Type::Word && !keyword("CONTAINS") && !keyword("AND") && !keyword("OR")) ||
               (peek().type == Token::Type::Text && name.empty()))
            name.push_back(next().text);
        if (name.empty())
            return fail(peek().type == Token::Type::End ? "the expression ends too early"
                                                        : "a field name is expected before \"" + peek().text + "\"");
        std::string op;
        if (peek().type == Token::Type::Op)
            op = next().text;
        else if (keyword("CONTAINS"))
        {
            next();
            op = "contains";
        }
        else
            return fail("an operator (=, !=, <, <=, >, >= or contains) is expected after \"" + text::join(name, " ") +
                        "\"");
        std::vector<std::string> value;
        if (peek().type == Token::Type::Text)
            value.push_back(next().text);
        else
        {
            while (peek().type == Token::Type::Word && !keyword("AND") && !keyword("OR"))
                value.push_back(next().text);
        }
        if (value.empty())
            return fail("a value is expected after \"" + op + "\"");
        auto n = std::make_shared<Query::Node>();
        n->kind = Query::Node::Kind::Compare;
        n->field = text::join(name, " ");
        n->op = op;
        n->value = text::join(value, " ");
        return n;
    }

    std::vector<Token> t_;
    std::size_t pos_ = 0;
    std::string error_;
};

bool compare(const std::string& lhs, const std::string& op, const std::string& rhs)
{
    if (op == "contains")
        return text::toUpper(lhs).find(text::toUpper(rhs)) != std::string::npos;
    const auto a = text::toDouble(lhs);
    const auto b = text::toDouble(rhs);
    if (a && b)
    {
        const double tol = 1e-9 * std::max(1.0, std::fabs(*b));
        if (op == "=")
            return std::fabs(*a - *b) <= tol;
        if (op == "!=")
            return std::fabs(*a - *b) > tol;
        if (op == "<")
            return *a < *b;
        if (op == "<=")
            return *a <= *b + tol;
        if (op == ">")
            return *a > *b;
        return *a >= *b - tol;
    }
    const std::string x = text::toUpper(text::unquote(lhs)), y = text::toUpper(rhs);
    if (op == "=")
        return x == y;
    if (op == "!=")
        return x != y;
    if (op == "<")
        return x < y;
    if (op == "<=")
        return x <= y;
    if (op == ">")
        return x > y;
    return x >= y;
}

bool evaluate(const Query::Node& n,
              const std::function<std::optional<std::string>(const std::string&)>& field)
{
    switch (n.kind)
    {
    case Query::Node::Kind::And:
        return evaluate(*n.children[0], field) && evaluate(*n.children[1], field);
    case Query::Node::Kind::Or:
        return evaluate(*n.children[0], field) || evaluate(*n.children[1], field);
    case Query::Node::Kind::Not:
        return !evaluate(*n.children[0], field);
    case Query::Node::Kind::Compare:
        break;
    }
    const auto v = field(n.field);
    return v && compare(*v, n.op, n.value);
}

void collectFields(const Query::Node& n, std::vector<std::string>& out)
{
    if (n.kind == Query::Node::Kind::Compare)
    {
        if (std::find(out.begin(), out.end(), n.field) == out.end())
            out.push_back(n.field);
        return;
    }
    for (const auto& c : n.children)
        collectFields(*c, out);
}

const char* resultTableName(Category c)
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
        return nullptr;
    }
}

std::vector<std::string> idsOf(const Model& m, Category c)
{
    std::vector<std::string> ids;
    if (c == Category::Node)
        for (const auto& n : m.nodes())
            ids.push_back(n.id);
    else if (c == Category::Link)
        for (const auto& l : m.links())
            ids.push_back(l.id);
    else if (c == Category::Area)
        for (const auto& a : m.areas())
            ids.push_back(a.id);
    else if (c == Category::Gage)
        for (const auto& g : m.gages())
            ids.push_back(g.id);
    return ids;
}

std::string kindOf(const Model& m, const ElementRef& e)
{
    switch (e.category)
    {
    case Category::Node:
        if (const auto* n = m.findNode(e.id))
            return n->kind;
        break;
    case Category::Link:
        if (const auto* l = m.findLink(e.id))
            return l->kind;
        break;
    case Category::Area:
        return "Subcatchment";
    case Category::Gage:
        return "RainGage";
    case Category::None:
        break;
    }
    return {};
}

const char* categoryLabel(Category c)
{
    switch (c)
    {
    case Category::Node:
        return "Node";
    case Category::Link:
        return "Link";
    case Category::Area:
        return "Subcatchment";
    case Category::Gage:
        return "Rain gage";
    default:
        return "";
    }
}

std::string csvCell(const std::string& s)
{
    if (s.find_first_of(",\"\n") == std::string::npos)
        return s;
    return "\"" + text::replaceAll(s, "\"", "\"\"") + "\"";
}

constexpr const char* kResultPrefix = "result.";
} // namespace

Query::Query() = default;
Query::~Query() = default;
Query::Query(const Query& other) = default;
Query& Query::operator=(const Query& other) = default;

std::optional<Query> Query::parse(std::string_view textValue, std::string* error)
{
    Query q;
    if (text::trim(textValue).empty())
        return q;
    std::vector<Token> tokens;
    if (!tokenize(textValue, tokens, error))
        return std::nullopt;
    Parser p(std::move(tokens));
    q.root_ = p.parse(error);
    if (!q.root_)
        return std::nullopt;
    return q;
}

bool Query::empty() const
{
    return root_ == nullptr;
}

std::vector<std::string> Query::fields() const
{
    std::vector<std::string> out;
    if (root_)
        collectFields(*root_, out);
    return out;
}

bool Query::matches(const std::function<std::optional<std::string>(const std::string&)>& field) const
{
    return !root_ || evaluate(*root_, field);
}

std::optional<std::string> fieldValue(const ElementFields& fields, std::string_view nameIn)
{
    const std::string name = text::trim(nameIn);
    const bool kind = text::iequals(name, "Kind");
    auto find = [&](std::string_view n) -> std::optional<std::string> {
        for (const auto& [k, v] : fields)
        {
            if (text::iequals(k, n))
                return v;
        }
        // Names written as in scripts: "max depth" or "max_depth" for MaxDepth.
        const std::string key = columnKey(n);
        for (const auto& [k, v] : fields)
        {
            if (!text::istartsWith(k, kResultPrefix) && columnKey(k) == key)
                return v;
        }
        return std::nullopt;
    };
    if (kind)
        return find("Type");
    if (auto v = find(name))
        return v;
    if (!text::istartsWith(name, kResultPrefix))
        return find(kResultPrefix + name);
    return std::nullopt;
}

FieldTable::FieldTable(const Model& model, Category category, const ResultSet* results, int period)
    : model_(model), category_(category), results_(results), period_(period)
{
    rows_ = model.rowsForMany(category, idsOf(model, category));
}

ElementFields FieldTable::fields(const std::string& id) const
{
    ElementFields out;
    if (!model_.idInUse(category_, id))
        return out;
    out.emplace_back("ID", id);
    out.emplace_back("Type", kindOf(model_, {category_, id}));
    std::unordered_set<std::string> seen = {"ID", "TYPE"};
    if (const auto it = rows_.find(id); it != rows_.end())
    {
        for (const auto& r : it->second)
        {
            if (!r.def)
                continue;
            const RowLayout layout = r.def->layout(r.tokens);
            for (std::size_t c = 1; c < r.tokens.size(); ++c)
            {
                const ColumnDef* col = layout.column(c);
                if (!col)
                    break;
                const std::string name = layout.columnName(c);
                if (!seen.insert(text::toUpper(name)).second)
                    continue;
                std::string value = text::unquote(r.tokens[c]);
                if (layout.variadic && c + 1 == layout.columns.size())
                {
                    std::vector<std::string> rest(r.tokens.begin() + static_cast<std::ptrdiff_t>(c), r.tokens.end());
                    value = text::join(rest, " ");
                    out.emplace_back(name, value);
                    break;
                }
                out.emplace_back(name, value);
            }
        }
    }
    const char* table = resultTableName(category_);
    if (results_ && table && period_ >= 0 && period_ < results_->periodCount())
    {
        if (const auto* t = results_->table(table))
        {
            const int e = t->elementIndex(id);
            if (e >= 0)
            {
                for (int v = 0; v < t->variableCount(); ++v)
                {
                    const double x = t->value(period_, v, e);
                    if (!std::isnan(x))
                        out.emplace_back(kResultPrefix + t->variables[static_cast<std::size_t>(v)].name,
                                         text::formatDouble(x, 7));
                }
            }
        }
    }
    return out;
}

std::vector<std::string> FieldTable::fieldNames() const
{
    std::vector<std::string> names = {"ID", "Type"};
    std::set<std::string> seen = {"ID", "TYPE"};
    auto add = [&](const std::string& n) {
        if (seen.insert(text::toUpper(n)).second)
            names.push_back(n);
    };
    for (const auto& def : sectionDefs(model_.dialect()))
    {
        if (def.keyCategory != category_ || def.name == "COORDINATES" || def.name == "VERTICES" ||
            def.name == "POLYGONS" || def.name == "SYMBOLS")
            continue;
        // Only sections the model uses, so the list stays short.
        if (!model_.document().find(def.name))
            continue;
        for (std::size_t c = 1; c < def.columns.size(); ++c)
            add(def.columns[c].name);
    }
    const char* table = resultTableName(category_);
    if (results_ && table)
    {
        // A result with the name of a property is read with the "result." prefix.
        if (const auto* t = results_->table(table))
            for (const auto& v : t->variables)
                add(seen.count(text::toUpper(v.name)) ? kResultPrefix + v.name : v.name);
    }
    return names;
}

std::vector<ElementRef> selectByQuery(const Model& model, const Query& query, Category category,
                                      const std::string& kind, const ResultSet* results, int period)
{
    std::vector<ElementRef> out;
    const std::vector<Category> cats = category == Category::None
                                           ? std::vector<Category>{Category::Node, Category::Link, Category::Area,
                                                                   Category::Gage}
                                           : std::vector<Category>{category};
    for (Category c : cats)
    {
        const FieldTable table(model, c, results, period);
        for (const auto& id : idsOf(model, c))
        {
            if (!kind.empty() && !text::iequals(kindOf(model, {c, id}), kind))
                continue;
            const ElementFields f = table.fields(id);
            if (query.matches([&](const std::string& name) { return fieldValue(f, name); }))
                out.push_back({c, id});
        }
    }
    return out;
}

std::string elementsCsv(const Model& model, const std::vector<ElementRef>& elements, const ResultSet* results,
                        int period)
{
    std::map<Category, std::unique_ptr<FieldTable>> tables;
    std::vector<std::string> columns;
    std::map<std::string, std::size_t> columnIndex; // upper-case name -> column
    struct Row
    {
        std::string category, kind, id;
        std::vector<std::pair<std::size_t, std::string>> cells;
    };
    std::vector<Row> rows;
    for (const auto& e : elements)
    {
        auto& t = tables[e.category];
        if (!t)
            t = std::make_unique<FieldTable>(model, e.category, results, period);
        const ElementFields f = t->fields(e.id);
        if (f.empty())
            continue;
        Row row{categoryLabel(e.category), kindOf(model, e), e.id, {}};
        for (const auto& [name, value] : f)
        {
            if (name == "ID" || name == "Type")
                continue;
            const std::string key = text::toUpper(name);
            auto it = columnIndex.find(key);
            if (it == columnIndex.end())
            {
                it = columnIndex.emplace(key, columns.size()).first;
                columns.push_back(name);
            }
            row.cells.emplace_back(it->second, value);
        }
        rows.push_back(std::move(row));
    }
    // Result columns after the properties, labelled with their units.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < columns.size(); ++i)
        if (!text::istartsWith(columns[i], kResultPrefix))
            order.push_back(i);
    for (std::size_t i = 0; i < columns.size(); ++i)
        if (text::istartsWith(columns[i], kResultPrefix))
            order.push_back(i);
    auto header = [&](const std::string& name) {
        if (!text::istartsWith(name, kResultPrefix))
            return name;
        const std::string var = name.substr(std::string(kResultPrefix).size());
        if (results)
        {
            for (const char* tn : {"node", "link", "subcatchment"})
            {
                if (const auto* t = results->table(tn))
                {
                    const int v = t->variableIndex(var);
                    if (v >= 0 && !t->variables[static_cast<std::size_t>(v)].unit.empty())
                        return var + " (" + t->variables[static_cast<std::size_t>(v)].unit + ")";
                }
            }
        }
        return var;
    };
    std::string out = "Category,Type,ID";
    for (std::size_t i : order)
        out += "," + csvCell(header(columns[i]));
    out += "\n";
    for (const auto& row : rows)
    {
        std::vector<std::string> cells(columns.size());
        for (const auto& [col, value] : row.cells)
            cells[col] = value;
        out += csvCell(row.category) + "," + csvCell(row.kind) + "," + csvCell(row.id);
        for (std::size_t i : order)
            out += "," + csvCell(cells[i]);
        out += "\n";
    }
    return out;
}

} // namespace mirab::core
