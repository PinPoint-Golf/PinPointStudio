/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// Swing-edit popover content — the focused shot's editable metadata: club,
// rating and note. Presentational (bindings only, no model lookups): the host
// (PpShotCarousel) injects `summary` (ShotListModel::shotSummary) and the
// `clubOptions` list, and wires the edit signals back to the active model's
// setClub / setRating / setNote. Replaces the club + rating + note fields of
// the retired PpShotPanel; video and metrics now live on the Review stage.

import QtQuick
import PinPointStudio

Item {
    id: root

    // Focused-shot metadata from ShotListModel::shotSummary(); { valid:false } when none.
    property var summary:     ({})
    // The golf-bag club list backing the picker (ShotListModel.clubOptions).
    property var clubOptions: []

    signal clubChosen(string club)
    signal rated(int value)
    signal noteChanged(string text)
    signal closeRequested()

    implicitWidth:  Theme.sp(288)
    implicitHeight: content.implicitHeight + Theme.sp(27)

    // Re-seed the editable controls from `summary` (the combo index and the note
    // field aren't plain bindings — the combo drives the model on select, and the
    // note field owns its text while editing). Called on load, when the host
    // rebinds `summary` (focus changed / write-through refreshed it), and when the
    // options arrive. The star rating is a pure read binding, so it needs no sync.
    function _sync() {
        const idx = root.clubOptions.indexOf(root.summary.club)
        clubCombo.currentIndex = idx >= 0 ? idx : 0
        noteField.text = (root.summary && root.summary.note !== undefined) ? root.summary.note : ""
    }
    Component.onCompleted:  _sync()
    onSummaryChanged:       _sync()
    onClubOptionsChanged:   _sync()

    Column {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: Theme.gap(14); rightMargin: Theme.gap(14); topMargin: Theme.gap(13) }

        Item {   // header: EDIT SWING · Done
            width: parent.width; height: doneText.implicitHeight
            PpMicro {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: Theme.caps(qsTr("Edit swing"))
            }
            PpLink {
                id: doneText
                anchors.right: parent.right
                text:        qsTr("Done")
                pressMargin: Theme.sp(4)
                onClicked:   root.closeRequested()
            }
        }

        Item { width: 1; height: Theme.sp(3) }

        Text {   // subtitle — #N · hh:mm:ss
            width: parent.width
            text:  (root.summary.ordinal !== undefined ? "#" + root.summary.ordinal : "")
                   + (root.summary.timestampLabel ? "   ·   " + root.summary.timestampLabel : "")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText2
            elide:          Text.ElideRight
        }

        // The card's ⚠, explained: on the film strip it is a glyph and a word with a hover
        // tooltip, and this panel is where the swing is looked at in full, so the words are
        // shown outright (DataWarningFormat — the same wording as that tooltip), set into
        // the panel with a fault's target and bar. The panel is sized by its content, so it
        // grows by exactly this block when the shot warns.
        Item {
            visible: warnBlock.visible
            width: 1; height: Theme.sp(10)
        }
        PpInset {
            id: warnBlock
            objectName: "swingWarning"
            visible: root.summary.dataWarning === true
            width:   parent.width
            bar:     true
            tone:    Theme.colorWarn

            Row {
                id: warnRow
                width:   parent.width
                spacing: Theme.gap(10)

                PpBadge {
                    id: warnBadge
                    kind: "target"
                    tone: Theme.colorWarn
                    size: Theme.sp(16)
                }
                Column {
                    width:   warnRow.width - warnBadge.width - warnRow.spacing
                    spacing: Theme.gap(4)
                    PpMicro {
                        // Centred on the badge, the way a coaching line's first line is.
                        height: warnBadge.height
                        verticalAlignment: Text.AlignVCenter
                        text:  Theme.caps(qsTr("Data warning"))
                        color: Theme.colorWarn
                    }
                    Text {
                        objectName: "swingWarningText"
                        width:          parent.width
                        text:           DataWarningFormat.text(root.summary.dataWarningDetail)
                        wrapMode:       Text.WordWrap
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        lineHeight:     1.3
                        color:          Theme.colorText2
                    }
                    // WHY, per reading (trust stage 6 session 2): the gate behind each fact the warning
                    // names, in the catalogue's words, opening in the Trust panel — or, with the panel
                    // off, here, under the line (PpTrustInlineFor).
                    Column {
                        id: whyCol
                        objectName: "swingWarningWhy"
                        width:   parent.width
                        spacing: Theme.gap(2)
                        property string openSubject: ""
                        readonly property string swingDir: (root.summary && root.summary.swingDir) || ""
                        // one line per subject the warning names: [{ subject, words }]
                        readonly property var lines: {
                            const out = []
                            for (const r of DataWarningFormat.reasons(root.summary.dataWarningDetail)) {
                                const w = TrustWords.words(r.gate)
                                let line = null
                                for (const l of out) if (l.subject === r.subject) line = l
                                if (!line) { line = { subject: r.subject, words: [] }; out.push(line) }
                                if (w && line.words.indexOf(w) < 0) line.words.push(w)
                            }
                            return out.map(l => ({ subject: l.subject, words: l.words.join(", ") }))
                        }
                        visible: swingDir !== "" && lines.length > 0
                        Repeater {
                            model: whyCol.lines
                            delegate: Text {
                                required property var modelData
                                width:          whyCol.width
                                wrapMode:       Text.WordWrap
                                textFormat:     Text.StyledText
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                text: (modelData.words ? modelData.words + "  " : "")
                                      + "<font color='" + Theme.colorAccent + "'>" + qsTr("Why") + " ›</font>"
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -2
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!TrustFocus.open(modelData.subject, whyCol.swingDir))
                                            whyCol.openSubject = whyCol.openSubject === modelData.subject ? "" : modelData.subject
                                    }
                                }
                            }
                        }
                        PpTrustInlineFor {
                            width:    whyCol.width
                            roles:    whyRoles
                            subject:  whyCol.openSubject
                            swingDir: whyCol.swingDir
                            onClosed: whyCol.openSubject = ""
                        }
                        PpTypeset { id: whyRoles }
                    }
                }
            }
        }

        Item { width: 1; height: Theme.sp(15) }

        PpMicro { text: Theme.caps(qsTr("Club")) }
        Item { width: 1; height: Theme.sp(7) }
        PpComboBox {
            id: clubCombo
            width: parent.width
            model: root.clubOptions
            displayFn: ClubFormat.display
            onActivated: (i) => root.clubChosen(root.clubOptions[i])
        }

        Item { width: 1; height: Theme.sp(15) }

        PpMicro { text: Theme.caps(qsTr("Rating")) }
        Item { width: 1; height: Theme.sp(7) }
        PpStarRating {
            interactive: true
            value:       root.summary.rating !== undefined ? root.summary.rating : 0
            starSize:    Theme.sp(22)
            spacing:     Theme.gap(5)
            onRated:     (n) => root.rated(n)
        }

        Item { width: 1; height: Theme.sp(15) }

        PpMicro { text: Theme.caps(qsTr("Note")) }
        Item { width: 1; height: Theme.sp(7) }
        PpTextField {
            id: noteField
            width: parent.width
            placeholderText:   qsTr("Add a note…")
            onEditingFinished: root.noteChanged(text)
        }
    }
}
