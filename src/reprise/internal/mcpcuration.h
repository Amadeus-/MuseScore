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
#include <set>
#include <string>
#include <vector>

#include "rcommand/icommandsregister.h"
#include "rcommand/imodulecommandsregister.h"
#include "rcommand/commandtypes.h"

#include "log.h"

namespace mu::reprise {
//! NOTE: Reprise's agent-facing MCP toolset curation. Reprise auto-exposes EVERY registered command as an MCP
//! tool (the `CommandInfo::availabilities` default is `All` which includes the `MCP` bit), so ~450+ mostly
//! destructive editing commands show up. We drop that to a small, curated agent-facing allowlist by clearing
//! ONLY the `MCP` availability bit on every non-allowlisted command. All other bits are left untouched:
//! `Extension` (the QML plugin API) stays so plugins that invoke commands via cmd() keep working, and
//! `Internal`/`Shortcut` stay so the UI + keyboard shortcuts are unaffected. Done entirely fork-side, with no
//! edits to the pristine `muse\` submodule: each module's command register (which lives behind
//! `IModuleCommandsRegister`, src\ OR muse\) is swapped for a wrapper that returns the same command list with
//! the curated availabilities. The wrapper is installed at `onAllInited`, after every module's
//! `resolveImports` has registered its command register.

//! The command PATHS (Uri::path(), i.e. the MCP tool name with '.' replaced by '/') that KEEP the MCP bit.
//! Everything else loses MCP (but keeps Extension/Shortcut/Internal). Paths, not full URIs, so they match the
//! exact field McpController uses to build tool names (mcpcontroller.cpp commandToToolName -> command.path()).
inline const std::set<std::string>& mcpAllowlistPaths()
{
    static const std::set<std::string> s_allow = {
        // read-only state queries (added for agent/MCP testing)
        "project/get-state",
        "score/get-counts",
        "score/get-health",
        "notation/get-editstate",
        "reprise/get-import-progress",
        "reprise/get-import-errors",
        // the OMR entry point (drivable headlessly with a "path" arg)
        "reprise/import-pdf",
        // dev screenshot tool (captures the GUI window or current score to a PNG)
        "reprise/capture-screenshot",
        // dev tool: open the Import-Scan intake dialog for a file and capture it to a PNG
        "reprise/capture-import-dialog",
        // a minimal, safe set of project-level ops for headless testing
        "project/open",
        "project/close",
        "project/save",
        "project/save-as",
        "project/export",
    };
    return s_allow;
}

//! Wraps one module's command register, returning its real command list with the MCP bit cleared on every
//! non-allowlisted command. Everything else (title, description, schemas, decoration, the OTHER availability
//! bits, annotations) is passed through unchanged.
class McpCurationModuleRegister : public muse::rcommand::IModuleCommandsRegister
{
public:
    McpCurationModuleRegister(const muse::rcommand::IModuleCommandsRegisterPtr& orig, const std::set<std::string>& allowPaths)
        : m_orig(orig)
    {
        for (const muse::rcommand::CommandInfo& info : orig->commandInfoList()) {
            muse::rcommand::CommandInfo ci = info;
            if (allowPaths.find(ci.command.path()) == allowPaths.end()) {
                //! Clear ONLY the MCP bit; keep Extension (plugins), Shortcut, Internal, Dispatch.
                ci.availabilities.setFlag(muse::rcommand::Availability::MCP, false);
            }
            m_curated.push_back(ci);
            m_commands.push_back(ci.command);
        }
    }

    std::string moduleName() const override { return m_orig->moduleName(); }
    const std::vector<muse::rcommand::Command>& commandList() const override { return m_commands; }
    const std::vector<muse::rcommand::CommandInfo>& commandInfoList() const override { return m_curated; }
    muse::async::Notification commandListChanged() const override { return m_orig->commandListChanged(); }

private:
    muse::rcommand::IModuleCommandsRegisterPtr m_orig;
    std::vector<muse::rcommand::CommandInfo> m_curated;
    std::vector<muse::rcommand::Command> m_commands;
};

//! Swap every registered module's command register for a curating wrapper. Idempotent (skips already-wrapped
//! modules). Call once, late (onAllInited), when all modules have registered their command registers.
inline void curateMcpToolset(const std::shared_ptr<muse::rcommand::ICommandsRegister>& reg)
{
    if (!reg) {
        return;
    }

    //! Collect all module names up front (via the command->module map) BEFORE mutating the register.
    const std::vector<muse::rcommand::CommandInfo> all = reg->commandInfoList();
    std::set<std::string> moduleNames;
    for (const muse::rcommand::CommandInfo& info : all) {
        const std::string& mn = reg->commandModuleName(info.command);
        if (!mn.empty()) {
            moduleNames.insert(mn);
        }
    }

    size_t wrapped = 0;
    for (const std::string& mn : moduleNames) {
        muse::rcommand::IModuleCommandsRegisterPtr orig = reg->moduleRegister(mn);
        if (!orig) {
            continue;
        }

        //! Already curated (defensive against a double call).
        if (std::dynamic_pointer_cast<McpCurationModuleRegister>(orig)) {
            continue;
        }

        auto wrapper = std::make_shared<McpCurationModuleRegister>(orig, mcpAllowlistPaths());
        reg->unreg(orig);
        reg->reg(wrapper);
        ++wrapped;
    }

    LOGI() << "Reprise: curated MCP toolset across " << wrapped << " module(s); allowlist size "
           << mcpAllowlistPaths().size();
}
}
