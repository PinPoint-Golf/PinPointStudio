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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import PinPointStudio

// "Which cause?" — asked with ONE text field that does both jobs: it narrows the conditions that
// already exist, and it names the one that does not.
//
// ── Why both halves are here rather than in two controls ────────────────────────────────────────
//
// The graph draws a NEIGHBOURHOOD, and a neighbourhood is drawn because its members are connected
// to the focus. So nearly every node on the canvas is already linked to it, and "already linked" is
// a refusal — which leaves a link drag with almost nothing legal to land on, and makes dragging to
// an on-screen node the RARE case rather than the common one. The cause an author actually wants is
// usually off the canvas entirely.
//
// A create-only popover at the end of that drag therefore answered the wrong question: it offered
// the one thing you can always do (make another condition) and withheld the one you usually want
// (name an existing one). The library filling with near-duplicates is the predictable consequence,
// and it is the failure mode that cannot be corrected after the fact because nobody merges them.
//
// So the list comes first and creation is the fallback, which is also the order of the two answers'
// likelihood. The candidates are PRE-FILTERED to legal ones in C++ — no self, no existing edge, no
// cycle — so an illegal claim cannot be constructed here, only refused before it is offered.
ModelTypeAhead {
    id: root

    // The caps line above the field. candidateSource: (searchText) -> [{ id, label, detail,
    // reachLabel }], asked again on every keystroke, so the filtering and the legality both stay in
    // C++. The type-ahead itself is ModelTypeAhead (ModelPicker is the same one with nothing to
    // create); what is here is the create row, the type chips and the words.
    title: qsTr("Add a cause")

    // Creation is offered from the canvas, where the author is standing at a point they chose and
    // "make one here" is meaningful. It is offered from the inspector too, because §7's rule is
    // that the two entrances reach the same verbs.
    property bool allowCreate: true

    // Which way round the claim being made runs. The candidate list is already filtered for the
    // right end by the caller — this only decides the WORDS, and it decides them here rather than
    // at each call site so the two directions cannot drift into two vocabularies.
    property bool upstream: true
    // Only the canvas has somewhere to put a ghost box.
    property bool hasGhost: false
    property real ghostX: 0
    property real ghostY: 0

    signal picked(string id)
    signal created(string objType, string name)

    width: Theme.sp(380)

    property string _type: "characteristics"
    property bool   _took: false

    readonly property bool _naming: root.allowCreate && root.searchText.trim().length > 0

    borderColor:      Theme.colorAccent
    countBottomGap:   Theme.gap(6)
    listMaxHeight:    Theme.sp(220)
    listMinHeight:    Theme.sp(28)
    listBottomMargin: Theme.gap(2)
    detailWidthFraction: 0.36
    footerVisible:    root._naming

    placeholderText: root.allowCreate ? qsTr("Find one, or name a new one…")
                                      : qsTr("Type to narrow…")
    countText: root._candidates.length === 0
                   ? (root.allowCreate ? qsTr("Nothing existing fits — name it and press Enter")
                                       : qsTr("No legal target"))
                   : root.upstream
                         ? qsTr("%n existing condition(s) could cause this", "",
                                root._candidates.length)
                         : qsTr("%n existing condition(s) this could cause", "",
                                root._candidates.length)

    // The list an author navigates: every legal existing condition, and then — only once something
    // has been typed to name it with — the row that makes a new one. One list, so Down/Down/Enter
    // reaches either without the hand leaving the keyboard.
    //
    // The create row is marked rather than merely last: a list where the final entry silently means
    // something different from the rest is a list that will be chosen by accident.
    rows: {
        var out = []
        var c = root._candidates
        for (var i = 0; i < c.length; i++)
            out.push({ kind: "pick", id: c[i].id, label: c[i].label,
                       detail: c[i].detail || "", tone: c[i].reachLabel || "", glyph: "◇" })
        if (root._naming) {
            var name = root.searchText.trim()
            out.push({ kind: "create", id: "", name: name, label: qsTr("Create “%1”").arg(name),
                       detail: "", tone: "", glyph: "＋", glyphAccent: true })
        }
        return out
    }
    // The create row's detail is the type it would make, which the chips change.
    rowDetail: (row) => row.kind === "create" ? root._typeLabel(root._type) : row.detail

    function openAt(canvasX, canvasY, layoutX, layoutY) {
        root.hasGhost = true
        root.ghostX = layoutX - Theme.sp(80)
        root.ghostY = layoutY - Theme.sp(17)
        var pw = root.parent ? root.parent.width  : 0
        var ph = root.parent ? root.parent.height : 0
        root.x = Math.max(Theme.sp(8), Math.min(pw - root.width - Theme.sp(8),
                                                canvasX - root.width / 2))
        root.y = Math.min(ph - Theme.sp(150), canvasY + Theme.sp(26))
        root.open()
    }

    onOpened: {
        root._took = false
        root._type = "characteristics"
    }
    // Dismissed without a choice is a cancelled gesture, and a cancelled gesture is not an error —
    // nothing is said about it, here or anywhere.
    onClosed: { root.hasGhost = false; if (!root._took) root.rejected() }
    signal rejected()

    onChosen: (row) => {
        root._took = true
        if (row.kind === "create") root.created(root._type, row.name)
        else                       root.picked(row.id)
    }

    // A type chip: the shared chip's filled pill, which says "make one of these".
    component Chip: PpChoiceChip {
        required property string key

        style:    "pillFilled"
        selected: root._type === key
        onClicked: {
            root._type = key
            // Picking a type is saying "make one", so the highlight moves to the row
            // that would — otherwise the chip would silently arm an Enter that still
            // took whatever condition happened to be at the top of the list.
            root.currentIndex = root.rows.length - 1
            root.focusField()
        }
    }

    footerData: [
        // The four types §5.3 asks for, and what each one MEANS once it lands: only a condition can
        // be a cause. A screen settles the characteristic, a drill answers it, a measure detects it
        // — so the chips do not change what a cause is, they change what is being made here and how
        // it attaches. Hidden entirely, with the note below, when there is nothing to create.
        RowLayout {
            Layout.fillWidth:   true
            Layout.leftMargin:  Theme.gap(10)
            Layout.rightMargin: Theme.gap(10)
            spacing: Theme.gap(6)

            Chip { label: qsTr("characteristic"); key: "characteristics" }
            Chip { label: qsTr("measure");        key: "measures" }
            Chip { label: qsTr("screen");         key: "screens" }
            Chip { label: qsTr("drill");          key: "drills" }
            Item { Layout.fillWidth: true }
        },

        Text {
            Layout.fillWidth:    true
            Layout.leftMargin:   Theme.gap(12)
            Layout.rightMargin:  Theme.gap(12)
            Layout.bottomMargin: Theme.gap(10)
            text: root._type === "measures"
                      ? qsTr("Measures are minted from their facets — Enter opens that.")
                      : qsTr("Created as a draft, attached, and undone as one step.")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
            wrapMode:       Text.WordWrap
        }
    ]

    function _typeLabel(t) {
        switch (t) {
        case "measures": return qsTr("new measure")
        case "screens":  return qsTr("new screen")
        case "drills":   return qsTr("new drill")
        }
        return qsTr("new characteristic")
    }
}
