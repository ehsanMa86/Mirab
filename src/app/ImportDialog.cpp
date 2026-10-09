/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/ImportDialog.cpp
 * Purpose: Import of shapefiles, DXF drawings and CSV tables into the project, with field mapping.
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
#include "ImportDialog.h"

#include "Icons.h"
#include "anahita/core/Crs.h"
#include "anahita/core/Text.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace anahita::app
{

using core::Category;
using interop::GeometryType;

namespace
{
QString geometryName(GeometryType g)
{
    switch (g)
    {
    case GeometryType::Point:
        return QObject::tr("points");
    case GeometryType::Line:
        return QObject::tr("lines");
    case GeometryType::Polygon:
        return QObject::tr("polygons");
    }
    return {};
}
} // namespace

ImportDialog::ImportDialog(ProjectDocument* doc, const QString& file, QWidget* parent)
    : QDialog(parent), doc_(doc), file_(file)
{
    setWindowTitle(tr("Import %1").arg(QFileInfo(file).fileName()));
    resize(980, 680);
    const QString ext = QFileInfo(file).suffix().toLower();
    try
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto path = std::filesystem::u8path(file.toStdString());
        if (ext == "shp")
            layers_.push_back(interop::readShapefile(path));
        else if (ext == "dxf")
            layers_ = interop::readDxf(path);
        else
            layers_.push_back(interop::readCsv(path));
        QApplication::restoreOverrideCursor();
    }
    catch (const std::exception& e)
    {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(parent, tr("Import"), tr("%1 could not be read: %2").arg(QFileInfo(file).fileName(), QString::fromUtf8(e.what())));
        QMetaObject::invokeMethod(this, &QDialog::reject, Qt::QueuedConnection);
        return;
    }

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Choose a layer, what its features become in the model, and which attribute fields fill "
                                "the element properties. Coordinates are taken as map coordinates of the project."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* body = new QHBoxLayout;
    layout->addLayout(body, 1);

    // Left: layers
    auto* left = new QVBoxLayout;
    auto* layersTitle = new QLabel(tr("Layers in the file"), this);
    layersTitle->setObjectName("panelTitle");
    layerList_ = new QListWidget(this);
    for (const auto& l : layers_)
    {
        const QString icon = l.geometry == GeometryType::Point ? "Junction" : l.geometry == GeometryType::Line ? "Pipe" : "Subcatchment";
        auto* item = new QListWidgetItem(icons::element(icon),
                                         QString::fromStdString(l.name) + "\n" +
                                             tr("%1 %2, %3 fields").arg(l.features.size()).arg(geometryName(l.geometry)).arg(l.fields.size()),
                                         layerList_);
        item->setSizeHint(QSize(220, 44));
    }
    layerInfo_ = new QLabel(this);
    layerInfo_->setObjectName("muted");
    layerInfo_->setWordWrap(true);
    left->addWidget(layersTitle);
    left->addWidget(layerList_, 1);
    left->addWidget(layerInfo_);
    body->addLayout(left, 2);

    // Right: mapping
    auto* right = new QVBoxLayout;
    auto* asBox = new QGroupBox(tr("Import as"), this);
    auto* form = new QFormLayout(asBox);
    kind_ = new QComboBox(asBox);
    idField_ = new QComboBox(asBox);
    idPrefix_ = new QLineEdit(asBox);
    idPrefix_->setMaximumWidth(80);
    auto* idRow = new QHBoxLayout;
    idRow->addWidget(idField_, 1);
    idRow->addWidget(new QLabel(tr("prefix for new IDs"), asBox));
    idRow->addWidget(idPrefix_);
    form->addRow(tr("Element type"), kind_);
    form->addRow(tr("ID field"), idRow);
    right->addWidget(asBox);

    linkBox_ = new QGroupBox(tr("Link ends"), this);
    auto* lf = new QFormLayout(linkBox_);
    fromField_ = new QComboBox(linkBox_);
    toField_ = new QComboBox(linkBox_);
    snap_ = new QDoubleSpinBox(linkBox_);
    snap_->setRange(0, 1e6);
    snap_->setDecimals(3);
    snap_->setValue(0.5);
    snap_->setToolTip(tr("Line ends closer than this distance to an existing node connect to it; otherwise a new node "
                         "is created."));
    endpointKind_ = new QComboBox(linkBox_);
    lf->addRow(tr("Start node field"), fromField_);
    lf->addRow(tr("End node field"), toField_);
    lf->addRow(tr("Snap tolerance"), snap_);
    lf->addRow(tr("New end nodes are"), endpointKind_);
    right->addWidget(linkBox_);

    areaBox_ = new QGroupBox(tr("Subcatchments"), this);
    auto* af = new QFormLayout(areaBox_);
    outletField_ = new QComboBox(areaBox_);
    af->addRow(tr("Outlet node field"), outletField_);
    right->addWidget(areaBox_);

    auto* mapBox = new QGroupBox(tr("Properties from attribute fields"), this);
    auto* ml = new QVBoxLayout(mapBox);
    mapping_ = new QTableWidget(mapBox);
    mapping_->setColumnCount(2);
    mapping_->setHorizontalHeaderLabels({tr("Property"), tr("Field")});
    mapping_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mapping_->horizontalHeader()->setStretchLastSection(true);
    mapping_->verticalHeader()->hide();
    mapping_->verticalHeader()->setDefaultSectionSize(30);
    mapping_->setMinimumHeight(170);
    ml->addWidget(mapping_);
    right->addWidget(mapBox, 3);

    auto* crsBox = new QGroupBox(tr("Coordinates"), this);
    auto* cl = new QVBoxLayout(crsBox);
    auto* crsRow = new QHBoxLayout;
    sourceCrs_ = new QLineEdit(crsBox);
    sourceCrs_->setPlaceholderText(tr("Coordinate system of the file (EPSG code or WKT); empty = same as the project"));
    crsRow->addWidget(new QLabel(tr("File CRS"), crsBox));
    crsRow->addWidget(sourceCrs_, 1);
    cl->addLayout(crsRow);
    adoptCrs_ = new QCheckBox(tr("Use it as the coordinate system of the project"), crsBox);
    crsInfo_ = new QLabel(crsBox);
    crsInfo_->setObjectName("muted");
    crsInfo_->setWordWrap(true);
    cl->addWidget(adoptCrs_);
    cl->addWidget(crsInfo_);
    right->addWidget(crsBox);
    body->addLayout(right, 3);

    result_ = new QLabel(this);
    result_->setWordWrap(true);
    layout->addWidget(result_);
    auto* buttons = new QDialogButtonBox(this);
    importButton_ = buttons->addButton(tr("Import layer"), QDialogButtonBox::ActionRole);
    importButton_->setProperty("primary", true);
    importButton_->setMinimumWidth(130);
    auto* close = buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(importButton_, &QPushButton::clicked, this, &ImportDialog::runImport);
    connect(close, &QPushButton::clicked, this, [this] { imported_ > 0 ? accept() : reject(); });
    connect(layerList_, &QListWidget::currentRowChanged, this, &ImportDialog::layerChanged);
    connect(kind_, &QComboBox::currentIndexChanged, this, &ImportDialog::targetChanged);
    connect(sourceCrs_, &QLineEdit::textChanged, this, [this] {
        const QString t = sourceCrs_->text().trimmed();
        const QString project = QString::fromStdString(doc_->settings().crs);
        if (t.isEmpty())
        {
            crsInfo_->setText(tr("Coordinates are used as they are."));
            adoptCrs_->setEnabled(false);
            return;
        }
        const auto src = crs::parse(t.toStdString());
        if (!src)
        {
            crsInfo_->setText(tr("Not recognized; coordinates are used as they are."));
            adoptCrs_->setEnabled(false);
            return;
        }
        const QString name = QString::fromStdString(src->name.empty() ? crs::toString(*src) : src->name);
        adoptCrs_->setEnabled(project.isEmpty());
        if (project.isEmpty())
            crsInfo_->setText(tr("File: %1. The project has no coordinate system yet.").arg(name));
        else if (const auto dst = crs::parse(project.toStdString()))
            crsInfo_->setText(tr("File: %1. Points are reprojected to the project system %2.")
                                  .arg(name, QString::fromStdString(dst->name.empty() ? crs::toString(*dst) : dst->name)));
    });
    layerList_->setCurrentRow(0);
    for (const auto& l : layers_)
    {
        if (!l.crsText.empty())
        {
            const auto c = crs::parse(l.crsText);
            sourceCrs_->setText(c && c->epsg > 0 ? QStringLiteral("EPSG:%1").arg(c->epsg) : QString::fromStdString(l.crsText));
            adoptCrs_->setChecked(doc_->settings().crs.empty());
            break;
        }
    }
    emit sourceCrs_->textChanged(sourceCrs_->text());
}

