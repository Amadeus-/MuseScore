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

#include <cstdint>

#include <QByteArray>
#include <QFile>
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

namespace {
//! Read cap for the in-house TIFF inspector. Large enough to cover a multi-frame scan whose image file directories may
//! sit near the end of the file; a TIFF larger than this can't be walked safely, so the inspector reports failure and
//! the caller shows the page range (never hides pages).
static constexpr qint64 TIFF_MAX_READ_BYTES = 128LL * 1024 * 1024;

//! Little/big-endian 16- and 32-bit readers for the TIFF byte-scan. Callers guarantee the bounds.
static uint32_t rdU16(const QByteArray& d, int64_t pos, bool le)
{
    const uint8_t a = static_cast<uint8_t>(d.at(static_cast<int>(pos)));
    const uint8_t b = static_cast<uint8_t>(d.at(static_cast<int>(pos + 1)));
    return le ? (static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8))
              : (static_cast<uint32_t>(b) | (static_cast<uint32_t>(a) << 8));
}

static uint32_t rdU32(const QByteArray& d, int64_t pos, bool le)
{
    const uint8_t a = static_cast<uint8_t>(d.at(static_cast<int>(pos)));
    const uint8_t b = static_cast<uint8_t>(d.at(static_cast<int>(pos + 1)));
    const uint8_t c = static_cast<uint8_t>(d.at(static_cast<int>(pos + 2)));
    const uint8_t e = static_cast<uint8_t>(d.at(static_cast<int>(pos + 3)));
    return le ? (static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8)
                 | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(e) << 24))
              : ((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16)
                 | (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(e));
}

//! In-house TIFF inspector: count frames (image file directories) by walking the IFD chain, and read the first frame's
//! pixel width/height (tags 256/257). This Qt kit ships NO TIFF image plugin, so QImageReader cannot read a TIFF at all
//! -- hence this plugin-independent fallback. Classic TIFF only (BigTIFF magic 43 -> false). Returns false on any parse
//! failure (not a classic TIFF, truncated, offset out of range) so the caller treats the frame count as unknown.
static bool readTiffInfo(const QByteArray& d, int& outFrames, int& outWidth, int& outHeight)
{
    if (d.size() < 8)
    {
        return false;
    }

    const QByteArray bo = d.left(2);
    bool le = false;
    if (bo == "II")
    {
        le = true;
    }
    else if (bo == "MM")
    {
        le = false;
    }
    else
    {
        return false;
    }

    if (rdU16(d, 2, le) != 42)   // 43 == BigTIFF, not handled here
    {
        return false;
    }

    int64_t off = rdU32(d, 4, le);
    int frames = 0;
    int width = 0;
    int height = 0;

    //! Guard caps the walk so a malformed / looping IFD chain can never spin forever.
    for (int guard = 0; guard < 100000 && off != 0; ++guard)
    {
        if (off < 0 || off + 2 > d.size())
        {
            return false;
        }

        const uint32_t entries = rdU16(d, off, le);

        //! First frame only: read ImageWidth (256) / ImageLength (257) for the single-image resolution info line.
        if (frames == 0)
        {
            for (uint32_t i = 0; i < entries; ++i)
            {
                const int64_t eo = off + 2 + static_cast<int64_t>(i) * 12;
                if (eo + 12 > d.size())
                {
                    break;
                }

                const uint32_t tag = rdU16(d, eo, le);
                const uint32_t typ = rdU16(d, eo + 2, le);
                if (tag == 256 || tag == 257)
                {
                    uint32_t val = 0;
                    if (typ == 3)
                    {
                        val = rdU16(d, eo + 8, le);        // SHORT
                    }
                    else if (typ == 4)
                    {
                        val = rdU32(d, eo + 8, le);        // LONG
                    }

                    if (tag == 256)
                    {
                        width = static_cast<int>(val);
                    }
                    else
                    {
                        height = static_cast<int>(val);
                    }
                }
            }
        }

        const int64_t nextPos = off + 2 + static_cast<int64_t>(entries) * 12;
        if (nextPos + 4 > d.size())
        {
            return false;
        }

        off = rdU32(d, nextPos, le);
        ++frames;
    }

    if (frames < 1)
    {
        return false;
    }

    outFrames = frames;
    outWidth = width;
    outHeight = height;

    return true;
}
}

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

        //! Make the intake-dialog show/hide decision observable on the HEADLESS path too (no GUI): this logs the same
        //! derived values the interactive dialog-prep logs -- pageCount / isMultiPage / willShowPageRange / resolution
        //! for an image, or pdfPageCount / confident / willShowPageRange for a PDF. The return value is unused here.
        inspectImage(pdfPath, io::suffix(pdfPath) != "pdf");

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

    //! Inspect the input ONCE (also logs the decision for headless parity): a multi-frame TIFF is multiple sheets, and a
    //! multi-page PDF needs the page range, but a single image and a confidently-single-page PDF do not. inspectImage
    //! returns the final showPageRange decision plus the image's native resolution for the single-image info line.
    const ImageInfo imageInfo = inspectImage(pdfPath, isImage);

    //! Intake dialog: page range (free-form) + a Faster/Standard/Best quality tier. Cancel discards the import. The
    //! controller passes the already-decided showPageRange (single source of truth) plus the image resolution so the
    //! dialog only renders what applies: tier for any PDF, page range per showPageRange, info line for a single image.
    UriQuery query("musescore://reprise/importpdf/options");
    query.addParam("pages", Val(std::string("all")));
    query.addParam("tier", Val(preselectTier));
    query.addParam("matched", Val(detected.matched));
    query.addParam("isImage", Val(isImage));
    query.addParam("showPageRange", Val(imageInfo.showPageRange));
    query.addParam("imageWidth", Val(imageInfo.width));
    query.addParam("imageHeight", Val(imageInfo.height));

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

