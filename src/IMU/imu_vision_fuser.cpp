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

#include "imu_vision_fuser.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#include "imu_calibration.h"   // toAnatomical (shared A*q*M)
#include "imu_retime.h"        // W2.4: a Witmotion lane dated by its own sample clock
#include "imu_sample.h"
#include "swing_window.h"
#include "format_descriptor.h"            // ImuFormat (nominal rate)
#include "orientation_filter.h"           // MadgwickFilter (re-fusion)
#include "orientation_refuser.h"          // refuseOrientationAdaptive

namespace pinpoint::analysis {

namespace {

// A lane's samples on the instants they are resampled at: the re-timed ones (W2.4).
// Empty when the lane is read through the window on its recorded stamps.
struct DatedLane {
    std::vector<int64_t>   tUs;    // strictly increasing
    std::vector<ImuSample> rows;
};

// The lane's configured rate (0 = unknown).
double nominalHzOf(const SwingWindow &window, SourceId src)
{
    if (const auto *imf = std::get_if<ImuFormat>(&window.formatOf(src).format))
        return double(imf->sample_rate_hz);
    return 0.0;
}

// ⚠ RE-TIME A HOST-STAMPED LANE, AND ONLY THAT. A Witmotion sample is stamped at
// host ARRIVAL (EventBuffer::nowMicros), in bursts of 8 about every 30 ms, so its
// stamp dates it to within a burst; imu_retime.h dates it by the device's own
// sample clock instead. The window keeps the arrivals untouched — this is a
// derived timeline, recomputed on every fusion, never written back. A HackMotion
// lane is dated upstream by its own clock fit and never comes here.
DatedLane retimeLane(const SwingWindow &window, SourceId src, LaneClock &clock)
{
    DatedLane lane;
    std::vector<int64_t> arrival;
    const std::vector<IndexEntry> entries = window.entriesFor(src);
    arrival.reserve(entries.size());
    lane.rows.reserve(entries.size());
    for (const IndexEntry &e : entries) {
        const SourceRing::ReadHandle h = window.payloadOf(e);
        if (!h.data || h.bytes != sizeof(ImuSample))
            continue;
        ImuSample s{};
        std::memcpy(&s, h.data, sizeof(ImuSample));
        lane.rows.push_back(s);
        arrival.push_back(e.timestamp_us);
    }
    const double nominalHz = nominalHzOf(window, src);
    const imu_retime::RetimeResult fit = imu_retime::retimeArrivals(arrival, nominalHz);
    clock.nominalHz     = nominalHz;
    clock.residualP50Us = fit.residualP50Us;
    clock.residualP95Us = fit.residualP95Us;
    clock.residualMaxUs = fit.residualMaxUs;
    clock.lossEvents    = fit.lossEvents;
    clock.lostSamples   = fit.lostSamples;
    clock.segments      = int(fit.segments.size());
    if (!fit.applied || lane.rows.size() < 2)
        return {};   // not one sample clock: read through the window on the stamps as recorded
    clock.retimed  = true;
    clock.periodUs = fit.periodUs;
    lane.tUs = fit.retimedUs;
    return lane;
}

// Interpolate a dated lane at `target_us` — the SAME arithmetic as
// SwingWindow::interpolateImu (lerp accel/gyro, shortest-arc slerp, nlerp when
// nearly identical), on the lane's own instants. False outside them, as there.
bool interpolateDated(const DatedLane &lane, int64_t target_us, ImuSample &o)
{
    const auto after = std::upper_bound(lane.tUs.begin(), lane.tUs.end(), target_us);
    if (after == lane.tUs.end() || after == lane.tUs.begin())
        return false;
    const std::size_t ni = std::size_t(after - lane.tUs.begin());
    const std::size_t pi = ni - 1;
    const ImuSample &p = lane.rows[pi];
    const ImuSample &n = lane.rows[ni];
    const int64_t denom = lane.tUs[ni] - lane.tUs[pi];
    if (denom == 0) { o = p; return true; }
    const float t = static_cast<float>(target_us - lane.tUs[pi]) / static_cast<float>(denom);

    o.accel_x = p.accel_x + t * (n.accel_x - p.accel_x);
    o.accel_y = p.accel_y + t * (n.accel_y - p.accel_y);
    o.accel_z = p.accel_z + t * (n.accel_z - p.accel_z);
    o.gyro_x  = p.gyro_x  + t * (n.gyro_x  - p.gyro_x);
    o.gyro_y  = p.gyro_y  + t * (n.gyro_y  - p.gyro_y);
    o.gyro_z  = p.gyro_z  + t * (n.gyro_z  - p.gyro_z);

    float pw = p.quat_w, px = p.quat_x, py = p.quat_y, pz = p.quat_z;
    float nw = n.quat_w, nx = n.quat_x, ny = n.quat_y, nz = n.quat_z;
    float dot = pw*nw + px*nx + py*ny + pz*nz;
    if (dot < 0.0f) { nw=-nw; nx=-nx; ny=-ny; nz=-nz; dot=-dot; }
    if (dot > 0.9995f) {
        const float rw = pw + t*(nw-pw), rx = px + t*(nx-px);
        const float ry = py + t*(ny-py), rz = pz + t*(nz-pz);
        const float len = std::sqrt(rw*rw + rx*rx + ry*ry + rz*rz);
        o.quat_w = rw/len; o.quat_x = rx/len; o.quat_y = ry/len; o.quat_z = rz/len;
    } else {
        const float theta0    = std::acos(dot);
        const float sinTheta0 = std::sin(theta0);
        const float s0 = std::sin((1.0f - t) * theta0) / sinTheta0;
        const float s1 = std::sin(t * theta0)          / sinTheta0;
        o.quat_w = s0*pw + s1*nw; o.quat_x = s0*px + s1*nx;
        o.quat_y = s0*py + s1*ny; o.quat_z = s0*pz + s1*nz;
    }
    return true;
}

// Re-derive a source's orientation offline from its raw accel+gyro under `cfg` (warm-started from the
// stored quat at the first sample), returning (t_us, q_refused) pairs aligned to the source's samples.
// The nominal cadence is taken from the IMU format so dt matches the live ImuBase::fuseRawImu exactly.
// A re-timed lane (`dated` non-empty) is re-fused on its re-timed instants.
std::vector<std::pair<int64_t, QQuaternion>>
refuseSource(const SwingWindow &window, SourceId src, const pinpoint::RefuseConfig &cfg,
             const DatedLane &dated)
{
    std::vector<pinpoint::RefuseSample> samples;
    auto add = [&samples](int64_t t, const ImuSample &s) {
        samples.push_back(pinpoint::RefuseSample{ t,
                                                  s.accel_x, s.accel_y, s.accel_z,
                                                  s.gyro_x,  s.gyro_y,  s.gyro_z,
                                                  s.quat_w,  s.quat_x,  s.quat_y, s.quat_z });
    };
    if (!dated.tUs.empty()) {
        samples.reserve(dated.tUs.size());
        for (std::size_t i = 0; i < dated.tUs.size(); ++i)
            add(dated.tUs[i], dated.rows[i]);
    } else {
        const std::vector<IndexEntry> entries = window.entriesFor(src);
        samples.reserve(entries.size());
        for (const IndexEntry &e : entries) {
            const SourceRing::ReadHandle h = window.payloadOf(e);
            if (!h.data || h.bytes < sizeof(ImuSample))
                continue;
            ImuSample s{};
            std::memcpy(&s, h.data, sizeof(ImuSample));
            add(e.timestamp_us, s);
        }
    }
    std::vector<std::pair<int64_t, QQuaternion>> seq;
    if (samples.size() < 2)
        return seq;

    pinpoint::RefuseConfig c = cfg;
    if (const auto *imf = std::get_if<ImuFormat>(&window.formatOf(src).format))
        if (imf->sample_rate_hz > 0)
            c.outputRateHz = float(imf->sample_rate_hz);

    MadgwickFilter filt(c.betaStatic);
    const pinpoint::RefuseResult r = pinpoint::refuseOrientationAdaptive(filt, samples, c);
    seq.reserve(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
        seq.emplace_back(samples[i].t_us,
                         QQuaternion(r.quat[i][0], r.quat[i][1], r.quat[i][2], r.quat[i][3]));
    return seq;
}

// Slerp a re-fused (t_us → quat) sequence at grid time t (clamped at the ends).
QQuaternion slerpAt(const std::vector<std::pair<int64_t, QQuaternion>> &seq, int64_t t)
{
    if (seq.empty())            return QQuaternion();
    if (t <= seq.front().first) return seq.front().second;
    if (t >= seq.back().first)  return seq.back().second;
    std::size_t lo = 0, hi = seq.size() - 1;
    while (hi - lo > 1) {
        const std::size_t mid = (lo + hi) / 2;
        if (seq[mid].first <= t) lo = mid; else hi = mid;
    }
    const float u = (seq[hi].first > seq[lo].first)
                        ? float(double(t - seq[lo].first) / double(seq[hi].first - seq[lo].first))
                        : 0.0f;
    return QQuaternion::slerp(seq[lo].second, seq[hi].second, u).normalized();
}

} // namespace

double ImuVisionFuser::effectiveHzFor(const SwingWindow &window, SourceId source,
                                      int64_t startUs, int64_t endUs)
{
    if (endUs <= startUs)
        return 0.0;

    // ⚠ MEASURED OVER THE SPAN ASKED FOR, NOT OVER THE WHOLE WINDOW. A stitched
    // lane's rate is not one number, and the figure that decides whether a
    // metric AT IMPACT can be computed is the one measured around impact.
    const std::vector<pinpoint::IndexEntry> es = window.entriesFor(source);
    size_t n = 0;
    for (const pinpoint::IndexEntry &e : es)
        if (e.timestamp_us >= startUs && e.timestamp_us < endUs) ++n;

    if (n < 2)
        return 0.0;   // ⚠ NOT MEASURABLE, and never "a rate of zero"
    return double(n) * 1.0e6 / double(endUs - startUs);
}

double ImuVisionFuser::peakHzFor(const SwingWindow &window, SourceId source,
                                 int64_t probeUs)
{
    const std::vector<pinpoint::IndexEntry> es = window.entriesFor(source);
    if (es.size() < 2 || probeUs <= 0)
        return 0.0;

    // ⚠ A PEAK, NOT AN AVERAGE, AND THAT IS THE WHOLE POINT. Averaging a
    // stitched lane over the window mixes ~800 Hz through the swing with ~100 Hz
    // over a 3 s still pre-roll and reports something near the base rate — which
    // would size the grid to throw away exactly the dense span the pull was
    // performed to obtain. Sliding a short probe finds that span wherever it is,
    // without depending on the impact estimate being right.
    // ⚠ A WINDOW NARROWER THAN HALF THE PROBE IS NOT RATE EVIDENCE. Witmotion
    // samples are stamped at HOST arrival (WT9011DCL_Base::receiveData), BLE
    // delivers several per connection interval, and the merger clamps
    // non-monotonic stamps to +1µs — so a real lane carries clusters of
    // near-identical timestamps, and at the lane head or after any delivery gap
    // the sliding window can shrink to a single burst pair. Two mates 1µs apart
    // read as ~10^6 Hz, clamp to kGridHzMax, and the grid then flips 200↔800
    // per shot depending on where the window trim lands inside a burst. A
    // genuinely dense lane (a deferred pull) is dense across the whole probe,
    // so requiring the window to have real width costs it nothing.
    const int64_t minSpanUs = probeUs / 2;
    size_t lo = 0;
    double best = 0.0;
    for (size_t hi = 0; hi < es.size(); ++hi) {
        while (es[hi].timestamp_us - es[lo].timestamp_us > probeUs)
            ++lo;
        if (hi == lo) continue;
        const int64_t span = es[hi].timestamp_us - es[lo].timestamp_us;
        if (span < minSpanUs) continue;
        best = std::max(best, double(hi - lo) * 1.0e6 / double(span));
    }
    return best;
}

void ImuVisionFuser::highRateSpanFor(const SwingWindow &window, SourceId source,
                                     double thresholdHz, int64_t out[2])
{
    out[0] = 0;
    out[1] = 0;
    if (thresholdHz <= 0.0)
        return;

    const std::vector<pinpoint::IndexEntry> es = window.entriesFor(source);
    if (es.size() < 2)
        return;

    // The widest spacing that still counts as "above the threshold".
    const int64_t maxStepUs = int64_t(1.0e6 / thresholdHz);

    bool    found = false;
    int64_t first = 0, last = 0;
    for (size_t i = 1; i < es.size(); ++i) {
        const int64_t step = es[i].timestamp_us - es[i - 1].timestamp_us;
        if (step <= 0 || step > maxStepUs)
            continue;
        if (!found) { first = es[i - 1].timestamp_us; found = true; }
        last = es[i].timestamp_us;
    }
    if (!found)
        return;
    out[0] = first;
    out[1] = last;
}

double ImuVisionFuser::gridHzForWindow(const SwingWindow &window,
                                       const std::vector<ImuSegmentBinding> &bindings)
{
    double fastest = 0.0;
    for (const ImuSegmentBinding &b : bindings) {
        if (b.role == SegmentRole::Unknown) continue;
        fastest = std::max(fastest, peakHzFor(window, b.source, kProbeUs));
    }

    // ⚠ Nothing measurable is NOT a reason to change the grid. A window with one
    // sample per lane must produce what it always produced. And a peak within
    // the floor's slack band is an ordinary lane measured through bursty host
    // stamps, not a faster lane — it lands on the floor EXACTLY, or the corpus
    // moves by the width of the measurement noise.
    if (fastest <= kGridHzMin * kGridHzFloorSlack)
        return kGridHzMin;
    return std::clamp(fastest, kGridHzMin, kGridHzMax);
}

FusedStreams ImuVisionFuser::fuse(const SwingWindow &window,
                                  const std::vector<ImuSegmentBinding> &bindings,
                                  double gridHz,
                                  const pinpoint::RefuseConfig *refusion)
{
    FusedStreams out;

    // Gather usable bindings (known role + ≥2 samples) and intersect their coverage
    // with the window span so every grid instant is interpolatable for every segment.
    struct Bound {
        const ImuSegmentBinding *b;
        std::vector<std::pair<int64_t, QQuaternion>> refused;   // empty unless filter.refuse is on
        DatedLane dated;     // the re-timed lane (W2.4); empty: read through the window
        LaneClock clock;
    };
    std::vector<Bound> bound;
    int64_t gridStart = window.startTimestampUs();
    int64_t gridEnd   = window.endTimestampUs();
    for (const ImuSegmentBinding &b : bindings) {
        if (b.role == SegmentRole::Unknown)
            continue;
        const std::vector<IndexEntry> entries = window.entriesFor(b.source);
        if (entries.size() < 2)
            continue;
        Bound bb{ &b, {}, {}, {} };
        bool hmLane = b.hackMotion;
        if (const auto *imf = std::get_if<ImuFormat>(&window.formatOf(b.source).format))
            hmLane = hmLane || imf->device == DeviceKind::IMU_HackMotion;
        if (!hmLane)
            bb.dated = retimeLane(window, b.source, bb.clock);
        // The coverage is the lane's samples on the instants they are resampled at.
        gridStart = std::max(gridStart, bb.dated.tUs.empty() ? entries.front().timestamp_us
                                                             : bb.dated.tUs.front());
        gridEnd   = std::min(gridEnd,   bb.dated.tUs.empty() ? entries.back().timestamp_us
                                                             : bb.dated.tUs.back());
        // ⚠ NEVER RE-FUSE A HACKMOTION LANE. refuseSource() re-runs Madgwick from the
        // recorded accel+gyro, but a wG3's accel column is GRAVITY-REMOVED linear
        // acceleration (hm_sample_convert.h:36-42), not the raw accelerometer vector
        // Madgwick's gravity reference needs — and the stored quaternion is the
        // device's own calibrated orientation, not a host-side fusion of those
        // vectors at all. The re-fused trajectory is therefore meaningless here, and
        // it would then be conjugated below as if it were the device's own output.
        // Same reasoning imu_refusion_check.h applies to this device, for the same
        // physical reason.
        if (refusion && !b.hackMotion)
            bb.refused = refuseSource(window, b.source, *refusion, bb.dated);
        bound.push_back(std::move(bb));
    }
    if (bound.empty() || gridEnd <= gridStart)
        return out;

    const int64_t dt = static_cast<int64_t>(1.0e6 / gridHz + 0.5);
    if (dt <= 0)
        return out;
    for (int64_t t = gridStart; t <= gridEnd; t += dt)
        out.timeGrid.push_back(t);

    for (const Bound &bd : bound) {
        SegmentStream s;
        s.role       = bd.b->role;
        s.hackMotion = bd.b->hackMotion;
        s.clock      = bd.clock;
        s.qAnat.reserve(out.timeGrid.size());
        s.gyroDps.reserve(out.timeGrid.size());
        s.accelG.reserve(out.timeGrid.size());
        // ImuSample vectors are RAW sensor-frame (imu_sample.h v2); rotate them
        // into the anatomical segment frame (v_anat = M⁻¹·v_sensor) so they share
        // qAnat's body frame. M is unit, so conjugated() is its inverse.
        //
        // ⚠ AND THIS LINE IS ALREADY CORRECT FOR A HACKMOTION — it needs no
        // instrument branch, which is worth stating because the quaternion path a
        // dozen lines below DOES. For a wG3, M is hm_frame::mountM() = R_ph, so
        // mountInv = R_ph* = frameMap(candidate) — character-for-character what
        // hm_frame::pronationRateDps already rotates its gyro by (hm_frame.h:281).
        // The device's gyro shares its quaternion's frame and its calibration
        // re-references both together, so only the constant map is needed and no
        // conjugate arises. The asymmetry (vectors fine, orientation not) is exactly
        // what makes the missing conjugate below so easy to walk past.
        const QQuaternion mountInv = bd.b->mountM.conjugated();
        QQuaternion last;            // hold-last fallback for a momentary gap
        QVector3D   lastGyro, lastAccel;
        bool haveLast = false;
        for (const int64_t t : out.timeGrid) {
            ImuSample smp{};
            QQuaternion qAnat;
            QVector3D   gyro, accel;
            const bool have = bd.dated.tUs.empty()
                ? window.interpolateImu(bd.b->source, t,
                                        reinterpret_cast<std::byte *>(&smp), sizeof(smp))
                : interpolateDated(bd.dated, t, smp);
            if (have) {
                // q_raw: the re-fused orientation when filter.refuse is on, else the stored quat.
                QQuaternion qRaw = bd.refused.empty()
                    ? QQuaternion(smp.quat_w, smp.quat_x, smp.quat_y, smp.quat_z)
                    : slerpAt(bd.refused, t);
                // ⚠⚠ THE CONJUGATE, AND IT IS THE ONE PLACE BEING WRONG IS INVISIBLE.
                // A wG3 streams WORLD->BODY; toAnatomical() composes A·q_raw·M and
                // wrist_angles.h's qFore⁻¹·qHand only typechecks for body->world. The
                // contract is q_anat = q_hm* ⊗ R_ph (hm_frame.h:130) — that is
                // toAnatomical(A = identity, q_raw = q_hm*, M = R_ph), with the raw
                // quaternion ALREADY conjugated. So it is conjugated here, once.
                //
                // ⚠ Not in hm_sample_convert.h, which stores the streamed quaternion
                // verbatim and explains at length why it refuses — that refusal is
                // correct, and conjugating in two places is the same as conjugating in
                // none. Not inside imu_calibration::toAnatomical either: that is the
                // shared composition site for every device and knows about none of them.
                //
                // ⚠ Nothing downstream can catch this if it is dropped. The wrist ANGLE
                // is convention-blind, all four frame candidates and both composition
                // orders score exactly zero cross-talk, and the result tracks the wrist
                // convincingly with every decomposed sign inverted. Only a motion whose
                // direction was recorded distinguishes them (hm_frame.h:69-108).
                if (bd.b->hackMotion)
                    qRaw = qRaw.conjugated();
                qAnat     = imu_calibration::toAnatomical(bd.b->alignA, qRaw, bd.b->mountM);
                gyro      = mountInv.rotatedVector(QVector3D(smp.gyro_x, smp.gyro_y, smp.gyro_z));
                accel     = mountInv.rotatedVector(QVector3D(smp.accel_x, smp.accel_y, smp.accel_z));
                last      = qAnat;
                lastGyro  = gyro;
                lastAccel = accel;
                haveLast  = true;
            } else {
                qAnat = haveLast ? last : QQuaternion();   // identity until first valid sample
                gyro  = lastGyro;                          // zero until first valid sample
                accel = lastAccel;
            }
            s.qAnat.push_back(qAnat);
            s.gyroDps.push_back(gyro);
            s.accelG.push_back(accel);
        }
        out.segments.push_back(std::move(s));
    }
    return out;
}

} // namespace pinpoint::analysis
