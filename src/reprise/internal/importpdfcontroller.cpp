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

#include "pdfresolutiondetector.h"

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

        //! No (or non-positive) "dpi" arg -> auto-detect from the PDF, same detector the GUI uses. A clean match
        //! snaps to a tier (200/300/400) so headless and GUI runs land on identical resolutions; a miss falls back
        //! to the stored scanResolution(). Intentionally NOT clamped to 200-400: an explicit "dpi" arg stays raw
        //! for headless agents who want a non-tier resolution.
        int dpi = static_cast<int>(params.at("dpi").toInt());
        if (dpi <= 0)
        {
            const PdfResolutionResult detected = detectPdfResolution(pdfPath);
            if (detected.matched)
            {
                dpi = snapToTier(detected.dpi);
                LOGI() << "Reprise PDF detect (headless): native dpi=" << detected.dpi << " -> tier " << dpi;
            }
            else
            {
                dpi = scanResolution();
                LOGI() << "Reprise PDF detect (headless): no embedded resolution found -> fallback dpi=" << dpi;
            }
        }

        //! Agent/headless-driven run (a "path" arg was supplied): mark the session non-restorable so the project this
        //! import opens never lands in the session-restore list. Otherwise the ~15s job-teardown kill (which never
        //! reaches SessionsManager::deinit()) would leave a leftover path that pops the "previous session quit
        //! unexpectedly" prompt on the next USER launch. The normal GUI import path below is untouched, so a genuine
        //! user quit with a project open still restores as before. Set before the project opens, so ordering is moot.
        sessionsManager()->setIgnoreSession(true);

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

    //! Auto-detect the PDF's native image resolution BEFORE the dialog opens so we can pre-select a matching quality
    //! tier. A clean detection pre-selects its snapped tier and flags the "matched to your PDF" badge; a miss falls
    //! back to the stored scanResolution() (snapped to a tier) with no badge.
    const PdfResolutionResult detected = detectPdfResolution(pdfPath);
    const int preselectTier = detected.matched ? snapToTier(detected.dpi) : snapToTier(scanResolution());
    if (detected.matched)
    {
        LOGI() << "Reprise PDF detect: native dpi=" << detected.dpi << " -> tier " << preselectTier << " (matched)";
    }
    else
    {
        LOGI() << "Reprise PDF detect: no embedded resolution found -> tier " << preselectTier << " (Standard, no badge)";
    }

    //! Intake dialog: page range (free-form) + a Faster/Standard/Best quality tier. Cancel discards the import.
    UriQuery query("musescore://reprise/importpdf/options");
    query.addParam("pages", Val(std::string("all")));
    query.addParam("tier", Val(preselectTier));
    query.addParam("matched", Val(detected.matched));

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
