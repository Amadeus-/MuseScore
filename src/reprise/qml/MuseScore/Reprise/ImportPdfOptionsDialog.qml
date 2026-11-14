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

//! NOTE: Reprise's Import-PDF intake dialog. A free-form page range plus a 3-tier "Recognition quality" selector
//! (Faster/Standard/Best -> Audiveris pdfResolution 200/300/400). Shown after the PDF is picked and before Audiveris
//! runs. The controller auto-detects the PDF's native resolution and passes in a pre-selected tier + a "matched" flag
//! (true only when a real embedded-image resolution was found). Returns { accepted, pages, dpi } via openSync().
StyledDialogView {
    id: root

    //! Set from the opening UriQuery params. "tier" is the controller's pre-selected quality (200/300/400, already
    //! snapped); "matched" is true when it came from a successful auto-detect (drives the badge).
    property string pages: "all"
    property int tier: 300
    property bool matched: false

    //! True when the picked file is an image (not a PDF): the controller derives this from the file extension. When set,
    //! the recognition-quality tier is always hidden (an image has no PDF resolution to pick); submit() keeps the
    //! seeded default tier 300.
    property bool isImage: false

    //! Frame count of an image input (0/irrelevant for a PDF). A single image is one sheet, but a multi-frame TIFF is
    //! several, so the page-range field is shown when !isImage (PDF) OR the image has more than one frame; it is hidden
    //! only for a genuine single-page image, in which case submit() defaults pages to "all".
    property int imagePageCount: 0

    //! Whether the page-range controls (the instruction label + the "Pages:" row) are shown. See imagePageCount.
    readonly property bool showPageRange: !root.isImage || root.imagePageCount > 1

    objectName: "ImportPdfOptionsDialog"

    title: qsTrc("reprise", "Import Scan")

    contentWidth: 440
    contentHeight: content.implicitHeight
    margins: 16

    modal: true

    //! The quality tier the control is currently on. Seeded by snapping the incoming "tier" (defensive: a stored
    //! free-form scanResolution like 350 must map to the nearest tier, never be assumed to already be 200/300/400).
    property int selectedTier: root.snapTier(root.tier)

    //! The tier the auto-detect matched to; the badge is pinned here regardless of what the user later picks.
    readonly property int detectedTier: root.snapTier(root.tier)

    readonly property var tierOptions: [
        { "value": 200, "title": qsTrc("reprise", "Faster (200 dpi)"), "subtitle": qsTrc("reprise", "Quicker, less detail") },
        { "value": 300, "title": qsTrc("reprise", "Standard (300 dpi)"), "subtitle": qsTrc("reprise", "Recommended for most scores") },
        { "value": 400, "title": qsTrc("reprise", "Best (400 dpi)"), "subtitle": qsTrc("reprise", "Slower, most detail") }
    ]

    //! Nearest of {200, 300, 400} to an arbitrary value (mirrors the C++ snapToTier()).
    function snapTier(value) {
        var tiers = [200, 300, 400]
        var best = tiers[0]
        var bestDist = Math.abs(value - best)
        for (var i = 1; i < tiers.length; ++i) {
            var dist = Math.abs(value - tiers[i])
            if (dist < bestDist) {
                bestDist = dist
                best = tiers[i]
            }
        }
        return best
    }

    function submit() {
        var typedPages = pagesField.currentText.trim()
        root.ret = {
            errcode: 0,
            value: {
                "accepted": true,
                "pages": typedPages.length > 0 ? typedPages : "all",
                "dpi": root.selectedTier
            }
        }
        root.hide()
    }

    function dismiss() {
        root.ret = { errcode: 0, value: { "accepted": false } }
        root.hide()
    }

    onNavigationActivateRequested: {
        pagesField.navigation.requestActive()
    }

    Column {
        id: content

        width: parent.width
        spacing: 12

        StyledTextLabel {
            width: parent.width

            //! Page-range instruction: shown for a PDF or a multi-frame TIFF; hidden for a single-page image.
            visible: root.showPageRange

            text: qsTrc("reprise", "Please enter the range of pages to open from this scan.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        Row {
            width: parent.width
            spacing: 8

            //! Page-range picker: shown for a PDF or a multi-frame TIFF; hidden for a single-page image (treated as one
            //! page; submit() defaults pages to "all").
            visible: root.showPageRange

            StyledTextLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrc("reprise", "Pages:")
            }

            TextInputField {
                id: pagesField

                width: 160

                currentText: root.pages
                hint: qsTrc("reprise", "all, 1-5, 1,3,8")

                navigation.panel: navPanel
                navigation.order: 1
                navigation.accessible.name: qsTrc("reprise", "Pages") + " " + pagesField.currentText

                onTextChanged: function(newTextValue) {
                    pagesField.currentText = newTextValue
                }
            }

            StyledTextLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrc("reprise", "(e.g. all, 1-5, 1,3,8)")
                opacity: 0.6
            }
        }

        StyledTextLabel {
            width: parent.width

            //! Recognition-quality label: PDF-only (an image has no PDF resolution to pick). Hidden for an image.
            visible: !root.isImage

            text: qsTrc("reprise", "Recognition quality")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        RadioButtonGroup {
            id: tierGroup

            //! Recognition-quality tier picker: PDF-only; hidden for a single image (submit() keeps the default 300).
            visible: !root.isImage

            width: parent.width
            spacing: 10
            orientation: ListView.Vertical

            model: root.tierOptions

            delegate: Item {
                id: tierItem

                required property var modelData
                required property int index

                width: tierGroup.width
                height: tierRow.implicitHeight

                readonly property bool isMatchedTier: root.matched && tierItem.modelData.value === root.detectedTier

                Row {
                    id: tierRow

                    width: parent.width
                    spacing: 8

                    RoundedRadioButton {
                        id: tierRadio

                        anchors.verticalCenter: parent.verticalCenter

                        checked: root.selectedTier === tierItem.modelData.value
                        text: tierItem.modelData.title

                        navigation.panel: navPanel
                        navigation.order: 2 + tierItem.index
                        navigation.accessible.name: tierItem.modelData.title + ". " + tierItem.modelData.subtitle
                            + (tierItem.isMatchedTier ? ". " + qsTrc("reprise", "Matched to your scan") : "")

                        onToggled: root.selectedTier = tierItem.modelData.value
                    }

                    StyledTextLabel {
                        anchors.verticalCenter: parent.verticalCenter

                        text: tierItem.modelData.subtitle
                        opacity: 0.6
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter

                        visible: tierItem.isMatchedTier
                        radius: 3
                        color: ui.theme.accentColor
                        height: badgeLabel.implicitHeight + 4
                        width: badgeLabel.implicitWidth + 12

                        StyledTextLabel {
                            id: badgeLabel

                            anchors.centerIn: parent
                            text: qsTrc("reprise", "Matched to your scan")
                            color: "#FFFFFF"
                        }
                    }
                }
            }
        }

        ButtonBox {
            id: buttonBox

            width: parent.width

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 3

            FlatButton {
                text: qsTrc("global", "Cancel")

                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.CustomButton + 1

                onClicked: root.dismiss()
            }

            FlatButton {
                text: qsTrc("global", "OK")
                accentButton: true

                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.CustomButton + 2

                onClicked: root.submit()
            }
        }
    }

    NavigationPanel {
        id: navPanel

        name: "ImportPdfOptions"
        section: root.navigationSection
        order: 1
        direction: NavigationPanel.Vertical
    }
}
