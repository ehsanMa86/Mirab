/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/WaterLinkDialog.cpp
 * Purpose: Wastewater from a water model: the link dialog of a SWMM project and the linked network layer on the map.
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
#include "WaterLinkDialog.h"

#include "Language.h"
#include "MapView.h"
#include "mirab/core/Crs.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace fs = std::filesystem;

namespace mirab::app
{

namespace
{

const QColor kWaterColor(37, 110, 200);
const QColor kLinkColor(214, 120, 30);
const QColor kMissingColor(200, 50, 50);

/// Converts points between two coordinate systems; keeps them when either is
/// unknown or both are the same.
class Projector
{
public:
    Projector(const std::string& from, const std::string& to)
    {
        if (from.empty() || to.empty())
            return;
        from_ = crs::parse(from);
        to_ = crs::parse(to);
        if (!from_ || !to_ || crs::toString(*from_) == crs::toString(*to_))
        {
            from_.reset();
            to_.reset();
        }
    }
    core::Point operator()(core::Point p) const
    {
        if (from_ && to_)
            crs::transform(*from_, *to_, p.x, p.y, p.x, p.y);
        return p;
    }

private:
    std::optional<crs::Crs> from_, to_;
};

std::vector<std::vector<core::Point>> networkLines(const core::Model& m, const Projector& project)
{
    std::vector<std::vector<core::Point>> out;
    out.reserve(m.links().size());
    for (const auto& l : m.links())
    {
        const auto* a = m.findNode(l.from);
        const auto* b = m.findNode(l.to);
        if (!a || !b || !a->placed || !b->placed)
            continue;
        std::vector<core::Point> line = {project(a->pos)};
        for (const auto& v : l.vertices)
            line.push_back(project(v));
        line.push_back(project(b->pos));
        out.push_back(std::move(line));
    }
    return out;
}

QString flowText(double v)
{
    return QString::number(v, 'g', 4);
}

QString categoryLabel(const std::string& c)
{
    return c.empty() ? QObject::tr("(none)") : QString::fromStdString(c);
}

QString statusText(core::NodeAssignment::Status s)
{
    using S = core::NodeAssignment::Status;
    switch (s)
    {
    case S::Matched:
        return QObject::tr("Nearest node");
    case S::Manual:
        return QObject::tr("Chosen");
    case S::TooFar:
        return QObject::tr("Farther than the distance");
    case S::NoCoordinates:
        return QObject::tr("No coordinates");
    case S::OutsideAreas:
        return QObject::tr("Outside the service areas");
    case S::NoCandidate:
        return QObject::tr("No SWMM node in reach");
    case S::Excluded:
        return QObject::tr("Not connected");
    }
    return {};
}

QString mapUnit(const std::string& crsText)
{
    if (const auto c = crs::parse(crsText); c && !c->isGeographic())
        return std::fabs(c->unitToMeters - 1.0) < 1e-9 ? QObject::tr("m") : QObject::tr("ft");
    return {};
}

QTableWidget* makeTable(const QStringList& headers, QWidget* parent)
{
    auto* t = new QTableWidget(parent);
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

QTableWidgetItem* cell(const QString& text, bool number = false)
{
    auto* item = new QTableWidgetItem(text);
    if (number)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

LinkPreview::LinkPreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(280, 240);
    setAutoFillBackground(false);
}

void LinkPreview::setData(const core::Model* swmm, std::vector<std::vector<core::Point>> waterLines,
                          std::vector<core::NodeAssignment> assignments)
{
    swmm_ = swmm;
    waterLines_ = std::move(waterLines);
    assignments_ = std::move(assignments);
    update();
}

void LinkPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setLayoutDirection(Qt::LayoutDirectionAuto); // a map: see ChartWidget::draw()
    p.fillRect(rect(), QColor(250, 251, 253));
    p.setPen(QColor(214, 220, 228));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!swmm_)
        return;
    core::Bounds b;
    for (const auto& n : swmm_->nodes())
    {
        if (n.placed)
            b.include(n.pos);
    }
    for (const auto& a : assignments_)
    {
        if (a.placed)
            b.include(a.waterPos);
    }
    if (!b.valid)
        return;
    const QRectF area = QRectF(rect()).adjusted(14, 14, -14, -34);
    const double w = std::max(b.width(), 1e-9), h = std::max(b.height(), 1e-9);
    const double scale = std::min(area.width() / w, area.height() / h);
    const double ox = area.center().x() - (b.minX + w / 2) * scale;
    const double oy = area.center().y() + (b.minY + h / 2) * scale;
    auto at = [&](const core::Point& q) { return QPointF(ox + q.x * scale, oy - q.y * scale); };

