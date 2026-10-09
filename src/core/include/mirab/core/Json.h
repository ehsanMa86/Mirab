/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Json.h
 * Purpose: Small JSON value type with an ordered object, parser and serializer.
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

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::json
{

class ParseError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// JSON value. Objects keep their keys in insertion order so that files
/// written by Mirab are stable and readable.
class Value
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int n) : type_(Type::Number), number_(n) {}
    Value(long n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(long long n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(unsigned n) : type_(Type::Number), number_(n) {}
    Value(unsigned long n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(unsigned long long n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    Value(std::string_view s) : type_(Type::String), string_(s) {}

    static Value array();
    static Value object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::string asString(const std::string& fallback = {}) const;

    // Arrays
    std::size_t size() const;
    const Value& operator[](std::size_t index) const;
    Value& operator[](std::size_t index);
    Value& push(Value v);
    const std::vector<Value>& items() const { return items_; }

    // Objects
    bool contains(std::string_view key) const;
    const Value* find(std::string_view key) const;
    Value* find(std::string_view key);
    /// Returns the member or a shared null value when missing.
    const Value& get(std::string_view key) const;
    /// Inserts a null member when missing (turns a null value into an object).
    Value& operator[](std::string_view key);
    Value& set(std::string_view key, Value v);
    const std::vector<std::string>& keys() const { return keys_; }

    std::string dump(int indent = -1) const;
    static Value parse(std::string_view text);

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<std::string> keys_; ///< object keys, parallel to items_
    std::vector<Value> items_;      ///< array items or object values
};

} // namespace mirab::json
