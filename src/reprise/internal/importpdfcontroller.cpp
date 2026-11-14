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

#include <QImageReader>
#include <QSize>
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

//! Audiveris (org.audiveris.omr.sheet.SheetStub, constant maxPixelCount) rejects any sheet whose pixel count exceeds
//! this cap and aborts the whole run with "Too large image: N pixels (vs 20,000,000 max)". Confirmed empirically
//! against Audiveris 5.11.0 (a 6000x6000 = 36,000,000-pixel image produced exactly that log line). We mirror the cap
//! so Reprise can block an oversized image BEFORE launching Audiveris.
static constexpr qint64 AUDIVERIS_MAX_PIXELS = 20000000;

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
                               muse::trc("reprise", "Install Audiveris or set its path to import scans."),
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
            return make_ret(Ret::Code::UnknownError, "Scan not found: " + headlessPath);
        }

        //! Oversized-image pre-flight (image inputs only). Block up front so the MCP/API caller gets a clean error
        //! rather than a raw Audiveris exit code. Also mirror it into the progress tracker so reprise.get-import-errors
        //! surfaces it, since this early return never reaches the popup view-model that normally reports failures.
        const muse::Ret sizeRet = preflightImageSize(pdfPath);
        if (!sizeRet)
        {
            importProgressTracker()->reportStarted(pdfPath.toStdString());
            importProgressTracker()->reportFinished(false, sizeRet.text(), std::string());
            return sizeRet;
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

    const io::path_t pdfPath = interactive()->selectOpeningFileSync(muse::trc("reprise", "Import Scan"), defaultImportPdfDir(),
                                                                    { muse::trc("reprise", "Scans") + " (*.pdf *.png *.jpg *.jpeg *.tif *.tiff *.bmp *.gif)" });
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

    //! A single image (png/jpg/tif/...) has no PDF-resolution tier to pick, so the intake dialog hides that control.
    //! Derive from the picked file's extension (io::suffix is lower-cased, no dot).
    const bool isImage = io::suffix(pdfPath) != "pdf";

    //! Oversized-image pre-flight (image inputs only; PDFs are skipped inside the helper). Block up front with a clear
    //! dialog instead of showing the options dialog and then letting Audiveris abort mid-run with a raw exit code.
    const muse::Ret sizeRet = preflightImageSize(pdfPath);
    if (!sizeRet)
    {
        interactive()->warning(muse::trc("reprise", "Scan too large"), sizeRet.text(),
                               { interactive()->buttonData(IInteractive::Button::Ok) });
        return make_ret(Ret::Code::Cancel);
    }

    //! How many sheets this input has: a PDF's page range always applies; an image is normally a single sheet, EXCEPT
    //! a multi-frame TIFF, which Audiveris treats as multiple sheets. imageCount() reports the frame count (>=1 after
    //! the clamp). The dialog shows the page-range field when !isImage (PDF) OR the image has more than one frame.
    int imagePageCount = 0;
    if (isImage)
    {
        QImageReader reader(pdfPath.toQString());
        imagePageCount = reader.imageCount();
        if (imagePageCount < 1)
        {
            imagePageCount = 1;
        }
    }

    //! Intake dialog: page range (free-form) + a Faster/Standard/Best quality tier. Cancel discards the import.
    UriQuery query("musescore://reprise/importpdf/options");
    query.addParam("pages", Val(std::string("all")));
    query.addParam("tier", Val(preselectTier));
    query.addParam("matched", Val(detected.matched));
    query.addParam("isImage", Val(isImage));
    query.addParam("imagePageCount", Val(imagePageCount));

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

muse::Ret ImportPdfController::preflightImageSize(const muse::io::path_t& path) const
{
    //! PDFs are rasterised by Audiveris at a chosen DPI (the quality tier), so their on-disk content has no fixed
    //! pixel count to pre-flight here. Only direct image inputs have fixed pixel dimensions.
    if (io::suffix(path) == "pdf")
    {
        return make_ok();
    }

    //! A multi-frame TIFF is several sheets, each of which Audiveris checks independently; iterate the frames and let
    //! the largest decide. size() reads the dimensions from the header without decoding the pixels (so this stays cheap
    //! and never has to load the very image we are trying to reject). A frame whose size can't be read is skipped.
    QImageReader reader(path.toQString());
    int frames = reader.imageCount();
    if (frames < 1)
    {
        frames = 1;
    }

    qint64 maxPixels = 0;
    for (int i = 0; i < frames; ++i)
    {
        reader.jumpToImage(i);
        const QSize size = reader.size();
        if (size.isValid())
        {
            const qint64 pixels = static_cast<qint64>(size.width()) * static_cast<qint64>(size.height());
            if (pixels > maxPixels)
            {
                maxPixels = pixels;
            }
        }
    }

    if (maxPixels > AUDIVERIS_MAX_PIXELS)
    {
        const QString message = muse::qtrc("reprise",
                                           "This scan is too large to recognize (%1 megapixels). The maximum is "
                                           "20 megapixels (20,000,000 pixels). Please use a lower-resolution scan.")
                                .arg(QString::number(double(maxPixels) / 1000000.0, 'f', 1));
        LOGW() << "Reprise: image pre-flight blocked oversized scan: " << maxPixels << " pixels (vs "
               << AUDIVERIS_MAX_PIXELS << " max): " << path.toQString();
        return make_ret(Ret::Code::UnknownError, message.toStdString());
    }

    return make_ok();
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
