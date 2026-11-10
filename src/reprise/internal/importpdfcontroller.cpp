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

#include <QUrl>

#include "global/translation.h"
#include "global/log.h"

#include "reprise/reprisecommands.h"
#include "project/projectcommands.h"

using namespace mu::reprise;
using namespace muse;

void ImportPdfController::init()
{
    m_audiverisRunner = std::make_shared<AudiverisRunner>(iocContext());

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

    const io::path_t pdfPath = interactive()->selectOpeningFileSync(muse::trc("reprise", "Import PDF"), "",
                                                                    { muse::trc("reprise", "PDF files") + " (*.pdf)" });
    if (pdfPath.empty())
    {
        return make_ret(Ret::Code::Cancel);
    }

    //! Blocking OMR run (skeleton). Async + progress UI is a later step.
    const RetVal<io::path_t> output = m_audiverisRunner->run(pdfPath);
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
    const QUrl url = QUrl::fromLocalFile(output.val.toQString());
    commandDispatcher()->dispatch(project::PROJECT_OPEN_COMMAND, { { "url", muse::Val(url.toString()) } });

    return make_ok();
}
