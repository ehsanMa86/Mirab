/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SwmmEditors.cpp
 * Purpose: Editors of SWMM objects: LID controls and usage, transects and unit hydrographs.
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
#include "SwmmEditors.h"

#include "ChartWidget.h"
#include "Icons.h"
#include "RulesEditor.h"
#include "Theme.h"

#include "mirab/core/Model.h"
#include "mirab/core/Text.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mirab::app
{

using core::LidLayer;

namespace
{
QString qs(const std::string& s)
{
    return QString::fromStdString(s);
}

std::string ss(const QString& s)
{
    return s.trimmed().toStdString();
}

QString numText(double v)
{
    return qs(text::formatDouble(v, 8));
}

std::optional<double> numOf(const QLineEdit* e)
{
    return text::toDouble(e->text().trimmed().toStdString());
}

QString sq(const QString& unit)
{
    return unit + QChar(0x00B2);
}

void markInvalid(QWidget* w, bool on)
{
    w->setStyleSheet(on ? "QLineEdit, QComboBox { border: 1px solid #c84b4b; }" : QString());
}

QLabel* mutedLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("muted");
    l->setWordWrap(true);
    return l;
}
} // namespace

// ---------------------------------------------------------------------------
// SpecialEditor

SpecialEditor::SpecialEditor(ProjectDocument* doc, const QString& section, const QString& id,
                           QWidget* parent)
    : QWidget(parent), doc_(doc), section_(section), id_(id)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* head = new QWidget(this);
    head->setObjectName("panelHeader");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(14, 10, 14, 10);
    auto* titles = new QVBoxLayout;
    title_ = new QLabel(head);
    title_->setObjectName("panelTitle");
    state_ = new QLabel(head);
    state_->setObjectName("panelSubtitle");
    titles->addWidget(title_);
    titles->addWidget(state_);
    hl->addLayout(titles, 1);
    tools_ = new QHBoxLayout;
    tools_->setSpacing(6);
    hl->addLayout(tools_);
    revert_ = new QPushButton(icons::get("undo"), tr("Revert"), head);
    apply_ = new QPushButton(tr("Apply"), head);
    apply_->setProperty("primary", true);
    hl->addWidget(revert_);
    hl->addWidget(apply_);
    layout->addWidget(head);

    auto* split = new QSplitter(Qt::Vertical, this);
    body_ = new QWidget(split);
    messages_ = new QListWidget(split);
    messages_->setMinimumHeight(48);
    split->addWidget(body_);
    split->addWidget(messages_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({600, 64});
    split_ = split;
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1);

    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(revert_, &QPushButton::clicked, this, [this] { load(); });
    auto reload = [this] {
        if (!dirty_)
            load();
    };
    connect(doc_, &ProjectDocument::modelChanged, this, reload);
    connect(doc_, &ProjectDocument::modelReset, this, reload);
}

bool SpecialEditor::hasPendingEdits() const
{
    return dirty_;
}

void SpecialEditor::setTitle(const QString& title)
{
    title_->setText(title);
}

void SpecialEditor::setState(const QString& text)
{
    state_->setText(text);
}

void SpecialEditor::setDirty(bool on)
{
    dirty_ = on;
    updateButtons();
    if (on)
        state_->setText(hasErrors_ ? tr("Edited; fix the errors listed below before applying.")
                                   : tr("Edited; Apply writes the changes to the project (undo with Ctrl+Z)."));
}

void SpecialEditor::updateButtons()
{
    apply_->setEnabled(dirty_ && !hasErrors_);
    revert_->setEnabled(dirty_);
}

bool SpecialEditor::showIssues(const std::vector<core::ObjectIssue>& issues)
{
    messages_->clear();
    hasErrors_ = core::hasErrors(issues);
    for (const auto& i : issues)
    {
        const bool err = i.severity == core::ObjectIssue::Severity::Error;
        auto* item = new QListWidgetItem(icons::get(err ? "error" : "warning"), qs(i.message), messages_);
        item->setData(Qt::UserRole, qs(i.field));
    }
    if (issues.empty())
    {
        auto* item = new QListWidgetItem(icons::get("info"), tr("No problems found."), messages_);
        item->setForeground(theme::kSuccess);
    }
    updateButtons();
    return hasErrors_;
}

void SpecialEditor::applied(const QString& newId)
{
    dirty_ = false;
    if (newId != id_)
    {
        id_ = newId;
        emit renamed(section_, newId);
    }
    load();
}

void SpecialEditor::setMessageHeight(int height)
{
    split_->setSizes({std::max(100, split_->height() - height), height});
}

core::UnitSystem SpecialEditor::unitSystem() const
{
    return doc_->model().units().system;
}

// ---------------------------------------------------------------------------
// LidSchematic

