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

#pragma once

#include "rcommand/imodulecommandsstate.h"
#include "rcommand/icommandsregister.h"

#include "global/modularity/ioc.h"
#include "global/async/asyncable.h"

namespace mu::reprise {
//! NOTE: Reprise command enable-states. Every reprise command (currently only "Import PDF") is ALWAYS enabled -
//! "Import PDF" creates a score, so it must be clickable with no score/notation open. Mirrors the project module's
//! PCondition::Any. Without a registered IModuleCommandsState, muse::rcommand::CommandsState falls back to the default
//! CommandState() whose enabled==false, so the menu item is grayed out.
class RepriseCommandsState : public muse::rcommand::IModuleCommandsState, public muse::async::Asyncable
{
    muse::GlobalInject<muse::rcommand::ICommandsRegister> commandsRegister;

public:
    RepriseCommandsState() = default;

    std::string moduleName() const override;

    void init() override;
    void deinit() override;

    muse::rcommand::CommandState commandState(const muse::rcommand::Command& command) const override;
    muse::async::Channel<muse::rcommand::Command, muse::rcommand::CommandState> commandStateChanged() const override;

private:
    muse::async::Channel<muse::rcommand::Command, muse::rcommand::CommandState> m_commandStateChanged;
};
}
