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

// THE ONE SERIALISER of a pose track — `analysis.pose2d` (face-on) and `analysis.poseDtl`
// (down the line) share the shape — the writer and its typed reader side by side, beside the
// producer (PoseRunner, DtlPoseStage). Refactor step 1.3a (information_model.md §8 "One serialiser
// per analysis block"). swing_doc_analysis.cpp composes the document from the writer; the reuse
// reader PoseRunner::fromJsonObject and the document's typed read (analysisFromDocument) read
// through poseTrackFromAnalysisJson.
//
// Shape: { camera, keypointCount, frames[{t_us, kp[x,y,c]×133, lead, trail, handConf}],
// [decode], [cropRect], [smoothed[{t_us, kp, tier, sigma}]], [adaptFallbacks], [synth[{t_us, kp}]] }.
// TIMES go through windowTimeUs (swing_analysis.h): `t0Us` 0 = as written, else the clock the
// track is on. `scanPath` is run telemetry (analysis.path.pose) and is not part of the block.

#include "../Analysis/swing_analysis.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace pinpoint::analysis {

inline QJsonObject poseTrackToAnalysisJson(const PoseTrack2D &t, int64_t t0Us)
{
    const auto rel = [t0Us](int64_t v) -> qint64 { return qint64(windowTimeUs(v, t0Us)); };
    QJsonArray frames;
    for (const PoseFrame2D &f : t.frames) {
        QJsonArray kp;
        for (int j = 0; j < kWholeBodyJoints; ++j) {
            kp.append(f.kp[size_t(j)].x());
            kp.append(f.kp[size_t(j)].y());
            kp.append(double(f.conf[size_t(j)]));
        }
        frames.append(QJsonObject{
            { QStringLiteral("t_us"), rel(f.t_us) },
            { QStringLiteral("kp"),   kp },
            { QStringLiteral("lead"),  QJsonArray{ f.leadHand.x(),  f.leadHand.y() } },
            { QStringLiteral("trail"), QJsonArray{ f.trailHand.x(), f.trailHand.y() } },
            { QStringLiteral("handConf"), double(f.handConf) } });
    }
    // keypointCount: explicit kp width (COCO-WholeBody 133; indices 0–16
    // are the unchanged COCO body joints, tail = feet/face/hands). Readers
    // use bounded loops so the field is provenance, not a parse contract.
    QJsonObject obj{
        { QStringLiteral("camera"), int(t.camera) },
        { QStringLiteral("keypointCount"), kWholeBodyJoints },
        { QStringLiteral("frames"), frames } };
    // WB1 accuracy-pass provenance (wholebody_pose_design.md §3). Written ONLY
    // when non-legacy — decode when DARK, cropRect when a crop was actually
    // used — so a flags-off (crop disabled + argmax) run serialises exactly as
    // the pre-WB1 tree (the WB1 byte-parity gate). cropRect is full-frame
    // normalized {x,y,w,h}.
    if (t.decode == QLatin1String("dark"))
        obj.insert(QStringLiteral("decode"), t.decode);
    if (t.cropRect) {
        const QRectF &r = *t.cropRect;
        obj.insert(QStringLiteral("cropRect"),
                      QJsonObject{ { QStringLiteral("x"), r.x() },
                                   { QStringLiteral("y"), r.y() },
                                   { QStringLiteral("w"), r.width() },
                                   { QStringLiteral("h"), r.height() } });
    }
    // Motion-overlay smoothed companion track (pose_smoother.cpp): parallel to
    // `frames` on the same t_us grid — kp[x,y,c]×133 flat exactly like `frames`
    // (conf carries the render-alpha contract) plus per-kp honesty tier[133] (int)
    // and sigma[133] (px). No lead/trail/handConf — the hands are NOT smoothed.
    // Written ONLY when non-empty (absent on swings analysed before the smoother
    // existed, or a format-less path), so an empty smoothed vector serializes
    // byte-identically to today. t_us is window-relative via rel(), same as frames'.
    if (!t.smoothed.empty()) {
        QJsonArray smoothed;
        const size_t n = std::min(t.smoothed.size(), t.smoothedAux.size());
        for (size_t i = 0; i < n; ++i) {
            const PoseFrame2D &f = t.smoothed[i];
            const PoseKpAux   &x = t.smoothedAux[i];
            QJsonArray kp, tier, sigma;
            for (int j = 0; j < kWholeBodyJoints; ++j) {
                kp.append(f.kp[size_t(j)].x());
                kp.append(f.kp[size_t(j)].y());
                kp.append(double(f.conf[size_t(j)]));
                tier.append(int(x.tier[size_t(j)]));
                sigma.append(double(x.sigma[size_t(j)]));
            }
            smoothed.append(QJsonObject{
                { QStringLiteral("t_us"),  rel(f.t_us) },
                { QStringLiteral("kp"),    kp },
                { QStringLiteral("tier"),  tier },
                { QStringLiteral("sigma"), sigma } });
        }
        obj.insert(QStringLiteral("smoothed"), smoothed);
    }
    // Phase-5 motion-adaptive window (poseSmooth.adapt.*): the count of keypoints
    // whose adaptive pass was REJECTED by the divergence guard and fell back to the
    // unadapted output. Written ONLY when non-zero — same discipline as sigma/valid —
    // so a swing analysed with the window off (or with nothing falling back)
    // serializes byte-identically. Diagnostic: nothing reads it back to decide; it exists so a
    // sweep can refuse a setting that would have moved the segmentation.
    if (t.adaptFallbacks > 0)
        obj.insert(QStringLiteral("adaptFallbacks"), t.adaptFallbacks);
    // Dense VIZ-tier pose synth (pose_synthesis.h): the smoothed skeleton
    // upsampled to 240 Hz so the replay overlays scrub smoothly — the body
    // sibling of club.synth. Lean shape { t_us, kp[x,y,c]×133 } — no tier/sigma
    // (the overlay's conf-gate skips Off joints, which carry conf 0 here) and no
    // hands (not drawn by the body overlays). Written ONLY when non-empty, so a
    // synth-off run omits it and serializes byte-identically. Metrics NEVER read
    // it (same discipline as the measured/smoothed split above).
    if (!t.smoothedSynth.empty()) {
        QJsonArray synth;
        for (const PoseFrame2D &f : t.smoothedSynth) {
            QJsonArray kp;
            for (int j = 0; j < kWholeBodyJoints; ++j) {
                kp.append(f.kp[size_t(j)].x());
                kp.append(f.kp[size_t(j)].y());
                kp.append(double(f.conf[size_t(j)]));
            }
            synth.append(QJsonObject{
                { QStringLiteral("t_us"), rel(f.t_us) },
                { QStringLiteral("kp"),   kp } });
        }
        obj.insert(QStringLiteral("synth"), synth);
    }
    return obj;
}

