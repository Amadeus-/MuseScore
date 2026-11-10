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

#include "modularity/ioc.h"
#include "global/iprocess.h"
#include "global/types/ret.h"
#include "global/types/retval.h"
#include "global/io/path.h"

namespace mu::reprise {
//! NOTE: Reprise's single, local OMR path. Runs the Audiveris CLI on a whole PDF (blocking, skeleton step),
//! then locates the exported MusicXML (.mxl, else .xml) to hand to MuseScore's native import.
class AudiverisRunner : public muse::Contextable
{
    muse::GlobalInject<muse::IProcess> process;

public:
    explicit AudiverisRunner(const muse::modularity::ContextPtr& iocCtx)
        : muse::Contextable(iocCtx) {}

    //! Locate the Audiveris executable (Program Files, PATH, common install dirs). Empty if not found.
    muse::io::path_t findAudiveris() const;

    //! Run Audiveris on the whole PDF, blocking. On success returns the produced .mxl (fallback .xml) to import.
    muse::RetVal<muse::io::path_t> run(const muse::io::path_t& pdfPath);

private:
    //! Best-effort detection of a Tesseract tessdata directory to export as TESSDATA_PREFIX. Empty if unknown.
    muse::io::path_t findTessdataPrefix(const muse::io::path_t& audiverisExe) const;

    //! Search a directory (recursively) for the first file named <base>.<ext>, else any *.<ext>. Empty if none.
    muse::io::path_t locateOutput(const muse::io::path_t& dir, const std::string& base, const std::string& ext) const;
};
}
