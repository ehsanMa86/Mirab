# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
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
add_executable(mirab-tests main.cpp test_core.cpp test_engines.cpp test_import.cpp test_crs.cpp
    test_swmm_data.cpp test_swmm_objects.cpp test_raster.cpp test_calibration.cpp
    test_deliverables.cpp test_scripting.cpp test_network.cpp test_scenarios.cpp test_linkage.cpp)
target_link_libraries(mirab-tests PRIVATE
    Mirab::core Mirab::engine Mirab::resilience Mirab::interop Mirab::warnings)
target_compile_definitions(mirab-tests PRIVATE
    MIRAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MIRAB_ENGINE_DIR="${MIRAB_ENGINE_BUILD_DIR}")
if(MINGW)
    target_link_options(mirab-tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(TARGET mirab_engines)
    add_dependencies(mirab-tests mirab_engines)
endif()

foreach(group IN ITEMS text json inp model features results settings hash crs import swmm
                     lid lidusage transects hydrographs rules raster elevation
                     calibration gif layout summary modeljson network geometry query editing
                     scenarios linkage)
    add_test(NAME ${group} COMMAND mirab-tests "${group}:")
endforeach()
if(MIRAB_BUILD_ENGINES)
    foreach(group IN ITEMS engines resilience interop calibration-run linkage-run)
        add_test(NAME ${group} COMMAND mirab-tests "${group}:")
    endforeach()
endif()
add_test(NAME cli-version COMMAND mirab-cli version)
# Dry-weather flow of the example sewer from the example water network, on copies.
set(link_dir "${CMAKE_CURRENT_BINARY_DIR}/cli-link")
add_test(NAME cli-link-setup
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples" "${link_dir}")
set_tests_properties(cli-link-setup PROPERTIES FIXTURES_SETUP link-input)
add_test(NAME cli-link
    COMMAND mirab-cli link dwf "${link_dir}/swmm/mirab-longbeach-sewer.inp" --return 0.75
            --out "${link_dir}/swmm/linked.inp")
set_tests_properties(cli-link PROPERTIES FIXTURES_REQUIRED link-input
    PASS_REGULAR_EXPRESSION "8 demand node[(]s[)], 0 not assigned")
if(MIRAB_BUILD_ENGINES)
    # Calibration through parallel engine processes, on a copy of the example.
    set(_calib "${CMAKE_CURRENT_BINARY_DIR}/calibrate")
    file(MAKE_DIRECTORY "${_calib}")
    add_test(NAME cli-calibrate-setup
        COMMAND ${CMAKE_COMMAND} -E copy
                "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo.inp"
                "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo.inp.mirab.json"
                "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo-pressure.csv"
                "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo-flow.dat"
                "${_calib}/")
    set_tests_properties(cli-calibrate-setup PROPERTIES FIXTURES_SETUP calib-input)
    add_test(NAME cli-calibrate
        COMMAND mirab-cli calibrate "${_calib}/mirab-demo.inp" --budget 10 --jobs 3
                --out-dir "${_calib}/out")
    set_tests_properties(cli-calibrate PROPERTIES
        FIXTURES_REQUIRED calib-input
        PASS_REGULAR_EXPRESSION "Pipe roughness = 0[.][89]")
    # Scenarios of the EPANET demo: list, export, and a batch run in parallel processes.
    set(scenario_dir "${CMAKE_CURRENT_BINARY_DIR}/cli-scenarios")
    add_test(NAME cli-scenarios-setup
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples/epanet" "${scenario_dir}")
    set_tests_properties(cli-scenarios-setup PROPERTIES FIXTURES_SETUP scenario-input)
    add_test(NAME cli-scenarios-list COMMAND mirab-cli scenarios list "${scenario_dir}/mirab-demo.inp")
    set_tests_properties(cli-scenarios-list PROPERTIES FIXTURES_REQUIRED scenario-input
        PASS_REGULAR_EXPRESSION "Growth with upsizing  [(]based on Demand growth[)]")
    add_test(NAME cli-scenarios-export
        COMMAND mirab-cli scenarios export "${scenario_dir}/mirab-demo.inp" --scenario "Pipe upsizing"
                --out "${scenario_dir}/upsized.inp")
    set_tests_properties(cli-scenarios-export PROPERTIES FIXTURES_REQUIRED scenario-input)
    add_test(NAME cli-scenarios-batch
        COMMAND mirab-cli batch "${scenario_dir}/mirab-demo.inp" --all --jobs 2)
    set_tests_properties(cli-scenarios-batch PROPERTIES FIXTURES_REQUIRED scenario-input
        PASS_REGULAR_EXPRESSION "[[]4/4[]]" FAIL_REGULAR_EXPRESSION "failed" TIMEOUT 120)
    # The analyses solve the scenario's model: the base model has about
    # 120 LPS available at J5, the grown demand about 89.
    add_test(NAME cli-scenarios-fireflow
        COMMAND mirab-cli resilience fireflow "${scenario_dir}/mirab-demo.inp" --flow 30 --nodes J5
                --scenario "Demand growth" --json)
    set_tests_properties(cli-scenarios-fireflow PROPERTIES FIXTURES_REQUIRED scenario-input
        PASS_REGULAR_EXPRESSION "\"availableFlow\": ?8[0-9][.]"
        FAIL_REGULAR_EXPRESSION "\"availableFlow\": ?1[0-9][0-9][.]")
    # A results period out of range is refused instead of exported as zeros.
    add_test(NAME cli-scenarios-export-period
        COMMAND mirab-cli export geojson "${scenario_dir}/mirab-demo.inp"
                --results "${scenario_dir}/mirab-demo.anr" --period 999
                --out "${scenario_dir}/period.geojson")
    set_tests_properties(cli-scenarios-export-period PROPERTIES FIXTURES_REQUIRED scenario-input
        DEPENDS cli-scenarios-batch PASS_REGULAR_EXPRESSION "period must be between 0 and 24")
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME license-headers
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/license_headers.py" --check)
    # Solver protocol as the desktop application uses it: live frames,
    # pause, resume and cancel over standard input.
    if(MIRAB_BUILD_ENGINES)
        add_test(NAME cli-protocol
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli_protocol.py" $<TARGET_FILE:mirab-cli>
                    "${PROJECT_SOURCE_DIR}/examples" "${CMAKE_CURRENT_BINARY_DIR}/cli-protocol")
        set_tests_properties(cli-protocol PROPERTIES TIMEOUT 180)
    endif()
    # The Python package (integrations/python) against this build's mirab-cli,
    # when pytest is installed. Nothing in the build itself needs Python.
    execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import pytest"
        RESULT_VARIABLE _mirab_pytest OUTPUT_QUIET ERROR_QUIET)
    if(_mirab_pytest EQUAL 0)
        add_test(NAME python-package
            COMMAND "${Python3_EXECUTABLE}" -m pytest -q -p no:cacheprovider
                    "${PROJECT_SOURCE_DIR}/integrations/python/tests")
        set_tests_properties(python-package PROPERTIES
            ENVIRONMENT "MIRAB_CLI=$<TARGET_FILE:mirab-cli>;PYTHONPATH=${PROJECT_SOURCE_DIR}/integrations/python/src;PYTHONDONTWRITEBYTECODE=1;MIRAB_EXAMPLES=${PROJECT_SOURCE_DIR}/examples"
            TIMEOUT 300)
    else()
        message(STATUS "pytest not found: the python-package test is skipped")
    endif()
endif()

# Desktop smoke test: open the demo, run it through the solver process and
# save a screenshot, all with Qt's offscreen platform (no display needed).
if(MIRAB_BUILD_GUI AND MIRAB_BUILD_ENGINES)
    add_test(NAME gui-smoke
        COMMAND Mirab "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.inp" --run --select J5
                --screenshot "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.png")
    set_tests_properties(gui-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
        FIXTURES_REQUIRED gui-input)
    add_test(NAME gui-smoke-setup
        COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo.inp"
                "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.inp")
    set_tests_properties(gui-smoke-setup PROPERTIES FIXTURES_SETUP gui-input)
    add_test(NAME gui-smoke-results
        COMMAND mirab-cli results "${CMAKE_CURRENT_BINARY_DIR}/gui-smoke.anr")
    set_tests_properties(gui-smoke-results PROPERTIES DEPENDS gui-smoke)

    # Deliverables: print layout, report document and animation written from
    # the command line (its own copy of the demo, so runs do not collide).
    add_test(NAME gui-export
        COMMAND Mirab "${CMAKE_CURRENT_BINARY_DIR}/gui-export.inp" --run
                --export-layout "${CMAKE_CURRENT_BINARY_DIR}/gui-export-layout.pdf"
                --export-report "${CMAKE_CURRENT_BINARY_DIR}/gui-export-report.html"
                --export-animation "${CMAKE_CURRENT_BINARY_DIR}/gui-export.gif")
    set_tests_properties(gui-export PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180
        FIXTURES_REQUIRED gui-export-input)
    add_test(NAME gui-export-setup
        COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo.inp"
                "${CMAKE_CURRENT_BINARY_DIR}/gui-export.inp")
    set_tests_properties(gui-export-setup PROPERTIES FIXTURES_SETUP gui-export-input)
endif()

# Selection tools offscreen: rectangle selection with the multi-edit panel, a
# downstream trace and the select by query dialog.
if(MIRAB_BUILD_GUI)
    add_test(NAME gui-select-region
        COMMAND Mirab "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo.inp" --select-region 1500,500,3500,2500
                --delay 300 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/gui-select-region.png")
    add_test(NAME gui-select-trace
        COMMAND Mirab "${PROJECT_SOURCE_DIR}/examples/swmm/mirab-urban.inp" --select J2 --trace downstream
                --delay 300 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/gui-select-trace.png")
    add_test(NAME gui-select-query
        COMMAND Mirab "${PROJECT_SOURCE_DIR}/examples/epanet/mirab-demo.inp" --open-tool select-query
                --select-query "Diameter <= 150 AND Length >= 300"
                --delay 300 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/gui-select-query.png")
    set_tests_properties(gui-select-region gui-select-trace gui-select-query PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()

# Object editors: open each one offscreen on a model that uses them.
if(MIRAB_BUILD_GUI)
    foreach(target IN ITEMS LID_CONTROLS:BioCell LID_USAGE:S1 TRANSECTS:Main HYDROGRAPHS:UH1 CONTROLS:)
        string(REGEX REPLACE "[:_]+" "-" name "gui-editor-${target}")
        string(REGEX REPLACE "-$" "" name "${name}")
        string(TOLOWER "${name}" name)
        add_test(NAME ${name}
            COMMAND Mirab "${CMAKE_CURRENT_SOURCE_DIR}/data/swmm-objects.inp" --show-data ${target}
                    --delay 300 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/${name}.png")
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    endforeach()
endif()

# Translations: the list of library strings lupdate reads, the .ts files
# (well formed, no obsolete messages, placeholders kept) and the interface
# started in each language.
if(MIRAB_BUILD_GUI)
    add_executable(mirab-i18n-check i18n_check.cpp)
    target_link_libraries(mirab-i18n-check PRIVATE Qt6::Core Mirab::core Mirab::engine Mirab::warnings)
    target_compile_definitions(mirab-i18n-check PRIVATE MIRAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    set(_core_strings "${PROJECT_SOURCE_DIR}/src/app/CoreStrings.cpp")
    add_test(NAME i18n-core-strings COMMAND mirab-i18n-check core-strings "${_core_strings}")
    add_custom_target(update_core_strings
        COMMAND mirab-i18n-check core-strings "${_core_strings}" --write
        COMMENT "Updating the list of library strings in src/app/CoreStrings.cpp")
    file(GLOB _ts_files "${PROJECT_SOURCE_DIR}/translations/*.ts")
    add_test(NAME i18n-ts-files COMMAND mirab-i18n-check ts ${_ts_files})
    # The .ts files hold every text of the program and no other: update_translations was run
    # after the sources changed. lupdate refreshes copies in the build directory.
    set(_lupdate_script "${Qt6LinguistTools_DIR}/GenerateLUpdateProject.cmake")
    get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(TARGET Qt6::lupdate AND TARGET Mirab AND EXISTS "${_lupdate_script}" AND NOT _multi_config)
        # Qt writes the project next to the top-level or the Mirab build directory.
        set(_lupdate_projects "${PROJECT_BINARY_DIR}/.lupdate/Mirab_lupdate_project.cmake"
                              "${PROJECT_BINARY_DIR}/src/app/.lupdate/Mirab_lupdate_project.cmake")
        string(REPLACE ";" "|" _lupdate_projects "${_lupdate_projects}")
        add_test(NAME i18n-ts-current
            COMMAND "${CMAKE_COMMAND}"
                    "-DLUPDATE=$<TARGET_FILE:Qt6::lupdate>"
                    "-DPROJECT_SCRIPT=${_lupdate_script}"
                    "-DPROJECTS=${_lupdate_projects}"
                    "-DCHECK=$<TARGET_FILE:mirab-i18n-check>"
                    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/i18n-ts-current"
                    "-DTS_DIR=${PROJECT_SOURCE_DIR}/translations"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/translations_current.cmake")
    endif()
    # The interface in each language (fails when its translation is not in the program).
    if(TARGET Qt6::lrelease)
        foreach(lang IN ITEMS fa ar he es pt_BR fr it de zh_CN zh_TW)
            add_test(NAME gui-language-${lang}
                COMMAND Mirab "${PROJECT_SOURCE_DIR}/examples/swmm/mirab-urban.inp" --language ${lang}
                        --select J2 --delay 300 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/gui-language-${lang}.png")
            set_tests_properties(gui-language-${lang} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
        endforeach()
    endif()
endif()
