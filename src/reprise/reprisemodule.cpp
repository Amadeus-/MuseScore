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

using namespace mu::reprise;
using namespace muse;
using namespace muse::modularity;

static const std::string mname("reprise");

std::string RepriseModule::moduleName() const
{
    return mname;
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
    }
}

IContextSetup* RepriseModule::newContext(const ContextPtr& ctx) const
{
    return new RepriseContext(ctx);
}

void RepriseContext::registerExports()
{
    m_importPdfController = std::make_shared<ImportPdfController>(iocContext());
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
}
