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
#include "reprisepreferencesmodel.h"

#include "io/path.h"

using namespace mu::preferences;

ReprisePreferencesModel::ReprisePreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ReprisePreferencesModel::load()
{
}

QString ReprisePreferencesModel::detectAudiveris() const
{
    return repriseConfiguration()->detectAudiveris().toQString();
}

QString ReprisePreferencesModel::detectTesseract() const
{
    return repriseConfiguration()->detectTesseract().toQString();
}

QString ReprisePreferencesModel::fileDirectory(const QString& filePath) const
{
    return muse::io::dirpath(filePath.toStdString()).toQString();
}

QString ReprisePreferencesModel::audiverisPath() const
{
    return repriseConfiguration()->audiverisPath().toQString();
}

QString ReprisePreferencesModel::tesseractPath() const
{
    return repriseConfiguration()->tesseractPath().toQString();
}

int ReprisePreferencesModel::scanResolution() const
{
    return repriseConfiguration()->scanResolution();
}

bool ReprisePreferencesModel::closePopupOnComplete() const
{
    return repriseConfiguration()->closePopupOnComplete();
}

void ReprisePreferencesModel::setAudiverisPath(QString path)
{
    if (path == audiverisPath())
    {
        return;
    }

    repriseConfiguration()->setAudiverisPath(path.toStdString());
    emit audiverisPathChanged(path);
}

void ReprisePreferencesModel::setTesseractPath(QString path)
{
    if (path == tesseractPath())
    {
        return;
    }

    repriseConfiguration()->setTesseractPath(path.toStdString());
    emit tesseractPathChanged(path);
}

void ReprisePreferencesModel::setScanResolution(int dpi)
{
    if (dpi == scanResolution())
    {
        return;
    }

    repriseConfiguration()->setScanResolution(dpi);
    emit scanResolutionChanged(dpi);
}

void ReprisePreferencesModel::setClosePopupOnComplete(bool close)
{
    if (close == closePopupOnComplete())
    {
        return;
    }

    repriseConfiguration()->setClosePopupOnComplete(close);
    emit closePopupOnCompleteChanged(close);
}