    QColor faint = kWaterColor;
    faint.setAlpha(70);
    p.setPen(QPen(faint, 1.0));
    for (const auto& line : waterLines_)
    {
        QPolygonF poly;
        for (const auto& q : line)
            poly << at(q);
        p.drawPolyline(poly);
    }
    p.setPen(QPen(QColor(110, 120, 135), 1.6));
    for (const auto& l : swmm_->links())
    {
        const auto* a = swmm_->findNode(l.from);
        const auto* c = swmm_->findNode(l.to);
        if (!a || !c || !a->placed || !c->placed)
            continue;
        QPolygonF poly;
        poly << at(a->pos);
        for (const auto& v : l.vertices)
            poly << at(v);
        poly << at(c->pos);
        p.drawPolyline(poly);
    }
    p.setPen(QPen(kLinkColor, 1.3, Qt::DashLine));
    for (const auto& a : assignments_)
    {
        const auto* n = a.swmmNode.empty() ? nullptr : swmm_->findNode(a.swmmNode);
        if (a.placed && n && n->placed)
            p.drawLine(at(a.waterPos), at(n->pos));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 70, 85));
    for (const auto& n : swmm_->nodes())
    {
        if (n.placed)
            p.drawEllipse(at(n.pos), 2.6, 2.6);
    }
    for (const auto& a : assignments_)
    {
        if (!a.placed)
            continue;
        p.setBrush(a.swmmNode.empty() ? kMissingColor : kWaterColor);
        p.drawEllipse(at(a.waterPos), 3.2, 3.2);
    }
    // Legend
    p.setFont(font());
    const QFontMetrics fm(font());
    const int y = height() - 10;
    int x = 10;
    auto key = [&](const QColor& c, const QString& label, bool line)
    {
        if (line)
        {
            p.setPen(QPen(c, 1.5, Qt::DashLine));
            p.drawLine(QPointF(x, y - 4), QPointF(x + 14, y - 4));
        }
        else
        {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(x + 7, y - 4), 3.2, 3.2);
        }
        p.setPen(QColor(70, 80, 95));
        p.drawText(QPointF(x + 18, y), label);
        x += 30 + fm.horizontalAdvance(label);
    };
    key(kWaterColor, tr("Water node"), false);
    key(kMissingColor, tr("Not connected"), false);
    key(kLinkColor, tr("Link"), true);
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

