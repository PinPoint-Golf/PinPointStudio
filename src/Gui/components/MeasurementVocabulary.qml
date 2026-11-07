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

// THE MEASUREMENT WORDS (docs/design/variability-mark/spec.md §3, §7.2): what a Needle mark and its
// badge say about how far a READING can be trusted. The ONE home for them.
//   grade 5 "Locked" (steadiest) … 1 "Noisy"; 0 "Checking" (not enough data to judge).
// These words describe the measurement, never the golfer: a screen always names the reading beside
// them ("Pelvis IMU · Steady"). A grade is an int 0–5 from the C++ grader; anything else is the
// grade-0 entry, with a console warning.

pragma Singleton

import QtQuick

QtObject {
    //: MeasurementVocabulary: the domain name, in the accessible name "Measurement: Steady (4 of 5)"
    readonly property string domain: qsTr("Measurement")

    // The word for a grade: "Locked", "Steady", …, "Checking".
    function word(grade) {
        switch (_grade(grade)) {
        //: MeasurementVocabulary, grade 5 (steadiest reading): the word. Describes the measurement, never the golfer
        case 5: return qsTr("Locked")
        //: MeasurementVocabulary, grade 4: the word. Describes the measurement, never the golfer
        case 4: return qsTr("Steady")
        //: MeasurementVocabulary, grade 3: the word. Describes the measurement, never the golfer
        case 3: return qsTr("Fair")
        //: MeasurementVocabulary, grade 2: the word. Describes the measurement, never the golfer
        case 2: return qsTr("Unsteady")
        //: MeasurementVocabulary, grade 1 (noisiest reading): the word. Describes the measurement, never the golfer
        case 1: return qsTr("Noisy")
        //: MeasurementVocabulary, grade 0 (not enough data to judge the reading): the word
        default: return qsTr("Checking")
        }
    }

    // The line for a grade (§7.2).
    function line(grade) {
        switch (_grade(grade)) {
        //: MeasurementVocabulary, grade 5 "Locked": the line
        case 5: return qsTr("This reading is rock steady. Even small changes are real.")
        //: MeasurementVocabulary, grade 4 "Steady": the line
        case 4: return qsTr("This reading is reliable. Small noise won't change what it tells you.")
        //: MeasurementVocabulary, grade 3 "Fair": the line
        case 3: return qsTr("This reading is usable. Treat small changes with caution.")
        //: MeasurementVocabulary, grade 2 "Unsteady": the line
        case 2: return qsTr("This reading is noisy. Only large changes mean anything.")
        //: MeasurementVocabulary, grade 1 "Noisy": the line
        case 1: return qsTr("This reading is too noisy to rely on. Check the setup.")
        //: MeasurementVocabulary, grade 0 "Checking": the line
        default: return qsTr("Not enough data yet to judge this reading.")
        }
    }

    // §4.2. `label` is the mark's label (§4.1: the label is used for the accessible name),
    // and defaults to this grade's word. "<domain>: <word> (<grade> of 5)", e.g. "Measurement: Steady (4 of 5)".
    // Grade 0 has no grade to count, so it reads "Measurement: Checking" (R25, Mark 7 Nov 2026).
    function accessibleName(grade, label) {
        const g = _grade(grade)
        if (g === 0)
            //: MeasurementVocabulary: a mark's accessible name at grade 0 ("Checking"), which has no grade to count. %1 the domain ("Measurement"), %2 the word ("Checking")
            return qsTr("%1: %2").arg(domain).arg(label === undefined ? word(0) : label)
        //: MeasurementVocabulary: a Needle mark's accessible name. %1 the domain ("Measurement"), %2 the grade word, %3 the grade 1-5
        return qsTr("%1: %2 (%3 of 5)").arg(domain).arg(label === undefined ? word(g) : label).arg(g)
    }

    // An int 0–5, else 0 with a warning.
    function _grade(grade) {
        if (Number.isInteger(grade) && grade >= 0 && grade <= 5)
            return grade
        console.warn("MeasurementVocabulary: grade", grade, "is not an int 0-5; using grade 0")
        return 0
    }
}
