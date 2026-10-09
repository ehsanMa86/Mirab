/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/MainWindow.h
 * Purpose: Main window of the Anahita desktop application.
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
#include "anahita/app/PluginInterface.h"
#include "anahita/engine/EngineRegistry.h"

#include <QMainWindow>
#include <QPointer>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QProgressBar;
class QTabWidget;
class QToolBar;

namespace anahita::app
{

class InpEditor;
class MapView;
class MsxEditor;
class ProjectBrowser;
class PropertyPanel;
class ResultsPanel;
class SolverProcess;

class MainWindow : public QMainWindow, public PluginHost
{
    Q_OBJECT
public:
    MainWindow();
    bool openFile(const QString& path);
    /// Starts a run with the selected engine (also used for automated screenshots).
    void runSimulation();
    void selectElement(const QString& id);

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

private:
    void createActions();
    void createMenus();
    void createDocks();
    void createStatusBar();
    void rebuildForDialect();
    void rebuildEngineChoices();
    void updateCompatibility();
    void updateTitle();
    void updateRecentMenu();
    void addRecent(const QString& path);

    bool maybeSave();
    bool save();
    bool saveAs();
    void newProject(core::Dialect dialect);
    void openDialog();
    void simulationFinished(bool ok, const QJsonObject& r);
    void loadResultsDialog();
    bool loadResults(const QString& path);
    void openReport();
    void exportGis(bool geopackage);
    void exportWntr();
    void importWntr();
    void openInQgis();
    void exportMapImage();
    void showResilience();
    void deleteSelection();
    void findElement();
    void chooseMsxFile();
    void chooseBackdrop();
    void loadPlugins();
    QString writeRunInput(QString* error);
    QString currentEngineId() const;
    const engine::EngineDescriptor* currentEngine() const;
    QString msxPath() const;
    void message(const QString& level, const QString& text);

    ProjectDocument* doc_;
    engine::EngineRegistry registry_;
    MapView* map_ = nullptr;
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
    SolverProcess* solver_ = nullptr;
    QString runInput_;

    QMenu* addMenu_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QMenu* pluginMenu_ = nullptr;
    QToolBar* editBar_ = nullptr;
    QActionGroup* toolGroup_ = nullptr;
    QAction* selectAct_ = nullptr;
    QAction* panAct_ = nullptr;
    QAction* runAct_ = nullptr;
    QAction* cancelAct_ = nullptr;
    QAction* saveAct_ = nullptr;
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QAction* deleteAct_ = nullptr;
    QList<QAction*> addActions_;
    QList<AnahitaPlugin*> plugins_;
};

} // namespace anahita::app
