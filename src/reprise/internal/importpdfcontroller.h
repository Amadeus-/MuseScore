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

#include <memory>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "interactive/iinteractive.h"
#include "rcommand/icommanddispatcher.h"
#include "rcommand/commandable.h"
#include "global/types/ret.h"
#include "global/iglobalconfiguration.h"
#include "global/io/ifilesystem.h"
#include "global/io/path.h"

#include "appshell/internal/isessionsmanager.h"

#include "irepriseconfiguration.h"
#include "iimportprogresstracker.h"
#include "audiverisrunner.h"

#include "reprise/reprisecommands.h"

namespace mu::reprise {
//! Orchestrates Reprise's "Import PDF" flow: pick a PDF -> intake dialog (pages + resolution) -> show the async
//! recognition progress popup. The popup's view-model (ImportProgressModel) owns the Audiveris run, the import,
//! and the Grand-Piano override; this controller only drives the picker + intake dialog and launches the popup.
class ImportPdfController : public muse::Contextable, public muse::async::Asyncable, public muse::rcommand::Commandable
{
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;
    muse::GlobalInject<mu::reprise::IRepriseConfiguration> repriseConfiguration;
    muse::GlobalInject<mu::reprise::IImportProgressTracker> importProgressTracker;
    muse::ContextInject<mu::appshell::ISessionsManager> sessionsManager = { this };

public:
    explicit ImportPdfController(const muse::modularity::ContextPtr& iocCtx)
        : muse::Contextable(iocCtx) {}

    void init();

private:
    //! Classic interactive flow (no "path" arg) OR headless flow (a "path" arg skips the picker + options dialog).
    muse::Ret importPdf(const muse::rcommand::Params& params);

    //! Launch the recognition progress popup for pdfPath with the given page range + resolution (the popup's
    //! view-model owns the Audiveris run + import). Also seeds the progress tracker so a poll sees it immediately.
    void launchImport(const muse::io::path_t& pdfPath, const std::string& pages, int dpi);

    //! Read-only MCP/API queries backed by the shared progress tracker.
    muse::RetVal<GetImportProgressCommand::Output> getImportProgress() const;
    muse::RetVal<GetImportErrorsCommand::Output> getImportErrors() const;

    //! Start folder for the PDF picker: the stored Reprise folder if it still exists, else the Documents folder.
    muse::io::path_t defaultImportPdfDir() const;
    //! Persist the folder of the picked PDF (Reprise-private, independent of File > Open). Non-empty picks only.
    void rememberImportPdfDir(const muse::io::path_t& pdfPath);

    //! Last-used scan resolution, used as the dialog default (defaults to 300 dpi).
    int scanResolution() const;
    void setScanResolution(int dpi);

    //! Kept only for the pre-flight findAudiveris() check; the actual run lives in ImportProgressModel.
    std::shared_ptr<AudiverisRunner> m_audiverisRunner;
};
}