LidSchematic::LidSchematic(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void LidSchematic::setControl(const core::LidControl& control, core::UnitSystem system)
{
    control_ = control;
    system_ = system;
    update();
}

namespace
{
const QColor kWater(120, 175, 230);
const QColor kSoil(176, 133, 88);
const QColor kSoilDots(120, 86, 52);
const QColor kGravel(214, 214, 206);
const QColor kGravelStone(160, 160, 150);
const QColor kPavement(150, 152, 160);
const QColor kMat(72, 82, 94);
const QColor kNative(206, 188, 160);
const QColor kGrass(64, 150, 70);
const QColor kRoof(120, 96, 90);
constexpr double kPi = 3.14159265358979323846;

struct Band
{
    Band(LidLayer l, double t, QString ti, QString d, QRectF r = {})
        : layer(l), thickness(t), title(std::move(ti)), detail(std::move(d)), rect(r)
    {
    }
    LidLayer layer;
    double thickness = 0.0;
    QString title;
    QString detail;
    QRectF rect;
};

void drawGrass(QPainter& p, double x0, double x1, double y, int count, double height)
{
    p.setPen(QPen(kGrass, 1.6, Qt::SolidLine, Qt::RoundCap));
    if (count <= 0)
        return;
    const double step = (x1 - x0) / count;
    for (int i = 0; i < count; ++i)
    {
        const double x = x0 + step * (i + 0.5);
        const double h = height * (0.7 + 0.3 * ((i * 37) % 10) / 10.0);
        p.drawLine(QPointF(x, y), QPointF(x - 3, y - h));
        p.drawLine(QPointF(x, y), QPointF(x + 1, y - h * 1.1));
        p.drawLine(QPointF(x, y), QPointF(x + 4, y - h * 0.8));
    }
}

void drawRain(QPainter& p, const QRectF& r)
{
    p.setPen(QPen(kWater.darker(130), 1.4, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 9; ++i)
    {
        const double x = r.left() + r.width() * (i + 0.5) / 9.0;
        const double y = r.top() + (i % 3) * 5.0;
        p.drawLine(QPointF(x, y), QPointF(x - 3, y + 9));
    }
}

void drawPipe(QPainter& p, QPointF c, double right, const QString& label)
{
    p.setPen(QPen(theme::kText, 1.4));
    p.setBrush(Qt::white);
    p.drawEllipse(c, 7, 7);
    p.setBrush(theme::kText);
    for (int k = 0; k < 4; ++k)
    {
        const double a = k * kPi / 2 + kPi / 4;
        p.drawEllipse(QPointF(c.x() + 4 * std::cos(a), c.y() + 4 * std::sin(a)), 0.9, 0.9);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme::kText, 1.4, Qt::DashLine));
    p.drawLine(QPointF(c.x() + 7, c.y()), QPointF(right - 6, c.y()));
    p.setPen(QPen(theme::kText, 1.4));
    p.drawLine(QPointF(right - 6, c.y()), QPointF(right - 11, c.y() - 4));
    p.drawLine(QPointF(right - 6, c.y()), QPointF(right - 11, c.y() + 4));
    if (!label.isEmpty())
        p.drawText(QRectF(c.x() + 10, c.y() - 16, right - c.x() - 10, 12), Qt::AlignRight, label);
}

void drawLabels(QPainter& p, const std::vector<Band>& bands, double sectionRight, double labelLeft,
                double labelRight)
{
    const QFont normal = p.font();
    const QFontMetrics fm(normal);
    QFont bold = normal;
    bold.setBold(true);
    double nextTop = -1e9;
    for (const auto& b : bands)
    {
        const double h = fm.height() * (b.detail.isEmpty() ? 1.0 : 2.0) + 2;
        double top = std::max(b.rect.center().y() - h / 2, nextTop);
        nextTop = top + h + 4;
        p.setPen(QPen(theme::kBorderStrong, 1));
        p.drawLine(QPointF(sectionRight + 2, b.rect.center().y()), QPointF(labelLeft - 4, top + fm.height() / 2.0));
        p.setPen(theme::kText);
        p.setFont(bold);
        p.drawText(QRectF(labelLeft, top, labelRight - labelLeft, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(b.title, Qt::ElideRight, static_cast<int>(labelRight - labelLeft)));
        p.setFont(normal);
        if (!b.detail.isEmpty())
        {
            p.setPen(theme::kTextMuted);
            p.drawText(QRectF(labelLeft, top + fm.height(), labelRight - labelLeft, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(b.detail, Qt::ElideRight, static_cast<int>(labelRight - labelLeft)));
        }
    }
}
} // namespace

void LidSchematic::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::kSurface);
    const QFontMetrics fm(font());
    const QRectF area = QRectF(rect()).adjusted(14, 10, -14, -12);
    p.setPen(theme::kTextMuted);
    p.drawText(QRectF(area.left(), area.top(), area.width(), fm.height()), Qt::AlignLeft,
               tr("Cross-section (layer heights are indicative)"));
    const auto* info = core::findLidType(control_.type);
    if (!info)
    {
        p.drawText(area, Qt::AlignCenter, tr("Choose a LID type to see its layers."));
        return;
    }
    const QString depthUnit = qs(core::lidUnitLabel("depth", system_));
    const QString rateUnit = qs(core::lidUnitLabel("rate", system_));
    auto val = [&](LidLayer l, int i) { return control_.number(l, i).value_or(0.0); };
    auto txt = [&](double v) { return qs(text::formatDouble(v, 4)); };
    const bool hasDrain = control_.hasLayer(LidLayer::Drain) && val(LidLayer::Drain, 0) > 0.0;

    const QRectF draw = area.adjusted(0, fm.height() + 8, 0, 0);
    const double sectionLeft = draw.left() + 4;
    const double sectionRight = draw.left() + draw.width() * 0.56;
    const double labelLeft = sectionRight + 22;
    const double labelRight = draw.right();

    if (control_.type == "VS")
    {
        // Trapezoidal swale cut into the ground.
        const double depth = val(LidLayer::Surface, 0);
        const double side = val(LidLayer::Surface, 4);
        const QRectF r(sectionLeft, draw.top() + 30, sectionRight - sectionLeft, draw.height() - 40);
        const double dPx = r.height() * 0.42;
        const double topW = r.width() * 0.86;
        const double run = std::min(topW * 0.42, std::max(6.0, side * dPx * 0.35));
        const double cx = r.center().x();
        const double gy = r.top() + r.height() * 0.25;
        QPainterPath ground;
        ground.moveTo(r.left(), gy);
        ground.lineTo(cx - topW / 2, gy);
        ground.lineTo(cx - topW / 2 + run, gy + dPx);
        ground.lineTo(cx + topW / 2 - run, gy + dPx);
        ground.lineTo(cx + topW / 2, gy);
        ground.lineTo(r.right(), gy);
        ground.lineTo(r.right(), r.bottom());
        ground.lineTo(r.left(), r.bottom());
        ground.closeSubpath();
        const double wy = gy + dPx * 0.35;
        const double wl = cx - topW / 2 + run * 0.35, wr = cx + topW / 2 - run * 0.35;
        QPainterPath water;
        water.moveTo(wl, wy);
        water.lineTo(cx - topW / 2 + run, gy + dPx);
        water.lineTo(cx + topW / 2 - run, gy + dPx);
        water.lineTo(wr, wy);
        water.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(kWater);
        p.drawPath(water);
        p.setBrush(kNative);
        p.setPen(QPen(kSoilDots, 1.2));
        p.drawPath(ground);
        drawGrass(p, r.left(), cx - topW / 2, gy, 4, 8);
        drawGrass(p, cx + topW / 2, r.right(), gy, 4, 8);
        drawGrass(p, cx - topW / 2 + run, cx + topW / 2 - run, gy + dPx, 6, 7);
        drawRain(p, QRectF(r.left(), draw.top(), r.width(), 24));
        p.setPen(QPen(theme::kText, 1, Qt::DashLine));
        p.drawLine(QPointF(cx + topW / 2 + 8, gy), QPointF(cx + topW / 2 + 8, gy + dPx));
        std::vector<Band> bands = {
            {LidLayer::Surface, depth, tr("Swale depth %1 %2").arg(txt(depth), depthUnit),
             tr("side slope %1 (run/rise)").arg(txt(side)), QRectF(r.left(), gy, r.width(), dPx)},
            {LidLayer::Surface, 0, tr("Surface flow"),
             tr("n = %1, slope %2%").arg(txt(val(LidLayer::Surface, 2)), txt(val(LidLayer::Surface, 3))),
             QRectF(r.left(), gy + dPx * 0.5, r.width(), dPx)},
        };
        if (val(LidLayer::Surface, 1) > 0)
            bands.push_back({LidLayer::Surface, 0, tr("Vegetation"),
                             tr("%1% of the volume").arg(txt(val(LidLayer::Surface, 1) * 100)),
                             QRectF(r.left(), gy - 10, r.width(), 10)});
        std::sort(bands.begin(), bands.end(),
                  [](const Band& a, const Band& b) { return a.rect.center().y() < b.rect.center().y(); });
        drawLabels(p, bands, sectionRight, labelLeft, labelRight);
        return;
    }

    if (control_.type == "RB")
    {
        const double h = val(LidLayer::Storage, 0);
        const double offset = val(LidLayer::Drain, 2);
        const bool covered = !text::iequals(control_.value(LidLayer::Storage, 4), "NO");
        const double barrelW = (sectionRight - sectionLeft) * 0.5;
        const QRectF barrel(sectionLeft + (sectionRight - sectionLeft - barrelW) / 2, draw.top() + 46, barrelW,
                            draw.height() - 70);
        // Downspout from the roof.
        p.setPen(QPen(kRoof, 6, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(barrel.center().x(), draw.top() + 4),
                   QPointF(barrel.center().x(), barrel.top() + (covered ? 2 : -6)));
        const QRectF water = barrel.adjusted(3, barrel.height() * 0.4, -3, -3);
        p.setPen(Qt::NoPen);
        p.setBrush(kWater);
        p.drawRoundedRect(water, 6, 6);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(theme::kText, 2));
        p.drawRoundedRect(barrel, 10, 10);
        if (covered)
            p.drawLine(barrel.topLeft() + QPointF(-4, 0), barrel.topRight() + QPointF(4, 0));
        for (double f : {0.33, 0.66})
        {
            p.setPen(QPen(theme::kBorderStrong, 1));
            p.drawLine(QPointF(barrel.left(), barrel.top() + barrel.height() * f),
                       QPointF(barrel.right(), barrel.top() + barrel.height() * f));
        }
        const double frac = h > 0 ? std::clamp(offset / h, 0.0, 1.0) : 0.0;
        const QPointF pipe(barrel.right() - 10, barrel.bottom() - 10 - frac * (barrel.height() - 20));
        if (hasDrain)
            drawPipe(p, pipe, labelLeft - 8, {});
        std::vector<Band> bands = {
            {LidLayer::Storage, h, tr("Barrel height %1 %2").arg(txt(h), depthUnit),
             covered ? tr("covered") : tr("open to rain"),
             QRectF(barrel.left(), barrel.top(), barrel.width(), barrel.height() * 0.4)},
        };
        if (control_.hasLayer(LidLayer::Drain))
            bands.push_back({LidLayer::Drain, 0,
                             hasDrain ? tr("Drain offset %1 %2").arg(txt(offset), depthUnit) : tr("No drain"),
                             hasDrain ? tr("C = %1, n = %2, delay %3 hr")
                                            .arg(txt(val(LidLayer::Drain, 0)), txt(val(LidLayer::Drain, 1)),
                                                 txt(val(LidLayer::Drain, 3)))
                                      : QString(),
                             QRectF(barrel.left(), pipe.y() - 1, barrel.width(), 2)});
        drawLabels(p, bands, sectionRight, labelLeft, labelRight);
        return;
    }

    if (control_.type == "RD")
    {
        // Roof, gutter and downspout discharging onto a lawn.
        const double w = sectionRight - sectionLeft;
        const double roofTop = draw.top() + 20, eave = draw.top() + draw.height() * 0.38;
        QPainterPath roof;
        roof.moveTo(sectionLeft, eave);
        roof.lineTo(sectionLeft + w * 0.45, roofTop);
        roof.lineTo(sectionLeft + w * 0.9, eave);
        roof.lineTo(sectionLeft + w * 0.9, eave + 8);
        roof.lineTo(sectionLeft + w * 0.45, roofTop + 8);
        roof.lineTo(sectionLeft, eave + 8);
        roof.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(kRoof);
        p.drawPath(roof);
        p.setBrush(QColor(236, 232, 226));
        p.drawRect(QRectF(sectionLeft + w * 0.08, eave + 8, w * 0.74, draw.bottom() - 40 - eave - 8));
        drawRain(p, QRectF(sectionLeft, draw.top(), w, 18));
        const double spoutX = sectionLeft + w * 0.92;
        p.setPen(QPen(theme::kText, 4, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(spoutX, eave + 4), QPointF(spoutX, draw.bottom() - 46));
        p.drawLine(QPointF(spoutX, draw.bottom() - 46), QPointF(spoutX + 12, draw.bottom() - 40));
        const QRectF lawn(sectionLeft, draw.bottom() - 40, w + 10, 30);
        p.setPen(Qt::NoPen);
        p.setBrush(kNative);
        p.drawRect(lawn);
        drawGrass(p, lawn.left(), lawn.right(), lawn.top(), 10, 7);
        std::vector<Band> bands = {
            {LidLayer::Surface, 0, tr("Roof surface"),
             tr("n = %1, slope %2%").arg(txt(val(LidLayer::Surface, 2)), txt(val(LidLayer::Surface, 3))),
             QRectF(sectionLeft, roofTop, w, eave - roofTop)},
            {LidLayer::Drain, 0,
             tr("Roof drain capacity %1 %2").arg(txt(val(LidLayer::Drain, 0)), rateUnit),
             tr("excess flows onto pervious area"), QRectF(spoutX, eave, 4, draw.bottom() - 40 - eave)},
            {LidLayer::Surface, 0, tr("Pervious area"), {}, lawn},
        };
        drawLabels(p, bands, sectionRight + 10, labelLeft, labelRight);
        return;
    }

    // Layered systems: BC, RG, GR, IT, PP.
    std::vector<Band> bands;
    if (control_.hasLayer(LidLayer::Surface))
    {
        const double berm = val(LidLayer::Surface, 0);
        QString detail;
        if (val(LidLayer::Surface, 1) > 0)
            detail = tr("vegetation %1%").arg(txt(val(LidLayer::Surface, 1) * 100));
        if (val(LidLayer::Surface, 2) > 0)
            detail += (detail.isEmpty() ? QString() : QStringLiteral(", ")) +
                      tr("n = %1, slope %2%").arg(txt(val(LidLayer::Surface, 2)), txt(val(LidLayer::Surface, 3)));
        bands.push_back({LidLayer::Surface, berm,
                         berm > 0 ? tr("Ponding up to %1 %2").arg(txt(berm), depthUnit) : tr("Surface (no ponding)"),
                         detail});
    }
    if (control_.hasLayer(LidLayer::Pavement))
        bands.push_back({LidLayer::Pavement, val(LidLayer::Pavement, 0),
                         tr("Pavement %1 %2").arg(txt(val(LidLayer::Pavement, 0)), depthUnit),
                         tr("void ratio %1, permeability %2 %3")
                             .arg(txt(val(LidLayer::Pavement, 1)), txt(val(LidLayer::Pavement, 3)), rateUnit)});
    if (control_.hasLayer(LidLayer::Soil))
        bands.push_back({LidLayer::Soil, val(LidLayer::Soil, 0),
                         tr("Soil %1 %2").arg(txt(val(LidLayer::Soil, 0)), depthUnit),
                         tr("porosity %1, conductivity %2 %3")
                             .arg(txt(val(LidLayer::Soil, 1)), txt(val(LidLayer::Soil, 4)), rateUnit)});
    if (control_.hasLayer(LidLayer::DrainMat))
        bands.push_back({LidLayer::DrainMat, val(LidLayer::DrainMat, 0),
                         tr("Drainage mat %1 %2").arg(txt(val(LidLayer::DrainMat, 0)), depthUnit),
                         tr("void fraction %1, n = %2")
                             .arg(txt(val(LidLayer::DrainMat, 1)), txt(val(LidLayer::DrainMat, 2)))});
    if (control_.hasLayer(LidLayer::Storage))
        bands.push_back({LidLayer::Storage, val(LidLayer::Storage, 0),
                         tr("Storage %1 %2").arg(txt(val(LidLayer::Storage, 0)), depthUnit),
                         tr("void ratio %1, seepage %2 %3")
                             .arg(txt(val(LidLayer::Storage, 1)), txt(val(LidLayer::Storage, 2)), rateUnit)});
    const bool roof = control_.type == "GR";
    const bool liner = control_.hasLayer(LidLayer::Storage) && val(LidLayer::Storage, 2) <= 0.0;

    const double nativeH = 30.0;
    const double top = draw.top() + 24; // room for rain
    const double avail = std::max(60.0, draw.bottom() - nativeH - top);
    const double minH = std::max(22.0, fm.height() * 1.6);
    double total = 0.0;
    for (const auto& b : bands)
        total += std::max(0.0, b.thickness);
    const double spare = std::max(0.0, avail - minH * bands.size());
    double y = top;
    for (auto& b : bands)
    {
        const double share = total > 0 ? std::max(0.0, b.thickness) / total : 1.0 / bands.size();
        const double h = minH + spare * share;
        b.rect = QRectF(sectionLeft, y, sectionRight - sectionLeft, h);
        y += h;
    }
    const QRectF native(sectionLeft, y, sectionRight - sectionLeft, nativeH);
    drawRain(p, QRectF(sectionLeft + 20, draw.top(), sectionRight - sectionLeft - 40, 20));

    for (const auto& b : bands)
    {
        const QRectF r = b.rect;
        p.setPen(Qt::NoPen);
        switch (b.layer)
        {
        case LidLayer::Surface:
        {
            const double bermW = std::min(18.0, r.width() * 0.08);
            if (b.thickness > 0)
            {
                QLinearGradient g(r.topLeft(), r.bottomLeft());
                g.setColorAt(0, QColor(kWater.red(), kWater.green(), kWater.blue(), 90));
                g.setColorAt(1, QColor(kWater.red(), kWater.green(), kWater.blue(), 200));
                p.setBrush(g);
                p.drawRect(r.adjusted(bermW, r.height() * 0.3, -bermW, 0));
                p.setBrush(kNative.darker(110));
                p.drawRect(QRectF(r.left(), r.top(), bermW, r.height()));
                p.drawRect(QRectF(r.right() - bermW, r.top(), bermW, r.height()));
                p.setPen(QPen(kWater.darker(140), 1, Qt::DashLine));
                p.drawLine(QPointF(r.left() + bermW, r.top() + r.height() * 0.3),
                           QPointF(r.right() - bermW, r.top() + r.height() * 0.3));
            }
            const int tufts = static_cast<int>(std::round(val(LidLayer::Surface, 1) * 30.0));
            if (tufts > 0 || control_.type == "GR" || control_.type == "BC" || control_.type == "RG")
                drawGrass(p, r.left() + bermW, r.right() - bermW, r.bottom(), std::max(tufts, 6),
                          std::min(r.height() * 0.6, 14.0));
            break;
        }
        case LidLayer::Pavement:
        {
            p.setBrush(kPavement);
            p.drawRect(r);
            p.setPen(QPen(kPavement.lighter(135), 1.2));
            const double rowH = std::max(8.0, r.height() / 2);
            int row = 0;
            for (double yy = r.top(); yy < r.bottom() - 1; yy += rowH, ++row)
            {
                p.drawLine(QPointF(r.left(), yy), QPointF(r.right(), yy));
                for (double xx = r.left() + (row % 2 ? 14 : 0); xx < r.right(); xx += 28)
                    p.drawLine(QPointF(xx, yy), QPointF(xx, std::min(yy + rowH, r.bottom())));
            }
            break;
        }
        case LidLayer::Soil:
        {
            p.setBrush(kSoil);
            p.drawRect(r);
            p.setBrush(kSoilDots);
            int k = 0;
            for (double yy = r.top() + 5; yy < r.bottom() - 2; yy += 7)
                for (double xx = r.left() + 4 + (k++ % 3) * 3; xx < r.right() - 2; xx += 11)
                    p.drawEllipse(QPointF(xx, yy), 1.1, 1.1);
            break;
        }
        case LidLayer::DrainMat:
        {
            p.setBrush(kMat);
            p.drawRect(r);
            p.setPen(QPen(QColor(200, 210, 220), 1.2));
            QPainterPath zig(QPointF(r.left(), r.center().y()));
            int k = 0;
            for (double xx = r.left(); xx <= r.right(); xx += 8, ++k)
                zig.lineTo(xx, r.center().y() + (k % 2 ? -r.height() * 0.25 : r.height() * 0.25));
            p.drawPath(zig);
            break;
        }
        case LidLayer::Storage:
        {
            p.setBrush(kGravel);
            p.drawRect(r);
            p.setPen(QPen(kGravelStone, 1));
            p.setBrush(Qt::NoBrush);
            int k = 0;
            for (double yy = r.top() + 6; yy < r.bottom() - 3; yy += 10, ++k)
                for (double xx = r.left() + 6 + (k % 2) * 6; xx < r.right() - 3; xx += 12)
                    p.drawEllipse(QPointF(xx, yy), 4.2, 3.2);
            break;
        }
        default:
            break;
        }
        p.setPen(QPen(theme::kBorderStrong, 1));
        p.setBrush(Qt::NoBrush);
        if (b.layer != LidLayer::Surface)
            p.drawRect(r);
    }
    // Ground below: native soil, a liner or the roof deck.
    p.setPen(Qt::NoPen);
    p.setBrush(roof ? QColor(190, 190, 190) : kNative);
    p.drawRect(native);
    p.setPen(QPen(roof ? QColor(150, 150, 150) : kSoilDots.lighter(130), 1));
    for (double xx = native.left() - native.height(); xx < native.right(); xx += 9)
        p.drawLine(QPointF(std::max(native.left(), xx), native.top() + std::max(0.0, native.left() - xx)),
                   QPointF(std::min(native.right(), xx + native.height()),
                           native.bottom() - std::max(0.0, xx + native.height() - native.right())));
    if (liner)
    {
        p.setPen(QPen(theme::kText, 3));
        p.drawLine(native.topLeft(), native.topRight());
    }
    std::vector<Band> labels = bands;
    const QString below = roof ? tr("Roof deck") : liner ? tr("Impermeable liner") : tr("Native soil");
    labels.push_back({LidLayer::Removals, 0, below, {}, native});
    // Underdrain in the storage layer.
    if (control_.hasLayer(LidLayer::Drain))
    {
        const auto st = std::find_if(bands.begin(), bands.end(),
                                     [](const Band& b) { return b.layer == LidLayer::Storage; });
        const double offset = val(LidLayer::Drain, 2);
        if (st != bands.end() && hasDrain)
        {
            const double h = std::max(1e-9, st->thickness);
            const double fy = st->rect.bottom() - 9 - std::clamp(offset / h, 0.0, 1.0) * (st->rect.height() - 18);
            const QPointF c(st->rect.left() + st->rect.width() * 0.3, fy);
            drawPipe(p, c, sectionRight + 14, {});
            labels.push_back({LidLayer::Drain, 0, tr("Drain at %1 %2").arg(txt(offset), depthUnit),
                              tr("C = %1, n = %2").arg(txt(val(LidLayer::Drain, 0)), txt(val(LidLayer::Drain, 1))),
                              QRectF(c.x(), c.y() - 1, 1, 2)});
        }
        else if (st != bands.end())
        {
            labels.push_back({LidLayer::Drain, 0, tr("No underdrain"), {},
                              QRectF(sectionLeft, st->rect.bottom() - 2, 1, 2)});
        }
    }
    std::stable_sort(labels.begin(), labels.end(),
                     [](const Band& a, const Band& b) { return a.rect.center().y() < b.rect.center().y(); });
    drawLabels(p, labels, sectionRight, labelLeft, labelRight);
}

// ---------------------------------------------------------------------------
// LidControlEditor

QString LidControlEditor::Field::text() const
{
    return edit ? edit->text().trimmed() : combo->currentText().trimmed();
}

void LidControlEditor::Field::setText(const QString& value)
{
    if (edit)
    {
        edit->setText(value);
        return;
    }
    const int i = combo->findText(value, Qt::MatchFixedString);
    if (i >= 0)
        combo->setCurrentIndex(i);
    else if (combo->isEditable())
        combo->setEditText(value);
}

QWidget* LidControlEditor::Field::widget() const
{
    return edit ? static_cast<QWidget*>(edit) : combo;
}

LidControlEditor::LidControlEditor(ProjectDocument* doc, const QString& id, QWidget* parent)
    : SpecialEditor(doc, "LID_CONTROLS", id, parent)
{
    auto* split = new QSplitter(Qt::Horizontal, body());
    auto* bl = new QVBoxLayout(body());
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(split);

    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(14, 12, 8, 8);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(left);
    type_ = new QComboBox(left);
    for (const auto& t : core::lidTypes())
        type_->addItem(qs(t.code + "  " + t.name), qs(t.code));
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Type"), type_);
    ll->addLayout(form);
    typeHelp_ = mutedLabel({}, left);
    ll->addWidget(typeHelp_);
    layerTabs_ = new QTabWidget(left);
    layerTabs_->setDocumentMode(true);
    layerTabs_->setStyleSheet("QTabBar::tab { padding: 6px 9px; }");
    ll->addWidget(layerTabs_, 1);
    paramHelp_ = mutedLabel(tr("Select a value to see what it means."), left);
    paramHelp_->setMinimumHeight(paramHelp_->fontMetrics().height() * 3);
    paramHelp_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ll->addWidget(paramHelp_);
    schematic_ = new LidSchematic(split);
    split->addWidget(left);
    split->addWidget(schematic_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({470, 520});

    connect(name_, &QLineEdit::textEdited, this, [this] {
        setDirty(true);
        refresh();
    });
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_)
            return;
        collect();
        buildLayerTabs();
        setDirty(true);
        refresh();
    });
    load();
}

