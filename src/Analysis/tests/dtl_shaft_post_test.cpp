// Standalone tests for the DTL deciding half, part 2 (src/Analysis/
// dtl_shaft_post) — the post-solve snap and the tier ladder.
//
// Three things here are the design rather than arithmetic:
//
//  · THE SNAP RUNS ON THE CONTRAST IMAGE. The shaft alternates black and white,
//    so a SIGNED ridge integral cancels along it: P1 plants exactly that shaft,
//    puts the anchor 30 px off its axis, and asks the snap to find it. P1b is the
//    control — the same search on the RAW frame does not.
//
//  · A BAND FRAME IS LEFT ALONE. The lock is a direct measurement of the line and
//    the snap's ridge search can only move it (face-on measured that, 0.26° →
//    0.61° p50). P2.
//
//  · A STUB IS NOT A SHAFT. The visibility law says how long the club should look
//    on this frame; a 40 px run where 104 px was due publishes NOTHING, with a
//    reason. P3. The point is not the threshold, it is that the absence is
//    reported as an absence rather than as a short measurement.
//
//   cmake --build build/tests --target dtl_shaft_post_test
//   ctest --test-dir build/tests -R dtl_shaft_post --output-on-failure

#include "../dtl_shaft_post.h"
#include "../shaft_track_shared.h"   // snapSearch, for the raw-channel control

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static constexpr double kPi = 3.14159265358979323846;
static constexpr int W = 320, H = 320;
static constexpr double GX = 160.0, GY = 160.0;     // the TRUE line's origin
static constexpr double kClub = 50.0;               // the TRUE direction (deg)
static constexpr double kLen  = 120.0;              // its drawn length (px)
static const double kNan = std::numeric_limits<double>::quiet_NaN();

static cv::Mat baseScene()
{
    cv::Mat m(H, W, CV_8UC1);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c)
            m.at<uchar>(r, c) = uchar(128 + ((r * 7 + c * 13) % 11) - 5);
    return m;
}

static cv::Point ptAt(double gx, double gy, double thDeg, double r)
{
    const double t = thDeg * kPi / 180.0;
    return cv::Point(int(std::lround(gx + r * std::cos(t))), int(std::lround(gy + r * std::sin(t))));
}

// Alternating black and white 10 px runs, 5 px wide — the taped shaft.
static void drawStripedShaft(cv::Mat &m, double thDeg, double r0, double r1)
{
    for (double r = r0; r < r1; r += 10.0) {
        const int v = (int(std::lround((r - r0) / 10.0)) % 2) ? 255 : 0;
        cv::line(m, ptAt(GX, GY, thDeg, r), ptAt(GX, GY, thDeg, std::min(r + 10.0, r1)),
                 cv::Scalar(v), 5, cv::LINE_8);
    }
}

static DtlShaftConfig testCfg()
{
    DtlShaftConfig c;
    c.evAbsFloor = 0.0;
    c.evAbsFloorDif = -1.0;
    c.ridge.rHi = 200.0f;     // the scene is 320 px; 470 would be all off-frame samples
    return c;
}

// A solve state with `n` frames all solved at `thetaDeg`, one band over them,
// evidence strong at the solved bin and weak on its reverse. Everything the post
// pass reads and nothing it does not — the point of dtl_shaft_post being pure
// over plain vectors is that a test can hand it exactly the tables the DP would.
static DtlSolveState makeState(int n, double thetaDeg, double rend, double rho,
                               const DtlShaftConfig &cfg)
{
    DtlSolveState st;
    st.nf = n;
    st.NS = int(std::lround(360.0 / cfg.grid));
    const int NS = st.NS;
    int bi = int(std::lround(thetaDeg / cfg.grid)) % NS;
    if (bi < 0) bi += NS;
    st.EV.assign(size_t(n), std::vector<float>(size_t(NS), 0.10f));
    st.SUP.assign(size_t(n), std::vector<float>(size_t(NS), 0.10f));
    st.REND.assign(size_t(n), std::vector<float>(size_t(NS), 0.f));
    st.ARMVETO.assign(size_t(n), std::vector<char>(size_t(NS), 0));
    st.ARMJOINT.assign(size_t(n), std::vector<signed char>(size_t(NS), -1));
    for (int i = 0; i < n; ++i) {
        st.EV[size_t(i)][size_t(bi)]   = 0.90f;
        st.SUP[size_t(i)][size_t(bi)]  = 0.80f;
        st.REND[size_t(i)][size_t(bi)] = float(rend);
    }
    st.band.assign(size_t(n), BandMatch{});
    st.bandOk.assign(size_t(n), 0);
    st.rhoPred.assign(size_t(n), rho);
    st.rhoSrc.assign(size_t(n), DtlRhoSrc::Measured);
    st.ballGate.assign(size_t(n), 0);
    st.thetaBallDeg.assign(size_t(n), kNan);
    st.thetaDeg.assign(size_t(n), thetaDeg);
    st.solved.assign(size_t(n), 1);
    st.gridDeg.assign(size_t(NS), 0.f);
    for (int k = 0; k < NS; ++k) st.gridDeg[size_t(k)] = float(k * cfg.grid);
    st.quarantined.assign(size_t(n), 0);
    st.sighted.assign(size_t(n), 1);
    st.inSpan.assign(size_t(n), 1);
    st.spanFrames = n;
    st.rowResid.assign(size_t(n), kNan);
    st.corrCentreADeg.assign(size_t(n), kNan);
    st.corrCentreBDeg.assign(size_t(n), kNan);
    st.corrHalfDeg.assign(size_t(n), kNan);
    st.corridorOn.assign(size_t(n), 0);
    st.corridorEscape.assign(size_t(n), 0);
    st.corrSignTaken.assign(size_t(n), 0);
    st.reason.assign(size_t(n), QString());
    DtlBand b; b.lo = 0; b.hi = n - 1; b.loUs = 0; b.hiUs = 0; b.name = QStringLiteral("test");
    st.bands.push_back(b);
    return st;
}

