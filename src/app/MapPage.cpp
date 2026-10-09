/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapPage.cpp
 * Purpose: Map tab: display bar (result colors, time playback, basemap), the map and the start page.
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
#include "MapPage.h"

#include "Icons.h"
#include "MapView.h"
#include "Theme.h"
#include "mirab/core/Text.h"

#include <QActionGroup>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace mirab::app
{

// ---------------------------------------------------------------------------
// StartPage
// ---------------------------------------------------------------------------
namespace
{
QPushButton* bigButton(const QString& icon, const QString& title, const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(parent);
    b->setIcon(icons::get(icon));
    b->setIconSize(QSize(34, 34));
    b->setText(title + "\n" + text);
    b->setMinimumHeight(66);
    b->setStyleSheet("QPushButton { text-align: left; padding: 10px 14px; border-radius: 10px; font-size: 10pt; }"
                     "QPushButton:hover { border-color: #1f6feb; background: #f4f8ff; }");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QListWidget* fileList(QWidget* parent)
{
    auto* l = new QListWidget(parent);
    l->setFrameShape(QFrame::NoFrame);
    l->setCursor(Qt::PointingHandCursor);
    l->setStyleSheet("QListWidget { background: transparent; } QListWidget::item { padding: 6px 4px; border-radius: 6px; }"
                     "QListWidget::item:hover { background: #eef3fb; }");
    l->setIconSize(QSize(18, 18));
    l->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->setTextElideMode(Qt::ElideMiddle);
    l->setWordWrap(false);
    l->setUniformItemSizes(false);
    return l;
}
} // namespace

StartPage::StartPage(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch(1);
    auto* card = new QFrame(this);
    card->setObjectName("card");
    card->setMaximumWidth(860);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* hl = new QHBoxLayout;
    hl->addStretch(1);
    hl->addWidget(card, 8);
    hl->addStretch(1);
    outer->addLayout(hl);
    outer->addStretch(2);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    auto* titleRow = new QHBoxLayout;
    auto* logo = new QLabel(card);
    logo->setPixmap(QIcon(":/mirab/mirab.png").pixmap(48, 48));
    auto* titles = new QVBoxLayout;
    auto* heading = new QLabel(tr("Welcome to Mirab"), card);
    heading->setObjectName("heading");
    auto* sub = new QLabel(tr("Model water distribution networks with EPANET and stormwater systems with SWMM, side by side."),
                           card);
    sub->setObjectName("subheading");
    sub->setWordWrap(true);
    titles->addWidget(heading);
    titles->addWidget(sub);
    titleRow->addWidget(logo);
    titleRow->addSpacing(8);
    titleRow->addLayout(titles, 1);
    layout->addLayout(titleRow);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(12);
    auto* bEpanet = bigButton("Junction", tr("New water network"), tr("EPANET 2.2 / 2.3, MSX"), card);
    auto* bSwmm = bigButton("Subcatchment", tr("New stormwater model"), tr("EPA SWMM 5.2, SWMM5+"), card);
    auto* bOpen = bigButton("open", tr("Open project"), tr("EPANET or SWMM .inp file"), card);
    actions->addWidget(bEpanet, 1);
    actions->addWidget(bSwmm, 1);
    actions->addWidget(bOpen, 1);
    layout->addLayout(actions);
    connect(bEpanet, &QPushButton::clicked, this, &StartPage::newEpanet);
    connect(bSwmm, &QPushButton::clicked, this, &StartPage::newSwmm);
    connect(bOpen, &QPushButton::clicked, this, &StartPage::openRequested);

    auto* lists = new QHBoxLayout;
    lists->setSpacing(18);
    auto* recentBox = new QVBoxLayout;
    auto* recentTitle = new QLabel(tr("Recent projects"), card);
    recentTitle->setObjectName("panelTitle");
    recent_ = fileList(card);
    recentBox->addWidget(recentTitle);
    recentBox->addWidget(recent_);
    auto* exampleBox = new QVBoxLayout;
    auto* exampleTitle = new QLabel(tr("Examples"), card);
    exampleTitle->setObjectName("panelTitle");
    examples_ = fileList(card);
    exampleBox->addWidget(exampleTitle);
    exampleBox->addWidget(examples_);
    lists->addLayout(recentBox, 3);
    lists->addLayout(exampleBox, 2);
    layout->addLayout(lists);
    auto* hint = new QLabel(tr("Tip: drop an .inp file anywhere on this window to open it."), card);
    hint->setObjectName("muted");
    layout->addWidget(hint);

    for (auto* l : {recent_, examples_})
    {
        connect(l, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const QString f = item->data(Qt::UserRole).toString();
            if (!f.isEmpty())
                emit openFile(f);
        });
        l->setMinimumHeight(150);
    }
}

void StartPage::setRecent(const QStringList& files)
{
    recent_->clear();
    for (const QString& f : files)
    {
        if (!QFileInfo::exists(f))
            continue;
        auto* item = new QListWidgetItem(icons::get("report"), QFileInfo(f).fileName() + "\n" + QFileInfo(f).absolutePath(),
                                         recent_);
        item->setData(Qt::UserRole, f);
        item->setToolTip(f);
        if (recent_->count() >= 6)
            break;
    }
    if (recent_->count() == 0)
    {
        auto* none = new QListWidgetItem(tr("No recent projects yet"), recent_);
        none->setFlags(Qt::NoItemFlags);
    }
}

void StartPage::setExamples(const QStringList& files)
{
    examples_->clear();
    for (const QString& f : files)
    {
        const bool swmm = f.contains("swmm", Qt::CaseInsensitive);
        auto* item = new QListWidgetItem(icons::element(swmm ? "Subcatchment" : "Junction"), QFileInfo(f).completeBaseName(),
                                         examples_);
        item->setData(Qt::UserRole, f);
        item->setToolTip(f);
    }
}

void StartPage::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(244, 246, 249, 236));
}

