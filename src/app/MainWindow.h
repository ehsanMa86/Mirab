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
    void selectElement(const QString& id);
    /// Opens a report tab by name (tables, timeseries, profile, frequency,
    /// variability, systemflow, energy, status); used by menus and automation.
    void openReport(const QString& kind);
    /// Opens the import dialog for a shapefile, DXF drawing or CSV table.
    void importFile(const QString& path);
    /// Opens the editor of a pattern, curve or time series ([PATTERNS], [CURVES], [TIMESERIES]).
    void openDataEditor(const QString& section, const QString& id);

    // PluginHost
    const core::Model& model() const override { return doc_->model(); }
    bool editModel(const QString& description, const std::function<bool(core::Model&, std::string*)>& fn) override;
    const core::ResultSet* results() const override { return doc_->results(); }
    QString projectPath() const override { return doc_->path(); }
    void log(const QString& message) override;
    QWidget* window() override { return this; }

signals:
    void simulationCompleted(bool success);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
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
    void chooseMsxFile();
    void chooseBackdrop();
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
    QString examplesDirectory() const;

    ProjectDocument* doc_;
    engine::EngineRegistry registry_;
    MapPage* mapPage_ = nullptr;
    MapView* map_ = nullptr;
    StartPage* start_ = nullptr;
    PropertyPanel* props_ = nullptr;
    ResultsPanel* results_ = nullptr;
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
    SolverProcess* solver_ = nullptr;
    QString runInput_;
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
    QAction* saveAct_ = nullptr;
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QAction* deleteAct_ = nullptr;
    QList<QAction*> addActions_;
    QList<MirabPlugin*> plugins_;
};

} // namespace mirab::app
