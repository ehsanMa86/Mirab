/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/main.cpp
 * Purpose: Entry point of the Mirab desktop application.
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
#include "EngineDownloader.h"
#include "MainWindow.h"
#include "Theme.h"

#include "mirab/MirabConfig.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Mirab");
    QApplication::setOrganizationName("Mirab");
    QApplication::setOrganizationDomain("mirab.app");
    QApplication::setApplicationVersion(MIRAB_VERSION_STRING);
    QApplication::setWindowIcon(QIcon(":/mirab/mirab.png"));
    mirab::app::theme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("Unified modeling environment for water distribution and stormwater systems");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "EPANET or SWMM input file to open", "[file.inp]");
    const QCommandLineOption screenshot("screenshot", "Save a picture of the window to <png> and quit.", "png");
    const QCommandLineOption runFirst("run", "With --screenshot: run the simulation before the picture is taken.");
    const QCommandLineOption selectOpt("select", "Select an element by ID after opening.", "id");
    const QCommandLineOption installEngine(
        "install-engine",
        "Download and install an engine without opening a window, then quit. <source> is \"swmm5plus\" "
        "(the published release for this computer), a manifest URL or a local manifest file.",
        "source");
    const QCommandLineOption reportOpt("report", "With --screenshot: open a report tab (tables, timeseries, profile, "
                                                 "frequency, variability, systemflow, energy, calibration, status) first.",
                                       "kind");
    const QCommandLineOption delayOpt("delay", "With --screenshot: milliseconds to wait before the picture (default 700).",
                                      "ms");
    const QCommandLineOption runFor("run-for", "With --run: take the picture <ms> after the simulation starts, while it "
                                               "is still running.",
                                    "ms");
    const QCommandLineOption pauseAfter("pause-after", "With --run: pause the simulation <ms> after it starts; the "
                                                       "picture follows.",
                                        "ms");
    const QCommandLineOption stopAfter("stop-after", "With --run: stop the simulation <ms> after it starts; the picture "
                                                     "follows.",
                                       "ms");
    parser.addOption(screenshot);
    parser.addOption(runFirst);
    parser.addOption(runFor);
    parser.addOption(pauseAfter);
    parser.addOption(stopAfter);
    const QCommandLineOption importOpt("import", "Open the import dialog for a shapefile, DXF drawing or CSV table.", "file");
    parser.addOption(reportOpt);
    const QCommandLineOption dataOpt("show-data",
                                     "Open the editor of a pattern, curve, time series, named object (for example "
                                     "POLLUTANTS:TSS), LID control, transect, unit hydrograph group or the control "
                                     "rules, given as SECTION:ID (for example PATTERNS:PAT1, LID_CONTROLS:Cell1, "
                                     "CONTROLS: or RULES:), or the LID usage dialog of a subcatchment (LID_USAGE:S1).",
                                     "section:id");
    const QCommandLineOption optionsOpt("options-dialog", "Open the simulation options dialog at a page (general, dates, "
                                                          "steps, dynwave, files, report, evaporation, climate, "
                                                          "adjustments for SWMM; hydraulics, quality, times, energy, "
                                                          "reactions, report for EPANET).",
                                        "page");
    const QCommandLineOption toolOpt("open-tool", "Open a tool dialog after the file: elevations (from a DEM), "
                                                  "raster-layers, map-service, observed-data, calibration or "
                                                  "calibration-run (starts the calibration; use --delay to "
                                                  "picture its progress).",
                                     "name");
    parser.addOption(toolOpt);
    parser.addOption(importOpt);
    parser.addOption(dataOpt);
    parser.addOption(optionsOpt);
    parser.addOption(delayOpt);
    parser.addOption(selectOpt);
    parser.addOption(installEngine);
    parser.process(app);

    if (parser.isSet(installEngine))
    {
        const QString source = parser.value(installEngine);
        mirab::app::EngineDownloader downloader;
        int status = 1;
        QObject::connect(&downloader, &mirab::app::EngineDownloader::progress, &app,
                         [last = QString()](qint64, qint64, const QString& stage) mutable {
                             if (stage != last)
                                 std::fprintf(stderr, "%s\n", qPrintable(stage));
                             last = stage;
                         });
        QObject::connect(&downloader, &mirab::app::EngineDownloader::finished, &app,
                         [&status](bool ok, const QString& message) {
                             std::fprintf(ok ? stdout : stderr, "%s\n", qPrintable(message));
                             status = ok ? 0 : 1;
                             QCoreApplication::quit();
                         });
        QTimer::singleShot(0, &app, [&] {
            if (source.compare("swmm5plus", Qt::CaseInsensitive) == 0 || source == "swmm5+")
                downloader.start(mirab::app::EngineDownloader::swmm5PlusManifestUrl());
            else if (QFileInfo::exists(source))
                downloader.installLocal(QFileInfo(source).absoluteFilePath());
            else
                downloader.start(QUrl::fromUserInput(source));
        });
        app.exec();
        return status;
    }

    // Automated runs (screenshots, tests) keep their settings in a temporary
    // folder, so they never change the user's recent files or window layout.
    QTemporaryDir automationSettings;
    if (parser.isSet(screenshot) && automationSettings.isValid())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, automationSettings.path());
    }

    bool runFailed = false;
    mirab::app::MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openFile(files.first());
    if (parser.isSet(selectOpt))
        window.selectElement(parser.value(selectOpt));
    if (parser.isSet(dataOpt))
    {
        const QString v = parser.value(dataOpt);
        window.openDataEditor(v.section(':', 0, 0).toUpper(), v.section(':', 1));
    }
    if (parser.isSet(optionsOpt))
    {
        const QString page = parser.value(optionsOpt);
        QTimer::singleShot(200, &window, [&window, page] { window.showSimulationOptions(page); });
    }
    if (parser.isSet(importOpt))
    {
        const QString f = QFileInfo(parser.value(importOpt)).absoluteFilePath();
        QTimer::singleShot(200, &window, [&window, f] { window.importFile(f); });
    }
    if (parser.isSet(toolOpt))
    {
        const QString name = parser.value(toolOpt);
        QTimer::singleShot(parser.isSet(runFirst) ? 3000 : 300, &window, [&window, name] { window.openTool(name); });
    }
    if (parser.isSet(screenshot))
    {
        const QString target = parser.value(screenshot);
        const QString report = parser.value(reportOpt);
        const int delay = parser.isSet(delayOpt) ? parser.value(delayOpt).toInt() : 700;
        auto shoot = [&window, target, report, delay] {
            if (!report.isEmpty())
                window.openReport(report);
            QTimer::singleShot(delay, &window, [&window, target] {
                // A dialog in front (for example the import dialog) is what the picture shows.
                QWidget* modal = QApplication::activeModalWidget();
                (modal ? modal : static_cast<QWidget*>(&window))->grab().save(target);
                QApplication::quit();
            });
        };
        if (parser.isSet(runFirst))
        {
            // Pictures during a run: after a delay, after pausing, or after stopping.
            const int during = parser.isSet(runFor) ? parser.value(runFor).toInt() : -1;
            const int pause = parser.isSet(pauseAfter) ? parser.value(pauseAfter).toInt() : -1;
            const int stop = parser.isSet(stopAfter) ? parser.value(stopAfter).toInt() : -1;
            QObject::connect(&window, &mirab::app::MainWindow::simulationCompleted, &window,
                             [shoot, &runFailed, during, pause, stop](bool ok) {
                                 if (during >= 0 || pause >= 0)
                                     return; // the picture was planned during the run
                                 runFailed = !ok && stop < 0;
                                 shoot();
                             });
            QTimer::singleShot(500, &window, [&window] { window.runSimulation(); });
            if (during >= 0)
                QTimer::singleShot(500 + during, &window, shoot);
            else if (pause >= 0)
                QTimer::singleShot(500 + pause, &window, [&window, shoot] {
                    window.togglePause();
                    shoot();
                });
            else if (stop >= 0)
                QTimer::singleShot(500 + stop, &window, [&window] { window.stopSimulation(); });
        }
        else
        {
            shoot();
        }
        QTimer::singleShot(60000 + delay, &app, &QApplication::quit); // safety net
    }
    const int code = app.exec();
    // A failed --run still saves the picture, but tests must see the failure.
    return runFailed ? 1 : code;
}
