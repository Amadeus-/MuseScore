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

#include <string>
#include <tuple>

#include "rcommand/commandtypes.h"
#include "rcommand/typedcommand.h"

namespace mu::reprise {
inline static const muse::rcommand::Command REPRISE_IMPORT_PDF_COMMAND("command://reprise/import-pdf");

//! NOTE Reprise read-only MCP/API query: progress of the current/last Import-PDF (Audiveris OMR) run. Lets a
//! headless agent poll a running import without any GUI interaction.
struct GetImportProgressCommand {
    static const inline auto id = muse::rcommand::Command("command://reprise/get-import-progress");
    static const inline auto title = muse::MnemonicString("reprise", "Get import progress");
    static const inline auto description = muse::TranslatableString("reprise", "Get progress of the current or last PDF import");
    static const inline auto availabilities = muse::rcommand::Availability::API;
    static const inline auto annotations = muse::rcommand::Annotation::ReadOnly;

    struct Output {
        bool active = false;
        std::string status;      // "idle" | "running" | "done" | "error"
        std::string phase;
        int percent = -1;
        int page = 0;
        int totalPages = 0;
        std::string pdfPath;
    };

    using O = GetImportProgressCommand::Output;
    static constexpr auto output = std::tuple {
        muse::rcommand::Field{ "active", &O::active, "Whether an import is currently running" },
        muse::rcommand::Field{ "status", &O::status, "idle | running | done | error" },
        muse::rcommand::Field{ "phase", &O::phase, "Human-readable current recognition stage" },
        muse::rcommand::Field{ "percent", &O::percent, "Progress 0-100, or -1 while indeterminate" },
        muse::rcommand::Field{ "page", &O::page, "Current sheet being recognised (1-based, 0 until known)" },
        muse::rcommand::Field{ "totalPages", &O::totalPages, "Total sheets in the run (0 until known)" },
        muse::rcommand::Field{ "pdfPath", &O::pdfPath, "Source PDF of the current/last run" },
    };
};

//! NOTE Reprise read-only MCP/API query: error(s) from the current/last Import-PDF run.
struct GetImportErrorsCommand {
    static const inline auto id = muse::rcommand::Command("command://reprise/get-import-errors");
    static const inline auto title = muse::MnemonicString("reprise", "Get import errors");
    static const inline auto description = muse::TranslatableString("reprise", "Get error details from the current or last PDF import");
    static const inline auto availabilities = muse::rcommand::Availability::API;
    static const inline auto annotations = muse::rcommand::Annotation::ReadOnly;

    struct Output {
        bool hasError = false;
        std::string error;
        std::string details;
    };

    using O = GetImportErrorsCommand::Output;
    static constexpr auto output = std::tuple {
        muse::rcommand::Field{ "hasError", &O::hasError, "Whether the last finished import ended in error" },
        muse::rcommand::Field{ "error", &O::error, "Short error message (empty if none)" },
        muse::rcommand::Field{ "details", &O::details, "A tail of Audiveris output for the error (empty if none)" },
    };
};
}
