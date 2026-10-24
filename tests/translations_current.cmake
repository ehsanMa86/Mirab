# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tests/translations_current.cmake
# Purpose: Checks that the .ts files hold the texts of the current sources.
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
#
# cmake -DLUPDATE=<lupdate> -DPROJECT_SCRIPT=<GenerateLUpdateProject.cmake>
#       -DPROJECTS=<a.cmake>|<b.cmake> -DCHECK=<mirab-i18n-check>
#       -DWORK_DIR=<dir> -DTS_DIR=<translations> -P translations_current.cmake
#
# Refreshes copies of the .ts files in WORK_DIR with lupdate, from the lupdate
# project Qt generates for the Mirab target (the first of PROJECTS that
# exists) and with the options of the update_translations target, then
# compares the files with their copies. The files in TS_DIR are not changed.

foreach(var LUPDATE PROJECT_SCRIPT PROJECTS CHECK WORK_DIR TS_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is not set")
    endif()
endforeach()

string(REPLACE "|" ";" candidates "${PROJECTS}")
set(project "")
foreach(candidate IN LISTS candidates)
    if(EXISTS "${candidate}")
        set(project "${candidate}")
        break()
    endif()
endforeach()
if(project STREQUAL "")
    message(FATAL_ERROR "The lupdate project of the Mirab target was not found (${PROJECTS}); "
                        "configure the build directory again.")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DIN_FILE=${project}" "-DOUT_FILE=${WORK_DIR}/project.json"
            -P "${PROJECT_SCRIPT}"
    RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "Writing the lupdate project failed (${status})")
endif()

file(GLOB ts_files "${TS_DIR}/*.ts")
set(copies "")
foreach(ts IN LISTS ts_files)
    file(COPY "${ts}" DESTINATION "${WORK_DIR}")
    get_filename_component(name "${ts}" NAME)
    list(APPEND copies "${WORK_DIR}/${name}")
endforeach()
execute_process(
    COMMAND "${LUPDATE}" -silent -project "${WORK_DIR}/project.json" -no-obsolete -locations none
            -ts ${copies}
    RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "lupdate failed (${status})")
endif()

execute_process(COMMAND "${CHECK}" current "${WORK_DIR}" ${ts_files} RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "The translation files do not match the texts of the program.")
endif()
