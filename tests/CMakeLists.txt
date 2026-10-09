# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
#
# File:    tests/CMakeLists.txt
# Purpose: Build definition of the unit and integration tests.
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
add_executable(anahita-tests main.cpp test_core.cpp test_engines.cpp)
target_link_libraries(anahita-tests PRIVATE
    Anahita::core Anahita::engine Anahita::resilience Anahita::interop Anahita::warnings)
target_compile_definitions(anahita-tests PRIVATE
    ANAHITA_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    ANAHITA_ENGINE_DIR="${ANAHITA_ENGINE_BUILD_DIR}")
if(MINGW)
    target_link_options(anahita-tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(TARGET anahita_engines)
    add_dependencies(anahita-tests anahita_engines)
endif()

foreach(group IN ITEMS text json inp model features results settings)
    add_test(NAME ${group} COMMAND anahita-tests "${group}:")
endforeach()
if(ANAHITA_BUILD_ENGINES)
    foreach(group IN ITEMS engines resilience interop)
        add_test(NAME ${group} COMMAND anahita-tests "${group}:")
    endforeach()
endif()
add_test(NAME cli-version COMMAND anahita-cli version)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME license-headers
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/license_headers.py" --check)
endif()

# Desktop smoke test: open the demo, run it through the solver process and
# save a screenshot, all with Qt's offscreen platform (no display needed).
if(ANAHITA_BUILD_GUI AND ANAHITA_BUILD_ENGINES)
    add_test(NAME gui-smoke
        COMMAND Anahita "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.inp" --run --select J5
                --screenshot "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.png")
    set_tests_properties(gui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
        FIXTURES_REQUIRED gui-input)
    add_test(NAME gui-smoke-setup
        COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/examples/epanet/anahita-demo.inp"
                "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.inp")
    set_tests_properties(gui-smoke-setup PROPERTIES FIXTURES_SETUP gui-input)
    add_test(NAME gui-smoke-results
        COMMAND anahita-cli results "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.anr")
    set_tests_properties(gui-smoke-results PROPERTIES DEPENDS gui-smoke)
endif()
