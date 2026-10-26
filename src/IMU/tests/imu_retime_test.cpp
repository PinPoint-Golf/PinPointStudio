// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// imu_retime_test — dating a host-stamped Witmotion lane by its own clock
// (imu_retime.h; trunk_imu_sequence_grf_design.md §7.1 step 1, W2.4).
// ---------------------------------------------------------------------------
//
// The synthetic link is built the way the W0 desk records say the real one
// works, not the way it is convenient to imagine: the device takes a sample
// every T (0.7 % slow), hands 8 at a time to the radio, and the host delivers
// each batch at its next 30 ms connection event, occasionally one late, the 8
// stamps ~0.2 ms apart as the driver parses them. That geometry is what makes
// the delivery floor a SAWTOOTH, and the sawtooth is what fooled the first
// loss detector into ~100 two-sample "losses" on a lossless two-minute record.
// So the first thing pinned is the control: a lossless slow-clock stream must
// come back with NO loss.
//
// Errors are judged after removing the one constant the arrivals cannot know —
// the fastest delivery latency, which the lower envelope sits on — and that
// constant is itself checked to be small and never negative (a retimed sample
// can never be dated BEFORE it was taken).
//
// Replay: `imu_retime_test <dir>...` additionally reads every
// pinpoint.imu_record/1 CSV in each directory (PINPOINT_IMU_RECORD_DIR output,
// e.g. the W0 desk records on the share) and prints the fit for the whole file
// and for 3 s / 5 s windows against it. ctest runs it with no arguments; the
// replay is a check run by hand, never a test that can fail on a missing share.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "imu_retime.h"

using namespace pinpoint::imu_retime;

namespace {

int g_fail = 0;

void check(bool ok, const char *label, const std::string &detail = {})
{
    std::printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", label, detail.c_str());
    if (!ok) ++g_fail;
}

std::string fmt(const char *f, double a, double b = 0.0, double c = 0.0)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

// Deterministic, portable (std distributions are not specified bit-for-bit).
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 1) {}
    double uniform()   // [0, 1)
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return double(s >> 11) * (1.0 / 9007199254740992.0);
    }
};

struct Stream {
    int                  droppedInStall = 0;   // samples the device's queue could not hold
    std::vector<int64_t> arrival;   // host stamps
    std::vector<double>  truth;     // the instant each arrived sample was taken
    std::vector<int64_t> device;    // its device index
};

struct LinkSpec {
    double periodUs   = 5000.0 / 0.993;    // the W0 device clock: 198.6 Hz at a configured 200
    int    batch      = 8;
    double ciUs       = 30000.0;           // host connection interval
    double ciPhaseUs  = 7300.0;
    double skipProb   = 0.03;              // a connection event that carries nothing
    double parseUs    = 180.0;             // stamp spacing inside one delivery
    double latencyUs  = 1500.0;            // radio + stack, before the connection event
    double hostJitterUs = 3000.0;          // the host's own delay after the event (skewed small):
                                           // why the W0 floor bottoms scatter 0.3–3 ms
    int    batches    = 700;               // 700 × 40 ms ≈ 28 s
    std::set<int> lostBatches;             // whole batches that never arrive
    std::set<int64_t> lostSamples;         // single device samples that never arrive
    double stallFromUs = -1.0, stallUs = 0.0;   // the link carries nothing in [from, from+len)
    int    queueKeep = 2;                  // batches the device holds through a stall (the newest)
};

// Batched delivery on a connection-event grid — the W0 geometry.
Stream simulateBatched(const LinkSpec &L, uint64_t seed)
{
    Rng rng(seed);
    Stream s;
    const double t0 = 1.0e6;
    double lastEvent = -1e300;
    for (int b = 0; b < L.batches; ++b) {
        const int64_t k0 = int64_t(b) * L.batch;
        const double ready = t0 + L.periodUs * double(k0 + L.batch - 1) + L.latencyUs;
        double ev = L.ciPhaseUs + std::ceil((ready - L.ciPhaseUs) / L.ciUs) * L.ciUs;
        while (rng.uniform() < L.skipProb) ev += L.ciUs;
        if (L.stallFromUs >= 0.0 && ev >= L.stallFromUs && ev < L.stallFromUs + L.stallUs) {
            // Queued through the stall; only the newest queueKeep batches survive it.
            const double lastReady = L.stallFromUs + L.stallUs;
            const int newer = int((lastReady - ready) / (L.periodUs * L.batch));
            if (newer >= L.queueKeep) { s.droppedInStall += L.batch; continue; }
            ev = L.ciPhaseUs + std::ceil((L.stallFromUs + L.stallUs - L.ciPhaseUs) / L.ciUs) * L.ciUs;
        }
        ev = std::max(ev, lastEvent);   // the link delivers in order
        lastEvent = ev;
        const double host = L.hostJitterUs * rng.uniform() * rng.uniform();
        if (L.lostBatches.count(b)) continue;
        int j = 0;
        for (int i = 0; i < L.batch; ++i) {
            const int64_t k = k0 + i;
            if (L.lostSamples.count(k)) continue;
            const double a = ev + host + L.parseUs * double(j++) + 60.0 * rng.uniform();
            s.arrival.push_back(int64_t(std::llround(a)));
            s.truth.push_back(t0 + L.periodUs * double(k));
            s.device.push_back(k);
        }
    }
    for (size_t i = 1; i < s.arrival.size(); ++i)   // the merger's monotonic clamp
        if (s.arrival[i] <= s.arrival[i - 1]) s.arrival[i] = s.arrival[i - 1] + 1;
    return s;
}

