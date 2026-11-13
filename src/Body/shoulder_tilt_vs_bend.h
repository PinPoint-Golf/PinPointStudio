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

// THE SHOULDER TILT JUDGED AGAINST THE GOLFER'S OWN FORWARD BEND (planned-metrics second pass W7,
// Mark's decision of 13 Nov 2026; docs/design/planned_metrics_promotion.md §6 W7).
//
//   shoulderTiltVsBend = shoulderPlaneAngle3d at the TOP − spineForwardBend at ADDRESS, in degrees.
//   + = the shoulders turned MORE STEEPLY than the posture at address sets them up to; − = flatter.
//
// A turn made square to the spine finishes with the shoulder line inclined by about the spine's forward
// bend, so the absolute tilt calls an upright golfer flat and a bent-over one steep for the same turn.
// The difference asks whether the shoulders turned on the plane THIS golfer's posture set. One value,
// one phase sample at the Top, no curve (as the setup readings are published).
//
// AFTER THE PRECEDENCE. Each half is whatever route won (applyTrunkSensorPrecedence, then
// reconcileSubstitutedReadings, runProfile in analysis_pipeline.cpp): the chest sensor's or the 3-D
// fit's tilt, the chest sensor's or the down-the-line camera's bend. So this runs ONCE, after both, on
// the series and records as they will be written — never inside a stage, which would see whichever
// readings happened to be there before the precedence settled them.
//
// EACH HALF IS READ EXACTLY AS THE GRADER READS IT (grader::graderReadingAt, grader_sample.h = measure_sample.cpp
// buildPhaseGrid + reduceOverGrid's `at`): the median of the VALID samples within ±sampler::kWindowHalfUs
// of the ladder instant (series_reduce.h reduceAt, one sample enough), or the instant itself on a fitted
// curve; else the phase sample the series labels at that phase. So the value graded here is exactly
// `m_shoulderPlane` − (spineForwardBend `at p1`) on the same swing. Either half missing at its phase:
// NO READING — no zero, no half value.
//
// σ: the two halves' graded σ (the phase sample's own, else the series σ — the grader's stampSigma) in
// quadrature. When both come from the chest sensor they share one term, the mount yaw δ: the tilt carries
// it through the chest's bend (asin(sin θ·sin σ_δ), trunk_sensor_series.h mountYawObliquityDeg), the
// bend through its address front (ballPlaneMountYawDeg, second order with no side lean). The garment term
// does not correlate (it moves the bend about the chest's left-right axis and the tilt about its front-back
// one), and the gyro-scale, drift and turn-leak terms are zero at address or tilt-only. The series carry
// no signed δ sensitivities, so the shared term cannot be taken exactly here; quadrature is used, and it is
// conservative in practice because the tilt's δ term is budgeted at its address worst case and not reduced
// as the chest turns (planned_metrics_promotion.md §6 W7 has the numbers). A half whose σ is only the
// frame-to-frame noise floor (a series σ with no sigmaKind) makes the result a noise floor too: it is
// written as a series σ with no kind, never as a phase-sample σ the grader would take as a full budget.
// No σ on either half: no σ (not characterised, never zero).
//
// TRUST: every Downgraded or Withheld record that speaks for either half (subject or covers) also covers
// this reading — the way xFactor sits under each turn's records — and, when the halves come from
// different instruments, the Downgraded body.tiltBendMixedSources. With NO reading (a half missing), the
// missing half's Refused / Bounded / Withheld records cover the key instead, so the reading's "not judged"
// reason is that half's real refusal (W7 review).
//
// THE BACKSTOP (FB, plane stack, 13 Nov 2026): a reading below tuned::body3d::kTiltBendFloorDeg (−20°,
// body3d.tiltBendFloorDeg) is outside what a turn can make — with a 35° forward bend it would need a
// shoulder turn under ~27° — and the library's clean 1st percentile. It is kept and judged, with the
// Downgraded body.tiltBendOutOfRange (subject shoulderTiltVsBend, covers it and shoulderPlaneAngle3d;
// evidence valueDeg, floorDeg): it is probably a misreading of the shoulders (Chris's −24 was the 3-D
// fit throwing the down-the-line trail shoulder away, plane_stack_design.md §11 FB). Never a refusal.
//
// ROUTES (the catalogue's rungs, metric_catalogue_manifest.cpp):
//   trunkImus             tilt thoraxImu   + bend trunkImus   both from the chest sensor; no extra record
//   thoraxImu+dtl         tilt thoraxImu   + bend down the line
//   faceOn+dtl            tilt faceOn+dtl  + bend down the line (the two-camera rig)
//   trunkImus+faceOn+dtl  tilt faceOn+dtl  + bend trunkImus
// The down-the-line posture series states no route (dtl_posture.h); a bend with none is that camera's.

