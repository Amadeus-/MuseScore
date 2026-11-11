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

muse_create_module(reprise)

target_sources(reprise PRIVATE
    reprisemodule.cpp
    reprisemodule.h
    reprisecommands.h

    internal/reprisecommandsregister.cpp
    internal/reprisecommandsregister.h
    internal/reprisecommandsstate.cpp
    internal/reprisecommandsstate.h
    internal/audiverisrunner.cpp
    internal/audiverisrunner.h
    internal/importpdfcontroller.cpp
    internal/importpdfcontroller.h
)

#! NOTE: Reprise's Import-PDF flow reaches into project (IProjectSession, PROJECT_OPEN_COMMAND) and into
#! notation/engraving (instruments repository + Part/Instrument API) to default imported parts to Grand Piano.
target_link_libraries(reprise PRIVATE project notation engraving)

if (MUE_BUILD_PROJECT_QML)
    add_subdirectory(qml/MuseScore/Reprise)
endif()