ImportPdfController::ImageInfo ImportPdfController::inspectImage(const muse::io::path_t& path, bool isImage) const
{
    ImageInfo info;
    info.isImage = isImage;

    if (!isImage)
    {
        //! PDF: count pages via the in-house byte-scan (no QtPdf in the kit). Show the page range UNLESS we can
        //! CONFIDENTLY determine a single page; any uncertainty (compressed object streams, file past the read cap,
        //! no page objects found) keeps it shown so pages are never silently dropped.
        const PdfPageCountResult pc = detectPdfPageCount(path);
        info.pageCount = pc.pageCount;
        info.pdfConfident = pc.confident;
        const bool confidentSinglePage = pc.confident && pc.pageCount == 1;
        info.showPageRange = !confidentSinglePage;

        LOGI() << "Reprise intake inspect: input=PDF pdfPageCount=" << pc.pageCount
               << " confident=" << (pc.confident ? "true" : "false")
               << " willShowPageRange=" << (info.showPageRange ? "true" : "false");

        return info;
    }

    //! Image frame count + native resolution. CRITICAL: this Qt kit ships NO TIFF image plugin, so QImageReader cannot
    //! read a TIFF at all (imageCount() == -1, size() invalid) -- that is the real cause of a multi-page TIFF showing no
    //! page selector, not an imageCount() quirk. We therefore count frames two ways and take the larger: QImageReader's
    //! jumpToNextImage() (works for the formats that DO have a plugin: PNG/JPG/GIF/...), and an in-house TIFF IFD-chain
    //! walk (plugin-independent). jumpToImage() is deliberately NOT used: in Qt 6 it returns false even for frame 0 of a
    //! perfectly readable image, so it always under-counts.
    int rawImageCount = 0;
    int qtWidth = 0;
    int qtHeight = 0;
    {
        QImageReader reader(path.toQString());
        rawImageCount = reader.imageCount();
        const QSize size = reader.size();
        if (size.isValid())
        {
            qtWidth = size.width();
            qtHeight = size.height();
        }
    }

    int nextCount = 0;
    {
        QImageReader reader(path.toQString());
        if (reader.canRead())
        {
            nextCount = 1;
            while (reader.jumpToNextImage())
            {
                ++nextCount;
            }
        }
    }

    //! In-house TIFF fallback (no Qt TIFF plugin): walk the IFD chain for the real frame count + first-frame dimensions.
    const std::string suffix = io::suffix(path);
    const bool isTiff = suffix == "tif" || suffix == "tiff";
    int tiffFrames = 0;
    int tiffWidth = 0;
    int tiffHeight = 0;
    bool tiffOk = false;
    if (isTiff)
    {
        QByteArray tiffData;
        QFile file(path.toQString());
        if (file.open(QIODevice::ReadOnly))
        {
            tiffData = file.read(TIFF_MAX_READ_BYTES);
            file.close();
        }

        tiffOk = readTiffInfo(tiffData, tiffFrames, tiffWidth, tiffHeight);
    }

    //! Did EITHER method actually read this image? A TIFF with no plugin AND a failed byte-scan is genuinely unknown.
    const bool frameCountKnown = nextCount >= 1 || tiffOk;

    int reliable = nextCount;
    if (tiffFrames > reliable)
    {
        reliable = tiffFrames;
    }
    if (reliable < 1)
    {
        reliable = 1;
    }

    info.width = qtWidth > 0 ? qtWidth : (tiffOk ? tiffWidth : 0);
    info.height = qtHeight > 0 ? qtHeight : (tiffOk ? tiffHeight : 0);

    if (isTiff && !frameCountKnown)
    {
        //! Could not determine the TIFF frame count by either method -> SHOW the page range (safe: never hide pages),
        //! mirroring the PDF "uncertain -> show" fallback. pageCount 0 == unknown.
        info.pageCount = 0;
        info.showPageRange = true;
    }
    else
    {
        info.pageCount = reliable;
        info.showPageRange = reliable > 1;
    }

    LOGI() << "Reprise intake inspect: input=image rawImageCount=" << rawImageCount
           << " nextCount=" << nextCount << " tiffOk=" << (tiffOk ? "true" : "false")
           << " tiffFrames=" << tiffFrames
           << " pageCount=" << info.pageCount << " isMultiPage=" << (info.pageCount > 1 ? "true" : "false")
           << " willShowPageRange=" << (info.showPageRange ? "true" : "false")
           << " resolution=" << info.width << "x" << info.height;

    return info;
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