struct Err { double offset, spread, maxAbs; };

// retimed − truth over [from, to), after removing its median (the unknowable latency).
Err errors(const RetimeResult &r, const Stream &s, size_t from = 0, size_t to = SIZE_MAX,
           const std::set<size_t> *skip = nullptr)
{
    std::vector<double> e;
    to = std::min(to, s.truth.size());
    for (size_t i = from; i < to; ++i)
        if (!skip || !skip->count(i)) e.push_back(double(r.retimedUs[i]) - s.truth[i]);
    std::vector<double> sorted = e;
    std::sort(sorted.begin(), sorted.end());
    const double med = sorted[sorted.size() / 2];
    double mx = 0.0;
    for (double v : e) mx = std::max(mx, std::fabs(v - med));
    return { med, sorted.back() - sorted.front(), mx };
}

size_t indexOfDevice(const Stream &s, int64_t k)
{
    return size_t(std::lower_bound(s.device.begin(), s.device.end(), k) - s.device.begin());
}

void testLosslessControl()
{
    std::printf("\n-- lossless slow clock: the sawtooth is not loss\n");
    LinkSpec L;
    const Stream s = simulateBatched(L, 11);
    const RetimeResult r = retimeArrivals(s.arrival, 200.0);
    check(r.applied, "applied");
    check(r.lossEvents == 0, "no loss found on a lossless link", fmt("%.0f events", r.lossEvents));
    check(std::fabs(r.periodUs / L.periodUs - 1.0) < 2e-4, "period fitted, not assumed",
          fmt("%.2f us vs truth %.2f", r.periodUs, L.periodUs));
    check(r.batchSize == 8, "batch size seen", fmt("%.0f", r.batchSize));
    const Err e = errors(r, s);
    check(e.maxAbs < 1000.0, "every sample within 1 ms of truth", fmt("max |e| %.0f us", e.maxAbs));
    check(e.offset >= 0.0 && e.offset < 5000.0, "anchored on the floor: never before the sample",
          fmt("offset %.0f us", e.offset));
    // The raw arrivals, for scale.
    double rawMax = 0.0;
    for (size_t i = 0; i < s.truth.size(); ++i)
        rawMax = std::max(rawMax, double(s.arrival[i]) - s.truth[i]);
    check(rawMax > 30000.0, "the arrivals themselves are off by a burst", fmt("max %.0f us", rawMax));
    check(r.residualP95Us > 20000.0 && r.residualMaxUs < 150000.0, "residual stats populated",
          fmt("p50 %.1f p95 %.1f max %.1f ms", r.residualP50Us / 1e3, r.residualP95Us / 1e3, r.residualMaxUs / 1e3));
}