const interop::ImportedLayer* ImportDialog::currentLayer() const
{
    const int r = layerList_->currentRow();
    return r >= 0 && r < static_cast<int>(layers_.size()) ? &layers_[static_cast<std::size_t>(r)] : nullptr;
}

QString ImportDialog::fieldOf(QComboBox* box) const
{
    return box->currentIndex() <= 0 ? QString() : box->currentText();
}

void ImportDialog::layerChanged()
{
    const auto* l = currentLayer();
    if (!l)
        return;
    const auto& m = doc_->model();
    const bool swmm = m.dialect() == core::Dialect::Swmm;
    kind_->blockSignals(true);
    kind_->clear();
    const bool hasPoints = !l->features.empty() && !l->features.front().points.empty();
    if (l->geometry == GeometryType::Point && hasPoints)
    {
        for (const auto& k : core::elementKinds(m.dialect(), Category::Node))
            kind_->addItem(icons::element(QString::fromStdString(k)), QString::fromStdString(k), static_cast<int>(Category::Node));
    }
    if (l->geometry == GeometryType::Line || !hasPoints)
    {
        for (const auto& k : core::elementKinds(m.dialect(), Category::Link))
            kind_->addItem(icons::element(QString::fromStdString(k)), QString::fromStdString(k), static_cast<int>(Category::Link));
    }
    if (l->geometry == GeometryType::Polygon && swmm)
        kind_->addItem(icons::element("Subcatchment"), "Subcatchment", static_cast<int>(Category::Area));
    if (l->geometry == GeometryType::Polygon && !swmm)
    {
        // Polygon outlines can still become pipes (for example parcels drawn as rings).
        for (const auto& k : core::elementKinds(m.dialect(), Category::Link))
            kind_->addItem(icons::element(QString::fromStdString(k)), QString::fromStdString(k), static_cast<int>(Category::Link));
    }
    kind_->blockSignals(false);

    for (QComboBox* box : {idField_, fromField_, toField_, outletField_})
    {
        box->clear();
        box->addItem(box == idField_ ? tr("(generate IDs)") : tr("(none)"));
        for (const auto& f : l->fields)
            box->addItem(QString::fromStdString(f));
    }
    endpointKind_->clear();
    for (const auto& k : core::elementKinds(m.dialect(), Category::Node))
        endpointKind_->addItem(icons::element(QString::fromStdString(k)), QString::fromStdString(k));

    // Extent, to compare with the project.
    double minX = std::numeric_limits<double>::max(), minY = minX, maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    for (const auto& f : l->features)
        for (const auto& p : f.points)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    if (minX <= maxX)
        layerInfo_->setText(tr("Extent: X %1 to %2, Y %3 to %4")
                                .arg(QString::fromStdString(text::formatDouble(minX, 8)), QString::fromStdString(text::formatDouble(maxX, 8)),
                                     QString::fromStdString(text::formatDouble(minY, 8)), QString::fromStdString(text::formatDouble(maxY, 8))));
    else
        layerInfo_->setText(tr("No coordinates: rows are linked through node ID fields."));
    targetChanged();
}

