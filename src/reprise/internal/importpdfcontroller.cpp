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

#include <vector>

#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include "settings.h"

#include "global/translation.h"
#include "global/log.h"
#include "global/types/val.h"
#include "async/notifylist.h"

#include "notation/imasternotation.h"
#include "notation/inotationparts.h"
#include "notation/types/instrumentstypes.h"

#include "engraving/dom/instrument.h"
#include "engraving/dom/part.h"

#include "reprise/reprisecommands.h"
#include "project/projectcommands.h"

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

    //! TEMPORARY Grand-Piano default: the imported score becomes the current master asynchronously (the open
    //! command is dispatched fire-and-forget), so apply the override on the next master-notation change.
    projectSession()->masterNotationChanged().onNotify(this, [this]()
    {
        if (!m_pendingGrandPianoReset)
        {
            return;
        }

        if (!projectSession()->masterNotation())
        {
            return;
        }

        m_pendingGrandPianoReset = false;
        applyGrandPianoDefault();
    });
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

    //! Blocking OMR run (skeleton). Async + progress UI is a later step.
    const RetVal<io::path_t> output = m_audiverisRunner->run(pdfPath, pages, dpi);
    if (!output.ret)
    {
        const std::string detail = output.ret.text().empty()
                                   ? muse::trc("reprise", "Audiveris could not recognise this PDF.")
                                   : output.ret.text();

        interactive()->warning(muse::trc("reprise", "PDF import failed"), detail,
                               { interactive()->buttonData(IInteractive::Button::Ok) });

        return output.ret;
    }

    //! Hand the produced MusicXML to MuseScore's native open path; a .mxl/.xml routes through MusicXmlReader on import.
    //! Arm the Grand-Piano override for the score about to open, then dispatch (fire-and-forget).
    m_pendingGrandPianoReset = true;

    const QUrl url = QUrl::fromLocalFile(output.val.toQString());
    commandDispatcher()->dispatch(project::PROJECT_OPEN_COMMAND, { { "url", muse::Val(url.toString()) } });

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

void ImportPdfController::applyGrandPianoDefault()
{
    //! TEMPORARY Reprise default. Audiveris assigns semi-random instruments to recognised parts; until an intake
    //! step adds real instrument selection, force every imported part to Grand Piano. Remove this whole method
    //! (and its call site in init()) when instrument choice is implemented.
    static const muse::String GRAND_PIANO_ID(u"grand-piano");

    const notation::InstrumentTemplate& templ = instrumentsRepository()->instrumentTemplate(GRAND_PIANO_ID);
    if (templ.id.isEmpty())
    {
        LOGW() << "Reprise: 'grand-piano' instrument template not found; leaving imported instruments as-is";
        return;
    }

    const notation::IMasterNotationPtr master = projectSession()->masterNotation();
    if (!master)
    {
        return;
    }

    const notation::INotationPartsPtr parts = master->parts();
    if (!parts)
    {
        return;
    }

    //! Snapshot the keys first: each replaceInstrument() mutates the score and notifies part changes.
    std::vector<notation::InstrumentKey> keys;
    for (const engraving::Part* part : parts->partList())
    {
        if (!part)
        {
            continue;
        }

        notation::InstrumentKey key;
        key.partId = part->id();
        key.instrumentId = part->instrumentId();
        key.tick = engraving::Part::MAIN_INSTRUMENT_TICK;
        keys.push_back(key);
    }

    const engraving::Instrument grandPiano = engraving::Instrument::fromTemplate(&templ);
    for (const notation::InstrumentKey& key : keys)
    {
        parts->replaceInstrument(key, grandPiano);
    }

    LOGI() << "Reprise: defaulted " << keys.size() << " imported part(s) to Grand Piano";
}
