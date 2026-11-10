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

#include <memory>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "interactive/iinteractive.h"
#include "rcommand/icommanddispatcher.h"
#include "rcommand/commandable.h"
#include "global/types/ret.h"

#include "audiverisrunner.h"

namespace mu::reprise {
//! Orchestrates Reprise's "Import PDF" flow: pick a PDF -> run Audiveris -> open the recognised score.
class ImportPdfController : public muse::Contextable, public muse::async::Asyncable, public muse::rcommand::Commandable
{
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };

public:
    explicit ImportPdfController(const muse::modularity::ContextPtr& iocCtx)
        : muse::Contextable(iocCtx) {}

    void init();

private:
    muse::Ret importPdf();

    std::shared_ptr<AudiverisRunner> m_audiverisRunner;
};
}
