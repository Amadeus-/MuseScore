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

#include "iimportprogresstracker.h"

namespace mu::reprise {
//! NOTE: Trivial in-memory holder for the latest Import-PDF run snapshot. All access is on the Qt UI thread
//! (both the popup view-model writes and the command-handler reads run there), so no locking is needed.
class ImportProgressTracker : public IImportProgressTracker
{
public:
    ImportProgressTracker() = default;

    void reportStarted(const std::string& pdfPath) override
    {
        m_snapshot = ImportProgressSnapshot();
        m_snapshot.active = true;
        m_snapshot.status = "running";
        m_snapshot.pdfPath = pdfPath;
    }

    void reportProgress(int percent, int page, int totalPages, const std::string& phase) override
    {
        m_snapshot.active = true;
        m_snapshot.status = "running";
        m_snapshot.percent = percent;
        m_snapshot.page = page;
        m_snapshot.totalPages = totalPages;
        m_snapshot.phase = phase;
    }

    void reportFinished(bool ok, const std::string& error, const std::string& details) override
    {
        m_snapshot.active = false;
        m_snapshot.status = ok ? "done" : "error";
        m_snapshot.hasError = !ok;
        m_snapshot.error = ok ? std::string() : error;
        m_snapshot.details = ok ? std::string() : details;
        if (ok) {
            m_snapshot.percent = 100;
        }
    }

    ImportProgressSnapshot snapshot() const override
    {
        return m_snapshot;
    }

private:
    ImportProgressSnapshot m_snapshot;
};
}
