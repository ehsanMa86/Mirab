/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/src/InpDocument.cpp
 * Purpose: Reader and writer for the section-preserving input file document.
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
#include "anahita/core/InpDocument.h"

#include "anahita/core/Text.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace anahita::core
{

using text::splitInpLine;
using text::toUpper;
using text::trim;

bool InpSection::isDataLine(std::string_view line)
{
    const std::string t = trim(line);
    return !t.empty() && t.front() != ';';
}

std::vector<int> InpSection::dataLineIndices() const
{
    std::vector<int> idx;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        if (isDataLine(lines[static_cast<std::size_t>(i)]))
            idx.push_back(i);
    }
    return idx;
}

int InpSection::rowCount() const
{
    int n = 0;
    for (const auto& l : lines)
    {
        if (isDataLine(l))
            ++n;
    }
    return n;
}

int InpSection::lineIndexOfRow(int row) const
{
    if (row < 0)
        return -1;
    int n = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        if (isDataLine(lines[static_cast<std::size_t>(i)]))
        {
            if (n == row)
                return i;
            ++n;
        }
    }
    return -1;
}

std::vector<std::string> InpSection::rowTokens(int row) const
{
    const int li = lineIndexOfRow(row);
    if (li < 0)
        return {};
    return splitInpLine(lines[static_cast<std::size_t>(li)]).tokens;
}

std::string InpSection::rowComment(int row) const
{
    const int li = lineIndexOfRow(row);
    if (li < 0)
        return {};
    return splitInpLine(lines[static_cast<std::size_t>(li)]).comment;
}

std::string InpSection::formatRow(const std::vector<std::string>& tokens,
                                  const std::string& comment)
{
    std::string out = " ";
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        std::string t = tokens[i];
        if (i + 1 < tokens.size() && t.size() < 16)
            t.append(16 - t.size(), ' ');
        else if (i + 1 < tokens.size())
            t += ' ';
        out += t;
    }
    if (!comment.empty())
        out += "\t;" + comment;
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

void InpSection::setRowTokens(int row, const std::vector<std::string>& tokens)
{
    const int li = lineIndexOfRow(row);
    if (li < 0)
        throw std::out_of_range("row " + std::to_string(row) + " not found in [" + name + "]");
    const auto split = splitInpLine(lines[static_cast<std::size_t>(li)]);
    lines[static_cast<std::size_t>(li)] = formatRow(tokens, split.comment);
}

int InpSection::appendRow(const std::vector<std::string>& tokens, const std::string& comment)
{
    // Insert after the last non-blank line so trailing spacing is preserved.
    std::size_t insertAt = lines.size();
    while (insertAt > 0 && trim(lines[insertAt - 1]).empty())
        --insertAt;
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), formatRow(tokens, comment));
    int row = -1;
    for (std::size_t i = 0; i <= insertAt; ++i)
    {
        if (isDataLine(lines[i]))
            ++row;
    }
    return row;
}

void InpSection::removeRow(int row)
{
    const int li = lineIndexOfRow(row);
    if (li < 0)
        return;
    lines.erase(lines.begin() + li);
}

std::string InpDocument::canonicalSectionName(std::string_view headerLine)
{
    std::string t = trim(headerLine);
    const auto close = t.find(']');
    if (t.empty() || t.front() != '[' || close == std::string::npos)
        return {};
    return toUpper(trim(t.substr(1, close - 1)));
}

InpDocument InpDocument::parse(std::string_view text)
{
    InpDocument doc;
    // Skip a UTF-8 byte order mark.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);

    InpSection* current = nullptr;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string line(text.substr(start, end - start));
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const bool lastLine = end >= text.size();

        const std::string name = canonicalSectionName(line);
        if (!name.empty())
        {
            InpSection s;
            s.name = name;
            s.header = trim(line);
            doc.sections.push_back(std::move(s));
            current = &doc.sections.back();
        }
        else if (!(lastLine && line.empty()))
        {
            if (current)
                current->lines.push_back(line);
            else
                doc.preamble.push_back(line);
        }
        if (lastLine)
            break;
        start = end + 1;
    }
    return doc;
}

InpDocument InpDocument::load(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.u8string());
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse(buf.str());
}

std::string InpDocument::toString() const
{
    std::string out;
    for (const auto& l : preamble)
        out += l + "\n";
    for (const auto& s : sections)
    {
        out += (s.header.empty() ? "[" + s.name + "]" : s.header) + "\n";
        for (const auto& l : s.lines)
            out += l + "\n";
    }
    return out;
}

void InpDocument::save(const std::filesystem::path& path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + path.u8string());
    const std::string s = toString();
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!out)
        throw std::runtime_error("error while writing " + path.u8string());
}

