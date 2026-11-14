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

// The down-the-line replay-overlay payload — `detail.dtl`.
//
//   dtl = { pose2d: { frames, smoothed, synth, … },
//           club:   { valid, samples, positions, frameWidth, frameHeight },
//           ball:   { samples } }
//
// The SAME keys and element shapes as the face-on `pose2d` / `club` / `ball` blocks,
// so PpCameraFrame's replay painter draws the DTL tile with no second code path. It
// is NESTED under `dtl` rather than living beside them because several readers treat
// a top-level `pose2d`/`club` as face-on (shot_replay_controller's hasFaceOn,
// PpMotionPanel's availability) and must go on doing so.
//
// ONE translator, called by the one projection of the analysis block
// (analysisDetailFor, swing_doc.h — refactor step 1.3b): live, replayed and reloaded
// shots all reach it from the disk form's analysis.clubDtl, in window time. The
// Charts pictures (posture_picture, delivery_picture) read its club samples too.
// Header-only so the swing document's reader, which every tool links, needs no
// extra source. It lives here, beside that projection (swing_doc_reader.cpp), and not
// in src/Gui/shot where it began: the store adapter must not include a UI header
// (moved in refactor step 1.7; it is plain JSON → QVariant, with no QML type). What the translator deliberately does NOT do
// (dtl_shaft_tracker_design §5.9): no synth, no prediction, no bridging — a frame
// that did not publish an angle (tier below RAY) is not a sample, so the shaft
// visibly disappears across the end-on and occluded bands.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <vector>

namespace pinpoint {

namespace dtl_overlay_detail {

// ShaftSample2D::flags bits the face-on painter reads. Every MEASURED DTL frame
// (RAY/SEG/BAND) carries ShaftMeasured alone, so _restsOnMeasurement() admits it.
// A HELD frame (dtl_shaft_track.h: the band's own Viterbi θ between two
// measurements) carries ShaftCoasted | ShaftHeadProjected instead: the painter
// then draws it as its dim lone pen and the fan never treats it as a measurement.
inline constexpr int kFlagMeasured      = 0x01;
inline constexpr int kFlagCoasted       = 0x04;
inline constexpr int kFlagHeadProjected = 0x10;

// Used only when the track has fewer than two frames to measure its own interval.
inline constexpr qint64 kFallbackFrameIntervalUs = 8333;   // 120 fps

// A clubDtl frame MEASURED an angle: tier RAY/SEG/BAND (dtl_shaft_track.h DtlTier).
inline bool isMeasured(const QJsonObject &f)
{
    const QString tier = f.value(QStringLiteral("tier")).toString();
    return tier == QLatin1String("RAY") || tier == QLatin1String("SEG")
        || tier == QLatin1String("BAND");
}
// … or is DRAWN: measured, or HELD inside a band between two measurements.
inline bool isDrawn(const QJsonObject &f)
{
    return isMeasured(f) || f.value(QStringLiteral("tier")).toString() == QLatin1String("HELD");
}

// A normalised [x, y] pair from clubDtl, or false when the point is absent (null).
inline bool point2(const QJsonValue &v, QVariantList &out)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a.at(0).isDouble() || !a.at(1).isDouble())
        return false;
    out = QVariantList{ a.at(0).toDouble(), a.at(1).toDouble() };
    return true;
}

inline double numOr(const QJsonValue &v, double fallback)
{
    return v.isDouble() ? v.toDouble() : fallback;
}

} // namespace dtl_overlay_detail