WaterLinkDialog::WaterLinkDialog(ProjectDocument* doc, QWidget* parent) : QDialog(parent), doc_(doc)
{
    setWindowTitle(tr("Wastewater from water model"));
    resize(1060, 760);
    initial_ = core::WaterLinkSettings::fromJson(doc_->settings().waterLink);
    overrides_ = initial_.overrides;
    const auto& swmm = doc_->model();

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Turn the water use of an EPANET model of the same area into dry-weather "
                                "inflow: each water node drains to a SWMM node, and the water it uses, "
                                "times a return factor, enters the sewer with the hourly pattern of its "
                                "demand."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("Water model"), this));
    water_ = new QLineEdit(this);
    water_->setPlaceholderText(tr("EPANET input file (.inp)"));
    if (!initial_.waterModel.empty())
        water_->setText(resolved(initial_.waterModel));
    auto* browse = new QPushButton(tr("Browse..."), this);
    fileRow->addWidget(water_, 1);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);
    waterInfo_ = new QLabel(this);
    waterInfo_->setObjectName("muted");
    waterInfo_->setWordWrap(true);
    layout->addWidget(waterInfo_);

    auto* options = new QHBoxLayout;
    // Matching
    auto* match = new QGroupBox(tr("Matching"), this);
    auto* mf = new QFormLayout(match);
    distance_ = new QDoubleSpinBox(match);
    distance_->setRange(0, 1e7);
    distance_->setDecimals(1);
    distance_->setSingleStep(10);
    distance_->setSpecialValueText(tr("No limit"));
    const QString unit = mapUnit(doc_->settings().crs);
    if (!unit.isEmpty())
        distance_->setSuffix(" " + unit);
    distance_->setValue(initial_.maxDistance);
    mf->addRow(tr("Maximum distance"), distance_);
    tag_ = new QComboBox(match);
    tag_->setEditable(true);
    tag_->addItem(QString());
    std::set<std::string> tags;
    if (const auto* s = swmm.document().find("TAGS"))
    {
        for (const auto& t : s->allRowTokens())
        {
            if (t.size() >= 3 && text::iequals(t[0], "Node"))
                tags.insert(text::unquote(t[2]));
        }
    }
    for (const auto& t : tags)
        tag_->addItem(QString::fromStdString(t));
    tag_->setCurrentText(QString::fromStdString(initial_.nodeTag));
    tag_->lineEdit()->setPlaceholderText(tr("Any node"));
    mf->addRow(tr("Only nodes tagged"), tag_);
    areas_ = new QComboBox(match);
    areas_->addItem(tr("None"), QString());
    areas_->addItem(tr("Subcatchments"), QString("subcatchments"));
    if (!initial_.serviceAreas.empty() && !text::iequals(initial_.serviceAreas, "subcatchments"))
        areas_->addItem(QFileInfo(QString::fromStdString(initial_.serviceAreas)).fileName(),
                        resolved(initial_.serviceAreas));
    areas_->addItem(tr("Polygon file..."), QString("?"));
    areas_->setCurrentIndex(text::iequals(initial_.serviceAreas, "subcatchments") ? 1
                            : initial_.serviceAreas.empty()                      ? 0
                                                                                 : 2);
    mf->addRow(tr("Service areas"), areas_);
    storage_ = new QCheckBox(tr("Storage units can receive flow"), match);
    storage_->setChecked(initial_.storage);
    mf->addRow(storage_);
    options->addWidget(match);

    // Return factors
    auto* ret = new QGroupBox(tr("Return to the sewer"), this);
    auto* rl = new QVBoxLayout(ret);
    auto* rf = new QFormLayout;
    factor_ = new QDoubleSpinBox(ret);
    factor_->setRange(0, 1.5);
    factor_->setDecimals(2);
    factor_->setSingleStep(0.05);
    factor_->setValue(initial_.returnFactor);
    rf->addRow(tr("Return factor"), factor_);
    rl->addLayout(rf);
    categories_ = new QTableWidget(ret);
    categories_->setColumnCount(3);
    categories_->setHorizontalHeaderLabels({tr("Category"), tr("Demand"), tr("Factor")});
    categories_->verticalHeader()->setVisible(false);
    categories_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    categories_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    categories_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    categories_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                                 QAbstractItemView::SelectedClicked);
    categories_->setMinimumHeight(90);
    rl->addWidget(categories_, 1);
    options->addWidget(ret, 1);

    // Patterns
    auto* pat = new QGroupBox(tr("Flow and patterns"), this);
    auto* pf = new QFormLayout(pat);
    source_ = new QComboBox(pat);
    source_->addItem(tr("Base demands and patterns"));
    source_->addItem(tr("Simulation results"));
    source_->setCurrentIndex(initial_.results.empty() ? 0 : 1);
    pf->addRow(tr("Water use from"), source_);
    auto* resultsRow = new QHBoxLayout;
    results_ = new QLineEdit(pat);
    results_->setPlaceholderText(tr("EPANET results (.anr)"));
    if (!initial_.results.empty())
        results_->setText(resolved(initial_.results));
    resultsBrowse_ = new QPushButton(tr("Browse..."), pat);
    resultsRow->addWidget(results_, 1);
    resultsRow->addWidget(resultsBrowse_);
    pf->addRow(tr("Results"), resultsRow);
    auto* weekRow = new QHBoxLayout;
    weekend_ = new QCheckBox(tr("Weekend pattern"), pat);
    weekend_->setChecked(initial_.weekend);
    weekStart_ = new QComboBox(pat);
    for (const QString& d : {tr("Monday"), tr("Tuesday"), tr("Wednesday"), tr("Thursday"), tr("Friday"),
                             tr("Saturday"), tr("Sunday")})
        weekStart_->addItem(tr("starts %1").arg(d));
    weekStart_->setCurrentIndex(initial_.weekStart);
    weekStart_->setToolTip(tr("Day of the week at the start of the EPANET patterns or results"));
    weekRow->addWidget(weekend_);
    weekRow->addWidget(weekStart_, 1);
    pf->addRow(tr("Weekly sources"), weekRow);
    lag_ = new QDoubleSpinBox(pat);
    lag_->setRange(-12, 24);
    lag_->setDecimals(2);
    lag_->setSingleStep(0.25);
    lag_->setSuffix(tr(" h"));
    lag_->setValue(initial_.lagHours);
    lag_->setToolTip(
        tr("Travel time from the buildings to the sewer node; the patterns are shifted by it"));
    pf->addRow(tr("Travel time"), lag_);
    prefix_ = new QLineEdit(QString::fromStdString(initial_.patternPrefix), pat);
    pf->addRow(tr("Pattern prefix"), prefix_);
    options->addWidget(pat);
    layout->addLayout(options);

    // Preview
    auto* split = new QSplitter(Qt::Horizontal, this);
    auto* tabs = new QTabWidget(split);
    const QString units = QString::fromStdString(swmm.units().flow);
    inflows_ = makeTable({tr("SWMM node"), tr("Average flow (%1)").arg(units), tr("Hourly pattern"),
                          tr("Water nodes")},
                         tabs);
    tabs->addTab(inflows_, tr("SWMM nodes"));
    auto* nodesPage = new QWidget(tabs);
    auto* nl = new QVBoxLayout(nodesPage);
    nl->setContentsMargins(0, 4, 0, 0);
    nodes_ = makeTable({tr("Water node"), tr("SWMM node"), tr("Distance"), tr("Demand (%1)").arg(units),
                        tr("Returned (%1)").arg(units), tr("Status")},
                       nodesPage);
    nodes_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    nl->addWidget(nodes_, 1);
    auto* overrideRow = new QHBoxLayout;
    auto* connectTo = new QPushButton(tr("Connect to..."), nodesPage);
    auto* exclude = new QPushButton(tr("Do not connect"), nodesPage);
    auto* automatic = new QPushButton(tr("Nearest node"), nodesPage);
    connectTo->setToolTip(tr("Send the selected water nodes to a SWMM node of your choice"));
    overrideRow->addWidget(connectTo);
    overrideRow->addWidget(exclude);
    overrideRow->addWidget(automatic);
    overrideRow->addStretch(1);
    nl->addLayout(overrideRow);
    tabs->addTab(nodesPage, tr("Water nodes"));
    preview_ = new LinkPreview(split);
    split->addWidget(tabs);
    split->addWidget(preview_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    apply_ = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    apply_->setProperty("primary", true);
    apply_->setToolTip(tr("Write the dry-weather inflows ([DWF]) and their patterns, and keep the link"));
    layout->addWidget(buttons);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(150);
    connect(timer_, &QTimer::timeout, this, &WaterLinkDialog::recompute);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &WaterLinkDialog::apply);
    connect(browse, &QPushButton::clicked, this, &WaterLinkDialog::browseWater);
    connect(resultsBrowse_, &QPushButton::clicked, this, &WaterLinkDialog::browseResults);
    connect(water_, &QLineEdit::editingFinished, this, &WaterLinkDialog::schedule);
    connect(results_, &QLineEdit::editingFinished, this, &WaterLinkDialog::schedule);
    connect(prefix_, &QLineEdit::editingFinished, this, &WaterLinkDialog::schedule);
    connect(tag_, &QComboBox::currentTextChanged, this, &WaterLinkDialog::schedule);
    connect(areas_, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (areas_->currentData().toString() == "?")
                    chooseAreas();
                schedule();
            });
    for (QDoubleSpinBox* s : {distance_, lag_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &WaterLinkDialog::schedule);
    connect(factor_, &QDoubleSpinBox::valueChanged, this,
            [this](double v)
            {
                // Categories without their own factor follow the default.
                filling_ = true;
                for (int r = 0; r < categories_->rowCount(); ++r)
                {
                    auto* item = categories_->item(r, 2);
                    if (item && !item->data(Qt::UserRole).toBool())
                        item->setText(QString::number(v, 'g', 4));
                }
                filling_ = false;
                schedule();
            });
    connect(categories_, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item)
            {
                if (filling_)
                    return;
                if (item->column() == 2)
                    item->setData(Qt::UserRole, true);
                schedule();
            });
    connect(source_, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                results_->setEnabled(index == 1);
                resultsBrowse_->setEnabled(index == 1);
                if (index == 1 && results_->text().isEmpty() && !water_->text().isEmpty())
                {
                    const QFileInfo f(water_->text());
                    const QString anr = f.dir().filePath(f.completeBaseName() + ".anr");
                    if (QFileInfo::exists(anr))
                        results_->setText(anr);
                }
                schedule();
            });
    for (QCheckBox* c : {storage_, weekend_})
        connect(c, &QCheckBox::toggled, this, &WaterLinkDialog::schedule);
    connect(weekend_, &QCheckBox::toggled, weekStart_, &QWidget::setEnabled);
    connect(weekStart_, &QComboBox::currentIndexChanged, this, &WaterLinkDialog::schedule);
    connect(connectTo, &QPushButton::clicked, this, [this] { setOverride(0); });
    connect(exclude, &QPushButton::clicked, this, [this] { setOverride(1); });
    connect(automatic, &QPushButton::clicked, this, [this] { setOverride(2); });
    results_->setEnabled(source_->currentIndex() == 1);
    resultsBrowse_->setEnabled(source_->currentIndex() == 1);
    weekStart_->setEnabled(weekend_->isChecked());
    recompute();
}

