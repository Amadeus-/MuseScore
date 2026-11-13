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

#include "reprisemodule.h"

#include "modularity/ioc.h"
#include "rcommand/icommandsregister.h"
#include "rcommand/icommandsstate.h"
#include "interactive/iinteractiveuriregister.h"

#include "internal/reprisecommandsregister.h"
#include "internal/reprisecommandsstate.h"
#include "internal/importpdfcontroller.h"
#include "internal/screenshotcontroller.h"
#include "internal/irepriseconfiguration.h"
#include "internal/repriseconfiguration.h"
#include "internal/iimportprogresstracker.h"
#include "internal/importprogresstracker.h"
#include "internal/mcpcuration.h"

using namespace mu::reprise;
using namespace muse;
using namespace muse::modularity;

static const std::string mname("reprise");

std::string RepriseModule::moduleName() const
{
    return mname;
}

void RepriseModule::registerExports()
{
    m_configuration = std::make_shared<RepriseConfiguration>();
    globalIoc()->registerExport<IRepriseConfiguration>(mname, m_configuration);

    //! NOTE: Persistent Import-PDF progress snapshot, shared between the transient progress popup (writer) and the
    //! reprise.get-import-progress / reprise.get-import-errors MCP queries (readers). Exported globally so the
    //! QML-instantiated ImportProgressModel can reach it via GlobalInject.
    m_importProgressTracker = std::make_shared<ImportProgressTracker>();
    globalIoc()->registerExport<IImportProgressTracker>(mname, m_importProgressTracker);
}

void RepriseModule::resolveImports()
{
    auto cr = globalIoc()->resolve<muse::rcommand::ICommandsRegister>(mname);
    if (cr)
    {
        cr->reg(std::make_shared<RepriseCommandsRegister>());
    }

    //! NOTE: Register the Import-PDF intake dialog (pages + resolution). Mirrors project's registerQmlUri pattern.
    auto ir = globalIoc()->resolve<muse::interactive::IInteractiveUriRegister>(mname);
    if (ir)
    {
        ir->registerQmlUri(Uri("musescore://reprise/importpdf/options"), "MuseScore.Reprise", "ImportPdfOptionsDialog");

        //! NOTE: Register the recognition progress popup, shown while Audiveris runs asynchronously.
        ir->registerQmlUri(Uri("musescore://reprise/importpdf/progress"), "MuseScore.Reprise", "ImportProgressDialog");
    }
}

void RepriseModule::onInit(const IApplication::RunMode&)
{
    m_configuration->init();
}

void RepriseModule::onAllInited(const IApplication::RunMode& mode)
{
    //! The MCP server only runs in GuiApp; curate there. By onAllInited every module's command register has been
    //! registered (in its resolveImports), so we can wrap them all -- src\ and pristine muse\ alike -- fork-side.
    if (IApplication::RunMode::GuiApp != mode)
    {
        return;
    }

    curateMcpToolset(globalIoc()->resolve<muse::rcommand::ICommandsRegister>(mname));
}

IContextSetup* RepriseModule::newContext(const ContextPtr& ctx) const
{
    return new RepriseContext(ctx);
}

void RepriseContext::registerExports()
{
    m_importPdfController = std::make_shared<ImportPdfController>(iocContext());
    m_screenshotController = std::make_shared<ScreenshotController>(iocContext());
}

void RepriseContext::resolveImports()
{
    //! NOTE: Register the reprise command enable-states so "Import PDF" is enabled with no score open. The command
    //! metadata is registered globally (RepriseModule::resolveImports); the enable-STATE is context-scoped, like project.
    auto cs = ioc()->resolve<muse::rcommand::ICommandsState>(mname);
    if (cs)
    {
        cs->reg(std::make_shared<RepriseCommandsState>());
    }
}

void RepriseContext::onInit(const IApplication::RunMode& mode)
{
    if (IApplication::RunMode::GuiApp != mode)
    {
        return;
    }

    m_importPdfController->init();
    m_screenshotController->init();
}
