/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MainWindow.h
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
#pragma once

#include "ProjectDocument.h"
#include "SelectionTools.h"
#include "mirab/app/PluginInterface.h"
#include "mirab/engine/EngineRegistry.h"

#include <QMainWindow>
#include <QPointer>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QMenu;
class QProgressBar;
class QTabWidget;
class QToolBar;
class QToolButton;

namespace mirab::app
{

class InpEditor;
class MapPage;
class MapView;
class MsxEditor;
class ProjectBrowser;
class PropertyPanel;
class ResultsPanel;
class ScenarioPanel;
class ScenarioRunDialog;
class SolverProcess;
class StartPage;

class MainWindow : public QMainWindow, public PluginHost
{
    Q_OBJECT
public:
    MainWindow();
    bool openFile(const QString& path);
    /// Starts a run with the selected engine (also used for automated screenshots).
    void runSimulation();
    /// Pauses or resumes the running simulation.
    void togglePause();
    /// Stops the running simulation; the results computed so far stay on the map.
    void stopSimulation();
    void selectElement(const QString& id);
    /// Selects elements by ID (nodes, links, subcatchments and rain gages).
    void selectElements(const QStringList& ids);
    /// Selects the elements inside a rectangle given in map coordinates.
    void selectRegion(const QRectF& world);
    /// Traces from the selection: "downstream", "upstream", "connected" or
    /// "path" (shortest path between the first and last selected node).
    void traceFromSelection(const QString& kind);
    /// Selects every element that matches a query expression.
    void selectByQuery(const QString& expression);
    /// Opens the select by query dialog, optionally with an expression.
    void showQueryDialog(const QString& expression = {});
    /// Opens a report tab by name (tables, timeseries, profile, frequency,
    /// variability, systemflow, energy, calibration, status); used by menus and automation.
    void openReport(const QString& kind);
    /// Shows and edits a scenario (empty: the base model).
    bool activateScenario(const QString& name);
    /// Colors the map by the change from another scenario's results (empty: the base model).
    bool showMapDifference(const QString& scenario);
    /// Opens the dialog that runs several scenarios.
    void runScenarios();
    /// Shows the results at a reporting period (automation).
    void showPeriod(int period) { doc_->setPeriod(period); }
    /// Opens the import dialog for a shapefile, DXF drawing or CSV table.
    void importFile(const QString& path);
    /// Opens the editor of a pattern, curve or time series ([PATTERNS], [CURVES], [TIMESERIES]),
    /// of a named object such as a SWMM pollutant ([POLLUTANTS]), LID control, transect or unit
    /// hydrograph group, of the control rules, or the LID usage dialog of a subcatchment ("LID_USAGE").
    void openDataEditor(const QString& section, const QString& id);
    /// Opens the simulation options dialog, optionally at a page (see OptionsDialog::showPage).
    void showSimulationOptions(const QString& page = {});
    /// Opens a tool dialog: "elevations", "raster-layers", "map-service", "observed-data",
    /// "calibration", "calibration-run" (the calibration dialog with the run started) or
    /// "run-scenarios".
    void openTool(const QString& name);

    // Deliverables. An empty path asks the user; a path exports without
    // questions (automation) and reports through the return value or signal.
    void exportAnimation(const QString& path = {});
    bool exportLayout(const QString& path = {}, const QString& chartReport = {});
    bool exportReport(const QString& path = {});

    // PluginHost
    const core::Model& model() const override { return doc_->model(); }
    bool editModel(const QString& description, const std::function<bool(core::Model&, std::string*)>& fn) override;
    const core::ResultSet* results() const override { return doc_->results(); }
    QString projectPath() const override { return doc_->path(); }
    void log(const QString& message) override;
    QWidget* window() override { return this; }

signals:
    void simulationCompleted(bool success);
    void animationExported(bool success);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void openCalibrationTool(const QString& name);
    void createActions();
    void createDocks();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void resetLayout();
    void rebuildForDialect();
    void rebuildEngineChoices();
    void updateCompatibility();
    void updateTitle();
    void updateRecentMenu();
    void updateStartPage();
    void updateStatusInfo();
    void addRecent(const QString& path);