QString WaterLinkDialog::stored(const QString& file) const
{
    if (doc_->path().isEmpty())
        return QFileInfo(file).absoluteFilePath();
    return QString::fromStdString(interop::storedLinkPath(fs::u8path(doc_->path().toStdString()),
                                                          fs::u8path(file.toStdString())));
}

QString WaterLinkDialog::resolved(const std::string& stored) const
{
    return QString::fromStdString(
        interop::resolveLinkPath(fs::u8path(doc_->path().toStdString()), stored).u8string());
}

void WaterLinkDialog::browseWater()
{
    const QString start =
        water_->text().isEmpty() ? QFileInfo(doc_->path()).absolutePath() : water_->text();
    const QString f = QFileDialog::getOpenFileName(this, tr("Water distribution model"), start,
                                                   tr("EPANET input files (*.inp);;All files (*)"));
    if (f.isEmpty())
        return;
    water_->setText(QDir::toNativeSeparators(f));
    schedule();
}

void WaterLinkDialog::browseResults()
{
    const QString start =
        results_->text().isEmpty() ? QFileInfo(water_->text()).absolutePath() : results_->text();
    const QString f = QFileDialog::getOpenFileName(this, tr("EPANET results"), start,
                                                   tr("Results (*.anr *.out *.bin);;All files (*)"));
    if (f.isEmpty())
        return;
    results_->setText(QDir::toNativeSeparators(f));
    schedule();
}

