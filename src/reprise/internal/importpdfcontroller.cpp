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

#include "importpdfcontroller.h"

#include <QString>
#include <QVariant>
#include <QVariantMap>

#include "global/translation.h"
#include "global/log.h"
#include "global/types/val.h"

#include "reprise/reprisecommands.h"

using namespace mu;
using namespace mu::reprise;
using namespace muse;

void ImportPdfController::init()
{
    m_audiverisRunner = std::make_shared<AudiverisRunner>(iocContext());

    commandDispatcher()->onRequest(this, REPRISE_IMPORT_PDF_COMMAND,
                                   [this](const muse::rcommand::Params& params) { return importPdf(params); });

    //! Read-only MCP/API queries. Always callable (not gated on a running import): they report the latest snapshot.
    commandDispatcher()->onRequest<GetImportProgressCommand>(this, std::function<muse::RetVal<GetImportProgressCommand::Output>(const GetImportProgressCommand&)>(
                                                                 [this](const GetImportProgressCommand&) { return getImportProgress(); }));
    commandDispatcher()->onRequest<GetImportErrorsCommand>(this, std::function<muse::RetVal<GetImportErrorsCommand::Output>(const GetImportErrorsCommand&)>(
                                                               [this](const GetImportErrorsCommand&) { return getImportErrors(); }));
}

muse::Ret ImportPdfController::importPdf(const muse::rcommand::Params& params)
{
    //! Reprise has exactly one OMR path: local Audiveris. Verify it is present before asking for a file.
    if (m_audiverisRunner->findAudiveris().empty())
    {
        //! Headless caller (a "path" was supplied): fail fast without a modal dialog that would block the thread.
        if (!params.at("path").toString().empty())
        {
            return make_ret(Ret::Code::NotSupported, "Audiveris was not found");
        }

        interactive()->warning(muse::trc("reprise", "Audiveris was not found"),
                               muse::trc("reprise", "Install Audiveris or set its path to import PDFs."),
                               { interactive()->buttonData(IInteractive::Button::Ok) });

        return make_ret(Ret::Code::NotSupported);
    }

    //! Headless / scripted path: a "path" arg skips the interactive picker and the options dialog entirely, so an
    //! MCP/API caller can drive the whole import with no GUI clicks. "pages"/"dpi" default when omitted.
    const std::string headlessPath = params.at("path").toString();
    if (!headlessPath.empty())
    {
        const io::path_t pdfPath(headlessPath);
        if (!fileSystem()->exists(pdfPath))
        {
            return make_ret(Ret::Code::UnknownError, "PDF not found: " + headlessPath);
        }

        std::string pages = params.at("pages").toString();
        if (pages.empty())
        {
            pages = "all";
        }

        int dpi = static_cast<int>(params.at("dpi").toInt());
        if (dpi <= 0)
        {
            dpi = scanResolution();
        }

        rememberImportPdfDir(pdfPath);
        setScanResolution(dpi);
        launchImport(pdfPath, pages, dpi);
        return make_ok();
    }

    const io::path_t pdfPath = interactive()->selectOpeningFileSync(muse::trc("reprise", "Import PDF"), defaultImportPdfDir(),
                                                                    { muse::trc("reprise", "PDF files") + " (*.pdf)" });
    if (pdfPath.empty())
    {
        return make_ret(Ret::Code::Cancel);
    }

    //! Remember the folder on a real pick (never on cancel), independent of MuseScore's File > Open folder.
    rememberImportPdfDir(pdfPath);

    //! Intake dialog: page range (free-form) + resolution (validated 200-400 on OK). Cancel discards the import.
    UriQuery query("musescore://reprise/importpdf/options");
    query.addParam("pages", Val(std::string("all")));
    query.addParam("dpi", Val(scanResolution()));

    const RetVal<Val> dialog = interactive()->openSync(query);
    if (!dialog.ret)
    {
        return make_ret(Ret::Code::Cancel);
    }

    const QVariantMap vals = dialog.val.toQVariant().toMap();
    if (!vals.value("accepted").toBool())
    {
        return make_ret(Ret::Code::Cancel);
    }

    const QString pages = vals.value("pages").toString();
    const int dpi = vals.value("dpi").toInt();

    //! Remember the chosen resolution so the dialog defaults to it next time.
    setScanResolution(dpi);

    launchImport(pdfPath, pages.toStdString(), dpi);

    return make_ok();
}

void ImportPdfController::launchImport(const muse::io::path_t& pdfPath, const std::string& pages, int dpi)
{
    //! Seed the shared progress tracker up front so an immediate reprise.get-import-progress poll already sees an
    //! active run (the popup's view-model takes over the detailed progress once it loads).
    importProgressTracker()->reportStarted(pdfPath.toStdString());

    //! Launch the recognition progress popup (non-blocking). Its view-model owns the async Audiveris run, the
    //! import of the produced MusicXML, and the temporary Grand-Piano override. Progress + Cancel live there.
    UriQuery progressQuery("musescore://reprise/importpdf/progress");
    progressQuery.addParam("pdfPath", Val(pdfPath.toQString().toStdString()));
    progressQuery.addParam("pages", Val(pages));
    progressQuery.addParam("dpi", Val(dpi));

    interactive()->open(progressQuery);
}

muse::RetVal<GetImportProgressCommand::Output> ImportPdfController::getImportProgress() const
{
    GetImportProgressCommand::Output output;

    const ImportProgressSnapshot snap = importProgressTracker()->snapshot();
    output.active = snap.active;
    output.status = snap.status;
    output.phase = snap.phase;
    output.percent = snap.percent;
    output.page = snap.page;
    output.totalPages = snap.totalPages;
    output.pdfPath = snap.pdfPath;

    return muse::RetVal<GetImportProgressCommand::Output>::make_ok(output);
}

muse::RetVal<GetImportErrorsCommand::Output> ImportPdfController::getImportErrors() const
{
    GetImportErrorsCommand::Output output;

    const ImportProgressSnapshot snap = importProgressTracker()->snapshot();
    output.hasError = snap.hasError;
    output.error = snap.error;
    output.details = snap.details;

    return muse::RetVal<GetImportErrorsCommand::Output>::make_ok(output);
}

muse::io::path_t ImportPdfController::defaultImportPdfDir() const
{
    const io::path_t dir = repriseConfiguration()->lastImportPdfDir();
    if (!dir.empty() && fileSystem()->exists(dir))
    {
        return dir;
    }

    return globalConfiguration()->documentsPath();
}

void ImportPdfController::rememberImportPdfDir(const muse::io::path_t& pdfPath)
{
    if (pdfPath.empty())
    {
        return;
    }

    repriseConfiguration()->setLastImportPdfDir(io::dirpath(pdfPath));
}

int ImportPdfController::scanResolution() const
{
    return repriseConfiguration()->scanResolution();
}

void ImportPdfController::setScanResolution(int dpi)
{
    repriseConfiguration()->setScanResolution(dpi);
}