InpSection* InpDocument::find(std::string_view name)
{
    const std::string n = toUpper(name);
    for (auto& s : sections)
    {
        if (s.name == n)
            return &s;
    }
    return nullptr;
}

const InpSection* InpDocument::find(std::string_view name) const
{
    return const_cast<InpDocument*>(this)->find(name);
}

InpSection& InpDocument::ensure(std::string_view name)
{
    if (InpSection* s = find(name))
        return *s;
    InpSection s;
    s.name = toUpper(name);
    s.header = "[" + s.name + "]";
    s.lines.push_back("");
    auto it = sections.end();
    for (auto i = sections.begin(); i != sections.end(); ++i)
    {
        if (i->name == "END")
        {
            it = i;
            break;
        }
    }
    // Keep a blank line between the previous section and the new one.
    if (it != sections.begin())
    {
        auto prev = std::prev(it);
        if (prev->lines.empty() || !trim(prev->lines.back()).empty())
            prev->lines.push_back("");
    }
    return *sections.insert(it, std::move(s));
}

bool InpDocument::remove(std::string_view name)
{
    const std::string n = toUpper(name);
    for (auto it = sections.begin(); it != sections.end(); ++it)
    {
        if (it->name == n)
        {
            sections.erase(it);
            return true;
        }
    }
    return false;
}

namespace
{
std::vector<std::string> keyWords(std::string_view key)
{
    return text::splitInpLine(toUpper(key)).tokens;
}

// Multi-word keys that start with a word that is also a single-word key.
bool isLongerKnownKey(const std::vector<std::string>& tokens, std::size_t keyLen)
{
    static const char* longer[][2] = {
        {"PRESSURE", "EXPONENT"}, {"DEMAND", "MODEL"},       {"DEMAND", "MULTIPLIER"},
        {"DEMAND", "PATTERN"},    {"DEMAND", "CHARGE"},      {"EMITTER", "EXPONENT"},
        {"EMITTER", "BACKFLOW"},  {"SPECIFIC", "GRAVITY"},   {"MINIMUM", "PRESSURE"},
        {"REQUIRED", "PRESSURE"}, {"QUALITY", "TIMESTEP"},   {"PATTERN", "TIMESTEP"},
        {"PATTERN", "START"},     {"REPORT", "TIMESTEP"},    {"REPORT", "START"},
        {"HYDRAULIC", "TIMESTEP"}, {"RULE", "TIMESTEP"},     {"START", "CLOCKTIME"},
        {"STATUS", "REPORT"},     {"FLOW", "PACED"}};
    if (keyLen != 1 || tokens.size() < 2)
        return false;
    const std::string a = toUpper(tokens[0]);
    const std::string b = toUpper(tokens[1]);
    for (const auto& pair : longer)
    {
        if (a == pair[0] && b == pair[1])
            return true;
    }
    return false;
}

bool rowMatchesKey(const std::vector<std::string>& tokens, const std::vector<std::string>& key)
{
    if (tokens.size() < key.size())
        return false;
    for (std::size_t i = 0; i < key.size(); ++i)
    {
        if (toUpper(tokens[i]) != key[i])
            return false;
    }
    return !isLongerKnownKey(tokens, key.size());
}
} // namespace

std::optional<std::string> InpDocument::keyValue(std::string_view section,
                                                 std::string_view key) const
{
    const InpSection* s = find(section);
    if (!s)
        return std::nullopt;
    const auto kw = keyWords(key);
    const int rows = s->rowCount();
    for (int r = 0; r < rows; ++r)
    {
        const auto tokens = s->rowTokens(r);
        if (rowMatchesKey(tokens, kw))
        {
            std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(kw.size()),
                                          tokens.end());
            return text::join(rest, " ");
        }
    }
    return std::nullopt;
}

void InpDocument::setKeyValue(std::string_view section, std::string_view key,
                              const std::string& value)
{
    InpSection& s = ensure(section);
    const auto kw = keyWords(key);
    std::vector<std::string> tokens = kw;
    for (auto& v : text::splitInpLine(value).tokens)
        tokens.push_back(v);
    const int rows = s.rowCount();
    for (int r = 0; r < rows; ++r)
    {
        if (rowMatchesKey(s.rowTokens(r), kw))
        {
            s.setRowTokens(r, tokens);
            return;
        }
    }
    s.appendRow(tokens);
}

bool InpDocument::removeKeyValue(std::string_view section, std::string_view key)
{
    InpSection* s = find(section);
    if (!s)
        return false;
    const auto kw = keyWords(key);
    bool removed = false;
    for (int r = s->rowCount() - 1; r >= 0; --r)
    {
        if (rowMatchesKey(s->rowTokens(r), kw))
        {
            s->removeRow(r);
            removed = true;
        }
    }
    return removed;
}

} // namespace anahita::core
