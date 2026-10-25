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

#include "session_bridge.h"
#include "session_warp.h"
#include "../../Kinematics/gap_bridge.h"

#include <algorithm>
#include <cmath>

namespace pinpoint::charts {

namespace A = pinpoint::analysis;

namespace {

double phaseMs(const ShotSeries &s, int ph)
{
    for (const PhaseMark &m : s.phases)
        if (m.phase == ph && s.impactUs >= 0) return double(m.tUs - s.impactUs) / 1000.0;
    return kNaN;
}

A::GapBridgeOptions coreOptions(const BridgeOptions &o)
{
    A::GapBridgeOptions g;
    g.beforeMs = o.beforeMs; g.afterMs = o.afterMs; g.searchMarginMs = o.searchMarginMs; g.gridStepMs = o.gridStepMs;
    g.draws = o.draws; g.maxGapMs = o.maxHoleMs; g.minEachSide = o.minEachSide; g.nearFromMs = o.nearFromMs;
    g.nearToMs = o.nearToMs; g.edgeShare = o.edgeShare;
    return g;
}

BridgeOutcome outcomeOf(A::GapBridgeOutcome o)
{
    switch (o) {
    case A::GapBridgeOutcome::NoGap:          return BridgeOutcome::NoHole;
    case A::GapBridgeOutcome::GapTooLong:     return BridgeOutcome::HoleTooLong;
    case A::GapBridgeOutcome::TooLittleCurve: return BridgeOutcome::TooLittleCurve;
    case A::GapBridgeOutcome::StillRising:    return BridgeOutcome::StillRising;
    case A::GapBridgeOutcome::FellIntoGap:    return BridgeOutcome::FellIntoHole;
    case A::GapBridgeOutcome::Bridged:        return BridgeOutcome::Bridged;
    }
    return BridgeOutcome::NoHole;
}

// THE ANALYSIS' OWN PROBABLE PEAK (segment_rates.cpp, KsNode::probable), when the swing was analysed with the
// gap bridge: read, never recomputed, so Session and This shot say the same number.
bool storedBridge(const ShotSeries &shot, ChestBridge *b)
{
    for (const QVariant &nv : shot.kinematicSequence.value(QStringLiteral("nodes")).toList()) {
        const QVariantMap n = nv.toMap();
        if (n.value(QStringLiteral("segment")).toString() != QLatin1String("thorax")) continue;
        const QVariantMap pr = n.value(QStringLiteral("probable")).toMap();
        if (pr.isEmpty()) return false;
        // the highest rate the camera saw (sequence.chestIndicated): the analysis' reading stands, but nothing was
        // bridged — the sequence already carries it (shotSequence), and the session draws no fit for it
        if (pr.value(QStringLiteral("how")).toString() == QLatin1String("seen")) { b->stored = true; return true; }
        const QVariantMap br = pr.value(QStringLiteral("bridge")).toMap();
        for (const QVariant &x : br.value(QStringLiteral("tMs")).toList())  b->t.push_back(x.toDouble());
        for (const QVariant &x : br.value(QStringLiteral("mean")).toList()) b->mean.push_back(x.toDouble());
        for (const QVariant &x : br.value(QStringLiteral("sd")).toList())   b->sd.push_back(x.toDouble());
        b->outcome = BridgeOutcome::Bridged;
        b->stored = true;
        b->holeFromMs = b->t.empty() ? kNaN : b->t.front();
        b->holeToMs = b->t.empty() ? kNaN : b->t.back();
        b->peakMs = pr.value(QStringLiteral("peakMs"), kNaN).toDouble();
        b->peakSigmaMs = pr.value(QStringLiteral("tSigmaMs"), kNaN).toDouble();
        b->peakDps = pr.value(QStringLiteral("peakDps"), kNaN).toDouble();
        b->peakSigmaDps = pr.value(QStringLiteral("peakSigmaDps"), kNaN).toDouble();
        b->earlierMs = pr.value(QStringLiteral("earlierMs"), kNaN).toDouble();
        b->earlierDps = pr.value(QStringLiteral("earlierDps"), kNaN).toDouble();
        return true;
    }
    return false;
}

} // namespace

SessionBridges bridgeChest(const SessionSnapshot &session, const SessionSequence &sequence, const BridgeOptions &o)
{
    SessionBridges out;
    out.shots.resize(session.shots.size());
    const QString key = sequenceSeriesKey(kChestSegment);
    const A::GapBridgeOptions go = coreOptions(o);
    struct Candidate { size_t index; std::vector<double> t, v; std::vector<uint8_t> ok; std::vector<double> tt, vv; double topMs, p8Ms; };
    std::vector<Candidate> cands;
    for (size_t si = 0; si < session.shots.size(); ++si) {
        const ShotSeries &shot = *session.shots[si];
        ChestBridge &b = out.shots[si];
        b.ordinal = shot.ordinal;
        if (shot.impactUs < 0) continue;
        if (si < sequence.shots.size() && sequence.shots[si].segments[size_t(kChestSegment)].placed) continue;
        if (storedBridge(shot, &b)) continue;          // the analysis bridged it: its numbers stand
        const TraceCurve c = shotCurve(shot, key);
        if (c.empty()) continue;
        const A::ImpactGap gap = A::findImpactGap(c.t, c.ok, go);
        if (!gap.found) continue;
        b.holeFromMs = gap.fromMs;
        b.holeToMs = gap.toMs;
        if (gap.toMs - gap.fromMs > o.maxHoleMs) { b.outcome = BridgeOutcome::HoleTooLong; continue; }
        Candidate cd{ si, c.t, c.v, c.ok, {}, {}, phaseMs(shot, phase::Top), phaseMs(shot, phase::P8) };
        if (!A::gapTrainingSet(c.t, c.v, c.ok, gap, go, &cd.tt, &cd.vv)) { b.outcome = BridgeOutcome::TooLittleCurve; continue; }
        cands.push_back(std::move(cd));
    }

    // the session's kernel, for the shots the analysis did not bridge: the length and height that best explain
    // every such shot's curve at once (fewer than three: the start values)
    BridgeKernel kern;
    kern.noiseDps = o.noiseDps;
    kern.ellMs = o.ellStartMs;
    kern.sfDps = o.sfStartDps;
    kern.shots = int(cands.size());
    if (cands.size() >= 3) {
        std::vector<std::vector<double>> ts, vs;
        for (const Candidate &cd : cands) { ts.push_back(cd.tt); vs.push_back(cd.vv); }
        bool ok = false;
        const A::GapBridgeKernel k = A::fitGapBridgeKernel(ts, vs, { o.ellStartMs, o.sfStartDps, o.noiseDps }, &ok);
        if (ok) { kern.ellMs = k.ellMs; kern.sfDps = k.sfDps; kern.fitted = true; }
    }
    out.kernel = kern;

    // each shot conditioned on its own samples
    for (const Candidate &cd : cands) {
        ChestBridge &b = out.shots[cd.index];
        const quint64 seed = 0x9E3779B97F4A7C15ULL * quint64(std::max(1, b.ordinal)) + 1ULL;
        const A::GapBridge g = A::bridgeImpactGap(cd.t, cd.v, cd.ok, cd.topMs, cd.p8Ms,
                                                  { kern.ellMs, kern.sfDps, kern.noiseDps }, seed, go);
        b.outcome = outcomeOf(g.outcome);
        b.searchFromMs = g.searchFromMs; b.searchToMs = g.searchToMs;
        b.t = g.tMs; b.mean = g.mean; b.sd = g.sd;
        b.edgeShare = g.edgeShare;
        if (!g.bridged()) continue;
        b.peakMs = g.peakMs; b.peakSigmaMs = g.peakSigmaMs;
        b.peakLoMs = g.peakMs - g.peakSigmaMs; b.peakHiMs = g.peakMs + g.peakSigmaMs;
        b.peakDps = g.peakValue; b.peakSigmaDps = g.peakSigmaValue;
        b.earlierMs = g.earlierMs; b.earlierDps = g.earlierValue;
    }
    return out;
}

void applyBridges(SessionSequence &sequence, const SessionBridges &bridges)
{
    const auto &ids = sequenceSegmentIds();
    for (size_t i = 0; i < sequence.shots.size() && i < bridges.shots.size(); ++i) {
        const ChestBridge &b = bridges.shots[i];
        ShotSequence &q = sequence.shots[i];
        SegmentPeak &p = q.segments[size_t(kChestSegment)];
        if (!b.bridged() || p.placed) continue;
        p.probable = true;
        p.probableMs = b.peakMs;
        p.probableSigmaMs = b.peakSigmaMs;
        p.probableDps = b.peakDps;
        p.probableSigmaDps = b.peakSigmaDps;
        rankSequence(q);
        std::vector<int> order;
        for (int s = 0; s < kSequenceSegments; ++s) if (q.segments[size_t(s)].timed()) order.push_back(s);
        std::stable_sort(order.begin(), order.end(), [&](int a, int c) { return q.segments[size_t(a)].timeMs() < q.segments[size_t(c)].timeMs(); });
        q.order.clear();
        for (int s : order) q.order << ids[size_t(s)];
    }
    summariseSequence(sequence);
}

int SessionBridges::bridgedCount() const
{
    int k = 0;
    for (const ChestBridge &b : shots) k += b.bridged() ? 1 : 0;
    return k;
}

int SessionBridges::attemptedCount() const
{
    int k = 0;
    for (const ChestBridge &b : shots) k += b.outcome != BridgeOutcome::NoHole ? 1 : 0;
    return k;
}

int SessionBridges::earlierCount() const
{
    int k = 0;
    for (const ChestBridge &b : shots) k += b.bridged() && std::isfinite(b.earlierMs) ? 1 : 0;
    return k;
}

double SessionBridges::holeMinMs() const
{
    double v = kNaN;
    for (const ChestBridge &b : shots)
        if (b.bridged()) v = std::isfinite(v) ? std::min(v, b.holeToMs - b.holeFromMs) : b.holeToMs - b.holeFromMs;
    return v;
}

double SessionBridges::holeMaxMs() const
{
    double v = kNaN;
    for (const ChestBridge &b : shots)
        if (b.bridged()) v = std::isfinite(v) ? std::max(v, b.holeToMs - b.holeFromMs) : b.holeToMs - b.holeFromMs;
    return v;
}

const ChestBridge *SessionBridges::forOrdinal(int ordinal) const
{
    for (const ChestBridge &b : shots) if (b.ordinal == ordinal) return &b;
    return nullptr;
}

} // namespace pinpoint::charts
