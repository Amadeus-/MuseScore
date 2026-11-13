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
    static const inline auto output = std::tuple {
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
    static const inline auto output = std::tuple {
        muse::rcommand::Field{ "hasError", &O::hasError, "Whether the last finished import ended in error" },
        muse::rcommand::Field{ "error", &O::error, "Short error message (empty if none)" },
        muse::rcommand::Field{ "details", &O::details, "A tail of Audiveris output for the error (empty if none)" },
    };
};

//! NOTE Reprise dev MCP/API tool: capture the running GUI to a PNG and return its absolute path. target="window"
//! (the default) grabs the whole main application window; target="score" renders the current score's page (0-based,
//! default page 0) through the registered "png" notation writer. The handler runs synchronously on the Qt GUI thread
//! (the same dispatch path score.get-counts uses), so QQuickWindow::grabWindow() + the PNG writer are called directly
//! -- no cross-thread marshalling. An optional "path" overrides the per-target fixed overwrite filename; "page"
//! applies only to the score target. Failures carry ok=false + an error string (the call itself still succeeds) so a
//! headless agent always receives structured output with ok/path/error.
struct CaptureScreenshotCommand {
    static const inline auto id = muse::rcommand::Command("command://reprise/capture-screenshot");
    static const inline auto title = muse::MnemonicString("reprise", "Capture screenshot");
    static const inline auto description
        = muse::TranslatableString("reprise", "Capture the GUI window or current score to a PNG and return its path");
    static const inline auto availabilities = muse::rcommand::Availability::API;
    static const inline auto annotations = muse::rcommand::Annotation::ReadOnly;

    std::string target;    // "window" (default when empty) | "score"
    std::string path;      // optional absolute output path override
    int page = 0;          // score target only: 0-based page to render

    static const inline auto input = std::tuple {
        muse::rcommand::Field{ "target", &CaptureScreenshotCommand::target,
                               "What to capture: 'window' (default) or 'score'", muse::rcommand::Presence::Optional },
        muse::rcommand::Field{ "path", &CaptureScreenshotCommand::path,
                               "Absolute output PNG path (optional; default is a per-target overwrite file)",
                               muse::rcommand::Presence::Optional },
        muse::rcommand::Field{ "page", &CaptureScreenshotCommand::page,
                               "Score page to render, 0-based (score target only; default 0)",
                               muse::rcommand::Presence::Optional },
    };

    struct Output {
        bool ok = false;
        std::string path;
        std::string error;
    };

    using O = CaptureScreenshotCommand::Output;
    static const inline auto output = std::tuple {
        muse::rcommand::Field{ "ok", &O::ok, "Whether the capture succeeded" },
        muse::rcommand::Field{ "path", &O::path, "Absolute path of the written PNG (empty on failure)" },
        muse::rcommand::Field{ "error", &O::error, "Error message (empty on success)" },
    };
};
}