void WaterLinkDialog::chooseAreas()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Service areas"),
                                                   QFileInfo(doc_->path()).absolutePath(),
                                                   tr("Polygons (*.shp *.dxf);;All files (*)"));
    const QSignalBlocker block(areas_);
    if (f.isEmpty())
    {
        areas_->setCurrentIndex(0);
        return;
    }
    if (areas_->count() == 4)
    {
        areas_->setItemText(2, QFileInfo(f).fileName());
        areas_->setItemData(2, f);
    }
    else
    {
        areas_->insertItem(2, QFileInfo(f).fileName(), f);
    }
    areas_->setCurrentIndex(2);
}

void WaterLinkDialog::schedule()
{
    timer_->start();
}

core::WaterLinkSettings WaterLinkDialog::collect() const
{
    core::WaterLinkSettings s = initial_;
    const QString water = water_->text().trimmed();
    s.waterModel = water.isEmpty() ? std::string() : stored(water).toStdString();
    s.maxDistance = distance_->value();
    s.nodeTag = tag_->currentText().trimmed().toStdString();
    const QString areas = areas_->currentData().toString();
    s.serviceAreas = areas.isEmpty() || areas == "?" ? std::string()
                     : areas == "subcatchments"     ? std::string("subcatchments")
                                                    : stored(areas).toStdString();
    s.storage = storage_->isChecked();
    s.returnFactor = factor_->value();
    s.categoryFactors.clear();
    s.categories.clear();
    bool all = true;
    std::vector<std::string> included;
    for (int r = 0; r < categories_->rowCount(); ++r)
    {
        const auto* name = categories_->item(r, 0);
        const auto* factor = categories_->item(r, 2);
        if (!name || !factor)
            continue;
        const std::string category = name->data(Qt::UserRole).toString().toStdString();
        if (name->checkState() == Qt::Checked)
            included.push_back(category);
        else
            all = false;
        if (factor->data(Qt::UserRole).toBool())
        {
            bool ok = false;
            const double v = factor->text().toDouble(&ok);
            if (ok && v >= 0)
                s.categoryFactors[category] = v;
        }
    }
    if (!all && !included.empty())
        s.categories = included;
    else if (!all)
    {
        for (int r = 0; r < categories_->rowCount(); ++r)
            s.categoryFactors[categories_->item(r, 0)->data(Qt::UserRole).toString().toStdString()] = 0.0;
    }
    s.results = source_->currentIndex() == 1 && !results_->text().trimmed().isEmpty()
                    ? stored(results_->text().trimmed()).toStdString()
                    : std::string();
    s.weekend = weekend_->isChecked();
    s.weekStart = weekStart_->currentIndex();
    s.lagHours = lag_->value();
    s.patternPrefix = prefix_->text().trimmed().toStdString();
    s.overrides = overrides_;
    return s;
}

