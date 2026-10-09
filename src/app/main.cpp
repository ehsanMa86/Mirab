/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/main.cpp
 * Purpose: Entry point of the Anahita desktop application.
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

#include "anahita/AnahitaConfig.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Anahita");
    QApplication::setOrganizationName("Anahita");
    QApplication::setOrganizationDomain("anahita.app");
    QApplication::setApplicationVersion(ANAHITA_VERSION_STRING);
    QApplication::setWindowIcon(QIcon(":/anahita/anahita.png"));

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
    parser.addOption(screenshot);
    parser.addOption(runFirst);
    parser.addOption(selectOpt);
    parser.addOption(installEngine);
    parser.process(app);

    if (parser.isSet(installEngine))
    {
        const QString source = parser.value(installEngine);
        anahita::app::EngineDownloader downloader;
        int status = 1;
        QObject::connect(&downloader, &anahita::app::EngineDownloader::progress, &app,
                         [last = QString()](qint64, qint64, const QString& stage) mutable {
                             if (stage != last)
                                 std::fprintf(stderr, "%s\n", qPrintable(stage));
                             last = stage;
                         });
        QObject::connect(&downloader, &anahita::app::EngineDownloader::finished, &app,
                         [&status](bool ok, const QString& message) {
                             std::fprintf(ok ? stdout : stderr, "%s\n", qPrintable(message));
                             status = ok ? 0 : 1;
                             QCoreApplication::quit();
                         });
        QTimer::singleShot(0, &app, [&] {
            if (source.compare("swmm5plus", Qt::CaseInsensitive) == 0 || source == "swmm5+")
                downloader.start(anahita::app::EngineDownloader::swmm5PlusManifestUrl());
            else if (QFileInfo::exists(source))
                downloader.installLocal(QFileInfo(source).absoluteFilePath());
            else
                downloader.start(QUrl::fromUserInput(source));
        });
        app.exec();
        return status;
    }

    anahita::app::MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openFile(files.first());
    if (parser.isSet(selectOpt))
        window.selectElement(parser.value(selectOpt));
    if (parser.isSet(screenshot))
    {
        const QString target = parser.value(screenshot);
        auto shoot = [&window, target] {
            QTimer::singleShot(700, &window, [&window, target] {
                window.grab().save(target);
                QApplication::quit();
            });
        };
        if (parser.isSet(runFirst))
        {
            QObject::connect(&window, &anahita::app::MainWindow::simulationCompleted, &window, shoot);
            QTimer::singleShot(500, &window, [&window] { window.runSimulation(); });
        }
        else
        {
            shoot();
        }
        QTimer::singleShot(60000, &app, &QApplication::quit); // safety net
    }
    return app.exec();
}
