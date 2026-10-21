/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/RasterDialogs.cpp
 * Purpose: Raster layer list and the elevations-from-DEM dialog.
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
#include "RasterDialogs.h"

#include "Icons.h"
#include "RasterLayers.h"
#include "mirab/core/Text.h"

#include <QButtonGroup>
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
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace mirab::app
{

namespace
{
/// Vertical units offered for a DEM: metres per unit, 0 = as declared in the file.
void fillUnits(QComboBox* c)
{
    c->addItem(QObject::tr("As declared in the file"), 0.0);
    c->addItem(QObject::tr("Metres"), 1.0);
    c->addItem(QObject::tr("Feet"), crs::kInternationalFoot);
    c->addItem(QObject::tr("US survey feet"), crs::kUsSurveyFoot);
}

void selectUnit(QComboBox* c, double unit)
{
    for (int i = 0; i < c->count(); ++i)
    {
        if (std::fabs(c->itemData(i).toDouble() - unit) < 1e-12)
            c->setCurrentIndex(i);
    }
}

QString describe(const raster::Raster& r)
{
    const auto& info = r.info();
    static const char* types[] = {"8-bit",  "8-bit signed",  "16-bit",       "16-bit signed",
                                  "32-bit", "32-bit signed", "32-bit float", "64-bit float"};
    QString s = QObject::tr("%1, %2 x %3 pixels, %n band(s) of %4", nullptr, info.bands)
                    .arg(QString::fromStdString(info.format))
                    .arg(info.width)
                    .arg(info.height)
                    .arg(QString::fromLatin1(types[static_cast<int>(info.type)]));
    if (!info.compression.empty() && info.compression != "none")
        s += QObject::tr(", %1 compression").arg(QString::fromStdString(info.compression));
    if (info.overviews > 0)
        s += QObject::tr(", %n overview(s)", nullptr, info.overviews);
    s += "\n";
    s += info.crs ? QObject::tr("Coordinate system: %1")
                        .arg(QString::fromStdString(info.crs->name.empty() ? info.crsDescription
                                                                           : info.crs->name))
                  : QObject::tr("Coordinate system: not declared (the project's is assumed)");
    if (!info.georeferenced)
        s += "\n" + QObject::tr("The file is not georeferenced.");
    if (info.noData)
        s += "\n" + QObject::tr("NoData value: %1").arg(*info.noData);
    if (info.verticalUnitToMeters > 0)
        s += "\n" + QObject::tr("Vertical units: %1 m").arg(info.verticalUnitToMeters, 0, 'g', 10);
    for (const auto& n : info.notes)
        s += "\n" + QString::fromStdString(n);
    return s;
}
} // namespace

// ---------------------------------------------------------------------------
// Raster layers
// ---------------------------------------------------------------------------
bool RasterLayersDialog::addLayer(ProjectDocument* doc, QWidget* parent, const QString& given)
{
    QString file = given;
    if (file.isEmpty())
    {
        const QString start =
            QSettings()
                .value("raster/lastDir", doc->path().isEmpty()
                                             ? QDir::homePath()
                                             : QFileInfo(doc->path()).absolutePath())
                .toString();
        file =
            QFileDialog::getOpenFileName(parent, tr("Add raster layer"), start, rasterFileFilter());
        if (file.isEmpty())
            return false;
        QSettings().setValue("raster/lastDir", QFileInfo(file).absolutePath());
    }
    QString err;
    auto r = openRaster(file, &err);
    if (!r)
    {
        QMessageBox::warning(parent, tr("Raster layer"), err);
        return false;
    }
    core::RasterLayerSettings layer;
    layer.path = storedRasterPath(doc, QFileInfo(file).absoluteFilePath());
    if (r->isImagery())
    {
        layer.style = "image";
        layer.opacity = 0.85;
    }
    else
    {
        layer.style = "hillshade-color";
        layer.opacity = 0.6;
    }
    doc->settings().rasters.push_back(layer);
    doc->markSettingsChanged();
    return true;
}

RasterLayersDialog::RasterLayersDialog(ProjectDocument* doc, QWidget* parent)
    : QDialog(parent), doc_(doc)
{
    setWindowTitle(tr("Raster layers"));
    resize(660, 540);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Elevation models and georeferenced images drawn under the network. GeoTIFF and "
           "ESRI ASCII grids are read; layers are reprojected to the project's coordinate "
           "system. The first elevation layer gives the ground under the cursor and in "
           "profile plots."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* row = new QHBoxLayout;
    list_ = new QListWidget(this);
    row->addWidget(list_, 1);
    auto* buttons = new QVBoxLayout;
    auto* add = new QPushButton(icons::get("new"), tr("Add..."), this);
    remove_ = new QPushButton(icons::get("delete"), tr("Remove"), this);
    up_ = new QPushButton(tr("Move up"), this);
    down_ = new QPushButton(tr("Move down"), this);
    buttons->addWidget(add);
    buttons->addWidget(remove_);
    buttons->addWidget(up_);
    buttons->addWidget(down_);
    buttons->addStretch(1);
    row->addLayout(buttons);
    layout->addLayout(row, 1);

    auto* form = new QFormLayout;
    style_ = new QComboBox(this);
    opacity_ = new QSlider(Qt::Horizontal, this);
    opacity_->setRange(0, 100);
    exaggeration_ = new QDoubleSpinBox(this);
    exaggeration_->setRange(0.1, 50.0);
    exaggeration_->setSingleStep(0.5);
    exaggeration_->setSuffix(QStringLiteral(" x"));
    units_ = new QComboBox(this);
    fillUnits(units_);
    info_ = new QLabel(this);
    info_->setObjectName("muted");
    info_->setWordWrap(true);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Style"), style_);
    form->addRow(tr("Opacity"), opacity_);
    form->addRow(tr("Hillshade exaggeration"), exaggeration_);
    form->addRow(tr("Elevation units"), units_);
    form->addRow(QString(), info_);
    layout->addLayout(form);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(add, &QPushButton::clicked, this,
            [this]
            {
                if (addLayer(doc_, this))
                {
                    fill(static_cast<int>(doc_->settings().rasters.size()) - 1);
                    emit layersChanged();
                }
            });
    connect(remove_, &QPushButton::clicked, this,
            [this]
            {
                const int i = list_->currentRow();
                auto& layers = doc_->settings().rasters;
                if (i < 0 || i >= static_cast<int>(layers.size()))
                    return;
                layers.erase(layers.begin() + i);
                doc_->markSettingsChanged();
                fill(std::min(i, static_cast<int>(layers.size()) - 1));
                emit layersChanged();
            });
    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });
    connect(list_, &QListWidget::currentRowChanged, this, &RasterLayersDialog::showCurrent);
    connect(list_, &QListWidget::itemChanged, this, [this] { store(); });
    connect(style_, &QComboBox::currentIndexChanged, this, [this] { store(); });
    connect(opacity_, &QSlider::valueChanged, this, [this] { store(); });
    connect(exaggeration_, &QDoubleSpinBox::valueChanged, this, [this] { store(); });
    connect(units_, &QComboBox::currentIndexChanged, this, [this] { store(); });
    fill(0);
}