#include "grader_sample.h"   // grader::graderReadingAt — the grader's `at` reading, shared with club_slot_top.h
#include "../Analysis/gate_record.h"
#include "../Analysis/swing_analysis.h"
#include "../Core/pp_tuned_constants.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

namespace tilt_vs_bend {

inline const QString &readingKey() { static const QString k = QStringLiteral("shoulderTiltVsBend"); return k; }
inline const QString &tiltKey()    { static const QString k = QStringLiteral("shoulderPlaneAngle3d"); return k; }
inline const QString &bendKey()    { static const QString k = QStringLiteral("spineForwardBend"); return k; }

// The instrument family of a half's route: the chest sensor, or a camera route.
inline bool fromChestSensor(const QString &route)
{
    return route == QLatin1String("thoraxImu") || route == QLatin1String("trunkImus");
}

// The catalogue rung for a (tilt route, bend route) pair; empty for a pair no rung names.
inline QString routeFor(const QString &tiltRoute, const QString &bendRoute)
{
    const bool tiltSensor = tiltRoute == QLatin1String("thoraxImu"), tilt3d = tiltRoute == QLatin1String("faceOn+dtl");
    const bool bendSensor = bendRoute == QLatin1String("trunkImus"), bendDtl = bendRoute == QLatin1String("dtl");
    if (tiltSensor && bendSensor) return QStringLiteral("trunkImus");
    if (tiltSensor && bendDtl)    return QStringLiteral("thoraxImu+dtl");
    if (tilt3d && bendDtl)        return QStringLiteral("faceOn+dtl");
    if (tilt3d && bendSensor)     return QStringLiteral("trunkImus+faceOn+dtl");
    return {};
}

struct Outcome {
    bool    produced = false;
    QString tiltRoute, bendRoute, route;   // the halves' routes and the reading's rung
    bool    mixed = false;                 // the halves came from different instruments (the record fired)
    int     recordsExtended = 0;           // the halves' records that now also cover the reading
    int     recordsExplaining = 0;         // no reading: the missing half's refusals that now name it too
    bool    outOfRange = false;            // below the backstop's floor: body.tiltBendOutOfRange fired (FB)
    grader::GradedReading tilt, bend;              // the halves as graded
};

} // namespace tilt_vs_bend

