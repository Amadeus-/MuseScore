/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

#include "reprisecommandsstate.h"

using namespace muse;
using namespace muse::rcommand;
using namespace mu::reprise;

std::string RepriseCommandsState::moduleName() const
{
    return "reprise";
}

void RepriseCommandsState::init()
{
    //! NOTE: All reprise commands are always enabled. Seed the cache so the UI reflects the enabled state immediately.
    IModuleCommandsRegisterPtr moduleRegister = commandsRegister()->moduleRegister(moduleName());
    IF_ASSERT_FAILED(moduleRegister) {
        return;
    }

    for (const Command& command : moduleRegister->commandList())
    {
        m_commandStateChanged.send(command, commandState(command));
    }

    return;
}

void RepriseCommandsState::deinit()
{
    return;
}

CommandState RepriseCommandsState::commandState(const Command& command) const
{
    //! NOTE: Reprise commands (e.g. "Import PDF") are always available - they create a score, so they must be
    //! clickable even when no score/notation is open. Equivalent to the project module's PCondition::Any.
    (void)command;

    return CommandState(true, false);
}

async::Channel<Command, CommandState> RepriseCommandsState::commandStateChanged() const
{
    return m_commandStateChanged;
}
