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
#include "Language.h"
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

#include <clocale>
#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Qt adopts the user's locale for the C library on Unix; numbers in model
    // files are always read and written with a decimal point.
    std::setlocale(LC_NUMERIC, "C");
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
    const QCommandLineOption selectOpt("select", "Select elements by ID after opening (several separated by commas).",
                                       "id[,id...]");
    const QCommandLineOption regionOpt("select-region",
                                       "Select the elements inside a rectangle of map coordinates.", "x1,y1,x2,y2");
    const QCommandLineOption queryOpt("select-query",
                                      "Select the elements that match an expression such as \"Diameter < 200\"; "
                                      "with --open-tool select-query, fill the dialog with it instead.",
                                      "expression");
    const QCommandLineOption traceOpt("trace", "Trace from the selection: downstream, upstream, connected or path "
                                               "(shortest path between the first and last selected node).",
                                      "kind");
    const QCommandLineOption installEngine(
        "install-engine",
        "Download and install an engine without opening a window, then quit. <source> is \"swmm5plus\" "
        "(the published release for this computer), a manifest URL or a local manifest file.",
        "source");
    const QCommandLineOption reportOpt("report", "With --screenshot: open a report tab (tables, timeseries, profile, "
                                                 "frequency, variability, systemflow, energy, calibration, scenarios, status) first.",
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
                                                  "raster-layers, map-service, observed-data, calibration, "
                                                  "calibration-run (starts the calibration; use --delay to "
                                                  "picture its progress), select-query, run-scenarios, "
                                                  "wastewater (dry-weather flow from a water model) or settings.",
                                     "name");
    const QCommandLineOption scenarioOpt("scenario", "Activate a scenario of the project after opening it.", "name");
    const QCommandLineOption differenceOpt("map-difference",
                                           "Color the map by the change from the results of a scenario (\"Base\" for "
                                           "the base model).",
                                           "name");
    const QCommandLineOption periodOpt("period", "Show the results at a reporting period (0 is the first).", "n");
    parser.addOption(scenarioOpt);
    parser.addOption(differenceOpt);
    parser.addOption(periodOpt);
    parser.addOption(toolOpt);
    parser.addOption(importOpt);
    parser.addOption(dataOpt);
    parser.addOption(optionsOpt);
    const QCommandLineOption animationOpt(
        "export-animation",
        "Save the results animation to <file> (.gif, or .png for numbered images) and quit. "
        "Add --run to simulate first.",
        "file");
    const QCommandLineOption layoutOpt(
        "export-layout", "Save a print layout of the map to <file> (.pdf or .png) and quit.",
        "file");
    const QCommandLineOption layoutChartOpt(
        "layout-chart",
        "With --export-layout: put this report (systemflow, "
        "variability, frequency, profile, energy, calibration, tables) below the map.",
        "kind");
    const QCommandLineOption reportDocOpt(
        "export-report", "Save a report document to <file> (.pdf or .html) and quit.", "file");
    const QCommandLineOption exportDialogOpt(
        "export-dialog", "Open an export dialog: layout, report or animation.", "name");
    parser.addOption(animationOpt);
    parser.addOption(layoutOpt);
    parser.addOption(layoutChartOpt);
    parser.addOption(reportDocOpt);
    parser.addOption(exportDialogOpt);
    parser.addOption(delayOpt);
    parser.addOption(selectOpt);
    parser.addOption(regionOpt);
    parser.addOption(queryOpt);
    parser.addOption(traceOpt);
    parser.addOption(installEngine);
    const QCommandLineOption languageOpt(
        "language", "Interface language: en, fa, ar, es, zh_CN or system (default: the language chosen in Settings).",
        "code");
    parser.addOption(languageOpt);
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
    const bool exporting =
        parser.isSet(animationOpt) || parser.isSet(layoutOpt) || parser.isSet(reportDocOpt);
    QTemporaryDir automationSettings;
    if ((parser.isSet(screenshot) || exporting) && automationSettings.isValid())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, automationSettings.path());
    }

    const QString language = parser.isSet(languageOpt) ? parser.value(languageOpt)
                                                       : mirab::app::configuredLanguage();
    const QString used = mirab::app::installTranslations(language);
    if (parser.isSet(languageOpt) && language != "system" && used != language)
    {
        std::fprintf(stderr, "No translation for language \"%s\".\n", qPrintable(language));
        return 1;
    }

    bool runFailed = false;
    bool exportFailed = false;
    mirab::app::MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openFile(files.first());
    if (parser.isSet(scenarioOpt))
        window.activateScenario(parser.value(scenarioOpt));
    if (parser.isSet(periodOpt))
        window.showPeriod(parser.value(periodOpt).toInt());
    if (parser.isSet(differenceOpt))
    {
        const QString from = parser.value(differenceOpt);
        window.showMapDifference(from.compare("base", Qt::CaseInsensitive) == 0 ? QString() : from);
    }
    if (parser.isSet(selectOpt))
        window.selectElements(parser.value(selectOpt).split(',', Qt::SkipEmptyParts));
    if (parser.isSet(regionOpt))
    {
        const QStringList v = parser.value(regionOpt).split(',');
        if (v.size() == 4)
            window.selectRegion(
                QRectF(QPointF(v[0].toDouble(), v[1].toDouble()), QPointF(v[2].toDouble(), v[3].toDouble())));
    }
    const bool queryDialog = parser.value(toolOpt) == "select-query";
    if (parser.isSet(queryOpt) && !queryDialog)
        window.selectByQuery(parser.value(queryOpt));
    if (parser.isSet(traceOpt))
        window.traceFromSelection(parser.value(traceOpt));
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
    if (parser.isSet(exportDialogOpt))
    {
        const QString name = parser.value(exportDialogOpt);
        QTimer::singleShot(200, &window,
                           [&window, name]
                           {
                               if (name == "layout")
                                   window.exportLayout();
                               else if (name == "report")
                                   window.exportReport();
                               else if (name == "animation")
                                   window.exportAnimation();
                           });
    }
    if (parser.isSet(importOpt))
    {
        const QString f = QFileInfo(parser.value(importOpt)).absoluteFilePath();
        QTimer::singleShot(200, &window, [&window, f] { window.importFile(f); });
    }
    if (parser.isSet(toolOpt))
    {
        const QString name = parser.value(toolOpt);
        const QString expression = parser.value(queryOpt);
        QTimer::singleShot(parser.isSet(runFirst) ? 3000 : 300, &window,
                           [&window, name, expression]
                           {
                               if (name == "select-query")
                                   window.showQueryDialog(expression);
                               else
                                   window.openTool(name);
                           });
    }
    if (parser.isSet(screenshot) || exporting)
    {
        const QString target = parser.value(screenshot);
        const QString report = parser.value(reportOpt);
        const int delay = parser.isSet(delayOpt) ? parser.value(delayOpt).toInt() : 700;
        auto shoot = [&window, target, report, delay]
        {
            if (target.isEmpty())
            {
                QApplication::quit();
                return;
            }
            if (!report.isEmpty())
                window.openReport(report);
            QTimer::singleShot(delay, &window, [&window, target] {
                // A dialog in front (for example the import dialog) is what the picture shows.
                QWidget* modal = QApplication::activeModalWidget();
                (modal ? modal : static_cast<QWidget*>(&window))->grab().save(target);
                QApplication::quit();
            });
        };
        // Exports run after the window is laid out (and after --run), then the picture is taken.
        auto exportThenShoot = [&window, &parser, &exportFailed, shoot, animationOpt, layoutOpt,
                                layoutChartOpt, reportDocOpt]
        {
            const auto file = [&parser](const QCommandLineOption& o)
            { return QFileInfo(parser.value(o)).absoluteFilePath(); };
            if (parser.isSet(layoutOpt) &&
                !window.exportLayout(file(layoutOpt), parser.value(layoutChartOpt)))
                exportFailed = true;
            if (parser.isSet(reportDocOpt) && !window.exportReport(file(reportDocOpt)))
                exportFailed = true;
            if (!parser.isSet(animationOpt))
            {
                shoot();
                return;
            }
            QObject::connect(
                &window, &mirab::app::MainWindow::animationExported, &window,
                [shoot, &exportFailed](bool ok)
                {
                    exportFailed = exportFailed || !ok;
                    shoot();
                },
                Qt::SingleShotConnection);
            window.exportAnimation(file(animationOpt));
        };
        if (parser.isSet(runFirst))
        {
            // Pictures (and exports) during a run: after a delay, after pausing, or after stopping.
            const int during = parser.isSet(runFor) ? parser.value(runFor).toInt() : -1;
            const int pause = parser.isSet(pauseAfter) ? parser.value(pauseAfter).toInt() : -1;
            const int stop = parser.isSet(stopAfter) ? parser.value(stopAfter).toInt() : -1;
            QObject::connect(&window, &mirab::app::MainWindow::simulationCompleted, &window,
                             [exportThenShoot, &runFailed, during, pause, stop](bool ok)
                             {
                                 if (during >= 0 || pause >= 0)
                                     return; // the picture was planned during the run
                                 runFailed = !ok && stop < 0;
                                 exportThenShoot();
                             });
            QTimer::singleShot(500, &window, [&window] { window.runSimulation(); });
            if (during >= 0)
                QTimer::singleShot(500 + during, &window, exportThenShoot);
            else if (pause >= 0)
                QTimer::singleShot(500 + pause, &window,
                                   [&window, exportThenShoot]
                                   {
                                       window.togglePause();
                                       exportThenShoot();
                                   });
            else if (stop >= 0)
                QTimer::singleShot(500 + stop, &window, [&window] { window.stopSimulation(); });
        }
        else
        {
            QTimer::singleShot(exporting ? 300 : 0, &window, exportThenShoot);
        }
        QTimer::singleShot((exporting ? 600000 : 60000) + delay, &app,
                           &QApplication::quit); // safety net
    }
    const int code = app.exec();
    // A failed --run still saves the picture, but tests must see the failure.
    return runFailed || exportFailed ? 1 : code;
}