// ---------------------------------------------------------------------------
// MapPage
// ---------------------------------------------------------------------------
namespace
{
QToolButton* barButton(const QString& icon, const QString& tip, QWidget* parent, bool checkable = false)
{
    auto* b = new QToolButton(parent);
    b->setIcon(icons::get(icon));
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    return b;
}

QLabel* barLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    return l;
}
} // namespace

MapPage::MapPage(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    bar_ = new QWidget(this);
    bar_->setObjectName("mapBar");
    bar_->setAttribute(Qt::WA_StyledBackground, true);
    auto* bl = new QHBoxLayout(bar_);
    bl->setContentsMargins(10, 5, 10, 5);
    bl->setSpacing(6);
    nodeVar_ = new QComboBox(bar_);
    linkVar_ = new QComboBox(bar_);
    areaVar_ = new QComboBox(bar_);
    for (auto* c : {nodeVar_, linkVar_, areaVar_})
    {
        c->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        c->setMinimumWidth(110);
    }
    nodeVar_->setToolTip(tr("Color the nodes by this result"));
    linkVar_->setToolTip(tr("Color the links by this result"));
    areaVar_->setToolTip(tr("Color the subcatchments by this result"));
    areaLabel_ = barLabel(tr("Areas"), bar_);
    bl->addWidget(barLabel(tr("Nodes"), bar_));
    bl->addWidget(nodeVar_);
    bl->addSpacing(4);
    bl->addWidget(barLabel(tr("Links"), bar_));
    bl->addWidget(linkVar_);
    bl->addSpacing(4);
    bl->addWidget(areaLabel_);
    bl->addWidget(areaVar_);
    bl->addSpacing(10);

    stepBack_ = barButton("step-back", tr("Previous time period"), bar_);
    play_ = barButton("play", tr("Animate the results over time"), bar_, true);
    stepForward_ = barButton("step-forward", tr("Next time period"), bar_);
    slider_ = new QSlider(Qt::Horizontal, bar_);
    slider_->setMinimumWidth(120);
    timeLabel_ = new QLabel(tr("No results"), bar_);
    timeLabel_->setMinimumWidth(130);
    bl->addWidget(stepBack_);
    bl->addWidget(play_);
    bl->addWidget(stepForward_);
    bl->addWidget(slider_, 1);
    bl->addWidget(timeLabel_);
    bl->addSpacing(10);

    basemap_ = barButton("basemap", tr("Background map"), bar_);
    basemap_->setPopupMode(QToolButton::InstantPopup);
    basemap_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    basemap_->setText(tr("Basemap"));
    basemap_->setMenu(new QMenu(basemap_));
    auto* labels = barButton("labels", tr("Show element labels"), bar_, true);
    auto* arrows = barButton("arrows", tr("Show flow direction arrows"), bar_, true);
    auto* legend = barButton("legend", tr("Show the legend"), bar_, true);
    labels->setChecked(QSettings().value("map/labels", true).toBool());
    arrows->setChecked(QSettings().value("map/arrows", true).toBool());
    legend->setChecked(QSettings().value("map/legend", true).toBool());
    bl->addWidget(basemap_);
    bl->addWidget(labels);
    bl->addWidget(arrows);
    bl->addWidget(legend);
    layout->addWidget(bar_);

    map_ = new MapView(doc_, this);
    layout->addWidget(map_, 1);
    map_->setShowLabels(labels->isChecked());
    map_->setShowArrows(arrows->isChecked());
    map_->setShowLegend(legend->isChecked());
    connect(labels, &QToolButton::toggled, this, [this](bool on) {
        map_->setShowLabels(on);
        QSettings().setValue("map/labels", on);
    });
    connect(arrows, &QToolButton::toggled, this, [this](bool on) {
        map_->setShowArrows(on);
        QSettings().setValue("map/arrows", on);
    });
    connect(legend, &QToolButton::toggled, this, [this](bool on) {
        map_->setShowLegend(on);
        QSettings().setValue("map/legend", on);
    });

    // Floating zoom buttons over the map
    zoomBox_ = new QFrame(map_);
    zoomBox_->setObjectName("card");
    auto* zl = new QVBoxLayout(zoomBox_);
    zl->setContentsMargins(3, 3, 3, 3);
    zl->setSpacing(1);
    auto* zin = barButton("zoom-in", tr("Zoom in"), zoomBox_);
    auto* zout = barButton("zoom-out", tr("Zoom out"), zoomBox_);
    auto* zfit = barButton("zoom-fit", tr("Zoom to the whole network (F)"), zoomBox_);
    zl->addWidget(zin);
    zl->addWidget(zout);
    zl->addWidget(zfit);
    connect(zin, &QToolButton::clicked, this, [this] { map_->zoomBy(1.5); });
    connect(zout, &QToolButton::clicked, this, [this] { map_->zoomBy(1 / 1.5); });
    connect(zfit, &QToolButton::clicked, map_, &MapView::fitToWindow);

    start_ = new StartPage(map_);
    start_->hide();

    timer_ = new QTimer(this);
    timer_->setInterval(400);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!doc_->results())
            return;
        int p = doc_->period() + 1;
        if (p >= doc_->results()->periodCount())
            p = 0;
        doc_->setPeriod(p);
    });
    connect(play_, &QToolButton::toggled, this, [this](bool on) {
        on ? timer_->start() : timer_->stop();
        play_->setIcon(icons::get(on ? "pause" : "play"));
    });
    connect(stepBack_, &QToolButton::clicked, this, [this] {
        if (doc_->results())
            doc_->setPeriod(std::max(0, doc_->period() - 1));
    });
    connect(stepForward_, &QToolButton::clicked, this, [this] {
        if (doc_->results())
            doc_->setPeriod(std::min(doc_->results()->periodCount() - 1, doc_->period() + 1));
    });
    connect(slider_, &QSlider::valueChanged, doc_, &ProjectDocument::setPeriod);
    connect(doc_, &ProjectDocument::periodChanged, this, [this](int p) {
        slider_->blockSignals(true);
        slider_->setValue(p);
        slider_->blockSignals(false);
        updateTimeLabel();
    });
    connect(nodeVar_, &QComboBox::currentTextChanged, map_, &MapView::setNodeVariable);
    connect(linkVar_, &QComboBox::currentTextChanged, map_, &MapView::setLinkVariable);
    connect(areaVar_, &QComboBox::currentTextChanged, map_, &MapView::setAreaVariable);
    connect(doc_, &ProjectDocument::resultsChanged, this, &MapPage::resultsChanged);
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        resultsChanged();
        refreshBasemapMenu();
    });
    resultsChanged();
    refreshBasemapMenu();
}