void RasterLayersDialog::fill(int select)
{
    filling_ = true;
    list_->clear();
    for (const auto& l : doc_->settings().rasters)
    {
        // Drawn in list order: later layers cover earlier ones.
        auto* item = new QListWidgetItem(QFileInfo(QString::fromStdString(l.path)).fileName());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(resolveRasterPath(doc_, l.path));
        item->setIcon(icons::get(isElevationLayer(l) ? "profile" : "basemap"));
        list_->addItem(item);
    }
    filling_ = false;
    list_->setCurrentRow(std::max(select, list_->count() ? 0 : -1));
    showCurrent();
}

void RasterLayersDialog::showCurrent()
{
    if (filling_)
        return;
    filling_ = true;
    const int i = list_->currentRow();
    const auto& layers = doc_->settings().rasters;
    const bool has = i >= 0 && i < static_cast<int>(layers.size());
    for (QWidget* w : {static_cast<QWidget*>(style_), static_cast<QWidget*>(opacity_),
                       static_cast<QWidget*>(exaggeration_), static_cast<QWidget*>(units_),
                       static_cast<QWidget*>(remove_)})
        w->setEnabled(has);
    up_->setEnabled(has && i > 0);
    down_->setEnabled(has && i + 1 < static_cast<int>(layers.size()));
    style_->clear();
    info_->clear();
    if (has)
    {
        const auto& l = layers[static_cast<std::size_t>(i)];
        QString err;
        auto r = openRaster(resolveRasterPath(doc_, l.path), &err);
        const bool imagery = r && r->isImagery();
        if (imagery)
            style_->addItem(tr("Image"), "image");
        else
        {
            style_->addItem(tr("Hillshade"), "hillshade");
            style_->addItem(tr("Colour ramp"), "color");
            style_->addItem(tr("Colour ramp with hillshade"), "hillshade-color");
        }
        style_->setCurrentIndex(std::max(0, style_->findData(QString::fromStdString(l.style))));
        opacity_->setValue(static_cast<int>(std::lround(l.opacity * 100)));
        exaggeration_->setValue(l.exaggeration);
        exaggeration_->setEnabled(!imagery);
        units_->setEnabled(!imagery);
        selectUnit(units_, l.verticalUnit);
        info_->setText(r ? describe(*r) : err);
    }
    filling_ = false;
}