QString LidControlEditor::typeCode() const
{
    return type_->currentData().toString();
}

void LidControlEditor::load()
{
    loading_ = true;
    const auto& doc = doc_->model().document();
    auto c = core::readLidControl(doc, ss(id_));
    exists_ = c.has_value();
    if (!c)
        c = core::defaultLidControl(ss(id_.isEmpty() ? QStringLiteral("LID1") : id_), "BC", unitSystem());
    name_->setText(qs(c->name));
    int ti = type_->findData(qs(c->type));
    type_->setCurrentIndex(ti >= 0 ? ti : 0);
    values_ = c->layers;
    buildLayerTabs();
    loading_ = false;
    refresh();
    setTitle(tr("LID control %1").arg(name_->text()));
    if (exists_)
    {
        setDirty(false);
        setState(tr("Edit the layers of the control; the drawing updates as you type."));
    }
    else
    {
        setDirty(true);
        setState(tr("New LID control; Apply adds it to the project."));
    }
}

void LidControlEditor::buildLayerTabs()
{
    const bool wasLoading = loading_;
    loading_ = true;
    const int current = layerTabs_->currentIndex();
    while (layerTabs_->count() > 0)
    {
        QWidget* w = layerTabs_->widget(0);
        layerTabs_->removeTab(0);
        w->deleteLater();
    }
    fields_.clear();
    removals_ = nullptr;
    const auto* info = core::findLidType(ss(typeCode()));
    if (!info)
    {
        loading_ = wasLoading;
        return;
    }
    typeHelp_->setText(qs(info->help));
    const std::string type = info->code;
    const auto system = unitSystem();
    QSet<QString> curves;
    for (const auto& n : core::keyedObjectNames(doc_->model().document(), "CURVES"))
        curves.insert(qs(n));
    for (LidLayer layer : info->layers)
    {
        auto* page = new QWidget(layerTabs_);
        auto* pl = new QVBoxLayout(page);
        pl->setContentsMargins(8, 10, 8, 8);
        if (layer == LidLayer::Removals)
        {
            pl->addWidget(mutedLabel(tr("Percent of each pollutant removed from the water that leaves through "
                                        "the drain."),
                                     page));
            removals_ = new QTableWidget(0, 2, page);
            removals_->setHorizontalHeaderLabels({tr("Pollutant"), tr("Removal (%)")});
            removals_->horizontalHeader()->setStretchLastSection(true);
            removals_->verticalHeader()->setDefaultSectionSize(24);
            const auto it = values_.find(layer);
            if (it != values_.end())
            {
                for (std::size_t i = 0; i + 1 < it->second.size(); i += 2)
                {
                    const int r = removals_->rowCount();
                    removals_->insertRow(r);
                    removals_->setItem(r, 0, new QTableWidgetItem(qs(it->second[i])));
                    removals_->setItem(r, 1, new QTableWidgetItem(qs(it->second[i + 1])));
                }
            }
            pl->addWidget(removals_, 1);
            auto* row = new QHBoxLayout;
            auto* add = new QPushButton(icons::get("new"), tr("Add pollutant"), page);
            auto* del = new QPushButton(icons::get("delete"), tr("Remove"), page);
            row->addWidget(add);
            row->addWidget(del);
            row->addStretch(1);
            pl->addLayout(row);
            connect(add, &QPushButton::clicked, this, [this] {
                removals_->insertRow(removals_->rowCount());
                setDirty(true);
                refresh();
            });
            connect(del, &QPushButton::clicked, this, [this] {
                if (removals_->currentRow() >= 0)
                    removals_->removeRow(removals_->currentRow());
                setDirty(true);
                refresh();
            });
            connect(removals_, &QTableWidget::itemChanged, this, [this] {
                if (loading_)
                    return;
                setDirty(true);
                refresh();
            });
            layerTabs_->addTab(page, tr("Removals"));
            continue;
        }
        if (!values_.count(layer))
            values_[layer] = core::defaultLidLayer(layer, type, system);
        const auto& values = values_[layer];
        auto* form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignLeft);
        const auto& params = core::lidLayerParams(layer);
        for (std::size_t i = 0; i < params.size(); ++i)
        {
            const auto& p = params[i];
            // Values a type ignores keep their place in the row but are not shown.
            const bool barrelOnly = p.key == "Covrd" || p.key == "Delay";
            const bool notBarrel = layer == LidLayer::Storage && (p.key == "Vratio" || p.key == "Seepage");
            const bool ignored = (barrelOnly && type != "RB") || (notBarrel && type == "RB") ||
                                 (p.key == "Xslope" && type != "VS");
            if (ignored)
                continue;
            Field f;
            f.layer = layer;
            f.index = static_cast<int>(i);
            const QString value = i < values.size() ? qs(values[i]) : QString();
            if (p.quantity == "yesno")
            {
                f.combo = new QComboBox(page);
                f.combo->addItems({"YES", "NO"});
            }
            else if (p.quantity == "curve")
            {
                f.combo = new QComboBox(page);
                f.combo->setEditable(true);
                f.combo->addItem(QString());
                QStringList names = curves.values();
                names.sort();
                f.combo->addItems(names);
            }
            else
            {
                f.edit = new QLineEdit(page);
                f.edit->setPlaceholderText(p.optional ? tr("optional") : QString());
            }
            f.setText(value.isEmpty() && f.combo && p.quantity == "yesno" ? qs(p.defaultUS) : value);
            const QString unit = qs(core::lidUnitLabel(p.quantity, system));
            QString label = type == "RB" && layer == LidLayer::Storage && i == 0 ? tr("Barrel height") : qs(p.label);
            if (!unit.isEmpty())
                label += " (" + unit + ")";
            QWidget* w = f.widget();
            w->setToolTip(qs(p.help));
            w->setProperty("helpTitle", qs(p.label));
            w->installEventFilter(this);
            auto* lab = new QLabel(label, page);
            lab->setToolTip(qs(p.help));
            form->addRow(lab, w);
            if (f.edit)
                connect(f.edit, &QLineEdit::textEdited, this, [this] {
                    setDirty(true);
                    refresh();
                });
            else
                connect(f.combo, &QComboBox::currentTextChanged, this, [this] {
                    if (loading_)
                        return;
                    setDirty(true);
                    refresh();
                });
            fields_.push_back(f);
        }
        pl->addLayout(form);
        pl->addStretch(1);
        layerTabs_->addTab(page, tr(core::lidLayerName(layer)));
    }
    if (current >= 0 && current < layerTabs_->count())
        layerTabs_->setCurrentIndex(current);
    loading_ = wasLoading;
}

