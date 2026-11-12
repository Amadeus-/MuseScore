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

#include "modularity/imoduleinterface.h"

namespace mu::reprise {
//! NOTE: A persistent, app-lifetime snapshot of the LATEST Import-PDF / OMR run, written by the transient progress
//! popup view-model (ImportProgressModel) as Audiveris runs, and read back by the reprise.get-import-progress /
//! reprise.get-import-errors MCP/API queries. The popup is destroyed when it closes, so this global tracker is the
//! only place a headless agent can observe import progress + errors.
struct ImportProgressSnapshot {
    bool active = false;             //! an import is currently running
    std::string status = "idle";     //! "idle" | "running" | "done" | "error"
    std::string phase;               //! human-readable Audiveris stage (e.g. "detecting staves")
    int percent = -1;                //! 0-100, or -1 while the sheet total is still unknown
    int page = 0;                    //! 1-based current sheet (0 until known)
    int totalPages = 0;              //! total sheets in the run (0 until known)
    std::string pdfPath;             //! source PDF of the current/last run
    bool hasError = false;           //! the last finished run ended in error
    std::string error;               //! short error message
    std::string details;             //! a tail of Audiveris output for the error
};

class IImportProgressTracker : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IImportProgressTracker)

public:
    virtual ~IImportProgressTracker() = default;

    //! A new run has begun for pdfPath: resets the snapshot to a fresh "running" state.
    virtual void reportStarted(const std::string& pdfPath) = 0;

    //! Streamed progress update from the running OMR.
    virtual void reportProgress(int percent, int page, int totalPages, const std::string& phase) = 0;

    //! The run finished: ok -> status "done"; else status "error" with the message + details tail.
    virtual void reportFinished(bool ok, const std::string& error, const std::string& details) = 0;

    //! The current snapshot (thread-confined to the UI thread; reads happen on the same thread as writes).
    virtual ImportProgressSnapshot snapshot() const = 0;
};
}
