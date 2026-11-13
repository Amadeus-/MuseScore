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
#include "async/asyncable.h"
#include "rcommand/icommanddispatcher.h"
#include "rcommand/commandable.h"
#include "global/types/retval.h"
#include "global/iglobalconfiguration.h"
#include "global/io/ifilesystem.h"
#include "global/io/path.h"

#include "ui/imainwindow.h"
#include "project/iprojectsession.h"
#include "project/inotationwritersregister.h"

#include "reprise/reprisecommands.h"

namespace mu::reprise {
//! Dev-only MCP/API tool backing reprise.capture-screenshot. target="window" (default) grabs the whole main
//! application window to a PNG; target="score" renders the current score's page through the registered "png"
//! notation writer. The command handler runs synchronously on the Qt GUI thread (the same dispatch path as
//! score.get-counts, which reads a live engraving::Score), so QQuickWindow::grabWindow() and the PNG writer are
//! invoked directly in the handler -- no invokeMethod / BlockingQueuedConnection needed.
class ScreenshotController : public muse::Contextable, public muse::async::Asyncable, public muse::rcommand::Commandable
{
    muse::ContextInject<muse::ui::IMainWindow> mainWindow = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::ContextInject<mu::project::IProjectSession> projectSession = { this };
    muse::GlobalInject<mu::project::INotationWritersRegister> notationWritersRegister;
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;

public:
    explicit ScreenshotController(const muse::modularity::ContextPtr& iocCtx)
        : muse::Contextable(iocCtx) {}

    void init();

private:
    //! Capture the requested target to a PNG. Always returns ok at the command level (make_ok); a failure to grab /
    //! render / write is reported via Output.ok=false + Output.error so a headless caller gets structuredContent.
    muse::RetVal<CaptureScreenshotCommand::Output> capture(const CaptureScreenshotCommand& cmd);

    //! Writable Reprise screenshots dir: <userAppDataPath>/screenshots (created if missing).
    muse::io::path_t screenshotsDir() const;
};
}
