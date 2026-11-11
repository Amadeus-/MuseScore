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

#include "importprogressmodel.h"

#include <vector>

#include <QFileInfo>
#include <QTimer>
#include <QUrl>

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

#include "internal/audiverisrunner.h"

using namespace mu;
using namespace mu::reprise;
using namespace muse;

//! Reprise-private setting: when checked, the popup closes itself automatically once the import succeeds.
static const Settings::Key CLOSE_POPUP_ON_COMPLETE("reprise", "import/closePopupOnComplete");

//! How long the Done state lingers before an auto-close (ms) -- enough to show the gold check, and to let the
//! just-opened score's master-notation change fire so the Grand-Piano override lands before the model is gone.
static constexpr int AUTO_CLOSE_DELAY_MS = 1400;

ImportProgressModel::ImportProgressModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    settings()->setDefaultValue(CLOSE_POPUP_ON_COMPLETE, Val(true));

    m_runner = std::make_shared<AudiverisRunner>(iocContext());

    QObject::connect(m_runner.get(), &AudiverisRunner::progressChanged, this, &ImportProgressModel::onRunnerProgress);
    QObject::connect(m_runner.get(), &AudiverisRunner::finished, this, &ImportProgressModel::onRunnerFinished);

    //! Apply the Grand-Piano override once the imported score becomes the current master (the open is async).
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

ImportProgressModel::~ImportProgressModel() = default;

bool ImportProgressModel::closePopupOnComplete() const
{
    return settings()->value(CLOSE_POPUP_ON_COMPLETE).toBool();
}

void ImportProgressModel::setClosePopupOnComplete(bool close)
{
    if (close == closePopupOnComplete())
    {
        return;
    }

    settings()->setSharedValue(CLOSE_POPUP_ON_COMPLETE, Val(close));
    emit closePopupOnCompleteChanged();
}

void ImportProgressModel::load(const QString& pdfPath, const QString& pages, int dpi)
{
    m_title = QFileInfo(pdfPath).fileName();
    emit titleChanged();

    setStatus("running");
    m_indeterminate = true;
    m_percent = -1;
    m_stageText = muse::qtrc("reprise", "Starting Audiveris...");
    emit progressChanged();

    const bool started = m_runner->start(io::path_t(pdfPath), pages, dpi);
    if (!started)
    {
        m_errorDetails.clear();
        setStatus("error");
        m_stageText = muse::qtrc("reprise", "Audiveris was not found.");
        emit progressChanged();
    }
}

void ImportProgressModel::cancel()
{
    if (m_runner)
    {
        m_runner->cancel();
    }

    m_pendingGrandPianoReset = false;
    emit closeRequested();
}

void ImportProgressModel::onRunnerProgress(int percent, int sheet, int total, const QString& stageHuman)
{
    m_percent = percent;
    m_indeterminate = (percent < 0);

    if (total > 0 && sheet > 0)
    {
        //: %1 = current page number, %2 = total pages, %3 = a friendly step label (e.g. "reading notes...")
        m_stageText = muse::qtrc("reprise", "Recognizing page %1 of %2 - %3").arg(sheet).arg(total).arg(stageHuman);
    }
    else
    {
        m_stageText = stageHuman;
    }

    emit progressChanged();
}

void ImportProgressModel::onRunnerFinished(bool ok, const QString& outputPath, const QString& error, const QString& tail)
{
    if (!ok)
    {
        //! Surface the message plus a tail of Audiveris output so the user has something actionable.
        m_errorDetails = tail;
        m_stageText = error.isEmpty() ? muse::qtrc("reprise", "Audiveris could not recognise this PDF.") : error;
        emit progressChanged();
        setStatus("error");
        return;
    }

    //! Success: show a full determinate bar, open the score, then flip to the Done state.
    m_percent = 100;
    m_indeterminate = false;
    m_stageText = muse::qtrc("reprise", "Done");
    emit progressChanged();

    openImported(outputPath);

    setStatus("done");

    if (closePopupOnComplete())
    {
        QTimer::singleShot(AUTO_CLOSE_DELAY_MS, this, [this]() { emit closeRequested(); });
    }
}

void ImportProgressModel::openImported(const QString& outputPath)
{
    //! Hand the produced MusicXML to MuseScore's native open path; a .mxl/.xml routes through MusicXmlReader.
    //! Arm the Grand-Piano override for the score about to open, then dispatch (fire-and-forget).
    m_pendingGrandPianoReset = true;

    const QUrl url = QUrl::fromLocalFile(outputPath);
    commandDispatcher()->dispatch(project::PROJECT_OPEN_COMMAND, { { "url", muse::Val(url.toString()) } });
}

void ImportProgressModel::setStatus(const QString& status)
{
    if (status == m_status)
    {
        return;
    }

    m_status = status;
    emit statusChanged();
}

void ImportProgressModel::applyGrandPianoDefault()
{
    //! TEMPORARY Reprise default. Audiveris assigns semi-random instruments to recognised parts; until an intake
    //! step adds real instrument selection, force every imported part to Grand Piano. Mirrors the original
    //! ImportPdfController implementation. Remove when real instrument choice lands.
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
