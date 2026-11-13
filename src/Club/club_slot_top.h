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

#pragma once

// TERMINOLOGY (Mark, 13 Nov 2026). "Slot" survives in this file's identifiers — clubSlotTop, club_slot, club.slotUnread,
// club.slotCameraAssumed, club.slotMinWidthDeg, kClubSlotTopVersion — because they are stored keys, record ids and
// override names that documents, ledgers and sweeps already carry. It is NOT the golf "slot", which is a DOWNSWING
// position (halfway down, the club approaching from inside, the trail elbow near the trail hip). This reading is in
// the BACKSWING, at lead-arm-parallel: the club placed in the on-plane window between the address shaft plane and the
// plane through the ball and the trail shoulder. Everything a golfer or coach reads says "on-plane window" or
// "halfway back"; the comments below use "slot" only as shorthand for that window.

// THE CLUB'S CLIMB PLACED IN THE SLOT (plane stack D3, 13 Nov 2026; docs/design/plane_stack_design.md §3.3).
//
//   clubSlotTop = (swingPlaneCurve at P3 − swingPlaneCurve at P1) / (shoulderPlane at P1 − swingPlaneCurve at P1)
//
// A ratio, one value at P3 (lead arm parallel going back). The COACHING SLOT is the wedge between two planes
// through the ball's target line: the plane the club was set on at address (its flat edge) and the plane from
// the ball through the trail shoulder at address (its steep edge; shoulderPlane, shaft_plane_curve.h).
//   0 = the club at P3 sits on the plane it was set on; 1 = on the shoulder plane at address;
//   below 0 = under the slot (the club dropped below its address plane); above 1 = above the slot.
// Worked: set at 58°, a shoulder plane at 68° (a 10° slot), 63° at P3: (63 − 58) / 10 = 0.5, the middle of the
// slot. 70° at P3 reads 1.2 (above the slot); 56° reads −0.2 (under it).
//
// It is per swing, so it follows each golfer's own set-up: an upright set-up has a narrow slot, a flat one a wide
// slot, and the same climb in degrees means a different place in each. It is a ratio of two inclination
// differences on one down-the-line camera within one swing, so a camera TURNED a few degrees moves it little
// (a plane's inclination moves under 1° for ±15° of camera turn; shaft fusion). That evidence is for camera
// turn only. NOT YET SHOWN TO CANCEL CAMERA POSITION (D3 review, plane stack D6, 13 Nov 2026): both edges
// and the P3 plane are VIEW planes, lines in the down-the-line image through the ball, which equal the true
// planes only for a camera on the ball line; a camera moved in height or toward the hands shifts the ball →
// trail-shoulder line and the P3 shaft by different amounts, so the ratio can move. Mark's 7-iron reads a
// median 0.71 on 4 July, 0.96 on 7 Oct and 1.12 on 8 Oct, with 36 of his 38 "above the slot" swings in those
// two October sessions. A frame check of those sessions is pending.
//
// EACH PART IS READ EXACTLY AS THE GRADER READS IT: grader::graderReadingAt (Body/grader_sample.h, shared with shoulder_tilt_vs_bend.h),
// the diagnostics' own `at` sampling (measure_sample.cpp buildPhaseGrid + series_reduce.h). swingPlaneCurve is
// a fitted curve, read AT the ladder instant (reduceInstant; a bridged instant reads nothing); shoulderPlane is
// one phase sample at Address. So the numerator is exactly m_shaftPlaneBackswing on the same swing.
//
// NOT PRODUCED, with the reason named (Refused club.slotUnread, subject clubSlotTop, evidence `code`):
//   noAddressPlane   no swing plane at P1;
//   noP3Plane        no swing plane at P3 (the curve bridged there, or no P3 on the ladder);
//   noShoulderPlane  no shoulder plane at address (it needs the down-the-line ball and the trail shoulder);
//   narrow           the slot is under club.slotMinWidthDeg wide (tuned::clubSlot::kMinWidthDeg, PROVISIONAL;
//                    evidence widthDeg, minWidthDeg), or the shoulder plane is not above the address plane at all.
// The record is fired only on a swing that has a down-the-line plane at all (swingPlaneCurve or shoulderPlane in
// the series): a face-on-only swing is byte-identical. And every Refused, Bounded or Withheld record that names a
// missing part (club.trackRefused covers both) also covers clubSlotTop; it comes first in the record list, so it
// is the "not judged" reason a reader is given (gate_catalogue.h primaryRecord keeps the first of equal rank),
// and club.slotUnread is the reason only where no part's own refusal exists (no down-the-line ball, no P3).
//
// σ: first order through the ratio, the three parts independent (the grader takes the delta's two ends as
// independent too). With s the value and w the width,
//   σ_s² = ( σ_P3² + s²·σ_shoulder² + (1 − s)²·σ_P1² ) / w²
// (∂s/∂P3 = 1/w, ∂s/∂shoulder = −s/w, ∂s/∂P1 = −(1 − s)/w). Each part's σ is its graded σ (the phase sample's,
// else the series' — the grader's stampSigma). A part whose σ is only a noise floor (a series σ with no
// sigmaKind) makes the result a noise floor: a series σ with no kind, no phase-sample σ. A part with no σ: no σ.
//
// TRUST: every Downgraded or Withheld record that speaks for a part (subject or covers) also covers clubSlotTop;
// a withheld part withholds the reading. Run once, AFTER the run (runProfile), on the series and records as they
// will be written. Idempotent: a reading, record or cover this left on an earlier call is taken back first.

