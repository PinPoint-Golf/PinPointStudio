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

// THE VARIABILITY BADGE (docs/design/variability-mark/spec.md §5): a PpVariabilityMark and its word,
// laid out for its form, so a grade is never colour alone (§1, principle 1).
//
//   Icon  the mark at variabilityIconSize (16 in dense lists), a gap of 6, the word in body 12.
//   Cell  the mark at variabilityCellH high (its width, height × 80 / 24, is the mark's own:
//         variabilityCellW at the token sizes), a gap of 10, the word in body2.
// The word wears the tone; at grade 0 it is colorText3 (§4.7). Screen readers hear the mark's
// accessible name ("Consistency: Repeatable (4 of 5)"); the drawn words are ignored, so nothing is
// read twice.
//
// TYPICALITY (§6.2, §7.4). With variant Typicality or TypicalityTrace the badge passes `values` and
// `showCorridor` to its mark and has no grade: the word is the band's (values.band, the C++ grader's:
// "Your usual", "A little off", "Unusual"), always in the tone, the accent (never a status colour and
// never colorText3); the golfer-layer tooltip is the band's line; the technical layer's basis string
// ("this swing 5.2 cm · median 4.8 cm") follows as for any badge. Measurement-limited substitution
// (§8.6) is a consistency rule and never applies to typicality. A TypicalityTrace badge is always laid
// out as a Cell badge, because its mark is always drawn as a Cell (§6.2: no icon form).
//
// LAYERS (§7.5). On the golfer layer the badge is the word and its line: the line is the tooltip.
// On the technical layer the computed basis string (`basisText`, from the grader: "σ 2.1° · 0.42 of
// corridor · n 24") follows the word in data-sm, colorText2, after a " · " separator. The badge never
// builds or edits a basis string; an empty one shows nothing, separator included.
//
// MEASUREMENT-LIMITED (§8.6). When the source's Needle grade is 1–2 the caller sets
// `measurementLimited`. The golfer layer then shows the grade-0 mark with the word "Checking"
// (MeasurementVocabulary's grade 0); the technical layer shows the real grade, and its basis string
// already ends "· measurement-limited". The grade itself never changes (the grader's rule).
//
// TYPESET HOSTS (R17, decisions.md §2). `roles` is the host's PpTypeset, optional, and passes to the
// mark. With roles, on screen and on paper alike, the sizes are roles.u(), the gaps roles.g(), the word
// is set in roles.fBody and the basis in roles.fFig, and the inks are the host's (the tone by grade,
// ink3, ink2), so the badge reads in proportion to the panel's type. Without roles it reads Theme.
//
// PRINT (§9.5.4). `medium` defaults to roles.medium and passes to the mark. The tooltip is screen-only.
// On paper without roles the gaps are sp(), never gap(), so a printed badge does not follow Density.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import PinPointStudio

