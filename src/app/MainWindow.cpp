/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/MainWindow.cpp
 * Purpose: Main window: menus, engine selection, simulation runs, exports, QGIS bridge and plugins.
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

#include "Dialogs.h"
#include "MapView.h"
#include "Panels.h"
#include "PropertyPanel.h"
#include "SolverProcess.h"
#include "anahita/AnahitaConfig.h"
#include "anahita/core/Text.h"
#include "anahita/engine/Compatibility.h"
#include "anahita/interop/Interop.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
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
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPluginLoader>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QLibrary>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>

namespace anahita::app
{

using core::Category;
using core::Dialect;

namespace
{
QIcon shapeIcon(const QString& kind)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink(60, 70, 85);
    const QColor fill(90, 140, 210);
    p.setPen(QPen(ink, 2));
    p.setBrush(fill);
    const QPointF c(16, 16);
    if (kind == "Junction")
        p.drawEllipse(c, 7, 7);
    else if (kind == "Reservoir")
        p.drawPolygon(QPolygonF({QPointF(5, 10), QPointF(27, 10), QPointF(23, 24), QPointF(9, 24)}));
    else if (kind == "Tank" || kind == "Storage")
        p.drawRect(QRectF(9, 6, 14, 20));
    else if (kind == "Outfall")
        p.drawPolygon(QPolygonF({QPointF(6, 9), QPointF(26, 9), QPointF(16, 25)}));
    else if (kind == "Divider")
        p.drawPolygon(QPolygonF({QPointF(16, 5), QPointF(27, 16), QPointF(16, 27), QPointF(5, 16)}));
    else if (kind == "Subcatchment")
    {
        p.setBrush(QColor(150, 205, 150));
        p.setPen(QPen(QColor(60, 140, 70), 2));
        p.drawPolygon(QPolygonF({QPointF(5, 8), QPointF(22, 5), QPointF(27, 22), QPointF(10, 27)}));
    }
    else if (kind == "RainGage")
    {
        p.setBrush(QColor(170, 205, 245));
        p.setPen(QPen(QColor(40, 110, 200), 2));
        p.drawEllipse(QPointF(11, 13), 6, 5);
        p.drawEllipse(QPointF(18, 10), 7, 6);
        p.drawEllipse(QPointF(23, 14), 5, 4);
        p.drawLine(QPointF(12, 21), QPointF(10, 27));
        p.drawLine(QPointF(18, 21), QPointF(16, 27));
    }
    else
    {
        p.setPen(QPen(ink, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4, 26), QPointF(28, 6));
        if (kind == "Pump")
        {
            p.setPen(QPen(ink, 2));
            p.drawEllipse(c, 6, 6);
        }
        else if (kind != "Pipe" && kind != "Conduit")
        {
            p.setPen(QPen(ink, 2));
            p.drawPolygon(QPolygonF({QPointF(10, 11), QPointF(22, 21), QPointF(22, 11), QPointF(10, 21)}));
        }
    }
    return QIcon(pm);
}

QIcon handIcon(bool pan)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(60, 70, 85), 2));
    if (pan)
    {
        p.drawLine(16, 4, 16, 28);
        p.drawLine(4, 16, 28, 16);
        for (QPointF tip : {QPointF(16, 4), QPointF(16, 28), QPointF(4, 16), QPointF(28, 16)})
            p.drawEllipse(tip, 2, 2);
    }
    else
    {
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF({QPointF(9, 4), QPointF(9, 26), QPointF(14, 21), QPointF(18, 29), QPointF(21, 27),
                                 QPointF(17, 19), QPointF(24, 19)}));
    }
    return QIcon(pm);
}
} // namespace