    bool maybeSave();
    bool save();
    bool saveAs();
    void newProject(core::Dialect dialect);
    void openDialog();
    void openExample(const QString& relative);
    void simulationFinished(bool ok, const QJsonObject& r);
    void updatePauseAction();
    void loadResultsDialog();
    bool loadResults(const QString& path);
    void exportGis(bool geopackage);
    void exportWntr();
    void importWntr();
    void importGisFile(const QString& format);
    void openInQgis();
    void exportMapImage();
    void showResilience(const QString& page = {});
    void deleteSelection();
    void findElement();
    void trace(core::TraceDirection direction);
    void selectShortestPath();
    void profileAlongSelection();
    void selectAllOfKind(core::Category category, const QString& kind);
    void invertSelection();
    void exportSelectionCsv();
    void reverseSelectedLinks();
    void updateSelectionActions();
    void fillSelectMenu(QMenu* menu);
    void chooseMsxFile();
    void chooseBackdrop();
    void addMapService();
    void showRasterLayers();
    void elevationsFromDem();
    void chooseCoordinateSystem();
    void loadPlugins();
    QString writeRunInput(QString* error);
    QString currentEngineId() const;
    const engine::EngineDescriptor* currentEngine() const;
    QString msxPath() const;
    QString runBase() const;
    void message(const QString& level, const QString& text);
    void showDock(const QString& name);
    void decorateTab(int index);
    bool openSpecialEditor(const QString& section, const QString& id);
    QString examplesDirectory() const;

    ProjectDocument* doc_;
    engine::EngineRegistry registry_;
    MapPage* mapPage_ = nullptr;
    MapView* map_ = nullptr;
    StartPage* start_ = nullptr;
    PropertyPanel* props_ = nullptr;
    ResultsPanel* results_ = nullptr;
    ScenarioPanel* scenarios_ = nullptr;
    QPointer<ScenarioRunDialog> runDialog_;
    QLabel* scenarioLabel_ = nullptr;
    ProjectBrowser* browser_ = nullptr;
    InpEditor* inpEditor_ = nullptr;
    MsxEditor* msxEditor_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QListWidget* messages_ = nullptr;
    QComboBox* engineCombo_ = nullptr;
    QCheckBox* msxCheck_ = nullptr;
    QAction* msxAction_ = nullptr; ///< toolbar slot of msxCheck_ (controls its visibility)
    QProgressBar* progress_ = nullptr;
    QLabel* coords_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* unitsLabel_ = nullptr;
    QLabel* countsLabel_ = nullptr;
    QLabel* runState_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    SolverProcess* solver_ = nullptr;
    QString runInput_;
    bool discardRun_ = false; ///< the project changed during the run
    QByteArray defaultState_;
    bool layoutRestored_ = false;
    bool compatibilityPass_ = false;

    QToolBar* mainBar_ = nullptr;
    QMenu* addMenu_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QMenu* pluginMenu_ = nullptr;
    QMenu* panelsMenu_ = nullptr;
    QToolButton* addButton_ = nullptr;
    QToolButton* runButton_ = nullptr;
    QActionGroup* toolGroup_ = nullptr;
    QAction* selectAct_ = nullptr;
    QAction* panAct_ = nullptr;
    QAction* zoomWindowAct_ = nullptr;
    QAction* runAct_ = nullptr;
    QAction* cancelAct_ = nullptr;
    QAction* pauseAct_ = nullptr;
    QAction* saveAct_ = nullptr;
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QAction* deleteAct_ = nullptr;
    QAction* lassoAct_ = nullptr;
    QAction* measureLengthAct_ = nullptr;
    QAction* measureAreaAct_ = nullptr;
    QAction* splitAct_ = nullptr;
    QAction* selectAllAct_ = nullptr;
    QAction* invertAct_ = nullptr;
    QAction* queryAct_ = nullptr;
    QAction* traceDownAct_ = nullptr;
    QAction* traceUpAct_ = nullptr;
    QAction* connectedAct_ = nullptr;
    QAction* pathAct_ = nullptr;
    QAction* profilePathAct_ = nullptr;
    QAction* zoomSelectionAct_ = nullptr;
    QAction* reverseAct_ = nullptr;
    QAction* exportSelectionAct_ = nullptr;
    QAction* followFlowsAct_ = nullptr;
    QAction* stopClosedAct_ = nullptr;
    QList<QAction*> addActions_;
    QList<MirabPlugin*> plugins_;
};

} // namespace mirab::app