void LidControlEditor::collect()
{
    for (const auto& f : fields_)
    {
        auto& v = values_[f.layer];
        if (v.size() <= static_cast<std::size_t>(f.index))
            v.resize(static_cast<std::size_t>(f.index) + 1);
        v[static_cast<std::size_t>(f.index)] = ss(f.text());
    }
    if (removals_)
    {
        std::vector<std::string> pairs;
        for (int r = 0; r < removals_->rowCount(); ++r)
        {
            const auto* a = removals_->item(r, 0);
            const auto* b = removals_->item(r, 1);
            const QString pol = a ? a->text().trimmed() : QString();
            if (pol.isEmpty())
                continue;
            pairs.push_back(ss(pol));
            pairs.push_back(b ? ss(b->text()) : std::string());
        }
        values_[LidLayer::Removals] = pairs;
    }
}

core::LidControl LidControlEditor::current() const
{
    core::LidControl c;
    c.name = ss(name_->text());
    c.type = ss(typeCode());
    const auto* info = core::findLidType(c.type);
    if (!info)
        return c;
    for (LidLayer layer : info->layers)
    {
        auto it = values_.find(layer);
        if (it == values_.end())
            continue;
        std::vector<std::string> v = it->second;
        if (layer == LidLayer::Removals)
        {
            if (!v.empty())
                c.layers[layer] = v;
            continue;
        }
        const auto& params = core::lidLayerParams(layer);
        if (v.size() > params.size())
            v.resize(params.size());
        if (layer == LidLayer::Storage && c.type != "RB" && v.size() > 4)
            v.resize(4);
        c.layers[layer] = v;
    }
    return c;
}