void ImportDialog::targetChanged()
{
    const auto* l = currentLayer();
    if (!l || kind_->count() == 0)
    {
        importButton_->setEnabled(false);
        return;
    }
    importButton_->setEnabled(true);
    const auto& m = doc_->model();
    const auto target = static_cast<Category>(kind_->currentData().toInt());
    const std::string kind = kind_->currentText().toStdString();
    linkBox_->setVisible(target == Category::Link);
    areaBox_->setVisible(target == Category::Area);
    const auto suggested = interop::suggestMapping(m, *l, target, kind);
    auto select = [](QComboBox* box, const std::string& field) {
        const int idx = field.empty() ? 0 : box->findText(QString::fromStdString(field), Qt::MatchFixedString);
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    select(idField_, suggested.idField);
    idPrefix_->setText(QString::fromStdString(suggested.idPrefix));
    select(fromField_, suggested.fromField);
    select(toField_, suggested.toField);
    select(outletField_, suggested.outletField);
    const int ek = endpointKind_->findText(QString::fromStdString(suggested.endpointKind));
    endpointKind_->setCurrentIndex(ek >= 0 ? ek : 0);
    if (suggested.snapTolerance > 0)
        snap_->setValue(suggested.snapTolerance);

    mapping_->setRowCount(0);
    for (const auto& prop : interop::mappableProperties(m.dialect(), target, kind))
    {
        const int r = mapping_->rowCount();
        mapping_->insertRow(r);
        auto* name = new QTableWidgetItem(QString::fromStdString(prop));
        name->setFlags(Qt::ItemIsEnabled);
        mapping_->setItem(r, 0, name);
        auto* box = new QComboBox(mapping_);
        box->addItem(tr("(default)"));
        for (const auto& f : l->fields)
            box->addItem(QString::fromStdString(f));
        const auto it = suggested.properties.find(prop);
        if (it != suggested.properties.end())
            select(box, it->second);
        mapping_->setCellWidget(r, 1, box);
    }
}

void ImportDialog::runImport()
{
    const auto* l = currentLayer();
    if (!l)
        return;
    interop::ImportMapping mp;
    mp.target = static_cast<Category>(kind_->currentData().toInt());
    mp.kind = kind_->currentText().toStdString();
    mp.idField = fieldOf(idField_).toStdString();
    mp.idPrefix = idPrefix_->text().trimmed().toStdString();
    mp.fromField = fieldOf(fromField_).toStdString();
    mp.toField = fieldOf(toField_).toStdString();
    mp.snapTolerance = snap_->value();
    mp.endpointKind = endpointKind_->currentText().toStdString();
    mp.outletField = fieldOf(outletField_).toStdString();
    for (int r = 0; r < mapping_->rowCount(); ++r)
    {
        auto* box = qobject_cast<QComboBox*>(mapping_->cellWidget(r, 1));
        if (box && box->currentIndex() > 0)
            mp.properties[mapping_->item(r, 0)->text().toStdString()] = box->currentText().toStdString();
    }

    // Reprojection from the file's coordinate system to the project's.
    const QString srcText = sourceCrs_->text().trimmed();
    std::optional<crs::Crs> src = srcText.isEmpty() ? std::nullopt : crs::parse(srcText.toStdString());
    std::optional<crs::Crs> dst;
    if (!doc_->settings().crs.empty())
        dst = crs::parse(doc_->settings().crs);
    if (src && dst && crs::toString(*src) != crs::toString(*dst))
    {
        const crs::Crs from = *src, to = *dst;
        mp.transform = [from, to](double& x, double& y) {
            double ox = 0, oy = 0;
            if (!crs::transform(from, to, x, y, ox, oy) || !std::isfinite(ox) || !std::isfinite(oy))
                return false;
            x = ox;
            y = oy;
            return true;
        };
    }
    const bool adopt = src && !dst && adoptCrs_->isChecked() && adoptCrs_->isEnabled();

    interop::ImportReport report;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = doc_->edit(tr("Import %1").arg(QString::fromStdString(l->name)), [&](core::Model& m, std::string* e) {
        try
        {
            report = interop::importLayer(m, *l, mp);
            return report.nodesAdded + report.linksAdded + report.areasAdded > 0;
        }
        catch (const std::exception& ex)
        {
            if (e)
                *e = ex.what();
            return false;
        }
    }, &err);
    QApplication::restoreOverrideCursor();
    if (!ok)
    {
        result_->setText("<span style='color:#c84b4b'>" +
                         (err.isEmpty() ? tr("Nothing was imported from this layer.") : err).toHtmlEscaped() + "</span>");
        for (const auto& n : report.notes)
            notes_ << QString::fromStdString(n);
        return;
    }
    if (adopt)
    {
        doc_->settings().crs = crs::toString(*src);
        doc_->markSettingsChanged();
        emit doc_->modelReset();
    }
    ++imported_;
    const QString line = tr("%1: %2 nodes, %3 links, %4 subcatchments added, %5 features skipped.")
                             .arg(QString::fromStdString(l->name))
                             .arg(report.nodesAdded)
                             .arg(report.linksAdded)
                             .arg(report.areasAdded)
                             .arg(report.skipped);
    summary_ += (summary_.isEmpty() ? QString() : QStringLiteral(" ")) + line;
    for (const auto& n : report.notes)
        notes_ << QString::fromStdString(n);
    result_->setText("<span style='color:#1e8c46'>" + tr("Imported. ") + line.toHtmlEscaped() + "</span>" +
                     (report.notes.empty() ? QString() : "<br>" + tr("%1 notes are listed in the Messages panel.").arg(report.notes.size())));
    if (auto* item = layerList_->currentItem())
        item->setIcon(icons::get("info"));
}

} // namespace anahita::app
