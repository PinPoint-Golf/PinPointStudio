// Capture data-integrity check (capture_integrity_check.h).
//
// The shape it exists for is 2026-08-18 swing_0003: 150 fps face-on lane,
// frames stop 33 ms after impact, three lone frames arrive during a ~0.8 s host
// stall, then ~50 backlog frames stamped ~1 ms apart, then normal pacing. Cases:
//
//   A. A jittery but complete lane (intervals 0.7–1.6 periods) is clean.
//   B. The s3 shape: holes after impact only — warns, postImpact, not preImpact,
//      framesLost ≈ the frames the 594 ms hole swallowed, the burst is not a hole.
//   C. The same hole BEFORE impact — preImpact.
//   D. No impact anchor — every hole counts as pre-impact (conservative).
//   E. A short lane (2 frames) is not checkable: camerasChecked 0, warns() false.
//   F. holePeriods <= 0 disables the check (no claim).
//   G. Merging: one clean lane + one holed lane warns; firstHoleUs is the earliest.
//
// Coverage against the capture window (issue #53 — 2026-10-20 Jess Wrist_03 swing_0010, whose
// video began ~264 ms after impact on both cameras and passed as clean):
//
//   H. The s10 shape: a hole-free lane that starts after impact FAILS (ok false: withheld) —
//      startsAfterImpact, lateStart, 0 frames before impact, the shortfall recorded per camera.
//   I. A lane that ends 1 s early is a WARNING: earlyEnd, ok stays true (the swing is on it).
//   J. CONTROL: a full lane (first frame 3 ms in, last 18 ms short — the library's usual) passes.
//   K. CONTROL: a small shortfall inside the tolerance (40 ms / 45 ms) passes clean; 60 ms at the
//      start is flagged lateStart, still ok (2026-10-24 Paddy s10 is 0.6 s late, swing on the clip).
//   L. No window known: a lane with no frame before impact still fails; the edges are not judged.
//   M. Impact unknown: no impact rule, the edges still are.
//   N. Merge: full + short lane fails, the worst lane's amounts, both cameras listed.
//   O. checkCaptureIntegrity over a SwingWindow: the impact camera's band is exempt from coverage.
//   P. A slow camera is never held tighter than two of its own frame periods.
//   Q. A phone clip (timed from its own first frame: a 3 s lane from 0) beside a full camera lane
//      passes when exempt; judged, it would be flagged short.
//   R. Coverage needs only one frame: a 2-frame lane, both after impact, fails; holes are not judged.

#include "../capture_integrity_check.h"
#include "swing_payload_source.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

using pinpoint::CaptureCoverageSpec;
using pinpoint::CaptureIntegrityVerdict;
using pinpoint::captureIntegrityOf;
using pinpoint::checkCaptureIntegrity;
using pinpoint::mergeCaptureIntegrity;

static int g_fail = 0;
static void check(const char *label, bool ok, const char *detail = "")
{
    std::printf("  [%s] %-52s %s\n", ok ? "PASS" : "FAIL", label, detail);
    if (!ok) ++g_fail;
}

static constexpr int64_t kPeriod = 6667;   // 150 fps

// n frames at kPeriod with deterministic jitter in [0.7, 1.6] periods (the real rig).
static std::vector<int64_t> jitteryLane(int n, int64_t t0 = 0)
{
    std::vector<int64_t> t;
    int64_t cur = t0;
    for (int i = 0; i < n; ++i) {
        t.push_back(cur);
        const double f = 0.7 + 0.9 * (0.5 + 0.5 * std::sin(0.37 * i));   // 0.7..1.6
        cur += int64_t(kPeriod * f);
    }
    return t;
}

// n frames at exactly kPeriod from t0.
static std::vector<int64_t> evenLane(int n, int64_t t0)
{
    std::vector<int64_t> t;
    for (int i = 0; i < n; ++i) t.push_back(t0 + int64_t(i) * kPeriod);
    return t;
}

// The 4 s capture window the shot processor freezes, in the document's window-relative domain.
static CaptureCoverageSpec window4s()
{
    CaptureCoverageSpec c;
    c.judge = true;
    c.windowStartUs = 0;
    c.windowEndUs   = 4000000;
    return c;
}