void testLosses()
{
    std::printf("\n-- a whole batch and single samples lost\n");
    LinkSpec L;
    L.lostBatches = { 200 };
    L.lostSamples = { 450 * 8 + 3, 560 * 8 + 5, 560 * 8 + 6 };   // one, then two from one batch
    const Stream s = simulateBatched(L, 23);
    const RetimeResult r = retimeArrivals(s.arrival, 200.0);
    check(r.applied, "applied");
    check(r.lostSamples == 11, "11 samples lost in all", fmt("%.0f found in %.0f events", r.lostSamples, r.lossEvents));
    const size_t iBatch = indexOfDevice(s, 201 * 8);
    const size_t iOne   = indexOfDevice(s, 450 * 8);
    const size_t iTwo   = indexOfDevice(s, 560 * 8);
    bool batchAt = false, oneAt = false, twoAt = false;
    for (const RetimeLoss &l : r.losses) {
        std::printf("     loss at %zu: %d (%s)\n", l.index, l.samples,
                    l.kind == RetimeLossKind::BatchResidue ? "batch residue"
                    : l.kind == RetimeLossKind::EnvelopeStep ? "envelope step" : "segment break");
        batchAt |= l.index == iBatch && l.samples == 8 && l.kind == RetimeLossKind::EnvelopeStep;
        oneAt   |= l.index == iOne   && l.samples == 1 && l.kind == RetimeLossKind::BatchResidue;
        twoAt   |= l.index == iTwo   && l.samples == 2 && l.kind == RetimeLossKind::BatchResidue;
    }
    check(batchAt, "whole batch: 8 samples, at the delivery after the gap");
    check(oneAt, "one sample: by the batch residue, at its delivery");
    check(twoAt, "two samples from one batch: by the batch residue");
    // Inside a short delivery WHERE the sample went is unknowable; outside them, exact.
    std::set<size_t> shortBatches;
    for (int64_t k = 450 * 8; k < 451 * 8; ++k) shortBatches.insert(indexOfDevice(s, k));
    for (int64_t k = 560 * 8; k < 561 * 8; ++k) shortBatches.insert(indexOfDevice(s, k));
    const Err e = errors(r, s, 0, SIZE_MAX, &shortBatches);
    check(e.maxAbs < 1000.0, "every other sample within 1 ms of truth", fmt("max |e| %.0f us", e.maxAbs));
    const Err in = errors(r, s, iOne, iOne + 7);
    check(in.maxAbs < L.periodUs * 2 + 1000.0, "inside a short delivery: within its missing periods",
          fmt("max |e| %.0f us", in.maxAbs));
    const Err before = errors(r, s, 0, iBatch);
    const Err after  = errors(r, s, iBatch, iOne);
    check(std::fabs(after.offset - before.offset) < 500.0, "no drift across the lost batch",
          fmt("before %.0f after %.0f us", before.offset, after.offset));
}

void testSwingWindow()
{
    std::printf("\n-- a 4 s swing window with a lost batch in the middle\n");
    LinkSpec L;
    L.batches = 100;                 // 4 s
    L.lostBatches = { 52 };
    const Stream s = simulateBatched(L, 37);
    const RetimeResult r = retimeArrivals(s.arrival, 200.0);
    check(r.applied && r.lostSamples == 8, "applied, the batch found", fmt("%.0f lost", r.lostSamples));
    const Err e = errors(r, s);
    // A 4 s window holds ~3 cycles of the delivery beat, so its floor is touched only a
    // few times: the period is looser than on a long record (the W0 replay: p50 1.5 ms).
    check(e.maxAbs < 2000.0, "every sample within 2 ms of truth", fmt("max |e| %.0f us", e.maxAbs));
    const size_t iLoss = indexOfDevice(s, 53 * 8);
    const Err before = errors(r, s, 0, iLoss), after = errors(r, s, iLoss);
    check(std::fabs(after.offset - before.offset) < 1500.0, "no step across the loss",
          fmt("before %.0f after %.0f us", before.offset, after.offset));
}

void testUnbatched()
{
    std::printf("\n-- no batching: one sample per delivery, a run of 10 lost\n");
    Rng rng(41);
    const double T = 10000.0 / 0.995;   // 100 Hz, 0.5 % slow
    std::vector<int64_t> arr;
    std::vector<double> truth;
    double last = 0.0;
    for (int k = 0; k < 1000; ++k) {
        if (k >= 500 && k < 510) continue;
        const double t = 5.0e5 + T * k;
        double a = t + 2000.0 + 15000.0 * rng.uniform() * rng.uniform();
        a = std::max(a, last + 1.0);
        last = a;
        arr.push_back(int64_t(std::llround(a)));
        truth.push_back(t);
    }
    const RetimeResult r = retimeArrivals(arr, 100.0);
    check(r.applied && r.batchSize == 0, "applied, no batching seen");
    check(r.lostSamples == 10 && r.lossEvents == 1 && r.losses[0].index == 500,
          "the run of 10 found by the envelope, at its gap",
          fmt("%.0f lost in %.0f events", r.lostSamples, r.lossEvents));
    double mx = 0.0, med = 0.0;
    std::vector<double> e;
    for (size_t i = 0; i < truth.size(); ++i) e.push_back(double(r.retimedUs[i]) - truth[i]);
    std::vector<double> srt = e;
    std::sort(srt.begin(), srt.end());
    med = srt[srt.size() / 2];
    for (double v : e) mx = std::max(mx, std::fabs(v - med));
    check(mx < 1000.0, "every sample within 1 ms of truth", fmt("max |e| %.0f us", mx));
}

