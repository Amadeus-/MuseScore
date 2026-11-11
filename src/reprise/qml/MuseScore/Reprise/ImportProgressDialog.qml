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

    //! Brand palette (from the Reprise mark SVG). The popup is a fixed LIGHT card (cream body/footer under the navy
    //! header), independent of the app theme, so its text/track colours are hard-set rather than theme-bound.
    readonly property color brandNavy: "#23264A"
    readonly property color brandNavyDeep: "#191C38"
    readonly property color brandGold: "#C9A24B"
    readonly property color brandGoldLight: "#DCB95F"
    readonly property color brandGoldDark: "#B0862C"
    readonly property color brandCream: "#EFE7D6"          // header "REPRISE" wordmark
    readonly property color cardBody: "#FBF9F3"            // cream body
    readonly property color cardFooter: "#F3EFE2"          // cream footer
    readonly property color cardStroke: "#E4DCC8"          // hairlines / log border on the cream card
    readonly property color inkStep: "#2A2E3F"             // dark body/step text
    readonly property color inkSub: "#8A8D96"              // gray sub-line
    readonly property color inkCaption: "#5A5E6B"          // progress caption row
    readonly property color inkCheck: "#3C4049"            // checkbox label
    readonly property color trackTan: "#E8DFC9"            // progress-bar track

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
        color: root.cardBody

        ColumnLayout {
            id: mainColumn

            width: parent.width
            spacing: 0

            // ===== Branded navy header =====
            Rectangle {
                id: header

                Layout.fillWidth: true
                Layout.preferredHeight: 60

                //! Subtle navy gradient (lighter top -> deeper bottom) for a little depth.
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#2A2E54" }
                    GradientStop { position: 1.0; color: root.brandNavyDeep }
                }

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
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 52
                        Layout.alignment: Qt.AlignVCenter

                        //! The full cream-R + gold-blade + two-notes mark, sized to match the approved mockup's
                        //! 52px header slot. sourceSize is rasterised at 2x so the SVG stays crisp when scaled down.
                        source: "resources/titlebar-mark.svg"
                        sourceSize.width: 104
                        sourceSize.height: 104
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: true
                        spacing: 1

                        StyledTextLabel {
                            text: "REPRISE"
                            color: root.brandGold
                            font.family: "Georgia"
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

                    //! Gold circular success badge, shown immediately left of the X once recognition has finished.
                    Rectangle {
                        id: doneBadge

                        visible: progressModel.status === "done"

                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        Layout.alignment: Qt.AlignVCenter

                        radius: width / 2
                        color: root.brandGold

                        StyledIconLabel {
                            anchors.centerIn: parent
                            iconCode: IconCode.TICK_RIGHT_ANGLE
                            color: root.brandNavyDeep
                            font.pixelSize: 14
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

            // ===== Body (cream) =====
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 16
                Layout.bottomMargin: 14
                spacing: 12

                //! Gray sub-line: "<filename>  ·  pages 1-3  ·  300 dpi", with gold middot separators.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7

                    StyledTextLabel {
                        text: progressModel.title
                        color: root.inkSub
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: 280
                        visible: progressModel.title.length > 0
                    }

                    StyledTextLabel {
                        text: "·"
                        color: root.brandGold
                        font.bold: true
                        visible: progressModel.title.length > 0
                    }

                    StyledTextLabel {
                        text: progressModel.pagesLabel
                        color: root.inkSub
                        horizontalAlignment: Text.AlignLeft
                    }

                    StyledTextLabel {
                        text: "·"
                        color: root.brandGold
                        font.bold: true
                    }

                    StyledTextLabel {
                        text: progressModel.dpi + " " + qsTrc("reprise", "dpi")
                        color: root.inkSub
                        horizontalAlignment: Text.AlignLeft
                    }

                    Item { Layout.fillWidth: true }
                }

                //! Step line (large, dark): running stage / Done-with-counts; red headline in the error state.
                StyledTextLabel {
                    Layout.fillWidth: true
                    text: progressModel.stageText
                    color: progressModel.status === "error" ? "#D64C3C" : root.inkStep
                    font: ui.theme.largeBodyBoldFont
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                }

                // ===== Gold progress bar + caption (hidden in the error state) =====
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    visible: progressModel.status !== "error"

                    Rectangle {
                        id: track

                        Layout.fillWidth: true
                        Layout.preferredHeight: 8

                        radius: height / 2
                        clip: true
                        color: root.trackTan

                        // Determinate gold-gradient fill
                        Rectangle {
                            id: fill
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            radius: parent.radius
                            visible: !progressModel.indeterminate
                            width: parent.width * Math.max(0, Math.min(100, progressModel.percent)) / 100

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: root.brandGoldLight }
                                GradientStop { position: 1.0; color: root.brandGoldDark }
                            }

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

                    //! Caption row: "Page X of N" (left) + "<pct>%" (right).
                    RowLayout {
                        Layout.fillWidth: true

                        StyledTextLabel {
                            text: progressModel.progressCaption
                            color: root.inkCaption
                            horizontalAlignment: Text.AlignLeft
                            visible: progressModel.progressCaption.length > 0
                        }

                        Item { Layout.fillWidth: true }

                        StyledTextLabel {
                            text: progressModel.percent >= 0 ? (progressModel.percent + "%") : ""
                            color: root.inkCaption
                            horizontalAlignment: Text.AlignRight
                            visible: progressModel.percent >= 0
                        }
                    }
                }

                // ===== "Details" expander -> the streamed recognition log (hidden in the error state) =====
                ColumnLayout {
                    id: detailsBlock

                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 8

                    visible: progressModel.status !== "error"

                    property bool expanded: false

                    RowLayout {
                        id: detailsRow

                        Layout.fillWidth: true
                        spacing: 6

                        StyledIconLabel {
                            iconCode: detailsBlock.expanded ? IconCode.SMALL_ARROW_DOWN : IconCode.SMALL_ARROW_RIGHT
                            color: root.brandGold
                            font.pixelSize: 12
                        }

                        StyledTextLabel {
                            text: qsTrc("reprise", "Details")
                            color: root.brandGold
                            font.bold: true
                            horizontalAlignment: Text.AlignLeft
                        }

                        Item { Layout.fillWidth: true }

                        StyledTextLabel {
                            text: detailsBlock.expanded ? qsTrc("reprise", "Hide recognition log")
                                                        : qsTrc("reprise", "Show recognition log")
                            color: root.brandGold
                            horizontalAlignment: Text.AlignRight
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: detailsBlock.expanded = !detailsBlock.expanded
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 130

                        visible: detailsBlock.expanded

                        radius: 4
                        color: "#FFFFFF"
                        border.width: 1
                        border.color: root.cardStroke

                        Flickable {
                            id: logFlick

                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            contentWidth: width
                            contentHeight: logLabel.implicitHeight

                            //! Keep the newest streamed output in view.
                            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

                            StyledTextLabel {
                                id: logLabel
                                width: parent.width
                                text: progressModel.logText
                                color: root.inkStep
                                font: ui.theme.bodyFont
                                horizontalAlignment: Text.AlignLeft
                                wrapMode: Text.Wrap
                                opacity: 0.85
                            }
                        }
                    }
                }

                // ===== Error output tail (retained error state) =====
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    Layout.topMargin: 2

                    visible: progressModel.status === "error" && progressModel.errorDetails.length > 0

                    radius: 4
                    color: "#FFFFFF"
                    border.width: 1
                    border.color: root.cardStroke

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
                            color: root.inkStep
                            font: ui.theme.bodyFont
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.Wrap
                            opacity: 0.85
                        }
                    }
                }
            }

            // ===== Footer (cream) =====
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: footerRow.implicitHeight + 28

                color: root.cardFooter

                //! Hairline separating the footer from the body.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: root.cardStroke
                }

                RowLayout {
                    id: footerRow

                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 14
                    anchors.bottomMargin: 14
                    spacing: 12

                    //! Custom (fixed-light) checkbox so the box + label read correctly on the cream footer
                    //! regardless of the app theme. Default UNCHECKED.
                    Item {
                        id: closeCheck

                        Layout.fillWidth: true
                        implicitHeight: 20

                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Rectangle {
                                id: cbox

                                width: 18
                                height: 18
                                anchors.verticalCenter: parent.verticalCenter

                                radius: 3
                                color: progressModel.closePopupOnComplete ? root.brandGold : "#FFFFFF"
                                border.width: 1
                                border.color: progressModel.closePopupOnComplete ? root.brandGold : "#B9B09A"

                                StyledIconLabel {
                                    anchors.centerIn: parent
                                    iconCode: IconCode.TICK_RIGHT_ANGLE
                                    color: "#FFFFFF"
                                    font.pixelSize: 12
                                    visible: progressModel.closePopupOnComplete
                                }
                            }

                            StyledTextLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTrc("reprise", "Close this window after importing")
                                color: root.inkCheck
                                horizontalAlignment: Text.AlignLeft
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: progressModel.closePopupOnComplete = !progressModel.closePopupOnComplete
                        }
                    }

                    //! Cancel = GOLD (while running).
                    FlatButton {
                        text: qsTrc("global", "Cancel")
                        visible: progressModel.status === "running"

                        normalColor: root.brandGold
                        hoverHitColor: root.brandGoldLight

                        contentItem: Component {
                            StyledTextLabel {
                                text: qsTrc("global", "Cancel")
                                color: root.brandNavyDeep
                                font: ui.theme.bodyFont
                            }
                        }

                        onClicked: progressModel.cancel()
                    }

                    //! Close = NAVY (when finished / error).
                    FlatButton {
                        text: qsTrc("global", "Close")
                        visible: progressModel.status !== "running"

                        normalColor: root.brandNavy
                        hoverHitColor: root.brandNavyDeep

                        contentItem: Component {
                            StyledTextLabel {
                                text: qsTrc("global", "Close")
                                color: "#FFFFFF"
                                font: ui.theme.bodyFont
                            }
                        }

                        onClicked: root.hide()
                    }
                }
            }
        }
    }
}