// A camera-only payload source: formats, no pixels (the check reads timestamps only).
class FormatsOnlySource : public pinpoint::SwingPayloadSource {
public:
    void add(pinpoint::SourceId id, const char *serial)
    {
        pinpoint::FormatDescriptor fd;
        fd.device_serial = serial;
        fd.format = pinpoint::CameraFormat{};
        formats_[id] = fd;
    }
    pinpoint::SourceRing::ReadHandle payloadOf(pinpoint::SourceId, uint64_t) const noexcept override { return {}; }
    const pinpoint::FormatDescriptor &formatOf(pinpoint::SourceId id) const noexcept override
    {
        static const pinpoint::FormatDescriptor none{};
        const auto it = formats_.find(id);
        return it == formats_.end() ? none : it->second;
    }
    bool validate(pinpoint::SourceId, const pinpoint::SourceRing::ReadHandle &) const noexcept override { return true; }
private:
    std::map<pinpoint::SourceId, pinpoint::FormatDescriptor> formats_;
};

int main()
{
    std::printf("=== capture data-integrity check ===\n\n");
    char buf[200];

    std::printf("-- A. complete jittery lane --\n");
    {
        const std::vector<int64_t> t = jitteryLane(600);
        const CaptureIntegrityVerdict v = captureIntegrityOf(t, 2400000);
        std::snprintf(buf, sizeof buf, "checked %d holes %d", v.camerasChecked, v.holes);
        check("lane checked",  v.camerasChecked == 1, buf);
        check("no holes",      v.ok && v.holes == 0 && !v.warns(), buf);
        check("no first hole", v.firstHoleUs < 0.0, buf);
    }

    std::printf("\n-- B. the s3 shape: stall after impact --\n");
    std::vector<int64_t> s3;
    int64_t impact = 0;
    {
        // 240 normal frames, impact at frame 235, then 594 ms hole, 3 lone frames
        // 69/138 ms apart, 50 burst frames 1 ms apart, then normal pacing.
        int64_t cur = 0;
        for (int i = 0; i < 240; ++i) { s3.push_back(cur); cur += kPeriod; }
        impact = s3[235];
        const int64_t lastGood = s3.back();
        cur = lastGood + 594400; s3.push_back(cur);
        cur += 69100;  s3.push_back(cur);
        cur += 137900; s3.push_back(cur);
        for (int i = 0; i < 50; ++i) { cur += 1000; s3.push_back(cur); }
        for (int i = 0; i < 200; ++i) { cur += kPeriod; s3.push_back(cur); }
        const CaptureIntegrityVerdict v = captureIntegrityOf(s3, impact);
        std::snprintf(buf, sizeof buf, "holes %d lost %d worst %.0f ms first %.0f us",
                      v.holes, v.framesLost, v.worstHoleMs, v.firstHoleUs);
        check("warns",                     v.warns(), buf);
        check("three holes",               v.holes == 3, buf);
        check("post-impact only",          v.postImpact && !v.preImpact, buf);
        check("worst hole is the stall",   std::fabs(v.worstHoleMs - 594.4) < 0.5, buf);
        check("frames lost ~ 88+9+20",     v.framesLost >= 110 && v.framesLost <= 120, buf);
        check("first hole opens at the last good frame", int64_t(v.firstHoleUs) == lastGood, buf);
    }

    std::printf("\n-- C. same hole before impact --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf(s3, s3.back());
        check("pre-impact", v.warns() && v.preImpact && !v.postImpact);
    }

    std::printf("\n-- D. no impact anchor --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf(s3, -1);
        check("holes counted as pre-impact (conservative)", v.warns() && v.preImpact);
    }

    std::printf("\n-- E. short lane not checkable --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf({0, 6667}, -1);
        check("camerasChecked 0, no warning", v.camerasChecked == 0 && !v.warns() && v.ok);
    }

    std::printf("\n-- F. holePeriods <= 0 disables --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf(s3, impact, 0.0);
        check("no claim", v.camerasChecked == 0 && !v.warns());
    }

    std::printf("\n-- G. merge clean + holed --\n");
    {
        const CaptureIntegrityVerdict clean = captureIntegrityOf(jitteryLane(600, 5000), impact);
        const CaptureIntegrityVerdict holed = captureIntegrityOf(s3, impact);
        const CaptureIntegrityVerdict m = mergeCaptureIntegrity(clean, holed);
        std::snprintf(buf, sizeof buf, "checked %d holes %d first %.0f", m.camerasChecked, m.holes, m.firstHoleUs);
        check("two lanes checked", m.camerasChecked == 2, buf);
        check("warns with the holed lane's counts", m.warns() && m.holes == 3 && m.postImpact, buf);
        check("firstHoleUs from the holed lane", int64_t(m.firstHoleUs) == int64_t(holed.firstHoleUs), buf);
        const CaptureIntegrityVerdict m2 = mergeCaptureIntegrity(holed, clean);
        check("merge is order-independent", m2.holes == m.holes && m2.firstHoleUs == m.firstHoleUs
                                             && m2.ok == m.ok && m2.camerasChecked == m.camerasChecked);
    }

    // ── coverage (issue #53) ─────────────────────────────────────────────────────────────────

    // The s10 numbers: impact 2 170 148 µs, the DTL's 232 frames from 2 434 079 µs at 149.3 fps.
    const int64_t s10Impact = 2170148;
    const std::vector<int64_t> s10 = evenLane(232, 2434079);

    std::printf("\n-- H. the s10 shape: video starts after impact --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf(s10, s10Impact, 3.0, window4s(), "18277032");
        std::snprintf(buf, sizeof buf, "ok %d holes %d before %d start %.1f end %.1f", v.ok, v.holes,
                      v.framesBeforeImpact, v.missingStartMs, v.missingEndMs);
        check("no holes, and still FAILS", v.holes == 0 && !v.ok && v.warns(), buf);
        check("no frames before impact", v.startsAfterImpact && v.framesBeforeImpact == 0, buf);
        check("starts late by ~2434 ms", v.lateStart && std::fabs(v.missingStartMs - 2434.079) < 0.01, buf);
        check("the end is fine", !v.earlyEnd, buf);
        check("one camera recorded, with its serial", v.cameras.size() == 1 && v.cameras[0].serial == "18277032");
        check("per camera: first/last frame vs window",
              v.cameras.size() == 1 && std::fabs(v.cameras[0].firstFrameMs - 2434.079) < 0.01
                  && std::fabs(v.cameras[0].lastFrameMs - (2434.079 + 231 * 6.667)) < 0.01
                  && v.cameras[0].framesBeforeImpact == 0, buf);
        check("the window's length", std::fabs(v.windowMs - 4000.0) < 1e-9);
    }

    std::printf("\n-- I. a clip that ends 1 s early --\n");
    {
        // 3 ms in, runs to ~3.0 s of the 4 s window; impact at 2.5 s is on it.
        std::vector<int64_t> t = evenLane(450, 3000);
        const CaptureIntegrityVerdict v = captureIntegrityOf(t, 2500000, 3.0, window4s());
        std::snprintf(buf, sizeof buf, "ok %d end %.1f before %d", v.ok, v.missingEndMs, v.framesBeforeImpact);
        check("a warning only: earlyEnd, and still ok", v.ok && !v.warns() && v.edgesShort() && v.earlyEnd
                                                          && !v.lateStart && !v.startsAfterImpact, buf);
        check("records ~1000 ms missing at the end", std::fabs(v.missingEndMs - (4000.0 - 3.0 - 449 * 6.667)) < 0.01, buf);
        check("frames before impact counted", v.framesBeforeImpact == int((2500000 - 3000) / kPeriod) + 1, buf);
    }

    std::printf("\n-- J. CONTROL: a full clip --\n");
    {
        // The library's usual: first frame 3 ms in, last 18 ms before the end (swing_0009 shape).
        std::vector<int64_t> t = jitteryLane(600, 3000);
        while (t.back() > 3982000) t.pop_back();
        const CaptureIntegrityVerdict v = captureIntegrityOf(t, 2505224, 3.0, window4s());
        std::snprintf(buf, sizeof buf, "ok %d start %.1f end %.1f before %d", v.ok, v.missingStartMs,
                      v.missingEndMs, v.framesBeforeImpact);
        check("passes", v.ok && !v.warns() && !v.clipped(), buf);
        check("still records its coverage", v.coverageJudged && v.cameras.size() == 1
                                               && v.missingStartMs == 3.0 && v.framesBeforeImpact > 300, buf);
    }

    std::printf("\n-- K. CONTROL: a small shortfall is tolerated --\n");
    {
        std::vector<int64_t> t = evenLane(590, 40000);              // 40 ms in
        while (t.back() > 3955000) t.pop_back();                     // ≥45 ms short at the end
        const CaptureIntegrityVerdict v = captureIntegrityOf(t, 2500000, 3.0, window4s());
        std::snprintf(buf, sizeof buf, "ok %d start %.1f end %.1f", v.ok, v.missingStartMs, v.missingEndMs);
        check("40 ms / 45 ms inside the 50 ms tolerance passes, nothing flagged", v.ok && !v.edgesShort() && v.missingEndMs < 50.0, buf);
        const CaptureIntegrityVerdict late = captureIntegrityOf(evenLane(590, 60000), 2500000, 3.0, window4s());
        check("60 ms at the start is flagged lateStart, still ok (a warning)", late.ok && late.lateStart && late.edgesShort());
        check("the tolerance it used is recorded", v.edgeToleranceMs == pinpoint::kCaptureEdgeToleranceMs);
    }

    std::printf("\n-- L. no window known: impact still judged --\n");
    {
        CaptureCoverageSpec c;
        c.judge = true;                                   // no window block in the document
        const CaptureIntegrityVerdict v = captureIntegrityOf(s10, s10Impact, 3.0, c);
        check("fails: no frame before impact", !v.ok && v.startsAfterImpact, "");
        check("edges not judged", !v.lateStart && !v.earlyEnd && std::isnan(v.missingStartMs)
                                      && std::isnan(v.windowMs));
        const CaptureIntegrityVerdict ok = captureIntegrityOf(jitteryLane(600), 2400000, 3.0, c);
        check("a lane over impact passes", ok.ok);
    }

    std::printf("\n-- M. impact unknown: edges only --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf(s10, -1, 3.0, window4s());
        check("no impact rule", !v.startsAfterImpact && v.framesBeforeImpact == -1);
        check("still flagged late at the start (a warning: ok)", v.ok && v.lateStart);
    }

    std::printf("\n-- N. merge full + short --\n");
    {
        std::vector<int64_t> full = evenLane(600, 3000);
        while (full.back() > 3982000) full.pop_back();
        const CaptureIntegrityVerdict a = captureIntegrityOf(full, s10Impact, 3.0, window4s());
        const CaptureIntegrityVerdict b = captureIntegrityOf(s10, s10Impact, 3.0, window4s());
        const CaptureIntegrityVerdict m = mergeCaptureIntegrity(mergeCaptureIntegrity(CaptureIntegrityVerdict{}, a), b);
        std::snprintf(buf, sizeof buf, "ok %d start %.1f before %d cams %zu", m.ok, m.missingStartMs,
                      m.framesBeforeImpact, m.cameras.size());
        check("fails with the short lane", !m.ok && m.startsAfterImpact && m.lateStart, buf);
        check("the worst lane's amounts", std::fabs(m.missingStartMs - 2434.079) < 0.01 && m.framesBeforeImpact == 0, buf);
        check("both cameras listed", m.cameras.size() == 2 && m.camerasChecked == 2, buf);
    }

    std::printf("\n-- O. over a window: the impact camera is exempt --\n");
    {
        // Face-on full, impact camera a 591 fps band of 200 ms before / 100 ms after impact.
        const pinpoint::SourceId faceOn = 0, impactCam = 1;
        auto src = std::make_unique<FormatsOnlySource>();
        src->add(faceOn, "17453937");
        src->add(impactCam, "IMPACT");
        std::vector<pinpoint::IndexEntry> e;
        std::vector<int64_t> fo = evenLane(600, 3000);
        while (fo.back() > 3982000) fo.pop_back();
        for (size_t i = 0; i < fo.size(); ++i) e.push_back({ fo[i], faceOn, uint64_t(i) });
        uint64_t seq = 0;
        for (int64_t t = 2300000; t <= 2600000; t += 1692) e.push_back({ t, impactCam, seq++ });
        std::stable_sort(e.begin(), e.end(), [](const pinpoint::IndexEntry &x, const pinpoint::IndexEntry &y) {
            return x.timestamp_us < y.timestamp_us; });
        const pinpoint::SwingWindow w(std::move(src), std::move(e), 0, 4000000);
        const CaptureIntegrityVerdict exempt = checkCaptureIntegrity(w, 2500000, window4s(), { impactCam });
        std::snprintf(buf, sizeof buf, "ok %d checked %d cams %zu", exempt.ok, exempt.camerasChecked,
                      exempt.cameras.size());
        check("passes with the band exempt", exempt.ok && exempt.camerasChecked == 2 && exempt.cameras.size() == 1, buf);
        check("the judged camera carries its serial", exempt.cameras.size() == 1 && exempt.cameras[0].serial == "17453937");
        const CaptureIntegrityVerdict judged = checkCaptureIntegrity(w, 2500000, window4s());
        check("…and would be flagged short were it judged", judged.lateStart && judged.earlyEnd);
        const CaptureIntegrityVerdict legacy = checkCaptureIntegrity(w, 2500000);
        check("no spec = the holes-only check", legacy.ok && !legacy.coverageJudged && legacy.cameras.empty());
    }

    std::printf("\n-- P. a slow camera: two of its periods --\n");
    {
        // 30 fps, first frame 60 ms in: past 50 ms but inside 2 × 33.3 ms.
        std::vector<int64_t> t;
        for (int64_t x = 60000; x <= 3970000; x += 33333) t.push_back(x);
        const CaptureIntegrityVerdict v = captureIntegrityOf(t, 2500000, 3.0, window4s());
        std::snprintf(buf, sizeof buf, "ok %d start %.1f end %.1f", v.ok, v.missingStartMs, v.missingEndMs);
        check("passes", v.ok, buf);
    }

    std::printf("\n-- Q. a phone clip on its own clock --\n");
    {
        // ppcp_clip_filer.cpp times a phone clip from its own first frame: the default 3 s clip
        // (2 s before, 1 s after) at 60 fps reads 0 … 3 s whatever the window says.
        const pinpoint::SourceId faceOn = 0, phone = 1;
        auto src = std::make_unique<FormatsOnlySource>();
        src->add(faceOn, "17453937");
        src->add(phone, "iPhone");
        std::vector<pinpoint::IndexEntry> e;
        std::vector<int64_t> fo = evenLane(600, 3000);
        while (fo.back() > 3982000) fo.pop_back();
        for (size_t i = 0; i < fo.size(); ++i) e.push_back({ fo[i], faceOn, uint64_t(i) });
        uint64_t seq = 0;
        for (int64_t t = 0; t <= 3000000; t += 16667) e.push_back({ t, phone, seq++ });
        std::stable_sort(e.begin(), e.end(), [](const pinpoint::IndexEntry &x, const pinpoint::IndexEntry &y) {
            return x.timestamp_us < y.timestamp_us; });
        const pinpoint::SwingWindow w(std::move(src), std::move(e), 0, 4000000);
        const CaptureIntegrityVerdict v = checkCaptureIntegrity(w, 2500000, window4s(), { phone });
        std::snprintf(buf, sizeof buf, "ok %d checked %d cams %zu end %.1f", v.ok, v.camerasChecked,
                      v.cameras.size(), v.missingEndMs);
        check("full camera + exempt phone clip passes, nothing flagged", v.ok && !v.clipped() && !v.edgesShort()
                                                                          && v.camerasChecked == 2, buf);
        check("only the camera's coverage is recorded", v.cameras.size() == 1 && v.cameras[0].serial == "17453937", buf);
        const CaptureIntegrityVerdict judged = checkCaptureIntegrity(w, 2500000, window4s());
        check("…held to the window it would read ~1 s short at the end", judged.earlyEnd && judged.missingEndMs > 900.0);
    }

    std::printf("\n-- R. a 2-frame lane is judged for coverage --\n");
    {
        const CaptureIntegrityVerdict v = captureIntegrityOf({ 2434079, 2440778 }, s10Impact, 3.0, window4s());
        std::snprintf(buf, sizeof buf, "ok %d checked %d holes %d before %d", v.ok, v.camerasChecked, v.holes,
                      v.framesBeforeImpact);
        check("2 frames, both after impact: checked and FAILS", v.camerasChecked == 1 && !v.ok && v.warns()
                                                               && v.startsAfterImpact && v.framesBeforeImpact == 0, buf);
        check("…with no hole verdict (too few frames)", v.holes == 0, buf);
        const CaptureIntegrityVerdict one = captureIntegrityOf({ 3000000 }, s10Impact, 3.0, window4s());
        check("1 frame: the 50 ms tolerance stands in for the unknown period", !one.ok && one.lateStart && one.earlyEnd);
        const CaptureIntegrityVerdict notAsked = captureIntegrityOf({ 2434079, 2440778 }, s10Impact);
        check("without a coverage spec a 2-frame lane is still not checkable", notAsked.camerasChecked == 0 && notAsked.ok);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