void LidControlEditor::refresh()
{
    if (loading_)
        return;
    collect();
    const auto c = current();
    schematic_->setControl(c, unitSystem());
    auto issues = core::validateLidControl(c, &doc_->model().document());
    const QString newName = name_->text().trimmed();
    if (newName != id_ && !newName.isEmpty())
    {
        for (const auto& n : core::lidControlNames(doc_->model().document()))
        {
            if (qs(n) == newName)
                issues.push_back({core::ObjectIssue::Severity::Error, "name",
                                  ss(tr("Another LID control is named %1.").arg(newName))});
        }
    }
    showIssues(issues);
    QSet<QString> bad;
    for (const auto& i : issues)
        if (i.severity == core::ObjectIssue::Severity::Error)
            bad.insert(qs(i.field));
    for (const auto& f : fields_)
    {
        const QString key = QString(core::lidLayerKeyword(f.layer)) + "/" +
                            qs(core::lidLayerParams(f.layer)[static_cast<std::size_t>(f.index)].key);
        markInvalid(f.widget(), bad.contains(key));
    }
    markInvalid(name_, bad.contains("name"));
    if (hasPendingEdits())
        setDirty(true);
}

bool LidControlEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (auto* w = qobject_cast<QWidget*>(watched))
            paramHelp_->setText("<b>" + w->property("helpTitle").toString().toHtmlEscaped() + "</b>: " +
                                w->toolTip().toHtmlEscaped());
    }
    return SpecialEditor::eventFilter(watched, event);
}

void LidControlEditor::apply()
{
    collect();
    const auto c = current();
    refresh();
    if (hasErrors_)
        return;
    const std::string old = exists_ ? ss(id_) : std::string();
    QString err;
    const bool ok = doc_->edit(tr("Edit LID control %1").arg(qs(c.name)),
                               [&](core::Model& m, std::string*) {
                                   core::writeLidControl(m.document(), c, old.empty() ? c.name : old);
                                   if (!old.empty() && old != c.name)
                                       core::renameSwmmObjectReferences(m.document(), "LID_CONTROLS", old, c.name);
                                   m.rebuild();
                                   return true;
                               },
                               &err);
    if (!ok)
    {
        setState(err);
        return;
    }
    applied(qs(c.name));
}

// ---------------------------------------------------------------------------
// LidUsageDialog

namespace
{
enum UsageColumn
{
    ColLid,
    ColNumber,
    ColArea,
    ColWidth,
    ColInitSat,
    ColFromImp,
    ColFromPerv,
    ColToPerv,
    ColDrainTo,
    ColReport,
    ColCount
};
} // namespace

