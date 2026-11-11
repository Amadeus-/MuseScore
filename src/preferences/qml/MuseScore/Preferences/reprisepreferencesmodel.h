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

#include <qqmlintegration.h>

#include <QObject>

#include "modularity/ioc.h"
#include "async/asyncable.h"

#include "reprise/internal/irepriseconfiguration.h"

namespace mu::preferences {
//! View-model behind the Reprise Preferences page. Reads/writes the single Reprise configuration (the Audiveris and
//! Tesseract executable paths, the scan resolution, and the "close popup after import" toggle). The Detect buttons
//! call the configuration's ported detection and return the found path to prefill the field.
class ReprisePreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT;

    Q_PROPERTY(QString audiverisPath READ audiverisPath WRITE setAudiverisPath NOTIFY audiverisPathChanged)
    Q_PROPERTY(QString tesseractPath READ tesseractPath WRITE setTesseractPath NOTIFY tesseractPathChanged)
    Q_PROPERTY(int scanResolution READ scanResolution WRITE setScanResolution NOTIFY scanResolutionChanged)
    Q_PROPERTY(bool closePopupOnComplete READ closePopupOnComplete WRITE setClosePopupOnComplete NOTIFY closePopupOnCompleteChanged)

    muse::GlobalInject<mu::reprise::IRepriseConfiguration> repriseConfiguration;

public:
    explicit ReprisePreferencesModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    //! Run the ported detection and return the found executable path (empty if nothing was found), for the Detect
    //! buttons to prefill the field + flash a status.
    Q_INVOKABLE QString detectAudiveris() const;
    Q_INVOKABLE QString detectTesseract() const;

    //! Directory of a file path, used as the FilePicker start folder (mirrors ImportPreferencesModel::fileDirectory).
    Q_INVOKABLE QString fileDirectory(const QString& filePath) const;

    QString audiverisPath() const;
    QString tesseractPath() const;
    int scanResolution() const;
    bool closePopupOnComplete() const;

public slots:
    void setAudiverisPath(QString path);
    void setTesseractPath(QString path);
    void setScanResolution(int dpi);
    void setClosePopupOnComplete(bool close);

signals:
    void audiverisPathChanged(QString audiverisPath);
    void tesseractPathChanged(QString tesseractPath);
    void scanResolutionChanged(int scanResolution);
    void closePopupOnCompleteChanged(bool closePopupOnComplete);
};
}
