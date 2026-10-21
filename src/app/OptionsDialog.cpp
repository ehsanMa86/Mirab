/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/OptionsDialog.cpp
 * Purpose: Simulation options dialog for SWMM and EPANET projects.
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
#include "OptionsDialog.h"

#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace mirab::app
{

namespace
{
const QStringList kMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

QString firstWord(const QString& v)
{
    return v.section(' ', 0, 0, QString::SectionSkipEmpty);
}

QDate parseDate(const QString& v, bool monthDay)
{
    const QString t = v.trimmed();
    if (monthDay)
    {
        for (const char* f : {"M/d", "MM/dd"})
        {
            const QDate d = QDate::fromString(t + "/2001", QString(f) + "/yyyy");
            if (d.isValid())
                return d;
        }
        return {};
    }
    for (const char* f : {"M/d/yyyy", "MM/dd/yyyy", "yyyy-MM-dd", "M/d/yy"})
    {
        const QDate d = QDate::fromString(t, f);
        if (d.isValid())
            return d;
    }
    return {};
}

QTime parseTime(const QString& v)
{
    const auto secs = text::parseClockOrDuration(v.trimmed().toStdString());
    if (!secs)
        return {};
    const int s = static_cast<int>(*secs) % 86400;
    return QTime(0, 0).addSecs(s);
}

/// Values of a row of a value table, blank cells as empty strings.
QStringList tableRow(const QTableWidget* t, int row)
{
    QStringList out;
    for (int c = 0; c < t->columnCount(); ++c)
    {
        const auto* item = t->item(row, c);
        out << (item ? item->text().trimmed() : QString());
    }
    return out;
}

void setTableRow(QTableWidget* t, int row, const QStringList& values)
{
    for (int c = 0; c < t->columnCount(); ++c)
        t->setItem(row, c, new QTableWidgetItem(c < values.size() ? values[c] : QString()));
}

QStringList words(const QString& v)
{
    QStringList out;
    for (const auto& t : text::splitInpLine(v.toStdString()).tokens)
        out << QString::fromStdString(t);
    return out;
}

/// All values of a key that may span several rows (SWMM [REPORT] NODES).
QStringList listValues(const core::InpDocument& doc, const char* section, const char* key)
{
    QStringList out;
    if (const auto* s = doc.find(section))
    {
        for (const auto& t : s->allRowTokens())
        {
            if (t.size() >= 2 && text::iequals(t[0], key))
            {
                for (std::size_t i = 1; i < t.size(); ++i)
                    out << QString::fromStdString(t[i]);
            }
        }
    }
    return out;
}
} // namespace

OptionsDialog::OptionsDialog(ProjectDocument* doc, QWidget* parent) : QDialog(parent), doc_(doc)
{
    setWindowTitle(tr("Simulation options"));
    auto* layout = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    pages_ = new QListWidget(this);
    pages_->setFixedWidth(170);
    pages_->setIconSize(QSize(16, 16));
    stack_ = new QStackedWidget(this);
    body->addWidget(pages_);
    body->addWidget(stack_, 1);
    layout->addLayout(body, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(pages_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    if (doc_->model().dialect() == core::Dialect::Swmm)
        buildSwmm();
    else
        buildEpanet();
    for (auto& f : fields_)
        f.initial = f.state();
    pages_->setCurrentRow(0);
    resize(900, 600);
}

void OptionsDialog::showPage(const QString& key)
{
    const int i = pageKeys_.indexOf(key.toLower());
    if (i >= 0)
        pages_->setCurrentRow(i);
}

QFormLayout* OptionsDialog::addPage(const QString& key, const QString& title)
{
    auto* scroll = new QScrollArea(stack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout;
    column->addLayout(form);
    column->addStretch(1);
    form->setContentsMargins(14, 6, 10, 6);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(8);
    auto* heading = new QLabel(title, page);
    heading->setObjectName("panelTitle");
    form->addRow(heading);
    scroll->setWidget(page);
    stack_->addWidget(scroll);
    pages_->addItem(title);
    pageKeys_ << key;
    return form;
}

void OptionsDialog::addField(std::function<QString()> state, std::function<void(core::InpDocument&)> write)
{
    fields_.push_back(Field{std::move(state), std::move(write), {}});
}

QString OptionsDialog::value(const char* section, const char* key) const
{
    auto v = doc_->model().document().keyValue(section, key);
    return v ? QString::fromStdString(*v) : QString();
}

QComboBox* OptionsDialog::choice(QFormLayout* form, const QString& label, const char* section, const char* key,
                                 const QStringList& choices, const QString& fallback, const QString& tip)
{
    auto* box = new QComboBox(form->parentWidget());
    box->addItems(choices);
    QString v = value(section, key);
    if (v.isEmpty())
        v = fallback;
    int i = box->findText(v, Qt::MatchFixedString);
    if (i < 0)
    {
        box->addItem(v);
        i = box->count() - 1;
    }
    box->setCurrentIndex(i);
    box->setToolTip(tip.isEmpty() ? QString::fromLatin1(key) : tip + "  [" + key + "]");
    box->setMinimumWidth(160);
    box->setMaximumWidth(300);
    form->addRow(label, box);
    const std::string s(section), k(key);
    addField([box] { return box->currentText(); },
             [box, s, k](core::InpDocument& d) { d.setKeyValue(s, k, box->currentText().toStdString()); });
    return box;
}

QCheckBox* OptionsDialog::yesNo(QFormLayout* form, const QString& label, const char* section, const char* key,
                                bool fallback, bool inverted, const QString& tip)
{
    auto* box = new QCheckBox(label, form->parentWidget());
    const QString v = firstWord(value(section, key)).toUpper();
    const bool yes = v.isEmpty() ? fallback : v == "YES";
    box->setChecked(inverted ? !yes : yes);
    box->setToolTip(tip.isEmpty() ? QString::fromLatin1(key) : tip + "  [" + key + "]");
    form->addRow(QString(), box);
    const std::string s(section), k(key);
    addField([box] { return box->isChecked() ? QStringLiteral("1") : QStringLiteral("0"); },
             [box, s, k, inverted](core::InpDocument& d) {
                 const bool yes = inverted ? !box->isChecked() : box->isChecked();
                 d.setKeyValue(s, k, yes ? "YES" : "NO");
             });
    return box;
}

QLineEdit* OptionsDialog::number(QFormLayout* form, const QString& label, const char* section, const char* key,
                                 const QString& fallback, const QString& unit, const QString& tip)
{
    auto* edit = new QLineEdit(form->parentWidget());
    const QString v = value(section, key);
    edit->setText(v.isEmpty() ? fallback : v);
    edit->setValidator(new QRegularExpressionValidator(QRegularExpression(R"(^-?\d*\.?\d*([eE][-+]?\d+)?$)"), edit));
    edit->setToolTip(tip.isEmpty() ? QString::fromLatin1(key) : tip + "  [" + key + "]");
    edit->setMaximumWidth(160);
    QWidget* field = edit;
    if (!unit.isEmpty())
    {
        field = new QWidget(form->parentWidget());
        auto* row = new QHBoxLayout(field);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(edit);
        auto* u = new QLabel(unit, field);
        u->setObjectName("panelSubtitle");
        row->addWidget(u);
        row->addStretch(1);
    }
    form->addRow(label, field);
    const std::string s(section), k(key);
    addField([edit] { return edit->text().trimmed(); },
             [edit, s, k](core::InpDocument& d) {
                 const std::string t = edit->text().trimmed().toStdString();
                 if (t.empty())
                     d.removeKeyValue(s, k);
                 else
                     d.setKeyValue(s, k, t);
             });
    return edit;
}

QLineEdit* OptionsDialog::duration(QFormLayout* form, const QString& label, const char* section, const char* key,
                                   const QString& fallback, const QString& tip)
{
    auto* edit = new QLineEdit(form->parentWidget());
    const QString v = value(section, key);
    edit->setText(v.isEmpty() ? fallback : v);
    edit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(R"(^\d+(\.\d+)?(:\d{1,2}){0,2}(\s+(SEC|SECONDS|MIN|MINUTES|HOURS?|DAYS?|AM|PM))?$)",
                           QRegularExpression::CaseInsensitiveOption),
        edit));
    edit->setPlaceholderText(tr("hh:mm:ss"));
    edit->setToolTip(tip.isEmpty() ? QString::fromLatin1(key) : tip + "  [" + key + "]");
    edit->setMaximumWidth(160);
    form->addRow(label, edit);
    const std::string s(section), k(key);
    addField([edit] { return edit->text().trimmed(); },
             [edit, s, k](core::InpDocument& d) {
                 const std::string t = edit->text().trimmed().toStdString();
                 if (t.empty())
                     d.removeKeyValue(s, k);
                 else
                     d.setKeyValue(s, k, t);
             });
    return edit;
}

void OptionsDialog::dateTime(QFormLayout* form, const QString& label, const char* dateKey, const char* timeKey)
{
    auto* row = new QWidget(form->parentWidget());
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    auto* date = new QDateEdit(row);
    date->setDisplayFormat("MM/dd/yyyy");
    date->setCalendarPopup(true);
    QDate d = parseDate(value("OPTIONS", dateKey), false);
    if (!d.isValid())
        d = parseDate(value("OPTIONS", "START_DATE"), false);
    date->setDate(d.isValid() ? d : QDate(2004, 1, 1));
    auto* time = new QTimeEdit(row);
    time->setDisplayFormat("HH:mm:ss");
    const QTime t = parseTime(value("OPTIONS", timeKey));
    time->setTime(t.isValid() ? t : QTime(0, 0));
    date->setToolTip(QString::fromLatin1(dateKey));
    time->setToolTip(QString::fromLatin1(timeKey));
    hl->addWidget(date);
    hl->addWidget(time);
    hl->addStretch(1);
    form->addRow(label, row);
    const std::string dk(dateKey), tk(timeKey);
    addField([date] { return date->date().toString("MM/dd/yyyy"); },
             [date, dk](core::InpDocument& doc) { doc.setKeyValue("OPTIONS", dk, date->date().toString("MM/dd/yyyy").toStdString()); });
    addField([time] { return time->time().toString("HH:mm:ss"); },
             [time, tk](core::InpDocument& doc) { doc.setKeyValue("OPTIONS", tk, time->time().toString("HH:mm:ss").toStdString()); });
}

QTableWidget* OptionsDialog::valueTable(const QStringList& rows, const QStringList& columns)
{
    auto* t = new QTableWidget(static_cast<int>(rows.size()), static_cast<int>(columns.size()), this);
    t->setHorizontalHeaderLabels(columns);
    t->setVerticalHeaderLabels(rows);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->horizontalHeader()->setMinimumSectionSize(36);
    t->verticalHeader()->setDefaultSectionSize(26);
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    t->setFixedHeight(t->horizontalHeader()->sizeHint().height() + 26 * static_cast<int>(rows.size()) + 4);
    return t;
}

QLineEdit* OptionsDialog::fileRow(QFormLayout* form, const QString& label, const char* key, bool save)
{
    auto* row = new QWidget(form->parentWidget());
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    auto* use = new QCheckBox(label, row);
    auto* path = new QLineEdit(row);
    auto* browse = new QPushButton(tr("Browse..."), row);
    const QString v = value("FILES", key);
    use->setChecked(!v.isEmpty());
    path->setText(QString::fromStdString(text::unquote(v.toStdString())));
    path->setEnabled(use->isChecked());
    browse->setEnabled(use->isChecked());
    use->setMinimumWidth(190);
    hl->addWidget(use);
    hl->addWidget(path, 1);
    hl->addWidget(browse);
    form->addRow(row);
    connect(use, &QCheckBox::toggled, path, &QWidget::setEnabled);
    connect(use, &QCheckBox::toggled, browse, &QWidget::setEnabled);
    const QString dir = doc_->path().isEmpty() ? QDir::homePath() : QFileInfo(doc_->path()).absolutePath();
    connect(browse, &QPushButton::clicked, this, [this, path, save, dir, label] {
        const QString f = save ? QFileDialog::getSaveFileName(this, label, dir) : QFileDialog::getOpenFileName(this, label, dir);
        if (f.isEmpty())
            return;
        const QString rel = QDir(dir).relativeFilePath(f);
        path->setText(rel.startsWith("..") ? f : rel);
    });
    const std::string k(key);
    addField([use, path] { return use->isChecked() ? path->text().trimmed() : QString(); },
             [use, path, k](core::InpDocument& d) {
                 const QString p = path->text().trimmed();
                 if (!use->isChecked() || p.isEmpty())
                     d.removeKeyValue("FILES", k);
                 else
                     d.setKeyValue("FILES", k, text::quoteIfNeeded(p.toStdString()));
             });
    return path;
}

// ---------------------------------------------------------------------------
// SWMM
// ---------------------------------------------------------------------------
void OptionsDialog::buildSwmm()
{
    const auto units = doc_->model().units();
    const bool si = units.system == core::UnitSystem::SI;
    const QString lenUnit = si ? "m" : "ft";

    QFormLayout* f = addPage("general", tr("General"));
    auto* flow = choice(f, tr("Flow units"), "OPTIONS", "FLOW_UNITS", {"CFS", "GPM", "MGD", "CMS", "LPS", "MLD"}, "CFS");
    auto* unitNote = new QLabel(tr("Switching between US and SI flow units does not convert the values in the model."), this);
    unitNote->setWordWrap(true);
    unitNote->setObjectName("panelSubtitle");
    unitNote->setVisible(false);
    f->addRow(QString(), unitNote);
    const QString startUnits = flow->currentText();
    connect(flow, &QComboBox::currentTextChanged, unitNote, [unitNote, startUnits](const QString& u) {
        unitNote->setVisible(core::isSiFlowUnit(core::Dialect::Swmm, u.toStdString()) !=
                             core::isSiFlowUnit(core::Dialect::Swmm, startUnits.toStdString()));
    });
    choice(f, tr("Infiltration model"), "OPTIONS", "INFILTRATION",
           {"HORTON", "MODIFIED_HORTON", "GREEN_AMPT", "MODIFIED_GREEN_AMPT", "CURVE_NUMBER"}, "HORTON");
    choice(f, tr("Flow routing"), "OPTIONS", "FLOW_ROUTING", {"STEADY", "KINWAVE", "DYNWAVE"}, "KINWAVE",
           tr("Steady flow, kinematic wave or dynamic wave routing"));
    choice(f, tr("Link offsets"), "OPTIONS", "LINK_OFFSETS", {"DEPTH", "ELEVATION"}, "DEPTH",
           tr("Whether link offsets are depths above the node invert or elevations"));
    number(f, tr("Minimum conduit slope"), "OPTIONS", "MIN_SLOPE", "0", "%");
    yesNo(f, tr("Allow ponding at flooded nodes"), "OPTIONS", "ALLOW_PONDING", false);
    yesNo(f, tr("Skip steady flow periods"), "OPTIONS", "SKIP_STEADY_STATE", false);
    number(f, tr("System flow tolerance"), "OPTIONS", "SYS_FLOW_TOL", "5", "%",
           tr("Largest change in system inflow and outflow of a steady period"));
    number(f, tr("Lateral flow tolerance"), "OPTIONS", "LAT_FLOW_TOL", "5", "%",
           tr("Largest change in lateral inflow of a steady period"));
    auto* models = new QLabel(tr("Process models"), this);
    models->setObjectName("panelTitle");
    f->addRow(models);
    yesNo(f, tr("Rainfall and runoff"), "OPTIONS", "IGNORE_RAINFALL", false, true);
    yesNo(f, tr("Snowmelt"), "OPTIONS", "IGNORE_SNOWMELT", false, true);
    yesNo(f, tr("Groundwater"), "OPTIONS", "IGNORE_GROUNDWATER", false, true);
    yesNo(f, tr("Rainfall-dependent infiltration/inflow"), "OPTIONS", "IGNORE_RDII", false, true);
    yesNo(f, tr("Flow routing"), "OPTIONS", "IGNORE_ROUTING", false, true);
    yesNo(f, tr("Water quality"), "OPTIONS", "IGNORE_QUALITY", false, true);

    f = addPage("dates", tr("Dates"));
    dateTime(f, tr("Start analysis on"), "START_DATE", "START_TIME");
    dateTime(f, tr("Start reporting on"), "REPORT_START_DATE", "REPORT_START_TIME");
    dateTime(f, tr("End analysis on"), "END_DATE", "END_TIME");
    auto sweep = [&](const QString& label, const char* key, const QDate& fallback) {
        auto* d = new QDateEdit(f->parentWidget());
        d->setDisplayFormat("MM/dd");
        const QDate v = parseDate(value("OPTIONS", key), true);
        d->setDate(v.isValid() ? v : fallback);
        d->setToolTip(QString::fromLatin1(key));
        d->setMaximumWidth(120);
        f->addRow(label, d);
        const std::string k(key);
        addField([d] { return d->date().toString("MM/dd"); },
                 [d, k](core::InpDocument& doc) { doc.setKeyValue("OPTIONS", k, d->date().toString("MM/dd").toStdString()); });
    };
    sweep(tr("Start street sweeping on"), "SWEEP_START", QDate(2001, 1, 1));
    sweep(tr("End street sweeping on"), "SWEEP_END", QDate(2001, 12, 31));
    number(f, tr("Antecedent dry days"), "OPTIONS", "DRY_DAYS", "0", tr("days"));

    f = addPage("steps", tr("Time steps"));
    duration(f, tr("Reporting step"), "OPTIONS", "REPORT_STEP", "00:15:00");
    duration(f, tr("Runoff, wet weather"), "OPTIONS", "WET_STEP", "00:05:00");
    duration(f, tr("Runoff, dry weather"), "OPTIONS", "DRY_STEP", "01:00:00");
    duration(f, tr("Routing step"), "OPTIONS", "ROUTING_STEP", "0:00:20",
             tr("Seconds or hh:mm:ss"));
    duration(f, tr("Control rule step"), "OPTIONS", "RULE_STEP", "00:00:00",
             tr("How often control rules are evaluated (0 = every routing step)"));
    number(f, tr("Variable step safety factor"), "OPTIONS", "VARIABLE_STEP", "0", {},
           tr("Fraction of the Courant time step used by dynamic wave routing (0 = fixed step)"));
    number(f, tr("Minimum variable step"), "OPTIONS", "MINIMUM_STEP", "0.5", tr("s"));
    number(f, tr("Conduit lengthening step"), "OPTIONS", "LENGTHENING_STEP", "0", tr("s"),
           tr("Time step used to lengthen short conduits (0 = no lengthening)"));
    number(f, tr("Minimum nodal surface area"), "OPTIONS", "MIN_SURFAREA", si ? "1.167" : "12.566",
           si ? "m\xC2\xB2" : "ft\xC2\xB2");

    f = addPage("dynwave", tr("Dynamic wave"));
    choice(f, tr("Inertial terms"), "OPTIONS", "INERTIAL_DAMPING", {"NONE", "PARTIAL", "FULL"}, "PARTIAL",
           tr("How the inertial terms of the momentum equation are damped"));
    choice(f, tr("Normal flow criterion"), "OPTIONS", "NORMAL_FLOW_LIMITED", {"SLOPE", "FROUDE", "BOTH"}, "BOTH");
    choice(f, tr("Force main equation"), "OPTIONS", "FORCE_MAIN_EQUATION", {"H-W", "D-W"}, "H-W",
           tr("Hazen-Williams or Darcy-Weisbach friction losses in force mains"));
    choice(f, tr("Surcharge method"), "OPTIONS", "SURCHARGE_METHOD", {"EXTRAN", "SLOT"}, "EXTRAN");
    number(f, tr("Maximum trials per step"), "OPTIONS", "MAX_TRIALS", "8");
    number(f, tr("Head convergence tolerance"), "OPTIONS", "HEAD_TOLERANCE", si ? "0.0015" : "0.005", lenUnit);
    number(f, tr("Threads"), "OPTIONS", "THREADS", "1", {}, tr("Number of parallel threads used by dynamic wave routing"));

    f = addPage("files", tr("Interface files"));
    auto* note = new QLabel(tr("Files that SWMM reads or writes to hand results from one run to another. "
                               "Paths are relative to the project file."),
                            this);
    note->setWordWrap(true);
    note->setObjectName("panelSubtitle");
    f->addRow(note);
    fileRow(f, tr("Use hotstart file"), "USE HOTSTART", false);
    fileRow(f, tr("Save hotstart file"), "SAVE HOTSTART", true);
    fileRow(f, tr("Use rainfall file"), "USE RAINFALL", false);
    fileRow(f, tr("Save rainfall file"), "SAVE RAINFALL", true);
    fileRow(f, tr("Use runoff file"), "USE RUNOFF", false);
    fileRow(f, tr("Save runoff file"), "SAVE RUNOFF", true);
    fileRow(f, tr("Use RDII file"), "USE RDII", false);
    fileRow(f, tr("Save RDII file"), "SAVE RDII", true);
    fileRow(f, tr("Use inflows file"), "USE INFLOWS", false);
    fileRow(f, tr("Save outflows file"), "SAVE OUTFLOWS", true);

    buildSwmmReport(addPage("report", tr("Reporting")));
    buildEvaporation(addPage("evaporation", tr("Evaporation")));
    buildClimate(addPage("climate", tr("Temperature and snow")));
    buildAdjustments(addPage("adjustments", tr("Monthly adjustments")));
}

void OptionsDialog::buildSwmmReport(QFormLayout* f)
{
    yesNo(f, tr("Input summary"), "REPORT", "INPUT", false);
    yesNo(f, tr("Continuity checks"), "REPORT", "CONTINUITY", true);
    yesNo(f, tr("Flow statistics"), "REPORT", "FLOWSTATS", true);
    yesNo(f, tr("Control actions"), "REPORT", "CONTROLS", false);
    yesNo(f, tr("Report averages over each reporting step"), "REPORT", "AVERAGES", false);
    const auto& doc = doc_->model().document();
    auto list = [&](const QString& label, const char* key) {
        auto* row = new QWidget(f->parentWidget());
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        auto* mode = new QComboBox(row);
        mode->addItems({tr("All"), tr("None"), tr("Listed")});
        auto* names = new QLineEdit(row);
        names->setPlaceholderText(tr("names separated by blanks"));
        const QStringList values = listValues(doc, "REPORT", key);
        if (values.isEmpty() || (values.size() == 1 && values[0].compare("ALL", Qt::CaseInsensitive) == 0))
            mode->setCurrentIndex(values.isEmpty() ? 1 : 0);
        else if (values.size() == 1 && values[0].compare("NONE", Qt::CaseInsensitive) == 0)
            mode->setCurrentIndex(1);
        else
        {
            mode->setCurrentIndex(2);
            names->setText(values.join(' '));
        }
        names->setEnabled(mode->currentIndex() == 2);
        connect(mode, &QComboBox::currentIndexChanged, names, [names](int i) { names->setEnabled(i == 2); });
        hl->addWidget(mode);
        hl->addWidget(names, 1);
        f->addRow(label, row);
        const std::string k(key);
        addField([mode, names] { return mode->currentIndex() == 2 ? names->text().simplified() : mode->currentText(); },
                 [mode, names, k](core::InpDocument& d) {
                     auto& s = d.ensure("REPORT");
                     std::vector<int> rows;
                     const auto all = s.allRowTokens();
                     for (int r = static_cast<int>(all.size()) - 1; r >= 0; --r)
                     {
                         if (!all[static_cast<std::size_t>(r)].empty() && text::iequals(all[static_cast<std::size_t>(r)][0], k))
                             s.removeRow(r);
                     }
                     if (mode->currentIndex() != 2)
                     {
                         s.appendRow({k, mode->currentIndex() == 0 ? "ALL" : "NONE"});
                         return;
                     }
                     std::vector<std::string> row = {k};
                     for (const auto& n : text::splitInpLine(names->text().toStdString()).tokens)
                     {
                         row.push_back(n);
                         if (row.size() == 9)
                         {
                             s.appendRow(row);
                             row = {k};
                         }
                     }
                     if (row.size() > 1)
                         s.appendRow(row);
                 });
    };
    list(tr("Subcatchments"), "SUBCATCHMENTS");
    list(tr("Nodes"), "NODES");
    list(tr("Links"), "LINKS");
}

void OptionsDialog::buildEvaporation(QFormLayout* f)
{
    const auto& doc = doc_->model().document();
    const bool si = doc_->model().units().system == core::UnitSystem::SI;
    const QString rate = si ? "mm/day" : "in/day";
    struct Source
    {
        const char* key;
        QString label;
    };
    const std::vector<Source> sources = {{"CONSTANT", tr("Constant value")},
                                         {"MONTHLY", tr("Monthly averages")},
                                         {"TIMESERIES", tr("Time series")},
                                         {"TEMPERATURE", tr("Computed from temperatures")},
                                         {"FILE", tr("Climate file")}};
    auto* source = new QComboBox(f->parentWidget());
    int current = 0;
    QString currentValue;
    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        source->addItem(sources[i].label);
        if (auto v = doc.keyValue("EVAPORATION", sources[i].key))
        {
            current = static_cast<int>(i);
            currentValue = QString::fromStdString(*v);
        }
    }
    source->setCurrentIndex(current);
    f->addRow(tr("Source of evaporation rates"), source);
    auto* stack = new QStackedWidget(f->parentWidget());
    // Constant
    auto* constant = new QLineEdit(current == 0 ? (currentValue.isEmpty() ? "0" : currentValue) : "0", stack);
    constant->setValidator(new QRegularExpressionValidator(QRegularExpression(R"(^\d*\.?\d*$)"), constant));
    constant->setMaximumWidth(160);
    auto* cpage = new QWidget(stack);
    auto* cl = new QHBoxLayout(cpage);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(new QLabel(tr("Daily evaporation"), cpage));
    cl->addWidget(constant);
    cl->addWidget(new QLabel(rate, cpage));
    cl->addStretch(1);
    stack->addWidget(cpage);
    // Monthly
    auto* monthly = valueTable({rate}, kMonths);
    setTableRow(monthly, 0, current == 1 ? words(currentValue) : QStringList());
    stack->addWidget(monthly);
    // Time series
    auto* series = new QComboBox(stack);
    series->setEditable(true);
    if (const auto* s = doc.find("TIMESERIES"))
    {
        QStringList names;
        for (const auto& t : s->allRowTokens())
        {
            const QString n = t.empty() ? QString() : QString::fromStdString(t[0]);
            if (!n.isEmpty() && !names.contains(n))
                names << n;
        }
        series->addItems(names);
    }
    series->setCurrentText(current == 2 ? currentValue : QString());
    stack->addWidget(series);
    // Temperature
    stack->addWidget(new QLabel(tr("Evaporation is computed from the daily temperatures (Hargreaves method)."), stack));
    // Climate file
    auto* pan = valueTable({tr("Pan coefficient")}, kMonths);
    setTableRow(pan, 0, current == 4 ? words(currentValue) : QStringList());
    stack->addWidget(pan);
    stack->setCurrentIndex(current);
    stack->setFixedHeight(monthly->height());
    connect(source, &QComboBox::currentIndexChanged, stack, &QStackedWidget::setCurrentIndex);
    f->addRow(stack);
    auto state = [source, constant, monthly, series, pan] {
        switch (source->currentIndex())
        {
        case 0:
            return "C " + constant->text();
        case 1:
            return "M " + tableRow(monthly, 0).join(' ');
        case 2:
            return "S " + series->currentText();
        case 3:
            return QStringLiteral("T");
        default:
            return "F " + tableRow(pan, 0).join(' ');
        }
    };
    addField(state, [source, constant, monthly, series, pan](core::InpDocument& d) {
        for (const char* k : {"CONSTANT", "MONTHLY", "TIMESERIES", "TEMPERATURE", "FILE"})
            d.removeKeyValue("EVAPORATION", k);
        auto fill = [](QStringList v, const char* blank) {
            for (auto& x : v)
                if (x.isEmpty())
                    x = blank;
            return v.join(' ').toStdString();
        };
        switch (source->currentIndex())
        {
        case 0:
            d.setKeyValue("EVAPORATION", "CONSTANT", constant->text().isEmpty() ? "0" : constant->text().toStdString());
            break;
        case 1:
            d.setKeyValue("EVAPORATION", "MONTHLY", fill(tableRow(monthly, 0), "0"));
            break;
        case 2:
            d.setKeyValue("EVAPORATION", "TIMESERIES", series->currentText().trimmed().toStdString());
            break;
        case 3:
            d.setKeyValue("EVAPORATION", "TEMPERATURE", "");
            break;
        default:
        {
            const auto v = tableRow(pan, 0);
            bool any = false;
            for (const auto& x : v)
                any = any || !x.isEmpty();
            d.setKeyValue("EVAPORATION", "FILE", any ? fill(v, "1") : "");
            break;
        }
        }
    });
    auto* recovery = new QComboBox(f->parentWidget());
    recovery->setEditable(true);
    recovery->addItem(QString());
    if (const auto* s = doc.find("PATTERNS"))
    {
        QStringList names;
        for (const auto& t : s->allRowTokens())
        {
            const QString n = t.empty() ? QString() : QString::fromStdString(t[0]);
            if (!n.isEmpty() && !names.contains(n))
                names << n;
        }
        recovery->addItems(names);
    }
    recovery->setCurrentText(value("EVAPORATION", "RECOVERY"));
    recovery->setToolTip(tr("Monthly pattern that adjusts the recovery rate of infiltration capacity  [RECOVERY]"));
    f->addRow(tr("Soil recovery pattern"), recovery);
    addField([recovery] { return recovery->currentText().trimmed(); },
             [recovery](core::InpDocument& d) {
                 const std::string p = recovery->currentText().trimmed().toStdString();
                 if (p.empty())
                     d.removeKeyValue("EVAPORATION", "RECOVERY");
                 else
                     d.setKeyValue("EVAPORATION", "RECOVERY", p);
             });
    yesNo(f, tr("Evaporate only during dry periods"), "EVAPORATION", "DRY_ONLY", false);
}

void OptionsDialog::buildClimate(QFormLayout* f)
{
    const bool si = doc_->model().units().system == core::UnitSystem::SI;
    // Air temperature
    auto* source = new QComboBox(f->parentWidget());
    source->addItems({tr("No temperature data"), tr("Time series"), tr("Climate file")});
    const QString ts = value("TEMPERATURE", "TIMESERIES");
    const QString file = value("TEMPERATURE", "FILE");
    source->setCurrentIndex(!ts.isEmpty() ? 1 : !file.isEmpty() ? 2 : 0);
    f->addRow(tr("Air temperature"), source);
    auto* name = new QLineEdit(f->parentWidget());
    const QStringList fileWords = words(file);
    name->setText(!ts.isEmpty() ? ts : fileWords.isEmpty() ? QString() : QString::fromStdString(text::unquote(fileWords[0].toStdString())));
    name->setPlaceholderText(tr("time series name or climate file path"));
    auto* start = new QLineEdit(fileWords.size() > 1 ? fileWords[1] : QString(), f->parentWidget());
    start->setPlaceholderText(tr("MM/DD/YYYY (optional)"));
    start->setMaximumWidth(160);
    name->setEnabled(source->currentIndex() != 0);
    start->setEnabled(source->currentIndex() == 2);
    connect(source, &QComboBox::currentIndexChanged, this, [name, start](int i) {
        name->setEnabled(i != 0);
        start->setEnabled(i == 2);
    });
    f->addRow(tr("Series or file"), name);
    f->addRow(tr("Start reading the file on"), start);
    addField([source, name, start] { return QString::number(source->currentIndex()) + name->text() + "|" + start->text(); },
             [source, name, start](core::InpDocument& d) {
                 d.removeKeyValue("TEMPERATURE", "TIMESERIES");
                 d.removeKeyValue("TEMPERATURE", "FILE");
                 const std::string n = name->text().trimmed().toStdString();
                 if (source->currentIndex() == 1 && !n.empty())
                     d.setKeyValue("TEMPERATURE", "TIMESERIES", n);
                 else if (source->currentIndex() == 2 && !n.empty())
                     d.setKeyValue("TEMPERATURE", "FILE",
                                   text::quoteIfNeeded(n) + (start->text().trimmed().isEmpty()
                                                                 ? std::string()
                                                                 : " " + start->text().trimmed().toStdString()));
             });

    // Wind speed
    auto* wind = new QComboBox(f->parentWidget());
    wind->addItems({tr("Monthly averages"), tr("Climate file")});
    const QStringList windWords = words(value("TEMPERATURE", "WINDSPEED"));
    wind->setCurrentIndex(!windWords.isEmpty() && windWords[0].compare("FILE", Qt::CaseInsensitive) == 0 ? 1 : 0);
    f->addRow(tr("Wind speed"), wind);
    auto* windTable = valueTable({si ? "km/h" : "mph"}, kMonths);
    setTableRow(windTable, 0, wind->currentIndex() == 0 ? windWords.mid(1) : QStringList());
    windTable->setEnabled(wind->currentIndex() == 0);
    connect(wind, &QComboBox::currentIndexChanged, windTable, [windTable](int i) { windTable->setEnabled(i == 0); });
    f->addRow(windTable);
    addField([wind, windTable] { return QString::number(wind->currentIndex()) + tableRow(windTable, 0).join(' '); },
             [wind, windTable](core::InpDocument& d) {
                 if (wind->currentIndex() == 1)
                 {
                     d.setKeyValue("TEMPERATURE", "WINDSPEED", "FILE");
                     return;
                 }
                 QStringList v = tableRow(windTable, 0);
                 bool any = false;
                 for (auto& x : v)
                 {
                     any = any || !x.isEmpty();
                     if (x.isEmpty())
                         x = "0";
                 }
                 if (any)
                     d.setKeyValue("TEMPERATURE", "WINDSPEED", "MONTHLY " + v.join(' ').toStdString());
                 else
                     d.removeKeyValue("TEMPERATURE", "WINDSPEED");
             });

    // Snowmelt
    auto* heading = new QLabel(tr("Snowmelt"), this);
    heading->setObjectName("panelTitle");
    f->addRow(heading);
    const QStringList melt = words(value("TEMPERATURE", "SNOWMELT"));
    const QStringList fallback = si ? QStringList{"1", "0.5", "0.6", "0", "50", "0"}
                                    : QStringList{"34", "0.5", "0.6", "0", "50", "0"};
    const QStringList labels = {tr("Dividing temperature between snow and rain"), tr("ATI weight"),
                                tr("Negative melt ratio"), tr("Elevation above sea level"), tr("Latitude"),
                                tr("Longitude correction")};
    const QStringList unitsList = {si ? "\xC2\xB0" "C" : "\xC2\xB0" "F", {}, {}, si ? "m" : "ft", tr("degrees"), tr("minutes")};
    std::vector<QLineEdit*> meltEdits;
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* e = new QLineEdit(i < melt.size() ? melt[i] : fallback[i], f->parentWidget());
        e->setValidator(new QRegularExpressionValidator(QRegularExpression(R"(^-?\d*\.?\d*$)"), e));
        e->setMaximumWidth(160);
        auto* row = new QWidget(f->parentWidget());
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(e);
        if (!unitsList[i].isEmpty())
        {
            auto* u = new QLabel(unitsList[i], row);
            u->setObjectName("panelSubtitle");
            hl->addWidget(u);
        }
        hl->addStretch(1);
        f->addRow(labels[i], row);
        meltEdits.push_back(e);
    }
    addField([meltEdits] {
        QStringList v;
        for (auto* e : meltEdits)
            v << e->text().trimmed();
        return v.join(' ');
    },
             [meltEdits](core::InpDocument& d) {
                 QStringList v;
                 for (auto* e : meltEdits)
                     v << (e->text().trimmed().isEmpty() ? QStringLiteral("0") : e->text().trimmed());
                 d.setKeyValue("TEMPERATURE", "SNOWMELT", v.join(' ').toStdString());
             });

    // Areal depletion
    auto* adcHeading = new QLabel(tr("Areal depletion (fraction of area covered at 0 to 90% of the depth at full cover)"), this);
    adcHeading->setWordWrap(true);
    f->addRow(adcHeading);
    QStringList cols;
    for (int i = 0; i < 10; ++i)
        cols << QString::number(i * 10) + "%";
    auto* adc = valueTable({tr("Impervious"), tr("Pervious")}, cols);
    setTableRow(adc, 0, words(value("TEMPERATURE", "ADC IMPERVIOUS")));
    setTableRow(adc, 1, words(value("TEMPERATURE", "ADC PERVIOUS")));
    f->addRow(adc);
    for (int r = 0; r < 2; ++r)
    {
        const char* key = r == 0 ? "ADC IMPERVIOUS" : "ADC PERVIOUS";
        addField([adc, r] { return tableRow(adc, r).join(' '); },
                 [adc, r, key](core::InpDocument& d) {
                     QStringList v = tableRow(adc, r);
                     bool any = false;
                     for (int i = 0; i < v.size(); ++i)
                     {
                         any = any || !v[i].isEmpty();
                         if (v[i].isEmpty())
                             v[i] = "1.0";
                     }
                     if (any)
                         d.setKeyValue("TEMPERATURE", key, v.join(' ').toStdString());
                     else
                         d.removeKeyValue("TEMPERATURE", key);
                 });
    }
}