LidUsageDialog::LidUsageDialog(ProjectDocument* doc, const QString& subcatchment, QWidget* parent)
    : QDialog(parent), doc_(doc), subcatchment_(subcatchment)
{
    setWindowTitle(tr("LID usage of subcatchment %1").arg(subcatchment));
    resize(1120, 460);
    const auto& m = doc_->model();
    for (const auto& n : core::lidControlNames(m.document()))
        lidNames_ << qs(n);
    drainTargets_ << QString();
    for (const auto& n : m.nodes())
        drainTargets_ << qs(n.id);
    for (const auto& a : m.areas())
        if (qs(a.id) != subcatchment)
            drainTargets_ << qs(a.id);

    auto* layout = new QVBoxLayout(this);
    auto* intro = mutedLabel(tr("LID units placed in the subcatchment. Each row is one LID control, used by a "
                                "number of identical units. Areas are per unit; the treated percentages "
                                "refer to the subcatchment's impervious and pervious areas."),
                             this);
    layout->addWidget(intro);
    if (lidNames_.isEmpty())
    {
        auto* none = new QLabel(tr("The project has no LID controls yet. Define one first in [LID_CONTROLS]."), this);
        none->setStyleSheet("color: #c47800;");
        layout->addWidget(none);
    }
    const bool si = m.units().system == core::UnitSystem::SI;
    const QString len = si ? "m" : "ft";
    table_ = new QTableWidget(0, ColCount, this);
    table_->setHorizontalHeaderLabels({tr("LID control"), tr("Units"), tr("Area each\n(%1)").arg(sq(len)),
                                       tr("Width\n(%1)").arg(len), tr("Initially\nsaturated (%)"),
                                       tr("Impervious\ntreated (%)"), tr("Pervious\ntreated (%)"),
                                       tr("Outflow to\npervious"), tr("Drain to"), tr("Report file")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(32);
    table_->setAlternatingRowColors(true);
    const QStringList tips = {
        tr("LID control used by these units"),
        tr("Number of identical units"),
        tr("Surface area of one unit"),
        tr("Width of the outflow face of one unit (for surface outflow)"),
        tr("Percent of the unit's storage that is full at the start"),
        tr("Percent of the impervious area's runoff routed to the units"),
        tr("Percent of the pervious area's runoff routed to the units"),
        tr("Send the units' surface outflow to the pervious area instead of the outlet"),
        tr("Node or subcatchment that receives the drain flow; empty for the subcatchment's outlet"),
        tr("Optional file for a detailed report of the units"),
    };
    for (int c = 0; c < ColCount; ++c)
        table_->horizontalHeaderItem(c)->setToolTip(tips[c]);
    layout->addWidget(table_, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(icons::get("new"), tr("Add LID unit"), this);
    auto* del = new QPushButton(icons::get("delete"), tr("Remove"), this);
    add->setEnabled(!lidNames_.isEmpty());
    row->addWidget(add);
    row->addWidget(del);
    row->addSpacing(16);
    summary_ = new QLabel(this);
    row->addWidget(summary_, 1);
    layout->addLayout(row);
    messages_ = new QListWidget(this);
    messages_->setMaximumHeight(72);
    layout->addWidget(messages_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &LidUsageDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, [this] {
        core::LidUsage u;
        u.lid = lidNames_.isEmpty() ? std::string() : ss(lidNames_.front());
        u.area = 100.0;
        u.width = 10.0;
        addRow(u);
        refresh();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        if (table_->currentRow() >= 0)
            table_->removeRow(table_->currentRow());
        refresh();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this] { refresh(); });

    for (const auto& u : core::readLidUsage(m.document(), ss(subcatchment)))
        addRow(u);
    refresh();
}

void LidUsageDialog::addRow(const core::LidUsage& u)
{
    const QSignalBlocker block(table_);
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto* lid = new QComboBox(table_);
    lid->addItems(lidNames_);
    if (lid->findText(qs(u.lid)) < 0 && !u.lid.empty())
        lid->addItem(qs(u.lid));
    lid->setCurrentText(qs(u.lid));
    table_->setCellWidget(r, ColLid, lid);
    connect(lid, &QComboBox::currentTextChanged, this, [this] { refresh(); });
    auto setNum = [&](int c, double v) { table_->setItem(r, c, new QTableWidgetItem(numText(v))); };
    table_->setItem(r, ColNumber, new QTableWidgetItem(QString::number(u.number)));
    setNum(ColArea, u.area);
    setNum(ColWidth, u.width);
    setNum(ColInitSat, u.initSat);
    setNum(ColFromImp, u.fromImp);
    setNum(ColFromPerv, u.fromPerv);
    auto* toPerv = new QTableWidgetItem;
    toPerv->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
    toPerv->setCheckState(u.toPervious ? Qt::Checked : Qt::Unchecked);
    table_->setItem(r, ColToPerv, toPerv);
    auto* drain = new QComboBox(table_);
    drain->setEditable(true);
    drain->addItems(drainTargets_);
    drain->setCurrentText(qs(u.drainTo));
    table_->setCellWidget(r, ColDrainTo, drain);
    connect(drain, &QComboBox::currentTextChanged, this, [this] { refresh(); });
    table_->setItem(r, ColReport, new QTableWidgetItem(qs(u.reportFile)));
}

std::vector<core::LidUsage> LidUsageDialog::units() const
{
    std::vector<core::LidUsage> out;
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        auto cell = [&](int c) {
            const auto* it = table_->item(r, c);
            return it ? it->text().trimmed() : QString();
        };
        auto num = [&](int c) { return text::toDouble(ss(cell(c))).value_or(-1.0); };
        core::LidUsage u;
        if (auto* lid = qobject_cast<QComboBox*>(table_->cellWidget(r, ColLid)))
            u.lid = ss(lid->currentText());
        u.number = static_cast<int>(text::toInt(ss(cell(ColNumber))).value_or(-1));
        u.area = num(ColArea);
        u.width = num(ColWidth);
        u.initSat = num(ColInitSat);
        u.fromImp = num(ColFromImp);
        u.fromPerv = cell(ColFromPerv).isEmpty() ? 0.0 : num(ColFromPerv);
        const auto* tp = table_->item(r, ColToPerv);
        u.toPervious = tp && tp->checkState() == Qt::Checked;
        if (auto* d = qobject_cast<QComboBox*>(table_->cellWidget(r, ColDrainTo)))
            u.drainTo = ss(d->currentText());
        u.reportFile = ss(cell(ColReport));
        out.push_back(u);
    }
    return out;
}

void LidUsageDialog::refresh()
{
    const auto list = units();
    const auto& m = doc_->model();
    const auto sum = core::lidAreaSummary(m, ss(subcatchment_), list);
    const QString unit = m.units().system == core::UnitSystem::SI ? tr("ha") : tr("ac");
    QString text = tr("LID area %1 %3 of %2 %3").arg(numText(std::round(sum.lidArea * 10000) / 10000),
                                                    numText(sum.subcatchmentArea), unit);
    if (sum.subcatchmentArea > 0)
        text += tr("  (%1% of the subcatchment)").arg(qs(text::formatDouble(sum.percent, 3)));
    summary_->setText(text);
    summary_->setStyleSheet(sum.percent > 100.0 ? "color: #c84b4b; font-weight: 600;" : QString());
    const auto issues = core::validateLidUsage(m, ss(subcatchment_), list);
    messages_->clear();
    for (const auto& i : issues)
    {
        const bool err = i.severity == core::ObjectIssue::Severity::Error;
        new QListWidgetItem(icons::get(err ? "error" : "warning"), qs(i.message), messages_);
    }
    if (issues.empty())
    {
        auto* item = new QListWidgetItem(icons::get("info"), tr("No problems found."), messages_);
        item->setForeground(theme::kSuccess);
    }
    ok_->setEnabled(!core::hasErrors(issues));
}

void LidUsageDialog::accept()
{
    const auto list = units();
    if (core::hasErrors(core::validateLidUsage(doc_->model(), ss(subcatchment_), list)))
        return;
    const std::string sub = ss(subcatchment_);
    if (!list.empty() || !core::readLidUsage(doc_->model().document(), sub).empty())
    {
        QString err;
        const bool ok = doc_->edit(tr("LID usage of %1").arg(subcatchment_),
                                   [&](core::Model& m, std::string*) {
                                       core::writeLidUsage(m.document(), sub, list);
                                       m.rebuild();
                                       return true;
                                   },
                                   &err);
        if (!ok)
        {
            new QListWidgetItem(icons::get("error"), err, messages_);
            return;
        }
    }
    QDialog::accept();
}

bool LidUsageDialog::edit(ProjectDocument* doc, const QString& subcatchment, QWidget* parent)
{
    if (!doc->model().findArea(ss(subcatchment)))
        return false;
    LidUsageDialog dlg(doc, subcatchment, parent);
    return dlg.exec() == QDialog::Accepted;
}

// ---------------------------------------------------------------------------
// TransectEditor

TransectEditor::TransectEditor(ProjectDocument* doc, const QString& id, QWidget* parent)
    : SpecialEditor(doc, "TRANSECTS", id, parent)
{
    auto* split = new QSplitter(Qt::Horizontal, body());
    auto* bl = new QVBoxLayout(body());
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(split);

    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(14, 12, 8, 8);
    const QString len = unitSystem() == core::UnitSystem::SI ? "m" : "ft";
    auto edit = [&](QWidget* parent, const QString& tip) {
        auto* e = new QLineEdit(parent);
        e->setToolTip(tip);
        e->setMinimumWidth(64);
        e->setMaximumWidth(90);
        connect(e, &QLineEdit::textEdited, this, [this] {
            setDirty(true);
            refresh();
        });
        return e;
    };
    auto* top = new QFormLayout;
    name_ = new QLineEdit(left);
    connect(name_, &QLineEdit::textEdited, this, [this] {
        setDirty(true);
        refresh();
    });
    top->addRow(tr("Name"), name_);
    ll->addLayout(top);

    // Roughness, banks and modifiers in one compact grid.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    nLeft_ = edit(left, tr("Manning's n of the left overbank"));
    nChannel_ = edit(left, tr("Manning's n of the main channel"));
    nRight_ = edit(left, tr("Manning's n of the right overbank"));
    leftBank_ = edit(left, tr("Station where the left overbank ends and the main channel begins"));
    rightBank_ = edit(left, tr("Station where the main channel ends and the right overbank begins"));
    stationFactor_ = edit(left, tr("Factor applied to every station to widen or narrow the section; 0 for none"));
    elevationOffset_ = edit(left, tr("Constant added to every elevation"));
    meander_ = edit(left, tr("Ratio of the main channel length to the overbank length; 0 for none"));
    auto put = [&](int row, int col, const QString& label, QLineEdit* e) {
        auto* l = new QLabel(label, left);
        l->setToolTip(e->toolTip());
        grid->addWidget(l, row, col * 2 + 1, Qt::AlignRight);
        grid->addWidget(e, row, col * 2 + 2);
    };
    auto head = [&](int row, const QString& label) {
        auto* l = new QLabel(label, left);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        grid->addWidget(l, row, 0);
    };
    head(0, tr("Manning's n"));
    put(0, 0, tr("Left"), nLeft_);
    put(0, 1, tr("Channel"), nChannel_);
    put(0, 2, tr("Right"), nRight_);
    head(1, tr("Banks (%1)").arg(len));
    put(1, 0, tr("Left"), leftBank_);
    put(1, 2, tr("Right"), rightBank_);
    head(2, tr("Modifiers"));
    put(2, 0, tr("Stations"), stationFactor_);
    put(2, 1, tr("Elevation"), elevationOffset_);
    put(2, 2, tr("Meander"), meander_);
    grid->setColumnStretch(7, 1);
    ll->addLayout(grid);
    ll->addSpacing(6);

    table_ = new QTableWidget(0, 2, left);
    table_->setHorizontalHeaderLabels({tr("Station (%1)").arg(len), tr("Elevation (%1)").arg(len)});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->setAlternatingRowColors(true);
    table_->setToolTip(tr("Stations from left to right, looking downstream. Paste two columns with Ctrl+V."));
    ll->addWidget(table_, 1);
    auto* tr_ = new QHBoxLayout;
    auto* add = new QPushButton(icons::get("new"), tr("Add row"), left);
    auto* del = new QPushButton(icons::get("delete"), tr("Delete row"), left);
    tr_->addWidget(add);
    tr_->addWidget(del);
    tr_->addStretch(1);
    ll->addLayout(tr_);
    connect(add, &QPushButton::clicked, this, [this] {
        const int r = table_->currentRow() >= 0 ? table_->currentRow() + 1 : table_->rowCount();
        table_->insertRow(r);
        setDirty(true);
        refresh();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        if (table_->currentRow() >= 0)
            table_->removeRow(table_->currentRow());
        setDirty(true);
        refresh();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this] {
        if (loading_)
            return;
        setDirty(true);
        refresh();
    });
    auto* pasteKey = new QShortcut(QKeySequence::Paste, table_);
    pasteKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteKey, &QShortcut::activated, this, &TransectEditor::paste);

    auto* right = new QWidget(split);
    auto* rl2 = new QVBoxLayout(right);
    rl2->setContentsMargins(8, 8, 14, 8);
    chart_ = new ChartWidget(right);
    chart_->setXAxis(tr("Station (%1)").arg(len), false);
    rl2->addWidget(chart_, 1);
    auto* dl = new QHBoxLayout;
    dl->addWidget(new QLabel(tr("Water depth"), right));
    depth_ = new QSlider(Qt::Horizontal, right);
    depth_->setRange(0, 100);
    depth_->setValue(60);
    depth_->setToolTip(tr("Preview a water surface, as a percent of the full depth"));
    dl->addWidget(depth_, 1);
    rl2->addLayout(dl);
    geometry_ = mutedLabel({}, right);
    rl2->addWidget(geometry_);
    connect(depth_, &QSlider::valueChanged, this, [this] { refresh(); });
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({400, 600});
    load();
}

void TransectEditor::load()
{
    loading_ = true;
    auto t = core::readTransect(doc_->model().document(), ss(id_));
    exists_ = t.has_value();
    if (!t)
    {
        t = core::Transect{};
        t->name = ss(id_.isEmpty() ? QStringLiteral("Transect1") : id_);
        t->nLeft = t->nRight = 0.05;
        t->nChannel = 0.03;
        t->leftBank = 10;
        t->rightBank = 40;
        t->points = {{0, 10}, {10, 6}, {20, 1}, {30, 1}, {40, 6}, {50, 10}};
    }
    name_->setText(qs(t->name));
    nLeft_->setText(numText(t->nLeft));
    nChannel_->setText(numText(t->nChannel));
    nRight_->setText(numText(t->nRight));
    leftBank_->setText(numText(t->leftBank));
    rightBank_->setText(numText(t->rightBank));
    stationFactor_->setText(numText(t->stationFactor));
    elevationOffset_->setText(numText(t->elevationOffset));
    meander_->setText(numText(t->meander));
    table_->setRowCount(0);
    for (const auto& [x, z] : t->points)
    {
        const int r = table_->rowCount();
        table_->insertRow(r);
        table_->setItem(r, 0, new QTableWidgetItem(numText(x)));
        table_->setItem(r, 1, new QTableWidgetItem(numText(z)));
    }
    loading_ = false;
    setTitle(tr("Transect %1").arg(name_->text()));
    refresh();
    if (exists_)
    {
        setDirty(false);
        setState(tr("Stations from left to right looking downstream; the chart applies the modifiers."));
    }
    else
    {
        setDirty(true);
        setState(tr("New transect; Apply adds it to the project."));
    }
}

core::Transect TransectEditor::current(std::vector<core::ObjectIssue>* issues) const
{
    core::Transect t;
    t.name = ss(name_->text());
    auto read = [&](QLineEdit* e, double& out, const QString& what) {
        if (e->text().trimmed().isEmpty())
        {
            out = 0.0;
            return;
        }
        if (auto v = numOf(e))
            out = *v;
        else if (issues)
            issues->push_back({core::ObjectIssue::Severity::Error, {}, ss(tr("%1 is not a number.").arg(what))});
    };
    read(nLeft_, t.nLeft, tr("Left overbank n"));
    read(nChannel_, t.nChannel, tr("Channel n"));
    read(nRight_, t.nRight, tr("Right overbank n"));
    read(leftBank_, t.leftBank, tr("Left bank station"));
    read(rightBank_, t.rightBank, tr("Right bank station"));
    read(stationFactor_, t.stationFactor, tr("Stations factor"));
    read(elevationOffset_, t.elevationOffset, tr("Elevation offset"));
    read(meander_, t.meander, tr("Meander factor"));
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        const auto* a = table_->item(r, 0);
        const auto* b = table_->item(r, 1);
        const QString xs = a ? a->text().trimmed() : QString();
        const QString zs = b ? b->text().trimmed() : QString();
        if (xs.isEmpty() && zs.isEmpty())
            continue;
        const auto x = text::toDouble(ss(xs));
        const auto z = text::toDouble(ss(zs));
        if (!x || !z)
        {
            if (issues)
                issues->push_back({core::ObjectIssue::Severity::Error, "points",
                                   ss(tr("Row %1 needs a station and an elevation.").arg(r + 1))});
            continue;
        }
        t.points.emplace_back(*x, *z);
    }
    return t;
}

