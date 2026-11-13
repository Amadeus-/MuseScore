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
        //! NOTE When "path" is supplied (e.g. from an MCP/API caller) the interactive file picker and options dialog
        //! are skipped and the import runs headlessly with the given page range + resolution. With no "path" the
        //! classic interactive flow (file picker + options dialog) runs.
        InputSchema({
            { "path", Arg(DataType::String, std::string("Absolute path of the PDF to import (optional; empty = pick interactively)"), Presence::Optional) },
            { "pages", Arg(DataType::String, std::string("Page range, e.g. 'all', '1', '1-3', '1,4,6-9' (optional; default all)"), Presence::Optional) },
            { "dpi", Arg(DataType::Int, std::string("Scan resolution handed to Audiveris (optional; default from settings)"), Presence::Optional) },
        }),
        Decoration(IconCode::Code::IMPORT)
    },

    // Reprise read-only MCP/API queries
    makeCommandInfo<GetImportProgressCommand>(),
    makeCommandInfo<GetImportErrorsCommand>(),

    // Reprise dev MCP/API tool: capture the GUI window or current score to a PNG
    makeCommandInfo<CaptureScreenshotCommand>(),
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
