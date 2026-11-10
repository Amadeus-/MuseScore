# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2024 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

if (APPLE)
    # Required to prevent issues with incremental builds of MuseScoreQuickLookPreviewExtension
    # (https://github.com/musescore/MuseScore/pull/33554)
    cmake_minimum_required(VERSION 3.31)
else()
    # Required to use C++20 modules in the future.
    cmake_minimum_required(VERSION 3.28)
endif()

set(MUSE_FRAMEWORK_PATH "${CMAKE_CURRENT_LIST_DIR}/muse" CACHE PATH "Path to muse framework")
set(MUSE_FRAMEWORK_SRC_PATH ${MUSE_FRAMEWORK_PATH}/framework)

include(${MUSE_FRAMEWORK_PATH}/buildscripts/cmake/SetupMacOSDeploymentTarget.cmake)

project(MuseScoreStudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/buildscripts
    ${CMAKE_CURRENT_LIST_DIR}/buildscripts/cmake
    ${MUSE_FRAMEWORK_SRC_PATH}/cmake
    ${MUSE_FRAMEWORK_PATH}/buildscripts/cmake
    ${CMAKE_MODULE_PATH}
    )

###########################################
# Check compiler version
###########################################
message(STATUS "C:   ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "CXX: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

include(CheckCompilerVersion)
check_compile_version("Clang" 20 "https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment")
check_compile_version("AppleClang" 21 "https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment")
check_compile_version("GCC" 14 "https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment")
check_compile_version("MSVC" 19.40 "https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment")

###########################################
# Setup option and build settings
###########################################
include(GetPaths)

set(MUSESCORE_BUILD_CONFIGURATION "app" CACHE STRING "Build configuration")
# Possible MUSESCORE_BUILD_CONFIGURATION values:
# - app             - for desktop app
# - app-portable    - for desktop portable app (Windows build for PortableApps.com)
# - app-web         - for web app (experimental for internal use)
# - vtest           - for visual tests (for CI)
# - utest           - for unit tests (for CI)

set(MUSE_APP_BUILD_MODE "dev" CACHE STRING "Build mode")
# Possible MUSE_APP_BUILD_MODE values:
# - dev     - for development/nightly builds
# - testing - for testing versions (alpha, beta, RC)
# - release - for stable release builds

set(MUSESCORE_REVISION "" CACHE STRING "Build revision")

option(MUSE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2 "Use KDDockWidgets v2" ON)

# Reprise: no update server and no telemetry. Force the update module OFF for every
# build of this fork (the muse submodule defaults it ON; FORCE overrides any stale
# cache entry too). OFF swaps in the update stub, a supported config also used by
# WIN_PORTABLE, so the app never checks for or offers an update. Set before the muse
# option() declarations so it wins (option() will not change an already-set value).
set(MUSE_MODULE_UPDATE OFF CACHE BOOL "Reprise: disable update module (no phone-home)" FORCE)

# Reprise slim build: disable modules this fork does not ship, to cut the deploy footprint.
# Same mechanism as MUSE_MODULE_UPDATE above -- FORCE the cache value OFF before the option()
# declarations so every later option() (the muse MuseDeclareOptions macros AND the MUE_BUILD_*
# options below) respects it. KEEP ON (core to Reprise): musicxml import, playback/audio/mpe +
# soundfont, palette/propertiespanel/instrumentsscene, audioexport/imagesexport, print, opengl32sw.
# -- muse framework modules --
set(MUSE_MODULE_MUSESAMPLER OFF CACHE BOOL "Reprise slim" FORCE)
set(MUSE_MODULE_AUDIOPLUGINS OFF CACHE BOOL "Reprise slim" FORCE)
set(MUSE_MODULE_TESTFLOW OFF CACHE BOOL "Reprise slim" FORCE)
# NOT disabled, left ON on purpose:
#  * DIAGNOSTICS   - has no stub and appshell_qml imports muse_diagnostics_qml unconditionally,
#                    so OFF breaks the configure. The heavy crashpad_handler is separately gated
#                    on MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT (OFF for this fork), so it is cheap.
#  * CLOUD / LEARN / TOURS / EXTENSIONS (muse) and MUSESOUNDS (app) - these DO stub when disabled,
#                    but the desktop appshell Home page consumes their services and SIGSEGVs at
#                    startup against the stubs (upstream only ships these stubs in the web build).
#                    Keeping them ON is required for a launchable build; they are small UI modules.
# -- MuseScore app modules --
# BRAILLE left ON: notationscene's NotationView.qml imports MuseScore.Braille unconditionally,
# and the braille stub's QML module does not register that URI, so the main window QML fails to
# load ("module MuseScore.Braille is not installed") with braille off. The module is tiny.
# -- unused import/export formats --
set(MUE_BUILD_IMPEXP_GUITARPRO_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_MNX_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_MEI_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_OVE_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_CAPELLA_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_BWW_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_TABLEDIT_MODULE OFF CACHE BOOL "Reprise slim" FORCE)
set(MUE_BUILD_IMPEXP_VIDEOEXPORT_MODULE OFF CACHE BOOL "Reprise slim" FORCE)

include(${MUSE_FRAMEWORK_SRC_PATH}/cmake/MuseDeclareOptions.cmake)

# Modules (alphabetical order please)
option(MUE_BUILD_APPSHELL_MODULE "Build appshell module" ON)
option(MUE_BUILD_APPSHELL_QML "Build appshell QML" ${MUE_BUILD_APPSHELL_MODULE})

option(MUE_BUILD_BRAILLE_MODULE "Build braille module" ON)
option(MUE_BUILD_BRAILLE_QML "Build braille QML" ${MUE_BUILD_BRAILLE_MODULE})
option(MUE_BUILD_BRAILLE_TESTS "Build braille tests" ${MUE_BUILD_BRAILLE_MODULE})

option(MUE_BUILD_CONVERTER_MODULE "Build converter module" ON)
option(MUE_BUILD_CONVERTER_TESTS "Build converter tests" ${MUE_BUILD_CONVERTER_MODULE})

option(MUE_BUILD_ENGRAVING_QML "Build engraving QML" ON)
option(MUE_BUILD_ENGRAVING_TESTS "Build engraving tests" ON)
option(MUE_BUILD_ENGRAVING_DEVTOOLS "Build engraving devtools" ON)
option(MUE_BUILD_ENGRAVING_PLAYBACK "Build engraving playback" ON)

# IMPORT EXPORT MODULES
option(MUE_BUILD_IMPEXP_BB_MODULE "Build importexport bb module" ON)
option(MUE_BUILD_IMPEXP_BWW_MODULE "Build importexport bww module" ON)
option(MUE_BUILD_IMPEXP_CAPELLA_MODULE "Build importexport capella module" ON)
option(MUE_BUILD_IMPEXP_MIDI_MODULE "Build importexport midi module" ON)
option(MUE_BUILD_IMPEXP_MLSCORE_MODULE "Build importexport mlscore module" OFF)
option(MUE_BUILD_IMPEXP_MNX_MODULE "Build importexport mnx module" ON)
option(MUE_BUILD_IMPEXP_MUSEDATA_MODULE "Build importexport musedata module" ON)
option(MUE_BUILD_IMPEXP_MUSICXML_MODULE "Build importexport musicxml module" ON)
option(MUE_BUILD_IMPEXP_OVE_MODULE "Build importexport ove module" ON)
option(MUE_BUILD_IMPEXP_AUDIOEXPORT_MODULE "Build importexport audioexport module" ON)
option(MUE_BUILD_IMPEXP_IMAGESEXPORT_MODULE "Build importexport imagesexport module" ON)
option(MUE_BUILD_IMPEXP_GUITARPRO_MODULE "Build importexport guitarpro module" ON)
option(MUE_BUILD_IMPEXP_MEI_MODULE "Build importexport mei module" ON)
option(MUE_BUILD_IMPEXP_VIDEOEXPORT_MODULE "Build importexport videoexport module" ON)
option(MUE_BUILD_IMPEXP_TABLEDIT_MODULE "Build importexport tabledit module" ON)
option(MUE_BUILD_IMPEXP_LYRICS_MODULE "Build importexport lyrics module" ON)

option(MUE_BUILD_IMPORTEXPORT_TESTS "Build importexport tests" ON)

option(MUE_BUILD_PROPERTIESPANEL_MODULE "Build propertiespanel module" ON)
option(MUE_BUILD_PROPERTIESPANEL_QML "Build propertiespanel QML" ${MUE_BUILD_PROPERTIESPANEL_MODULE})

option(MUE_BUILD_INSTRUMENTSSCENE_MODULE "Build instruments scene module" ON)
option(MUE_BUILD_INSTRUMENTSSCENE_QML "Build instruments scene QML" ${MUE_BUILD_INSTRUMENTSSCENE_MODULE})

option(MUE_BUILD_MUSESOUNDS_MODULE "Build MuseSounds module" ON)
option(MUE_BUILD_MUSESOUNDS_QML "Build musesounds QML" ${MUE_BUILD_MUSESOUNDS_MODULE})
option(MUE_BUILD_MUSESOUNDS_TESTS "Build musesounds tests" ${MUE_BUILD_MUSESOUNDS_MODULE})

option(MUE_BUILD_NOTATION_MODULE "Build notation module" ON)
option(MUE_BUILD_NOTATION_TESTS "Build notation tests" ${MUE_BUILD_NOTATION_MODULE})

option(MUE_BUILD_NOTATIONSCENE_MODULE "Build notation scene module" ON)
option(MUE_BUILD_NOTATIONSCENE_QML "Build notation scene QML" ${MUE_BUILD_NOTATIONSCENE_MODULE})
option(MUE_BUILD_NOTATIONSCENE_TESTS "Build notation scene tests" ${MUE_BUILD_NOTATIONSCENE_MODULE})

option(MUE_BUILD_PALETTE_MODULE "Build palette module" ON)
option(MUE_BUILD_PALETTE_QML "Build palette QML" ${MUE_BUILD_PALETTE_MODULE})

option(MUE_BUILD_PLAYBACK_MODULE "Build playback module" ON)
option(MUE_BUILD_PLAYBACK_QML "Build playback QML" ${MUE_BUILD_PLAYBACK_MODULE})
option(MUE_BUILD_PLAYBACK_TESTS "Build playback tests" ${MUE_BUILD_PLAYBACK_MODULE})

option(MUE_BUILD_PREFERENCES_MODULE "Build preferences module" ON)
option(MUE_BUILD_PRINT_MODULE "Build print module" ON)

option(MUE_BUILD_PROJECT_MODULE "Build project module" ON)
option(MUE_BUILD_PROJECT_QML "Build project QML" ${MUE_BUILD_PROJECT_MODULE})
option(MUE_BUILD_PROJECT_TESTS "Build project tests" ${MUE_BUILD_PROJECT_MODULE})

option(MUE_BUILD_MACOS_INTEGRATION "Build macOS integration module" ON)

# === Setup ===
option(MUE_DOWNLOAD_SOUNDFONT "Download the latest soundfont version as part of the build process" ON)

# === Pack ===
option(MUE_RUN_LRELEASE "Generate .qm files" ON)
option(MUE_INSTALL_SOUNDFONT "Install sound font" ON)
option(MUE_RUN_WINDEPLOYQT "Run windeployqt" OFF)

# === Tests ===
set(MUE_VTEST_MSCORE_REF_BIN "${CMAKE_CURRENT_LIST_DIR}/../MU_ORIGIN/MuseScore/build.debug/install/${INSTALL_SUBDIR}/mscore" CACHE PATH "Path to mscore ref bin")
set(APP_WORKSPACE_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/src/app/configs/workspaces.cfg")
set(APP_BUILTIN_WORKSPACES_DIR "${CMAKE_CURRENT_LIST_DIR}/share/workspaces")

# === Compile ===
option(MUSE_GENERATE_COMPILE_COMMANDS "Generate compile commands" OFF)
option(MUSE_COMPILE_USE_UNITY "Use unity build" ON)
option(MUSE_COMPILE_USE_COMPILER_CACHE "Try to use compiler cache: tries ccache, sccache, buildcache in that order. Use COMPILER_CACHE_PROGRAM to specify a specific compiler cache program." ON)
option(MUSE_COMPILE_USE_SHARED_LIBS_IN_DEBUG "Build shared libs if possible in debug" OFF)

# === System libraries ===
# Important for the maintainers of Linux distributions
option(MSS_USE_SYSTEM_MNXDOM "Try use system mnxdom" OFF)

# === Debug ===
option(MUE_ENABLE_ENGRAVING_RENDER_DEBUG "Enable rendering debug" OFF)
option(MUE_ENABLE_ENGRAVING_LD_ACCESS "Enable diagnostic engraving check layout data access" OFF)
option(MUE_ENABLE_ENGRAVING_LD_PASSES "Enable engraving layout by passes" OFF)

###########################################
# Setup Configure
###########################################

# default
set(QT_ADD_LINGUISTTOOLS ON)
set(QT_ADD_CONCURRENT ON)
set(QT_ADD_WEBSOCKET OFF)
set(QT_QPROCESS_SUPPORTED ON)
set(QT_CONCURRENT_SUPPORTED ON)
set(QT_NO_PRIVATE_MODULE_WARNING ON)
set(MUSE_MODULE_MULTIWINDOWS_SINGLEPROC_MODE ON)
set(MUSE_MODULE_SHORTCUTS_V2 ON)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/SetupConfigure.local.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/SetupConfigure.local.cmake)
else()
    include(SetupConfigure)
endif()

###########################################
# Setup compiler and build environment
###########################################

include(SetupBuildEnvironment)
include(GetPlatformInfo)

if (MUSE_COMPILE_USE_COMPILER_CACHE)
    include(SetupCompilerCache)
endif(MUSE_COMPILE_USE_COMPILER_CACHE)


###########################################
# Setup external dependencies
###########################################
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_BASE_DIR ${PROJECT_BINARY_DIR}/_deps)

