// Standalone tests for the DTL deciding half, part 1 (src/Analysis/
// dtl_shaft_decide) — the band solve, over synthetic scenes.
//
// The scenes carry the two findings the design turns on, because neither is
// visible in a unit test that only checks arithmetic:
//
//  · THE POLARITY TRAP. The shaft alternates black and white bands, and down the
//    line much of it is seen against the MID-GREY lit simulator screen. A signed
//    bright-ridge response CANCELS along such a shaft and a wide bright limb
//    wins — §2's failure exactly: a measured-tier line along the lead forearm
//    while the real shaft is in plain view. T1 plants both and shows that the
//    polarity-free local-contrast channel is what makes the shaft a candidate at
//    all, and — the part worth reading — that being a candidate is not the same
//    as winning: under the shared percentile normalisation BOTH top bins reach
//    1.0, so the evidence TIES and D2 is what separates them (§5.6, §8). T3 is
//    that second half.
//
//  · THE SCHEDULE IS THE POINT. A forearm lock at the top is not out-scored, it
//    is made IMPOSSIBLE: the club cannot be 300 px long when it is pointing at
//    the lens. T2/T5/T6 assert that an end-on or UNKNOWN ρ̂_D produces nothing —
//    not a low-confidence angle, not a bridged one.
//
// evAbsFloor is switched OFF in these scenes on purpose: it is an ABSOLUTE score
// calibrated on 1.3 MP real frames and these are 320² toys. What is under test
// here is the decision layer, not the floor's calibration.
//
//   cmake --build build/tests --target dtl_shaft_decide_test
//   ctest --test-dir build/tests -R dtl_shaft_decide --output-on-failure

#include "../dtl_shaft_decide.h"
#include "../shaft_track_shared.h"   // normScores, for the raw-only control

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
static constexpr double GX = 160.0, GY = 160.0;
static const double kNan = std::numeric_limits<double>::quiet_NaN();

static constexpr double kClub = 50.0;    // the shaft's direction from the grip
static constexpr double kArm  = 150.0;   // the counterfeit's

// The face-on witness values used by the sighted scenes. ρ_F = 0.8 at θ_F = 63.4°
// puts u_x = 0.358, so ρ̂_D = 0.93 (long), and the corridor's second centre
// θ̂_D⁻ = atan2(ρ_F sinθ_F, +√(1−ρ_F²)) lands at the club. The end-on scenes use
// ρ_F = 0.995 at θ_F = 0° — the club along the optical axis, ρ̂_D = 0.10.
static constexpr double kRhoLong = 0.8,   kThetaLongDeg = 63.4;
static constexpr double kRhoStub = 0.995, kThetaStubDeg = 0.0;

// ── the scene ────────────────────────────────────────────────────────────────
// Mid-grey with a deterministic texture: on a perfectly flat field normScores
// would turn pure rounding into a full-strength winner, which is a different
// bug's test.
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

// A taped shaft: alternating black and white runs, 5 px wide. Over mid-grey its
// SIGNED ridge response cancels; its local contrast does not.
static void drawStripedShaft(cv::Mat &m, double thDeg, double r0, double r1)
{
    for (double r = r0; r < r1; r += 10.0) {
        const int v = (int(std::lround((r - r0) / 10.0)) % 2) ? 255 : 0;
        cv::line(m, ptAt(GX, GY, thDeg, r), ptAt(GX, GY, thDeg, std::min(r + 10.0, r1)),
                 cv::Scalar(v), 5, cv::LINE_8);
    }
}

// The same taped shaft over a background that barely lights it — the regime the
// markerless work names ("bare steel over a lit but unclipped mat has no
// contrast"). It is still a real alternating ridge; it simply does not top the
// contrast channel's own percentile normalisation, so a brighter, longer limb
// can out-score it instead of tying it.
static void drawFaintShaft(cv::Mat &m, double thDeg, double r0, double r1)
{
    for (double r = r0; r < r1; r += 10.0) {
        const int v = (int(std::lround((r - r0) / 10.0)) % 2) ? 156 : 100;
        cv::line(m, ptAt(GX, GY, thDeg, r), ptAt(GX, GY, thDeg, std::min(r + 10.0, r1)),
                 cv::Scalar(v), 5, cv::LINE_8);
    }
}

// A wide uniformly bright limb from the grip — the counterfeit the signed ridge
// loves, and the one §2 found published at measured tier through the whole top.
static void drawLimb(cv::Mat &m, double thDeg, double r1)
{
    cv::line(m, ptAt(GX, GY, thDeg, 0.0), ptAt(GX, GY, thDeg, r1), cv::Scalar(225), 15, cv::LINE_8);
}

// ── the inputs ───────────────────────────────────────────────────────────────
struct Scene {
    std::vector<cv::Mat> frames;
    std::vector<int64_t> tUs;
    DtlAnchors           an;
    FaceOnWitness        wit;
};

static constexpr int64_t kDt = 6640;   // 150.6 fps, the 07-04 DTL cadence
// The scene does not start at t = 0. A ladder entry at a NEGATIVE time is read as
// "no P1 in the witness" and falls back to the first sample (dtl_shaft_decide,
// inherited time), so a scene that wants to say "P1 is before all of this" — which
// every mid-band scene below does, to keep the still-club gate out of it — needs
// room to the left of its own first frame.
static constexpr int64_t kT0 = 1000000;

static Scene makeScene(int n)
{
    Scene s;
    s.tUs.resize(size_t(n));
    for (int i = 0; i < n; ++i) s.tUs[size_t(i)] = kT0 + int64_t(i) * kDt;
    s.frames.assign(size_t(n), cv::Mat());
    s.an.gx.assign(size_t(n), GX);
    s.an.gy.assign(size_t(n), GY);
    s.an.leadElbow.assign(size_t(n), {kNan, kNan});
    s.an.trailElbow.assign(size_t(n), {kNan, kNan});
    s.an.leadWrist.assign(size_t(n), {GX - 6.0, GY});
    s.an.trailWrist.assign(size_t(n), {GX + 6.0, GY});
    s.an.quarantined.assign(size_t(n), 0);
    // The 8 body joints in the face-on tracker's order: shoulders, hips, knees,
    // ankles. Placed so the grip sits OUT toward the ball from the hips and the
    // ankles are above the mat, which is the DTL ball prior's sentence (§4.3).
    s.an.joints.assign(size_t(n), std::vector<cv::Point2d>{
        {100, 90}, {120, 90}, {100, 150}, {120, 150},
        {100, 210}, {120, 210}, {100, 260}, {120, 260} });
    return s;
}

