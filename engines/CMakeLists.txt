# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
#
# File:    engines/CMakeLists.txt
# Purpose: Fetches the upstream EPANET, EPANET-MSX and SWMM sources at pinned commits and builds them as engine libraries.
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
# Each engine is compiled from its unmodified upstream sources into a shared
# library with a unique name, so several versions of the same solver can be
# shipped side by side and loaded at run time by Anahita. The upstream
# copyright notices and licenses travel with the sources and are reproduced in
# THIRD_PARTY_NOTICES.md.
#
# To build from local checkouts (for offline builds or engine development),
# set FETCHCONTENT_SOURCE_DIR_<NAME> to the checkout, for example
#   -DFETCHCONTENT_SOURCE_DIR_ANAHITA_UPSTREAM_OWA_EPANET=/path/to/EPANET

include(FetchContent)

macro(anahita_fetch name url sha256)
    FetchContent_Declare(${name}
        URL      ${url}
        URL_HASH SHA256=${sha256}
        SOURCE_SUBDIR do-not-add
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(${name})
endmacro()

# USEPA/epanet-engine (EPANET 2.2, public domain and MIT)
anahita_fetch(anahita_upstream_epa_epanet
    https://github.com/USEPA/epanet-engine/archive/6aff01d7d9be7c9eee64246c6588b486776b3a75.tar.gz
    dc5ad1ffce064dff466200e9d017dd216873231b5e735885ed6bb116f1e19f38)

# OpenWaterAnalytics/EPANET v2.3.5 (MIT)
anahita_fetch(anahita_upstream_owa_epanet
    https://github.com/OpenWaterAnalytics/EPANET/archive/444725fbf16ab28b0470ef0ed281dc4849d49583.tar.gz
    9bd03df20d53b0fe26be755be40b9219a95843f4a92c8d878dcc42d020e857e2)

# USEPA/EPANETMSX 2.0 (MIT). A one-word typo in the non-Windows code path is
# corrected after download; see patches/msx-fix-compiler-typo.cmake.
FetchContent_Declare(anahita_upstream_epanet_msx
    URL      https://github.com/USEPA/EPANETMSX/archive/84c057e0a62b7fb3a89ef555a86fa0e3e0ed87ad.tar.gz
    URL_HASH SHA256=5e44e9cc01e8088ea4d78a1a13adbbe79c88f9e0819aeee443c4145748600e97
    PATCH_COMMAND ${CMAKE_COMMAND} -DFILE=<SOURCE_DIR>/Src/msxcompiler.c
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/patches/msx-fix-compiler-typo.cmake
    SOURCE_SUBDIR do-not-add
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(anahita_upstream_epanet_msx)

# USEPA/Stormwater-Management-Model v5.2.4 (public domain)
anahita_fetch(anahita_upstream_epa_swmm
    https://github.com/USEPA/Stormwater-Management-Model/archive/7952ca837988b1c32f791812eccc9fd64547e093.tar.gz
    0c882c315b64d5236b3569f891e9ad71421bc5b7d92a5710ceb1ae3b7f34c6cf)

set(EPA_EPANET_DIR "${anahita_upstream_epa_epanet_SOURCE_DIR}")
set(OWA_EPANET_DIR "${anahita_upstream_owa_epanet_SOURCE_DIR}")
set(EPANET_MSX_DIR "${anahita_upstream_epanet_msx_SOURCE_DIR}")
set(EPA_SWMM_DIR   "${anahita_upstream_epa_swmm_SOURCE_DIR}")

find_library(ANAHITA_LIBM m)

# Common settings for every engine library.
function(anahita_engine_library target)
    cmake_parse_arguments(E "" "" "SOURCES;INCLUDES;DEFINES;LINK" ${ARGN})
    add_library(${target} SHARED ${E_SOURCES})
    target_include_directories(${target} PRIVATE ${E_INCLUDES})
    target_compile_definitions(${target} PRIVATE ${E_DEFINES})
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        IMPORT_PREFIX ""
        C_STANDARD 99
        C_VISIBILITY_PRESET default
        RUNTIME_OUTPUT_DIRECTORY "${ANAHITA_ENGINE_BUILD_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${ANAHITA_ENGINE_BUILD_DIR}")
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            INSTALL_NAME_DIR "@rpath"
            BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES
            BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
    endif()
    if(MSVC)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target} PRIVATE /W0)
    else()
        # -fcommon: some engines rely on tentative definitions of globals.
        target_compile_options(${target} PRIVATE -w -fcommon)
    endif()
    if(MINGW)
        # Engines must not depend on MinGW runtime DLLs.
        target_link_options(${target} PRIVATE -static -static-libgcc)
    endif()
    if(ANAHITA_LIBM)
        target_link_libraries(${target} PRIVATE ${ANAHITA_LIBM})
    endif()
    if(E_LINK)
        target_link_libraries(${target} PRIVATE ${E_LINK})
    endif()
    install(TARGETS ${target}
        RUNTIME DESTINATION "${ANAHITA_INSTALL_ENGINEDIR}"
        LIBRARY DESTINATION "${ANAHITA_INSTALL_ENGINEDIR}")