void MapPage::refreshBasemapMenu()
{
    QMenu* menu = basemap_->menu();
    menu->clear();
    QString why;
    const bool usable = map_->basemapUsable(&why);
    auto* group = new QActionGroup(menu);
    const QString current = map_->basemap();
    auto add = [&](const QString& id, const QString& label) {
        QAction* a = menu->addAction(label);
        a->setCheckable(true);
        a->setChecked(current == id);
        a->setEnabled(id.isEmpty() || usable);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, id] {
            map_->setBasemap(id);
            doc_->settings().basemap = id.toStdString();
            doc_->markSettingsChanged();
        });
    };
    add({}, tr("None"));
    menu->addSeparator();
    for (const auto& [id, label] : MapView::basemapProviders())
        add(id, label);
    menu->addSeparator();
    QAction* crs = menu->addAction(icons::get("globe"), tr("Coordinate system..."));
    connect(crs, &QAction::triggered, this, &MapPage::coordinateSystemRequested);
    if (!usable)
    {
        QAction* note = menu->addAction(why);
        note->setEnabled(false);
    }
    basemap_->setText(current.isEmpty() ? tr("Basemap") : MapView::basemapLabel(current));
}

void MapPage::setStartVisible(bool on)
{
    start_->setVisible(on);
    bar_->setVisible(!on);
    zoomBox_->setVisible(!on);
    if (on)
        start_->raise();
    placeOverlays();
}