MainWindow::MainWindow() : doc_(new ProjectDocument(this))
{
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/anahita/anahita.png"));
    map_ = new MapView(doc_, this);
    inpEditor_ = new InpEditor(doc_, this);
    msxEditor_ = new MsxEditor(this);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->addTab(map_, tr("Map"));
    tabs_->addTab(inpEditor_, tr("Input file"));
    setCentralWidget(tabs_);
    solver_ = new SolverProcess(this);

    createActions();
    createDocks();
    createMenus();
    createStatusBar();

    connect(doc_, &ProjectDocument::titleChanged, this, &MainWindow::updateTitle);
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        rebuildForDialect();
        updateCompatibility();
    });
    connect(doc_, &ProjectDocument::modelChanged, this, &MainWindow::updateCompatibility);
    connect(doc_, &ProjectDocument::selectionChanged, this,
            [this] { deleteAct_->setEnabled(!doc_->selection().isEmpty()); });
    connect(map_, &MapView::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(map_, &MapView::cursorMoved, this, [this](const QPointF& w) {
        coords_->setText(QString("X %1   Y %2").arg(w.x(), 0, 'f', 2).arg(w.y(), 0, 'f', 2));
    });
    connect(map_, &MapView::toolFinished, this, [this] { selectAct_->setChecked(true); });
    connect(map_, &MapView::contextMenuForSelection, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(deleteAct_);
        menu.addAction(tr("Zoom to fit"), map_, &MapView::fitToWindow);
        menu.exec(pos);
    });
    connect(inpEditor_, &InpEditor::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(msxEditor_, &MsxEditor::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(props_, &PropertyPanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });
    connect(solver_, &SolverProcess::progress, this, [this](double f, const QString& stage) {
        progress_->setValue(static_cast<int>(f * 100));
        progress_->setFormat(stage + "  %p%");
    });
    connect(solver_, &SolverProcess::log, this, &MainWindow::message);
    connect(solver_, &SolverProcess::finished, this, &MainWindow::simulationFinished);

    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    restoreState(s.value("window/state").toByteArray());
    if (!s.contains("window/geometry"))
        resize(1360, 860);

    doc_->newProject(Dialect::Epanet, "LPS");
    loadPlugins();
    updateRecentMenu();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
void MainWindow::createActions()
{
    toolGroup_ = new QActionGroup(this);
    selectAct_ = new QAction(handIcon(false), tr("Select"), this);
    selectAct_->setCheckable(true);
    selectAct_->setChecked(true);
    selectAct_->setShortcut(Qt::Key_S);
    selectAct_->setToolTip(tr("Select and move elements (S)"));
    panAct_ = new QAction(handIcon(true), tr("Pan"), this);
    panAct_->setCheckable(true);
    panAct_->setToolTip(tr("Pan the map; the middle mouse button also pans"));
    toolGroup_->addAction(selectAct_);
    toolGroup_->addAction(panAct_);
    connect(selectAct_, &QAction::triggered, this, [this] { map_->setTool(MapView::Tool::Select); });
    connect(panAct_, &QAction::triggered, this, [this] { map_->setTool(MapView::Tool::Pan); });

    runAct_ = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), tr("Run simulation"), this);
    runAct_->setShortcut(Qt::Key_F5);
    connect(runAct_, &QAction::triggered, this, &MainWindow::runSimulation);
    cancelAct_ = new QAction(style()->standardIcon(QStyle::SP_MediaStop), tr("Stop simulation"), this);
    cancelAct_->setEnabled(false);
    connect(cancelAct_, &QAction::triggered, solver_, &SolverProcess::cancel);
    saveAct_ = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("&Save"), this);
    saveAct_->setShortcut(QKeySequence::Save);
    connect(saveAct_, &QAction::triggered, this, &MainWindow::save);
    undoAct_ = doc_->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    undoAct_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    redoAct_ = doc_->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAct_->setShortcut(QKeySequence::Redo);
    redoAct_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    deleteAct_ = new QAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("&Delete"), this);
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
        addDockWidget(area, d);
        return d;
    };
    browser_ = new ProjectBrowser(doc_, this);
    props_ = new PropertyPanel(doc_, this);
    results_ = new ResultsPanel(doc_, map_, this);
    messages_ = new QListWidget(this);
    messages_->setWordWrap(true);
    auto* dBrowser = dock(tr("Project"), "projectDock", browser_, Qt::LeftDockWidgetArea);
    auto* dProps = dock(tr("Properties"), "propertiesDock", props_, Qt::RightDockWidgetArea);
    auto* dResults = dock(tr("Results"), "resultsDock", results_, Qt::BottomDockWidgetArea);
    auto* dMessages = dock(tr("Messages"), "messagesDock", messages_, Qt::BottomDockWidgetArea);
    tabifyDockWidget(dResults, dMessages);
    dResults->raise();
    (void)dBrowser;
    (void)dProps;
    connect(browser_, &ProjectBrowser::elementActivated, this, [dProps] { dProps->raise(); });
    connect(map_, &MapView::elementActivated, this, [dProps] {
        dProps->show();
        dProps->raise();
    });
}