void testClusteredNotBatched()
{
    std::printf("\n-- one sample at a time, clustered by a 15.1 ms connection interval\n");
    // ~3 per event, sometimes 4: a dominant size that is NOT a device batch. Its residue
    // walks, so it must not be read as batches (trunk_hw.py's rate self-test geometry).
    std::vector<int64_t> arr;
    std::vector<double> truth;
    std::set<int> drop;
    for (int g = 0; g < 10; ++g)
        for (int i = 0; i < 5; ++i) drop.insert(1000 + g * 1100 + i);
    const double ci = 15100.0, ph = 4321.0;
    double lastEv = -1.0;
    int inEv = 0;
    for (int k = 0; k < 12000; ++k) {
        if (drop.count(k)) continue;
        const double t = 5000.0 * k;
        const double ev = ph + std::ceil((t - ph) / ci) * ci;
        inEv = ev == lastEv ? inEv + 1 : 0;
        lastEv = ev;
        arr.push_back(int64_t(std::llround(ev + 100.0 * inEv)));
        truth.push_back(t);
    }
    const RetimeResult r = retimeArrivals(arr, 200.0);
    check(r.applied && r.batchSize == 0, "applied, not read as batches", fmt("batch %.0f", r.batchSize));
    check(r.lostSamples == 50 && r.lossEvents == 10, "ten runs of 5 found",
          fmt("%.0f lost in %.0f events", r.lostSamples, r.lossEvents));
    std::vector<double> e;
    for (size_t i = 0; i < truth.size(); ++i) e.push_back(double(r.retimedUs[i]) - truth[i]);
    std::vector<double> srt = e;
    std::sort(srt.begin(), srt.end());
    const double med = srt[srt.size() / 2];
    double mx = 0.0;
    for (double v : e) mx = std::max(mx, std::fabs(v - med));
    check(mx < 1000.0, "every sample within 1 ms of truth", fmt("max |e| %.0f us", mx));
}

void testSegmentBreak()
{
    std::printf("\n-- a 400 ms stall: two segments, each anchored\n");
    LinkSpec L;
    L.batches = 250;                 // 10 s
    L.stallFromUs = 1.0e6 + 5.0e6;
    L.stallUs = 400000.0;
    // The device's queue overflows during the stall and keeps the newest two batches: the
    // older ones are gone. (Had it kept the OLDEST, the loss would sit inside one delivery
    // with no gap and no residue change — invisible to any arrival-time method.)
    const Stream s = simulateBatched(L, 53);
    const RetimeResult r = retimeArrivals(s.arrival, 200.0);
    check(r.applied && r.segments.size() == 2, "two segments", fmt("%.0f", double(r.segments.size())));
    int estimated = 0;
    for (const RetimeLoss &l : r.losses)
        if (l.kind == RetimeLossKind::SegmentBreak) estimated += l.samples;
    const int truthLost = s.droppedInStall;
    check(std::abs(estimated - truthLost) <= 8, "lost count across the break estimated to a batch",
          fmt("%.0f vs %.0f", estimated, truthLost));
    const size_t cut = r.segments.size() == 2 ? r.segments[1].begin : 0;
    const Err a = errors(r, s, 0, cut), b = errors(r, s, cut);
    check(a.maxAbs < 2000.0 && b.maxAbs < 2000.0, "each side dated to its own clock (2 ms: ~5 s each)",
          fmt("max |e| %.0f / %.0f us", a.maxAbs, b.maxAbs));
}

void testRefusals()
{
    std::printf("\n-- what is not one sample clock is left alone\n");
    const RetimeResult one = retimeArrivals({ 1000 }, 200.0);
    check(!one.applied && one.retimedUs.size() == 1 && one.retimedUs[0] == 1000, "one sample: arrivals kept");

    // A lane stitched from two rates (a deferred pull): no single clock fits.
    std::vector<int64_t> stitched;
    for (int i = 0; i < 400; ++i) stitched.push_back(int64_t(i) * 5000);
    for (int i = 0; i < 400; ++i) stitched.push_back(2'000'000 + int64_t(i) * 1250);
    for (int i = 0; i < 400; ++i) stitched.push_back(2'500'000 + int64_t(i) * 5000);
    const RetimeResult st = retimeArrivals(stitched, 200.0);
    check(!st.applied && st.retimedUs == stitched, "a stitched lane: not applied, arrivals kept",
          std::string(st.why) + fmt(" (p95 %.0f ms)", st.residualP95Us / 1e3));

    // Too short to fit: the configured period, anchored on the floor.
    std::vector<int64_t> few;
    for (int i = 0; i < 10; ++i) few.push_back(100000 + int64_t(i) * 5000 + (i % 3) * 700);
    const RetimeResult f = retimeArrivals(few, 200.0);
    check(f.applied && f.segments.size() == 1 && f.segments[0].source == RetimePeriodSource::Nominal
              && std::fabs(f.periodUs - 5000.0) < 1e-9,
          "ten samples: nominal period, still on the floor");
    bool strictly = true;
    for (size_t i = 1; i < f.retimedUs.size(); ++i) strictly &= f.retimedUs[i] > f.retimedUs[i - 1];
    check(strictly, "retimed series strictly increasing");
}

