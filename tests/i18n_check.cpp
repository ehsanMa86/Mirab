/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/i18n_check.cpp
 * Purpose: Checks of the translations: the list of library strings and the .ts files.
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
//
// mirab-i18n-check core-strings <CoreStrings.cpp> [--write]
//     Compares the library strings the interface translates (section
//     descriptions, column help, element kinds, LID parameters, rule
//     templates, engine descriptions, ...) with the list lupdate reads.
//     --write rewrites the list.
// mirab-i18n-check ts <file.ts>...
//     Each file must parse, carry no obsolete or vanished messages, and keep
//     the placeholders (%1, %2, %n, ...) of every source text. Right-to-left
//     translations that start with a Latin word, a number or a placeholder
//     begin with U+200F RIGHT-TO-LEFT MARK, and have no mnemonic "&": the
//     underlined letter is drawn on its own, which breaks the joined letters
//     of Persian and Arabic words.

#include "mirab/core/ControlRules.h"
#include "mirab/core/Features.h"
#include "mirab/core/Schema.h"
#include "mirab/core/SwmmObjects.h"
#include "mirab/engine/EngineRegistry.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace mirab;

namespace
{

// Same words as columnLabel() in src/app/SchemaWidgets.cpp: "SweepInterval" -> "Sweep interval".
std::string splitName(const std::string& n)
{
    std::string out;
    for (std::size_t i = 0; i < n.size(); ++i)
    {
        const char c = n[i];
        const bool upper = c >= 'A' && c <= 'Z';
        if (i > 0 && upper && n[i - 1] >= 'a' && n[i - 1] <= 'z')
        {
            out += ' ';
            out += static_cast<char>(c - 'A' + 'a');
        }
        else
        {
            out += c;
        }
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

/// The texts of the calls matched by @p call whose argument is made of string
/// literals only (adjacent literals form one text; a ?: choice gives one text
/// per branch). Arguments that join literals with other values are skipped.
std::vector<std::string> literalArguments(const QString& source, const QString& call)
{
    std::vector<std::string> out;
    const QRegularExpression re(call);
    for (auto it = re.globalMatch(source); it.hasNext();)
    {
        int i = static_cast<int>(it.next().capturedEnd());
        int depth = 1;
        bool literalOnly = true;
        QStringList texts;
        QString current;
        bool inGroup = false;
        while (i < source.size() && depth > 0)
        {
            const QChar c = source[i];
            if (c == '"')
            {
                QString lit;
                for (++i; i < source.size() && source[i] != '"'; ++i)
                {
                    if (source[i] == '\\' && i + 1 < source.size())
                        ++i;
                    lit += source[i];
                }
                current += lit;
                inGroup = true;
            }
            else if (c == '(')
                ++depth;
            else if (c == ')')
                --depth;
            else if (c == '?' || c == ':')
            {
                if (inGroup)
                    texts << current;
                current.clear();
                inGroup = false;
            }
            else if (!c.isSpace() && depth == 1 && c != ',')
            {
                // Identifiers of a ?: condition are fine before the first literal.
                if (c == '+' || inGroup)
                    literalOnly = false;
            }
            ++i;
        }
        if (inGroup)
            texts << current;
        if (literalOnly)
            for (const QString& t : texts)
                out.push_back(t.toStdString());
    }
    return out;
}

std::vector<std::string> coreStrings()
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& s) {
        if (!s.empty() && seen.insert(s).second)
            out.push_back(s);
    };
    auto addColumn = [&](const core::ColumnDef& c) {
        add(c.help);
        if (c.help.empty() || c.help.size() > 48)
            add(splitName(c.name));
    };
    // Element kinds first, then the sections of each dialect.
    for (auto d : {core::Dialect::Epanet, core::Dialect::Swmm})
        for (auto cat : {core::Category::Node, core::Category::Link})
            for (const auto& k : core::elementKinds(d, cat))
                add(k);
    for (const char* k : {"Subcatchment", "Rain gage", "Node", "Link"})
        add(k);
    for (auto d : {core::Dialect::Epanet, core::Dialect::Swmm, core::Dialect::Msx})
    {
        for (const auto& s : core::sectionDefs(d))
        {
            add(s.description);
            for (const auto& c : s.columns)
                addColumn(c);
            for (const auto& v : s.variants)
                for (const auto& c : v.columns)
                    addColumn(c);
        }
    }
    for (auto layer : {core::LidLayer::Surface, core::LidLayer::Soil, core::LidLayer::Pavement,
                       core::LidLayer::Storage, core::LidLayer::Drain, core::LidLayer::DrainMat,
                       core::LidLayer::Removals})
    {
        add(core::lidLayerName(layer));
        for (const auto& p : core::lidLayerParams(layer))
        {
            add(p.label);
            add(p.help);
        }
    }
    for (const auto& t : core::lidTypes())
    {
        add(t.name);
        add(t.help);
    }
    for (auto syntax : {core::RuleSyntax::Swmm, core::RuleSyntax::Epanet})
    {
        for (const auto& t : core::ruleTemplates(syntax, core::RuleContext{}, {}))
        {
            add(t.title);
            add(t.description);
        }
    }
    for (int f = 0; f <= static_cast<int>(core::Feature::SwmmControls); ++f)
        add(core::featureName(static_cast<core::Feature>(f)));
    for (const auto& e : engine::builtinDescriptors())
        add(e.description);
    // Names of the key figures of scenario comparisons (core::resultSummary()).
    QFile scenario(QStringLiteral(MIRAB_SOURCE_DIR "/src/core/src/Scenario.cpp"));
    if (scenario.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        static const QRegularExpression figure(R"re(out\.push_back\(\{"([^"]+)",)re");
        for (auto it = figure.globalMatch(QString::fromUtf8(scenario.readAll())); it.hasNext();)
            add(it.next().captured(1).toStdString());
    }
    // Fixed notes of the wastewater link (core::matchWaterNodes(), core::computeDwfPlan()).
    QFile linkage(QStringLiteral(MIRAB_SOURCE_DIR "/src/core/src/Linkage.cpp"));
    if (linkage.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        for (const auto& t : literalArguments(QString::fromUtf8(linkage.readAll()),
                                              QStringLiteral(R"re(\bnote\(|notes\.push_back\()re")))
            add(t);
    }
    return out;
}

QString cEscape(const std::string& s)
{
    QString out;
    for (const QChar c : QString::fromStdString(s))
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

int checkCoreStrings(const QString& path, bool write)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        std::fprintf(stderr, "cannot read %s\n", qPrintable(path));
        return 1;
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    const std::vector<std::string> expected = coreStrings();

    if (write)
    {
        const int body = text.indexOf("namespace mirab::app");
        QString out = text.left(body < 0 ? text.size() : body);
        out += "namespace mirab::app\n{\n\nconst char* const kCoreStrings[] = {\n";
        for (const auto& s : expected)
            out += "    QT_TRANSLATE_NOOP(\"core\", \"" + cEscape(s) + "\"),\n";
        out += "};\n\nconst std::size_t kCoreStringCount = sizeof(kCoreStrings) / "
               "sizeof(kCoreStrings[0]);\n\n} // namespace mirab::app\n";
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(path));
            return 1;
        }
        f.write(out.toUtf8());
        std::printf("%zu strings written to %s\n", expected.size(), qPrintable(path));
        return 0;
    }

    static const QRegularExpression entry(
        R"re(QT_TRANSLATE_NOOP\("core",\s*"((?:[^"\\]|\\.)*)"\))re");
    std::vector<std::string> listed;
    for (auto it = entry.globalMatch(text); it.hasNext();)
    {
        QString s = it.next().captured(1);
        s.replace("\\\"", "\"").replace("\\\\", "\\");
        listed.push_back(s.toStdString());
    }
    const std::set<std::string> have(listed.begin(), listed.end());
    const std::set<std::string> want(expected.begin(), expected.end());
    int problems = 0;
    for (const auto& s : expected)
        if (!have.count(s))
        {
            std::printf("missing: %s\n", s.c_str());
            ++problems;
        }
    for (const auto& s : listed)
        if (!want.count(s))
        {
            std::printf("no longer used: %s\n", s.c_str());
            ++problems;
        }
    if (problems)
        std::printf("%d difference(s); run the update_core_strings target (or mirab-i18n-check "
                    "core-strings %s --write), then update_translations\n",
                    problems, qPrintable(path));
    else
        std::printf("%zu library strings listed\n", expected.size());
    return problems ? 1 : 0;
}

/// Placeholders of a text with their counts: "%1" -> 2, "%n" -> 1, ...
QMap<QString, int> placeholders(const QString& s)
{
    static const QRegularExpression re(R"(%L?(\d{1,2}|n))");
    QMap<QString, int> out;
    for (auto it = re.globalMatch(s); it.hasNext();)
    {
        QString p = it.next().captured(0);
        p.remove('L');
        ++out[p];
    }
    return out;
}

bool isRtlLetter(QChar c)
{
    const auto d = c.direction();
    return d == QChar::DirR || d == QChar::DirAL;
}

bool containsRtlLetter(const QString& s)
{
    return std::any_of(s.begin(), s.end(), isRtlLetter);
}

/// True when the first character is not a right-to-left letter (a Latin word,
/// a digit, a placeholder, ...) apart from leading spaces and punctuation that
/// joins fragments.
bool startsWithoutRtlLetter(const QString& s)
{
    for (const QChar c : s)
    {
        if (isRtlLetter(c))
            return false;
        if (c.isLetterOrNumber() || c == '%')
            return true;
    }
    return false;
}

int checkTs(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        std::printf("%s: cannot be read\n", qPrintable(path));
        return 1;
    }
    const QString base = QFileInfo(path).completeBaseName(); // mirab_fa
    const QString lang = base.section('_', 1);
    const bool rtl = lang == "fa" || lang == "ar" || lang == "he" || lang == "ur";
    QXmlStreamReader xml(&f);
    int problems = 0, messages = 0, finished = 0;
    QString context, source, type;
    QStringList forms;
    bool numerus = false;
    auto report = [&](const QString& what) {
        ++problems;
        std::printf("%s: [%s] \"%s\": %s\n", qPrintable(QFileInfo(path).fileName()),
                    qPrintable(context),
                    qPrintable(source.left(80)), qPrintable(what));
    };
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement())
        {
            const auto name = xml.name();
            if (name == QLatin1String("TS"))
            {
                const QString l = xml.attributes().value("language").toString();
                if (l != lang)
                {
                    ++problems;
                    std::printf("%s: language \"%s\" does not match the file name\n",
                                qPrintable(path),
                                qPrintable(l));
                }
            }
            else if (name == QLatin1String("name"))
                context = xml.readElementText();
            else if (name == QLatin1String("message"))
            {
                numerus = xml.attributes().value("numerus") == QLatin1String("yes");
                source.clear();
                type.clear();
                forms.clear();
            }
            else if (name == QLatin1String("source"))
                source = xml.readElementText();
            else if (name == QLatin1String("translation"))
            {
                type = xml.attributes().value("type").toString();
                if (!numerus)
                    forms << xml.readElementText();
            }
            else if (name == QLatin1String("numerusform"))
                forms << xml.readElementText();
        }
        else if (xml.isEndElement() && xml.name() == QLatin1String("message"))
        {
            ++messages;
            if (type == "obsolete" || type == "vanished")
            {
                report(type + " message; run update_translations");
                continue;
            }
            bool empty = forms.isEmpty();
            const auto want = placeholders(source);
            for (const QString& t : forms)
            {
                empty = empty || t.isEmpty();
                if (!t.isEmpty() && placeholders(t) != want)
                    report("placeholders differ in \"" + t.left(80) + "\"");
                // A right-to-left sentence that starts with a Latin word, a number or a
                // placeholder needs a leading RIGHT-TO-LEFT MARK, or it is laid out left to right.
                if (rtl && !t.isEmpty() && t.front() != QChar(0x200F) &&
                    startsWithoutRtlLetter(t) &&
                    containsRtlLetter(t))
                    report("starts without a right-to-left letter; begin it with U+200F");
                if (rtl && QString(t).replace(QStringLiteral("&&"), QString()).contains(QLatin1Char('&')))
                    report("has a mnemonic \"&\", which splits joined letters; remove it in \"" + t.left(80) +
                           "\"");
            }
            if (!empty && type.isEmpty())
                ++finished;
        }
    }
    if (xml.hasError())
    {
        std::printf("%s: line %lld: %s\n", qPrintable(path),
                    static_cast<long long>(xml.lineNumber()),
                    qPrintable(xml.errorString()));
        return 1;
    }
    std::printf("%s: %d messages, %d finished, %d to review\n",
                qPrintable(QFileInfo(path).fileName()), messages,
                finished, messages - finished);
    return problems ? 1 : 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments().mid(1);
    if (args.size() >= 2 && args[0] == "core-strings")
        return checkCoreStrings(args[1], args.contains("--write"));
    if (args.size() >= 2 && args[0] == "ts")
    {
        int status = 0;
        for (const QString& file : args.mid(1))
            status |= checkTs(file);
        return status;
    }
    std::fprintf(stderr, "usage: mirab-i18n-check core-strings <CoreStrings.cpp> [--write]\n"
                         "       mirab-i18n-check ts <file.ts>...\n");
    return 2;
}