include(SetupQt6)

if (MUE_DOWNLOAD_SOUNDFONT)
    include(DownloadSoundFont)
endif(MUE_DOWNLOAD_SOUNDFONT)

set(FETCHCONTENT_BASE_DIR ${PROJECT_BINARY_DIR}/_deps)

include(SetupDependencies)

###########################################
# Add source tree
###########################################
if (MUSE_ENABLE_UNIT_TESTS)
    enable_testing()
    message(STATUS "Enabled testing")
endif()

add_subdirectory(share)
add_subdirectory(${MUSE_FRAMEWORK_SRC_PATH})
add_subdirectory(src)


###########################################
# Setup Packaging
###########################################

if (OS_IS_LIN)
    include(packaging/Linux+BSD/SetupAppImagePackaging)
endif(OS_IS_LIN)

if (OS_IS_WIN)
    include(packaging/Windows/SetupWindowsPackaging)
endif(OS_IS_WIN)

###########################################
# Add VTest
###########################################
add_subdirectory(vtest)

message(STATUS "Generate compile commands: ${MUSE_GENERATE_COMPILE_COMMANDS}")
if (MUSE_GENERATE_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --preset=compile_commands -DMUSE_GENERATE_COMPILE_COMMANDS=OFF
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/build/compile_commands/compile_commands.json
        DESTINATION ${CMAKE_CURRENT_SOURCE_DIR})
endif()