// ⊥ distance from a point to the TRUE line through (GX,GY) at kClub.
static double distToTrueAxis(double x, double y)
{
    const double u = kClub * kPi / 180.0;
    return std::abs((x - GX) * std::sin(u) - (y - GY) * std::cos(u));
}

int main()
{
    const DtlShaftConfig cfg = testCfg();

    // ── P0 an unsolved state is an invalid track ────────────────────────────
    std::printf("=== P0: dtlPostSolve — an unsolved state is an invalid track ===\n");
    {
        const SegmentGeom geom;
        const std::vector<int64_t> tUs = { 0, 6640, 13280 };
        const DtlAnchors anchors;
        DtlSolveState st;
        st.nf = int(tUs.size());
        const FrameSource frameAt = [](int) { return cv::Mat(); };
        const DtlShaftTrack2D tr = dtlPostSolve(frameAt, tUs, anchors, nullptr, st,
                                                512, 1024, geom, cfg, nullptr);
        check(!tr.valid && tr.samples.empty(),
              "P0: no solve ⇒ invalid, no samples");
        check(tr.frameWidth == 512 && tr.frameHeight == 1024,
              "P0: the frame geometry it was asked about rides through");
        check(tr.publishedInEndOn == 0,
              "P0: nothing published inside an end-on gap");
    }

    // ── P1 the snap re-registers a 30 px anchor error ───────────────────────
    std::printf("\n=== P1: snap on the CONTRAST image finds the striped shaft from 30 px off-axis ===\n");
    {
        const int n = 4;
        const double u = kClub * kPi / 180.0;
        const double nx = -std::sin(u), ny = std::cos(u);
        const double ax = GX + 30.0 * nx, ay = GY + 30.0 * ny;   // the anchor, 30 px off the axis

        std::vector<cv::Mat> frames;
        frames.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15.0, kLen);
            frames[size_t(i)] = m;
        }
        const FrameSource frameAt = [&frames](int i) -> cv::Mat {
            return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
        };
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), ax);
        an.gy.assign(size_t(n), ay);
        an.quarantined.assign(size_t(n), 0);

        DtlSolveState st = makeState(n, kClub, kLen, 0.93, cfg);
        DtlDecideTrace tr;
        const SegmentGeom geom;
        const DtlShaftTrack2D out = dtlPostSolve(frameAt, tUs, an, nullptr, st,
                                                 W, H, geom, cfg, &tr);
        const DtlSample &s = out.samples[0];
        const double dTheta = std::abs(shaftshared::circWrap(s.thetaRad * 180.0 / kPi - kClub));
        const double dPerp  = distToTrueAxis(s.gripPx.x(), s.gripPx.y());
        std::printf("       snap: offset %.1f px, Δθ %.2f° | published grip (%.1f, %.1f) is %.2f px "
                    "off the true axis, θ error %.2f°\n",
                    tr.snapOffsetPx[0], tr.snapDThetaDeg[0], s.gripPx.x(), s.gripPx.y(), dPerp, dTheta);
        check(tr.snapAccepted[0] != 0, "P1: the snap is accepted");
        check(dTheta <= 1.5, "P1: the published direction lands within 1.5° of the true axis");
        check(dPerp <= 3.0, "P1: and the published grip within 3 px of it");
        check(distToTrueAxis(ax, ay) > 29.0, "P1 control: the anchor it started from really was 30 px off");

        // The raw channel, FOR THE RECORD rather than as a gate. On this synthetic
        // a signed integral does not cancel along the shaft, because the per-sample
        // evidence clip is asymmetric (eClipPos 90 against eClipNeg 30): the white
        // runs are credited three times as much as the black runs are debited, so
        // the raw search finds the line too. The cancellation that matters is a
        // COMPARISON — a bright limb out-scoring the shaft — and the scene for that
        // is the decide half's T1, not a lone shaft on an empty background. Stated
        // here rather than asserted away, because an assertion this scene cannot
        // support would be a false witness for the channel it is meant to justify.
        cv::Mat g32; frames[0].convertTo(g32, CV_32F);
        const shaftshared::SnapResult raw =
            shaftshared::snapSearch(g32, ax, ay, u, kLen, cfg.snap, cfg.ridge);
        const double rawPerp = distToTrueAxis(ax + raw.offsetPx * nx, ay + raw.offsetPx * ny);
        std::printf("       raw-channel, for the record: offset %.1f px, Δθ %.2f° ⇒ %.1f px off the axis\n",
                    raw.offsetPx, raw.dThetaDeg, rawPerp);
        check(tr.snapOffsetPx[0] < -20.0,
              "P1: the accepted snap really moved the line, it did not merely decline to");
    }

    // ── P2 a BAND frame is left alone ───────────────────────────────────────
    std::printf("\n=== P2: the snap never re-registers a BAND frame ===\n");
    {
        const int n = 4;
        const double u = kClub * kPi / 180.0;
        const double ax = GX + 30.0 * -std::sin(u), ay = GY + 30.0 * std::cos(u);
        std::vector<cv::Mat> frames;
        frames.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15.0, kLen);
            frames[size_t(i)] = m;
        }
        const FrameSource frameAt = [&frames](int i) -> cv::Mat {
            return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
        };
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), ax);
        an.gy.assign(size_t(n), ay);

        DtlSolveState st = makeState(n, kClub, kLen, 0.93, cfg);
        for (int i = 0; i < n; ++i) {
            BandMatch bm;
            bm.ok = true; bm.n = 5; bm.s = 0.39f; bm.r0 = 120.f; bm.thetaDeg = float(kClub);
            st.band[size_t(i)] = bm;
            st.bandOk[size_t(i)] = 1;
        }
        DtlDecideTrace tr;
        const SegmentGeom geom;
        const DtlShaftTrack2D out = dtlPostSolve(frameAt, tUs, an, nullptr, st, W, H, geom, cfg, &tr);
        check(out.samples[0].tier == DtlTier::Band, "P2: the frame tiers BAND");
        check(tr.snapAccepted[0] == 0, "P2: and the snap did not run on it");
        check(std::abs(out.samples[0].gripPx.x() - ax) < 1e-9
              && std::abs(out.samples[0].gripPx.y() - ay) < 1e-9,
              "P2: the published grip is the pose anchor, unmoved");
        check(out.samples[0].conf > 0.75f && out.samples[0].conf <= 0.90f,
              "P2: BAND confidence sits above 0.75 and never above 0.9");
    }

    // ── P3 the minimum-length rule ──────────────────────────────────────────
    std::printf("\n=== P3: a 40 px stub where 104 px was due publishes NOTHING, with a reason ===\n");
    {
        const int n = 4;
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), GX);
        an.gy.assign(size_t(n), GY);
        const FrameSource none = [](int) { return cv::Mat(); };
        const SegmentGeom geom;

        DtlSolveState stub = makeState(n, kClub, 40.0, 0.93, cfg);
        stub.lFullPx = 320.0;
        stub.lFullSource = QStringLiteral("ball");
        const DtlShaftTrack2D a = dtlPostSolve(none, tUs, an, nullptr, stub, W, H, geom, cfg, nullptr);
        std::printf("       stub: tier %s — \"%s\"\n", dtlTierName(a.samples[0].tier),
                    a.samples[0].reason.toUtf8().constData());
        check(a.samples[0].tier == DtlTier::Unseen, "P3: the stub is UNSEEN");
        check(!std::isfinite(a.samples[0].thetaRad), "P3: and publishes no angle");
        check(a.samples[0].reason.contains(QStringLiteral("under")), "P3: and says the run was short");

        DtlSolveState full = makeState(n, kClub, 200.0, 0.93, cfg);
        full.lFullPx = 320.0;
        full.lFullSource = QStringLiteral("ball");
        const DtlShaftTrack2D b = dtlPostSolve(none, tUs, an, nullptr, full, W, H, geom, cfg, nullptr);
        std::printf("       control: tier %s, conf %.2f\n", dtlTierName(b.samples[0].tier),
                    double(b.samples[0].conf));
        check(b.samples[0].tier == DtlTier::Ray, "P3 control: a full-length run on the same evidence is RAY");
        check(b.samples[0].conf > 0.0f && b.samples[0].conf <= 0.90f,
              "P3 control: RAY confidence is evidence-scaled and never above 0.9");
        check(b.publishedInEndOn == 0, "P3 control: nothing published in an end-on frame");
    }

    // ── P5 the length is measured off the RE-REGISTERED line ────────────────
    std::printf("\n=== P5: lenPx comes off the snapped line, not off a ray from the pose grip ===\n");
    {
        // The measured defect, planted. The pose grip is the midpoint of two hand
        // keypoints and sits 30 px off the shaft axis, so a ray cast from it leaves
        // the thin shaft after ~100 px however long the club is — and ridgeSweep's
        // rEnd then reports EXACTLY rLo + minLenPx = 98 px, its own floor, because
        // that is where the score's argmax is searched from. On the dev six that
        // 98 px refused 51–101 frames a swing at instants where the club is in
        // plain view. The state below carries that 98 as the DP would have; the
        // rule under test is that the ladder no longer believes it.
        const int n = 4;
        const double u = kClub * kPi / 180.0;
        const double nx = -std::sin(u), ny = std::cos(u);
        const double ax = GX + 30.0 * nx, ay = GY + 30.0 * ny;
        const double kFloor = 98.0;          // 8 + 90 — ridgeSweep's minimum terminus radius
        const double kLFull = 322.0;         // ⇒ lenFloor = 0.35 × 0.93 × 322 ≈ 105 px, above the floor

        const auto runOne = [&](double shaftEnd, double lFull) {
            std::vector<cv::Mat> frames;
            frames.resize(size_t(n));
            for (int i = 0; i < n; ++i) {
                cv::Mat m = baseScene();
                drawStripedShaft(m, kClub, 15.0, shaftEnd);
                frames[size_t(i)] = m;
            }
            std::vector<int64_t> tUs(size_t(n), 0);
            for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
            DtlAnchors an;
            an.gx.assign(size_t(n), ax);
            an.gy.assign(size_t(n), ay);
            an.quarantined.assign(size_t(n), 0);
            DtlSolveState st = makeState(n, kClub, kFloor, 0.93, cfg);
            st.lFullPx = lFull;
            st.lFullSource = QStringLiteral("ball");
            const SegmentGeom geom;
            DtlDecideTrace tr;
            const FrameSource frameAt = [&frames](int i) -> cv::Mat {
                return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
            };
            const DtlShaftTrack2D out = dtlPostSolve(frameAt, tUs, an, nullptr, st,
                                                     W, H, geom, cfg, &tr);
            return out;
        };

        const double lenFloor = cfg.minLenFrac * 0.93 * kLFull;
        const DtlShaftTrack2D full = runOne(kLen, kLFull);    // a 120 px club
        const DtlSample &s = full.samples[0];
        std::printf("       before (the DP's rEnd): %.0f px, under the %.0f px the schedule asks for\n",
                    kFloor, lenFloor);
        std::printf("       after: tier %s, lenPx %.0f px (%s), true drawn length %.0f px\n",
                    dtlTierName(s.tier), s.lenPx, dtlLenSrcName(s.lenSrc), kLen);
        check(kFloor < lenFloor,
              "P5 control: the DP's own rEnd really was under the length the schedule asks for");
        check(std::isfinite(s.lenPx) && std::abs(s.lenPx - kLen) <= 0.10 * kLen,
              "P5: the measured run lands within 10% of the shaft's true length");
        check(s.lenSrc != DtlLenSrc::Rend,
              "P5: and it was measured off the re-registered line, not off the pose grip's ray");
        check(s.tier == DtlTier::Ray, "P5: so the frame publishes");

        // … and the rule still refuses a frame that is honestly short. A 110 px run
        // is a credible run — the ray plainly followed a shaft — and it is still
        // well under the 137 px a 420 px club at ρ̂ 0.93 is due, so the frame stays
        // UNSEEN on the MEASUREMENT rather than on the DP's floor. The point of the
        // minimum-length rule is not the threshold, it is that a stub is reported
        // as an absence rather than as a short measurement; measuring the run
        // better must not turn it into a measurement.
        const DtlShaftTrack2D stub = runOne(110.0, 420.0);
        const DtlSample &t = stub.samples[0];
        std::printf("       stub: tier %s — \"%s\"\n",
                    dtlTierName(t.tier), t.reason.toUtf8().constData());
        check(t.tier == DtlTier::Unseen, "P5: a genuinely short stub is still UNSEEN");
        check(t.lenSrc != DtlLenSrc::Rend,
              "P5: and it was refused on the measured run, not on the DP's floor");
        check(!std::isfinite(t.thetaRad), "P5: and still publishes no angle");
        check(t.reason.contains(QStringLiteral("under")), "P5: and still says the run was short");

        // … and a frame whose CONTRAST image carries no run at all is not a short
        // club, it is a channel that saw nothing. MEASURED at P5: the club is at
        // its fastest, motion blur wipes local contrast, and the contrast run
        // breaks after 10–48 px where the channel that won the frame terminates at
        // 232–390 px. The measurement then has nothing to say and the DP's own rEnd
        // stands, named as such — which is a different claim from a measured stub
        // and the trace has to be able to tell them apart.
        const DtlShaftTrack2D blind = runOne(15.0, kLFull);   // nothing drawn at all
        const DtlSample &blindS = blind.samples[0];
        std::printf("       nothing in the contrast image: tier %s, length source %s\n",
                    dtlTierName(blindS.tier), dtlLenSrcName(blindS.lenSrc));
        check(blindS.lenSrc == DtlLenSrc::Rend,
              "P5: with no credible run to measure, the length falls back to the DP's rEnd");
    }

    // ── P4 determinism ──────────────────────────────────────────────────────
    std::printf("\n=== P4: two post passes over one scene are identical ===\n");
    {
        const int n = 4;
        const double u = kClub * kPi / 180.0;
        const double ax = GX + 30.0 * -std::sin(u), ay = GY + 30.0 * std::cos(u);
        std::vector<cv::Mat> frames;
        frames.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15.0, kLen);
            frames[size_t(i)] = m;
        }
        const FrameSource frameAt = [&frames](int i) -> cv::Mat {
            return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
        };
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), ax);
        an.gy.assign(size_t(n), ay);
        const SegmentGeom geom;
        DtlSolveState s1 = makeState(n, kClub, kLen, 0.93, cfg);
        DtlSolveState s2 = makeState(n, kClub, kLen, 0.93, cfg);
        const DtlShaftTrack2D a = dtlPostSolve(frameAt, tUs, an, nullptr, s1, W, H, geom, cfg, nullptr);
        const DtlShaftTrack2D b = dtlPostSolve(frameAt, tUs, an, nullptr, s2, W, H, geom, cfg, nullptr);
        bool same = a.samples.size() == b.samples.size();
        for (size_t i = 0; same && i < a.samples.size(); ++i) {
            const DtlSample &x = a.samples[i], &y = b.samples[i];
            // Bit equality on purpose: club_dtl.json and trace_dtl.jsonl are
            // byte-identical across two runs of one swing, and that is a gate.
            same = x.tier == y.tier && x.conf == y.conf && x.gripPx == y.gripPx
                && ((std::isnan(x.thetaRad) && std::isnan(y.thetaRad)) || x.thetaRad == y.thetaRad);
        }
        check(same, "P4: tier, conf, grip and θ compare equal to the bit");
    }

    // ── P6 the reverse ray's ARM excuse (item 1) ────────────────────────────
    std::printf("\n=== P6: a reverse ray that runs up an arm is not held against the frame ===\n");
    {
        // Down the line the lead arm is near-collinear with the shaft at address
        // and impact and the forearms are at P3/P5, on the OPPOSITE side of the
        // grip, so the reverse ray of a CORRECT direction runs up the golfer's own
        // arm. Face-on's attachment test assumed free space behind the butt; this
        // view does not have it, and 126 in-span frames a run were refused on it.
        const int n = 4;
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        const FrameSource none = [](int) { return cv::Mat(); };
        const SegmentGeom geom;
        const int NSt = int(std::lround(360.0 / cfg.grid));
        const double revDeg = kClub + 180.0;

        // A state whose reverse bin is as strong as the solved one — the exact
        // condition the publication test refuses on.
        const auto strongReverse = [&](DtlSolveState &st) {
            int rb = int(std::lround(revDeg / cfg.grid)) % NSt;
            if (rb < 0) rb += NSt;
            for (int i = 0; i < n; ++i) st.EV[size_t(i)][size_t(rb)] = 0.90f;
        };
        const auto anchorsWithElbow = [&](double dirDeg, bool haveElbow) {
            DtlAnchors an;
            an.gx.assign(size_t(n), GX);
            an.gy.assign(size_t(n), GY);
            if (haveElbow) {
                const double t = dirDeg * kPi / 180.0;
                an.leadElbow.assign(size_t(n), cv::Point2d(GX + 100.0 * std::cos(t),
                                                           GY + 100.0 * std::sin(t)));
            }
            return an;
        };
        const auto runOne = [&](const DtlAnchors &an, bool ballGate, DtlDecideTrace *tr) {
            DtlSolveState st = makeState(n, kClub, 200.0, 0.93, cfg);
            st.lFullPx = 320.0;
            st.lFullSource = QStringLiteral("ball");
            strongReverse(st);
            if (ballGate) st.ballGate.assign(size_t(n), 1);
            return dtlPostSolve(none, tUs, an, nullptr, st, W, H, geom, cfg, tr);
        };

        DtlDecideTrace tArm, tFree, tBall;
        const DtlShaftTrack2D armed = runOne(anchorsWithElbow(revDeg, true), false, &tArm);
        const DtlShaftTrack2D freeS = runOne(anchorsWithElbow(revDeg - 60.0, true), false, &tFree);
        const DtlShaftTrack2D gated = runOne(anchorsWithElbow(revDeg - 60.0, true), true, &tBall);
        std::printf("       elbow ON the reverse: tier %s, waiver \"%s\"\n",
                    dtlTierName(armed.samples[0].tier),
                    dtlRevWaiverName(tArm.revWaived[0]));
        std::printf("       elbow 60 deg away:    tier %s, waiver \"%s\" — \"%s\"\n",
                    dtlTierName(freeS.samples[0].tier),
                    dtlRevWaiverName(tFree.revWaived[0]),
                    freeS.samples[0].reason.toUtf8().constData());
        std::printf("       same, ball-gated:     tier %s, waiver \"%s\"\n",
                    dtlTierName(gated.samples[0].tier),
                    dtlRevWaiverName(tBall.revWaived[0]));
        check(armed.samples[0].tier == DtlTier::Ray,
              "P6: an elbow planted along the reverse waives the test and the frame publishes");
        check(tArm.revWaived[0] == DtlRevWaiver::Arm, "P6: and the trace names the arm");
        check(freeS.samples[0].tier == DtlTier::Unseen,
              "P6 control: a reverse into FREE SPACE still refuses the frame");
        check(tFree.revWaived[0] == DtlRevWaiver::None, "P6 control: and nothing was waived");
        check(freeS.samples[0].reason.contains(QStringLiteral("reverse ray")),
              "P6 control: and says so");
        check(gated.samples[0].tier == DtlTier::Ray
              && tBall.revWaived[0] == DtlRevWaiver::BallGate,
              "P6: a ball-gated frame waives it too — the direction is already decided");
    }

    // ── P7 lineConf may stand in for the ray's EV (item 2) ──────────────────
    std::printf("\n=== P7: an ACCEPTED snap's line support publishes a frame the ray's EV refuses ===\n");
    {
        // EV is read along a ray from the POSE grip, which sits tens of px off the
        // shaft axis — so on a frame where the shaft is a plainly visible bright
        // streak the ray reads 0.37–0.40 against the 0.45 gate (0007 P5, 0008 P3,
        // 0008 P5, all adjudicated right by eye). The snapped line is a better
        // statement about the same frame, and it is admissible only where the snap
        // was ACCEPTED.
        const int n = 4;
        const double u = kClub * kPi / 180.0;
        const double nx = -std::sin(u), ny = std::cos(u);
        const double ax = GX + 30.0 * nx, ay = GY + 30.0 * ny;
        std::vector<cv::Mat> frames;
        frames.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15.0, kLen);
            frames[size_t(i)] = m;
        }
        const FrameSource frameAt = [&frames](int i) -> cv::Mat {
            return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
        };
        const FrameSource none = [](int) { return cv::Mat(); };
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), ax);
        an.gy.assign(size_t(n), ay);
        const SegmentGeom geom;
        const int NSt = int(std::lround(360.0 / cfg.grid));
        int bi = int(std::lround(kClub / cfg.grid)) % NSt; if (bi < 0) bi += NSt;

        const auto weakEv = [&](DtlSolveState &st) {
            for (int i = 0; i < n; ++i) st.EV[size_t(i)][size_t(bi)] = 0.40f;   // under evRay 0.45
        };
        const auto runOne = [&](const FrameSource &src, double lineConfRay, DtlDecideTrace *tr) {
            DtlShaftConfig c = cfg;
            c.lineConfRay = lineConfRay;
            DtlSolveState st = makeState(n, kClub, kLen, 0.93, c);
            weakEv(st);
            return dtlPostSolve(src, tUs, an, nullptr, st, W, H, geom, c, tr);
        };

        // First pass with the gate out of reach, to READ the snap's own support —
        // the threshold is a measured p10 on real frames and a synthetic must not
        // pretend to know it.
        DtlDecideTrace t0;
        const DtlShaftTrack2D inert = runOne(frameAt, 2.0, &t0);
        const double lc = t0.snapBestConf[0];
        std::printf("       accepted snap = %d, its line support = %.3f, ray EV = 0.40\n",
                    int(t0.snapAccepted[0]), lc);
        check(t0.snapAccepted[0] != 0 && std::isfinite(lc),
              "P7 precondition: the snap ran and was accepted on this frame");
        check(inert.samples[0].tier == DtlTier::Unseen,
              "P7 control: with the line gate out of reach, EV 0.40 publishes nothing");

        DtlDecideTrace tOn, tOff, tNoSnap;
        const DtlShaftTrack2D on  = runOne(frameAt, lc - 0.01, &tOn);
        const DtlShaftTrack2D off = runOne(frameAt, lc + 0.01, &tOff);
        const DtlShaftTrack2D noS = runOne(none,    lc - 0.01, &tNoSnap);
        std::printf("       lineConfRay %.3f: tier %s (evSrc %s) | %.3f: tier %s | no snap: tier %s — \"%s\"\n",
                    lc - 0.01, dtlTierName(on.samples[0].tier),
                    dtlEvSrcName(on.samples[0].evSrc), lc + 0.01,
                    dtlTierName(off.samples[0].tier), dtlTierName(noS.samples[0].tier),
                    noS.samples[0].reason.toUtf8().constData());
        check(on.samples[0].tier == DtlTier::Ray,
              "P7: the snapped line's support publishes the frame");
        check(on.samples[0].evSrc == DtlEvSrc::LineConf,
              "P7: and the sample SAYS the line is what let it through");
        check(off.samples[0].tier == DtlTier::Unseen,
              "P7: a line that misses the floor publishes nothing");
        check(noS.samples[0].tier == DtlTier::Unseen,
              "P7: and with NO accepted snap there is no line to stand in for the ray");
        check(noS.samples[0].reason.contains(QStringLiteral("no evidence")),
              "P7: which is still reported as an absence of evidence");
    }

    // ── P8 the published length is the LONGER of the two (item 3) ───────────
    std::printf("\n=== P8: a bloomed ribbon breaks the contrast run, so rEnd may be the longer truth ===\n");
    {
        // At this exposure a moving shaft is a ribbon 10–20 px wide — wider than
        // the lateral background offsets the thin-line profile credits against —
        // so the measured run reads evidence-free and breaks early while rEnd
        // carries the club. 0006 P3 went 326 px (rEnd, c2) → 130 px (snapLine, c3)
        // with the shaft visible to the corner in both.
        const int n = 4;
        const double u = kClub * kPi / 180.0;
        const double nx = -std::sin(u), ny = std::cos(u);
        const double ax = GX + 30.0 * nx, ay = GY + 30.0 * ny;
        std::vector<cv::Mat> frames;
        frames.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15.0, 110.0);       // the run the contrast image can see
            frames[size_t(i)] = m;
        }
        const FrameSource frameAt = [&frames](int i) -> cv::Mat {
            return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
        };
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), ax);
        an.gy.assign(size_t(n), ay);
        const SegmentGeom geom;
        const double kLFull = 320.0;                       // ceiling = 1.25 x 0.93 x 320 = 372 px
        const auto runOne = [&](double rend) {
            DtlSolveState st = makeState(n, kClub, rend, 0.93, cfg);
            st.lFullPx = kLFull;
            st.lFullSource = QStringLiteral("ball");
            return dtlPostSolve(frameAt, tUs, an, nullptr, st, W, H, geom, cfg, nullptr);
        };
        const DtlShaftTrack2D trFloor = runOne(98.0);    // the 98 px floor artefact
        const DtlShaftTrack2D trReal  = runOne(300.0);   // credible, under the ceiling
        const DtlShaftTrack2D trOver  = runOne(400.0);   // over D3's own ceiling
        const DtlSample &base = trFloor.samples[0];
        const DtlSample &wins = trReal.samples[0];
        const DtlSample &over = trOver.samples[0];
        std::printf("       rEnd  98 (floor): lenPx %.0f (%s)\n", base.lenPx, dtlLenSrcName(base.lenSrc));
        std::printf("       rEnd 300 (real):  lenPx %.0f (%s)\n", wins.lenPx, dtlLenSrcName(wins.lenSrc));
        std::printf("       rEnd 400 (over):  lenPx %.0f (%s)\n", over.lenPx, dtlLenSrcName(over.lenSrc));
        check(base.lenSrc != DtlLenSrc::Rend && base.lenPx < 150.0,
              "P8: the 98 px floor artefact never wins — it is not a length");
        check(wins.lenSrc == DtlLenSrc::Rend && std::abs(wins.lenPx - 300.0) < 1e-6,
              "P8: a credible rEnd under D3's ceiling DOES win over a broken contrast run");
        check(over.lenSrc != DtlLenSrc::Rend && over.lenPx < 150.0,
              "P8: and an rEnd over the ceiling is a ray past the club, not a longer one");
    }

    // ── P9 the snap is scored over the club, not over the DP's argmax ───────
    std::printf("\n=== P9: the snap's objective extent is the visibility law, not the DP's rEnd ===\n");
    {
        // snapSearch's objective is the MEAN evidence over [rLo, drawnLen), so
        // drawnLen decides which part of the club the search is scored on. At
        // address the DP's rEnd is the NEAR HALF (98–216 px against a 316 px
        // projection) and the near half contains a brighter ridge than the club:
        // measured on swing 0004, thirteen address frames, the snap lands at
        // 55.0–58.0° with drawnLen = rEnd and at 54.0–54.5° with the law's own
        // length, against a band truth of 50.75°.
        const int n = 4;
        const double kTrue = 50.0, kDecoy = 62.0;
        const double u = kTrue * kPi / 180.0;
        const double nx = -std::sin(u), ny = std::cos(u);
        const double ax = GX + 8.0 * nx, ay = GY + 8.0 * ny;
        std::vector<cv::Mat> frames;
        frames.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kTrue, 15.0, 190.0);                  // the club
            cv::line(m, ptAt(GX, GY, kDecoy, 12.0), ptAt(GX, GY, kDecoy, 80.0),
                     cv::Scalar(255), 5, cv::LINE_8);                  // a short bright decoy
            frames[size_t(i)] = m;
        }
        const FrameSource frameAt = [&frames](int i) -> cv::Mat {
            return (i >= 0 && i < int(frames.size())) ? frames[size_t(i)] : cv::Mat();
        };
        std::vector<int64_t> tUs(size_t(n), 0);
        for (int i = 0; i < n; ++i) tUs[size_t(i)] = int64_t(i) * 6640;
        DtlAnchors an;
        an.gx.assign(size_t(n), ax);
        an.gy.assign(size_t(n), ay);
        const SegmentGeom geom;
        const auto runOne = [&](double lFull, DtlDecideTrace *tr) {
            DtlSolveState st = makeState(n, kTrue, 80.0, 0.93, cfg);   // rEnd = the decoy's length
            st.lFullPx = lFull;                                        // NaN ⇒ no law to appeal to
            st.lFullSource = std::isfinite(lFull) ? QStringLiteral("ball") : QStringLiteral("none");
            return dtlPostSolve(frameAt, tUs, an, nullptr, st, W, H, geom, cfg, tr);
        };
        DtlDecideTrace tShort, tLaw;
        const DtlShaftTrack2D shortE = runOne(kNan, &tShort);
        const DtlShaftTrack2D lawE   = runOne(204.0, &tLaw);           // 0.93 x 204 = 190 px, the club
        const double eShort = std::abs(shaftshared::circWrap(tShort.thetaOutDeg[0] - kTrue));
        const double eLaw   = std::abs(shaftshared::circWrap(tLaw.thetaOutDeg[0]   - kTrue));
        std::printf("       drawn %.0f px (the DP's rEnd): th %.2f, %.2f deg off the club\n",
                    tShort.snapDrawnPx[0], tShort.thetaOutDeg[0], eShort);
        std::printf("       drawn %.1f px (rho x L):     th %.2f, %.2f deg off the club\n",
                    tLaw.snapDrawnPx[0], tLaw.thetaOutDeg[0], eLaw);
        check(std::abs(tShort.snapDrawnPx[0] - 80.0) < 1e-6,
              "P9 control: with no length law the extent is still the DP's rEnd");
        check(std::abs(tLaw.snapDrawnPx[0] - 0.93 * 204.0) < 1e-6,
              "P9: with the law known the extent is rho_D x L_D, the predicted projected club");
        check(eLaw <= eShort,
              "P9: and scoring over the whole club is never worse than scoring over its near end");
        check(eLaw <= 2.0, "P9: it lands on the club");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