// gripYPx = gy so the cross-view row fit is the identity and no frame is
// quarantined for a row residual.
static void fillWitness(Scene &s, const std::vector<double> &rhoF,
                        const std::vector<double> &thetaFDeg,
                        FoTier tier, int64_t p1Us, int64_t impactUs)
{
    const int n = int(s.tUs.size());
    s.wit.tUs = s.tUs;
    s.wit.thetaUnwrapRad.resize(size_t(n));
    for (int i = 0; i < n; ++i) s.wit.thetaUnwrapRad[size_t(i)] = thetaFDeg[size_t(i)] * kPi / 180.0;
    s.wit.thetaDotRadS.assign(size_t(n), 0.0);
    s.wit.rhoF = rhoF;
    s.wit.gripYPx.assign(size_t(n), GY);
    s.wit.tier.assign(size_t(n), tier);
    s.wit.phase.assign(size_t(n), 1);
    s.wit.ladder = { {1, p1Us}, {7, impactUs} };
    s.wit.impactUs = impactUs;
    s.wit.fullLenPx = 140.0;
    s.wit.frameIntervalUs = kDt;
}

static void fillWitnessFlat(Scene &s, double rhoF, double thetaFDeg,
                            FoTier tier, int64_t p1Us, int64_t impactUs)
{
    const size_t n = s.tUs.size();
    fillWitness(s, std::vector<double>(n, rhoF), std::vector<double>(n, thetaFDeg),
                tier, p1Us, impactUs);
}

// A witness whose θ_F sits STILL through the address hold and then leaves, at
// 3°/frame from `moveFrom`. The still-club hold (§4.3) reads exactly that column:
// a witness whose θ_F never moves is saying the club never left the ball, so a
// scene that wants ordinary mid-band frames has to let the takeaway happen.
static void fillWitnessTakeaway(Scene &s, double rhoF, double thetaFDeg, FoTier tier,
                                int64_t p1Us, int64_t impactUs, int moveFrom)
{
    const int n = int(s.tUs.size());
    std::vector<double> thF(size_t(n), thetaFDeg);
    for (int i = moveFrom; i < n; ++i)
        thF[size_t(i)] = thetaFDeg + 3.0 * double(i - moveFrom + 1);
    fillWitness(s, std::vector<double>(size_t(n), rhoF), thF, tier, p1Us, impactUs);
}

// A ladder placed so NO frame of the scene is a still-club frame: P1 a second
// before it and impact ten seconds after. The still-club gate (address hold,
// ±20 ms of impact) refuses a frame outright when there is no DTL ball to point
// at, which is correct and is T10/T11's subject — but it would otherwise silently
// eat the first frames of every mid-band scene below, and those scenes are about
// evidence, not about the ball.
static constexpr int64_t kNoStillP1  = 1;
static constexpr int64_t kNoStillImp = 10000000;

static DtlShaftConfig testCfg()
{
    DtlShaftConfig c;
    c.evAbsFloor = 0.0;          // see the header note
    c.evAbsFloorDif = -1.0;
    c.ridge.rHi = 200.0f;        // the scene is 320 px; 470 would be all off-frame samples
    return c;
}

static FrameSource srcOf(const Scene &s)
{
    return [&s](int i) -> cv::Mat {
        return (i >= 0 && i < int(s.frames.size())) ? s.frames[size_t(i)] : cv::Mat();
    };
}

static double dWrap(double a, double b)
{
    const double d = std::fmod(std::fmod(a - b + 180.0, 360.0) + 360.0, 360.0) - 180.0;
    return std::abs(d);
}

static int solvedCount(const DtlSolveState &st)
{
    int n = 0;
    for (const char c : st.solved) n += c ? 1 : 0;
    return n;
}