namespace pose_json_detail {

// kp[x,y,c]×N, bounded by BOTH the array and the struct width: old 51-float files fill 0–16 and
// leave the wholebody tail default-initialized.
inline void readKp(const QJsonArray &kp, PoseFrame2D &pf)
{
    for (int j = 0; j < kWholeBodyJoints && j * 3 + 2 < kp.size(); ++j) {
        pf.kp[size_t(j)]   = QPointF(kp[j * 3].toDouble(), kp[j * 3 + 1].toDouble());
        pf.conf[size_t(j)] = float(kp[j * 3 + 2].toDouble());
    }
}

} // namespace pose_json_detail

// The typed reader of one pose block (the object poseTrackToAnalysisJson writes, or a SwingLab
// {"frames":[...]} file, which is the same shape with nothing but frames). Every field comes back:
// frames, provenance (decode, cropRect), the smoothed tier with its aux, adaptFallbacks and the
// synth tier. `camera` is the block's own; a caller binding the track to a live source sets it.
inline PoseTrack2D poseTrackFromAnalysisJson(const QJsonObject &block, int64_t t0Us = 0)
{
    using pose_json_detail::readKp;
    PoseTrack2D track;
    track.camera = pinpoint::SourceId(block.value(QStringLiteral("camera")).toInt(int(pinpoint::kInvalidSourceId)));
    if (block.contains(QStringLiteral("decode"))) track.decode = block.value(QStringLiteral("decode")).toString();
    if (block.contains(QStringLiteral("cropRect"))) {
        const QJsonObject r = block.value(QStringLiteral("cropRect")).toObject();
        track.cropRect = QRectF(r.value(QStringLiteral("x")).toDouble(), r.value(QStringLiteral("y")).toDouble(),
                                r.value(QStringLiteral("w")).toDouble(), r.value(QStringLiteral("h")).toDouble());
    }
    for (const QJsonValue &fv : block.value(QStringLiteral("frames")).toArray()) {
        const QJsonObject o = fv.toObject();
        PoseFrame2D pf;
        pf.t_us = windowTimeUs(int64_t(o.value(QStringLiteral("t_us")).toDouble()), t0Us);
        readKp(o.value(QStringLiteral("kp")).toArray(), pf);
        const QJsonArray lead  = o.value(QStringLiteral("lead")).toArray();
        const QJsonArray trail = o.value(QStringLiteral("trail")).toArray();
        if (lead.size() == 2)  pf.leadHand  = QPointF(lead[0].toDouble(), lead[1].toDouble());
        if (trail.size() == 2) pf.trailHand = QPointF(trail[0].toDouble(), trail[1].toDouble());
        pf.handConf = float(o.value(QStringLiteral("handConf")).toDouble());
        track.frames.push_back(std::move(pf));
    }
    for (const QJsonValue &fv : block.value(QStringLiteral("smoothed")).toArray()) {
        const QJsonObject o = fv.toObject();
        PoseFrame2D pf;
        PoseKpAux   aux;
        pf.t_us = windowTimeUs(int64_t(o.value(QStringLiteral("t_us")).toDouble()), t0Us);
        readKp(o.value(QStringLiteral("kp")).toArray(), pf);
        const QJsonArray tier  = o.value(QStringLiteral("tier")).toArray();
        const QJsonArray sigma = o.value(QStringLiteral("sigma")).toArray();
        for (int j = 0; j < kWholeBodyJoints && j < tier.size(); ++j)  aux.tier[size_t(j)]  = uint8_t(tier[j].toInt());
        for (int j = 0; j < kWholeBodyJoints && j < sigma.size(); ++j) aux.sigma[size_t(j)] = float(sigma[j].toDouble());
        track.smoothed.push_back(std::move(pf));
        track.smoothedAux.push_back(aux);
    }
    track.adaptFallbacks = block.value(QStringLiteral("adaptFallbacks")).toInt(0);
    for (const QJsonValue &fv : block.value(QStringLiteral("synth")).toArray()) {
        const QJsonObject o = fv.toObject();
        PoseFrame2D pf;
        pf.t_us = windowTimeUs(int64_t(o.value(QStringLiteral("t_us")).toDouble()), t0Us);
        readKp(o.value(QStringLiteral("kp")).toArray(), pf);
        track.smoothedSynth.push_back(std::move(pf));
    }
    return track;
}

} // namespace pinpoint::analysis
