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

#include <string>
#include <tuple>

#include "rcommand/commandtypes.h"
#include "rcommand/typedcommand.h"

namespace mu::project {
inline static const muse::rcommand::Command PROJECT_NEW_COMMAND("command://project/new");
inline static const muse::rcommand::Command PROJECT_OPEN_COMMAND("command://project/open");
inline static const muse::rcommand::Command PROJECT_CLOSE_COMMAND("command://project/close");

inline static const muse::rcommand::Command PROJECT_SAVE_COMMAND("command://project/save");
inline static const muse::rcommand::Command PROJECT_SAVE_AS_COMMAND("command://project/save-as");
inline static const muse::rcommand::Command PROJECT_SAVE_A_COPY_COMMAND("command://project/save-a-copy");
inline static const muse::rcommand::Command PROJECT_SAVE_SELECTION_COMMAND("command://project/save-selection");
inline static const muse::rcommand::Command PROJECT_SAVE_TO_CLOUD_COMMAND("command://project/save-to-cloud");
inline static const muse::rcommand::Command PROJECT_SAVE_AT_COMMAND("command://project/save-at");

inline static const muse::rcommand::Command PROJECT_PUBLISH_COMMAND("command://project/publish");
inline static const muse::rcommand::Command PROJECT_SHARE_AUDIO_COMMAND("command://project/share-audio");

inline static const muse::rcommand::Command PROJECT_EXPORT_COMMAND("command://project/export");
inline static const muse::rcommand::Command PROJECT_CONVERT_TO_SCORE_COMMAND("command://project/convert-to-score");

inline static const muse::rcommand::Command PROJECT_PRINT_COMMAND("command://project/print");
inline static const muse::rcommand::Command PROJECT_CLEAR_RECENT_COMMAND("command://project/clear-recent");
inline static const muse::rcommand::Command PROJECT_CONTINUE_LAST_SESSION_COMMAND("command://project/continue-last-session");
inline static const muse::rcommand::Command PROJECT_PROPERTIES_COMMAND("command://project/properties");

//! NOTE Reprise read-only MCP/API query: current project state. Registered directly on the dispatcher (no
//! commands-state gating), so it is always callable and reports isOpen = false when nothing is loaded.
struct GetProjectStateCommand {
    static const inline auto id = muse::rcommand::Command("command://project/get-state");
    static const inline auto title = muse::MnemonicString("project", "Get project state");
    static const inline auto description = muse::TranslatableString("project", "Get the current project state (open, name, path, dirty, cloud)");
    static const inline auto availabilities = muse::rcommand::Availability::API;
    static const inline auto annotations = muse::rcommand::Annotation::ReadOnly;

    struct Output {
        bool isOpen = false;
        std::string displayName;
        std::string path;
        bool isDirty = false;
        bool isCloud = false;
        bool isNewlyCreated = false;
    };

    using O = GetProjectStateCommand::Output;
    static constexpr auto output = std::tuple {
        muse::rcommand::Field{ "isOpen", &O::isOpen, "Whether a project is currently open" },
        muse::rcommand::Field{ "displayName", &O::displayName, "Display name of the open project" },
        muse::rcommand::Field{ "path", &O::path, "Filesystem path of the open project (empty if unsaved/new)" },
        muse::rcommand::Field{ "isDirty", &O::isDirty, "Whether the project has unsaved changes" },
        muse::rcommand::Field{ "isCloud", &O::isCloud, "Whether the project is a cloud project" },
        muse::rcommand::Field{ "isNewlyCreated", &O::isNewlyCreated, "Whether the project was newly created and not yet saved" },
    };
};
}