void RasterLayersDialog::store()
{
    if (filling_)
        return;
    auto& layers = doc_->settings().rasters;
    for (int i = 0; i < list_->count() && i < static_cast<int>(layers.size()); ++i)
        layers[static_cast<std::size_t>(i)].visible = list_->item(i)->checkState() == Qt::Checked;
    const int i = list_->currentRow();
    if (i >= 0 && i < static_cast<int>(layers.size()))
    {
        auto& l = layers[static_cast<std::size_t>(i)];
        if (style_->currentIndex() >= 0)
            l.style = style_->currentData().toString().toStdString();
        l.opacity = opacity_->value() / 100.0;
        l.exaggeration = exaggeration_->value();
        l.verticalUnit = units_->currentData().toDouble();
    }
    doc_->markSettingsChanged();
    emit layersChanged();
}

void RasterLayersDialog::move(int delta)
{
    auto& layers = doc_->settings().rasters;
    const int i = list_->currentRow(), j = i + delta;
    if (i < 0 || j < 0 || i >= static_cast<int>(layers.size()) ||
        j >= static_cast<int>(layers.size()))
        return;
    std::swap(layers[static_cast<std::size_t>(i)], layers[static_cast<std::size_t>(j)]);
    doc_->markSettingsChanged();
    fill(j);
    emit layersChanged();
}