bool WaterLinkDialog::loadWater()
{
    const auto s = collect();
    const std::string key =
        s.waterModel + "|" + s.results + "|" + s.serviceAreas + "|" + doc_->settings().crs;
    if (linked_ && key == loadedKey_)
        return true;
    linked_.reset();
    loadedKey_.clear();
    try
    {
        linked_ = interop::loadLinkedModels(fs::u8path(doc_->path().toStdString()), doc_->model(),
                                            doc_->settings().crs, s);
    }
    catch (const std::exception& e)
    {
        waterInfo_->setText(QString::fromUtf8(e.what()));
        return false;
    }
    loadedKey_ = key;
    const auto& w = linked_->water;
    int junctions = 0;
    for (const auto& n : w.nodes())
        junctions += n.kind == "Junction" ? 1 : 0;
    QString info = tr("%1: %2 junctions, flow in %3")
                       .arg(QString::fromStdString(w.title().empty()
                                                       ? linked_->waterPath.filename().u8string()
                                                       : w.title()))
                       .arg(junctions)
                       .arg(QString::fromStdString(w.units().flow));
    info += linked_->context.waterCrs.empty()
                ? tr(", no coordinate system")
                : tr(", %1").arg(QString::fromStdString(linked_->context.waterCrs));
    if (linked_->results)
        info += tr("; results of %n period(s)", nullptr, linked_->results->periodCount());
    waterInfo_->setText(info);
    fillCategories();
    return true;
}

void WaterLinkDialog::fillCategories()
{
    std::map<std::string, double> totals;
    for (const auto& d : core::waterDemands(linked_->water))
        totals[d.category] += d.base;
    filling_ = true;
    categories_->setRowCount(0);
    int r = 0;
    for (const auto& [category, total] : totals)
    {
        categories_->insertRow(r);
        auto* name = new QTableWidgetItem(categoryLabel(category));
        name->setData(Qt::UserRole, QString::fromStdString(category));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        const bool included = initial_.categories.empty() ||
                               std::find(initial_.categories.begin(), initial_.categories.end(), category) !=
                                   initial_.categories.end();
        name->setCheckState(included ? Qt::Checked : Qt::Unchecked);
        categories_->setItem(r, 0, name);
        auto* demand = cell(QString::number(total, 'g', 5), true);
        demand->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        categories_->setItem(r, 1, demand);
        const auto own = initial_.categoryFactors.find(category);
        const bool explicitFactor = own != initial_.categoryFactors.end();
        auto* factor =
            cell(QString::number(explicitFactor ? own->second : factor_->value(), 'g', 4), true);
        factor->setData(Qt::UserRole, explicitFactor);
        factor->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        factor->setToolTip(tr("Share of this category's water that reaches the sewer"));
        categories_->setItem(r, 2, factor);
        ++r;
    }
    filling_ = false;
}

void WaterLinkDialog::recompute()
{
    valid_ = false;
    apply_->setEnabled(false);
    inflows_->setRowCount(0);
    nodes_->setRowCount(0);
    if (water_->text().trimmed().isEmpty())
    {
        waterInfo_->setText(tr("Choose the EPANET model of the same area."));
        summary_->clear();
        preview_->setData(&doc_->model(), {}, {});
        return;
    }
    if (!loadWater())
    {
        summary_->clear();
        preview_->setData(&doc_->model(), {}, {});
        return;
    }
    const auto s = collect();
    try
    {
        plan_ = core::planDwf(linked_->water, doc_->model(), s, linked_->context);
    }
    catch (const std::exception& e)
    {
        summary_->setText(QString::fromUtf8(e.what()));
        preview_->setData(&doc_->model(), {}, {});
        return;
    }
    const Projector project(linked_->context.waterCrs, linked_->context.swmmCrs);
    preview_->setData(&doc_->model(), networkLines(linked_->water, project), plan_.assignments);

    inflows_->setRowCount(static_cast<int>(plan_.inflows.size()));
    for (int i = 0; i < static_cast<int>(plan_.inflows.size()); ++i)
    {
        const auto& in = plan_.inflows[static_cast<std::size_t>(i)];
        QString pattern = QString::fromStdString(in.hourly);
        if (!in.weekend.empty())
            pattern += ", " + QString::fromStdString(in.weekend);
        if (!in.daily.empty())
            pattern += ", " + QString::fromStdString(in.daily);
        QStringList sources;
        for (const auto& id : in.sources)
            sources << QString::fromStdString(id);
        inflows_->setItem(i, 0, cell(QString::fromStdString(in.node)));
        inflows_->setItem(i, 1, cell(flowText(in.average), true));
        inflows_->setItem(i, 2, cell(pattern.isEmpty() ? tr("constant") : pattern));
        inflows_->setItem(i, 3, cell(sources.join(", ")));
    }
    inflows_->resizeColumnsToContents();

    nodes_->setRowCount(static_cast<int>(plan_.assignments.size()));
    for (int i = 0; i < static_cast<int>(plan_.assignments.size()); ++i)
    {
        const auto& a = plan_.assignments[static_cast<std::size_t>(i)];
        const bool missing = a.swmmNode.empty();
        const QString distance = a.distance >= 0 ? QString::number(a.distance, 'f', 1) : QString();
        const QStringList cells = {QString::fromStdString(a.waterNode),
                                   missing && !a.nearest.empty()
                                       ? tr("(%1)").arg(QString::fromStdString(a.nearest))
                                       : QString::fromStdString(a.swmmNode),
                                   distance,
                                   flowText(a.demand),
                                   flowText(a.returned),
                                   statusText(a.status)};
        for (int c = 0; c < cells.size(); ++c)
        {
            auto* item = cell(cells[c], c >= 2 && c <= 4);
            if (missing && a.status != core::NodeAssignment::Status::Excluded)
                item->setForeground(QColor(180, 70, 30));
            else if (a.status == core::NodeAssignment::Status::Manual)
                item->setForeground(QColor(30, 90, 170));
            nodes_->setItem(i, c, item);
        }
    }
    nodes_->resizeColumnsToContents();

    const QString units = QString::fromStdString(plan_.flowUnits);
    QString summary = tr("Water demand %1 %2; %3 %2 returns to the sewer at %4 SWMM node(s).")
                          .arg(flowText(plan_.totalDemand), units, flowText(plan_.returned))
                          .arg(plan_.inflows.size());
    if (plan_.unassigned > 0)
    {
        summary += " " + tr("%1 of %2 water nodes are not connected (%3 %4 not returned)")
                             .arg(plan_.unassigned)
                             .arg(plan_.assignments.size())
                             .arg(flowText(plan_.unassignedFlow), units);
        if (plan_.tooFar > 0)
            summary += tr(", %n farther than the distance", nullptr, plan_.tooFar);
        summary += ".";
    }
    for (const auto& n : plan_.notes)
        summary += " " + coreText(n);
    summary_->setText(summary);
    valid_ = true;
    apply_->setEnabled(!plan_.inflows.empty());
}

