# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others
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

muse_create_qml_module(reprise_qml FOR reprise)

qt_add_qml_module(reprise_qml
    URI MuseScore.Reprise
    VERSION 1.0
    SOURCES
        importprogressmodel.cpp
        importprogressmodel.h
    QML_FILES
        ImportPdfOptionsDialog.qml
        ImportProgressDialog.qml
    RESOURCES
        resources/reprise-mark.svg
    IMPORTS
        TARGET muse_ui_qml
        TARGET muse_uicomponents_qml
)

fixup_qml_module_dependencies(reprise_qml)

#! NOTE: ImportProgressModel reaches into project (ICommandDispatcher, IProjectSession, PROJECT_OPEN_COMMAND) and
#! notation/engraving (instruments repository + Part/Instrument API) for the async import + Grand-Piano override.
target_link_libraries(reprise_qml PRIVATE
    muse::uicomponents_qml
    project
    notation
    engraving
)