void MapPage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    placeOverlays();
}

void MapPage::placeOverlays()
{
    start_->setGeometry(map_->rect());
    zoomBox_->adjustSize();
    zoomBox_->move(map_->width() - zoomBox_->width() - 12, 12);
}

void MapPage::resultsChanged()
{
    const auto* r = doc_->results();
    auto fill = [&](QComboBox* box, const char* table, const QString& preferred) {
        const QString keep = box->currentText();
        box->blockSignals(true);
        box->clear();
        box->addItem(tr("(none)"));
        if (r)
        {
            if (const auto* t = r->table(table))
            {
                for (const auto& v : t->variables)
                    box->addItem(QString::fromStdString(v.name));
            }
        }
        int idx = box->findText(keep);
        if (idx <= 0)
            idx = box->findText(preferred);
        box->setCurrentIndex(idx > 0 ? idx : 0);
        box->setEnabled(r != nullptr);
        box->blockSignals(false);
        emit box->currentTextChanged(box->currentText());
    };
    const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
    fill(nodeVar_, "node", swmm ? "Depth" : "Pressure");
    fill(linkVar_, "link", "Flow");
    fill(areaVar_, "subcatchment", "Runoff");
    areaVar_->setVisible(swmm);
    areaLabel_->setVisible(swmm);
    slider_->setRange(0, r ? std::max(0, r->periodCount() - 1) : 0);
    for (QWidget* w : std::initializer_list<QWidget*>{slider_, play_, stepBack_, stepForward_})
        w->setEnabled(r != nullptr && r->periodCount() > 1);
    if (!r)
        play_->setChecked(false);
    updateTimeLabel();
}

void MapPage::updateTimeLabel()
{
    const auto* r = doc_->results();
    if (!r || r->periodCount() == 0)
    {
        timeLabel_->setText(tr("No results yet"));
        return;
    }
    timeLabel_->setText(tr("%1   %2 of %3")
                            .arg(QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc_->period())])))
                            .arg(doc_->period() + 1)
                            .arg(r->periodCount()));
}

} // namespace mirab::app
