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

// THE TYPICALITY WORDS (docs/design/variability-mark/spec.md §6.2, §7.4, §8.7): how this swing
// compared with the golfer's usual. The ONE home for them. Never a grade, and never praise or
// fault: an unusual swing may be the best one; whether it was good is the corridor's job.
//
// A band is the C++ TypicalityBand (src/Metrics/variability/variability_typicality.h), spelled
//   "usual" (|z| ≤ 1) | "aLittleOff" (1 < |z| ≤ 2) | "unusual" (|z| > 2)
// "" (no band yet) is "" quietly; anything else is "" with a console warning.
//
// The zone labels name the envelope's two bands on the typicality marks: "most swings" (±1 SD) and
// "nearly all" (±2 SD). Never "normal" or "abnormal".

pragma Singleton

import QtQuick

QtObject {
    //: TypicalityVocabulary: the domain name, in the accessible name "Typicality: Your usual". Never graded, so no "(n of 5)"
    readonly property string domain: qsTr("Typicality")

    //: TypicalityVocabulary: the zone label for the inner band of the golfer's usual (±1 SD). Lower case; never "normal"
    readonly property string zoneMost: qsTr("most swings")
    //: TypicalityVocabulary: the zone label for the outer band of the golfer's usual (±2 SD). Lower case; never "abnormal"
    readonly property string zoneNearlyAll: qsTr("nearly all")

    // The word for a band: "Your usual", "A little off", "Unusual".
    function word(band) {
        switch (_band(band)) {
        //: TypicalityVocabulary, usual (this swing within 1 SD of the golfer's usual): the word. Never praise or fault
        case "usual":      return qsTr("Your usual")
        //: TypicalityVocabulary, aLittleOff (between 1 and 2 SD from the golfer's usual): the word. Never praise or fault
        case "aLittleOff": return qsTr("A little off")
        //: TypicalityVocabulary, unusual (more than 2 SD from the golfer's usual): the word. Never praise or fault
        case "unusual":    return qsTr("Unusual")
        default:           return ""
        }
    }

    // The golfer-layer line (§7.4).
    function line(band) {
        switch (_band(band)) {
        //: TypicalityVocabulary, usual "Your usual": the golfer-layer line
        case "usual":      return qsTr("This swing looked like most of your swings.")
        //: TypicalityVocabulary, aLittleOff "A little off": the golfer-layer line
        case "aLittleOff": return qsTr("This swing was a little different from your usual.")
        //: TypicalityVocabulary, unusual "Unusual": the golfer-layer line. Keep "for better or worse": an unusual swing may be the best one
        case "unusual":    return qsTr("This swing was quite different from your usual, for better or worse.")
        default:           return ""
        }
    }

    // §4.2's accessible name without the grade: typicality is never graded (§8.7), so there is no
    // "(n of 5)". `label` is the mark's label (§4.1) and defaults to the band's word:
    // "Typicality: Your usual". With no word (no band yet) the domain stands alone.
    function accessibleName(band, label) {
        const w = label === undefined ? word(band) : label
        if (w === "")
            return domain
        //: TypicalityVocabulary: a typicality mark's accessible name. %1 the domain ("Typicality"), %2 the word ("Your usual"). No grade: typicality is never graded
        return qsTr("%1: %2").arg(domain).arg(w)
    }

    // A known band, else "" (with a warning unless it is "": no band yet, quietly, as TrendVocabulary).
    function _band(band) {
        if (band === "usual" || band === "aLittleOff" || band === "unusual")
            return band
        if (band !== "")
            console.warn("TypicalityVocabulary: band", band, "is not usual | aLittleOff | unusual; no word shown")
        return ""
    }
}
