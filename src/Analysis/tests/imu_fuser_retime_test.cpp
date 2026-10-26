// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// imu_fuser_retime_test — the fuser resamples a Witmotion lane on its
// RE-TIMED instants, and leaves a HackMotion lane exactly as it was (W2.4).
// ---------------------------------------------------------------------------
//
// imu_retime_test pins the arithmetic; this pins the wiring, over a real
// SwingWindow: a lane whose host stamps arrive in bursts of 8 on a 30 ms
// connection grid carries a trunk-like gyro peak, and the peak the fuser puts
// on the grid must land where the device saw it — not up to a burst later,
// where the arrival stamps put it. The same lane flagged as a HackMotion must
// come back byte-identical to SwingWindow::interpolateImu, with no clock fit
// recorded, because a wG3 is dated by its own clock fit upstream.
#include "../../IMU/imu_vision_fuser.h"

#include "swing_window.h"          // before ram_payload_source.h: it derives SwingPayloadSource
#include "ram_payload_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;

static void check(bool ok, const char *label, const char *detail = "")
{
    std::printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", label, detail);
    if (!ok) ++g_fail;
}

namespace {

constexpr double kPeriodUs = 5000.0 / 0.993;   // the W0 clock: 198.6 Hz at a configured 200
constexpr double kPeakUs   = 2.4e6;            // the trunk's peak, on the device's clock
constexpr double kWidthUs  = 40000.0;

float gyroAt(double t) { return float(600.0 * std::exp(-std::pow((t - kPeakUs) / kWidthUs, 2.0))); }

struct Lane {
    std::vector<int64_t>   arrival;
    std::vector<ImuSample> rows;
};

// 4.8 s at 198.6 Hz, delivered 8 at a time at the next 30 ms connection event.
Lane makeLane()
{
    Lane l;
    uint64_t rng = 0x9e3779b97f4a7c15ull;
    auto uni = [&rng]() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return double(rng >> 11) / 9007199254740992.0; };
    for (int b = 0; b < 120; ++b) {
        const double ready = kPeriodUs * double(b * 8 + 7) + 1500.0;
        double ev = 4100.0 + std::ceil((ready - 4100.0) / 30000.0) * 30000.0;
        if (uni() < 0.03) ev += 30000.0;
        const double host = 3000.0 * uni() * uni();
        for (int i = 0; i < 8; ++i) {
            const double t = kPeriodUs * double(b * 8 + i);
            ImuSample s{};
            s.accel_z = 1.0f;
            s.gyro_z = gyroAt(t);
            s.quat_w = 1.0f;
            int64_t a = int64_t(std::llround(ev + host + 180.0 * i));
            if (!l.arrival.empty() && a <= l.arrival.back()) a = l.arrival.back() + 1;
            l.arrival.push_back(a);
            l.rows.push_back(s);
        }
    }
    return l;
}

SwingWindow makeWindow(const Lane &l, DeviceKind kind)
{
    auto src = std::make_unique<RamPayloadSource>();
    FormatDescriptor fd;
    fd.device = kind;
    fd.device_serial = "retime-lane";
    ImuFormat imf{};
    imf.device = kind;
    imf.sample_rate_hz = 200;
    imf.packet_bytes = sizeof(ImuSample);
    imf.packet_schema = "imu_sample_v2";
    fd.format = imf;
    std::vector<IndexEntry> entries;
    for (size_t i = 0; i < l.arrival.size(); ++i)
        entries.push_back(IndexEntry{ l.arrival[i], SourceId(0), uint64_t(i) });
    src->addImu(SourceId(0), fd, l.rows);
    return SwingWindow(std::move(src), std::move(entries), 0, l.arrival.back());
}

ImuSegmentBinding binding(bool hm)
{
    ImuSegmentBinding b;
    b.source = SourceId(0);
    b.role = SegmentRole::Pelvis;
    b.hackMotion = hm;
    return b;
}

