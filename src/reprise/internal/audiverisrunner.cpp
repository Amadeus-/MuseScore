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

#include "audiverisrunner.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "global/log.h"

using namespace muse;
using namespace mu::reprise;

muse::io::path_t AudiverisRunner::findAudiveris() const
{
    //! Step 1. Default Program Files install.
    static const QString defaultExe = "C:/Program Files/Audiveris/Audiveris.exe";
    if (QFileInfo::exists(defaultExe))
    {
        return io::path_t(defaultExe);
    }

    //! Step 2. On PATH.
    const QString onPath = QStandardPaths::findExecutable("Audiveris");
    if (!onPath.isEmpty())
    {
        return io::path_t(onPath);
    }

    //! Step 3. Other common install locations (exe and launcher variants).
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    QStringList candidates = {
        "C:/Program Files (x86)/Audiveris/Audiveris.exe",
        "C:/Program Files/Audiveris/bin/Audiveris.exe",
        "C:/Program Files/Audiveris/Audiveris.bat",
    };

    if (!localAppData.isEmpty())
    {
        candidates << (QDir::fromNativeSeparators(localAppData) + "/Programs/Audiveris/Audiveris.exe");
    }

    for (const QString& candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return io::path_t(candidate);
        }
    }

    return io::path_t();
}

muse::io::path_t AudiverisRunner::findTessdataPrefix(const muse::io::path_t& audiverisExe) const
{
    //! If the user already configured it, don't second-guess them.
    if (qEnvironmentVariableIsSet("TESSDATA_PREFIX"))
    {
        return io::path_t();
    }

    const QDir exeDir = QFileInfo(audiverisExe.toQString()).absoluteDir();

    QStringList candidates = {
        exeDir.absoluteFilePath("tessdata"),
        exeDir.absoluteFilePath("../tessdata"),
        exeDir.absoluteFilePath("app/tessdata"),
        "C:/Program Files/tesseract-ocr/tessdata",
    };

    const QString programData = qEnvironmentVariable("PROGRAMDATA");
    if (!programData.isEmpty())
    {
        candidates << (QDir::fromNativeSeparators(programData) + "/tessdata");
    }

    for (const QString& candidate : candidates)
    {
        const QDir dir(candidate);
        if (dir.exists() && !dir.entryList(QStringList() << "*.traineddata", QDir::Files).isEmpty())
        {
            return io::path_t(QDir::cleanPath(dir.absolutePath()));
        }
    }

    return io::path_t();
}

muse::io::path_t AudiverisRunner::locateOutput(const muse::io::path_t& dir, const std::string& base, const std::string& ext) const
{
    const QString wanted = QString::fromStdString(base) + "." + QString::fromStdString(ext);
    const QString glob = "*." + QString::fromStdString(ext);

    //! Prefer an exact <base>.<ext> match, anywhere under the output dir.
    QString firstAny;
    QDirIterator it(dir.toQString(), QStringList() << glob, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString path = it.next();
        if (QFileInfo(path).fileName().compare(wanted, Qt::CaseInsensitive) == 0)
        {
            return io::path_t(path);
        }

        if (firstAny.isEmpty())
        {
            firstAny = path;
        }
    }

    return firstAny.isEmpty() ? io::path_t() : io::path_t(firstAny);
}

muse::RetVal<muse::io::path_t> AudiverisRunner::run(const muse::io::path_t& pdfPath)
{
    const io::path_t audiverisExe = findAudiveris();
    if (audiverisExe.empty())
    {
        return RetVal<io::path_t>(make_ret(Ret::Code::NotSupported, "Audiveris executable not found"));
    }

    //! A persistent temp dir: outputs (.mxl, .omr) must survive this call. Durable copy is a later step.
    QTemporaryDir tempDir(QDir::tempPath() + "/reprise-omr-XXXXXX");
    tempDir.setAutoRemove(false);
    if (!tempDir.isValid())
    {
        return RetVal<io::path_t>(make_ret(Ret::Code::InternalError, "Could not create a temporary output directory"));
    }

    const QString outDir = tempDir.path();

    //! Point Tesseract at its training data if we can detect it; otherwise proceed and let Audiveris try its own default.
    const io::path_t tessdata = findTessdataPrefix(audiverisExe);
    if (!tessdata.empty())
    {
        qputenv("TESSDATA_PREFIX", tessdata.toQString().toUtf8());
        LOGI() << "Reprise: TESSDATA_PREFIX set to " << tessdata.toQString();
    }
    else if (!qEnvironmentVariableIsSet("TESSDATA_PREFIX"))
    {
        LOGW() << "Reprise: TESSDATA_PREFIX not set and no tessdata directory detected; running Audiveris without it";
    }

    const std::vector<std::string> args = {
        "-batch",
        "-save",
        "-export",
        "-output", outDir.toStdString(),
        "--", pdfPath.toQString().toStdString()
    };

    LOGI() << "Reprise: running Audiveris: " << audiverisExe.toQString() << " on " << pdfPath.toQString();

    //! Blocking run (skeleton). Async + progress UI is a later step.
    const int code = process()->execute(audiverisExe.toQString().toStdString(), args);
    if (code != 0)
    {
        LOGE() << "Reprise: Audiveris exited with code " << code;
        return RetVal<io::path_t>(make_ret(Ret::Code::InternalError, "Audiveris failed to recognise the PDF (exit code "
                                           + std::to_string(code) + ")"));
    }

    const std::string base = QFileInfo(pdfPath.toQString()).completeBaseName().toStdString();

    io::path_t output = locateOutput(io::path_t(outDir), base, "mxl");
    if (output.empty())
    {
        output = locateOutput(io::path_t(outDir), base, "xml");
    }

    if (output.empty())
    {
        LOGE() << "Reprise: Audiveris produced no .mxl/.xml under " << outDir;
        return RetVal<io::path_t>(make_ret(Ret::Code::InternalError, "Audiveris produced no MusicXML output"));
    }

    LOGI() << "Reprise: Audiveris output located: " << output.toQString();

    return RetVal<io::path_t>::make_ok(output);
}