void OptionsDialog::buildAdjustments(QFormLayout* f)
{
    auto* note = new QLabel(tr("Temperature and evaporation adjustments are added to the climate data; rainfall and "
                               "conductivity factors multiply the rainfall and the infiltration conductivity. Leave a "
                               "row empty for no adjustment."),
                            this);
    note->setWordWrap(true);
    note->setObjectName("panelSubtitle");
    f->addRow(note);
    const bool si = doc_->model().units().system == core::UnitSystem::SI;
    struct Row
    {
        const char* key;
        QString label;
        const char* blank;
    };
    const std::vector<Row> rows = {{"TEMPERATURE", tr("Temperature (%1)").arg(si ? "\xC2\xB0" "C" : "\xC2\xB0" "F"), "0"},
                                   {"EVAPORATION", tr("Evaporation (%1)").arg(si ? "mm/day" : "in/day"), "0"},
                                   {"RAINFALL", tr("Rainfall (factor)"), "1.0"},
                                   {"CONDUCTIVITY", tr("Conductivity (factor)"), "1.0"}};
    QStringList labels;
    for (const auto& r : rows)
        labels << r.label;
    auto* table = valueTable(labels, kMonths);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        setTableRow(table, i, words(value("ADJUSTMENTS", rows[static_cast<std::size_t>(i)].key)));
    f->addRow(table);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const Row row = rows[static_cast<std::size_t>(i)];
        addField([table, i] { return tableRow(table, i).join(' '); },
                 [table, i, row](core::InpDocument& d) {
                     QStringList v = tableRow(table, i);
                     bool any = false;
                     for (auto& x : v)
                     {
                         any = any || !x.isEmpty();
                         if (x.isEmpty())
                             x = row.blank;
                     }
                     if (any)
                         d.setKeyValue("ADJUSTMENTS", row.key, v.join(' ').toStdString());
                     else
                         d.removeKeyValue("ADJUSTMENTS", row.key);
                 });
    }
}