// `pose2d`      — the DTL pose block already shaped exactly like the face-on
//                 `detail.pose2d` (empty ⇒ no pose on the DTL tile).
// `clubDtl`     — a `pinpoint.clubDtl/1` object (empty ⇒ no shaft, no ball).
// `foPositions` — the face-on `club.positions` list ({p, t_us, …}), already in
//                 the payload's time domain; each is looked up on the DTL track.
// `impactUs`    — the Impact phase in the payload's time domain; < 0 = unknown.
// `retime`      — maps a clubDtl `t_us` value into the payload's time domain
//                 (windowTimeUs against the document's analysis origin).
//
// Returns an empty map when there is nothing to draw, so the caller can omit
// `dtl` altogether.
inline QVariantMap dtlOverlayDetail(const QVariantMap &pose2d,
                             const QJsonObject &clubDtl,
                             const QVariantList &foPositions,
                             qint64 impactUs,
                             const std::function<qint64(const QJsonValue &)> &retime)
{
    using namespace dtl_overlay_detail;
    QVariantMap dtl;
    if (!pose2d.isEmpty())
        dtl.insert(QStringLiteral("pose2d"), pose2d);

    const QJsonArray frames = clubDtl.value(QStringLiteral("frames")).toArray();
    const int fw = clubDtl.value(QStringLiteral("frameWidth")).toInt();
    const int fh = clubDtl.value(QStringLiteral("frameHeight")).toInt();
    if (frames.isEmpty() || fw <= 0 || fh <= 0)
        return dtl;

    // ── Club samples: DRAWN frames only (measured, plus HELD). grip/head in clubDtl
    //    are already normalised to the DTL frame (x / frameWidth, y / frameHeight),
    //    which is the face-on payload's convention, so they pass through as numbers.
    //    The P-position borrow below reads MEASURED samples only.
    QVariantList samples;
    std::vector<qint64> sampleT;        // every drawn sample's time (parallel to `samples`)
    std::vector<char>   sampleMeasured;
    std::vector<qint64> allT;
    allT.reserve(size_t(frames.size()));
    for (const QJsonValue &fv : frames) {
        const QJsonObject f = fv.toObject();
        const qint64 t = retime(f.value(QStringLiteral("t_us")));
        allT.push_back(t);
        if (!isDrawn(f))
            continue;
        QVariantList grip, head;
        if (!point2(f.value(QStringLiteral("grip")), grip)
            || !point2(f.value(QStringLiteral("head")), head))
            continue;
        const bool measured = isMeasured(f);
        samples.append(QVariantMap{
            { QStringLiteral("t_us"),  t },
            { QStringLiteral("grip"),  grip },
            { QStringLiteral("head"),  head },
            { QStringLiteral("theta"), numOr(f.value(QStringLiteral("theta")), 0.0) },
            { QStringLiteral("lenPx"), numOr(f.value(QStringLiteral("lenPx")), -1.0) },
            { QStringLiteral("conf"),  numOr(f.value(QStringLiteral("conf")), 0.0) },
            { QStringLiteral("flags"), measured ? kFlagMeasured : (kFlagCoasted | kFlagHeadProjected) },
            { QStringLiteral("held"),  !measured } });
        sampleT.push_back(t);
        sampleMeasured.push_back(measured ? 1 : 0);
    }

    // The track's own frame interval (median spacing of ALL its frames, published or
    // not) — the tolerance within which a face-on P-position may borrow a DTL sample.
    qint64 intervalUs = kFallbackFrameIntervalUs;
    if (allT.size() >= 2) {
        std::vector<qint64> dts;
        dts.reserve(allT.size() - 1);
        for (size_t i = 1; i < allT.size(); ++i)
            if (allT[i] > allT[i - 1]) dts.push_back(allT[i] - allT[i - 1]);
        if (!dts.empty()) {
            std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
            intervalUs = dts[dts.size() / 2];
        }
    }

    if (!samples.isEmpty()) {
        // ── P-positions: each face-on P moment, drawn on the DTL tile ONLY where the
        //    DTL track published a sample within one frame of it. clockOffsetUs is
        //    DTL − face-on (0 on a shared clock), so a face-on instant lands on the
        //    DTL clock by adding it. Absent where DTL did not see the club — that is
        //    the finding, not a gap to fill.
        const qint64 clockOffsetUs =
            static_cast<qint64>(clubDtl.value(QStringLiteral("clockOffsetUs")).toDouble());
        struct Pos { qint64 t; QVariantMap m; };
        std::vector<Pos> positions;
        for (const QVariant &pv : foPositions) {
            const QVariantMap fo = pv.toMap();
            if (!fo.contains(QStringLiteral("t_us")))
                continue;
            const qint64 want = fo.value(QStringLiteral("t_us")).toLongLong() + clockOffsetUs;
            const auto it = std::lower_bound(sampleT.begin(), sampleT.end(), want);
            int best = -1;
            qint64 bestDt = 0;
            for (auto c : { it, it == sampleT.begin() ? sampleT.end() : it - 1 }) {
                if (c == sampleT.end()) continue;
                const qint64 dt = std::llabs(*c - want);
                if (best < 0 || dt < bestDt) { best = int(c - sampleT.begin()); bestDt = dt; }
            }
            if (best < 0 || bestDt > intervalUs || !sampleMeasured[size_t(best)])
                continue;
            const QVariantMap s = samples.at(best).toMap();
            positions.push_back(Pos{ sampleT[size_t(best)], QVariantMap{
                { QStringLiteral("p"),      fo.value(QStringLiteral("p")).toInt() },
                { QStringLiteral("t_us"),   sampleT[size_t(best)] },
                { QStringLiteral("grip"),   s.value(QStringLiteral("grip")) },
                { QStringLiteral("head"),   s.value(QStringLiteral("head")) },
                { QStringLiteral("theta"),  s.value(QStringLiteral("theta")) },
                { QStringLiteral("lenPx"),  s.value(QStringLiteral("lenPx")) },
                { QStringLiteral("conf"),   s.value(QStringLiteral("conf")) },
                { QStringLiteral("source"), 0 } } });   // PositionSource::TrackSample
        }
        // The painter bisects positions by t_us.
        std::stable_sort(positions.begin(), positions.end(),
                         [](const Pos &a, const Pos &b) { return a.t < b.t; });
        QVariantList posList;
        for (const Pos &p : positions) posList.append(p.m);

        // `heldTier` says the track was produced with the HELD tier available, so
        // the tile ties its "is this sample current?" window to ONE frame interval
        // instead of the fixed 40 ms that used to bridge holes silently: with the
        // tier on, a hole is either a HELD sample (drawn dim) or nothing. Off, the
        // tile keeps its old rule. `intervalUs` is the track's own frame interval.
        const QJsonObject continuous = clubDtl.value(QStringLiteral("summary")).toObject()
                                              .value(QStringLiteral("continuous")).toObject();
        const bool heldTier = clubDtl.value(QStringLiteral("config")).toObject()
                                     .value(QStringLiteral("heldEnabled")).toBool(false)
                              && !continuous.isEmpty();
        QVariantMap club{
            { QStringLiteral("valid"),       true },
            { QStringLiteral("frameWidth"),  fw },
            { QStringLiteral("frameHeight"), fh },
            { QStringLiteral("heldTier"),    heldTier },
            { QStringLiteral("intervalUs"),  intervalUs },
            { QStringLiteral("samples"),     samples } };
        if (!posList.isEmpty())
            club.insert(QStringLiteral("positions"), posList);
        // The 3-D synthetic shaft's projection (clubDtl `synth3d`), as its own series:
        // flags ShaftSynthesized | ShaftHeadProjected, so the painter's dim lone pen is
        // the most it can ever be drawn as, and `preview` says the camera was uncalibrated.
        const QJsonArray synth3d = clubDtl.value(QStringLiteral("synth3d")).toArray();
        if (!synth3d.isEmpty()) {
            constexpr int kFlagSynthesized = 0x100;
            QVariantList syn;
            for (const QJsonValue &sv : synth3d) {
                const QJsonObject f = sv.toObject();
                QVariantList grip, head;
                if (!point2(f.value(QStringLiteral("grip")), grip)
                    || !point2(f.value(QStringLiteral("head")), head))
                    continue;
                syn.append(QVariantMap{
                    { QStringLiteral("t_us"),  retime(f.value(QStringLiteral("t_us"))) },
                    { QStringLiteral("grip"),  grip },
                    { QStringLiteral("head"),  head },
                    { QStringLiteral("theta"), numOr(f.value(QStringLiteral("theta")), 0.0) },
                    { QStringLiteral("lenPx"), numOr(f.value(QStringLiteral("lenPx")), -1.0) },
                    { QStringLiteral("conf"),  0.3 },
                    { QStringLiteral("flags"), kFlagSynthesized | kFlagHeadProjected } });
            }
            if (!syn.isEmpty()) {
                club.insert(QStringLiteral("synth3d"), syn);
                club.insert(QStringLiteral("synth3dPreview"),
                            clubDtl.value(QStringLiteral("summary")).toObject()
                                   .value(QStringLiteral("synth3d")).toObject()
                                   .value(QStringLiteral("preview")).toBool(true));
            }
        }
        dtl.insert(QStringLiteral("club"), club);
    }

    // ── Ball: one static position (the tracker finds it once, at address), shown
    //    from the track's first frame and withdrawn at impact — the face-on block's
    //    found=true … found=false convention. x/y/radiusPx are image px in clubDtl;
    //    normalised here like face-on (x, y by the frame; r by the frame WIDTH).
    //    An unmeasured radius is 0, which the painter floors to its minimum ring.
    const QJsonObject ball = clubDtl.value(QStringLiteral("summary")).toObject()
                                    .value(QStringLiteral("ball")).toObject();
    const QJsonValue bx = ball.value(QStringLiteral("x")), by = ball.value(QStringLiteral("y"));
    if (ball.value(QStringLiteral("found")).toBool() && bx.isDouble() && by.isDouble()) {
        const qint64 firstT = allT.front();
        const double rPx = numOr(ball.value(QStringLiteral("radiusPx")), 0.0);
        QVariantList bs{ QVariantMap{
            { QStringLiteral("t_us"),  firstT },
            { QStringLiteral("x"),     bx.toDouble() / fw },
            { QStringLiteral("y"),     by.toDouble() / fh },
            { QStringLiteral("r"),     (std::isfinite(rPx) && rPx > 0.0) ? rPx / fw : 0.0 },
            { QStringLiteral("conf"),  1.0 },
            { QStringLiteral("found"), true } } };
        if (impactUs > firstT)
            bs.append(QVariantMap{
                { QStringLiteral("t_us"),  impactUs },
                { QStringLiteral("found"), false } });
        dtl.insert(QStringLiteral("ball"), QVariantMap{ { QStringLiteral("samples"), bs } });
    }
    return dtl;
}

} // namespace pinpoint
