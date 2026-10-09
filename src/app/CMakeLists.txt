# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
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
    Basemap.cpp Basemap.h
    ChartWidget.cpp ChartWidget.h
    DataEditor.cpp DataEditor.h
    Dialogs.cpp Dialogs.h
    EngineDownloader.cpp EngineDownloader.h
    Icons.cpp Icons.h
    ImportDialog.cpp ImportDialog.h
    MainWindow.cpp MainWindow.h
    MapPage.cpp MapPage.h
    MapView.cpp MapView.h
    Panels.cpp Panels.h
    ProjectDocument.cpp ProjectDocument.h
    PropertyPanel.cpp PropertyPanel.h
    Reports.cpp Reports.h
    SolverProcess.cpp SolverProcess.h
    Theme.cpp Theme.h
    include/anahita/app/PluginInterface.h
    main.cpp
    resources/anahita.qrc)

if(WIN32)
    set(ANAHITA_ICO "${PROJECT_SOURCE_DIR}/packaging/windows/anahita.ico")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/anahita.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/anahita.rc" @ONLY)
    list(APPEND APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/anahita.rc")
elseif(APPLE)
    set(ANAHITA_ICNS "${PROJECT_SOURCE_DIR}/packaging/macos/anahita.icns")
    set_source_files_properties("${ANAHITA_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND APP_SOURCES "${ANAHITA_ICNS}")
endif()

qt_add_executable(Anahita WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_include_directories(Anahita PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(Anahita PRIVATE
    Qt6::Widgets Qt6::Network Anahita::engine Anahita::resilience Anahita::interop Anahita::warnings)

if(APPLE)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(Anahita PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
elseif(UNIX)
    # Linux convention: lower-case program names.
    set_target_properties(Anahita PROPERTIES OUTPUT_NAME anahita)
endif()

install(TARGETS Anahita
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${ANAHITA_INSTALL_BINDIR}")

# Headers for plugin authors.
install(FILES include/anahita/app/PluginInterface.h DESTINATION include/anahita/app COMPONENT sdk EXCLUDE_FROM_ALL)