void WaterLinkDialog::setOverride(int mode)
{
    std::set<int> rows;
    for (const auto* item : nodes_->selectedItems())
        rows.insert(item->row());
    if (rows.empty() || !valid_)
        return;
    std::string target;
    if (mode == 0)
    {
        QStringList ids;
        for (const auto& n : doc_->model().nodes())
        {
            if (n.kind == "Junction" || n.kind == "Storage")
                ids << QString::fromStdString(n.id);
        }
        bool ok = false;
        const auto& current = plan_.assignments[static_cast<std::size_t>(*rows.begin())];
        const QString first = QString::fromStdString(current.swmmNode);
        const QString choice = QInputDialog::getItem(this, tr("Connect to"), tr("SWMM node"), ids,
                                                     std::max(0, static_cast<int>(ids.indexOf(first))),
                                                     true, &ok);
        if (!ok || choice.trimmed().isEmpty())
            return;
        if (!doc_->model().findNode(choice.trimmed().toStdString()))
        {
            QMessageBox::warning(this, windowTitle(), tr("The SWMM model has no node %1.").arg(choice));
            return;
        }
        target = choice.trimmed().toStdString();
    }
    for (int r : rows)
    {
        const std::string id = plan_.assignments[static_cast<std::size_t>(r)].waterNode;
        if (mode == 2)
            overrides_.erase(id);
        else
            overrides_[id] = target;
    }
    recompute();
}