// Publish shoulderTiltVsBend into `series` / `gates` from the halves that stand there. Run once, after
// the precedence and the substitution clean-up. Idempotent: a reading, record or cover this left on an
// earlier call is taken back first.
inline tilt_vs_bend::Outcome deriveShoulderTiltVsBend(std::vector<MetricSeries> &series, std::vector<GateRecord> &gates,
                                                      const std::vector<PhaseEvent> &ladder,
                                                      double floorDeg = tuned::body3d::kTiltBendFloorDeg)
{
    namespace tb = tilt_vs_bend;
    tb::Outcome out;
    const QString &K = tb::readingKey();
    series.erase(std::remove_if(series.begin(), series.end(), [&](const MetricSeries &m) { return m.key == K; }),
                 series.end());
    gates.erase(std::remove_if(gates.begin(), gates.end(),
                               [](const GateRecord &g) {
                                   return g.gate == QLatin1String("body.tiltBendMixedSources")
                                       || g.gate == QLatin1String("body.tiltBendOutOfRange");
                               }),
                gates.end());
    for (GateRecord &g : gates) g.covers.removeAll(K);

    // The FIRST series under each key, as the grader takes it (one per key after the precedence).
    const auto first = [&](const QString &key) -> const MetricSeries * {
        for (const MetricSeries &m : series)
            if (m.key == key) return &m;
        return nullptr;
    };
    const MetricSeries *tilt = first(tb::tiltKey());
    const MetricSeries *bend = first(tb::bendKey());
    if (tilt) out.tilt = grader::graderReadingAt(*tilt, Phase::Top, ladder);
    if (bend) out.bend = grader::graderReadingAt(*bend, Phase::Address, ladder);
    // A HALF IS MISSING: no reading — and the reason a reader gives for that is the missing half's own.
    // Every record that refused, bounded or withheld that half (the effects gates::primaryRecord ranks,
    // which LiveMeasureSource::gateCause shows) also covers the reading, so "not judged" names the real
    // refusal (posture.address on a swing whose down-the-line posture refused at address), never the
    // generic "metric not produced". Downgraded and Masked records are not extended here: a kept-value
    // warning or a frame count is not a reason for absence. The clean-up above makes this idempotent.
    if (!out.tilt.ok || !out.bend.ok) {
        for (GateRecord &g : gates) {
            if (g.effect != GateEffect::Refused && g.effect != GateEffect::Bounded && g.effect != GateEffect::Withheld)
                continue;
            const auto names = [&](const QString &k) { return g.subject == k || g.covers.contains(k); };
            if ((!out.tilt.ok && names(tb::tiltKey())) || (!out.bend.ok && names(tb::bendKey()))) {
                g.covers << K;
                ++out.recordsExplaining;
            }
        }
        return out;
    }

    out.tiltRoute = tilt->routeId;
    out.bendRoute = bend->routeId.isEmpty() ? QStringLiteral("dtl") : bend->routeId;
    out.route     = tb::routeFor(out.tiltRoute, out.bendRoute);
    out.mixed     = !(tb::fromChestSensor(out.tiltRoute) && tb::fromChestSensor(out.bendRoute));

    MetricSeries m;
    m.key     = K;
    m.label   = QStringLiteral("Shoulder tilt against posture");
    m.unit    = QStringLiteral("°");
    m.routeId = out.route;
    // Direct exactly where the catalogue's rung is: both halves on Direct rungs of their own ladders
    // (thoraxImu, trunkImus, dtl); the 3-D fit's tilt is Estimated.
    m.routeDirect = out.route == QLatin1String("trunkImus") || out.route == QLatin1String("thoraxImu+dtl");
    // A half that may not be judged takes the difference with it.
    m.withheld = !tilt->withheld.isEmpty() ? tilt->withheld : bend->withheld;
    PhaseSample ps{ Phase::Top, out.tilt.tUs, out.tilt.value - out.bend.value, QString() };
    if (out.tilt.sigmaFrom != grader::SigmaFrom::None && out.bend.sigmaFrom != grader::SigmaFrom::None) {
        const double s = std::hypot(out.tilt.sigma, out.bend.sigma);
        if (out.tilt.sigmaFrom == grader::SigmaFrom::Noise || out.bend.sigmaFrom == grader::SigmaFrom::Noise) {
            m.sigma = s;                // a noise floor stays one: no kind, no phase-sample σ
        } else {
            const bool assumed = out.tilt.sigmaKind == uint8_t(SigmaKind::AssumedInput)
                              || out.bend.sigmaKind == uint8_t(SigmaKind::AssumedInput);
            const uint8_t kind = uint8_t(assumed ? SigmaKind::AssumedInput : SigmaKind::Propagated);
            ps.sigma = s; ps.sigmaKind = kind;
            m.sigma = s;  m.sigmaKind = kind;
        }
    }
    m.phaseSamples.push_back(ps);
    series.push_back(std::move(m));
    out.produced = true;

    // The halves' kept-value records (Downgraded, Withheld) speak for the difference too.
    for (GateRecord &g : gates) {
        if (g.effect != GateEffect::Downgraded && g.effect != GateEffect::Withheld) continue;
        const bool speaks = g.subject == tb::tiltKey() || g.subject == tb::bendKey()
                         || g.covers.contains(tb::tiltKey()) || g.covers.contains(tb::bendKey());
        if (!speaks) continue;
        g.covers << K;
        ++out.recordsExtended;
    }
    if (out.mixed)
        gates.push_back(GateRecord{ PP_GATE("body.tiltBendMixedSources"), K, GateEffect::Downgraded,
                                    QVariantMap{ { QStringLiteral("tiltFrom"), out.tiltRoute.isEmpty()
                                                                                   ? QStringLiteral("unstated") : out.tiltRoute },
                                                 { QStringLiteral("bendFrom"), out.bendRoute } },
                                    {}, {}, QStringList{ K } });
    // The backstop (top of file): below the floor a turn cannot reach, the reading is kept with its ⚠.
    const double value = out.tilt.value - out.bend.value;
    if (std::isfinite(floorDeg) && value < floorDeg) {
        out.outOfRange = true;
        gates.push_back(GateRecord{ PP_GATE("body.tiltBendOutOfRange"), K, GateEffect::Downgraded,
                                    QVariantMap{ { QStringLiteral("valueDeg"), std::round(value * 10.0) / 10.0 },
                                                 { QStringLiteral("floorDeg"), floorDeg } },
                                    {}, {}, QStringList{ K, tb::tiltKey() } });
    }
    return out;
}

} // namespace pinpoint::analysis