// ---------------------------------------------------------------------------
// Elevations from a DEM
// ---------------------------------------------------------------------------
ElevationDialog::ElevationDialog(ProjectDocument* doc, QWidget* parent) : QDialog(parent), doc_(doc)
{
    const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
    setWindowTitle(tr("Elevations from DEM"));
    resize(760, 600);
    auto* layout = new QVBoxLayout(this);
    auto* intro =
        new QLabel(swmm ? tr("Sample a digital elevation model at the nodes. The ground is the rim "
                             "of junctions and storage "
                             "units: choose whether it sets their invert or their maximum depth.")
                        : tr("Sample a digital elevation model at the nodes and use the ground as "
                             "their elevation."),
                   this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    auto* demRow = new QHBoxLayout;
    dem_ = new QComboBox(this);
    for (const auto& l : doc_->settings().rasters)
    {
        if (isElevationLayer(l))
            dem_->addItem(QFileInfo(QString::fromStdString(l.path)).fileName(),
                          resolveRasterPath(doc_, l.path));
    }
    auto* browse = new QPushButton(tr("Browse..."), this);
    demRow->addWidget(dem_, 1);
    demRow->addWidget(browse);
    units_ = new QComboBox(this);
    fillUnits(units_);
    demInfo_ = new QLabel(this);
    demInfo_->setObjectName("muted");
    demInfo_->setWordWrap(true);
    form->addRow(tr("Elevation model"), demRow);
    form->addRow(tr("Elevation units"), units_);
    form->addRow(QString(), demInfo_);
    layout->addLayout(form);

    auto* options = new QHBoxLayout;
    auto* which = new QGroupBox(tr("Nodes"), this);
    auto* wl = new QVBoxLayout(which);
    all_ = new QRadioButton(tr("All nodes"), which);
    missing_ = new QRadioButton(
        swmm ? tr("Only nodes with a zero value") : tr("Only nodes with zero elevation"), which);
    int selectedNodes = 0;
    for (const auto& sel : doc_->selectedElements())
        selectedNodes += sel.category == core::Category::Node ? 1 : 0;
    selected_ = new QRadioButton(tr("Only the selected nodes (%1)").arg(selectedNodes), which);
    all_->setChecked(true);
    selected_->setEnabled(selectedNodes > 0);
    wl->addWidget(all_);
    wl->addWidget(missing_);
    wl->addWidget(selected_);
    options->addWidget(which);
    auto* kinds = new QGroupBox(tr("Values"), this);
    auto* kl = new QVBoxLayout(kinds);
    junctions_ = new QCheckBox(tr("Junctions"), kinds);
    tanks_ = new QCheckBox(swmm ? tr("Storage units") : tr("Tanks"), kinds);
    reservoirs_ = new QCheckBox(tr("Reservoir heads"), kinds);
    junctions_->setChecked(true);
    tanks_->setChecked(true);
    reservoirs_->setVisible(!swmm);
    kl->addWidget(junctions_);
    kl->addWidget(tanks_);
    kl->addWidget(reservoirs_);
    swmmMode_ = new QComboBox(kinds);
    swmmMode_->addItem(tr("Invert = ground - maximum depth"), 0);
    swmmMode_->addItem(tr("Maximum depth = ground - invert"), 1);
    swmmMode_->setVisible(swmm);
    kl->addWidget(swmmMode_);
    options->addWidget(kinds, 1);
    layout->addLayout(options);

    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({tr("Node"), tr("Type"), tr("Value"), tr("Current"),
                                       tr("Ground"), tr("New"), tr("Result")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    addLayer_ = new QCheckBox(tr("Add the elevation model to the map"), this);
    addLayer_->setVisible(false);
    layout->addWidget(addLayer_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    apply_ = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    apply_->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &ElevationDialog::apply);
    connect(browse, &QPushButton::clicked, this, &ElevationDialog::browse);
    connect(dem_, &QComboBox::currentIndexChanged, this,
            [this]
            {
                // A file picked here (not a layer of the project) can be added to the map.
                bool known = false;
                for (const auto& l : doc_->settings().rasters)
                    known =
                        known || resolveRasterPath(doc_, l.path) == dem_->currentData().toString();
                addLayer_->setVisible(!known && dem_->count() > 0);
                addLayer_->setChecked(true);
                for (const auto& l : doc_->settings().rasters)
                {
                    if (resolveRasterPath(doc_, l.path) == dem_->currentData().toString())
                        selectUnit(units_, l.verticalUnit);
                }
                recompute();
            });
    for (QComboBox* c : {units_, swmmMode_})
        connect(c, &QComboBox::currentIndexChanged, this, &ElevationDialog::recompute);
    for (QAbstractButton* b : std::initializer_list<QAbstractButton*>{
             all_, missing_, selected_, junctions_, tanks_, reservoirs_})
        connect(b, &QAbstractButton::toggled, this, &ElevationDialog::recompute);
    if (!doc_->settings().rasters.empty())
    {
        for (const auto& l : doc_->settings().rasters)
        {
            if (isElevationLayer(l) &&
                resolveRasterPath(doc_, l.path) == dem_->currentData().toString())
                selectUnit(units_, l.verticalUnit);
        }
    }
    recompute();
}

void ElevationDialog::browse()
{
    const QString start = QSettings()
                              .value("raster/lastDir", doc_->path().isEmpty()
                                                           ? QDir::homePath()
                                                           : QFileInfo(doc_->path()).absolutePath())
                              .toString();
    const QString f =
        QFileDialog::getOpenFileName(this, tr("Elevation model"), start, rasterFileFilter());
    if (f.isEmpty())
        return;
    QSettings().setValue("raster/lastDir", QFileInfo(f).absolutePath());
    const QString abs = QFileInfo(f).absoluteFilePath();
    int index = dem_->findData(abs);
    if (index < 0)
    {
        dem_->addItem(QFileInfo(f).fileName(), abs);
        index = dem_->count() - 1;
    }
    dem_->setCurrentIndex(index);
}

void ElevationDialog::recompute()
{
    plan_.clear();
    table_->setRowCount(0);
    apply_->setEnabled(false);
    if (dem_->count() == 0)
    {
        demInfo_->setText(tr("Choose a DEM (GeoTIFF or ESRI ASCII grid) with Browse."));
        summary_->clear();
        return;
    }
    QString err;
    auto r = openRaster(dem_->currentData().toString(), &err);
    if (!r)
    {
        demInfo_->setText(err);
        return;
    }
    if (r->isImagery())
    {
        demInfo_->setText(tr("This file holds colour imagery, not elevations."));
        return;
    }
    const auto units = doc_->model().units();
    const core::TerrainSampler terrain(r, doc_->settings().crs, units,
                                       units_->currentData().toDouble());
    QString info = describe(*r).section('\n', 0, 1);
    if (std::fabs(terrain.verticalFactor() - 1.0) > 1e-9)
        info += "\n" + tr("DEM values (1 unit = %1 m) are converted to %2.")
                           .arg(terrain.demUnitToMeters(), 0, 'g', 8)
                           .arg(QString::fromStdString(units.label("elevation")));
    for (const auto& n : terrain.notes())
        info += "\n" + QString::fromStdString(n);
    demInfo_->setText(info);

    core::ElevationOptions o;
    o.scope = all_->isChecked()       ? core::ElevationScope::All
              : missing_->isChecked() ? core::ElevationScope::Missing
                                      : core::ElevationScope::Selected;
    if (o.scope == core::ElevationScope::Selected)
    {
        for (const auto& sel : doc_->selectedElements())
            if (sel.category == core::Category::Node)
                o.selected.push_back(sel.id.toStdString());
    }
    o.junctions = junctions_->isChecked();
    o.tanks = tanks_->isChecked();
    o.reservoirs = reservoirs_->isChecked();
    o.swmmMode = swmmMode_->currentData().toInt() == 1 ? core::SwmmElevationMode::MaxDepth
                                                       : core::SwmmElevationMode::Invert;
    plan_ = core::planElevations(doc_->model(), terrain, o);

    const QString unit = QString::fromStdString(units.label("elevation"));
    auto num = [](const std::optional<double>& v)
    { return v ? QString::fromStdString(text::formatDouble(*v, 10)) : QString(); };
    int counts[6] = {};
    table_->setRowCount(static_cast<int>(plan_.size()));
    for (int i = 0; i < static_cast<int>(plan_.size()); ++i)
    {
        const auto& c = plan_[static_cast<std::size_t>(i)];
        ++counts[static_cast<int>(c.status)];
        QString result;
        switch (c.status)
        {
        case core::ElevationChange::Status::Update:
            result = tr("Change");
            break;
        case core::ElevationChange::Status::Same:
            result = tr("Unchanged");
            break;
        case core::ElevationChange::Status::Outside:
            result = tr("Outside the DEM");
            break;
        case core::ElevationChange::Status::NoData:
            result = tr("No data at the node");
            break;
        case core::ElevationChange::Status::Unplaced:
            result = tr("No coordinates");
            break;
        case core::ElevationChange::Status::Skipped:
            result = tr("Skipped");
            break;
        }
        if (!c.note.empty())
            result += " - " + QString::fromStdString(c.note);
        const QStringList cells = {QString::fromStdString(c.id),
                                   QString::fromStdString(c.kind),
                                   QString::fromStdString(c.field),
                                   num(c.oldValue),
                                   num(c.ground),
                                   num(c.newValue),
                                   result};
        for (int col = 0; col < cells.size(); ++col)
        {
            auto* item = new QTableWidgetItem(cells[col]);
            if (col >= 3 && col <= 5)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (c.status == core::ElevationChange::Status::Outside ||
                c.status == core::ElevationChange::Status::NoData)
                item->setForeground(QColor(180, 90, 0));
            else if (c.status != core::ElevationChange::Status::Update)
                item->setForeground(QColor(120, 128, 140));
            table_->setItem(i, col, item);
        }
    }
    table_->resizeColumnsToContents();
    using S = core::ElevationChange::Status;
    QString summary = tr("%n value(s) to change", nullptr, counts[static_cast<int>(S::Update)]);
    summary += tr(", %n unchanged", nullptr, counts[static_cast<int>(S::Same)]);
    if (counts[static_cast<int>(S::Outside)])
        summary += tr(", %n outside the DEM", nullptr, counts[static_cast<int>(S::Outside)]);
    if (counts[static_cast<int>(S::NoData)])
        summary += tr(", %n on NoData", nullptr, counts[static_cast<int>(S::NoData)]);
    if (counts[static_cast<int>(S::Unplaced)])
        summary += tr(", %n without coordinates", nullptr, counts[static_cast<int>(S::Unplaced)]);
    summary_->setText(summary + tr(". Values in %1.").arg(unit));
    apply_->setEnabled(counts[static_cast<int>(S::Update)] > 0);
}

void ElevationDialog::apply()
{
    QString err;
    const auto plan = plan_;
    if (!doc_->edit(
            tr("Elevations from DEM"),
            [&](core::Model& m, std::string*) { return core::applyElevations(m, plan) > 0; }, &err))
    {
        QMessageBox::warning(this, windowTitle(),
                             err.isEmpty() ? tr("No value was changed.") : err);
        return;
    }
    if (addLayer_->isVisible() && addLayer_->isChecked())
    {
        core::RasterLayerSettings layer;
        layer.path = storedRasterPath(doc_, dem_->currentData().toString());
        layer.style = "hillshade-color";
        layer.opacity = 0.6;
        layer.verticalUnit = units_->currentData().toDouble();
        doc_->settings().rasters.push_back(layer);
        doc_->markSettingsChanged();
        emit layersChanged();
    }
    accept();
}

} // namespace mirab::app
