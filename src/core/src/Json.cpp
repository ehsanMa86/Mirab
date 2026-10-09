/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/src/Json.cpp
 * Purpose: JSON parser and serializer used for results headers, settings and interchange files.
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
#include "anahita/core/Json.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>

namespace anahita::json
{

namespace
{
const Value& nullValue()
{
    static const Value v;
    return v;
}

void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void escapeString(std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (c < 0x20)
            {
                static const char* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0xF];
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

std::string formatNumber(double v)
{
    if (!std::isfinite(v))
        return "null";
    std::ostringstream o;
    o.imbue(std::locale::classic());
    if (std::fabs(v) < 1e15 && v == std::floor(v))
        o << static_cast<long long>(v);
    else
        o << std::setprecision(15) << v;
    return o.str();
}

class Parser
{
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Value parseDocument()
    {
        skipWs();
        Value v = parseValue(0);
        skipWs();
        if (pos_ != s_.size())
            fail("unexpected trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const
    {
        throw ParseError("JSON parse error at offset " + std::to_string(pos_) + ": " + what);
    }

    void skipWs()
    {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t'))
            ++pos_;
    }

    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }

    void expect(char c)
    {
        if (peek() != c)
            fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool consume(std::string_view word)
    {
        if (s_.substr(pos_, word.size()) == word)
        {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    Value parseValue(int depth)
    {
        if (depth > 512)
            fail("nesting too deep");
        const char c = peek();
        if (c == '{')
            return parseObject(depth);
        if (c == '[')
            return parseArray(depth);
        if (c == '"')
            return Value(parseString());
        if (consume("true"))
            return Value(true);
        if (consume("false"))
            return Value(false);
        if (consume("null"))
            return Value();
        if (consume("NaN"))
            return Value();
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();
        fail("unexpected character");
    }

    Value parseObject(int depth)
    {
        expect('{');
        Value obj = Value::object();
        skipWs();
        if (peek() == '}')
        {
            ++pos_;
            return obj;
        }
        for (;;)
        {
            skipWs();
            std::string key = parseString();
            skipWs();
            expect(':');
            skipWs();
            obj.set(key, parseValue(depth + 1));
            skipWs();
            if (peek() == ',')
            {
                ++pos_;
                continue;
            }
            expect('}');
            return obj;
        }
    }

    Value parseArray(int depth)
    {
        expect('[');
        Value arr = Value::array();
        skipWs();
        if (peek() == ']')
        {
            ++pos_;
            return arr;
        }
        for (;;)
        {
            skipWs();
            arr.push(parseValue(depth + 1));
            skipWs();
            if (peek() == ',')
            {
                ++pos_;
                continue;
            }
            expect(']');
            return arr;
        }
    }

    std::uint32_t parseHex4()
    {
        if (pos_ + 4 > s_.size())
            fail("truncated unicode escape");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                fail("bad unicode escape");
        }
        return v;
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_++];
            if (c == '"')
                return out;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= s_.size())
                break;
            const char e = s_[pos_++];
            switch (e)
            {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                std::uint32_t cp = parseHex4();
                if (cp >= 0xD800 && cp <= 0xDBFF && consume("\\u"))
                {
                    const std::uint32_t lo = parseHex4();
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                fail("bad escape");
            }
        }
        fail("unterminated string");
    }

    Value parseNumber()
    {
        const std::size_t start = pos_;
        if (peek() == '-')
            ++pos_;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                ++pos_;
            else
                break;
        }
        std::istringstream in(std::string(s_.substr(start, pos_ - start)));
        in.imbue(std::locale::classic());
        double v = 0;
        in >> v;
        if (in.fail())
            fail("bad number");
        return Value(v);
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};
} // namespace

Value Value::array()
{
    Value v;
    v.type_ = Type::Array;
    return v;
}

Value Value::object()
{
    Value v;
    v.type_ = Type::Object;
    return v;
}

bool Value::asBool(bool fallback) const
{
    if (type_ == Type::Bool)
        return bool_;
    if (type_ == Type::Number)
        return number_ != 0.0;
    return fallback;
}

double Value::asNumber(double fallback) const
{
    if (type_ == Type::Number)
        return number_;
    if (type_ == Type::Bool)
        return bool_ ? 1.0 : 0.0;
    return fallback;
}

std::string Value::asString(const std::string& fallback) const
{
    if (type_ == Type::String)
        return string_;
    if (type_ == Type::Number)
        return formatNumber(number_);
    if (type_ == Type::Bool)
        return bool_ ? "true" : "false";
    return fallback;
}

std::size_t Value::size() const
{
    return (type_ == Type::Array || type_ == Type::Object) ? items_.size() : 0;
}

const Value& Value::operator[](std::size_t index) const
{
    if (index >= items_.size())
        return nullValue();
    return items_[index];
}

Value& Value::operator[](std::size_t index)
{
    if (index >= items_.size())
        throw std::out_of_range("JSON array index out of range");
    return items_[index];
}

Value& Value::push(Value v)
{
    if (type_ == Type::Null)
        type_ = Type::Array;
    if (type_ != Type::Array)
        throw std::logic_error("JSON value is not an array");
    items_.push_back(std::move(v));
    return items_.back();
}

bool Value::contains(std::string_view key) const
{
    return find(key) != nullptr;
}

const Value* Value::find(std::string_view key) const
{
    if (type_ != Type::Object)
        return nullptr;
    for (std::size_t i = 0; i < keys_.size(); ++i)
    {
        if (keys_[i] == key)
            return &items_[i];
    }
    return nullptr;
}

Value* Value::find(std::string_view key)
{
    return const_cast<Value*>(static_cast<const Value*>(this)->find(key));
}

const Value& Value::get(std::string_view key) const
{
    const Value* v = find(key);
    return v ? *v : nullValue();
}

Value& Value::operator[](std::string_view key)
{
    if (type_ == Type::Null)
        type_ = Type::Object;
    if (type_ != Type::Object)
        throw std::logic_error("JSON value is not an object");
    if (Value* v = find(key))
        return *v;
    keys_.emplace_back(key);
    items_.emplace_back();
    return items_.back();
}

Value& Value::set(std::string_view key, Value v)
{
    Value& slot = (*this)[key];
    slot = std::move(v);
    return slot;
}

std::string Value::dump(int indent) const
{
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

void Value::dumpTo(std::string& out, int indent, int depth) const
{
    auto newline = [&](int d) {
        if (indent < 0)
            return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent * d), ' ');
    };
    switch (type_)
    {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Type::Number:
        out += formatNumber(number_);
        break;
    case Type::String:
        escapeString(out, string_);
        break;
    case Type::Array:
        out += '[';
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            if (i)
                out += ',';
            newline(depth + 1);
            items_[i].dumpTo(out, indent, depth + 1);
        }
        if (!items_.empty())
            newline(depth);
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            if (i)
                out += ',';
            newline(depth + 1);
            escapeString(out, keys_[i]);
            out += indent < 0 ? ":" : ": ";
            items_[i].dumpTo(out, indent, depth + 1);
        }
        if (!items_.empty())
            newline(depth);
        out += '}';
        break;
    }
}

Value Value::parse(std::string_view text)
{
    // Skip a UTF-8 byte order mark if present.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    return Parser(text).parseDocument();
}

} // namespace anahita::json
