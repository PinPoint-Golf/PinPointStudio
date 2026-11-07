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

// THE CONSISTENCY WORDS (docs/design/variability-mark/spec.md §3, §7.1): what a Dispersion or Overlay
// mark, its badge and the spread funnel say about a grade. This singleton is the ONE home for them:
// no screen, data pack or C++ presenter spells a grade word or line.
//   grade 5 "Grooved" (least variable) … 1 "Scattered" (most); 0 "Not enough swings" (no grade), "Too few" in the
//   Consistency table only (shortWord, R33).
// A grade is an int 0–5 from the C++ grader (variabilityResult.grade); QML never grades. Anything
// else is the grade-0 entry, with a console warning.
//
// The words describe how the GOLFER repeats. Whether a reading can be trusted is
// MeasurementVocabulary's question, never this one's (§1, principle 4).

pragma Singleton

import QtQuick

QtObject {
    //: ConsistencyVocabulary: the domain name, in the accessible name "Consistency: Repeatable (4 of 5)"
    readonly property string domain: qsTr("Consistency")

    // The word for a grade: "Grooved", "Repeatable", …, "Not enough swings".
    function word(grade) {
        switch (_grade(grade)) {
        //: ConsistencyVocabulary, grade 5 (least variable): the word
        case 5: return qsTr("Grooved")
        //: ConsistencyVocabulary, grade 4: the word
        case 4: return qsTr("Repeatable")
        //: ConsistencyVocabulary, grade 3: the word
        case 3: return qsTr("Settling")
        //: ConsistencyVocabulary, grade 2: the word
        case 2: return qsTr("Variable")
        //: ConsistencyVocabulary, grade 1 (most variable): the word
        case 1: return qsTr("Scattered")
        //: ConsistencyVocabulary, grade 0 (too few swings to grade): the word
        default: return qsTr("Not enough swings")
        }
    }

    // The word in the Consistency table only (R33, Mark 7 Nov 2026): grade 0 is "Too few", so the words fit the matrix's
    // cells; grades 1-5 are word()'s. The table's key says what it means. Every other surface keeps word(0).
    function shortWord(grade) {
        const g = _grade(grade)
        if (g !== 0) return word(g)
        //: ConsistencyVocabulary, grade 0 (too few swings to grade) in the Consistency table only, where the cells are narrow; the table's key explains it ("Too few: fewer than eight swings to grade it."). Elsewhere the word is "Not enough swings"
        return qsTr("Too few")
    }

    // The golfer-layer line for a grade (§7.1). The word and this line are all the golfer layer shows.
    function line(grade) {
        switch (_grade(grade)) {
        //: ConsistencyVocabulary, grade 5 "Grooved": the golfer-layer line
        case 5: return qsTr("You repeat this almost exactly, swing after swing.")
        //: ConsistencyVocabulary, grade 4 "Repeatable": the golfer-layer line
        case 4: return qsTr("This stays much the same from one swing to the next.")
        //: ConsistencyVocabulary, grade 3 "Settling": the golfer-layer line
        case 3: return qsTr("This is starting to repeat, with some swings still drifting.")
        //: ConsistencyVocabulary, grade 2 "Variable": the golfer-layer line
        case 2: return qsTr("This changes noticeably from swing to swing.")
        //: ConsistencyVocabulary, grade 1 "Scattered": the golfer-layer line
        case 1: return qsTr("This is different almost every swing.")
        //: ConsistencyVocabulary, grade 0 "Not enough swings": the golfer-layer line
        default: return qsTr("Hit a few more and we'll show how consistent this is.")
        }
    }

    // §4.2. `label` is the mark's label (§4.1: the label is used for the accessible name),
    // and defaults to this grade's word. "<domain>: <word> (<grade> of 5)", e.g. "Consistency: Repeatable (4 of 5)".
    // Grade 0 has no grade to count, so it reads "Consistency: Not enough swings" (R25, Mark 7 Nov 2026).
    function accessibleName(grade, label) {
        const g = _grade(grade)
        if (g === 0)
            //: ConsistencyVocabulary: a mark's accessible name at grade 0, which has no grade to count. %1 the domain ("Consistency"), %2 the word ("Not enough swings")
            return qsTr("%1: %2").arg(domain).arg(label === undefined ? word(0) : label)
        //: ConsistencyVocabulary: a mark's accessible name. %1 the domain ("Consistency"), %2 the grade word, %3 the grade 1-5
        return qsTr("%1: %2 (%3 of 5)").arg(domain).arg(label === undefined ? word(g) : label).arg(g)
    }

    // An int 0–5, else 0 with a warning.
    function _grade(grade) {
        if (Number.isInteger(grade) && grade >= 0 && grade <= 5)
            return grade
        console.warn("ConsistencyVocabulary: grade", grade, "is not an int 0-5; using grade 0")
        return 0
    }
}
