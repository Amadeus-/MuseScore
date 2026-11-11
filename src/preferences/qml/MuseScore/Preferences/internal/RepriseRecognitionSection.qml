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

import Muse.Ui
import Muse.UiComponents

BaseSection {
    id: root

    title: qsTrc("preferences", "Recognition")

    navigation.direction: NavigationPanel.Both

    property string audiverisPath: ""
    property string tesseractPath: ""
    property string audiverisDir: ""
    property string tesseractDir: ""
    property string audiverisStatus: ""
    property string tesseractStatus: ""

    property int scanResolution: 300
    property bool closePopupOnComplete: false

    readonly property var executableFilter: [qsTrc("preferences", "Executable files") + " (*.exe)"]

    signal audiverisPathChangeRequested(string path)
    signal tesseractPathChangeRequested(string path)
    signal scanResolutionChangeRequested(int dpi)
    signal closePopupOnCompleteChangeRequested(bool value)
    signal detectAudiverisRequested()
    signal detectTesseractRequested()

    Column {
        width: parent.width
        spacing: 6

        StyledTextLabel {
            text: qsTrc("preferences", "Audiveris program (Audiveris.exe)")
            horizontalAlignment: Text.AlignLeft
        }

        Row {
            width: parent.width
            spacing: root.columnSpacing

            FilePicker {
                id: audiverisPicker

                pathFieldWidth: root.columnWidth
                anchors.verticalCenter: parent.verticalCenter

                dialogTitle: qsTrc("preferences", "Choose the Audiveris program")
                filter: root.executableFilter
                dir: root.audiverisDir

                path: root.audiverisPath

                navigation: root.navigation
                navigationRowOrderStart: 0
                navigationColumnOrderStart: 0

                onPathEdited: function(newPath) {
                    root.audiverisPathChangeRequested(newPath)
                }
            }

            FlatButton {
                anchors.verticalCenter: parent.verticalCenter

                text: qsTrc("preferences", "Detect")

                navigation.name: "DetectAudiverisButton"
                navigation.panel: root.navigation
                navigation.row: 0
                navigation.column: 2

                onClicked: {
                    root.detectAudiverisRequested()
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            text: root.audiverisStatus
            visible: Boolean(root.audiverisStatus)
            opacity: 0.6
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
        }
    }

    Column {
        width: parent.width
        spacing: 6

        StyledTextLabel {
            text: qsTrc("preferences", "Tesseract program (tesseract.exe)")
            horizontalAlignment: Text.AlignLeft
        }

        Row {
            width: parent.width
            spacing: root.columnSpacing

            FilePicker {
                id: tesseractPicker

                pathFieldWidth: root.columnWidth
                anchors.verticalCenter: parent.verticalCenter

                dialogTitle: qsTrc("preferences", "Choose the Tesseract program")
                filter: root.executableFilter
                dir: root.tesseractDir

                path: root.tesseractPath

                navigation: root.navigation
                navigationRowOrderStart: 1
                navigationColumnOrderStart: 0

                onPathEdited: function(newPath) {
                    root.tesseractPathChangeRequested(newPath)
                }
            }

            FlatButton {
                anchors.verticalCenter: parent.verticalCenter

                text: qsTrc("preferences", "Detect")

                navigation.name: "DetectTesseractButton"
                navigation.panel: root.navigation
                navigation.row: 1
                navigation.column: 2

                onClicked: {
                    root.detectTesseractRequested()
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            text: root.tesseractStatus
            visible: Boolean(root.tesseractStatus)
            opacity: 0.6
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
        }
    }

    IncrementalPropertyControlWithTitle {
        title: qsTrc("preferences", "Scan resolution")

        columnWidth: root.columnWidth
        spacing: root.columnSpacing
        control.width: 112

        minValue: 200
        maxValue: 400
        currentValue: root.scanResolution
        measureUnitsSymbol: qsTrc("preferences", "dpi")

        navigation.name: "ScanResolutionControl"
        navigation.panel: root.navigation
        navigation.row: 2
        navigation.column: 0

        onValueEdited: function(newValue) {
            root.scanResolutionChangeRequested(newValue)
        }
    }

    CheckBox {
        width: parent.width

        text: qsTrc("preferences", "Close the progress window automatically after a successful import")
        checked: root.closePopupOnComplete

        navigation.name: "ClosePopupOnCompleteBox"
        navigation.panel: root.navigation
        navigation.row: 3

        onClicked: {
            root.closePopupOnCompleteChangeRequested(!checked)
        }
    }
}