// ---------------------------------------------------------------------------
// EPANET
// ---------------------------------------------------------------------------
void OptionsDialog::buildEpanet()
{
    const auto units = doc_->model().units();
    const bool si = units.system == core::UnitSystem::SI;

    QFormLayout* f = addPage("hydraulics", tr("Hydraulics"));
    choice(f, tr("Flow units"), "OPTIONS", "UNITS",
           {"CFS", "GPM", "MGD", "IMGD", "AFD", "LPS", "LPM", "MLD", "CMH", "CMD", "CMS"}, "GPM",
           tr("CMS needs EPANET 2.3"));
    choice(f, tr("Pressure units"), "OPTIONS", "PRESSURE", {"PSI", "KPA", "METERS", "BAR", "FEET"},
           si ? "METERS" : "PSI", tr("BAR and FEET need EPANET 2.3"));
    choice(f, tr("Headloss formula"), "OPTIONS", "HEADLOSS", {"H-W", "D-W", "C-M"}, "H-W",
           tr("Hazen-Williams, Darcy-Weisbach or Chezy-Manning"));
    number(f, tr("Specific gravity"), "OPTIONS", "SPECIFIC GRAVITY", "1.0");
    number(f, tr("Relative viscosity"), "OPTIONS", "VISCOSITY", "1.0");
    number(f, tr("Maximum trials"), "OPTIONS", "TRIALS", "200");
    number(f, tr("Accuracy"), "OPTIONS", "ACCURACY", "0.001");
    auto* unbalanced = choice(f, tr("If unbalanced"), "OPTIONS", "UNBALANCED", {"STOP", "CONTINUE", "CONTINUE 10"}, "STOP");
    unbalanced->setEditable(true);
    number(f, tr("Check frequency"), "OPTIONS", "CHECKFREQ", "2");
    number(f, tr("Maximum check"), "OPTIONS", "MAXCHECK", "10");
    number(f, tr("Damping limit"), "OPTIONS", "DAMPLIMIT", "0");
    number(f, tr("Head error limit"), "OPTIONS", "HEADERROR", "0", si ? "m" : "ft");
    number(f, tr("Flow change limit"), "OPTIONS", "FLOWCHANGE", "0", QString::fromStdString(units.flow));
    auto* demands = new QLabel(tr("Demands"), this);
    demands->setObjectName("panelTitle");
    f->addRow(demands);
    auto* pattern = choice(f, tr("Default pattern"), "OPTIONS", "PATTERN", {}, "1");
    pattern->setEditable(true);
    number(f, tr("Demand multiplier"), "OPTIONS", "DEMAND MULTIPLIER", "1.0");
    choice(f, tr("Demand model"), "OPTIONS", "DEMAND MODEL", {"DDA", "PDA"}, "DDA",
           tr("Demand-driven or pressure-driven analysis"));
    number(f, tr("Minimum pressure"), "OPTIONS", "MINIMUM PRESSURE", "0", QString::fromStdString(units.label("pressure")));
    number(f, tr("Required pressure"), "OPTIONS", "REQUIRED PRESSURE", "0.1", QString::fromStdString(units.label("pressure")));
    number(f, tr("Pressure exponent"), "OPTIONS", "PRESSURE EXPONENT", "0.5");
    number(f, tr("Emitter exponent"), "OPTIONS", "EMITTER EXPONENT", "0.5");

    f = addPage("quality", tr("Water quality"));
    auto* quality = choice(f, tr("Parameter"), "OPTIONS", "QUALITY", {"NONE", "CHEMICAL mg/L", "AGE", "TRACE"}, "NONE",
                           tr("NONE, CHEMICAL with its units, AGE, or TRACE with the traced node"));
    quality->setEditable(true);
    number(f, tr("Relative diffusivity"), "OPTIONS", "DIFFUSIVITY", "1.0");
    number(f, tr("Quality tolerance"), "OPTIONS", "TOLERANCE", "0.01");
    auto* reactions = new QLabel(tr("Reactions"), this);
    reactions->setObjectName("panelTitle");
    f->addRow(reactions);
    number(f, tr("Bulk reaction order"), "REACTIONS", "ORDER BULK", "1");
    choice(f, tr("Wall reaction order"), "REACTIONS", "ORDER WALL", {"0", "1"}, "1");
    number(f, tr("Tank reaction order"), "REACTIONS", "ORDER TANK", "1");
    number(f, tr("Global bulk coefficient"), "REACTIONS", "GLOBAL BULK", "0", tr("1/day"));
    number(f, tr("Global wall coefficient"), "REACTIONS", "GLOBAL WALL", "0");
    number(f, tr("Limiting concentration"), "REACTIONS", "LIMITING POTENTIAL", "0");
    number(f, tr("Wall coefficient correlation"), "REACTIONS", "ROUGHNESS CORRELATION", "0");

    f = addPage("times", tr("Times"));
    duration(f, tr("Total duration"), "TIMES", "DURATION", "0:00");
    duration(f, tr("Hydraulic time step"), "TIMES", "HYDRAULIC TIMESTEP", "1:00");
    duration(f, tr("Quality time step"), "TIMES", "QUALITY TIMESTEP", "0:05");
    duration(f, tr("Pattern time step"), "TIMES", "PATTERN TIMESTEP", "1:00");
    duration(f, tr("Pattern start"), "TIMES", "PATTERN START", "0:00");
    duration(f, tr("Reporting time step"), "TIMES", "REPORT TIMESTEP", "1:00");
    duration(f, tr("Report start"), "TIMES", "REPORT START", "0:00");
    duration(f, tr("Clock start time"), "TIMES", "START CLOCKTIME", "12 AM");
    duration(f, tr("Rule time step"), "TIMES", "RULE TIMESTEP", "0:06");
    choice(f, tr("Statistic"), "TIMES", "STATISTIC", {"NONE", "AVERAGED", "MINIMUM", "MAXIMUM", "RANGE"}, "NONE",
           tr("Report time series or a statistic over the simulation"));

    f = addPage("energy", tr("Energy"));
    number(f, tr("Pump efficiency"), "ENERGY", "GLOBAL EFFICIENCY", "75", "%");
    number(f, tr("Energy price"), "ENERGY", "GLOBAL PRICE", "0", tr("per kWh"));
    auto* price = choice(f, tr("Price pattern"), "ENERGY", "GLOBAL PATTERN", {QString()}, QString());
    price->setEditable(true);
    number(f, tr("Demand charge"), "ENERGY", "DEMAND CHARGE", "0", tr("per max kW"));

    f = addPage("report", tr("Reporting"));
    choice(f, tr("Status report"), "REPORT", "STATUS", {"NO", "YES", "FULL"}, "NO");
    choice(f, tr("Summary"), "REPORT", "SUMMARY", {"YES", "NO"}, "YES");
    choice(f, tr("Energy report"), "REPORT", "ENERGY", {"NO", "YES"}, "NO");
}

void OptionsDialog::accept()
{
    std::vector<const Field*> changed;
    for (const auto& f : fields_)
    {
        if (f.state() != f.initial)
            changed.push_back(&f);
    }
    changed_ = static_cast<int>(changed.size());
    if (!changed.empty())
    {
        doc_->edit(tr("Edit simulation options"), [&](core::Model& m, std::string*) {
            for (const auto* f : changed)
                f->write(m.document());
            m.rebuild();
            return true;
        });
    }
    QDialog::accept();
}

} // namespace mirab::app