endfunction()

# --- EPA EPANET 2.2 ---------------------------------------------------------
file(GLOB EPA_EPANET_SOURCES "${EPA_EPANET_DIR}/src/solver/*.c")
anahita_engine_library(anahita_epanet22_epa
    SOURCES  ${EPA_EPANET_SOURCES}
    INCLUDES "${EPA_EPANET_DIR}/src/solver/include" "${EPA_EPANET_DIR}/src/solver"
    DEFINES  epanet2_EXPORTS)

# --- OWA EPANET 2.3 ---------------------------------------------------------
file(GLOB OWA_EPANET_SOURCES "${OWA_EPANET_DIR}/src/*.c" "${OWA_EPANET_DIR}/src/util/*.c")
anahita_engine_library(anahita_epanet23_owa
    SOURCES  ${OWA_EPANET_SOURCES}
    INCLUDES "${OWA_EPANET_DIR}/include" "${OWA_EPANET_DIR}/src" "${OWA_EPANET_DIR}/src/util"
    DEFINES  epanet2_EXPORTS)

# --- EPANET-MSX 2.0, built once against each EPANET engine ------------------
file(GLOB EPANET_MSX_SOURCES "${EPANET_MSX_DIR}/Src/*.c")
list(FILTER EPANET_MSX_SOURCES EXCLUDE REGEX "msxmain\.c$")
set(MSX_INCLUDES "${EPANET_MSX_DIR}/Include" "${EPANET_MSX_DIR}/Src")
if(APPLE)
    list(APPEND MSX_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/compat/apple")
endif()
foreach(base IN ITEMS epanet22_epa epanet23_owa)
    string(REPLACE "epanet" "" suffix "${base}")
    anahita_engine_library(anahita_msx20_${suffix}
        SOURCES  ${EPANET_MSX_SOURCES}
        INCLUDES ${MSX_INCLUDES}
        DEFINES  epanetmsx_EXPORTS
        LINK     anahita_${base})
endforeach()

# --- EPA SWMM 5.2 -----------------------------------------------------------
file(GLOB EPA_SWMM_SOURCES "${EPA_SWMM_DIR}/src/solver/*.c")
anahita_engine_library(anahita_swmm52_epa
    SOURCES  ${EPA_SWMM_SOURCES}
    INCLUDES "${EPA_SWMM_DIR}/src/solver/include" "${EPA_SWMM_DIR}/src/solver"
    DEFINES  swmm5_EXPORTS)

add_custom_target(anahita_engines DEPENDS
    anahita_epanet22_epa anahita_epanet23_owa
    anahita_msx20_22_epa anahita_msx20_23_owa
    anahita_swmm52_epa)