void TransectEditor::refresh()
{
    if (loading_)
        return;
    std::vector<core::ObjectIssue> issues;
    const auto t = current(&issues);
    const auto more = core::validateTransect(t);
    issues.insert(issues.end(), more.begin(), more.end());
    const QString newName = name_->text().trimmed();
    if (newName != id_ || !exists_)
    {
        for (const auto& n : core::transectNames(doc_->model().document()))
        {
            if (qs(n) == newName && (newName != id_ || !exists_))
                issues.push_back({core::ObjectIssue::Severity::Error, "name",
                                  ss(tr("Another transect is named %1.").arg(newName))});
        }
    }
    showIssues(issues);
    if (hasPendingEdits())
        setDirty(true);

    const QString len = unitSystem() == core::UnitSystem::SI ? "m" : "ft";
    const auto pts = t.adjustedPoints();
    if (pts.size() < 2)
    {
        chart_->clear(tr("Enter at least two stations to see the cross-section."));
        geometry_->clear();
        return;
    }
    double zmin = pts.front().second, zmax = zmin;
    for (const auto& [x, z] : pts)
    {
        zmin = std::min(zmin, z);
        zmax = std::max(zmax, z);
    }
    const double wl = zmin + (zmax - zmin) * depth_->value() / 100.0;
    ChartWidget::Series ground;
    ground.name = tr("Ground");
    ground.style = ChartWidget::Series::Style::Line;
    ground.color = QColor(120, 90, 60);
    ground.width = 2.2;
    ChartWidget::Series water;
    water.name = tr("Water");
    water.style = ChartWidget::Series::Style::Band;
    water.color = QColor(50, 130, 220);
    // Water band: from the ground (or the surface where the ground is higher) up to the surface.
    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        const auto [x, z] = pts[i];
        if (i > 0)
        {
            const auto [x0, z0] = pts[i - 1];
            if ((z0 - wl) * (z - wl) < 0)
            {
                const double xi = x0 + (wl - z0) * (x - x0) / (z - z0);
                water.points.append(QPointF(xi, wl));
                water.upper.append(QPointF(xi, wl));
            }
        }
        ground.points.append(QPointF(x, z));
        water.points.append(QPointF(x, std::min(z, wl)));
        water.upper.append(QPointF(x, wl));
    }
    ChartWidget::Series fill = ground;
    fill.name = tr("Section");
    fill.style = ChartWidget::Series::Style::Band;
    fill.color = QColor(176, 133, 88);
    fill.inLegend = false;
    fill.upper.clear();
    for (const auto& pt : ground.points)
        fill.upper.append(QPointF(pt.x(), zmin - (zmax - zmin) * 0.08));
    auto bank = [&](double x, const QString& name, bool legend) {
        ChartWidget::Series s;
        s.name = name;
        s.color = QColor(110, 120, 135);
        s.dashed = true;
        s.width = 1.4;
        s.inLegend = legend;
        s.points = {QPointF(x, zmin), QPointF(x, zmax)};
        return s;
    };
    // A round bottom for the axis so the section fill reaches it.
    const double raw = std::max(1e-6, zmax - zmin) / 5.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = 10.0 * mag;
    for (double k : {1.0, 2.0, 2.5, 5.0, 10.0})
    {
        if (raw <= k * mag)
        {
            step = k * mag;
            break;
        }
    }
    const double bottom = std::floor((zmin - 0.04 * (zmax - zmin)) / step) * step;
    for (auto& pt : fill.upper)
        pt.setY(bottom);
    chart_->setYRange(bottom, std::nullopt);
    chart_->setSeries({fill, water, ground, bank(t.adjustedLeftBank(), tr("Banks"), true),
                       bank(t.adjustedRightBank(), tr("Right bank"), false)},
                      tr("Elevation (%1)").arg(len), tr("Transect %1").arg(name_->text().trimmed()));
    const auto g = core::transectGeometry(t, wl);
    geometry_->setText(tr("Depth %1 %2: flow area %3 %4, top width %5 %2, wetted perimeter %6 %2, "
                          "hydraulic radius %7 %2.")
                           .arg(qs(text::formatDouble(wl - zmin, 4)), len, qs(text::formatDouble(g.area, 5)),
                                sq(len), qs(text::formatDouble(g.topWidth, 5)),
                                qs(text::formatDouble(g.wettedPerimeter, 5)),
                                qs(text::formatDouble(g.hydraulicRadius, 4))));
}

void TransectEditor::paste()
{
    const QStringList lines = QApplication::clipboard()->text().split('\n', Qt::SkipEmptyParts);
    int r = std::max(0, table_->currentRow());
    loading_ = true;
    for (const QString& line : lines)
    {
        const QStringList cells = line.split(QRegularExpression("[\\t,; ]+"), Qt::SkipEmptyParts);
        if (cells.size() < 2)
            continue;
        if (r >= table_->rowCount())
            table_->insertRow(r);
        table_->setItem(r, 0, new QTableWidgetItem(cells[0].trimmed()));
        table_->setItem(r, 1, new QTableWidgetItem(cells[1].trimmed()));
        ++r;
    }
    loading_ = false;
    setDirty(true);
    refresh();
}

void TransectEditor::apply()
{
    std::vector<core::ObjectIssue> issues;
    const auto t = current(&issues);
    refresh();
    if (hasErrors_)
        return;
    const std::string old = exists_ ? ss(id_) : std::string();
    QString err;
    const bool ok = doc_->edit(tr("Edit transect %1").arg(qs(t.name)),
                               [&](core::Model& m, std::string*) {
                                   core::writeTransect(m.document(), t, old.empty() ? t.name : old);
                                   if (!old.empty() && old != t.name)
                                       core::renameSwmmObjectReferences(m.document(), "TRANSECTS", old, t.name);
                                   m.rebuild();
                                   return true;
                               },
                               &err);
    if (!ok)
    {
        setState(err);
        return;
    }
    applied(qs(t.name));
}

// ---------------------------------------------------------------------------
// HydrographEditor

namespace
{
const char* kMonthNames[] = {QT_TRANSLATE_NOOP("HydrographEditor", "All months"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "January"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "February"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "March"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "April"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "May"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "June"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "July"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "August"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "September"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "October"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "November"),
                             QT_TRANSLATE_NOOP("HydrographEditor", "December")};
} // namespace

HydrographEditor::HydrographEditor(ProjectDocument* doc, const QString& id, QWidget* parent)
    : SpecialEditor(doc, "HYDROGRAPHS", id, parent)
{
    auto* bl = new QVBoxLayout(body());
    bl->setContentsMargins(14, 10, 14, 6);
    auto* top = new QHBoxLayout;
    name_ = new QLineEdit(body());
    name_->setMaximumWidth(200);
    gage_ = new QComboBox(body());
    gage_->setEditable(true);
    gage_->setMinimumWidth(140);
    gage_->setToolTip(tr("Rain gage whose rainfall drives the unit hydrographs"));
    top->addWidget(new QLabel(tr("Name"), body()));
    top->addWidget(name_);
    top->addSpacing(16);
    top->addWidget(new QLabel(tr("Rain gage"), body()));
    top->addWidget(gage_);
    top->addStretch(1);
    bl->addLayout(top);
    connect(name_, &QLineEdit::textEdited, this, [this] {
        setDirty(true);
        refresh();
    });
    connect(gage_, &QComboBox::currentTextChanged, this, [this] {
        if (loading_)
            return;
        setDirty(true);
        refresh();
    });

    auto* split = new QSplitter(Qt::Horizontal, body());
    months_ = new QListWidget(split);
    months_->setMaximumWidth(200);
    for (const char* m : kMonthNames)
        new QListWidgetItem(tr(m), months_);
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 0, 0, 0);
    const bool si = unitSystem() == core::UnitSystem::SI;
    const QString d = si ? "mm" : "in";
    table_ = new QTableWidget(3, 6, right);
    table_->setHorizontalHeaderLabels({tr("R"), tr("T (hr)"), tr("K"), tr("Max. IA\n(%1)").arg(d),
                                       tr("IA recovery\n(%1/day)").arg(d), tr("Initial IA\n(%1)").arg(d)});
    table_->setVerticalHeaderLabels({tr("Short term"), tr("Medium term"), tr("Long term")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setDefaultSectionSize(26);
    const QStringList tips = {tr("Fraction of the rainfall volume that enters the sewer"),
                              tr("Time from the start of rain to the peak of the hydrograph"),
                              tr("Ratio of the recession time to the time to peak"),
                              tr("Maximum initial abstraction depth (optional)"),
                              tr("Rate at which the initial abstraction recovers in dry weather (optional)"),
                              tr("Initial abstraction already used at the start (optional)")};
    for (int c = 0; c < 6; ++c)
        table_->horizontalHeaderItem(c)->setToolTip(tips[c]);
    table_->setMaximumHeight(table_->horizontalHeader()->sizeHint().height() + 3 * 26 + 6);
    rl->addWidget(table_);
    note_ = mutedLabel({}, right);
    rl->addWidget(note_);
    chart_ = new ChartWidget(right);
    chart_->setXAxis(tr("Time (hours)"), false);
    rl->addWidget(chart_, 1);
    split->addWidget(months_);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({170, 800});
    bl->addWidget(split, 1);

    connect(months_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            showMonth(row);
    });
    connect(table_, &QTableWidget::itemChanged, this, [this] {
        if (loading_)
            return;
        storeMonth();
        setDirty(true);
        refresh();
    });
    load();
}

