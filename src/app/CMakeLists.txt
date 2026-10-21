# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    src/app/CMakeLists.txt
# Purpose: Build definition of the Qt 6 desktop application.
#
# Copyright (C) 2026 Ehsan Madadi
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set(APP_SOURCES
    AnimationExport.cpp AnimationExport.h
    Basemap.cpp Basemap.h
    CalibrationDialogs.cpp CalibrationDialogs.h
    CalibrationReport.cpp CalibrationReport.h
    ChartWidget.cpp ChartWidget.h
    DataEditor.cpp DataEditor.h
    Dialogs.cpp Dialogs.h
    EngineDownloader.cpp EngineDownloader.h
    Icons.cpp Icons.h
    ImportDialog.cpp ImportDialog.h
    LayoutDialog.cpp LayoutDialog.h
    MainWindow.cpp MainWindow.h
    MapDecorations.cpp MapDecorations.h
    MapPage.cpp MapPage.h
    MapServices.cpp MapServices.h
    MapView.cpp MapView.h
    ObjectEditor.cpp ObjectEditor.h
    OptionsDialog.cpp OptionsDialog.h
    ObservedData.cpp ObservedData.h
    Panels.cpp Panels.h
    ProjectDocument.cpp ProjectDocument.h
    PropertyPanel.cpp PropertyPanel.h
    RasterDialogs.cpp RasterDialogs.h
    RasterLayers.cpp RasterLayers.h
    ReportExport.cpp ReportExport.h
    Reports.cpp Reports.h
    RulesEditor.cpp RulesEditor.h
    ScenarioCompare.cpp ScenarioCompare.h
    ScenarioPanel.cpp ScenarioPanel.h
    SchemaWidgets.cpp SchemaWidgets.h
    SelectionTools.cpp SelectionTools.h
    SolverProcess.cpp SolverProcess.h
    SwmmEditors.cpp SwmmEditors.h
    Theme.cpp Theme.h
    include/mirab/app/PluginInterface.h
    main.cpp
    resources/mirab.qrc)

if(WIN32)
    set(MIRAB_ICO "${PROJECT_SOURCE_DIR}/packaging/windows/mirab.ico")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/mirab.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mirab.rc" @ONLY)
    list(APPEND APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/mirab.rc")
elseif(APPLE)
    set(MIRAB_ICNS "${PROJECT_SOURCE_DIR}/packaging/macos/mirab.icns")
    set_source_files_properties("${MIRAB_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND APP_SOURCES "${MIRAB_ICNS}")
endif()

qt_add_executable(Mirab WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_include_directories(Mirab PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(Mirab PRIVATE
    Qt6::Widgets Qt6::Network Qt6::PrintSupport Mirab::engine Mirab::resilience Mirab::interop Mirab::warnings)

if(APPLE)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(Mirab PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
elseif(UNIX)
    # Linux convention: lower-case program names.
    set_target_properties(Mirab PROPERTIES OUTPUT_NAME mirab)
endif()

install(TARGETS Mirab
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${MIRAB_INSTALL_BINDIR}")

# Headers for plugin authors.
install(FILES include/mirab/app/PluginInterface.h DESTINATION include/mirab/app COMPONENT sdk EXCLUDE_FROM_ALL)
