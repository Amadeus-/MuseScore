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

#include <QString>

#include "irepriseconfiguration.h"

namespace mu::reprise {
//! settings()-backed implementation of IRepriseConfiguration. Keys live under the reprise/* group so upstream
//! merges stay clean. The detection + tessdata resolution are a faithful Qt port of the C# Reprise Recognizer.
class RepriseConfiguration : public IRepriseConfiguration
{
public:
    RepriseConfiguration() = default;

    //! Seed the default values (called from the module's onInit).
    void init();

    muse::io::path_t audiverisPath() const override;
    void setAudiverisPath(const muse::io::path_t& path) override;

    muse::io::path_t tesseractPath() const override;
    void setTesseractPath(const muse::io::path_t& path) override;

    int scanResolution() const override;
    void setScanResolution(int dpi) override;

    bool closePopupOnComplete() const override;
    void setClosePopupOnComplete(bool close) override;

    muse::io::path_t lastImportPdfDir() const override;
    void setLastImportPdfDir(const muse::io::path_t& dir) override;

    muse::io::path_t detectAudiveris() const override;
    muse::io::path_t detectTesseract() const override;

    muse::io::path_t resolveTessdataPrefix(const muse::io::path_t& configured) const override;

private:
    //! C# Recognizer.Which(): scan each PATH directory for <name>, <name>.exe, <name>.bat, <name>.cmd. Empty if none.
    muse::io::path_t which(const QString& name) const;
};
}