#include "../Analysis/gate_record.h"
#include "../Analysis/swing_analysis.h"
#include "../Body/grader_sample.h"   // grader::graderReadingAt — the grader's `at` sampling
#include "../Core/pp_tuned_constants.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

namespace club_slot {

inline const QString &readingKey()  { static const QString k = QStringLiteral("clubSlotTop"); return k; }
inline const QString &curveKey()    { static const QString k = QStringLiteral("swingPlaneCurve"); return k; }
inline const QString &shoulderKey() { static const QString k = QStringLiteral("shoulderPlane"); return k; }
inline const QString &gateId()      { static const QString k = PP_GATE("club.slotUnread"); return k; }

struct Outcome {
    bool    produced = false;
    QString code;                          // the club.slotUnread code when not produced; empty when produced or silent
    double  value = std::numeric_limits<double>::quiet_NaN();
    double  widthDeg = std::numeric_limits<double>::quiet_NaN();   // shoulder plane − plane at address
    double  sigma = std::numeric_limits<double>::quiet_NaN();
    int     recordsExtended = 0;           // the parts' kept-value records that now also cover the reading
    int     recordsExplaining = 0;         // no reading: the missing parts' refusals that now name it too
    grader::GradedReading p1, p3, shoulder;   // the parts as graded
    bool    cameraAssumed = false;         // club.slotCameraAssumed fired (the down-the-line camera uncalibrated)
};

} // namespace club_slot