void HydrographEditor::load()
{
    loading_ = true;
    auto g = core::readUnitHydrograph(doc_->model().document(), ss(id_));
    exists_ = g.has_value();
    if (!g)
    {
        g = core::UnitHydrographGroup{};
        g->name = ss(id_.isEmpty() ? QStringLiteral("UH1") : id_);
        if (!doc_->model().gages().empty())
            g->gage = doc_->model().gages().front().id;
        g->params[0][0] = {true, 0.01, 1, 2, std::nullopt, std::nullopt, std::nullopt};
        g->params[0][1] = {true, 0.02, 4, 3, std::nullopt, std::nullopt, std::nullopt};
        g->params[0][2] = {true, 0.03, 10, 4, std::nullopt, std::nullopt, std::nullopt};
    }
    group_ = *g;
    name_->setText(qs(group_.name));
    gage_->clear();
    for (const auto& gg : doc_->model().gages())
        gage_->addItem(qs(gg.id));
    gage_->setCurrentText(qs(group_.gage));
    loading_ = false;
    updateMonthLabels();
    months_->setCurrentRow(month_);
    showMonth(month_);
    setTitle(tr("Unit hydrographs %1").arg(name_->text()));
    refresh();
    if (exists_)
    {
        setDirty(false);
        setState(tr("R-T-K parameters by month; months without values use the All months values."));
    }
    else
    {
        setDirty(true);
        setState(tr("New unit hydrograph group; Apply adds it to the project."));
    }
}

void HydrographEditor::updateMonthLabels()
{
    for (int m = 0; m < months_->count(); ++m)
    {
        auto* item = months_->item(m);
        const bool defined = group_.monthDefined(m);
        QFont f = item->font();
        f.setBold(defined);
        item->setFont(f);
        item->setForeground(defined ? theme::kAccent : theme::kText);
        item->setSizeHint(QSize(0, 24));
        item->setToolTip(defined ? tr("Has its own parameters") : tr("Uses the All months parameters"));
    }
}

void HydrographEditor::showMonth(int month)
{
    month_ = month;
    const bool was = loading_;
    loading_ = true;
    const auto& rows = group_.params[static_cast<std::size_t>(month)];
    for (int r = 0; r < 3; ++r)
    {
        const auto& p = rows[static_cast<std::size_t>(r)];
        auto cell = [&](int c, const QString& v) { table_->setItem(r, c, new QTableWidgetItem(v)); };
        cell(0, p.defined ? numText(p.r) : QString());
        cell(1, p.defined ? numText(p.t) : QString());
        cell(2, p.defined ? numText(p.k) : QString());
        cell(3, p.iaMax ? numText(*p.iaMax) : QString());
        cell(4, p.iaRecovery ? numText(*p.iaRecovery) : QString());
        cell(5, p.iaInitial ? numText(*p.iaInitial) : QString());
    }
    loading_ = was;
    refresh();
}

void HydrographEditor::storeMonth()
{
    auto& rows = group_.params[static_cast<std::size_t>(month_)];
    for (int r = 0; r < 3; ++r)
    {
        auto get = [&](int c) -> std::optional<double> {
            const auto* it = table_->item(r, c);
            return it ? text::toDouble(ss(it->text())) : std::nullopt;
        };
        auto filled = [&](int c) {
            const auto* it = table_->item(r, c);
            return it && !it->text().trimmed().isEmpty();
        };
        auto& p = rows[static_cast<std::size_t>(r)];
        p.defined = filled(0) || filled(1) || filled(2);
        p.r = get(0).value_or(0.0);
        p.t = get(1).value_or(0.0);
        p.k = get(2).value_or(0.0);
        p.iaMax = get(3);
        p.iaRecovery = get(4);
        p.iaInitial = get(5);
    }
    updateMonthLabels();
}

void HydrographEditor::refresh()
{
    if (loading_)
        return;
    group_.name = ss(name_->text());
    group_.gage = ss(gage_->currentText());
    auto issues = core::validateUnitHydrograph(group_, &doc_->model());
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 6; ++c)
        {
            const auto* it = table_->item(r, c);
            if (it && !it->text().trimmed().isEmpty() && !text::toDouble(ss(it->text())))
                issues.push_back({core::ObjectIssue::Severity::Error, {},
                                  ss(tr("%1, %2: \"%3\" is not a number.")
                                         .arg(table_->verticalHeaderItem(r)->text(),
                                              table_->horizontalHeaderItem(c)->text(), it->text()))});
        }
    }
    const QString newName = name_->text().trimmed();
    if (newName != id_ || !exists_)
    {
        for (const auto& n : core::unitHydrographNames(doc_->model().document()))
            if (qs(n) == newName && (newName != id_ || !exists_))
                issues.push_back({core::ObjectIssue::Severity::Error, "name",
                                  ss(tr("Another unit hydrograph group is named %1.").arg(newName))});
    }
    showIssues(issues);
    if (hasPendingEdits())
        setDirty(true);

    int shown = month_;
    if (!group_.monthDefined(month_))
    {
        shown = 0;
        note_->setText(month_ == 0 ? tr("No parameters yet: enter R, T and K for at least one response.")
                                   : tr("%1 has no parameters of its own and uses the All months values (shown).")
                                         .arg(tr(kMonthNames[month_])));
    }
    else
    {
        note_->setText(tr("The area under each triangle equals its R value; the total is the RDII per unit "
                          "of rainfall."));
    }
    QVector<ChartWidget::Series> series;
    const QStringList names = {tr("Short term"), tr("Medium term"), tr("Long term")};
    std::vector<std::vector<std::pair<double, double>>> tris;
    std::vector<double> xs;
    for (int r = 0; r < 3; ++r)
    {
        const auto& p = group_.params[static_cast<std::size_t>(shown)][static_cast<std::size_t>(r)];
        const auto tri = core::triangularHydrograph(p);
        tris.push_back(tri);
        ChartWidget::Series s;
        s.name = names[r];
        for (const auto& [x, y] : tri)
        {
            s.points.append(QPointF(x, y));
            xs.push_back(x);
        }
        if (!s.points.isEmpty())
            series.append(s);
    }
    if (series.isEmpty())
    {
        chart_->clear(tr("Enter R, T and K to see the unit hydrographs."));
        return;
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    ChartWidget::Series total;
    total.name = tr("Total");
    total.color = QColor(110, 120, 135);
    total.dashed = true;
    for (double x : xs)
    {
        double y = 0.0;
        for (const auto& tri : tris)
        {
            if (tri.size() != 3 || x <= 0 || x >= tri[2].first)
                continue;
            y += x <= tri[1].first ? tri[1].second * x / tri[1].first
                                   : tri[1].second * (tri[2].first - x) / (tri[2].first - tri[1].first);
        }
        total.points.append(QPointF(x, y));
    }
    series.append(total);
    chart_->setSeries(series, tr("Ordinate (1/hr)"),
                      tr("%1, %2").arg(name_->text().trimmed(), tr(kMonthNames[shown])));
}

void HydrographEditor::apply()
{
    storeMonth();
    refresh();
    if (hasErrors_)
        return;
    const auto g = group_;
    const std::string old = exists_ ? ss(id_) : std::string();
    QString err;
    const bool ok = doc_->edit(tr("Edit unit hydrographs %1").arg(qs(g.name)),
                               [&](core::Model& m, std::string*) {
                                   core::writeUnitHydrograph(m.document(), g, old.empty() ? g.name : old);
                                   if (!old.empty() && old != g.name)
                                       core::renameSwmmObjectReferences(m.document(), "HYDROGRAPHS", old, g.name);
                                   m.rebuild();
                                   return true;
                               },
                               &err);
    if (!ok)
    {
        setState(err);
        return;
    }
    applied(qs(g.name));
}

// ---------------------------------------------------------------------------
// Factory

bool hasSpecialEditor(core::Dialect dialect, const QString& section)
{
    if (dialect == core::Dialect::Swmm)
        return section == "LID_CONTROLS" || section == "LID_USAGE" || section == "TRANSECTS" ||
               section == "HYDROGRAPHS" || section == "CONTROLS";
    if (dialect == core::Dialect::Epanet)
        return section == "CONTROLS" || section == "RULES";
    return false;
}

SpecialEditor* createSpecialEditor(ProjectDocument* doc, const QString& section, const QString& id, QWidget* parent)
{
    const auto dialect = doc->model().dialect();
    if (!hasSpecialEditor(dialect, section))
        return nullptr;
    if (section == "LID_CONTROLS")
        return new LidControlEditor(doc, id, parent);
    if (section == "TRANSECTS")
        return new TransectEditor(doc, id, parent);
    if (section == "HYDROGRAPHS")
        return new HydrographEditor(doc, id, parent);
    if (section == "CONTROLS" || section == "RULES")
        return new ControlRulesEditor(doc, section, parent);
    return nullptr;
}

QString objectEditorTitle(core::Dialect dialect, const QString& section, const QString& id)
{
    if (section == "CONTROLS")
        return dialect == core::Dialect::Swmm ? QObject::tr("Control rules") : QObject::tr("Simple controls");
    if (section == "RULES")
        return QObject::tr("Rules");
    if (!id.isEmpty())
        return id;
    if (section == "LID_CONTROLS")
        return QObject::tr("New LID control");
    if (section == "TRANSECTS")
        return QObject::tr("New transect");
    return QObject::tr("New unit hydrographs");
}

QString objectEditorIcon(const QString& section)
{
    if (section == "CONTROLS" || section == "RULES")
        return "controls";
    if (section == "TRANSECTS")
        return "profile";
    if (section == "HYDROGRAPHS")
        return "chart";
    return "layers";
}

} // namespace mirab::app
