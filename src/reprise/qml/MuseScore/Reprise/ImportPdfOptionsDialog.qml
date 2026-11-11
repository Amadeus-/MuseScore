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

//! NOTE: Reprise's Import-PDF intake dialog. Ports the C# PdfResolutionDialog (minus "Read as"):
//! a free-form page range and a validated-on-OK resolution. Shown after the PDF is picked and
//! before Audiveris runs. Returns { accepted, pages, dpi } to ImportPdfController via openSync().
StyledDialogView {
    id: root

    //! Set from the opening UriQuery params.
    property string pages: "all"
    property int dpi: 300

    objectName: "ImportPdfOptionsDialog"

    title: qsTrc("reprise", "Import PDF")

    contentWidth: 440
    contentHeight: content.implicitHeight
    margins: 16

    modal: true

    //: Shown when the typed resolution is not a whole number
    readonly property string numberError: qsTrc("reprise", "Please enter a number between 200 and 400.")
    //: Shown when the typed resolution is a number but outside the 200-400 dpi range
    readonly property string rangeError: qsTrc("reprise", "You have selected too low or high a resolution for reading music. Please select between 200 & 400 dpi.")

    function submit() {
        var text = resolutionField.currentText.trim()
        var isInteger = /^[0-9]+$/.test(text)
        if (!isInteger) {
            errorLabel.text = root.numberError
            return
        }

        var value = parseInt(text, 10)
        if (value < 200 || value > 400) {
            errorLabel.text = root.rangeError
            return
        }

        var typedPages = pagesField.currentText.trim()
        root.ret = {
            errcode: 0,
            value: {
                "accepted": true,
                "pages": typedPages.length > 0 ? typedPages : "all",
                "dpi": value
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

            text: qsTrc("reprise", "Please enter the range of pages to open from this PDF.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        Row {
            width: parent.width
            spacing: 8

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

            text: qsTrc("reprise", "Please select the resolution of the PDF contents:")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        Row {
            width: parent.width
            spacing: 8

            TextInputField {
                id: resolutionField

                width: 90

                currentText: String(root.dpi)

                navigation.panel: navPanel
                navigation.order: 2
                navigation.accessible.name: qsTrc("reprise", "Resolution") + " " + resolutionField.currentText

                onTextChanged: function(newTextValue) {
                    resolutionField.currentText = newTextValue
                    errorLabel.text = ""
                }
            }

            StyledTextLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrc("reprise", "dpi  (200-400)")
            }
        }

        StyledTextLabel {
            width: parent.width

            text: qsTrc("reprise", "Higher = better for small/dense notation")
            opacity: 0.6
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        StyledTextLabel {
            id: errorLabel

            width: parent.width

            text: ""
            visible: Boolean(errorLabel.text)
            color: "#B03020"
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
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
