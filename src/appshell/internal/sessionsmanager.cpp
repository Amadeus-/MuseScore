/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "sessionsmanager.h"

#include "project/inotationproject.h"
#include "project/projectcommands.h"

using namespace mu::appshell;
using namespace muse;

void SessionsManager::init()
{
    update();

    projectSession()->projectChanged().onNotify(this, [this]() {
        update();

        if (project::INotationProjectPtr project = projectSession()->project()) {
            project->pathChanged().onNotify(this, [this]() {
                update();
            });
        }
    });
}

void SessionsManager::deinit()
{
    bool isLastContext = multiwindowsProvider()->windowCount() == 1;
    if (!isLastContext) {
        return;
    }

    if (configuration()->startupModeType() != StartupModeType::ContinueLastSession) {
        reset();
    }
}

bool SessionsManager::hasProjectsForRestore()
{
    return !configuration()->sessionProjectsPaths().empty();
}

void SessionsManager::restore()
{
    io::paths_t projects = configuration()->sessionProjectsPaths();
    if (projects.empty()) {
        return;
    }

    for (const muse::io::path_t& path : projects) {
        commandDispatcher()->dispatch(project::PROJECT_OPEN_COMMAND, { { "url", muse::Val(path.toQUrl().toString()) } });
    }
}

void SessionsManager::reset()
{
    configuration()->setSessionProjectsPaths({});
}

void SessionsManager::setIgnoreSession(bool ignore)
{
    m_ignoreSession = ignore;

    if (m_ignoreSession) {
        //! Clear anything already persisted for this instance and stop tracking, so no later teardown can leave a
        //! leftover restore path. In the headless import flow this runs before the project opens, so the imported
        //! project never enters the list in the first place.
        reset();
        m_lastOpenedProjectPath = muse::io::path_t();
    }
}

void SessionsManager::update()
{
    //! Reprise: agent/headless-driven instances never persist the open project (see setIgnoreSession). Checked on
    //! every update so the imported project is not added regardless of when projectChanged fires.
    if (m_ignoreSession) {
        return;
    }

    muse::io::path_t newProjectPath;

    if (auto project = projectSession()->project()) {
        newProjectPath = project->isNewlyCreated() ? projectConfiguration()->newProjectTemporaryPath() : project->path();
    }

    if (newProjectPath == m_lastOpenedProjectPath) {
        return;
    }

    if (!m_lastOpenedProjectPath.empty()) {
        removeProjectFromSession(m_lastOpenedProjectPath);
    }

    if (!newProjectPath.empty()) {
        addProjectToSession(newProjectPath);
    }

    m_lastOpenedProjectPath = newProjectPath;
}

void SessionsManager::removeProjectFromSession(const muse::io::path_t& projectPath)
{
    io::paths_t projects = configuration()->sessionProjectsPaths();
    if (projects.empty()) {
        return;
    }

    projects.erase(std::remove(projects.begin(), projects.end(), projectPath), projects.end());
    configuration()->setSessionProjectsPaths(projects);
}

void SessionsManager::addProjectToSession(const muse::io::path_t& projectPath)
{
    io::paths_t projects = configuration()->sessionProjectsPaths();

    if (std::find(projects.begin(), projects.end(), projectPath) == projects.end()) {
        projects.push_back(projectPath);
    }

    configuration()->setSessionProjectsPaths(projects);
}
