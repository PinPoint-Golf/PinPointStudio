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

import QtQuick
import QtTest
import PinPointStudio

// One shot card in the carousel — what it SAYS a shot is.
//
// THE CARD IS WHERE A SHOT ANNOUNCES ITS PROVENANCE, and the component says so itself:
// "'IMU ONLY' on a shot with no IMU would be a small lie in the one place a reader looks
// to find out what produced it." That sentence is a contract, and it is the oracle for
// most of this file — the card must not claim a witness the shot did not have.
//
// The same goes for the ⚠: the badge exists because "the recording is known broken, the
// tooltip says which, and the session assessment leaves the shot out". A badge that names
// the wrong fault, or names one on a clean shot, is worse than no badge — it is the only
// thing standing between a broken recording and a golfer trusting the numbers on it.
//
// None of these assertions were obtained by rendering the card and recording what appeared.
// They are the component's own stated rules, pressed with the inputs the pipeline can
// actually produce (ShotProcessor::maybeJoin passes hasVideo = exportOk, and metrics = {}
// whenever the analysis did not succeed).
Item {
    id: probe
    width: 400; height: 320

    property int  ratedCount: 0
    property int  lastRating: -1

    PpShotCard {
        id: card
        anchors.centerIn: parent

        shotId:          101
        ordinal:         7
        timestampLabel:  "14:31"
        club:            "7 iron"
        hasVideo:        true
        thumbnailSource: ""
        tracePoints:     []
        score:           82
        rating:          3
        note:            ""
        metrics:         ({})
        analysisDetail:  ({})
        swingDir:        "/s/swing_0007"
        dataWarning:     false
        dataWarningDetail: ({})

        onRated: (newValue) => { probe.ratedCount++; probe.lastRating = newValue }
    }

    TestCase {
        name: "ShotCard"
        when: windowShown

        function init() {
            card.hasVideo          = true
            card.metrics           = ({})
            card.ordinal           = 7
            card.rating            = 3
            card.dataWarning       = false
            card.dataWarningDetail = ({})
            probe.ratedCount       = 0
            probe.lastRating       = -1
        }

        function label()  { return findChild(card, "provenanceLabel") }
        function badge()  { return findChild(card, "dataWarnBadge") }
        function chip()   { return findChild(card, "ordinalText") }

        // ── the ordinal is the number the user is told ──────────────────────────
        //
        // shot_list_model.h: "A notification saying 'Shot 7' must mean the row the user
        // can point at." The chip is that row.
        function test_the_chip_shows_the_shots_ordinal() {
            const c = chip()
            verify(c !== null)
            compare(c.text, "#7")
            card.ordinal = 12
            compare(c.text, "#12")
        }

        // ── a video shot claims nothing ─────────────────────────────────────────
        function test_a_recorded_shot_carries_no_provenance_label() {
            card.hasVideo = true
            const l = label()
            verify(l !== null)
            verify(!l.visible)
        }

        // ── a monitor-only shot says so ─────────────────────────────────────────
        function test_a_monitor_only_shot_is_labelled_monitor_only() {
            card.hasVideo = false
            card.metrics  = ({ "lm.ballSpeed": { label: "Ball", value: 141 },
                               "lm.carry":     { label: "Carry", value: 168 } })
            verify(card.deviceOnly)
            const l = label()
            verify(l.visible)
            compare(l.text, "MONITOR ONLY")
        }

        // ── a mixed shot is not monitor-only ────────────────────────────────────
        //
        // One non-lm. metric means something else saw the swing, so the monitor was
        // not its only witness.
        function test_a_shot_with_any_non_monitor_metric_is_not_monitor_only() {
            card.hasVideo = false
            card.metrics  = ({ "lm.ballSpeed":  { label: "Ball", value: 141 },
                               "wrist.extAtTop": { label: "Ext", value: 21 } })
            verify(!card.deviceOnly)
        }

        // ── a recorded shot is never monitor-only, whatever it carries ──────────
        function test_a_recorded_shot_is_never_monitor_only() {
            card.hasVideo = true
            card.metrics  = ({ "lm.ballSpeed": { label: "Ball", value: 141 } })
            verify(!card.deviceOnly)
        }

        // ── ⭐ the shot nothing witnessed ───────────────────────────────────────
        //
        // Both stages failed: ShotProcessor still puts the shot on the carousel
        // (maybeJoin: "The shot happened — it always lands on the carousel, with
        // whatever the pipeline produced"), with hasVideo = exportOk = false and no
        // metrics at all. There is no video, no monitor reading, and no IMU trace —
        // and the card announces it as an IMU swing. That is precisely the "small lie
        // in the one place a reader looks to find out what produced it" the component
        // set out to avoid; it was avoided for the monitor and not for this.
        function test_a_shot_with_no_witness_is_not_labelled_imu_only() {
            card.hasVideo = false
            card.metrics  = ({})
            card.tracePoints = []
            const l = label()
            verify(l.visible)
            verify(l.text !== "IMU ONLY")
        }

        // ── a real IMU shot does say IMU ────────────────────────────────────────
        function test_an_imu_shot_is_labelled_imu_only() {
            card.hasVideo    = false
            card.metrics     = ({ "wrist.extAtTop": { label: "Ext", value: 21 } })
            card.tracePoints = [ Qt.point(0.0, 0.5), Qt.point(0.5, 0.2), Qt.point(1.0, 0.6) ]
            const l = label()
            verify(l.visible)
            compare(l.text, "IMU ONLY")
        }

        // ── the ⚠ appears only on a shot that has one ───────────────────────────
        function test_the_warning_badge_follows_the_flag() {
            const b = badge()
            verify(b !== null)
            verify(!b.visible)
            card.dataWarning       = true
            card.dataWarningDetail = ({ capture: true, imu: false, holes: 1,
                                        framesLost: 4, worstHoleMs: 31.4, preImpact: true })
            verify(b.visible)
        }

        // ── the tooltip names the fault it actually found ───────────────────────
        function test_a_capture_hole_is_described_as_a_capture_hole() {
            card.dataWarning       = true
            card.dataWarningDetail = ({ capture: true, imu: false, holes: 1,
                                        framesLost: 4, worstHoleMs: 31.4, preImpact: true })
            const t = card.dataWarningText
            verify(t.indexOf("Frames were lost") === 0)
            verify(t.indexOf("4 frames") >= 0)
            verify(t.indexOf("1 hole") >= 0)
            verify(t.indexOf("1 holes") < 0)          // singular, for one hole
            verify(t.indexOf("during the swing") >= 0)
            verify(t.indexOf("IMU data integrity") < 0)   // it did not find that
            verify(t.indexOf("not included in the session assessment") >= 0)
        }

        function test_several_holes_are_described_in_the_plural() {
            card.dataWarning       = true
            card.dataWarningDetail = ({ capture: true, imu: false, holes: 3,
                                        framesLost: 12, worstHoleMs: 55.0, preImpact: false })
            const t = card.dataWarningText
            verify(t.indexOf("3 holes") >= 0)
            verify(t.indexOf("after impact") >= 0)
        }

        function test_an_imu_parity_failure_is_described_as_one() {
            card.dataWarning       = true
            card.dataWarningDetail = ({ capture: false, imu: true, worstMaxDeg: 14.2 })
            const t = card.dataWarningText
            verify(t.indexOf("IMU data integrity") >= 0)
            verify(t.indexOf("cannot be re-analysed") >= 0)
            verify(t.indexOf("Frames were lost") < 0)     // it did not find that
        }

        function test_both_faults_are_both_named() {
            card.dataWarning       = true
            card.dataWarningDetail = ({ capture: true, imu: true, holes: 2, framesLost: 9,
                                        worstHoleMs: 40.0, preImpact: true, worstMaxDeg: 9.1 })
            const t = card.dataWarningText
            verify(t.indexOf("Frames were lost") >= 0)
            verify(t.indexOf("IMU data integrity") >= 0)
        }

        // ── the tooltip never states a fact it was not given ────────────────────
        //
        // The text is assembled from the detail map by name. Any key the map does not
        // carry becomes the literal "undefined" or "NaN" in a sentence the golfer is
        // being asked to trust about their own recording.
        function test_the_warning_text_states_no_placeholders() {
            const cases = [
                ({ capture: true, imu: false, holes: 1, framesLost: 4,
                   worstHoleMs: 31.4, preImpact: true }),
                ({ capture: false, imu: true, worstMaxDeg: 14.2 }),
                ({ capture: true, imu: true, holes: 2, framesLost: 9,
                   worstHoleMs: 40.0, preImpact: false, worstMaxDeg: 9.1 }),
                ({})
            ]
            for (var i = 0; i < cases.length; ++i) {
                card.dataWarningDetail = cases[i]
                const t = card.dataWarningText
                verify(t.indexOf("undefined") < 0, "case " + i + ": " + t)
                verify(t.indexOf("NaN") < 0,       "case " + i + ": " + t)
            }
        }

        // ── the stars are the shot's rating, and tapping one asks for it ────────
        function test_tapping_a_star_asks_for_that_rating() {
            const stars = findChild(card, "starRating")
            if (stars === null)
                skip("PpStarRating carries no objectName — rating taps are untested here")
            mouseClick(stars, stars.width * 0.9, stars.height / 2)
            compare(probe.ratedCount, 1)
            verify(probe.lastRating > 0)
        }
    }
}
