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

#include <vector>

#include <QObject>
#include <QStringList>
#include <QString>

#include "modularity/ioc.h"
#include "global/io/path.h"

class QProcess;

namespace mu::reprise {
//! NOTE: Reprise's single, local OMR path. Drives the Audiveris CLI on a whole PDF ASYNCHRONOUSLY via a raw
//! Qt QProcess in signal mode (start() + readyReadStandardOutput + finished), so a live progress popup and a
//! working Cancel are possible without blocking the UI thread. Progress is parsed out of Audiveris' own stdout
//! (StepMonitoring stages + the "N sheets" total + the "#<sheet>" index). On success it reports the produced
//! MusicXML (.mxl, else .xml) to hand to MuseScore's native import.
class AudiverisRunner : public QObject, public muse::Contextable
{
    Q_OBJECT

public:
    explicit AudiverisRunner(const muse::modularity::ContextPtr& iocCtx, QObject* parent = nullptr)
        : QObject(parent), muse::Contextable(iocCtx) {}

    //! Locate the Audiveris executable (Program Files, PATH, common install dirs). Empty if not found.
    muse::io::path_t findAudiveris() const;

    //! True once start() has launched a process that has not yet finished/been cancelled.
    bool isRunning() const;

    //! Begin an async Audiveris run. pages: free-form range ("all"/"" = every page, "3-7", "1,4,6-9");
    //! dpi: scan resolution passed to Audiveris. Progress arrives via progressChanged(); the outcome via
    //! finished(). Returns false (and emits no signals) only if Audiveris itself could not be found.
    bool start(const muse::io::path_t& pdfPath, const QString& pages, int dpi);

    //! Kill the running Audiveris process and abandon the run. finished() is NOT emitted after a cancel().
    void cancel();

signals:
    //! percent: 0-100 determinate, or -1 while the sheet total is still unknown (indeterminate/animated bar).
    //! sheet/total: 1-based current sheet and the total (both -1 until known). stageHuman: a friendly, already
    //! localised-at-source step label (e.g. "detecting staves...").
    void progressChanged(int percent, int sheet, int total, const QString& stageHuman);

    //! ok=true -> outputPath is the produced .mxl/.xml. ok=false -> error is a short message and tail holds the
    //! last lines of Audiveris output (stdout+stderr) for surfacing in the popup's error state.
    void finished(bool ok, const QString& outputPath, const QString& error, const QString& tail);

private:
    //! Expand a free-form page range into explicit 1-based sheet numbers for Audiveris '-sheets'. Empty = all pages.
    std::vector<int> expandSheets(const QString& pages) const;

    //! Best-effort detection of a Tesseract tessdata directory to export as TESSDATA_PREFIX. Empty if unknown.
    muse::io::path_t findTessdataPrefix(const muse::io::path_t& audiverisExe) const;

    //! Search a directory (recursively) for the first file named <base>.<ext>, else any *.<ext>. Empty if none.
    muse::io::path_t locateOutput(const muse::io::path_t& dir, const std::string& base, const std::string& ext) const;

    //! Drain + parse new stdout; drives progressChanged().
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onFinished(int exitCode);
    void onErrorOccurred();

    //! Parse a single Audiveris output line for the sheet total, the current sheet, and the current step.
    void parseLine(const QString& line);

    //! Map an Audiveris step name (LOAD/BINARY/GRID/HEADS/...) to a friendly label; raw (lowercased) as fallback.
    QString humanStage(const QString& rawStep) const;

    //! Compute 0-100 from the current sheet + step position within the known step order; -1 if total unknown.
    int computePercent() const;

    //! Keep only the last N lines (a small ring) for the error tail.
    void pushTail(const QString& line);

    QProcess* m_process = nullptr;
    muse::io::path_t m_pdfPath;
    QString m_outDir;
    QString m_stdoutBuffer;

    int m_totalSheets = -1;
    int m_currentSheet = -1;
    QString m_currentStep;

    QStringList m_tail;
    bool m_cancelled = false;
};
}
