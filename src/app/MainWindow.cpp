/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MainWindow.cpp
 * Purpose: Main application window: menus, tool bars, map, panels and simulation control.
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
#include "MainWindow.h"

#include "Basemap.h"
#include "CalibrationDialogs.h"
#include "DataEditor.h"
#include "Dialogs.h"
#include "Icons.h"
#include "ImportDialog.h"
#include "MapPage.h"
#include "MapServices.h"
#include "MapView.h"
#include "ObjectEditor.h"
#include "OptionsDialog.h"
#include "Panels.h"
#include "PropertyPanel.h"
#include "RasterDialogs.h"
#include "RasterLayers.h"
#include "Reports.h"
#include "SchemaWidgets.h"
#include "SolverProcess.h"
#include "SwmmEditors.h"
#include "Theme.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/Text.h"
#include "mirab/engine/Compatibility.h"
#include "mirab/interop/Interop.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLibrary>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPluginLoader>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

namespace mirab::app
{

using core::Category;
using core::Dialect;

namespace
{
const char* kStateKey = "window/state-v3";
const char* kGeometryKey = "window/geometry-v3";
} // namespace

MainWindow::MainWindow() : doc_(new ProjectDocument(this))
{
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/mirab/mirab.png"));
    setDockNestingEnabled(true);
    setDocumentMode(true);
    mapPage_ = new MapPage(doc_, this);
    map_ = mapPage_->map();
    start_ = mapPage_->startPage();
    inpEditor_ = new InpEditor(doc_, this);
    msxEditor_ = new MsxEditor(this);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(false); // report tabs get their own close buttons
    tabs_->setMovable(true);
    tabs_->addTab(mapPage_, icons::get("basemap"), tr("Map"));
    tabs_->addTab(inpEditor_, icons::get("report"), tr("Input file"));
    for (int i = 0; i < tabs_->count(); ++i)
        tabs_->tabBar()->setTabButton(i, QTabBar::RightSide, nullptr);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* w = tabs_->widget(index);
        if (w == mapPage_ || w == inpEditor_)
            return;
        tabs_->removeTab(index);
        if (w != msxEditor_)
            w->deleteLater();
        else
        {
            msxCheck_->setChecked(false);
        }
    });
    setCentralWidget(tabs_);
    solver_ = new SolverProcess(this);

    createActions();
    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();

    connect(doc_, &ProjectDocument::titleChanged, this, &MainWindow::updateTitle);
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        // A run of the previous project must not deliver results into this one.
        if (solver_->isRunning())
        {
            discardRun_ = true;
            solver_->cancel();
        }
        rebuildForDialect();
        updateCompatibility();
        updateStartPage();
        updateStatusInfo();
        map_->setCoordinateSystem(QString::fromStdString(doc_->settings().crs));
        mapPage_->refreshBasemapMenu();
    });
    connect(doc_, &ProjectDocument::modelChanged, this, [this] {
        updateCompatibility();
        updateStatusInfo();
        updateStartPage();
    });
    connect(doc_, &ProjectDocument::selectionChanged, this,
            [this] { deleteAct_->setEnabled(!doc_->selection().isEmpty()); });
    connect(map_, &MapView::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(map_, &MapView::cursorMoved, this, [this](const QPointF& w) {
        QString text = QString("X %1   Y %2").arg(w.x(), 0, 'f', 2).arg(w.y(), 0, 'f', 2);
        if (const auto z = map_->groundElevationAt(w))
            text += tr("   Ground %1 %2")
                        .arg(*z, 0, 'f', 2)
                        .arg(QString::fromStdString(doc_->model().units().label("elevation")));
        coords_->setText(text);
    });
    connect(map_, &MapView::toolFinished, this, [this] { selectAct_->setChecked(true); });
    connect(map_, &MapView::elementActivated, this, [this] { showDock("propertiesDock"); });
    connect(map_, &MapView::contextMenuForSelection, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(icons::get("properties"), tr("Properties"), this, [this] { showDock("propertiesDock"); });
        menu.addAction(icons::get("chart"), tr("Plot over time"), this, [this] { openReport("timeseries"); });
        menu.addAction(icons::get("zoom-fit"), tr("Zoom to"), this, [this] { map_->zoomTo(doc_->selection()); });
        if (doc_->selection().category == Category::Area)
            menu.addAction(icons::get("layers"), tr("LID usage..."), this,
                           [this] { openDataEditor("LID_USAGE", doc_->selection().id); });
        menu.addAction(icons::get("table"), tr("Copy ID"), this,
                       [this] { QApplication::clipboard()->setText(doc_->selection().id); });
        menu.addSeparator();
        menu.addAction(deleteAct_);
        menu.exec(pos);
    });
    connect(map_, &MapView::contextMenuForMap, this, [this](const QPoint& pos, const QPointF& world) {
        QMenu menu(this);
        QMenu* add = menu.addMenu(icons::get("new"), tr("Add here"));
        const Dialect d = doc_->model().dialect();
        for (const auto& k : core::elementKinds(d, Category::Node))
        {
            const QString kind = QString::fromStdString(k);
            add->addAction(icons::element(kind), kind, this, [this, kind, world] {
                const auto& m = doc_->model();
                static const QHash<QString, QString> prefixes = {{"Junction", "J"}, {"Reservoir", "R"}, {"Tank", "T"},
                                                                 {"Outfall", "O"}, {"Storage", "SU"}, {"Divider", "D"}};
                const QString id = QString::fromStdString(
                    m.uniqueId(Category::Node, prefixes.value(kind, kind.left(1)).toStdString()));
                QString err;
                if (doc_->edit(tr("Add %1 %2").arg(kind.toLower(), id),
                               [&](core::Model& mm, std::string* er) {
                                   return mm.addNode(kind.toStdString(), id.toStdString(), {world.x(), world.y()}, er);
                               },
                               &err))
                    doc_->select({Category::Node, id});
                else
                    statusBar()->showMessage(err, 6000);
            });
        }
        menu.addSeparator();
        menu.addAction(icons::get("zoom-fit"), tr("Zoom to the whole network"), map_, &MapView::fitToWindow);
        menu.addAction(icons::get("table"), tr("Copy coordinates"), this, [world] {
            QApplication::clipboard()->setText(QStringLiteral("%1, %2").arg(world.x(), 0, 'f', 3).arg(world.y(), 0, 'f', 3));
        });
        menu.addAction(icons::get("globe"), tr("Coordinate system..."), this, &MainWindow::chooseCoordinateSystem);
        menu.exec(pos);
    });
    connect(mapPage_, &MapPage::coordinateSystemRequested, this, &MainWindow::chooseCoordinateSystem);
    connect(mapPage_, &MapPage::mapServiceRequested, this, &MainWindow::addMapService);
    connect(mapPage_, &MapPage::rasterLayersRequested, this, &MainWindow::showRasterLayers);
    connect(start_, &StartPage::newEpanet, this, [this] { newProject(Dialect::Epanet); });
    connect(start_, &StartPage::newSwmm, this, [this] { newProject(Dialect::Swmm); });
    connect(start_, &StartPage::openRequested, this, &MainWindow::openDialog);
    connect(start_, &StartPage::openFile, this, [this](const QString& f) {
        if (maybeSave())
            openFile(f);
    });
    connect(browser_, &ProjectBrowser::elementActivated, this, [this] { showDock("propertiesDock"); });
    connect(browser_, &ProjectBrowser::zoomToRequested, map_, &MapView::zoomTo);
    connect(browser_, &ProjectBrowser::deleteRequested, this, &MainWindow::deleteSelection);
    connect(browser_, &ProjectBrowser::dataActivated, this, &MainWindow::openDataEditor);
    connect(browser_, &ProjectBrowser::optionsActivated, this, [this] { showSimulationOptions(); });
    connect(browser_, &ProjectBrowser::objectRenamed, this, [this](const QString& section, const QString& from, const QString& to) {
        // An open editor follows the renamed object.
        for (int i = 0; i < tabs_->count(); ++i)
        {
            auto* editor = qobject_cast<ObjectEditor*>(tabs_->widget(i));
            if (!editor || editor->objectName() != "data:" + section + ":" + from)
                continue;
            editor->setObjectName("data:" + section + ":" + to);
            editor->setName(to);
            tabs_->setTabText(i, to);
        }
    });
    connect(browser_, &ProjectBrowser::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(inpEditor_, &InpEditor::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(msxEditor_, &MsxEditor::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(props_, &PropertyPanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(solver_, &SolverProcess::progress, this, [this](double f, const QString& stage) {
        progress_->setValue(static_cast<int>(f * 100));
        progress_->setFormat(stage + "  %p%");
    });
    connect(solver_, &SolverProcess::log, this, &MainWindow::message);
    connect(solver_, &SolverProcess::finished, this, &MainWindow::simulationFinished);
    connect(solver_, &SolverProcess::liveStarted, this, [this] {
        if (discardRun_)
            return;
        doc_->beginLiveResults(solver_->liveResults());
        // With an element selected, its time series is the most useful view of a run.
        if (!doc_->selection().isEmpty())
            showDock("resultsDock");
    });
    connect(solver_, &SolverProcess::liveUpdated, this, [this](int firstChanged) {
        if (!discardRun_)
            doc_->liveResultsGrew(solver_->liveResults().get(), firstChanged);
    });
    connect(solver_, &SolverProcess::paused, this, [this](double seconds) {
        const QString at = QString::fromStdString(text::formatDuration(seconds));
        runState_->setText(tr("Paused at %1").arg(at));
        runState_->setVisible(true);
        updatePauseAction();
    });
    connect(solver_, &SolverProcess::resumed, this, [this] {
        runState_->setVisible(false);
        updatePauseAction();
    });

    resize(1440, 900);
    doc_->newProject(Dialect::Epanet, "LPS");
    loadPlugins();
    updateRecentMenu();
    updateStartPage();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
void MainWindow::createActions()
{
    toolGroup_ = new QActionGroup(this);
    selectAct_ = new QAction(icons::get("select"), tr("Select"), this);
    selectAct_->setCheckable(true);
    selectAct_->setChecked(true);
    selectAct_->setShortcut(Qt::Key_S);
    selectAct_->setToolTip(tr("Select and move elements (S)"));
    panAct_ = new QAction(icons::get("pan"), tr("Pan"), this);
    panAct_->setCheckable(true);
    panAct_->setShortcut(Qt::Key_H);
    panAct_->setToolTip(tr("Pan the map (H); the middle mouse button also pans"));
    zoomWindowAct_ = new QAction(icons::get("zoom-window"), tr("Zoom to rectangle"), this);
    zoomWindowAct_->setCheckable(true);
    zoomWindowAct_->setShortcut(Qt::Key_Z);
    zoomWindowAct_->setToolTip(tr("Drag a rectangle to zoom into (Z)"));
    toolGroup_->addAction(selectAct_);
    toolGroup_->addAction(panAct_);
    toolGroup_->addAction(zoomWindowAct_);
    connect(selectAct_, &QAction::triggered, this, [this] { map_->setTool(MapView::Tool::Select); });
    connect(panAct_, &QAction::triggered, this, [this] { map_->setTool(MapView::Tool::Pan); });
    connect(zoomWindowAct_, &QAction::triggered, this, [this] { map_->setTool(MapView::Tool::ZoomWindow); });

    runAct_ = new QAction(icons::get("run"), tr("Run"), this);
    runAct_->setShortcut(Qt::Key_F5);
    runAct_->setToolTip(tr("Run the simulation with the selected engine (F5)"));
    connect(runAct_, &QAction::triggered, this, &MainWindow::runSimulation);
    cancelAct_ = new QAction(icons::get("stop"), tr("Stop simulation"), this);
    cancelAct_->setEnabled(false);
    cancelAct_->setToolTip(tr("Stop the simulation; the results computed so far stay on the map"));
    connect(cancelAct_, &QAction::triggered, this, &MainWindow::stopSimulation);
    pauseAct_ = new QAction(icons::get("pause"), tr("Pause simulation"), this);
    pauseAct_->setEnabled(false);
    pauseAct_->setShortcut(Qt::Key_F6);
    connect(pauseAct_, &QAction::triggered, this, &MainWindow::togglePause);
    saveAct_ = new QAction(icons::get("save"), tr("&Save"), this);
    saveAct_->setShortcut(QKeySequence::Save);
    saveAct_->setToolTip(tr("Save the project (Ctrl+S)"));
    connect(saveAct_, &QAction::triggered, this, &MainWindow::save);
    undoAct_ = doc_->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    undoAct_->setIcon(icons::get("undo"));
    redoAct_ = doc_->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAct_->setShortcut(QKeySequence::Redo);
    redoAct_->setIcon(icons::get("redo"));
    deleteAct_ = new QAction(icons::get("delete"), tr("&Delete"), this);
    deleteAct_->setShortcut(QKeySequence::Delete);
    deleteAct_->setEnabled(false);
    connect(deleteAct_, &QAction::triggered, this, &MainWindow::deleteSelection);
}

void MainWindow::createDocks()
{
    auto dock = [this](const QString& title, const QString& name, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        addDockWidget(area, d);
        return d;
    };
    browser_ = new ProjectBrowser(doc_, this);
    props_ = new PropertyPanel(doc_, this);
    results_ = new ResultsPanel(doc_, this);
    messages_ = new QListWidget(this);
    messages_->setWordWrap(true);
    messages_->setIconSize(QSize(16, 16));
    messages_->setSpacing(2);
    browser_->setMinimumWidth(260);
    props_->setMinimumWidth(340);
    auto* dBrowser = dock(tr("Project"), "projectDock", browser_, Qt::LeftDockWidgetArea);
    auto* dProps = dock(tr("Properties"), "propertiesDock", props_, Qt::RightDockWidgetArea);
    auto* dResults = dock(tr("Time series"), "resultsDock", results_, Qt::BottomDockWidgetArea);
    auto* dMessages = dock(tr("Messages"), "messagesDock", messages_, Qt::BottomDockWidgetArea);
    tabifyDockWidget(dMessages, dResults);
    dMessages->raise();
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    (void)dBrowser;
    (void)dProps;
}

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    if (layoutRestored_)
        return;
    layoutRestored_ = true;
    // Default proportions (kept for "Reset layout") once the window has its
    // final size, then the user's last layout when one was saved.
    QTimer::singleShot(0, this, [this] {
        resizeDocks({findChild<QDockWidget*>("projectDock"), findChild<QDockWidget*>("propertiesDock")}, {290, 390},
                    Qt::Horizontal);
        resizeDocks({findChild<QDockWidget*>("messagesDock")}, {220}, Qt::Vertical);
        defaultState_ = saveState();
        QSettings s;
        if (s.contains(kGeometryKey))
            restoreGeometry(s.value(kGeometryKey).toByteArray());
        if (s.contains(kStateKey))
            restoreState(s.value(kStateKey).toByteArray());
    });
}

void MainWindow::resetLayout()
{
    for (auto* d : findChildren<QDockWidget*>())
    {
        d->setFloating(false);
        d->show();
    }
    if (!defaultState_.isEmpty())
        restoreState(defaultState_);
    resizeDocks({findChild<QDockWidget*>("projectDock"), findChild<QDockWidget*>("propertiesDock")}, {290, 380},
                Qt::Horizontal);
    resizeDocks({findChild<QDockWidget*>("messagesDock")}, {210}, Qt::Vertical);
}

void MainWindow::showDock(const QString& name)
{
    if (auto* d = findChild<QDockWidget*>(name))
    {
        d->show();
        d->raise();
    }
}

void MainWindow::createMenus()
{
    // File
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(icons::element("Junction"), tr("New water network (EPANET)..."), QKeySequence::New, this,
                    [this] { newProject(Dialect::Epanet); });
    file->addAction(icons::element("Subcatchment"), tr("New stormwater model (SWMM)..."), this,
                    [this] { newProject(Dialect::Swmm); });
    file->addAction(icons::get("open"), tr("&Open..."), QKeySequence::Open, this, &MainWindow::openDialog);
    recentMenu_ = file->addMenu(tr("Open &recent"));
    QMenu* examples = file->addMenu(tr("Open e&xample"));
    connect(examples, &QMenu::aboutToShow, this, [this, examples] {
        examples->clear();
        const QDir dir(examplesDirectory());
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            for (const QString& f : QDir(dir.filePath(sub)).entryList({"*.inp"}, QDir::Files))
            {
                const QString path = QDir(dir.filePath(sub)).filePath(f);
                examples->addAction(icons::element(sub == "swmm" ? "Subcatchment" : "Junction"), f, this, [this, path] {
                    if (maybeSave())
                        openFile(path);
                });
            }
        }
        if (examples->isEmpty())
            examples->addAction(tr("No examples found"))->setEnabled(false);
    });
    file->addSeparator();
    file->addAction(saveAct_);
    file->addAction(tr("Save &as..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    QMenu* imp = file->addMenu(icons::get("import"), tr("&Import"));
    imp->addAction(icons::get("layers"), tr("Shapefile (SHP)..."), this, [this] { importGisFile("shp"); });
    imp->addAction(icons::get("layers"), tr("CAD drawing (DXF)..."), this, [this] { importGisFile("dxf"); });
    imp->addAction(icons::get("table"), tr("Text table (CSV)..."), this, [this] { importGisFile("csv"); });
    imp->addSeparator();
    imp->addAction(tr("WNTR network (JSON)..."), this, &MainWindow::importWntr);
    QMenu* exp = file->addMenu(icons::get("export"), tr("&Export"));
    exp->addAction(tr("GeoPackage for GIS..."), this, [this] { exportGis(true); });
    exp->addAction(tr("GeoJSON for GIS..."), this, [this] { exportGis(false); });
    exp->addAction(tr("WNTR network (JSON)..."), this, &MainWindow::exportWntr);
    exp->addAction(tr("Map image (PNG)..."), this, &MainWindow::exportMapImage);
    exp->addAction(tr("Selected element time series (CSV)..."), this, [this] {
        const QString csv = results_->csvForSelection();
        if (csv.isEmpty())
        {
            statusBar()->showMessage(tr("Select an element with results first."), 6000);
            return;
        }
        const QString f = QFileDialog::getSaveFileName(this, tr("Export time series"), doc_->selection().id + ".csv", "CSV (*.csv)");
        if (f.isEmpty())
            return;
        QFile out(f);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write(csv.toUtf8());
    });
    file->addAction(icons::get("globe"), tr("Open in &QGIS"), this, &MainWindow::openInQgis);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    // Edit
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(undoAct_);
    edit->addAction(redoAct_);
    edit->addSeparator();
    edit->addAction(deleteAct_);
    edit->addAction(icons::get("find"), tr("&Find element..."), QKeySequence::Find, this, &MainWindow::findElement);
    edit->addSeparator();
    edit->addAction(icons::get("report"), tr("Edit the input file as text"), this, [this] { tabs_->setCurrentWidget(inpEditor_); });

    // View
    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(icons::get("zoom-fit"), tr("Zoom to &fit"), QKeySequence(Qt::Key_F), map_, &MapView::fitToWindow);
    view->addAction(icons::get("zoom-in"), tr("Zoom &in"), QKeySequence::ZoomIn, this, [this] { map_->zoomBy(1.5); });
    view->addAction(icons::get("zoom-out"), tr("Zoom &out"), QKeySequence::ZoomOut, this, [this] { map_->zoomBy(1 / 1.5); });
    view->addAction(zoomWindowAct_);
    view->addSeparator();
    QMenu* basemaps = view->addMenu(icons::get("basemap"), tr("&Basemap"));
    connect(basemaps, &QMenu::aboutToShow, this, [this, basemaps] {
        basemaps->clear();
        QString why;
        const bool usable = map_->basemapUsable(&why);
        auto add = [&](const QString& id, const QString& label) {
            QAction* a = basemaps->addAction(label, this, [this, id] {
                map_->chooseBasemap(id);
                mapPage_->refreshBasemapMenu();
            });
            a->setCheckable(true);
            a->setChecked(map_->basemap() == id);
            a->setEnabled(id.isEmpty() || usable);
        };
        add({}, tr("None"));
        basemaps->addSeparator();
        for (const auto& [id, label] : MapView::basemapProviders())
            add(id, label);
        if (!usable)
            basemaps->addAction(why)->setEnabled(false);
        basemaps->addSeparator();
        basemaps->addAction(tr("Add map service..."), this, &MainWindow::addMapService);
        if (const auto* current = Basemap::find(map_->basemap()); current && current->custom)
        {
            basemaps->addAction(tr("Remove \"%1\"").arg(current->label), this, [this, id = current->id] {
                Basemap::removeService(id);
                map_->chooseBasemap({});
                mapPage_->refreshBasemapMenu();
            });
        }
    });
    view->addAction(icons::get("globe"), tr("&Coordinate system..."), this, &MainWindow::chooseCoordinateSystem);
    view->addAction(icons::get("layers"), tr("&Raster layers..."), this, &MainWindow::showRasterLayers);
    view->addAction(tr("Backdrop &image..."), this, &MainWindow::chooseBackdrop);
    view->addAction(tr("Clear backdrop image"), this, [this] {
        map_->clearBackdrop();
        doc_->settings().backdropImage.clear();
        doc_->markSettingsChanged();
    });
    view->addSeparator();
    panelsMenu_ = view->addMenu(tr("&Panels"));
    for (auto* d : findChildren<QDockWidget*>())
        panelsMenu_->addAction(d->toggleViewAction());
    view->addAction(tr("&Reset layout"), this, &MainWindow::resetLayout);

    // Add (filled per model type)
    addMenu_ = menuBar()->addMenu(tr("&Add"));

    // Project
    QMenu* project = menuBar()->addMenu(tr("&Project"));
    project->addAction(icons::get("options"), tr("Simulation &options..."), this, [this] { showSimulationOptions(); });
    project->addAction(icons::get("engine"), tr("Simulation &engines..."), this, [this] {
        EnginesDialog dlg(registry_, this);
        connect(&dlg, &EnginesDialog::enginesChanged, this, [this] {
            rebuildEngineChoices();
            updateCompatibility();
        });
        dlg.exec();
    });
    project->addAction(icons::get("controls"), tr("Control &rules..."), this, [this] {
        openDataEditor(doc_->model().dialect() == Dialect::Swmm ? "CONTROLS" : "RULES", {});
    });
    project->addAction(tr("Choose &MSX file..."), this, &MainWindow::chooseMsxFile);
    project->addAction(icons::get("globe"), tr("&Coordinate system..."), this, &MainWindow::chooseCoordinateSystem);
    project->addAction(tr("C&heck compatibility"), this, [this] {
        updateCompatibility();
        showDock("messagesDock");
    });
    project->addSeparator();
    QAction* observedAct = project->addAction(icons::get("table"), tr("Observed &data..."), this,
                                              [this] { openTool("observed-data"); });
    observedAct->setObjectName("observedDataAction");
    QAction* calibrateAct = project->addAction(icons::get("calibration"), tr("Ca&librate..."), this,
                                               [this] { openTool("calibration"); });
    calibrateAct->setObjectName("calibrateAction");

    // Simulation
    QMenu* sim = menuBar()->addMenu(tr("&Simulation"));
    sim->addAction(runAct_);
    sim->addAction(pauseAct_);
    sim->addAction(cancelAct_);
    sim->addSeparator();
    sim->addAction(tr("&Load results..."), this, &MainWindow::loadResultsDialog);
    sim->addAction(tr("C&lear results"), doc_, &ProjectDocument::clearResults);

    // Reports (filled per model type)
    QMenu* reports = menuBar()->addMenu(tr("&Reports"));
    connect(reports, &QMenu::aboutToShow, this, [this, reports] {
        reports->clear();
        for (const auto& r : availableReports(doc_->model().dialect()))
        {
            QAction* a = reports->addAction(icons::get(r.icon), r.title, this, [this, kind = r.kind] { openReport(kind); });
            a->setStatusTip(r.description);
        }
        if (doc_->model().dialect() == Dialect::Epanet)
        {
            reports->addSeparator();
            reports->addAction(icons::get("fireflow"), tr("Fire flow analysis..."), this, [this] { showResilience("fireflow"); });
        }
    });

    // Analysis
    QMenu* analysis = menuBar()->addMenu(tr("A&nalysis"));
    analysis->addAction(icons::get("variability"), tr("&Resilience tools..."), this, [this] { showResilience(); });
    analysis->addAction(icons::get("fireflow"), tr("&Fire flow capacity..."), this, [this] { showResilience("fireflow"); });
    analysis->addAction(tr("Pipe &criticality..."), this, [this] { showResilience("criticality"); });
    analysis->addAction(tr("Network &topology..."), this, [this] { showResilience("topology"); });

    // Tools
    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(icons::get("profile"), tr("&Elevations from DEM..."), this, &MainWindow::elevationsFromDem);
    tools->addSeparator();
    pluginMenu_ = tools->addMenu(tr("&Plugins"));
    tools->addAction(icons::get("settings"), tr("&Settings..."), this, [this] { SettingsDialog(this).exec(); });

    // Help
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(icons::get("home"), tr("&Start page"), this, [this] {
        tabs_->setCurrentWidget(mapPage_);
        mapPage_->setStartVisible(true);
    });
    help->addAction(tr("&Documentation"), this, [] {
        QDesktopServices::openUrl(QUrl(QString(MIRAB_HOMEPAGE) + "/tree/main/docs"));
    });
    help->addAction(tr("Open &examples folder"), this,
                    [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(examplesDirectory())); });
    help->addSeparator();
    help->addAction(tr("&About Mirab"), this, [this] { AboutDialog(this).exec(); });
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createToolBar()
{
    mainBar_ = addToolBar(tr("Main"));
    mainBar_->setObjectName("mainToolBar");
    mainBar_->setMovable(false);
    mainBar_->setIconSize(QSize(20, 20));

    auto* newButton = new QToolButton(this);
    newButton->setIcon(icons::get("new"));
    newButton->setToolTip(tr("New project"));
    newButton->setPopupMode(QToolButton::InstantPopup);
    auto* newMenu = new QMenu(newButton);
    newMenu->addAction(icons::element("Junction"), tr("Water network (EPANET)"), this, [this] { newProject(Dialect::Epanet); });
    newMenu->addAction(icons::element("Subcatchment"), tr("Stormwater model (SWMM)"), this, [this] { newProject(Dialect::Swmm); });
    newButton->setMenu(newMenu);
    mainBar_->addWidget(newButton);
    QAction* openAct = mainBar_->addAction(icons::get("open"), tr("Open"), this, &MainWindow::openDialog);
    openAct->setToolTip(tr("Open a project (Ctrl+O)"));
    mainBar_->addAction(saveAct_);
    mainBar_->addSeparator();
    mainBar_->addAction(undoAct_);
    mainBar_->addAction(redoAct_);
    mainBar_->addSeparator();
    mainBar_->addAction(selectAct_);
    mainBar_->addAction(panAct_);
    mainBar_->addAction(zoomWindowAct_);
    mainBar_->addSeparator();
    // Element tools are inserted here by rebuildForDialect().
    QAction* elementsAnchor = mainBar_->addSeparator();
    elementsAnchor->setObjectName("elementsAnchor");
    mainBar_->addAction(deleteAct_);
    mainBar_->addAction(icons::get("find"), tr("Find element (Ctrl+F)"), this, &MainWindow::findElement);
    mainBar_->addSeparator();
    QToolButton* reportsButton = new QToolButton(this);
    reportsButton->setIcon(icons::get("chart"));
    reportsButton->setText(tr("Reports"));
    reportsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    reportsButton->setPopupMode(QToolButton::InstantPopup);
    auto* reportsMenu = new QMenu(reportsButton);
    connect(reportsMenu, &QMenu::aboutToShow, this, [this, reportsMenu] {
        reportsMenu->clear();
        for (const auto& r : availableReports(doc_->model().dialect()))
            reportsMenu->addAction(icons::get(r.icon), r.title, this, [this, kind = r.kind] { openReport(kind); });
        if (doc_->model().dialect() == Dialect::Epanet)
            reportsMenu->addAction(icons::get("fireflow"), tr("Fire flow analysis..."), this, [this] { showResilience("fireflow"); });
    });
    reportsButton->setMenu(reportsMenu);
    mainBar_->addWidget(reportsButton);

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mainBar_->addWidget(spacer);

    auto* engineLabel = new QLabel(tr("Engine"), this);
    engineLabel->setObjectName("muted");
    mainBar_->addWidget(engineLabel);
    engineCombo_ = new QComboBox(this);
    engineCombo_->setMinimumWidth(200);
    engineCombo_->setToolTip(tr("Solver used for the next run"));
    mainBar_->addWidget(engineCombo_);
    msxCheck_ = new QCheckBox(tr("MSX"), this);
    msxCheck_->setToolTip(tr("Run EPANET-MSX (multi-species water quality) on top of the selected EPANET engine"));
    msxAction_ = mainBar_->addWidget(msxCheck_);
    runButton_ = new QToolButton(this);
    runButton_->setObjectName("runButton");
    runButton_->setDefaultAction(runAct_);
    runButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    runButton_->setIconSize(QSize(16, 16));
    mainBar_->addWidget(runButton_);
    mainBar_->addAction(pauseAct_);
    mainBar_->addAction(cancelAct_);

    connect(engineCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        doc_->settings().engineId = engineCombo_->currentData().toString().toStdString();
        doc_->markSettingsChanged();
        updateCompatibility();
    });
    connect(msxCheck_, &QCheckBox::toggled, this, [this](bool on) {
        doc_->settings().useMsx = on;
        doc_->markSettingsChanged();
        if (on && msxPath().isEmpty())
            chooseMsxFile();
        const int idx = tabs_->indexOf(msxEditor_);
        if (on && !msxPath().isEmpty())
        {
            QString err;
            msxEditor_->load(msxPath(), &err);
            if (idx < 0)
                decorateTab(tabs_->addTab(msxEditor_, icons::get("report"), tr("MSX file")));
        }
        else if (!on && idx >= 0)
        {
            tabs_->removeTab(idx);
        }
        updateCompatibility();
    });
}

void MainWindow::createStatusBar()
{
    progress_ = new QProgressBar(this);
    progress_->setMaximumWidth(320);
    progress_->setMinimumWidth(220);
    progress_->setVisible(false);
    unitsLabel_ = new QLabel(this);
    countsLabel_ = new QLabel(this);
    engineLabel_ = new QLabel(this);
    coords_ = new QLabel(this);
    coords_->setMinimumWidth(190);
    runState_ = new QLabel(this);
    runState_->setVisible(false);
    runState_->setStyleSheet(QString("color: %1; font-weight: 600;").arg(theme::kWarning.name()));
    statusBar()->addPermanentWidget(runState_);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(countsLabel_);
    statusBar()->addPermanentWidget(unitsLabel_);
    statusBar()->addPermanentWidget(engineLabel_);
    statusBar()->addPermanentWidget(coords_);
}

void MainWindow::updateStatusInfo()
{
    const auto& m = doc_->model();
    const auto u = m.units();
    unitsLabel_->setText(tr("Flow %1").arg(QString::fromStdString(u.flow)) +
                         (m.dialect() == Dialect::Epanet && !u.pressure.empty() ? tr("  ·  Pressure %1").arg(QString::fromStdString(u.pressure)) : QString()));
    countsLabel_->setText(tr("%1 nodes  ·  %2 links").arg(m.nodes().size()).arg(m.links().size()));
}

void MainWindow::updateStartPage()
{
    const bool empty = doc_->path().isEmpty() && doc_->model().nodes().empty() && doc_->model().links().empty() &&
                       !doc_->isModified();
    QStringList examples;
    const QDir dir(examplesDirectory());
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        for (const QString& f : QDir(dir.filePath(sub)).entryList({"*.inp"}, QDir::Files))
            examples << QDir(dir.filePath(sub)).filePath(f);
    start_->setExamples(examples);
    start_->setRecent(QSettings().value("files/recent").toStringList());
    mapPage_->setStartVisible(empty);
}

QString MainWindow::examplesDirectory() const
{
    const QString base = QCoreApplication::applicationDirPath();
    for (const char* rel : {"../examples", "examples", "../share/mirab/examples", "../Resources/examples", "../../examples",
                            "../../../examples"})
    {
        const QDir d(QDir(base).filePath(rel));
        if (d.exists() && !d.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
            return d.absolutePath();
    }
    return {};
}

void MainWindow::rebuildForDialect()
{
    const Dialect d = doc_->model().dialect();
    for (QAction* a : addActions_)
    {
        toolGroup_->removeAction(a);
        mainBar_->removeAction(a);
        a->deleteLater();
    }
    addActions_.clear();
    addMenu_->clear();
    QAction* anchor = nullptr;
    for (QAction* a : mainBar_->actions())
    {
        if (a->objectName() == "elementsAnchor")
            anchor = a;
    }
    auto addTool = [&](const QString& kind, const QString& label, MapView::Tool tool) {
        auto* a = new QAction(icons::element(kind), label, this);
        a->setCheckable(true);
        a->setToolTip(tr("Add %1").arg(label.toLower()));
        toolGroup_->addAction(a);
        connect(a, &QAction::triggered, this, [this, kind, tool] { map_->setTool(tool, kind); });
        addMenu_->addAction(a);
        mainBar_->insertAction(anchor, a);
        addActions_ << a;
    };
    for (const auto& k : core::elementKinds(d, Category::Node))
        addTool(QString::fromStdString(k), QString::fromStdString(k), MapView::Tool::AddNode);
    addMenu_->addSeparator();
    for (const auto& k : core::elementKinds(d, Category::Link))
        addTool(QString::fromStdString(k), QString::fromStdString(k), MapView::Tool::AddLink);
    if (d == Dialect::Swmm)
    {
        addMenu_->addSeparator();
        addTool("Subcatchment", tr("Subcatchment"), MapView::Tool::AddArea);
        addTool("RainGage", tr("Rain gage"), MapView::Tool::AddGage);
    }
    selectAct_->setChecked(true);
    map_->setTool(MapView::Tool::Select);
    msxAction_->setVisible(d == Dialect::Epanet);
    rebuildEngineChoices();
    if (!doc_->settings().backdropImage.empty() && !doc_->path().isEmpty())
    {
        const QString img = QFileInfo(doc_->path()).dir().filePath(QString::fromStdString(doc_->settings().backdropImage));
        map_->setBackdrop(img);
    }
    else
    {
        map_->clearBackdrop();
    }
    const int msxTab = tabs_->indexOf(msxEditor_);
    if (msxTab >= 0)
        tabs_->removeTab(msxTab);
    if (d == Dialect::Epanet && doc_->settings().useMsx && !msxPath().isEmpty())
    {
        msxEditor_->load(msxPath());
        decorateTab(tabs_->addTab(msxEditor_, icons::get("report"), tr("MSX file")));
    }
    // Reports of the previous project do not apply to the new one.
    for (int i = tabs_->count() - 1; i >= 0; --i)
    {
        QWidget* w = tabs_->widget(i);
        if (w->objectName().startsWith("report:") || w->objectName().startsWith("data:"))
        {
            tabs_->removeTab(i);
            w->deleteLater();
        }
    }
}

void MainWindow::rebuildEngineChoices()
{
    const Dialect d = doc_->model().dialect();
    engineCombo_->blockSignals(true);
    msxCheck_->blockSignals(true);
    engineCombo_->clear();
    std::string wanted = doc_->settings().engineId;
    if (const auto* desc = registry_.descriptor(wanted); desc && desc->multiSpecies)
        wanted = desc->baseEngineId;
    if (wanted.empty() || !registry_.descriptor(wanted) || registry_.descriptor(wanted)->dialect != d)
        wanted = engine::EngineRegistry::defaultEngineId(d);
    int select = 0;
    for (const auto* desc : registry_.forDialect(d, false))
    {
        std::string why;
        const bool ok = registry_.isAvailable(desc->id, &why);
        engineCombo_->addItem(icons::get("engine"),
                              QString::fromStdString(desc->name) + (ok ? QString() : tr(" (not installed)")),
                              QString::fromStdString(desc->id));
        const int idx = engineCombo_->count() - 1;
        engineCombo_->setItemData(idx, QString::fromStdString(ok ? desc->description : why), Qt::ToolTipRole);
        if (!ok)
        {
            if (auto* model = qobject_cast<QStandardItemModel*>(engineCombo_->model()))
                model->item(idx)->setEnabled(false);
        }
        if (desc->id == wanted)
            select = idx;
    }
    engineCombo_->setCurrentIndex(select);
    msxCheck_->setChecked(d == Dialect::Epanet && doc_->settings().useMsx);
    engineCombo_->blockSignals(false);
    msxCheck_->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Engines and compatibility
// ---------------------------------------------------------------------------
QString MainWindow::currentEngineId() const
{
    QString id = engineCombo_->currentData().toString();
    if (msxAction_->isVisible() && msxCheck_->isChecked())
    {
        if (const auto* v = registry_.msxVariantOf(id.toStdString()))
            id = QString::fromStdString(v->id);
    }
    return id;
}

const engine::EngineDescriptor* MainWindow::currentEngine() const
{
    return registry_.descriptor(currentEngineId().toStdString());
}

QString MainWindow::msxPath() const
{
    const std::string rel = doc_->settings().msxFile;
    if (rel.empty())
        return {};
    const QFileInfo fi(QString::fromStdString(rel));
    if (fi.isAbsolute() || doc_->path().isEmpty())
        return fi.absoluteFilePath();
    return QFileInfo(doc_->path()).dir().filePath(QString::fromStdString(rel));
}

QString MainWindow::runBase() const
{
    return doc_->path().isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("untitled")
                                  : QFileInfo(doc_->path()).dir().filePath(QFileInfo(doc_->path()).completeBaseName());
}

void MainWindow::updateCompatibility()
{
    const auto* desc = currentEngine();
    props_->setEngine(desc);
    browser_->setEngine(desc);
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (auto* editor = qobject_cast<ObjectEditor*>(tabs_->widget(i)))
            editor->setEngine(desc);
    }
    // Replace the previous compatibility notes; keep the run log.
    for (int i = messages_->count() - 1; i >= 0; --i)
    {
        if (messages_->item(i)->data(Qt::UserRole).toString() == "compat")
            delete messages_->takeItem(i);
    }
    compatibilityPass_ = true;
    if (!desc)
    {
        compatibilityPass_ = false;
        runAct_->setEnabled(false);
        return;
    }
    std::string why;
    const bool available = registry_.isAvailable(desc->id, &why);
    engineLabel_->setText(QString::fromStdString(desc->name));
    const auto report = engine::checkCompatibility(doc_->model(), *desc);
    bool ok = report.runnable() && available && !solver_->isRunning();
    if (!available)
        message("error", tr("%1 is not installed: %2").arg(QString::fromStdString(desc->name), QString::fromStdString(why)));
    if (desc->multiSpecies && msxPath().isEmpty())
    {
        message("warning", tr("Choose an EPANET-MSX file (Project > Choose MSX file) before running."));
        ok = false;
    }
    for (const auto& i : report.issues)
        message(i.level == engine::Support::Unsupported ? "error" : "warning", QString::fromStdString(i.message));
    for (const auto& i : doc_->model().validate())
    {
        if (i.severity == core::ModelIssue::Severity::Error)
            message("error", QString::fromStdString(i.message));
    }
    if (report.issues.empty() && available)
        message("info", tr("The project is compatible with %1.").arg(QString::fromStdString(desc->name)));
    if (doc_->model().nodes().empty())
        ok = false;
    compatibilityPass_ = false;
    runAct_->setEnabled(ok);
}

void MainWindow::message(const QString& level, const QString& text)
{
    auto* item = new QListWidgetItem(text, messages_);
    if (compatibilityPass_)
        item->setData(Qt::UserRole, "compat");
    item->setIcon(icons::get(level == "error" ? "error" : level == "warning" ? "warning" : "info"));
    if (level == "error")
    {
        item->setForeground(QColor(170, 40, 40));
        showDock("messagesDock");
    }
    messages_->scrollToBottom();
}

void MainWindow::log(const QString& m)
{
    message("info", m);
}

bool MainWindow::editModel(const QString& description, const std::function<bool(core::Model&, std::string*)>& fn)
{
    return doc_->edit(description, fn);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
void MainWindow::updateTitle()
{
    setWindowTitle(QString("%1%2 - Mirab").arg(doc_->displayName(), doc_->isModified() ? "*" : ""));
    setWindowModified(doc_->isModified());
}

bool MainWindow::maybeSave()
{
    if (inpEditor_->hasPendingEdits())
        inpEditor_->apply();
    if (msxEditor_->isModified())
        msxEditor_->save();
    if (!doc_->isModified())
        return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("Save the changes to %1?").arg(doc_->displayName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return save();
    return true;
}

bool MainWindow::save()
{
    if (doc_->path().isEmpty())
        return saveAs();
    if (inpEditor_->hasPendingEdits() && !inpEditor_->apply())
        return false;
    try
    {
        doc_->save(doc_->path());
        if (msxEditor_->isModified())
            msxEditor_->save();
        statusBar()->showMessage(tr("Saved %1").arg(doc_->path()), 5000);
        return true;
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(e.what()));
        return false;
    }
}

bool MainWindow::saveAs()
{
    const QString start = doc_->path().isEmpty() ? QDir::homePath() + "/untitled.inp" : doc_->path();
    const QString f = QFileDialog::getSaveFileName(this, tr("Save project"), start, tr("Input files (*.inp)"));
    if (f.isEmpty())
        return false;
    const QString oldMsx = msxPath();
    try
    {
        doc_->save(f);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(e.what()));
        return false;
    }
    if (!oldMsx.isEmpty())
    {
        doc_->settings().msxFile = QFileInfo(f).dir().relativeFilePath(oldMsx).toStdString();
        doc_->save(f);
    }
    addRecent(f);
    return true;
}

void MainWindow::newProject(Dialect dialect)
{
    if (!maybeSave())
        return;
    NewProjectDialog dlg(dialect, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    doc_->newProject(dialect, dlg.flowUnits());
    tabs_->setCurrentWidget(mapPage_);
    mapPage_->setStartVisible(false);
    statusBar()->showMessage(tr("New project. Pick an element on the tool bar and click on the map to place it."), 10000);
}

void MainWindow::openDialog()
{
    if (!maybeSave())
        return;
    const QString start = QSettings().value("files/lastDir", QDir::homePath()).toString();
    const QString f = QFileDialog::getOpenFileName(this, tr("Open project"), start,
                                                   tr("EPANET or SWMM input files (*.inp);;All files (*)"));
    if (!f.isEmpty())
        openFile(f);
}

void MainWindow::openExample(const QString& relative)
{
    const QString dir = examplesDirectory();
    if (!dir.isEmpty() && maybeSave())
        openFile(QDir(dir).filePath(relative));
}

bool MainWindow::openFile(const QString& file)
{
    // Absolute paths: the solver runs in another working folder.
    const QString path = QFileInfo(file).absoluteFilePath();
    try
    {
        doc_->open(path);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Open failed"), QString::fromUtf8(e.what()));
        return false;
    }
    QSettings().setValue("files/lastDir", QFileInfo(path).absolutePath());
    addRecent(path);
    tabs_->setCurrentWidget(mapPage_);
    mapPage_->setStartVisible(false);
    // Reload results from a previous run of this project, when present.
    const QString anr = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + ".anr");
    if (QFileInfo::exists(anr) && QFileInfo(anr).lastModified() >= QFileInfo(path).lastModified())
        loadResults(anr);
    statusBar()->showMessage(tr("Opened %1 (%2 nodes, %3 links)")
                                 .arg(QFileInfo(path).fileName())
                                 .arg(doc_->model().nodes().size())
                                 .arg(doc_->model().links().size()),
                             6000);
    return true;
}

void MainWindow::addRecent(const QString& path)
{
    QSettings s;
    QStringList list = s.value("files/recent").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 10)
        list.removeLast();
    s.setValue("files/recent", list);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    recentMenu_->clear();
    const QStringList list = QSettings().value("files/recent").toStringList();
    for (const QString& f : list)
    {
        recentMenu_->addAction(QFileInfo(f).fileName(), this, [this, f] {
            if (maybeSave())
                openFile(f);
        })->setToolTip(f);
    }
    recentMenu_->setEnabled(!list.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave())
    {
        e->ignore();
        return;
    }
    if (solver_->isRunning())
        solver_->cancel();
    QSettings s;
    s.setValue(kGeometryKey, saveGeometry());
    s.setValue(kStateKey, saveState());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls())
    {
        const QString f = url.toLocalFile();
        if (f.endsWith(".inp", Qt::CaseInsensitive) && maybeSave())
        {
            openFile(f);
            return;
        }
        if (f.endsWith(".anr", Qt::CaseInsensitive) || f.endsWith(".out", Qt::CaseInsensitive))
        {
            loadResults(f);
            return;
        }
        for (const char* ext : {"shp", "dxf", "csv"})
        {
            if (f.endsWith(QString(".") + ext, Qt::CaseInsensitive))
            {
                importFile(f);
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------
QString MainWindow::writeRunInput(QString* error)
{
    if (inpEditor_->hasPendingEdits() && !inpEditor_->apply())
    {
        if (error)
            *error = tr("The input text has errors.");
        return {};
    }
    // Run from a copy next to the project so relative file references keep working.
    QString dir = doc_->path().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                         : QFileInfo(doc_->path()).absolutePath();
    const QString base = doc_->path().isEmpty() ? QString("untitled") : QFileInfo(doc_->path()).completeBaseName();
    const QString file = QDir(dir).filePath("." + base + ".mirab-run.inp");
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = tr("Cannot write %1").arg(file);
        return {};
    }
    f.write(QByteArray::fromStdString(doc_->model().toText()));
    return file;
}

void MainWindow::runSimulation()
{
    if (solver_->isRunning())
        return;
    const auto* desc = currentEngine();
    if (!desc)
        return;
    if (msxEditor_->isModified())
        msxEditor_->save();
    QString err;
    runInput_ = writeRunInput(&err);
    if (runInput_.isEmpty())
    {
        QMessageBox::warning(this, tr("Cannot run"), err);
        return;
    }
    const QString base = runBase();
    QStringList args = {"run", runInput_, "--engine", QString::fromStdString(desc->id), "--out", base + ".anr",
                        "--report", base + ".rpt", "--live"};
    if (desc->multiSpecies)
        args << "--msx" << msxPath();
    messages_->clear();
    message("info", tr("Running %1...").arg(QString::fromStdString(desc->name)));
    progress_->setValue(0);
    progress_->setVisible(true);
    runAct_->setEnabled(false);
    cancelAct_->setEnabled(true);
    discardRun_ = false;
    solver_->start(args);
    updatePauseAction();
}

void MainWindow::togglePause()
{
    if (!solver_->isRunning())
        return;
    if (solver_->isPaused() || pauseAct_->data().toBool())
    {
        pauseAct_->setData(false);
        solver_->resume();
        runState_->setText(tr("Resuming..."));
    }
    else
    {
        pauseAct_->setData(true);
        solver_->pause();
        runState_->setText(tr("Pausing..."));
    }
    runState_->setVisible(true);
    updatePauseAction();
}

void MainWindow::stopSimulation()
{
    if (solver_->isRunning())
        solver_->cancel();
}

void MainWindow::updatePauseAction()
{
    const bool running = solver_->isRunning();
    const bool paused = running && (solver_->isPaused() || pauseAct_->data().toBool());
    pauseAct_->setEnabled(running);
    pauseAct_->setIcon(icons::get(paused ? "play" : "pause"));
    pauseAct_->setText(paused ? tr("Resume simulation") : tr("Pause simulation"));
    pauseAct_->setToolTip(paused ? tr("Continue the simulation (F6)")
                                 : tr("Pause the simulation between two time steps (F6)"));
}

void MainWindow::simulationFinished(bool ok, const QJsonObject& r)
{
    progress_->setVisible(false);
    cancelAct_->setEnabled(false);
    pauseAct_->setData(false);
    runState_->setVisible(false);
    updatePauseAction();
    if (!runInput_.isEmpty())
    {
        QFile::remove(runInput_);
        runInput_.clear();
    }
    if (discardRun_)
    {
        discardRun_ = false;
        updateCompatibility();
        emit simulationCompleted(false);
        return;
    }
    const QString reached = QString::fromStdString(text::formatDuration(solver_->liveTime()));
    if (ok)
    {
        message("info", tr("Simulation finished in %1 s with %2 reporting periods.")
                            .arg(r.value("seconds").toDouble(), 0, 'f', 2)
                            .arg(r.value("periods").toInt()));
        // The saved results replace the live ones.
        if (!loadResults(r.value("results").toString()))
            doc_->endLiveResults(tr("Partial results (the saved results could not be read)"));
        showDock("resultsDock");
        statusBar()->showMessage(tr("Simulation finished. Color the map with the bar above it, and open Reports for "
                                    "tables and charts."),
                                 10000);
    }
    else if (r.value("cancelled").toBool())
    {
        if (doc_->isLive())
        {
            doc_->endLiveResults(tr("Partial results (cancelled at %1)").arg(reached));
            message("warning", tr("Simulation cancelled at %1. The map shows the partial results; they are "
                                  "not saved.")
                                   .arg(reached));
        }
        else
        {
            message("warning", tr("Simulation cancelled."));
        }
    }
    else
    {
        if (doc_->isLive())
            doc_->endLiveResults(tr("Partial results (the run failed at %1)").arg(reached));
        message("error", tr("Simulation failed: %1").arg(r.value("message").toString()));
    }
    updateCompatibility();
    emit simulationCompleted(ok);
}

void MainWindow::selectElement(const QString& id)
{
    const auto& m = doc_->model();
    const std::string sid = id.toStdString();
    if (m.findNode(sid))
        doc_->select({Category::Node, id});
    else if (m.findLink(sid))
        doc_->select({Category::Link, id});
    else if (m.findArea(sid))
        doc_->select({Category::Area, id});
}

bool MainWindow::loadResults(const QString& path)
{
    try
    {
        auto r = std::make_unique<core::ResultSet>(core::ResultSet::loadAny(std::filesystem::u8path(path.toStdString())));
        doc_->setResults(std::move(r));
        statusBar()->showMessage(tr("Loaded results %1").arg(QFileInfo(path).fileName()), 5000);
        return true;
    }
    catch (const std::exception& e)
    {
        message("error", tr("Results could not be read: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
}

void MainWindow::loadResultsDialog()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Load results"), QFileInfo(doc_->path()).absolutePath(),
                                                   tr("Results (*.anr *.out);;All files (*)"));
    if (!f.isEmpty())
        loadResults(f);
}

void MainWindow::openReport(const QString& kind)
{
    if (kind == "fireflow")
    {
        showResilience("fireflow");
        return;
    }
    const QString name = "report:" + kind;
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (tabs_->widget(i)->objectName() == name)
        {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
    QWidget* w = createReport(kind, doc_, runBase() + ".rpt", tabs_);
    if (!w)
        return;
    w->setObjectName(name);
    QString title = kind, icon = "chart";
    for (const auto& r : availableReports(doc_->model().dialect()))
    {
        if (r.kind == kind)
        {
            title = r.title;
            icon = r.icon;
        }
    }
    const int index = tabs_->addTab(w, icons::get(icon), title);
    decorateTab(index);
    tabs_->setCurrentIndex(index);
}

void MainWindow::openCalibrationTool(const QString& name)
{
    if (name == "observed-data")
    {
        ObservedDataDialog dlg(doc_, this);
        dlg.exec();
        return;
    }
    if (name != "calibration" && name != "calibration-run")
        return;
    const auto* desc = currentEngine();
    if (!desc)
        return;
    if (inpEditor_->hasPendingEdits() && !inpEditor_->apply())
    {
        QMessageBox::warning(this, tr("Calibrate"), tr("The input text has errors."));
        return;
    }
    if (msxEditor_->isModified())
        msxEditor_->save();
    CalibrationDialog dlg(doc_, currentEngineId(), desc->multiSpecies ? msxPath() : QString(), this);
    if (name == "calibration-run")
        QTimer::singleShot(300, &dlg, &CalibrationDialog::start);
    dlg.exec();
}

void MainWindow::openDataEditor(const QString& section, const QString& id)
{
    // LID controls, transects, unit hydrographs, control rules and LID usage.
    if (openSpecialEditor(section, id))
        return;
    const QString name = "data:" + section + ":" + id;
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (tabs_->widget(i)->objectName() == name)
        {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
    QWidget* editor = nullptr;
    QIcon icon;
    const auto* def = core::findSectionDef(doc_->model().dialect(), section.toStdString());
    if (section == "PATTERNS" || section == "CURVES" || section == "TIMESERIES")
    {
        editor = new DataEditor(doc_, section, id, tabs_);
        icon = icons::get(section == "PATTERNS" ? "pattern" : section == "TIMESERIES" ? "chart" : "curve");
    }
    // Named objects without a specialised editor use the schema-driven object editor.
    else if (def && !def->objectKind.empty())
    {
        auto* objectEditor = new ObjectEditor(doc_, QString::fromStdString(def->objectKind), id, tabs_);
        objectEditor->setEngine(currentEngine());
        connect(objectEditor, &ObjectEditor::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
        editor = objectEditor;
        icon = objectKindIcon(QString::fromStdString(def->objectKind));
    }
    if (!editor)
        return;
    editor->setObjectName(name);
    const int index = tabs_->addTab(editor, icon, id);
    decorateTab(index);
    tabs_->setCurrentIndex(index);
    browser_->revealData(section, id);
}

void MainWindow::showSimulationOptions(const QString& page)
{
    if (doc_->model().dialect() == Dialect::Msx)
        return;
    OptionsDialog dlg(doc_, this);
    if (!page.isEmpty())
        dlg.showPage(page);
    if (dlg.exec() == QDialog::Accepted && dlg.changedCount() > 0)
        statusBar()->showMessage(tr("Simulation options updated (%n value(s) changed).", nullptr, dlg.changedCount()), 6000);
}

bool MainWindow::openSpecialEditor(const QString& section, const QString& id)
{
    const Dialect dialect = doc_->model().dialect();
    if (!hasSpecialEditor(dialect, section))
        return false;
    if (section == "LID_USAGE")
    {
        // Shown from the event loop, so callers (menus, automation) return first.
        QTimer::singleShot(0, this, [this, id] { LidUsageDialog::edit(doc_, id, this); });
        return true;
    }
    const QString name = "object:" + section + ":" + id;
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (tabs_->widget(i)->objectName() == name)
        {
            tabs_->setCurrentIndex(i);
            return true;
        }
    }
    SpecialEditor* editor = createSpecialEditor(doc_, section, id, tabs_);
    if (!editor)
        return false;
    editor->setObjectName(name);
    const int index =
        tabs_->addTab(editor, icons::get(objectEditorIcon(section)), objectEditorTitle(dialect, section, id));
    decorateTab(index);
    tabs_->setCurrentIndex(index);
    connect(editor, &SpecialEditor::renamed, this, [this, editor](const QString& sec, const QString& newId) {
        editor->setObjectName("object:" + sec + ":" + newId);
        const int i = tabs_->indexOf(editor);
        if (i >= 0)
            tabs_->setTabText(i, objectEditorTitle(doc_->model().dialect(), sec, newId));
    });
    return true;
}

void MainWindow::decorateTab(int index)
{
    auto* close = new QToolButton(tabs_);
    close->setIcon(icons::get("close"));
    close->setIconSize(QSize(12, 12));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close"));
    close->setStyleSheet("QToolButton { padding: 1px; border-radius: 3px; } QToolButton:hover { background: #e3e9f2; }");
    QWidget* page = tabs_->widget(index);
    connect(close, &QToolButton::clicked, this, [this, page] {
        const int i = tabs_->indexOf(page);
        if (i >= 0)
            emit tabs_->tabCloseRequested(i);
    });
    tabs_->tabBar()->setTabButton(index, QTabBar::RightSide, close);
}

void MainWindow::importFile(const QString& path)
{
    ImportDialog dlg(doc_, path, this);
    if (dlg.exec() == QDialog::Accepted)
    {
        message("info", dlg.summary());
        for (const QString& n : dlg.notes())
            message("warning", n);
        tabs_->setCurrentWidget(mapPage_);
        mapPage_->setStartVisible(false);
        map_->fitToWindow();
    }
}

void MainWindow::chooseMsxFile()
{
    const QString start = doc_->path().isEmpty() ? QDir::homePath() : QFileInfo(doc_->path()).absolutePath();
    const QString f = QFileDialog::getOpenFileName(this, tr("EPANET-MSX file"), start, tr("MSX files (*.msx);;All files (*)"));
    if (f.isEmpty())
    {
        if (doc_->settings().msxFile.empty())
            msxCheck_->setChecked(false);
        return;
    }
    doc_->settings().msxFile = doc_->path().isEmpty() ? f.toStdString()
                                                      : QFileInfo(doc_->path()).dir().relativeFilePath(f).toStdString();
    doc_->settings().useMsx = true;
    doc_->markSettingsChanged();
    msxCheck_->blockSignals(true);
    msxCheck_->setChecked(true);
    msxCheck_->blockSignals(false);
    msxEditor_->load(f);
    if (tabs_->indexOf(msxEditor_) < 0)
        decorateTab(tabs_->addTab(msxEditor_, icons::get("report"), tr("MSX file")));
    updateCompatibility();
}

void MainWindow::chooseCoordinateSystem()
{
    CoordinateSystemDialog dlg(QString::fromStdString(doc_->settings().crs), doc_->model(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    doc_->settings().crs = dlg.crs().toStdString();
    doc_->markSettingsChanged();
    map_->setCoordinateSystem(dlg.crs());
    mapPage_->refreshBasemapMenu();
    QString why;
    if (map_->basemapUsable(&why))
    {
        if (map_->basemap().isEmpty())
        {
            map_->setBasemap("osm");
            doc_->settings().basemap = "osm";
            mapPage_->refreshBasemapMenu();
        }
        statusBar()->showMessage(tr("Coordinate system set to %1.").arg(map_->coordinateSystemName()), 8000);
    }
    else if (!dlg.crs().isEmpty())
    {
        message("warning", why);
    }
}

// ---------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------
void MainWindow::deleteSelection()
{
    const Selection sel = doc_->selection();
    if (sel.isEmpty())
        return;
    const std::string id = sel.id.toStdString();
    doc_->edit(tr("Delete %1").arg(sel.id), [&](core::Model& m, std::string*) { return m.removeElement(sel.category, id); });
}

void MainWindow::findElement()
{
    bool ok = false;
    const QString id = QInputDialog::getText(this, tr("Find element"), tr("Element ID"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || id.isEmpty())
        return;
    const auto& m = doc_->model();
    const std::string sid = id.toStdString();
    Selection s;
    if (m.findNode(sid))
        s = {Category::Node, id};
    else if (m.findLink(sid))
        s = {Category::Link, id};
    else if (m.findArea(sid))
        s = {Category::Area, id};
    else if (m.findGage(sid))
        s = {Category::Gage, id};
    if (s.isEmpty())
    {
        statusBar()->showMessage(tr("No element named %1.").arg(id), 5000);
        return;
    }
    doc_->select(s);
    tabs_->setCurrentWidget(mapPage_);
    map_->zoomTo(s);
}

void MainWindow::chooseBackdrop()
{
    const QString start = doc_->path().isEmpty() ? QDir::homePath() : QFileInfo(doc_->path()).absolutePath();
    const QString f = QFileDialog::getOpenFileName(this, tr("Backdrop image"), start,
                                                   tr("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp)"));
    if (f.isEmpty())
        return;
    QString err;
    const QString suffix = QFileInfo(f).suffix().toLower();
    if (suffix == "tif" || suffix == "tiff")
    {
        // GeoTIFF files carry their own georeferencing: show them as raster layers.
        auto r = openRaster(f, &err);
        if (r && r->info().georeferenced)
        {
            if (RasterLayersDialog::addLayer(doc_, this, f))
                map_->reloadRasters();
            return;
        }
    }
    if (!map_->setBackdrop(f, &err))
    {
        QMessageBox::warning(this, tr("Backdrop"), err);
        return;
    }
    doc_->settings().backdropImage = doc_->path().isEmpty() ? f.toStdString()
                                                            : QFileInfo(doc_->path()).dir().relativeFilePath(f).toStdString();
    doc_->markSettingsChanged();
}

void MainWindow::addMapService()
{
    MapServiceDialog dlg(QString::fromStdString(doc_->settings().crs), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Basemap::Provider p = dlg.provider();
    Basemap::addService(p);
    QString why;
    if (!map_->basemapUsable(&why))
    {
        statusBar()->showMessage(why, 8000);
        return;
    }
    map_->chooseBasemap(p.id);
    mapPage_->refreshBasemapMenu();
}

void MainWindow::showRasterLayers()
{
    RasterLayersDialog dlg(doc_, this);
    connect(&dlg, &RasterLayersDialog::layersChanged, map_, &MapView::reloadRasters);
    dlg.exec();
}

void MainWindow::elevationsFromDem()
{
    ElevationDialog dlg(doc_, this);
    connect(&dlg, &ElevationDialog::layersChanged, map_, &MapView::reloadRasters);
    if (dlg.exec() == QDialog::Accepted)
        statusBar()->showMessage(tr("Elevations updated from the DEM. Undo restores the previous values."), 8000);
}

void MainWindow::openTool(const QString& name)
{
    if (name == "elevations")
        elevationsFromDem();
    else if (name == "raster-layers")
        showRasterLayers();
    else if (name == "map-service")
        addMapService();
    else
        openCalibrationTool(name);
}

// ---------------------------------------------------------------------------
// Interchange and QGIS
// ---------------------------------------------------------------------------
void MainWindow::importGisFile(const QString& format)
{
    const QString filter = format == "shp" ? tr("Shapefiles (*.shp)") : format == "dxf" ? tr("DXF drawings (*.dxf)") : tr("CSV files (*.csv *.txt)");
    const QString start = QSettings().value("files/importDir", QDir::homePath()).toString();
    const QString f = QFileDialog::getOpenFileName(this, tr("Import"), start, filter + ";;" + tr("All files (*)"));
    if (f.isEmpty())
        return;
    QSettings().setValue("files/importDir", QFileInfo(f).absolutePath());
    importFile(f);
}

void MainWindow::exportGis(bool geopackage)
{
    if (geopackage && !interop::geoPackageAvailable())
    {
        QMessageBox::warning(this, tr("Export"), tr("This build has no GeoPackage support."));
        return;
    }
    const QString base = doc_->path().isEmpty() ? QDir::homePath() + "/network" : QFileInfo(doc_->path()).absolutePath() + "/" + QFileInfo(doc_->path()).completeBaseName();
    const QString f = QFileDialog::getSaveFileName(this, tr("Export for GIS"), base + (geopackage ? ".gpkg" : ".geojson"),
                                                   geopackage ? tr("GeoPackage (*.gpkg)") : tr("GeoJSON (*.geojson *.json)"));
    if (f.isEmpty())
        return;
    if (doc_->settings().crs.empty())
        chooseCoordinateSystem();
    interop::GisOptions g;
    g.crs = doc_->settings().crs;
    g.results = doc_->results();
    g.period = doc_->period();
    try
    {
        const auto path = std::filesystem::u8path(f.toStdString());
        geopackage ? interop::writeGeoPackage(doc_->model(), path, g) : interop::writeGeoJson(doc_->model(), path, g);
        statusBar()->showMessage(tr("Exported %1").arg(f), 6000);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Export failed"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::exportWntr()
{
    if (doc_->model().dialect() != Dialect::Epanet)
    {
        QMessageBox::information(this, tr("Export"), tr("WNTR exchange applies to EPANET models."));
        return;
    }
    const QString f = QFileDialog::getSaveFileName(this, tr("Export WNTR network"),
                                                   QFileInfo(doc_->path()).completeBaseName() + ".wntr.json", tr("JSON (*.json)"));
    if (f.isEmpty())
        return;
    try
    {
        std::vector<std::string> notes;
        interop::writeWntrJson(doc_->model(), std::filesystem::u8path(f.toStdString()), &notes);
        for (const auto& n : notes)
            message("warning", QString::fromStdString(n));
        statusBar()->showMessage(tr("Exported %1").arg(f), 6000);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Export failed"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::importWntr()
{
    if (!maybeSave())
        return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Import WNTR network"), QDir::homePath(), tr("JSON (*.json)"));
    if (f.isEmpty())
        return;
    try
    {
        std::vector<std::string> notes;
        const auto model = interop::readWntrJson(std::filesystem::u8path(f.toStdString()), &notes);
        const QString target = QFileDialog::getSaveFileName(this, tr("Save the imported network as"),
                                                            QFileInfo(f).absolutePath() + "/" + QFileInfo(f).baseName() + ".inp",
                                                            tr("Input files (*.inp)"));
        if (target.isEmpty())
            return;
        model.save(std::filesystem::u8path(target.toStdString()));
        openFile(target);
        for (const auto& n : notes)
            message("warning", QString::fromStdString(n));
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Import failed"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::openInQgis()
{
    if (doc_->path().isEmpty() && !saveAs())
        return;
    const QString gpkg = QFileInfo(doc_->path()).dir().filePath(QFileInfo(doc_->path()).completeBaseName() + ".gpkg");
    interop::GisOptions g;
    g.crs = doc_->settings().crs;
    g.results = doc_->results();
    g.period = doc_->period();
    try
    {
        interop::writeGeoPackage(doc_->model(), std::filesystem::u8path(gpkg.toStdString()), g);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, tr("Export failed"), QString::fromUtf8(e.what()));
        return;
    }
    const QString qgis = SettingsDialog::findQgis();
    bool started = false;
    if (!qgis.isEmpty())
    {
#if defined(Q_OS_MACOS)
        started = QProcess::startDetached("open", {"-a", qgis, gpkg});
#else
        started = QProcess::startDetached(qgis, {gpkg});
#endif
    }
    if (!started)
        started = QDesktopServices::openUrl(QUrl::fromLocalFile(gpkg));
    message(started ? "info" : "warning",
            started ? tr("Opened %1 in QGIS.").arg(QFileInfo(gpkg).fileName())
                    : tr("QGIS was not found. The GeoPackage was written to %1; set the QGIS program in Tools > Settings.").arg(gpkg));
}

void MainWindow::exportMapImage()
{
    const QString f = QFileDialog::getSaveFileName(this, tr("Export map image"),
                                                   QFileInfo(doc_->path()).completeBaseName() + ".png", tr("PNG image (*.png)"));
    if (f.isEmpty())
        return;
    if (map_->renderImage().save(f))
        statusBar()->showMessage(tr("Saved %1").arg(f), 5000);
}

void MainWindow::showResilience(const QString& page)
{
    if (doc_->model().dialect() != Dialect::Epanet && page != "topology")
    {
        QMessageBox::information(this, tr("Resilience"),
                                 tr("The resilience tools apply to water distribution (EPANET) models. "
                                    "Network topology is available for both model types from this dialog."));
    }
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString input = QDir(tmpDir).filePath("mirab-resilience-" + QString::number(QCoreApplication::applicationPid()) + ".inp");
    QFile f(input);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::fromStdString(doc_->model().toText()));
    f.close();
    QString engineId = engineCombo_->currentData().toString();
    ResilienceDialog dlg(doc_, engineId, input, this);
    if (!page.isEmpty())
        dlg.selectAnalysis(page);
    dlg.exec();
    QFile::remove(input);
}

// ---------------------------------------------------------------------------
// Plugins
// ---------------------------------------------------------------------------
void MainWindow::loadPlugins()
{
    const QString app = QCoreApplication::applicationDirPath();
    QStringList dirs = {QDir(app).filePath("plugins"), QDir(app).filePath("../lib/mirab/plugins"),
                        QDir(app).filePath("../PlugIns/mirab"),
                        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("plugins")};
    for (const QString& d : dirs)
    {
        QDir dir(d);
        if (!dir.exists())
            continue;
        for (const QString& file : dir.entryList(QDir::Files))
        {
            if (!QLibrary::isLibrary(file))
                continue;
            QPluginLoader loader(dir.absoluteFilePath(file));
            QObject* obj = loader.instance();
            auto* plugin = qobject_cast<MirabPlugin*>(obj);
            if (!plugin)
            {
                if (!loader.errorString().isEmpty() && obj == nullptr)
                    message("warning", tr("Plugin %1 was not loaded: %2").arg(file, loader.errorString()));
                continue;
            }
            plugins_ << plugin;
            QMenu* sub = pluginMenu_->addMenu(plugin->name());
            sub->setToolTip(plugin->description());
            for (QAction* a : plugin->actions(this, this))
                sub->addAction(a);
            message("info", tr("Loaded plugin %1 %2").arg(plugin->name(), plugin->version()));
        }
    }
    if (plugins_.isEmpty())
    {
        QAction* none = pluginMenu_->addAction(tr("No plugins installed"));
        none->setEnabled(false);
    }
    pluginMenu_->addSeparator();
    pluginMenu_->addAction(tr("Open plugin folder"), this, [] {
        const QString d = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("plugins");
        QDir().mkpath(d);
        QDesktopServices::openUrl(QUrl::fromLocalFile(d));
    });
}

} // namespace mirab::app