void MainWindow::createMenus()
{
    // File
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("New &EPANET project..."), QKeySequence::New, this, [this] { newProject(Dialect::Epanet); });
    file->addAction(tr("New &SWMM project..."), this, [this] { newProject(Dialect::Swmm); });
    file->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open..."), QKeySequence::Open, this,
                    &MainWindow::openDialog);
    recentMenu_ = file->addMenu(tr("Open &recent"));
    file->addAction(saveAct_);
    file->addAction(tr("Save &as..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    QMenu* imp = file->addMenu(tr("&Import"));
    imp->addAction(tr("WNTR network (JSON)..."), this, &MainWindow::importWntr);
    QMenu* exp = file->addMenu(tr("&Export"));
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
    file->addAction(tr("Open in &QGIS"), this, &MainWindow::openInQgis);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    // Edit
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(undoAct_);
    edit->addAction(redoAct_);
    edit->addSeparator();
    edit->addAction(deleteAct_);
    edit->addAction(tr("&Find element..."), QKeySequence::Find, this, &MainWindow::findElement);

    // View
    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Zoom to &fit"), QKeySequence(Qt::Key_F), map_, &MapView::fitToWindow);
    view->addAction(tr("Zoom &in"), QKeySequence::ZoomIn, this, [this] { map_->zoomBy(1.5); });
    view->addAction(tr("Zoom &out"), QKeySequence::ZoomOut, this, [this] { map_->zoomBy(1 / 1.5); });
    QAction* labels = view->addAction(tr("Show &labels"));
    labels->setCheckable(true);
    labels->setChecked(true);
    connect(labels, &QAction::toggled, map_, &MapView::setShowLabels);
    view->addSeparator();
    view->addAction(tr("&Backdrop image..."), this, &MainWindow::chooseBackdrop);
    view->addAction(tr("&Clear backdrop"), this, [this] {
        map_->clearBackdrop();
        doc_->settings().backdropImage.clear();
        doc_->markSettingsChanged();
    });
    view->addSeparator();
    for (auto* d : findChildren<QDockWidget*>())
        view->addAction(d->toggleViewAction());

    // Add
    addMenu_ = menuBar()->addMenu(tr("&Add"));

    // Simulation
    QMenu* sim = menuBar()->addMenu(tr("&Simulation"));
    sim->addAction(runAct_);
    sim->addAction(cancelAct_);
    sim->addSeparator();
    sim->addAction(tr("Choose &MSX file..."), this, &MainWindow::chooseMsxFile);
    sim->addAction(tr("&Check compatibility"), this, [this] {
        updateCompatibility();
        findChild<QDockWidget*>("messagesDock")->raise();
    });
    sim->addSeparator();
    sim->addAction(tr("Open &report"), this, &MainWindow::openReport);
    sim->addAction(tr("&Load results..."), this, &MainWindow::loadResultsDialog);
    sim->addAction(tr("C&lear results"), doc_, &ProjectDocument::clearResults);

    // Analysis
    QMenu* analysis = menuBar()->addMenu(tr("A&nalysis"));
    analysis->addAction(tr("&Resilience tools..."), this, &MainWindow::showResilience);

    // Tools
    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("Simulation &engines..."), this, [this] {
        EnginesDialog dlg(registry_, this);
        connect(&dlg, &EnginesDialog::enginesChanged, this, [this] {
            rebuildEngineChoices();
            updateCompatibility();
        });
        dlg.exec();
    });
    pluginMenu_ = tools->addMenu(tr("&Plugins"));
    tools->addAction(tr("&Settings..."), this, [this] { SettingsDialog(this).exec(); });

    // Help
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Documentation"), this, [] {
        QDesktopServices::openUrl(QUrl(QString(ANAHITA_HOMEPAGE) + "/tree/main/docs"));
    });
    help->addAction(tr("Open &examples folder"), this, [] {
        const QString base = QCoreApplication::applicationDirPath();
        for (const char* rel : {"../examples", "examples", "../share/anahita/examples", "../Resources/examples"})
        {
            const QDir d(QDir(base).filePath(rel));
            if (d.exists())
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(d.absolutePath()));
                return;
            }
        }
    });
    help->addSeparator();
    help->addAction(tr("&About Anahita"), this, [this] { AboutDialog(this).exec(); });
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

    // Tool bars
    QToolBar* fileBar = addToolBar(tr("File"));
    fileBar->setObjectName("fileToolBar");
    fileBar->addAction(style()->standardIcon(QStyle::SP_FileIcon), tr("New EPANET project"), this,
                       [this] { newProject(Dialect::Epanet); });
    fileBar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open"), this, &MainWindow::openDialog);
    fileBar->addAction(saveAct_);
    fileBar->addAction(undoAct_);
    fileBar->addAction(redoAct_);

    editBar_ = addToolBar(tr("Edit"));
    editBar_->setObjectName("editToolBar");

    QToolBar* simBar = addToolBar(tr("Simulation"));
    simBar->setObjectName("simulationToolBar");
    simBar->addWidget(new QLabel(tr(" Engine "), this));
    engineCombo_ = new QComboBox(this);
    engineCombo_->setMinimumWidth(230);
    engineCombo_->setToolTip(tr("Solver used for the next run"));
    simBar->addWidget(engineCombo_);
    msxCheck_ = new QCheckBox(tr("Multi-species (MSX)"), this);
    msxCheck_->setToolTip(tr("Run EPANET-MSX on top of the selected EPANET engine"));
    msxAction_ = simBar->addWidget(msxCheck_);
    simBar->addAction(runAct_);
    simBar->addAction(cancelAct_);
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
                tabs_->addTab(msxEditor_, tr("MSX file"));
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
    progress_->setVisible(false);
    coords_ = new QLabel(this);
    coords_->setMinimumWidth(200);
    engineLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(engineLabel_);
    statusBar()->addPermanentWidget(coords_);
}