int main()
{
    // ── T1 the polarity trap ────────────────────────────────────────────────
    std::printf("=== T1: the striped shaft is a candidate ONLY through the contrast channel ===\n");
    {
        Scene s = makeScene(12);
        for (int i = 0; i < 12; ++i) {
            cv::Mat m = baseScene();
            drawLimb(m, kArm, 130.0);              // the counterfeit
            drawStripedShaft(m, kClub, 15, 140);   // the club
            s.frames[size_t(i)] = m;
        }
        fillWitnessFlat(s, kRhoLong, kThetaLongDeg, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlShaftConfig cfg = testCfg();
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, cfg, nullptr);
        check(st.bands.size() == 1 && solvedCount(st) == 12, "T1: one band, every frame solved");

        // The control the whole channel rests on: a SIGNED ridge really does
        // prefer the limb, and would leave the shaft well short of full strength.
        cv::Mat g32; s.frames[0].convertTo(g32, CV_32F);
        std::vector<float> th(360);
        for (int k = 0; k < 360; ++k) th[size_t(k)] = float(double(k) * kPi / 180.0);
        const RidgeResult raw = ridgeSweep(g32, GX, GY, th, cfg.ridge, false);
        const std::vector<float> rawNorm = shaftshared::normScores(raw.score);
        std::printf("       raw score: limb %.0f vs shaft %.0f | rawNorm: limb %.2f shaft %.2f\n",
                    raw.score[size_t(int(kArm))], raw.score[size_t(int(kClub))],
                    rawNorm[size_t(int(kArm))], rawNorm[size_t(int(kClub))]);
        check(raw.score[size_t(int(kArm))] > raw.score[size_t(int(kClub))],
              "T1 control: the raw signed ridge scores the limb ABOVE the shaft");
        check(rawNorm[size_t(int(kClub))] < 0.80,
              "T1 control: raw alone leaves the shaft short of full strength");

        const double evClub = double(st.EV[0][size_t(int(kClub))]);
        const double evLimb = double(st.EV[0][size_t(int(kArm))]);
        std::printf("       fused EV: shaft %.3f  limb %.3f\n", evClub, evLimb);
        check(evClub >= 0.95,
              "T1: the fused evidence puts the shaft at FULL strength — the contrast channel did that");
        // Worth saying out loud rather than asserting away: under the shared
        // percentile normalisation the limb's top bin also reaches 1.0, so the
        // EVIDENCE ties. §5.6 says D2 is what separates them; T3 is that test.
        std::printf("       (the limb ties at full strength too — evidence alone cannot separate them;\n"
                    "        D2 is the discriminator, per §5.6, and T3 exercises it)\n");
    }

    // ── T2 end-on publishes nothing ─────────────────────────────────────────
    std::printf("\n=== T2: ρ̂_D = 0.1 with only a forearm in frame ⇒ END-ON, nothing solved ===\n");
    {
        Scene s = makeScene(12);
        for (int i = 0; i < 12; ++i) {
            cv::Mat m = baseScene();
            drawLimb(m, kArm, 130.0);
            s.frames[size_t(i)] = m;
        }
        const cv::Point2d elbow{ GX + 80 * std::cos(kArm * kPi / 180.0),
                                 GY + 80 * std::sin(kArm * kPi / 180.0) };
        for (int i = 0; i < 12; ++i) { s.an.leadElbow[size_t(i)] = elbow; s.an.trailElbow[size_t(i)] = elbow; }
        fillWitnessFlat(s, kRhoStub, kThetaStubDeg, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, testCfg(), nullptr);
        std::printf("       ρ̂_D = %.3f, bands = %zu, solved = %d, reason = \"%s\"\n",
                    st.rhoPred[0], st.bands.size(), solvedCount(st),
                    st.reason[0].toUtf8().constData());
        check(st.rhoPred[0] < 0.35, "T2: the schedule reports a stub");
        check(st.bands.empty() && solvedCount(st) == 0,
              "T2: no band, no solved frame — the forearm lock is impossible, not out-scored");
        check(st.reason[0].contains(QStringLiteral("end-on")), "T2: and it says why");
    }

    // ── T3 the same limb, with the club visible ─────────────────────────────
    std::printf("\n=== T3: ρ̂_D = 0.93 with the club present ⇒ the club, and D2 records the veto ===\n");
    {
        Scene s = makeScene(12);
        for (int i = 0; i < 12; ++i) {
            cv::Mat m = baseScene();
            drawLimb(m, kArm, 130.0);
            drawStripedShaft(m, kClub, 15, 140);
            s.frames[size_t(i)] = m;
        }
        const cv::Point2d elbow{ GX + 80 * std::cos(kArm * kPi / 180.0),
                                 GY + 80 * std::sin(kArm * kPi / 180.0) };
        for (int i = 0; i < 12; ++i) { s.an.leadElbow[size_t(i)] = elbow; s.an.trailElbow[size_t(i)] = elbow; }
        fillWitnessFlat(s, kRhoLong, kThetaLongDeg, FoTier::Ray, kNoStillP1, kNoStillImp);
        DtlDecideTrace tr;
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, testCfg(), &tr);
        double worst = 0.0;
        for (int i = 0; i < 12; ++i) worst = std::max(worst, dWrap(st.thetaDeg[size_t(i)], kClub));
        std::printf("       solved θ = %.1f° (club %.1f°), worst |Δ| = %.2f°\n",
                    st.thetaDeg[0], kClub, worst);
        std::printf("       EV plateau:");
        for (int k = 40; k <= 60; k += 2) std::printf(" %d:%.2f", k, double(st.EV[0][size_t(k)]));
        std::printf("\n");
        // 6°, not 2°: the drawn shaft is a rasterised zig-zag 5 px wide, so its
        // fused evidence is a FLAT plateau ~10 bins across and the DP's tie-break
        // takes the plateau's low edge. What the test is separating is the club
        // from a limb 100° away, and that separation is not in doubt at 6°.
        check(solvedCount(st) == 12 && worst <= 6.0,
              "T3: solved θ is the club (within the synthetic ridge's 6° plateau)");
        check(st.ARMVETO[0][size_t(int(kArm))] != 0,
              "T3: the along-the-forearm bin is vetoed (the ray runs along it AND passes the elbow)");
        check(st.ARMVETO[0][size_t(int(kClub))] == 0, "T3: the club's bin is not");
        check(!tr.d2ArmVeto.empty() && tr.d2ArmVeto[0] != 0, "T3: the trace records the veto");
        check(!tr.corrSignTaken.empty() && tr.corrSignTaken[0] != 0,
              "T3: and the trace records WHICH corridor centre the solve took (§4.2's open sign)");
    }

    // ── T4 the alignment stick ──────────────────────────────────────────────
    std::printf("\n=== T4: the static alignment stick is not chosen ===\n");
    {
        // 80 frames with a REAL address hold (P1 at frame 30, impact at frame 50)
        // so the phase-aware plate has an address median AND the ball's launch can
        // be tested. The ball sits on the club's own line, beyond the shaft's drawn
        // end — the still-club gate needs something to point at, and where there is
        // none the address frames are refused (T11). The stick is placed where the
        // real one is: lying on the mat, NOT along a ray out of the grip, so what
        // kills it is that no ray from the hands runs down it.
        const int n = 80;
        Scene s = makeScene(n);
        const cv::Point ball = ptAt(GX, GY, kClub, 160.0);
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            cv::line(m, cv::Point(60, 200), cv::Point(66, 300), cv::Scalar(250), 7, cv::LINE_8);
            drawStripedShaft(m, kClub, 15, 120);
            if (i < 50) cv::circle(m, ball, 7, cv::Scalar(250), -1);
            else        cv::circle(m, ball, 10, cv::Scalar(110), -1);   // struck, and gone
            s.frames[size_t(i)] = m;
        }
        const int64_t p1 = kT0 + int64_t(30) * kDt, imp = kT0 + int64_t(50) * kDt;
        fillWitnessFlat(s, kRhoLong, kThetaLongDeg, FoTier::Ray, p1, imp);
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, testCfg(), nullptr);
        int nSolved = 0, f = -1;
        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            if (st.solved[size_t(i)]) {
                worst = std::max(worst, dWrap(st.thetaDeg[size_t(i)], kClub));
                ++nSolved;
                if (f < 0) f = i;
            }
        std::printf("       ball found=%d, solved %d frames, worst |Δ| from the club = %.2f°\n",
                    int(st.ball.found), nSolved, worst);
        check(st.ball.found, "T4: the ball is found, so the address hold is solvable at all");
        // The inherited span ends 150 ms after the last ladder entry (impact at
        // frame 50), which is frame 72 — so 73 frames, not 80. Stated rather than
        // counted loosely: the span is a claim about what was LOOKED at.
        check(nSolved == 73, "T4: every frame inside the inherited span solves");
        check(worst <= 6.0, "T4: every solved frame is the club, never the stick");
        // ~138° is the direction of the stick from the grip; a ray CROSSES it
        // rather than running down it, so it never assembles a credible run.
        check(f >= 0 && st.EV[size_t(f)][size_t(int(kClub))] > st.EV[size_t(f)][138],
              "T4: and the evidence, not a tie-break, is what prefers the club");
    }

    // ── T5 a NaN ρ_F is the schedule's conservative BOUND ───────────────────
    std::printf("\n=== T5: ρ_F NaN ⇒ ρ_F := 1, i.e. ρ̂_D = |sin θ_F| — sighted when face-on is "
                "vertical, END-ON when it is horizontal ===\n");
    {
        // Face-on does not MEASURE a club length at address (it coasts) or at
        // impact (it reconstructs), and those are the two best-seen DTL moments of
        // the swing. The bound admits them without admitting anything else: it
        // MAXIMISES |u_x| and so MINIMISES ρ̂_D, which is why the horizontal half
        // below still reads END-ON.
        const auto scene = [](int n) {
            Scene sc = makeScene(n);
            for (int i = 0; i < n; ++i) {
                cv::Mat m = baseScene();
                drawStripedShaft(m, kClub, 15, 140);
                sc.frames[size_t(i)] = m;
            }
            return sc;
        };
        Scene up = scene(12);
        fillWitnessFlat(up, kNan, 90.0, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlSolveState a = dtlSolve(srcOf(up), up.tUs, up.an, &up.wit, W, H, 150.6,
                                         {}, 1120.0, testCfg(), nullptr);
        std::printf("       θ_F = 90° (address/impact): ρ̂_D = %.3f, src = %s, bands = %zu, solved = %d\n",
                    a.rhoPred[0], dtlRhoSrcName(a.rhoSrc[0]), a.bands.size(), solvedCount(a));
        check(a.rhoSrc[0] == DtlRhoSrc::Bound, "T5: the schedule records that it used the bound");
        check(std::isfinite(a.rhoPred[0]) && a.rhoPred[0] > 0.99,
              "T5: a vertical face-on shaft of unknown length is FULLY sighted down the line");
        check(a.bands.size() == 1 && solvedCount(a) == 12,
              "T5: and the club in plain view is solved — 96 frames a swing stop being thrown away");
        // The half that keeps the bound honest.
        Scene flat = scene(12);
        fillWitnessFlat(flat, kNan, 0.0, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlSolveState b = dtlSolve(srcOf(flat), flat.tUs, flat.an, &flat.wit, W, H, 150.6,
                                         {}, 1120.0, testCfg(), nullptr);
        std::printf("       θ_F = 0° (the top): ρ̂_D = %.3f, src = %s, bands = %zu, reason = \"%s\"\n",
                    b.rhoPred[0], dtlRhoSrcName(b.rhoSrc[0]), b.bands.size(),
                    b.reason[0].toUtf8().constData());
        check(b.rhoSrc[0] == DtlRhoSrc::Bound && b.rhoPred[0] < 0.01,
              "T5: a HORIZONTAL face-on shaft of unknown length bounds to a stub");
        check(b.bands.empty() && solvedCount(b) == 0,
              "T5: so the club in plain view is STILL not solved there — the bound is conservative");
        check(b.reason[0].contains(QStringLiteral("end-on")),
              "T5: and it is an end-on absence, which is a claim about the geometry");
    }

    // ── T6 bands are independent ────────────────────────────────────────────
    std::printf("\n=== T6: a 10-frame end-on gap yields two bands and NaN θ in the gap ===\n");
    {
        const int n = 30;
        Scene s = makeScene(n);
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15, 140);
            s.frames[size_t(i)] = m;
        }
        std::vector<double> rho(size_t(n), kRhoLong), thF(size_t(n), kThetaLongDeg);
        for (int i = 10; i < 20; ++i) { rho[size_t(i)] = kRhoStub; thF[size_t(i)] = kThetaStubDeg; }
        fillWitness(s, rho, thF, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, testCfg(), nullptr);
        std::printf("       bands = %zu", st.bands.size());
        for (const DtlBand &b : st.bands)
            std::printf("  [%d..%d] \"%s\"", b.lo, b.hi, b.name.toUtf8().constData());
        std::printf("\n");
        check(st.bands.size() == 2, "T6: two bands");
        check(st.bands.size() == 2 && st.bands[0].lo == 0 && st.bands[0].hi == 9
              && st.bands[1].lo == 20 && st.bands[1].hi == 29,
              "T6: with the gap's edges exactly");
        bool gapClean = true;
        for (int i = 10; i < 20; ++i)
            gapClean &= !st.solved[size_t(i)] && !std::isfinite(st.thetaDeg[size_t(i)]);
        check(gapClean, "T6: nothing is solved and nothing is bridged across the gap");
    }

    // ── T7 the corridor ablation changes nothing else ───────────────────────
    std::printf("\n=== T7: corridor off vs on — same bands, same END-ON set ===\n");
    {
        const int n = 30;
        Scene s = makeScene(n);
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15, 140);
            s.frames[size_t(i)] = m;
        }
        std::vector<double> rho(size_t(n), kRhoLong), thF(size_t(n), kThetaLongDeg);
        for (int i = 10; i < 20; ++i) { rho[size_t(i)] = kRhoStub; thF[size_t(i)] = kThetaStubDeg; }
        fillWitness(s, rho, thF, FoTier::Ray, kNoStillP1, kNoStillImp);
        DtlShaftConfig on = testCfg(), off = testCfg();
        off.corridor.enabled = false;
        const DtlSolveState a = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6, {}, 1120.0, on,  nullptr);
        const DtlSolveState b = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6, {}, 1120.0, off, nullptr);
        bool sameBands = a.bands.size() == b.bands.size();
        for (size_t k = 0; sameBands && k < a.bands.size(); ++k)
            sameBands &= a.bands[k].lo == b.bands[k].lo && a.bands[k].hi == b.bands[k].hi;
        check(sameBands, "T7: the same bands");
        check(a.sighted == b.sighted && a.solved == b.solved && a.rhoPred == b.rhoPred,
              "T7: the same sighted / solved / ρ̂_D — the ablation moves only the cost");
        check(b.corridorOn == std::vector<char>(size_t(n), 0),
              "T7: and with it off, no frame reports a corridor at all");
        check(a.corridorOn != b.corridorOn, "T7 control: with it on, frames DO report one");
    }

    // ── T8 the DTL ball ─────────────────────────────────────────────────────
    std::printf("\n=== T8: dtlFindBall — the ball leaves; the mat marking and the stick do not ===\n");
    {
        const int n = 40;
        Scene s = makeScene(n);
        // Beyond the grip on the golfer's side and below the ankle line: hips at
        // x = 110, grip at x = 160, ankles at (110, 260), so a ball must sit
        // right of x ≈ 210 and below y ≈ 254.
        const double bx = 250.0, by = 285.0;
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            cv::circle(m, cv::Point(int(bx), int(by)), 7, cv::Scalar(250), -1);         // the ball
            cv::circle(m, cv::Point(300, 300), 7, cv::Scalar(250), -1);                 // a permanent white disc
            cv::line(m, cv::Point(270, 250), cv::Point(276, 315), cv::Scalar(250), 7);  // an alignment stick
            if (i >= 25) cv::circle(m, cv::Point(int(bx), int(by)), 10, cv::Scalar(110), -1);  // launched
            s.frames[size_t(i)] = m;
        }
        std::vector<int> addr, post;
        for (int i = 0; i < 20; ++i) addr.push_back(i);
        for (int i = 30; i < n; ++i) post.push_back(i);
        const DtlBall ball = dtlFindBall(srcOf(s), addr, post, s.an, W, H);
        std::printf("       found=%d at (%.1f, %.1f) — %s\n", int(ball.found), ball.x, ball.y,
                    ball.reason.toUtf8().constData());
        check(ball.found, "T8: the ball is found");
        check(ball.found && std::hypot(ball.x - bx, ball.y - by) < 4.0, "T8: at the planted position");

        // With nothing that ever leaves, the honest answer is NO ball.
        Scene s2 = makeScene(n);
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            cv::circle(m, cv::Point(300, 300), 7, cv::Scalar(250), -1);
            cv::line(m, cv::Point(270, 250), cv::Point(276, 315), cv::Scalar(250), 7);
            s2.frames[size_t(i)] = m;
        }
        const DtlBall none = dtlFindBall(srcOf(s2), addr, post, s2.an, W, H);
        std::printf("       permanent-only: found=%d — %s\n", int(none.found),
                    none.reason.toUtf8().constData());
        check(!none.found, "T8: a permanent white disc and an elongated stick yield NO ball, with a reason");
    }

    // ── T9 determinism ──────────────────────────────────────────────────────
    std::printf("\n=== T9: two solves of one scene are identical ===\n");
    {
        Scene s = makeScene(16);
        for (int i = 0; i < 16; ++i) {
            cv::Mat m = baseScene();
            drawLimb(m, kArm, 130.0);
            drawStripedShaft(m, kClub, 15, 140);
            s.frames[size_t(i)] = m;
        }
        const cv::Point2d elbow{ GX + 80 * std::cos(kArm * kPi / 180.0),
                                 GY + 80 * std::sin(kArm * kPi / 180.0) };
        for (int i = 0; i < 16; ++i) { s.an.leadElbow[size_t(i)] = elbow; s.an.trailElbow[size_t(i)] = elbow; }
        fillWitnessFlat(s, kRhoLong, kThetaLongDeg, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlShaftConfig cfg = testCfg();
        const DtlSolveState a = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6, {}, 1120.0, cfg, nullptr);
        const DtlSolveState b = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6, {}, 1120.0, cfg, nullptr);
        check(a.thetaDeg == b.thetaDeg && a.solved == b.solved && a.rhoPred == b.rhoPred
              && a.EV == b.EV && a.SUP == b.SUP && a.REND == b.REND,
              "T9: θ, solved, ρ̂_D and every evidence row compare equal");
    }

    // ── T10 the ball gate at the still club ─────────────────────────────────
    std::printf("\n=== T10: at the still club the ball gate refuses the leg and takes the shaft ===\n");
    {
        // The measured address failure, planted: the trail leg is a long, wide,
        // high-contrast ray out of the hands and the truth is that the head is at
        // the ball. Only the TIMING of "the club is still" is inherited from
        // face-on; grip→ball is DTL's own measurement.
        //
        // The control cannot be "the leg out-scores the shaft", because in a
        // synthetic it cannot: under the per-channel percentile normalisation both
        // top bins clip to 1.0 and the evidence TIES (T1 records the same thing),
        // so which one a solve takes is the DP's tie-break and not a measurement.
        // The control is therefore the scene with NO shaft at all — the leg is then
        // the only line there is and wins outright — and the question asked of the
        // gate is whether it still refuses it.
        const int n = 80;
        const double kLeg = 111.8;        // grip (160,160) → the trail ankle (120,260)
        const cv::Point ball = ptAt(GX, GY, kClub, 160.0);
        const auto build = [&](bool withShaft) {
            Scene sc = makeScene(n);
            for (int i = 0; i < n; ++i) {
                cv::Mat m = baseScene();
                drawLimb(m, kLeg, 150);              // the leg: long, wide, uniformly bright
                if (withShaft) drawFaintShaft(m, kClub, 15, 120);
                if (i < 50) cv::circle(m, ball, 7, cv::Scalar(250), -1);
                else        cv::circle(m, ball, 10, cv::Scalar(110), -1);
                sc.frames[size_t(i)] = m;
            }
            // θ_F leaves address from frame 31 so the still-club hold releases and
            // frame 60 is an ordinary mid-band frame — which is what the last
            // assertion below is about.
            fillWitnessTakeaway(sc, kRhoLong, kThetaLongDeg, FoTier::Ray,
                                kT0 + int64_t(30) * kDt, kT0 + int64_t(50) * kDt, 31);
            return sc;
        };
        Scene withClub = build(true), legOnly = build(false);

        // D2 is switched out of all three runs so the ONLY thing that moves is
        // wGate. D6's 4-deep well stays on throughout — "the well loses and the
        // gate does not" is the finding, and a control that removed both would be
        // measuring the pair.
        DtlShaftConfig off = testCfg(), on = testCfg();
        off.arm.wArm = 0.0; off.ball.wGate = 0.0;
        on.arm.wArm  = 0.0;
        const DtlSolveState cOff = dtlSolve(srcOf(legOnly), legOnly.tUs, legOnly.an, &legOnly.wit,
                                            W, H, 150.6, {}, 1120.0, off, nullptr);
        const DtlSolveState cOn  = dtlSolve(srcOf(legOnly), legOnly.tUs, legOnly.an, &legOnly.wit,
                                            W, H, 150.6, {}, 1120.0, on, nullptr);
        const DtlSolveState a    = dtlSolve(srcOf(withClub), withClub.tUs, withClub.an, &withClub.wit,
                                            W, H, 150.6, {}, 1120.0, on, nullptr);
        std::printf("       ball found=%d at (%.0f, %.0f), θ_ball %.1f° | leg-only: gate off %.1f°, "
                    "gate on %.1f° | with the club, gate on: %.1f° (club %.1f°, leg %.1f°)\n",
                    int(a.ball.found), a.ball.x, a.ball.y, a.thetaBallDeg[10],
                    cOff.thetaDeg[10], cOn.thetaDeg[10], a.thetaDeg[10], kClub, kLeg);
        check(a.ball.found && a.ballGate[10] && std::isfinite(a.thetaBallDeg[10]),
              "T10: frame 10 is a still-club frame with a DTL ball to point at");
        std::printf("       leg-only EV: at the leg %.2f, at the ball direction %.2f  "
                    "(gate-off solve %.1f°)\n",
                    double(cOff.EV[10][size_t(int(kLeg))]), double(cOff.EV[10][size_t(int(kClub))]),
                    cOff.thetaDeg[10]);
        // The control says what this scene can support and no more. The leg is a
        // FULL-STRENGTH candidate — that is the thing the gate has to out-argue,
        // and it is the whole difficulty at address. What the synthetic cannot
        // show is the leg WINNING with the gate off: D6's own well already reaches
        // the ball direction here, because there is no competing line to normalise
        // against. On the real dev six the leg does win, by 60–82°, and that
        // measurement lives in the run report rather than in a planted scene.
        check(cOff.EV[10][size_t(int(kLeg))] >= 0.95,
              "T10 control: the leg is a full-strength candidate, not a weak one");
        check(dWrap(cOn.thetaDeg[10], kLeg) > 20.0
              && dWrap(cOn.thetaDeg[10], a.thetaBallDeg[10]) <= testCfg().ball.gateDeg,
              "T10: with the gate on, the same leg is refused and the solve stays inside the gate");
        check(dWrap(a.thetaDeg[10], kClub) <= 8.0,
              "T10: and with the shaft present the solve is the shaft");
        check(!a.ballGate[60], "T10: a mid-band frame is NOT gated — the ball is not a global prior");
    }

    // ── T11 no ball ⇒ the still-club frames are not solved at all ───────────
    std::printf("\n=== T11: the same scene with NO ball leaves the still-club frames unsolved ===\n");
    {
        const int n = 80;
        const double kLeg = 111.8;
        Scene s = makeScene(n);
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawLimb(m, kLeg, 150);
            drawFaintShaft(m, kClub, 15, 120);
            s.frames[size_t(i)] = m;
        }
        const int64_t p1 = kT0 + int64_t(30) * kDt, imp = kT0 + int64_t(50) * kDt;
        // θ_F leaves address from frame 31, so the still-club hold releases and
        // frame 40 is an ordinary mid-band frame — the control below.
        fillWitnessTakeaway(s, kRhoLong, kThetaLongDeg, FoTier::Ray, p1, imp, 31);
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, testCfg(), nullptr);
        std::printf("       ball found=%d (%s); frame 10 solved=%d reason=\"%s\"\n",
                    int(st.ball.found), st.ball.reason.toUtf8().constData(),
                    int(st.solved[10]), st.reason[10].toUtf8().constData());
        check(!st.ball.found, "T11: there is no ball");
        check(!st.solved[10] && !st.sighted[10],
              "T11: so the still club is not solved BLIND — the leg is not published as a shaft");
        check(st.reason[10] == QStringLiteral("no ball witness at address"),
              "T11: and the absence says exactly which witness was missing");
        check(st.solved[40], "T11 control: the mid-band frames are unaffected");
    }

    // ── T12 the limb veto beyond the forearms ───────────────────────────────
    std::printf("\n=== T12: D2 fires for a hip, a knee and an ankle, and names which ===\n");
    {
        // The joints are makeScene's: hips (100,150)/(120,150), knees
        // (100,210)/(120,210), ankles (100,260)/(120,260), grip (160,160). From the
        // grip that is 189.5° to the left hip, 128.7° to the right knee and 111.8°
        // to the right ankle. The right hip is 41 px away — inside minJointPx — so
        // it is deliberately NOT a veto direction: at that range grip→J is pose
        // jitter.
        const int n = 12;
        Scene s = makeScene(n);
        for (int i = 0; i < n; ++i) {
            cv::Mat m = baseScene();
            drawStripedShaft(m, kClub, 15, 140);
            s.frames[size_t(i)] = m;
        }
        fillWitnessFlat(s, kRhoLong, kThetaLongDeg, FoTier::Ray, kNoStillP1, kNoStillImp);
        const DtlSolveState st = dtlSolve(srcOf(s), s.tUs, s.an, &s.wit, W, H, 150.6,
                                          {}, 1120.0, testCfg(), nullptr);
        const int hipBin = 190, kneeBin = 129, ankBin = 112;
        std::printf("       veto/joint at hip %d:%d/%s  knee %d:%d/%s  ankle %d:%d/%s  club %d:%d\n",
                    hipBin,  int(st.ARMVETO[0][hipBin]),  dtlLimbName(st.ARMJOINT[0][hipBin]),
                    kneeBin, int(st.ARMVETO[0][kneeBin]), dtlLimbName(st.ARMJOINT[0][kneeBin]),
                    ankBin,  int(st.ARMVETO[0][ankBin]),  dtlLimbName(st.ARMJOINT[0][ankBin]),
                    int(kClub), int(st.ARMVETO[0][size_t(int(kClub))]));
        check(st.ARMVETO[0][hipBin]  && st.ARMJOINT[0][hipBin]  == static_cast<signed char>(DtlLimb::LeftHip),
              "T12: the ray down the hip line is vetoed, and the hip is named");
        // Bin 129 is inside the veto angle of BOTH knees (128.7° and 140.2° from
        // the grip); the first slot to fire is the one named, and which of the two
        // that is is bookkeeping, not a finding.
        check(st.ARMVETO[0][kneeBin]
              && (st.ARMJOINT[0][kneeBin] == static_cast<signed char>(DtlLimb::LeftKnee)
                  || st.ARMJOINT[0][kneeBin] == static_cast<signed char>(DtlLimb::RightKnee)),
              "T12: the ray down the knee line is vetoed, and a knee is named");
        check(st.ARMVETO[0][ankBin]
              && (st.ARMJOINT[0][ankBin] == static_cast<signed char>(DtlLimb::LeftAnkle)
                  || st.ARMJOINT[0][ankBin] == static_cast<signed char>(DtlLimb::RightAnkle)),
              "T12: the ray down the leg to the feet is vetoed, and an ankle is named");
        check(!st.ARMVETO[0][size_t(int(kClub))],
              "T12: and the club's own direction is not — the veto is a lateral test, not a sector");
        check(dWrap(st.thetaDeg[0], kClub) <= 6.0, "T12: so the solve is still the club");
    }

    // ── T13 the still-club hold ends when the club leaves, not at P1 + 30 ms ─
    std::printf("\n=== T13: the ball gate holds while face-on says the club is still ===\n");
    {
        // MEASURED on swings 0005 and 0007: one frame after the 30 ms window closes
        // the solve jumps to the trouser/shin edge at 103–132°, while band truth has
        // the club within 5° of the ball line for ~190 ms — a slow one-piece
        // takeaway. P1 + 30 ms is face-on's instant for "the takeaway has begun",
        // which is a claim about the HANDS. The hold wants "the head has left the
        // ball", and the face-on witness is the thing that can say so.
        //
        // The scene is T10's: the trail leg is the only line there is, so with the
        // gate off the solve takes it, and the question is whether the gate is still
        // on at a frame 66 ms past P1.
        const int n = 100;
        const double kLeg = 111.8;
        const cv::Point ball = ptAt(GX, GY, kClub, 160.0);
        const int p1f = 30, impf = 70;
        const auto build = [&](bool truncateWitness) {
            Scene sc = makeScene(n);
            for (int i = 0; i < n; ++i) {
                cv::Mat m = baseScene();
                drawLimb(m, kLeg, 150);
                if (i < impf) cv::circle(m, ball, 7,  cv::Scalar(250), -1);
                else          cv::circle(m, ball, 10, cv::Scalar(110), -1);
                sc.frames[size_t(i)] = m;
            }
            // θ_F sits still through the takeaway and then swings away at 3°/frame.
            // Nothing about the hold is a DTL measurement: it is the witness's own
            // statement that the club has not moved yet.
            fillWitnessTakeaway(sc, kRhoLong, kThetaLongDeg, FoTier::Ray,
                                kT0 + int64_t(p1f) * kDt, kT0 + int64_t(impf) * kDt, 51);
            if (truncateWitness) {
                // The witness stops two frames after P1. Every later frame is
                // out of range, so at() is not ok and the hold has nothing to
                // stand on — which must leave the OLD rule, not a hold on nothing.
                const size_t keep = size_t(p1f) + 2;
                sc.wit.tUs.resize(keep);
                sc.wit.thetaUnwrapRad.resize(keep);
                sc.wit.thetaDotRadS.resize(keep);
                sc.wit.rhoF.resize(keep);
                sc.wit.gripYPx.resize(keep);
                sc.wit.tier.resize(keep);
                sc.wit.phase.resize(keep);
            }
            return sc;
        };
        Scene held = build(false), cut = build(true);

        // stillMaxUs = 0 IS the as-built rule: the hold cannot extend past P1, so
        // the gate is exactly "before P1 + 30 ms". One variable moves between the
        // two runs and it is the hold.
        DtlShaftConfig oldCfg = testCfg(), newCfg = testCfg();
        oldCfg.arm.wArm = 0.0; newCfg.arm.wArm = 0.0;   // D2 out, as T10: only the gate moves
        oldCfg.ball.stillMaxUs = 0;
        const DtlSolveState a = dtlSolve(srcOf(held), held.tUs, held.an, &held.wit,
                                         W, H, 150.6, {}, 1120.0, oldCfg, nullptr);
        const DtlSolveState b = dtlSolve(srcOf(held), held.tUs, held.an, &held.wit,
                                         W, H, 150.6, {}, 1120.0, newCfg, nullptr);
        std::printf("       frame 40 (P1 + 66 ms): gate old=%d new=%d | solve old %.1f° new %.1f° "
                    "(ball %.1f°, leg %.1f°)\n",
                    int(a.ballGate[40]), int(b.ballGate[40]), a.thetaDeg[40], b.thetaDeg[40],
                    b.thetaBallDeg[40], kLeg);
        check(!a.ballGate[40], "T13 control: the as-built rule had already let go at P1 + 66 ms");
        check(b.ballGate[40] && std::isfinite(b.thetaBallDeg[40]),
              "T13: the hold keeps frame 40 gated, with a DTL ball to point at");
        check(dWrap(b.thetaDeg[40], b.thetaBallDeg[40]) <= newCfg.ball.gateDeg,
              "T13: and the held frame's solve stays on the ball line");
        // What the synthetic cannot show, said out loud rather than asserted away
        // (T10 records the same limit): with the gate off the DP does not run to
        // the leg here, because the band is ONE Viterbi and the smoothness term
        // carries the gated address frames' 50° forward over a scene where the leg
        // and the ball direction tie on evidence. On the dev six it does run —
        // swings 0005 and 0007, 103–132° within two frames of the window closing,
        // against band truth of 51–57° — and that measurement lives in the run
        // report, where it was made, rather than in a planted scene.
        std::printf("       (with the gate off the solve does not reach the leg in a synthetic: the\n"
                    "        band's own smoothness carries the address frames forward. The 103–132°\n"
                    "        jump is measured on swings 0005 and 0007, not planted here.)\n");

        // It RELEASES. θ_F moves 3°/frame from frame 51, so |Δθ_F| passes 10° at
        // frame 54 — and the hold is a hold, not a new window: nothing after that
        // is gated.
        std::printf("       gate by frame: 53=%d 54=%d 60=%d | θ_F(53) − θ_F(P1) = %.1f°, "
                    "θ_F(54) − θ_F(P1) = %.1f°\n",
                    int(b.ballGate[53]), int(b.ballGate[54]), int(b.ballGate[60]),
                    3.0 * 3.0, 4.0 * 3.0);
        check(b.ballGate[53] && !b.ballGate[54],
              "T13: the hold releases on the first frame face-on says the club has moved");
        check(!b.ballGate[60], "T13: and does not come back");

        // The backstop, and the fallback. Neither is caution: a swing whose face-on
        // θ happens to sit still for another reason must not hold the gate open for
        // a second, and a frame with no witness must not inherit a hold nobody saw.
        DtlShaftConfig capped = newCfg;
        capped.ball.stillMaxUs = 60000;
        const DtlSolveState c = dtlSolve(srcOf(held), held.tUs, held.an, &held.wit,
                                         W, H, 150.6, {}, 1120.0, capped, nullptr);
        std::printf("       stillMaxUs 60 ms: gate 35 (P1+33 ms)=%d, 40 (P1+66 ms)=%d\n",
                    int(c.ballGate[35]), int(c.ballGate[40]));
        check(c.ballGate[35] && !c.ballGate[40],
              "T13: stillMaxUs caps the hold however still θ_F is");

        const DtlSolveState d = dtlSolve(srcOf(cut), cut.tUs, cut.an, &cut.wit,
                                         W, H, 150.6, {}, 1120.0, newCfg, nullptr);
        std::printf("       witness truncated at P1 + 2 frames: gate 34 (P1+27 ms)=%d, "
                    "35 (P1+33 ms)=%d\n", int(d.ballGate[34]), int(d.ballGate[35]));
        check(d.ballGate[34] && !d.ballGate[35],
              "T13: with no witness to hold on, the gate is exactly the P1 + 30 ms rule");
    }

    // ── T14 D1's reverse ray, and this view's standing excuse ───────────────
    std::printf("\n=== T14: a reverse ray that runs up an arm costs nothing; one into free space does ===\n");
    {
        // Face-on's C1 assumes FREE SPACE behind the butt. Down the line there is
        // none: the lead arm is near-collinear with the shaft at address and
        // impact and the forearms are at P3/P5, on the OPPOSITE side of the grip.
        // So the reverse ray of a CORRECT direction runs up the golfer's own arm,
        // and a D1 that charges for it makes the club cost exactly what an
        // evidence-free direction costs (wRev 10 = wE2 10). The scene plants the
        // club and a bright limb on its reverse, and moves only the ARM.
        const double kRev = kClub + 180.0;
        const auto build = [&](double shoulderDirDeg, double elbowDirDeg, bool reverseLine) {
            Scene s = makeScene(12);
            for (int i = 0; i < 12; ++i) {
                cv::Mat m = baseScene();
                if (reverseLine) drawLimb(m, kRev, 130.0);  // a bright line out the back of the grip
                drawStripedShaft(m, kClub, 15, 140);        // …and the club
                s.frames[size_t(i)] = m;
            }
            const cv::Point2d elb{ GX + 100 * std::cos(elbowDirDeg * kPi / 180.0),
                                   GY + 100 * std::sin(elbowDirDeg * kPi / 180.0) };
            const cv::Point2d sho{ GX + 140 * std::cos(shoulderDirDeg * kPi / 180.0),
                                   GY + 140 * std::sin(shoulderDirDeg * kPi / 180.0) };
            for (int i = 0; i < 12; ++i) {
                s.an.leadElbow[size_t(i)]  = elb;
                s.an.trailElbow[size_t(i)] = elb;
                s.an.joints[size_t(i)][0]  = sho;
                s.an.joints[size_t(i)][1]  = sho;
            }
            fillWitnessFlat(s, kRhoLong, kThetaLongDeg, FoTier::Ray, kNoStillP1, kNoStillImp);
            return s;
        };
        // The arm ON the reverse — the real geometry at address and impact.
        Scene onArm = build(kRev, kRev, true);
        const DtlSolveState a = dtlSolve(srcOf(onArm), onArm.tUs, onArm.an, &onArm.wit,
                                         W, H, 150.6, {}, 1120.0, testCfg(), nullptr);
        // The same bright reverse line with NO arm behind it — free space, which
        // is the condition D1 was written for.
        Scene freeSp = build(kRev - 90.0, kRev - 90.0, true);
        const DtlSolveState b = dtlSolve(srcOf(freeSp), freeSp.tUs, freeSp.an, &freeSp.wit,
                                         W, H, 150.6, {}, 1120.0, testCfg(), nullptr);
        // …and the same arm placement with NO reverse line at all, so the third
        // row says the reverse line is what refused the club in the second.
        Scene noRev = build(kRev - 90.0, kRev - 90.0, false);
        const DtlSolveState c = dtlSolve(srcOf(noRev), noRev.tUs, noRev.an, &noRev.wit,
                                         W, H, 150.6, {}, 1120.0, testCfg(), nullptr);
        std::printf("       reverse line + arm on it   : solved θ = %.1f° (club %.1f°)\n",
                    a.thetaDeg[0], kClub);
        std::printf("       reverse line, arm elsewhere: solved θ = %.1f°\n", b.thetaDeg[0]);
        std::printf("       no reverse line at all     : solved θ = %.1f°\n", c.thetaDeg[0]);
        check(solvedCount(a) == 12 && dWrap(a.thetaDeg[0], kClub) <= 6.0,
              "T14: with the arm along the reverse the club still wins the emission");
        check(dWrap(b.thetaDeg[0], kClub) > 6.0,
              "T14 control: with the reverse in free space D1 still refuses to prefer the club");
        check(dWrap(c.thetaDeg[0], kClub) <= 6.0,
              "T14 control: and the reverse LINE is what refused it — remove it and the club wins");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
