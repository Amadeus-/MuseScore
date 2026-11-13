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

#include "screenshotcontroller.h"

#include <QImage>
#include <QQuickWindow>
#include <QWindow>

#include "global/log.h"
#include "global/types/val.h"
#include "global/io/file.h"
#include "global/io/iodevice.h"

#include "notation/inotation.h"
#include "project/inotationwriter.h"

#include "reprise/reprisecommands.h"

using namespace mu;
using namespace mu::reprise;
using namespace muse;

void ScreenshotController::init()
{
    //! Explicit std::function selects the exact HasOutput onRequest overload (auto-marshals Output -> structuredContent),
    //! mirroring ImportPdfController's read-only query registration.
    commandDispatcher()->onRequest<CaptureScreenshotCommand>(
        this,
        std::function<muse::RetVal<CaptureScreenshotCommand::Output>(const CaptureScreenshotCommand&)>(
            [this](const CaptureScreenshotCommand& c) { return capture(c); }));

    return;
}

muse::io::path_t ScreenshotController::screenshotsDir() const
{
    const io::path_t dir = globalConfiguration()->userAppDataPath() + "/screenshots";
    fileSystem()->makePath(dir);

    return dir;
}

muse::RetVal<CaptureScreenshotCommand::Output> ScreenshotController::capture(const CaptureScreenshotCommand& cmd)
{
    using RV = muse::RetVal<CaptureScreenshotCommand::Output>;

    CaptureScreenshotCommand::Output out;

    //! Default target is "window" when the arg is omitted/empty.
    const std::string target = cmd.target.empty() ? std::string("window") : cmd.target;

    const io::path_t dir = screenshotsDir();

    if (target == "window")
    {
        const io::path_t absPath = cmd.path.empty() ? (dir + "/mcp-window.png") : io::path_t(cmd.path);

        QWindow* qw = mainWindow() ? mainWindow()->qWindow() : nullptr;
        QQuickWindow* w = qobject_cast<QQuickWindow*>(qw);
        if (!w)
        {
            out.error = "no main window";
            return RV::make_ok(out);
        }

        //! Bring the window forward so the grab is of visible content (best-effort; grab still works if it is exposed).
        w->raise();
        w->requestActivate();

        QImage img = w->grabWindow();
        if (img.isNull())
        {
            out.error = "grab failed (window not visible?)";
            return RV::make_ok(out);
        }

        if (!img.save(absPath.toQString(), "PNG"))
        {
            out.error = "failed to save PNG to " + absPath.toStdString();
            return RV::make_ok(out);
        }

        out.ok = true;
        out.path = absPath.toStdString();
        return RV::make_ok(out);
    }

    if (target == "score")
    {
        const io::path_t absPath = cmd.path.empty() ? (dir + "/mcp-score.png") : io::path_t(cmd.path);

        notation::INotationPtr notation = projectSession()->currentNotation();
        if (!notation)
        {
            out.error = "no score open";
            return RV::make_ok(out);
        }

        project::INotationWriterPtr writer = notationWritersRegister()->writer("png");
        if (!writer)
        {
            out.error = "no PNG writer registered";
            return RV::make_ok(out);
        }

        //! PngWriter is PER_PAGE: one page per call. PAGE_NUMBER is 0-based; the approved default is page 0.
        project::INotationWriter::Options options;
        options[project::INotationWriter::OptionKey::PAGE_NUMBER] = Val(cmd.page);

        io::File file(absPath);
        if (!file.open(io::IODevice::WriteOnly))
        {
            out.error = "could not open output file: " + absPath.toStdString();
            return RV::make_ok(out);
        }

        Ret ret = writer->write(notation, file, options);
        file.close();

        if (!ret)
        {
            out.error = "png writer failed: " + ret.toString();
            return RV::make_ok(out);
        }

        out.ok = true;
        out.path = absPath.toStdString();
        return RV::make_ok(out);
    }

    out.error = "unknown target '" + target + "' (expected 'window' or 'score')";

    return RV::make_ok(out);
}