Item {
    id: badge

    property int    grade: 0
    property int    variant: PpVariabilityMark.Dispersion
    property int    form: PpVariabilityMark.Icon
    property bool   dense: false                 // Icon only: the mark at 16 rather than 20 (§5)
    // "golfer" | "technical" (§7.5). Not `layer`: that is Item's own (FINAL) layer property.
    property string contentLayer: "golfer"
    property string basisText: ""                // technical layer: the grader's basis string (§7.5)
    property bool   measurementLimited: false    // §8.6: the source's Needle grade is 1–2
    // The typicality variants only (§6.2): the mark's per-instance values (with the band) and corridor.
    property var    values: null
    property bool   showCorridor: false
    // The tone of the mark and the word: the shown grade's (§3) unless overridden, as the mark's.
    property color  tone: markItem._toneOf(markItem._grade)
    property var    roles: null                                // a PpTypeset, optional
    property string medium: roles ? roles.medium : "screen"   // "screen" | "print"

    // §8.6: the golfer layer stands a measurement-limited grade down to the grade-0 "Checking" mark.
    readonly property bool substituted: measurementLimited && contentLayer === "golfer" && !markItem._typicality
    readonly property int  shownGrade: substituted ? 0 : grade
    readonly property alias word: markItem.label
    // The golfer-layer line of the word shown (§7): the tooltip. Typicality: the band's line (§7.4).
    readonly property string line: !markItem.vocabulary || typeof markItem.vocabulary.line !== "function" ? ""
                                   : markItem._typicality ? markItem.vocabulary.line(markItem._band)
                                   : markItem.vocabulary.line(markItem._grade)

    // The form as the mark draws it (a TypicalityTrace is always a Cell), so the sizes and gap match it.
    readonly property bool _cell: markItem._cell
    readonly property bool _technical: contentLayer === "technical"
    readonly property bool _print: medium === "print"
    // With the host's roles (R17): its sizes, spaces, type and inks, on screen as on paper.
    readonly property bool _ts: roles !== null && roles !== undefined
    // A size: roles.u() with roles, else Theme.sp().
    function _size(n) { return _ts ? roles.u(n) : Theme.sp(n) }
    // A space: roles.g() with roles (gap() on screen, u() on paper); without roles gap() on screen and
    // sp() on paper, so Density never reaches paper.
    function _space(n) { return _ts ? roles.g(n) : _print ? Theme.sp(n) : Theme.gap(n) }
    // §4.7: grade 0 is colorText3. Typicality has no grade, so its word is always in the tone (§6.2).
    readonly property color _wordInk: !markItem._typicality && markItem._grade === 0 ? (_ts ? roles.ink3 : Theme.colorText3) : tone
    readonly property color _basisInk: _ts ? roles.ink2 : Theme.colorText2

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    Row {
        id: row
        spacing: 0

        PpVariabilityMark {
            id: markItem
            objectName: "variabilityMark"
            anchors.verticalCenter: parent.verticalCenter
            grade: badge.shownGrade
            variant: badge.variant
            form: badge.form
            tone: badge.tone
            values: badge.values
            showCorridor: badge.showCorridor
            roles: badge.roles
            medium: badge.medium
            // §8.6: "Checking" is the measurement word, so the substituted mark speaks Measurement and
            // its accessible name reads "Measurement: Checking (0 of 5)".
            vocabulary: badge.substituted ? MeasurementVocabulary
                      : markItem._typicality ? TypicalityVocabulary
                      : badge.variant === PpVariabilityMark.Needle ? MeasurementVocabulary
                      : ConsistencyVocabulary
            // Icon: variabilityIconSize, 16 in dense lists (§5 gives the dense size as a number, not a
            // token). Cell: variabilityCellH high; the width is the mark's implicit height × 80 / 24.
            size: badge._cell ? badge._size(Theme.variabilityCellH)
                              : badge._size(badge.dense ? 16 : Theme.variabilityIconSize)
        }
        Item {   // the §5 gap: 6 after an Icon, 10 after a Cell
            width: badge._space(badge._cell ? 10 : 6)
            height: 1
        }
        Text {
            id: wordText
            objectName: "variabilityWord"
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true   // the mark's accessible name already says it
            text: markItem.label
            // body 12 (Icon) and body2 (Cell) are the same token: the body face at fontSzBody2. With
            // roles, the host's body role.
            font: badge._ts ? badge.roles.fBody
                            : Qt.font({ family: Theme.fontBody, pixelSize: Theme.fontSzBody2, weight: Theme.fontBodyWeight })
            color: badge._wordInk
            Behavior on color { ColorAnimation { duration: Theme.durationSlow } }
        }
        // The technical layer's basis: a separator, then the grader's string, both in data-sm. The
        // separator is punctuation, not a word, so it stays outside the translatable strings (§7: as
        // the arrow glyphs do).
        Text {
            id: separator
            objectName: "variabilityBasisSeparator"
            anchors.verticalCenter: parent.verticalCenter
            visible: basis.visible
            Accessible.ignored: true
            text: " · "
            font: basis.font
            color: basis.color
        }
        Text {
            id: basis
            objectName: "variabilityBasis"
            anchors.verticalCenter: parent.verticalCenter
            visible: badge._technical && badge.basisText !== ""
            Accessible.ignored: true
            text: badge.basisText
            font: badge._ts ? badge.roles.fFig
                            : Qt.font({ family: Theme.fontData, pixelSize: Theme.fontSzDataSm, letterSpacing: Theme.trackingData })
            color: badge._basisInk
        }
    }

    // The golfer layer's line, in the app's tooltip (PpButton's and PpRowAction's: the Controls
    // attached ToolTip after 400 ms). Never on paper.
    HoverHandler { id: hover; enabled: !badge._technical && !badge._print }
    ToolTip.visible: !badge._technical && !badge._print && hover.hovered && badge.line !== ""
    ToolTip.text:    badge.line
    ToolTip.delay:   400
}
