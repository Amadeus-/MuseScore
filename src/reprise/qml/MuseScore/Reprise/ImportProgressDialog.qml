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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import MuseScore.Reprise

//! NOTE: Reprise's recognition PROGRESS popup. Shown after the intake dialog's OK; drives the async Audiveris run
//! via ImportProgressModel. Branded navy header (Reprise mark + REPRISE wordmark + title + X), a gold progress bar,
//! a gold success check on the Done state, and an error state that keeps the window up with Audiveris' output tail.
StyledDialogView {
    id: root

    //! Set from the opening UriQuery params (see ImportPdfController::importPdf).
    property string pdfPath: ""
    property string pages: "all"
    property int dpi: 300

    objectName: "ImportProgressDialog"

    //! Brand palette (from the Reprise mark SVG).
    readonly property color brandNavy: "#23264A"
    readonly property color brandNavyDeep: "#191C38"
    readonly property color brandGold: "#C9A24B"
    readonly property color brandCream: "#EFE7D6"

    contentWidth: 468
    contentHeight: mainColumn.implicitHeight
    margins: 0

    modal: true
    frameless: true
    //! A run is only ended by Cancel / Close / the header X -- never by a stray Escape that would orphan Audiveris.
    closeOnEscape: false

    ImportProgressModel {
        id: progressModel

        onCloseRequested: {
            root.hide()
        }
    }

    Component.onCompleted: {
        progressModel.load(root.pdfPath, root.pages, root.dpi)
    }

    //! Header X: during a run it cancels (kills Audiveris, aborts import); otherwise it just closes.
    function headerClose() {
        if (progressModel.status === "running") {
            progressModel.cancel()
        } else {
            root.hide()
        }
    }

    Rectangle {
        id: container

        anchors.fill: parent

        radius: 6
        clip: true
        color: ui.theme.backgroundPrimaryColor

        ColumnLayout {
            id: mainColumn

            width: parent.width
            spacing: 0

            // ===== Branded navy header =====
            Rectangle {
                id: header

                Layout.fillWidth: true
                Layout.preferredHeight: 60

                color: root.brandNavy

                //! Subtle deeper band at the very bottom of the header for a little depth.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    color: root.brandGold
                    opacity: 0.85
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    spacing: 12

                    Image {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        Layout.alignment: Qt.AlignVCenter

                        source: "resources/reprise-mark.svg"
                        sourceSize.width: 68
                        sourceSize.height: 68
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: true
                        spacing: 1

                        StyledTextLabel {
                            text: "REPRISE"
                            color: root.brandCream
                            font.pixelSize: 13
                            font.bold: true
                            font.letterSpacing: 2.5
                            horizontalAlignment: Text.AlignLeft
                        }

                        StyledTextLabel {
                            text: qsTrc("reprise", "Recognizing sheet music")
                            color: "#FFFFFF"
                            font.pixelSize: 16
                            font.bold: true
                            horizontalAlignment: Text.AlignLeft
                        }
                    }

                    // Header close / cancel X
                    Rectangle {
                        id: closeButton

                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        Layout.alignment: Qt.AlignVCenter

                        radius: 4
                        color: closeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : "transparent"

                        StyledIconLabel {
                            anchors.centerIn: parent
                            iconCode: IconCode.CLOSE_X_ROUNDED
                            color: root.brandCream
                            font.pixelSize: 16
                        }

                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.headerClose()
                        }
                    }
                }
            }

            // ===== Body =====
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 18
                Layout.bottomMargin: 14
                spacing: 10

                // File title
                StyledTextLabel {
                    Layout.fillWidth: true
                    text: progressModel.title
                    font: ui.theme.largeBodyBoldFont
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideMiddle
                    visible: progressModel.title.length > 0
                }

                // Current stage line (running) / Done line / error headline
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    StyledIconLabel {
                        visible: progressModel.status === "done"
                        iconCode: IconCode.TICK_RIGHT_ANGLE_THICK
                        color: root.brandGold
                        font.pixelSize: 22
                    }

                    StyledTextLabel {
                        Layout.fillWidth: true
                        text: {
                            if (progressModel.status === "done") {
                                return qsTrc("reprise", "Done")
                            }
                            if (progressModel.status === "error") {
                                return progressModel.stageText
                            }
                            return progressModel.stageText
                        }
                        color: progressModel.status === "error" ? "#D64C3C"
                                                                : (progressModel.status === "done" ? root.brandGold
                                                                                                   : ui.theme.fontPrimaryColor)
                        font: progressModel.status === "done" ? ui.theme.largeBodyBoldFont : ui.theme.bodyFont
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                    }
                }

                // ===== Gold progress bar (hidden in the error state) =====
                Rectangle {
                    id: track

                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    Layout.topMargin: 2

                    visible: progressModel.status !== "error"

                    radius: height / 2
                    clip: true
                    color: Qt.rgba(root.brandNavy.r, root.brandNavy.g, root.brandNavy.b, 0.12)

                    // Determinate fill
                    Rectangle {
                        id: fill
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        radius: parent.radius
                        color: root.brandGold
                        visible: !progressModel.indeterminate
                        width: parent.width * Math.max(0, Math.min(100, progressModel.percent)) / 100

                        Behavior on width {
                            NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
                        }
                    }

                    // Indeterminate sweeping segment
                    Rectangle {
                        id: sweep
                        height: parent.height
                        radius: parent.radius
                        color: root.brandGold
                        visible: progressModel.indeterminate
                        width: parent.width * 0.35

                        SequentialAnimation on x {
                            running: track.visible && progressModel.indeterminate
                            loops: Animation.Infinite
                            NumberAnimation { from: -sweep.width; to: track.width; duration: 1100; easing.type: Easing.InOutQuad }
                        }
                    }
                }

                // ===== Error output tail =====
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    Layout.topMargin: 4

                    visible: progressModel.status === "error" && progressModel.errorDetails.length > 0

                    radius: 4
                    color: ui.theme.textFieldColor
                    border.width: 1
                    border.color: ui.theme.strokeColor

                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 8
                        clip: true
                        contentWidth: width
                        contentHeight: tailLabel.implicitHeight

                        StyledTextLabel {
                            id: tailLabel
                            width: parent.width
                            text: progressModel.errorDetails
                            font: ui.theme.bodyFont
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.Wrap
                            opacity: 0.8
                        }
                    }
                }
            }

            // ===== Footer =====
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: footerRow.implicitHeight + 24

                color: ui.theme.backgroundSecondaryColor

                RowLayout {
                    id: footerRow

                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 12
                    anchors.bottomMargin: 12
                    spacing: 12

                    CheckBox {
                        Layout.fillWidth: true

                        text: qsTrc("reprise", "Close after import complete")
                        checked: progressModel.closePopupOnComplete

                        onClicked: {
                            progressModel.closePopupOnComplete = !progressModel.closePopupOnComplete
                        }
                    }

                    FlatButton {
                        text: qsTrc("global", "Cancel")
                        visible: progressModel.status === "running"
                        onClicked: progressModel.cancel()
                    }

                    FlatButton {
                        text: qsTrc("global", "Close")
                        accentButton: progressModel.status === "done"
                        visible: progressModel.status !== "running"
                        onClicked: root.hide()
                    }
                }
            }
        }
    }
}
