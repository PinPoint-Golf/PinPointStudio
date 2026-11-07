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

// THE TREND WORDS (docs/design/variability-mark/spec.md §7.3, §8.8): is the golfer getting more
// consistent session by session? The ONE home for them. The trend word always FOLLOWS the grade
// word on a surface, never replaces it.
//
// A direction is the C++ TrendDirection (src/Metrics/variability/variability_trend.h), spelled
//   "tightening" | "loosening" | "holding"
// and "insufficient" (too few sessions, or an ordinal series): no trend to show, so every function
// returns "" for it, quietly, as it does for "". Anything else is "" with a console warning.
//
// The arrow is a glyph, not a word, and sits outside every translatable string (§7): ↑ tightening
// (spread falling), ↓ loosening (spread rising), → holding.

pragma Singleton

import QtQuick
import PinPointStudio

QtObject {
    // The word alone, no arrow: "Tightening", "Loosening", "Holding".
    function word(direction) {
        switch (_direction(direction)) {
        //: TrendVocabulary, tightening (spread falling by more than the noise): the word, shown after an up arrow
        case "tightening": return qsTr("Tightening")
        //: TrendVocabulary, loosening (spread rising by more than the noise): the word, shown after a down arrow
        case "loosening":  return qsTr("Loosening")
        //: TrendVocabulary, holding (no change beyond the noise): the word, shown after a right arrow
        case "holding":    return qsTr("Holding")
        default:           return ""
        }
    }

    // The arrow glyph alone: "↑", "↓", "→".
    function arrow(direction) {
        switch (_direction(direction)) {
        case "tightening": return "↑"
        case "loosening":  return "↓"
        case "holding":    return "→"
        default:           return ""
        }
    }

    // The arrow and the word: "↑ Tightening".
    function label(direction) {
        const w = word(direction)
        return w ? arrow(direction) + " " + w : ""
    }

    // The golfer-layer line (§7.3).
    function line(direction) {
        switch (_direction(direction)) {
        //: TrendVocabulary, tightening "Tightening": the golfer-layer line
        case "tightening": return qsTr("This is becoming more repeatable session by session.")
        //: TrendVocabulary, loosening "Loosening": the golfer-layer line
        case "loosening":  return qsTr("This is repeating less than it was.")
        //: TrendVocabulary, holding "Holding": the golfer-layer line
        case "holding":    return qsTr("No real change in how consistently you do this.")
        default:           return ""
        }
    }

    // §6.1: what the spread funnel says in place of a trend while the latest unbroken run holds fewer
    // than TREND_MIN_SESSIONS sessions: "Trend after 3 sessions". The count is the grader's constant
    // (Variability.trendMinSessions), never a number typed here.
    //: TrendVocabulary: shown beside the grade's badge while too few sessions (%n, always 3 today) have been graded for a trend
    readonly property string tooFewSessions: qsTr("Trend after %n sessions", "", Variability.trendMinSessions)

    // A known direction, else "" (with a warning unless it is the no-trend "insufficient" or "").
    function _direction(direction) {
        if (direction === "tightening" || direction === "loosening" || direction === "holding")
            return direction
        if (direction !== "insufficient" && direction !== "")
            console.warn("TrendVocabulary: direction", direction,
                         "is not tightening | loosening | holding | insufficient; no trend shown")
        return ""
    }
}
