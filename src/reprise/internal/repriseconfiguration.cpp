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

#include "repriseconfiguration.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include "settings.h"
#include "global/types/val.h"

using namespace mu;
using namespace mu::reprise;
using namespace muse;

//! Reprise-private settings group, kept out of the "project"/"import" modules so upstream merges stay clean.
static const std::string module_name("reprise");

//! Migrated from the earlier ImportPdfController/ImportProgressModel literals (same key strings -> values carry over).
static const Settings::Key LAST_IMPORT_PDF_DIR(module_name, "application/paths/lastImportPdfDir");
static const Settings::Key SCAN_RESOLUTION(module_name, "import/scanResolution");
static const Settings::Key CLOSE_POPUP_ON_COMPLETE(module_name, "import/closePopupOnComplete");

//! New recognition-path keys (C# parity: both point at the program's .exe).
static const Settings::Key AUDIVERIS_PATH(module_name, "omr/audiverisPath");
static const Settings::Key TESSERACT_PATH(module_name, "omr/tesseractPath");

//! Default scan resolution (dpi). The useful range for notation really starts ~300.
static constexpr int DEFAULT_SCAN_RESOLUTION = 300;

void RepriseConfiguration::init()
{
    settings()->setDefaultValue(SCAN_RESOLUTION, Val(DEFAULT_SCAN_RESOLUTION));
    settings()->setDefaultValue(CLOSE_POPUP_ON_COMPLETE, Val(false));
    settings()->setDefaultValue(AUDIVERIS_PATH, Val(std::string()));
    settings()->setDefaultValue(TESSERACT_PATH, Val(std::string()));
    settings()->setDefaultValue(LAST_IMPORT_PDF_DIR, Val(std::string()));
}

muse::io::path_t RepriseConfiguration::audiverisPath() const
{
    return settings()->value(AUDIVERIS_PATH).toPath();
}

void RepriseConfiguration::setAudiverisPath(const muse::io::path_t& path)
{
    settings()->setSharedValue(AUDIVERIS_PATH, Val(path.toStdString()));
}

muse::io::path_t RepriseConfiguration::tesseractPath() const
{
    return settings()->value(TESSERACT_PATH).toPath();
}

void RepriseConfiguration::setTesseractPath(const muse::io::path_t& path)
{
    settings()->setSharedValue(TESSERACT_PATH, Val(path.toStdString()));
}

int RepriseConfiguration::scanResolution() const
{
    return settings()->value(SCAN_RESOLUTION).toInt();
}

void RepriseConfiguration::setScanResolution(int dpi)
{
    settings()->setSharedValue(SCAN_RESOLUTION, Val(dpi));
}

bool RepriseConfiguration::closePopupOnComplete() const
{
    return settings()->value(CLOSE_POPUP_ON_COMPLETE).toBool();
}

void RepriseConfiguration::setClosePopupOnComplete(bool close)
{
    settings()->setSharedValue(CLOSE_POPUP_ON_COMPLETE, Val(close));
}

muse::io::path_t RepriseConfiguration::lastImportPdfDir() const
{
    return settings()->value(LAST_IMPORT_PDF_DIR).toPath();
}

void RepriseConfiguration::setLastImportPdfDir(const muse::io::path_t& dir)
{
    settings()->setSharedValue(LAST_IMPORT_PDF_DIR, Val(dir.toStdString()));
}

muse::io::path_t RepriseConfiguration::which(const QString& name) const
{
    //! C# Recognizer.Which(): walk the PATH, trying the bare name and the Windows executable/script suffixes.
    const QString pathEnv = qEnvironmentVariable("PATH");
    if (pathEnv.isEmpty())
    {
        return muse::io::path_t();
    }

    const QStringList dirs = pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString& dir : dirs)
    {
        const QStringList candidates = { name, name + ".exe", name + ".bat", name + ".cmd" };
        for (const QString& candidate : candidates)
        {
            const QFileInfo info(QDir(dir).absoluteFilePath(candidate));
            if (info.exists() && info.isFile())
            {
                return muse::io::path_t(QDir::cleanPath(info.absoluteFilePath()));
            }
        }
    }

    return muse::io::path_t();
}

muse::io::path_t RepriseConfiguration::detectAudiveris() const
{
    //! C# Recognizer.DetectOmr(): PATH first (names audiveris, Audiveris, oemer -- kept verbatim for search-order
    //! parity even though Reprise only runs Audiveris), then the standard install folders.
    static const char* const PATH_NAMES[] = { "audiveris", "Audiveris", "oemer" };
    for (const char* const name : PATH_NAMES)
    {
        const muse::io::path_t found = which(QString::fromLatin1(name));
        if (!found.empty())
        {
            return found;
        }
    }

    //! Env bases x relative install layouts, in the C# order.
    static const char* const BASE_VARS[] = { "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "LOCALAPPDATA" };
    static const char* const REL_PATHS[] = {
        "Audiveris/bin/Audiveris.bat",
        "Audiveris/Audiveris.exe",
        "Audiveris/bin/Audiveris.exe",
        "Programs/Audiveris/bin/Audiveris.bat"
    };

    for (const char* const var : BASE_VARS)
    {
        const QString base = qEnvironmentVariable(var);
        if (base.isEmpty())
        {
            continue;
        }

        for (const char* const rel : REL_PATHS)
        {
            const QFileInfo info(QDir(base).absoluteFilePath(QString::fromLatin1(rel)));
            if (info.exists())
            {
                return muse::io::path_t(QDir::cleanPath(info.absoluteFilePath()));
            }
        }
    }

    return muse::io::path_t();
}

muse::io::path_t RepriseConfiguration::detectTesseract() const
{
    //! C# Recognizer.DetectTessdata(): <base>/Tesseract-OCR/tesseract.exe for the three ProgramFiles bases, else PATH.
    static const char* const BASE_VARS[] = { "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432" };
    for (const char* const var : BASE_VARS)
    {
        const QString base = qEnvironmentVariable(var);
        if (base.isEmpty())
        {
            continue;
        }

        const QFileInfo info(QDir(base).absoluteFilePath("Tesseract-OCR/tesseract.exe"));
        if (info.exists())
        {
            return muse::io::path_t(QDir::cleanPath(info.absoluteFilePath()));
        }
    }

    return which("tesseract");
}

muse::io::path_t RepriseConfiguration::resolveTessdataPrefix(const muse::io::path_t& configured) const
{
    //! C# Recognizer.ResolveTessdata(): a file -> its sibling "tessdata"; a folder -> itself when it IS a tessdata
    //! folder, else its "tessdata" child, else the folder itself (back-compat with an old folder-style value).
    const QString c = configured.toQString().trimmed();
    if (c.isEmpty())
    {
        return muse::io::path_t();
    }

    const QFileInfo info(c);
    if (info.exists() && info.isFile())
    {
        const QString sibling = info.absoluteDir().absoluteFilePath("tessdata");
        if (QFileInfo(sibling).isDir())
        {
            return muse::io::path_t(QDir::cleanPath(sibling));
        }

        return muse::io::path_t();
    }

    if (info.exists() && info.isDir())
    {
        const QString cleaned = QDir::cleanPath(c);
        if (QDir(cleaned).dirName().compare("tessdata", Qt::CaseInsensitive) == 0)
        {
            return muse::io::path_t(cleaned);
        }

        const QString child = QDir(cleaned).absoluteFilePath("tessdata");
        if (QFileInfo(child).isDir())
        {
            return muse::io::path_t(QDir::cleanPath(child));
        }

        return muse::io::path_t(cleaned);
    }

    return muse::io::path_t();
}
