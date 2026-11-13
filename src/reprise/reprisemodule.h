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

#include "modularity/imodulesetup.h"

namespace mu::reprise {
class ImportPdfController;
class ScreenshotController;
class ImportProgressTracker;
class RepriseConfiguration;

//! NOTE: Reprise fork module. Houses the local-Audiveris "Import PDF" orchestration, kept isolated
//! from upstream modules so that "Update (from upstream)" merges stay clean.
class RepriseModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;

    //! Export the single Reprise configuration globally so the Preferences page view-model (a separate module) and
    //! the reprise-internal consumers all share it via GlobalInject<IRepriseConfiguration>.
    void registerExports() override;

    void resolveImports() override;

    //! Seed the configuration defaults (mirrors the per-module configuration init pattern, e.g. MusicXmlModule).
    void onInit(const muse::IApplication::RunMode& mode) override;

    //! Curate the MCP toolset once every module's command register is registered (clears the MCP availability
    //! bit on all non-allowlisted commands). GuiApp only -- the MCP server only runs there.
    void onAllInited(const muse::IApplication::RunMode& mode) override;

    muse::modularity::IContextSetup* newContext(const muse::modularity::ContextPtr& ctx) const override;

private:
    std::shared_ptr<RepriseConfiguration> m_configuration;
    std::shared_ptr<ImportProgressTracker> m_importProgressTracker;
};

class RepriseContext : public muse::modularity::IContextSetup
{
public:
    RepriseContext(const muse::modularity::ContextPtr& ctx)
        : muse::modularity::IContextSetup(ctx) {}

    void registerExports() override;
    void resolveImports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;

private:
    std::shared_ptr<ImportPdfController> m_importPdfController;
    std::shared_ptr<ScreenshotController> m_screenshotController;
};
}