double peakTime(const FusedStreams &f)
{
    const SegmentStream &s = f.segments.front();
    size_t best = 0;
    for (size_t i = 1; i < s.gyroDps.size(); ++i)
        if (s.gyroDps[i].z() > s.gyroDps[best].z()) best = i;
    // Parabolic refinement through the three samples about the maximum.
    if (best > 0 && best + 1 < s.gyroDps.size()) {
        const double y0 = s.gyroDps[best - 1].z(), y1 = s.gyroDps[best].z(), y2 = s.gyroDps[best + 1].z();
        const double den = y0 - 2.0 * y1 + y2;
        const double dx = den != 0.0 ? 0.5 * (y0 - y2) / den : 0.0;
        return double(f.timeGrid[best]) + dx * double(f.timeGrid[best + 1] - f.timeGrid[best]);
    }
    return double(f.timeGrid[best]);
}

} // namespace

int main()
{
    std::printf("=== the fuser on re-timed Witmotion instants (W2.4) ===\n\n");
    char buf[200];
    const Lane lane = makeLane();

    // ── Witmotion: re-timed ───────────────────────────────────────────────────
    const SwingWindow w = makeWindow(lane, DeviceKind::IMU_WitMotion);
    const FusedStreams f = ImuVisionFuser::fuse(w, { binding(false) });
    check(f.segments.size() == 1, "one stream");
    if (f.segments.empty()) return 1;
    const LaneClock &c = f.segments.front().clock;
    std::snprintf(buf, sizeof(buf), "period %.2f us, p95 %.1f ms, %d lost", c.periodUs, c.residualP95Us / 1e3, c.lostSamples);
    check(c.retimed && std::fabs(c.periodUs / kPeriodUs - 1.0) < 1e-3 && c.lostSamples == 0,
          "the lane was re-timed on its own period", buf);
    check(c.nominalHz == 200.0 && c.segments == 1, "provenance: configured rate and one segment");

    const double peak = peakTime(f);
    std::snprintf(buf, sizeof(buf), "%+.2f ms", (peak - kPeakUs) / 1e3);
    // The retimed instants sit on the fastest delivery — a millisecond or two late, never early.
    check(peak - kPeakUs > -500.0 && peak - kPeakUs < 4000.0, "the peak lands where the device saw it", buf);

    // The same lane on its arrival stamps, for scale: what the fuser did before.
    {
        double bestV = -1.0, bestT = 0.0;
        for (int64_t t = w.startTimestampUs(); t < w.endTimestampUs(); t += 1000) {
            ImuSample s{};
            if (w.interpolateImu(SourceId(0), t, reinterpret_cast<std::byte *>(&s), sizeof(s)) && s.gyro_z > bestV) {
                bestV = s.gyro_z; bestT = double(t);
            }
        }
        std::snprintf(buf, sizeof(buf), "arrival stamps put it %+.1f ms late", (bestT - kPeakUs) / 1e3);
        check(bestT - kPeakUs > 10000.0, "(control) the arrivals alone are a burst late", buf);
    }

    // ── HackMotion: untouched ─────────────────────────────────────────────────
    const SwingWindow wh = makeWindow(lane, DeviceKind::IMU_HackMotion);
    const FusedStreams h = ImuVisionFuser::fuse(wh, { binding(true) });
    bool same = !h.segments.empty();
    for (size_t i = 0; same && i < h.timeGrid.size(); ++i) {
        ImuSample s{};
        const bool ok = wh.interpolateImu(SourceId(0), h.timeGrid[i], reinterpret_cast<std::byte *>(&s), sizeof(s));
        same = ok && h.segments.front().gyroDps[i].z() == s.gyro_z;
    }
    check(same, "a HackMotion lane is resampled on its stamps, exactly as before");
    check(!h.segments.empty() && !h.segments.front().clock.retimed && h.segments.front().clock.periodUs == 0.0,
          "and records no re-timing");
    check(h.timeGrid.front() == lane.arrival.front(), "its grid starts at its first stamp");

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