// ── Replay of real recordings (by hand; see the header) ──────────────────────

bool readRecord(const std::filesystem::path &p, std::vector<int64_t> &t, double &hz)
{
    std::ifstream in(p);
    std::string line;
    if (!std::getline(in, line) || line.rfind("# pinpoint.imu_record/1", 0) != 0) return false;
    hz = 0.0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            const auto at = line.find("configuredRateHz=");
            if (at != std::string::npos) hz = std::atof(line.c_str() + at + 17);
            continue;
        }
        if (line.rfind("t_us", 0) == 0) continue;
        t.push_back(std::atoll(line.c_str()));
    }
    return t.size() > 1;
}

void replay(const char *dir)
{
    std::vector<std::filesystem::path> files;
    for (const auto &e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".csv") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto &p : files) {
        std::vector<int64_t> t;
        double hz = 0.0;
        if (!readRecord(p, t, hz)) continue;
        const RetimeResult r = retimeArrivals(t, hz);
        std::printf("\n%s\n  %zu samples over %.2f s; period %.2f us (%.3f Hz) vs configured %g; "
                    "%zu segment(s), batches of %d; applied %s\n"
                    "  arrival - retimed p50 %.2f / p95 %.2f / max %.2f ms; loss %d event(s), %d sample(s)\n",
                    p.filename().c_str(), t.size(), double(t.back() - t.front()) / 1e6, r.periodUs,
                    r.periodHz(), hz, r.segments.size(), r.batchSize, r.applied ? "yes" : r.why,
                    r.residualP50Us / 1e3, r.residualP95Us / 1e3, r.residualMaxUs / 1e3,
                    r.lossEvents, r.lostSamples);
        for (double W : { 3.0e6, 5.0e6 }) {
            int windows = 0, lost = 0;
            double tMin = 1e300, tMax = 0.0, offMin = 1e300, offMax = -1e300, spreadMax = 0.0;
            std::vector<double> spreads;
            size_t i = 0;
            while (true) {
                size_t j = i;
                while (j < t.size() && double(t[j] - t[i]) < W) ++j;
                if (j >= t.size()) break;
                const RetimeResult w = retimeArrivals(std::vector<int64_t>(t.begin() + long(i), t.begin() + long(j)), hz);
                ++windows;
                lost += w.lostSamples;
                tMin = std::min(tMin, w.periodUs); tMax = std::max(tMax, w.periodUs);
                std::vector<double> d;
                for (size_t q = i; q < j; ++q) d.push_back(double(w.retimedUs[q - i] - r.retimedUs[q]));
                std::sort(d.begin(), d.end());
                offMin = std::min(offMin, d[d.size() / 2]); offMax = std::max(offMax, d[d.size() / 2]);
                spreads.push_back(d.back() - d.front());
                spreadMax = std::max(spreadMax, d.back() - d.front());
                i = j;
            }
            if (!windows) continue;
            std::sort(spreads.begin(), spreads.end());
            std::printf("  %.0f s windows (%d): period %.1f..%.1f us; offset vs whole-file fit %+.2f..%+.2f ms; "
                        "spread within a window p50 %.2f / max %.2f ms; loss %d\n",
                        W / 1e6, windows, tMin, tMax, offMin / 1e3, offMax / 1e3,
                        spreads[spreads.size() / 2] / 1e3, spreadMax / 1e3, lost);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    std::printf("=== imu_retime: Witmotion lanes dated by their own clock ===\n");
    testLosslessControl();
    testLosses();
    testSwingWindow();
    testUnbatched();
    testClusteredNotBatched();
    testSegmentBreak();
    testRefusals();
    for (int a = 1; a < argc; ++a) replay(argv[a]);
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