void MainWindow::rebuildForDialect()
{
    const Dialect d = doc_->model().dialect();
    for (QAction* a : addActions_)
    {
        toolGroup_->removeAction(a);
        a->deleteLater();
    }
    addActions_.clear();
    addMenu_->clear();
    editBar_->clear();
    editBar_->addAction(selectAct_);
    editBar_->addAction(panAct_);
    editBar_->addSeparator();
    auto addTool = [&](const QString& kind, const QString& label, MapView::Tool tool) {
        auto* a = new QAction(shapeIcon(kind), label, this);
        a->setCheckable(true);
        a->setToolTip(tr("Add %1").arg(label.toLower()));
        toolGroup_->addAction(a);
        connect(a, &QAction::triggered, this, [this, kind, tool] { map_->setTool(tool, kind); });
        addMenu_->addAction(a);
        editBar_->addAction(a);
        addActions_ << a;
    };
    for (const auto& k : core::elementKinds(d, Category::Node))
        addTool(QString::fromStdString(k), QString::fromStdString(k), MapView::Tool::AddNode);
    addMenu_->addSeparator();
    editBar_->addSeparator();
    for (const auto& k : core::elementKinds(d, Category::Link))
        addTool(QString::fromStdString(k), QString::fromStdString(k), MapView::Tool::AddLink);
    if (d == Dialect::Swmm)
    {
        addMenu_->addSeparator();
        editBar_->addSeparator();
        addTool("Subcatchment", tr("Subcatchment"), MapView::Tool::AddArea);
        addTool("RainGage", tr("Rain gage"), MapView::Tool::AddGage);
    }
    editBar_->addSeparator();
    editBar_->addAction(deleteAct_);
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
        tabs_->addTab(msxEditor_, tr("MSX file"));
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
        engineCombo_->addItem(QString::fromStdString(desc->name) + (ok ? QString() : tr(" (not installed)")),
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

void MainWindow::updateCompatibility()
{
    const auto* desc = currentEngine();
    props_->setEngine(desc);
    messages_->clear();
    if (!desc)
    {
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
        message("warning", tr("Choose an EPANET-MSX file (Simulation > Choose MSX file) before running."));
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
    runAct_->setEnabled(ok);
}

void MainWindow::message(const QString& level, const QString& text)
{
    auto* item = new QListWidgetItem(text, messages_);
    if (level == "error")
    {
        item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical));
    }
    else if (level == "warning")
    {
        item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    }
    else
    {
        item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
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
    setWindowTitle(QString("%1%2 - Anahita").arg(doc_->displayName(), doc_->isModified() ? "*" : ""));
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
    tabs_->setCurrentWidget(map_);
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

bool MainWindow::openFile(const QString& path)
{
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
    tabs_->setCurrentWidget(map_);
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
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
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
    const QString file = QDir(dir).filePath("." + base + ".anahita-run.inp");
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
    const QString base = doc_->path().isEmpty()
                             ? QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("untitled")
                             : QFileInfo(doc_->path()).dir().filePath(QFileInfo(doc_->path()).completeBaseName());
    QStringList args = {"run", runInput_, "--engine", QString::fromStdString(desc->id), "--out", base + ".anr",
                        "--report", base + ".rpt"};
    if (desc->multiSpecies)
        args << "--msx" << msxPath();
    messages_->clear();
    message("info", tr("Running %1...").arg(QString::fromStdString(desc->name)));
    findChild<QDockWidget*>("messagesDock")->raise();
    progress_->setValue(0);
    progress_->setVisible(true);
    runAct_->setEnabled(false);
    cancelAct_->setEnabled(true);
    solver_->start(args);
}

void MainWindow::simulationFinished(bool ok, const QJsonObject& r)
{
    progress_->setVisible(false);
    cancelAct_->setEnabled(false);
    if (!runInput_.isEmpty())
    {
        QFile::remove(runInput_);
        runInput_.clear();
    }
    if (ok)
    {
        message("info", tr("Simulation finished in %1 s with %2 reporting periods.")
                            .arg(r.value("seconds").toDouble(), 0, 'f', 2)
                            .arg(r.value("periods").toInt()));
        loadResults(r.value("results").toString());
        findChild<QDockWidget*>("resultsDock")->raise();
    }
    else if (r.value("cancelled").toBool())
    {
        message("warning", tr("Simulation cancelled."));
    }
    else
    {
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

void MainWindow::openReport()
{
    const QString base = doc_->path().isEmpty()
                             ? QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("untitled")
                             : QFileInfo(doc_->path()).dir().filePath(QFileInfo(doc_->path()).completeBaseName());
    const QString rpt = base + ".rpt";
    if (!QFileInfo::exists(rpt))
    {
        statusBar()->showMessage(tr("Run a simulation first."), 5000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(rpt));
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
        tabs_->addTab(msxEditor_, tr("MSX file"));
    updateCompatibility();
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
    tabs_->setCurrentWidget(map_);
}

void MainWindow::chooseBackdrop()
{
    const QString start = doc_->path().isEmpty() ? QDir::homePath() : QFileInfo(doc_->path()).absolutePath();
    const QString f = QFileDialog::getOpenFileName(this, tr("Backdrop image"), start,
                                                   tr("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp)"));
    if (f.isEmpty())
        return;
    QString err;
    if (!map_->setBackdrop(f, &err))
    {
        QMessageBox::warning(this, tr("Backdrop"), err);
        return;
    }
    doc_->settings().backdropImage = doc_->path().isEmpty() ? f.toStdString()
                                                            : QFileInfo(doc_->path()).dir().relativeFilePath(f).toStdString();
    doc_->markSettingsChanged();
}

// ---------------------------------------------------------------------------
// Interchange and QGIS
// ---------------------------------------------------------------------------
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
    {
        bool ok = false;
        const QString crs = QInputDialog::getText(this, tr("Coordinate system"),
                                                  tr("Coordinate reference system of the map coordinates (for example EPSG:32614). "
                                                     "Leave empty if unknown."),
                                                  QLineEdit::Normal, {}, &ok);
        if (ok && !crs.trimmed().isEmpty())
        {
            doc_->settings().crs = crs.trimmed().toUpper().toStdString();
            doc_->markSettingsChanged();
        }
    }
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

void MainWindow::showResilience()
{
    if (doc_->model().dialect() != Dialect::Epanet)
    {
        QMessageBox::information(this, tr("Resilience"),
                                 tr("The resilience tools apply to water distribution (EPANET) models. "
                                    "Network topology is available for both model types from this dialog."));
    }
    QString err;
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString input = QDir(tmpDir).filePath("anahita-resilience-" + QString::number(QCoreApplication::applicationPid()) + ".inp");
    QFile f(input);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::fromStdString(doc_->model().toText()));
    f.close();
    QString engineId = engineCombo_->currentData().toString();
    ResilienceDialog dlg(doc_, engineId, input, this);
    dlg.exec();
    QFile::remove(input);
}

// ---------------------------------------------------------------------------
// Plugins
// ---------------------------------------------------------------------------
void MainWindow::loadPlugins()
{
    const QString app = QCoreApplication::applicationDirPath();
    QStringList dirs = {QDir(app).filePath("plugins"), QDir(app).filePath("../lib/anahita/plugins"),
                        QDir(app).filePath("../PlugIns/anahita"),
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
            auto* plugin = qobject_cast<AnahitaPlugin*>(obj);
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

} // namespace anahita::app
