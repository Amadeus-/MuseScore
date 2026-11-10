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

#include "reprisecommandsregister.h"

#include "reprise/reprisecommands.h"

using namespace muse;
using namespace muse::rcommand;
using namespace muse::ui;
using namespace mu::reprise;

static const std::vector<CommandInfo> s_commandInfos = {
    CommandInfo{
        REPRISE_IMPORT_PDF_COMMAND,
        TranslatableString("reprise", "Import &PDF…"),
        TranslatableString("reprise", "Import a PDF and recognise it as a score (Audiveris)"),
        InputSchema(),
        Decoration(IconCode::Code::IMPORT)
    },
};

std::string RepriseCommandsRegister::moduleName() const
{
    return "reprise";
}

const std::vector<muse::rcommand::Command>& RepriseCommandsRegister::commandList() const
{
    static std::vector<muse::rcommand::Command> commands;
    if (commands.empty())
    {
        commands.reserve(s_commandInfos.size());
        for (const auto& info : s_commandInfos)
        {
            commands.push_back(info.command);
        }
    }

    return commands;
}

const std::vector<muse::rcommand::CommandInfo>& RepriseCommandsRegister::commandInfoList() const
{
    return s_commandInfos;
}