// Publish clubSlotTop into `series` / `gates` from the parts that stand there. `ladder` is the document's
// (detail->phases), which is the grader's.
// `dtlCalibrated`: a measured calibration of the down-the-line camera reached the job (the flag
// club3d.cameraAssumed reads, ShaftFusionStage::configFor). The pipeline always passes it; false fires the
// Downgraded club.slotCameraAssumed on a produced reading (D6 round 2).
inline club_slot::Outcome deriveClubSlotTop(std::vector<MetricSeries> &series, std::vector<GateRecord> &gates,
                                            const std::vector<PhaseEvent> &ladder,
                                            double minWidthDeg = tuned::clubSlot::kMinWidthDeg,
                                            bool dtlCalibrated = true)
{
    namespace cs = club_slot;
    cs::Outcome out;
    const QString &K = cs::readingKey();
    series.erase(std::remove_if(series.begin(), series.end(), [&](const MetricSeries &m) { return m.key == K; }),
                 series.end());
    gates.erase(std::remove_if(gates.begin(), gates.end(), [](const GateRecord &g) {
                    return g.gate == cs::gateId() || g.gate == QLatin1String("club.slotCameraAssumed");
                }),
                gates.end());
    for (GateRecord &g : gates) g.covers.removeAll(K);

    const auto first = [&](const QString &key) -> const MetricSeries * {
        for (const MetricSeries &m : series)
            if (m.key == key) return &m;
        return nullptr;
    };
    const MetricSeries *curve = first(cs::curveKey());
    const MetricSeries *shoulder = first(cs::shoulderKey());
    if (curve) {
        out.p1 = grader::graderReadingAt(*curve, Phase::Address, ladder);
        out.p3 = grader::graderReadingAt(*curve, Phase::MidBackswing, ladder);
    }
    if (shoulder) out.shoulder = grader::graderReadingAt(*shoulder, Phase::Address, ladder);

    const auto refuse = [&](const char *code, QVariantMap ev) {
        out.code = QString::fromLatin1(code);
        ev.insert(QStringLiteral("code"), out.code);
        gates.push_back(GateRecord{ cs::gateId(), K, GateEffect::Refused, ev, {}, {}, QStringList{ K } });
    };

    // A PART IS MISSING: no reading. The missing part's own refusals name the reading first; club.slotUnread
    // names the part, on a swing that has a down-the-line plane at all.
    if (!out.p1.ok || !out.p3.ok || !out.shoulder.ok) {
        for (GateRecord &g : gates) {
            if (g.effect != GateEffect::Refused && g.effect != GateEffect::Bounded && g.effect != GateEffect::Withheld)
                continue;
            const auto names = [&](const QString &k) { return g.subject == k || g.covers.contains(k); };
            if (((!out.p1.ok || !out.p3.ok) && names(cs::curveKey())) || (!out.shoulder.ok && names(cs::shoulderKey()))) {
                g.covers << K;
                ++out.recordsExplaining;
            }
        }
        if (curve || shoulder)
            refuse(!out.p1.ok ? "noAddressPlane" : !out.p3.ok ? "noP3Plane" : "noShoulderPlane", {});
        return out;
    }

    // THE SLOT TOO NARROW to place a club in (or upside down): no reading, the width named.
    out.widthDeg = out.shoulder.value - out.p1.value;
    if (!(out.widthDeg >= minWidthDeg) || !(out.widthDeg > 0.0)) {
        refuse("narrow", QVariantMap{ { QStringLiteral("widthDeg"), std::round(out.widthDeg * 10.0) / 10.0 },
                                      { QStringLiteral("minWidthDeg"), minWidthDeg } });
        return out;
    }

    const double w = out.widthDeg;
    const double s = (out.p3.value - out.p1.value) / w;
    out.value = s;

    MetricSeries m;
    m.key         = K;
    m.label       = QStringLiteral("Club in the on-plane window at P3");
    m.unit        = QStringLiteral("ratio");
    m.routeId     = QStringLiteral("dtl");
    m.routeDirect = true;                              // the catalogue's one rung: dtl, Direct
    m.withheld    = !curve->withheld.isEmpty() ? curve->withheld : shoulder->withheld;
    PhaseSample ps{ Phase::MidBackswing, out.p3.tUs, s, QString() };
    const grader::GradedReading *parts[] = { &out.p1, &out.p3, &out.shoulder };
    bool anyNone = false, anyNoise = false, anyAssumed = false;
    for (const grader::GradedReading *p : parts) {
        anyNone    = anyNone || p->sigmaFrom == grader::SigmaFrom::None;
        anyNoise   = anyNoise || p->sigmaFrom == grader::SigmaFrom::Noise;
        anyAssumed = anyAssumed || p->sigmaKind == uint8_t(SigmaKind::AssumedInput);
    }
    if (!anyNone) {
        const double var = out.p3.sigma * out.p3.sigma + s * s * out.shoulder.sigma * out.shoulder.sigma
                         + (1.0 - s) * (1.0 - s) * out.p1.sigma * out.p1.sigma;
        const double sg = std::sqrt(var) / w;
        out.sigma = sg;
        if (anyNoise) {
            m.sigma = sg;                              // a noise floor stays one: no kind, no phase-sample σ
        } else {
            const uint8_t kind = uint8_t(anyAssumed ? SigmaKind::AssumedInput : SigmaKind::Propagated);
            ps.sigma = sg; ps.sigmaKind = kind;
            m.sigma = sg;  m.sigmaKind = kind;
        }
    }
    m.phaseSamples.push_back(ps);
    series.push_back(std::move(m));
    out.produced = true;

    // The parts' kept-value records (Downgraded, Withheld) speak for the slot too.
    for (GateRecord &g : gates) {
        if (g.effect != GateEffect::Downgraded && g.effect != GateEffect::Withheld) continue;
        const bool speaks = g.subject == cs::curveKey() || g.subject == cs::shoulderKey()
                         || g.covers.contains(cs::curveKey()) || g.covers.contains(cs::shoulderKey());
        if (!speaks) continue;
        g.covers << K;
        ++out.recordsExtended;
    }
    // THE CAMERA IS ASSUMED (D6 round 2, Mark 13 Nov 2026). The slot's two edges and the P3 plane are view planes
    // from the down-the-line camera, and a camera that STANDS somewhere else moves the ball → trail-shoulder line
    // and the shaft lines by different amounts, which the ratio does not cancel: Mark's 7-iron reads 0.71 / 0.96
    // / 1.12 across four camera set-ups, and re-aiming the camera on 7 Oct (between swings 5 and 6) moved the
    // address planes 2–3°, a third of his ~9° slot. Uncalibrated, it is kept and judged with this ⚠: compare it
    // within a session, not between them. The takeaway, also a plane difference within one swing, stays steady
    // across the same set-ups (−2.6 to −4.0°): it compares the shaft's own plane at two instants, so a camera
    // move shifts both alike; the slot divides by the gap between two DIFFERENT lines, so it is slot-specific.
    if (!dtlCalibrated) {
        gates.push_back(GateRecord{ PP_GATE("club.slotCameraAssumed"), K, GateEffect::Downgraded,
                                    QVariantMap{ { QStringLiteral("widthDeg"), std::round(w * 10.0) / 10.0 } },
                                    {}, {}, QStringList{ K } });
        out.cameraAssumed = true;
    }
    return out;
}

} // namespace pinpoint::analysis