void WaterLinkDialog::apply()
{
    if (timer_->isActive())
    {
        timer_->stop();
        recompute();
    }
    if (!valid_)
        return;
    auto s = collect();
    s.links.clear();
    for (const auto& a : plan_.assignments)
    {
        if (!a.swmmNode.empty())
            s.links[a.waterNode] = a.swmmNode;
    }
    const auto plan = plan_;
    const std::string prefix = s.patternPrefix;
    QString err;
    int changed = 0;
    const bool edited = doc_->edit(
        tr("Wastewater from water model"),
        [&](core::Model& m, std::string*)
        {
            changed = core::applyDwf(m, plan, prefix);
            return changed > 0;
        },
        &err);
    if (!edited && !err.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    doc_->settings().waterLink = s.toJson();
    doc_->markSettingsChanged();
    emit linkChanged();
    accept();
}

// ---------------------------------------------------------------------------
// Linked network layer
// ---------------------------------------------------------------------------

LinkedNetworkLayer::LinkedNetworkLayer(ProjectDocument* doc, MapView* map, QObject* parent)
    : QObject(parent), doc_(doc), map_(map)
{
}

bool LinkedNetworkLayer::available() const
{
    if (doc_->model().dialect() == core::Dialect::Swmm)
        return !core::WaterLinkSettings::fromJson(doc_->settings().waterLink).waterModel.empty();
    return doc_->model().dialect() == core::Dialect::Epanet;
}

void LinkedNetworkLayer::clear()
{
    map_->removeOverlay("linked-network");
    map_->removeOverlay("linked-nodes");
}

void LinkedNetworkLayer::setVisible(bool on)
{
    visible_ = on;
    if (doc_->model().dialect() == core::Dialect::Swmm)
    {
        auto s = core::WaterLinkSettings::fromJson(doc_->settings().waterLink);
        if (!s.waterModel.empty() && s.showReference != on)
        {
            s.showReference = on;
            doc_->settings().waterLink = s.toJson();
            doc_->markSettingsChanged();
        }
    }
    refresh();
}

bool LinkedNetworkLayer::chooseSewerModel(QWidget* parent)
{
    const QString start = sewerFile_.isEmpty() ? QFileInfo(doc_->path()).absolutePath() : sewerFile_;
    const QString f = QFileDialog::getOpenFileName(parent, tr("Sewer model"), start,
                                                   tr("SWMM input files (*.inp);;All files (*)"));
    if (f.isEmpty())
        return false;
    sewerFile_ = f;
    visible_ = true;
    refresh();
    return true;
}

void LinkedNetworkLayer::refresh()
{
    if (project_ != doc_->path())
    {
        project_ = doc_->path();
        sewerFile_.clear();
        visible_ = true;
    }
    clear();
    const auto& model = doc_->model();
    const fs::path projectPath = fs::u8path(doc_->path().toStdString());
    QColor faint = kWaterColor;
    faint.setAlpha(150);
    MapView::Overlay network;
    network.lineWidth = 2.0;
    MapView::Overlay nodes;
    nodes.aboveLinks = true;
    nodes.lineColor = kLinkColor;
    nodes.lineStyle = Qt::DashLine;
    nodes.lineWidth = 2.4;
    nodes.pointSize = 6.0;
    try
    {
        if (model.dialect() == core::Dialect::Swmm)
        {
            auto s = core::WaterLinkSettings::fromJson(doc_->settings().waterLink);
            visible_ = s.showReference;
            if (s.waterModel.empty() || !visible_)
                return;
            s.results.clear();
            s.serviceAreas.clear();
            auto linked = interop::loadLinkedModels(projectPath, model, doc_->settings().crs, s);
            const Projector project(linked->context.waterCrs, linked->context.swmmCrs);
            network.lines = networkLines(linked->water, project);
            network.lineColor = faint;
            std::map<std::string, std::string> links = s.links;
            if (links.empty())
            {
                for (const auto& a : core::matchWaterNodes(linked->water, model, s, linked->context))
                {
                    if (!a.swmmNode.empty())
                        links[a.waterNode] = a.swmmNode;
                }
            }
            QColor point = kWaterColor;
            point.setAlpha(200);
            nodes.pointColor = point;
            for (const auto& [waterId, swmmId] : links)
            {
                const auto* w = linked->water.findNode(waterId);
                const auto* n = model.findNode(swmmId);
                if (!w || !w->placed)
                    continue;
                const core::Point at = project(w->pos);
                nodes.points.push_back(at);
                if (n && n->placed)
                    nodes.lines.push_back({at, n->pos});
            }
        }
        else if (model.dialect() == core::Dialect::Epanet)
        {
            if (sewerFile_.isEmpty() || !visible_)
                return;
            const fs::path sewerPath = fs::u8path(sewerFile_.toStdString());
            const auto sewer = core::Model::fromFile(sewerPath);
            const auto sewerSettings = core::ProjectSettings::loadFor(sewerPath);
            const Projector project(sewerSettings.crs, doc_->settings().crs);
            network.lines = networkLines(sewer, project);
            network.lineColor = QColor(140, 95, 50, 120);
            nodes.pointColor = QColor(140, 95, 50, 170);
            nodes.pointSize = 4.0;
            for (const auto& n : sewer.nodes())
            {
                if (n.placed)
                    nodes.points.push_back(project(n.pos));
            }
            // Lines to the sewer nodes when that model is linked to this one.
            const auto s = core::WaterLinkSettings::fromJson(sewerSettings.waterLink);
            std::error_code ec;
            if (!s.waterModel.empty() &&
                fs::equivalent(interop::resolveLinkPath(sewerPath, s.waterModel), projectPath, ec))
            {
                for (const auto& [waterId, swmmId] : s.links)
                {
                    const auto* w = model.findNode(waterId);
                    const auto* n = sewer.findNode(swmmId);
                    if (w && n && w->placed && n->placed)
                        nodes.lines.push_back({w->pos, project(n->pos)});
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        emit map_->statusMessage(tr("Linked network: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    map_->setOverlay("linked-network", std::move(network));
    map_->setOverlay("linked-nodes", std::move(nodes));
}

} // namespace mirab::app
