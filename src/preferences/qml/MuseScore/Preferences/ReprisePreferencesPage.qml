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
import QtQuick

import Muse.UiComponents
import MuseScore.Preferences

import "internal"

PreferencesPage {
    id: root

    Component.onCompleted: {
        reprisePreferencesModel.load()

        //! Never leave a path blank: auto-detect any path that was never set. A found path is persisted at once
        //! (the model's setters write the shared setting immediately), so it is still there on reopen.
        if (!Boolean(reprisePreferencesModel.audiverisPath)) {
            var audiveris = reprisePreferencesModel.detectAudiveris()
            if (Boolean(audiveris)) {
                reprisePreferencesModel.audiverisPath = audiveris
                recognitionSection.showAudiverisStatus(qsTrc("preferences", "Found: %1").arg(audiveris), true)
            } else {
                recognitionSection.showAudiverisStatus(qsTrc("preferences", "Not detected. Set the path manually."), false)
            }
        }

        if (!Boolean(reprisePreferencesModel.tesseractPath)) {
            var tesseract = reprisePreferencesModel.detectTesseract()
            if (Boolean(tesseract)) {
                reprisePreferencesModel.tesseractPath = tesseract
                recognitionSection.showTesseractStatus(qsTrc("preferences", "Found: %1").arg(tesseract), true)
            } else {
                recognitionSection.showTesseractStatus(qsTrc("preferences", "Not detected. Set the path manually."), false)
            }
        }
    }

    ReprisePreferencesModel {
        id: reprisePreferencesModel
    }

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        RepriseRecognitionSection {
            id: recognitionSection

            audiverisPath: reprisePreferencesModel.audiverisPath
            tesseractPath: reprisePreferencesModel.tesseractPath
            audiverisDir: reprisePreferencesModel.fileDirectory(reprisePreferencesModel.audiverisPath)
            tesseractDir: reprisePreferencesModel.fileDirectory(reprisePreferencesModel.tesseractPath)

            scanResolution: reprisePreferencesModel.scanResolution
            closePopupOnComplete: reprisePreferencesModel.closePopupOnComplete

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart + 1

            onAudiverisPathChangeRequested: function(path) {
                reprisePreferencesModel.audiverisPath = path
                recognitionSection.showAudiverisStatus("", false)
            }

            onTesseractPathChangeRequested: function(path) {
                reprisePreferencesModel.tesseractPath = path
                recognitionSection.showTesseractStatus("", false)
            }

            onScanResolutionChangeRequested: function(dpi) {
                reprisePreferencesModel.scanResolution = dpi
            }

            onClosePopupOnCompleteChangeRequested: function(value) {
                reprisePreferencesModel.closePopupOnComplete = value
            }

            onDetectAudiverisRequested: {
                var found = reprisePreferencesModel.detectAudiveris()
                if (Boolean(found)) {
                    reprisePreferencesModel.audiverisPath = found
                    recognitionSection.showAudiverisStatus(qsTrc("preferences", "Found: %1").arg(found), true)
                } else {
                    recognitionSection.showAudiverisStatus(qsTrc("preferences", "Audiveris was not found. Set the path manually."), false)
                }
            }

            onDetectTesseractRequested: {
                var found = reprisePreferencesModel.detectTesseract()
                if (Boolean(found)) {
                    reprisePreferencesModel.tesseractPath = found
                    recognitionSection.showTesseractStatus(qsTrc("preferences", "Found: %1").arg(found), true)
                } else {
                    recognitionSection.showTesseractStatus(qsTrc("preferences", "Tesseract was not found. Set the path manually."), false)
                }
            }

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }
        }
    }
}
