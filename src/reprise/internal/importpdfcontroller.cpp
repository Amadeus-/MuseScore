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

#include "settings.h"

#include "global/translation.h"
#include "global/log.h"
#include "global/types/val.h"

#include "reprise/reprisecommands.h"

using namespace mu;
using namespace mu::reprise;
using namespace muse;

//! Reprise-private settings, kept out of the "project" module so upstream merges stay clean.
static const Settings::Key LAST_IMPORT_PDF_DIR("reprise", "application/paths/lastImportPdfDir");
static const Settings::Key SCAN_RESOLUTION("reprise", "import/scanResolution");

//! Default scan resolution (dpi). The useful range for notation really starts ~300.
static constexpr int DEFAULT_SCAN_RESOLUTION = 300;

void ImportPdfController::init()
{
    m_audiverisRunner = std::make_shared<AudiverisRunner>(iocContext());

    settings()->setDefaultValue(SCAN_RESOLUTION, Val(DEFAULT_SCAN_RESOLUTION));

    commandDispatcher()->onRequest(this, REPRISE_IMPORT_PDF_COMMAND, [this]() { return importPdf(); });
}

muse::Ret ImportPdfController::importPdf()
{
    //! Reprise has exactly one OMR path: local Audiveris. Verify it is present before asking for a file.
    if (m_audiverisRunner->findAudiveris().empty())
    {
        interactive()->warning(muse::trc("reprise", "Audiveris was not found"),
                               muse::trc("reprise", "Install Audiveris or set its path to import PDFs."),
                               { interactive()->buttonData(IInteractive::Button::Ok) });

        return make_ret(Ret::Code::NotSupported);
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

    //! Launch the recognition progress popup (non-blocking). Its view-model owns the async Audiveris run, the
    //! import of the produced MusicXML, and the temporary Grand-Piano override. Progress + Cancel live there.
    UriQuery progressQuery("musescore://reprise/importpdf/progress");
    progressQuery.addParam("pdfPath", Val(pdfPath.toQString().toStdString()));
    progressQuery.addParam("pages", Val(pages.toStdString()));
    progressQuery.addParam("dpi", Val(dpi));

    interactive()->open(progressQuery);

    return make_ok();
}

muse::io::path_t ImportPdfController::defaultImportPdfDir() const
{
    const io::path_t dir = settings()->value(LAST_IMPORT_PDF_DIR).toPath();
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

    settings()->setSharedValue(LAST_IMPORT_PDF_DIR, Val(io::dirpath(pdfPath)));
}

int ImportPdfController::scanResolution() const
{
    return settings()->value(SCAN_RESOLUTION).toInt();
}

void ImportPdfController::setScanResolution(int dpi)
{
    settings()->setSharedValue(SCAN_RESOLUTION, Val(dpi));
}
