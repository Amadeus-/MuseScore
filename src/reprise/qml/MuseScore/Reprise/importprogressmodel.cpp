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
#include "engraving/dom/masterscore.h"

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
    //! Default UNCHECKED: the window stays up after a successful import unless the user opts in.
    settings()->setDefaultValue(CLOSE_POPUP_ON_COMPLETE, Val(false));

    m_runner = std::make_shared<AudiverisRunner>(iocContext());

    QObject::connect(m_runner.get(), &AudiverisRunner::progressChanged, this, &ImportProgressModel::onRunnerProgress);
    QObject::connect(m_runner.get(), &AudiverisRunner::finished, this, &ImportProgressModel::onRunnerFinished);
    QObject::connect(m_runner.get(), &AudiverisRunner::outputLine, this, &ImportProgressModel::appendLog);

    //! Once the imported score becomes the current master (the open is async): apply the Grand-Piano override and
    //! fill the Done line with the real measure count.
    projectSession()->masterNotationChanged().onNotify(this, [this]()
    {
        if (!projectSession()->masterNotation())
        {
            return;
        }

        if (m_pendingGrandPianoReset)
        {
            m_pendingGrandPianoReset = false;
            applyGrandPianoDefault();
        }

        if (m_pendingDoneCounts)
        {
            m_pendingDoneCounts = false;
            updateDoneLine();
        }
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
    m_pages = pages;
    m_dpi = dpi;
    m_pagesLabel = formatPagesLabel(pages);
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
    m_pendingDoneCounts = false;
    emit closeRequested();
}

void ImportProgressModel::onRunnerProgress(int percent, int sheet, int total, const QString& stageHuman)
{
    m_percent = percent;
    m_indeterminate = (percent < 0);

    if (total > 0)
    {
        m_totalPages = total;
    }

    if (sheet > 0)
    {
        m_currentPage = sheet;
    }

    if (total > 0 && sheet > 0)
    {
        //: %1 = current page number, %2 = total pages; an em-dash + a friendly step label (e.g. "reading notes...") follow.
        m_stageText = muse::qtrc("reprise", "Analyzing page %1 of %2").arg(sheet).arg(total)
                      + " " + QChar(0x2014) + " " + stageHuman;
    }
    else
    {
        m_stageText = stageHuman;
    }

    emit progressChanged();
}

QString ImportProgressModel::progressCaption() const
{
    if (m_status == "error")
    {
        return QString();
    }

    const int total = m_totalPages;
    const int current = m_currentPage > 0 ? m_currentPage : (total > 0 ? 1 : -1);

    if (total > 0 && current > 0)
    {
        return muse::qtrc("reprise", "Page %1 of %2").arg(current).arg(total);
    }

    return QString();
}

void ImportProgressModel::appendLog(const QString& line)
{
    if (!m_logText.isEmpty())
    {
        m_logText += '\n';
    }
    m_logText += line;

    //! Cap the retained log so a very long run cannot grow it without bound (keep the most recent output).
    static const int MAX_LOG_CHARS = 40000;
    if (m_logText.size() > MAX_LOG_CHARS)
    {
        m_logText = m_logText.right(MAX_LOG_CHARS);
    }

    emit logTextChanged();
}

QString ImportProgressModel::formatPagesLabel(const QString& pages) const
{
    const QString trimmed = pages.trimmed();
    if (trimmed.isEmpty() || trimmed.compare("all", Qt::CaseInsensitive) == 0)
    {
        return muse::qtrc("reprise", "all pages");
    }

    //! Display hyphenated ranges with an en-dash ("1-3" -> "1<U+2013>3"); comma lists are left as-is.
    QString display = trimmed;
    display.replace('-', QChar(0x2013));

    return muse::qtrc("reprise", "pages %1").arg(display);
}

void ImportProgressModel::updateDoneLine()
{
    const int pages = m_runner ? m_runner->pageCount() : m_totalPages;

    int measures = 0;
    const notation::IMasterNotationPtr master = projectSession()->masterNotation();
    if (master && master->masterScore())
    {
        measures = static_cast<int>(master->masterScore()->nmeasures());
    }

    if (measures > 0 && pages > 0)
    {
        const QString measuresText = (measures == 1)
                                     ? muse::qtrc("reprise", "%1 measure").arg(measures)
                                     : muse::qtrc("reprise", "%1 measures").arg(measures);
        const QString pagesText = (pages == 1)
                                  ? muse::qtrc("reprise", "%1 page").arg(pages)
                                  : muse::qtrc("reprise", "%1 pages").arg(pages);

        //: Done line, e.g. "Done - recognized 21 measures across 3 pages." (%1 = measures, %2 = pages); the dash is an em-dash.
        m_stageText = muse::qtrc("reprise", "Done") + " " + QChar(0x2014) + " "
                      + muse::qtrc("reprise", "recognized %1 across %2").arg(measuresText).arg(pagesText);
    }
    else
    {
        m_stageText = muse::qtrc("reprise", "Done");
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

    //! Success: show a full determinate bar, open the score, then flip to the Done state. The real measure + page
    //! counts are filled by updateDoneLine() once the imported score becomes the current master (see openImported()).
    m_percent = 100;
    m_indeterminate = false;
    const int pages = m_runner ? m_runner->pageCount() : m_totalPages;
    if (pages > 0)
    {
        m_totalPages = pages;
        m_currentPage = pages;
    }
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
    //! Arm the Grand-Piano override and the Done-line measure count for the score about to open, then dispatch.
    m_pendingGrandPianoReset = true;
    m_pendingDoneCounts = true;

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
