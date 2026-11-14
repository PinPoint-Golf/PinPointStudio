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

// THE RECORDING CONTRACT (docs/developer/information_model.md §2): same Recording + same effective
// context → same job, for live capture and re-analysis alike; a corrected club re-analyses with the
// club really hit; an undeclared club stays undeclared.
//
//   Test 1  live == reloaded. swing_window_parity_test's synthetic swing (one face-on camera, one
//           IMU bound as lead forearm + lead hand, a Wrist session, impact 50 ms in) as the live
//           capture holds it (ring clock, t0 = the window start) and as a store gives it back
//           (window-relative, t0 = 0, the raw sidecar backing). The two jobs are equal field by
//           field once their times are taken relative to t0Us; only analysis.path differs (origin,
//           frame backing), as the design says.
//           THE ROUND TRIP IS IN MEMORY: the analysisContext block's writer and reader are step
//           1.2's (recording_store_test takes the same Recording through the disk).
//   Test 2  camera roles from the one perspective mapping (face-on first, last impact, first DTL).
//   Test 3  the effective club: a correction → its bag geometry and standard length, no prior;
//           a correction absent from the bag → no geometry, the standard length for the club set;
//           a correction naming the captured club → the recorded club, prior kept.
//   Test 3b the declared club (the popover's review.club; Mark 14 Nov 2026): it sets the STANDARD
//           length, as today's re-analysis does, the geometry staying the captured club's; the
//           board's override wins over it; neither → the captured club's recorded standard length.
//   Test 4  undeclared stays undeclared — never the "DRIVER" stub.
//   Test 5  the ball rule (D5): a valid empty-mat baseline → the recorded stream is not passed.
//   Test 6  the run options, and nothing else, carry the run's knobs; markerSourceId is never set.
//   Test 7  the code mappings (perspective, handedness).
//
//   cmake --build build/tests --target recording_contract_test -j 6
//   ctest --test-dir build/tests -R '^recording_contract_test$' --output-on-failure

#include "../analysis_job_builder.h"
#include "../effective_context.h"
#include "../../Recording/recording.h"
#include "../../Core/club_standards.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::analysis;
namespace rec = pinpoint::recording;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

// ── swing_window_parity_test's synthetic swing ────────────────────────────────────────────────
constexpr int64_t kT0       = 1'000'000;
constexpr int64_t kFrameDt  = 10'000;
constexpr int     kFrames   = 12;
constexpr int64_t kImuDt    = 5'000;
constexpr int     kImu      = 30;
constexpr int64_t kImpact   = kT0 + 50'000;
// captureSwingWindow(kT0 - 1, last + 1): the window starts one µs before the first sample.
constexpr int64_t kWindowStart = kT0 - 1;
constexpr int64_t kWindowEnd   = std::max(kT0 + (kImu - 1) * kImuDt, kT0 + (kFrames - 1) * kFrameDt) + 1;
constexpr SourceId kCam = 1;   // registration order in the fixture: the camera, then the IMU
constexpr SourceId kImuId = 2;

rec::ClubGeometry geometry(double lengthMm, std::vector<double> bands, double hosel = 0.0)
{
    rec::ClubGeometry g;
    g.lengthMm      = lengthMm;
    g.lengthKnown   = lengthMm > 0.0;
    g.shaftType     = QStringLiteral("steel");
    g.bandCentersMm = std::move(bands);
    g.hoselFromButtMm = hosel;
    g.shaftLengthMm = hosel > 0.0 ? hosel - 265.0 : 0.0;
    g.handsEndMm    = hosel > 0.0 ? 180.0 : 0.0;
    return g;
}

rec::AnalysisContext syntheticContext()
{
    rec::AnalysisContext c;
    c.handedness = rec::Handedness::Right;
    c.heightM    = 1.80;
    c.clubSet    = ClubSet::Men;
    c.club.name  = QStringLiteral("7 IRON");
    c.club.geometry = geometry(940.0, { 300.0, 500.0, 700.0 }, 880.0);
    c.club.lengthPrior.px = 310.0; c.club.lengthPrior.varPx = 25.0; c.club.lengthPrior.n = 6;
    c.club.standardLengthMm = standardClub(c.club.name, c.clubSet, c.heightM).lengthMm;
    c.bag.insert(QStringLiteral("7 IRON"), c.club.geometry);
    c.bag.insert(QStringLiteral("9 IRON"), geometry(914.0, { 290.0, 490.0 }, 855.0));
    c.bag.insert(QStringLiteral("DRIVER"), geometry(1156.0, {}, 1080.0));
    c.bag.insert(QStringLiteral("PITCHING WEDGE"), geometry(0.0, { 280.0 }));   // never measured
    return c;
}

// The synthetic swing as the live capture holds it at the freeze: every time window-relative,
// t0Us the window's start on the ring clock.
rec::Recording liveRecording()
{
    rec::Recording r;
    r.t0Us = kWindowStart;
    r.sources.cameras.push_back({ kCam, rec::Perspective::FaceOn, false, true, QString() });
    r.sources.imuLanes = { kImuId };
    r.capture.sessionType = 1;                       // Wrist
    r.capture.shotSource  = 4;
    r.capture.impactUs    = kImpact - kWindowStart;
    r.capture.motionCaptureQuality = QStringLiteral("High");

    // A face-on ball stream: found from frame 2, launched at frame 6.
    r.ball.track.camera = kCam;
    for (int i = 0; i < kFrames; ++i) {
        BallSample2D s;
        s.t_us   = kT0 + i * kFrameDt - kWindowStart;
        s.found  = i >= 2 && i < 6;
        s.center = QPointF(0.5 + 0.001 * i, 0.8);
        s.radiusNorm = 0.01f;
        s.conf   = s.found ? 0.9f : 0.0f;
        r.ball.track.frames.push_back(s);
    }
    r.ball.track.launchTUs    = kT0 + 6 * kFrameDt - kWindowStart;
    r.ball.track.launchCenter = QPointF(0.505, 0.8);
    r.ball.searchRoi = QRectF(0.3, 0.6, 0.4, 0.3);

    ImuSegmentBinding fore, hand;
    fore.source = kImuId; fore.role = SegmentRole::LeadForearm; fore.calibrated = true;
    hand.source = kImuId; hand.role = SegmentRole::LeadHand;    hand.calibrated = true;
    fore.alignA = QQuaternion(0.9f, 0.1f, 0.2f, 0.3f).normalized();
    hand.mountM = QQuaternion(0.8f, -0.1f, 0.4f, 0.1f).normalized();
    fore.calibratedAtUtc = QStringLiteral("2026-11-14T09:00:00.000Z");
    fore.calibAgeSec = 600.0;
    r.imuBindings = { fore, hand };

    r.integrity.hostOrientationFilter = QStringLiteral("madgwick");
    r.integrity.windowStartUs = 0;
    r.integrity.windowEndUs   = kWindowEnd - kWindowStart;
    r.context = syntheticContext();
    return r;
}

// What a store gives back for that Recording: the same facts, the loaded window's own clock (t0 0,
// entries window-relative), frames from the raw sidecar. In memory: see the file header.
rec::Recording persistedAndReloaded(const rec::Recording &live)
{
    rec::Recording r = live;
    r.window = nullptr;
    r.t0Us   = 0;
    for (rec::CameraStream &c : r.sources.cameras)
        c.frames = QStringLiteral("raw");
    return r;
}

// ── Field-by-field job comparison, times relative to each job's t0 ───────────────────────────
QString firstDiff(const ShotAnalysisJob &a, int64_t t0a, const ShotAnalysisJob &b, int64_t t0b)
{
#define DIFF(cond, name) do { if (!(cond)) return QStringLiteral(name); } while (0)
    DIFF(a.sessionType == b.sessionType, "sessionType");
    DIFF(a.shotSource == b.shotSource, "shotSource");
    DIFF((a.impactUs < 0 && b.impactUs < 0) || a.impactUs - t0a == b.impactUs - t0b, "impactUs");
    DIFF(a.cameraSources == b.cameraSources, "cameraSources");
    DIFF(a.faceOnCameraCount == b.faceOnCameraCount, "faceOnCameraCount");
    DIFF(a.impactSource == b.impactSource, "impactSource");
    DIFF(a.dtlSource == b.dtlSource, "dtlSource");
    DIFF(a.imuSources == b.imuSources, "imuSources");
    DIFF(a.markerSourceId == b.markerSourceId, "markerSourceId");
    DIFF(a.clubLengthM == b.clubLengthM, "clubLengthM");
    DIFF(a.clubLengthKnown == b.clubLengthKnown, "clubLengthKnown");
    DIFF(a.standardClubLengthM == b.standardClubLengthM, "standardClubLengthM");
    DIFF(a.athleteHeightM == b.athleteHeightM, "athleteHeightM");
    DIFF(a.skeletonCalib.has_value() == b.skeletonCalib.has_value(), "skeletonCalib");
    DIFF(a.bandCentersMm == b.bandCentersMm, "bandCentersMm");
    DIFF(a.shaftType == b.shaftType, "shaftType");
    DIFF(a.hoselFromButtMm == b.hoselFromButtMm, "hoselFromButtMm");
    DIFF(a.shaftLengthMm == b.shaftLengthMm, "shaftLengthMm");
    DIFF(a.handsEndMm == b.handsEndMm, "handsEndMm");
    DIFF(a.clubName == b.clubName, "clubName");
    DIFF(a.priorClubLenPx == b.priorClubLenPx, "priorClubLenPx");
    DIFF(a.priorClubLenVarPx == b.priorClubLenVarPx, "priorClubLenVarPx");
    DIFF(a.priorClubLenN == b.priorClubLenN, "priorClubLenN");
    DIFF(a.imuBindings.size() == b.imuBindings.size(), "imuBindings.size");
    for (size_t i = 0; i < a.imuBindings.size(); ++i) {
        const ImuSegmentBinding &x = a.imuBindings[i], &y = b.imuBindings[i];
        DIFF(x.source == y.source && x.role == y.role && x.alignA == y.alignA && x.mountM == y.mountM
             && x.anatCalibrated == y.anatCalibrated && x.calibrated == y.calibrated
             && x.mountDeviationDeg == y.mountDeviationDeg && x.mountGravityErrorDeg == y.mountGravityErrorDeg
             && x.calibratedAtUtc == y.calibratedAtUtc && x.calibAgeSec == y.calibAgeSec
             && x.trunkCalib.present == y.trunkCalib.present && x.hackMotion == y.hackMotion,
             "imuBindings[i]");
    }
    DIFF(a.handedness == b.handedness, "handedness");
    DIFF(a.swingDir == b.swingDir, "swingDir");
    DIFF(a.motionCaptureQuality == b.motionCaptureQuality, "motionCaptureQuality");
    DIFF(a.poseTrackPath == b.poseTrackPath, "poseTrackPath");
    DIFF(a.posePreloaded.frames.size() == b.posePreloaded.frames.size(), "posePreloaded");
    DIFF(a.posePreloadedScope == b.posePreloadedScope, "posePreloadedScope");
    DIFF(a.ballPreloaded.frames.size() == b.ballPreloaded.frames.size(), "ballPreloaded");
    DIFF(a.poseDtlPreloaded.frames.size() == b.poseDtlPreloaded.frames.size(), "poseDtlPreloaded");
    DIFF(a.poseDtlTrackPath == b.poseDtlTrackPath, "poseDtlTrackPath");
    DIFF(a.dtlCameraCalib.calibrated == b.dtlCameraCalib.calibrated, "dtlCameraCalib");
    DIFF(a.shaftPreloaded.samples.size() == b.shaftPreloaded.samples.size(), "shaftPreloaded");
    DIFF(a.ladderPreloaded.has_value() == b.ladderPreloaded.has_value(), "ladderPreloaded");
    DIFF(a.ballTrack.camera == b.ballTrack.camera, "ballTrack.camera");
    DIFF(a.ballTrack.frames.size() == b.ballTrack.frames.size(), "ballTrack.frames.size");
    for (size_t i = 0; i < a.ballTrack.frames.size(); ++i) {
        const BallSample2D &x = a.ballTrack.frames[i], &y = b.ballTrack.frames[i];
        DIFF(x.t_us - t0a == y.t_us - t0b && x.found == y.found && x.center == y.center
             && x.radiusNorm == y.radiusNorm && x.conf == y.conf, "ballTrack.frames[i]");
    }
    DIFF((a.ballTrack.launchTUs < 0 && b.ballTrack.launchTUs < 0)
         || a.ballTrack.launchTUs - t0a == b.ballTrack.launchTUs - t0b, "ballTrack.launchTUs");
    DIFF(a.ballTrack.launchCenter == b.ballTrack.launchCenter, "ballTrack.launchCenter");
    DIFF(a.ballTrackPath == b.ballTrackPath, "ballTrackPath");
    DIFF(a.ballSearchRoi == b.ballSearchRoi, "ballSearchRoi");
    DIFF(a.ballBaseline.path == b.ballBaseline.path && a.ballBaseline.w == b.ballBaseline.w
         && a.ballBaseline.h == b.ballBaseline.h && a.ballBaseline.roi == b.ballBaseline.roi, "ballBaseline");
    DIFF(a.tuningOverrides == b.tuningOverrides, "tuningOverrides");
    DIFF(a.fullWindow == b.fullWindow, "fullWindow");
#undef DIFF
    return QString();
}

RunOptions options(const char *origin)
{
    RunOptions o;
    o.origin = QString::fromLatin1(origin);
    return o;
}

// ── Test 1 ────────────────────────────────────────────────────────────────────────────────────
void testLiveEqualsReloaded()
{
    std::printf("[Test 1] live Recording -> job == persisted-and-reloaded Recording -> job\n");
    const rec::Recording live   = liveRecording();
    const rec::Recording loaded = persistedAndReloaded(live);
    const EffectiveContext effLive   = effectiveContext(live.context, ClubReview{});
    const EffectiveContext effLoaded = effectiveContext(loaded.context, ClubReview{});

    const ShotAnalysisJob jLive   = buildAnalysisJob(live, effLive, options("live"));
    const ShotAnalysisJob jLoaded = buildAnalysisJob(loaded, effLoaded, options("reanalysis"));

    const QString diff = firstDiff(jLive, live.t0Us, jLoaded, loaded.t0Us);
    if (!diff.isEmpty()) std::printf("    first difference: %s\n", diff.toUtf8().constData());
    check(diff.isEmpty(), "every job field equal, times relative to t0Us");

    // The job runs in the window's own clock: the ring clock live, the relative clock loaded.
    check(jLive.impactUs == kImpact, "live job: impact on the ring clock");
    check(jLoaded.impactUs == kImpact - kWindowStart, "reloaded job: impact window-relative");
    check(!jLive.ballTrack.frames.empty() && jLive.ballTrack.frames.front().t_us == kT0,
          "live job: ball stream on the ring clock");
    check(jLive.ballTrack.launchTUs == kT0 + 6 * kFrameDt, "live job: ball launch on the ring clock");

    // The facts themselves.
    check(jLive.sessionType == 1 && jLive.shotSource == 4, "session type and shot source recorded");
    check(jLive.cameraSources == std::vector<SourceId>{ kCam } && jLive.faceOnCameraCount == 1,
          "face-on camera");
    check(jLive.imuSources == std::vector<SourceId>{ kImuId } && jLive.imuBindings.size() == 2,
          "IMU lane and its two bindings");
    check(jLive.handedness == 1 && jLive.athleteHeightM == 1.80, "handedness and height from the context");
    check(jLive.clubName == QStringLiteral("7 IRON") && jLive.clubLengthKnown
              && std::abs(jLive.clubLengthM - 0.940) < 1e-12,
          "the captured club and its recorded length");
    check(jLive.bandCentersMm == std::vector<double>{ 300.0, 500.0, 700.0 } && jLive.hoselFromButtMm == 880.0,
          "the captured club's geometry");
    check(jLive.priorClubLenN == 6 && jLive.priorClubLenPx == 310.0 && jLive.priorClubLenVarPx == 25.0,
          "the recorded length prior");
    check(jLive.standardClubLengthM == 0.0, "no standard length when the length is recorded");
    check(jLive.motionCaptureQuality == QStringLiteral("High"), "motion-capture quality recorded");

    // Only analysis.path differs, and only as the design says.
    check(jLive.path.origin == QStringLiteral("live") && jLoaded.path.origin == QStringLiteral("reanalysis"),
          "path.origin from the run options");
    check(jLive.path.framesFaceOn.isEmpty() && jLoaded.path.framesFaceOn == QStringLiteral("raw"),
          "path.framesFaceOn from the stream's backing");
}

// ── Test 2 ────────────────────────────────────────────────────────────────────────────────────
void testCameraRoles()
{
    std::printf("[Test 2] camera roles from the one perspective mapping\n");
    rec::Recording r;
    r.sources.cameras = {
        { 10, rec::Perspective::DownTheLine, false, false, QStringLiteral("mp4") },
        { 11, rec::Perspective::FaceOn,      false, false, QStringLiteral("raw") },
        { 12, rec::Perspective::Impact,      false, false, QStringLiteral("mp4") },
        { 13, rec::Perspective::FaceOn,      false, false, QStringLiteral("mp4") },
        { 14, rec::Perspective::DownTheLine, false, false, QStringLiteral("raw") },
        { 15, rec::Perspective::Impact,      false, false, QStringLiteral("mp4") },
    };
    const ShotAnalysisJob j = buildAnalysisJob(r, effectiveContext(r.context, ClubReview{}), options("replay"));
    check(j.cameraSources == (std::vector<SourceId>{ 13, 11, 10, 12, 14, 15 }),
          "face-on streams first (each at the front), the rest in order");
    check(j.faceOnCameraCount == 2, "faceOnCameraCount counts the face-on prefix");
    check(j.impactSource == 15, "the last Impact stream is the impact camera");
    check(j.dtlSource == 10, "the FIRST DownTheLine stream is the DTL camera");
    check(j.path.framesFaceOn == QStringLiteral("mp4") && j.path.framesDtl == QStringLiteral("mp4"),
          "frame backing of the leading face-on and of the DTL stream");
    check(j.impactUs == -1, "no impact recorded -> unknown");
}

// ── Test 3 ────────────────────────────────────────────────────────────────────────────────────
void testCorrectedClub()
{
    std::printf("[Test 3] the effective club (IM-3)\n");
    const rec::Recording r = liveRecording();

    // A correction found in the bag: its geometry and standard length, no prior.
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{ QStringLiteral("9 iron"), QString() });   // a stored spelling
        const ShotAnalysisJob  j = buildAnalysisJob(r, e, options("reanalysis"));
        const int std9 = standardClub(QStringLiteral("9 IRON"), ClubSet::Men, 1.80).lengthMm;
        check(e.club.corrected && e.club.name == QStringLiteral("9 IRON"), "the correction is the effective club");
        check(e.club.captured == QStringLiteral("7 IRON"), "the captured club kept as provenance");
        check(r.context.club.name == QStringLiteral("7 IRON"), "the Recording's captured club untouched");
        check(e.club.standardLengthMm == std9 && std9 > 0, "its standard length for the club set and height");
        check(!e.club.lengthPrior.present(), "no length prior");
        check(j.clubName == QStringLiteral("9 IRON"), "job: the corrected club's name");
        check(j.clubLengthKnown && std::abs(j.clubLengthM - 0.914) < 1e-12, "job: the bag's length for it");
        check(j.bandCentersMm == std::vector<double>{ 290.0, 490.0 } && j.hoselFromButtMm == 855.0,
              "job: the bag's bands and hosel for it");
        check(j.priorClubLenN == 0 && j.priorClubLenPx < 0.0 && j.priorClubLenVarPx < 0.0,
              "job: no length prior");
    }
    // A correction in the bag with no measured length: the bag's other geometry, the standard length.
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{ QStringLiteral("PITCHING WEDGE"), QString() });
        const ShotAnalysisJob  j = buildAnalysisJob(r, e, options("reanalysis"));
        const int stdPw = standardClub(QStringLiteral("PITCHING WEDGE"), ClubSet::Men, 1.80).lengthMm;
        check(!j.clubLengthKnown && j.clubLengthM == ShotAnalysisJob{}.clubLengthM,
              "unmeasured bag club: length unknown, the default search radius");
        check(std::abs(j.standardClubLengthM - stdPw / 1000.0) < 1e-12, "...and its standard length");
        check(j.bandCentersMm == std::vector<double>{ 280.0 }, "...and the bag's bands");
    }
    // A correction absent from the bag: no geometry, the standard length for the RECORDED club set.
    {
        rec::Recording w = r;
        w.context.clubSet = ClubSet::Women;
        const EffectiveContext e = effectiveContext(w.context, ClubReview{ QStringLiteral("SAND WEDGE"), QString() });
        const ShotAnalysisJob  j = buildAnalysisJob(w, e, options("reanalysis"));
        const ClubStandard stW = standardClub(QStringLiteral("SAND WEDGE"), ClubSet::Women, 1.80);
        const ClubStandard stM = standardClub(QStringLiteral("SAND WEDGE"), ClubSet::Men, 1.80);
        check(e.club.geometry.bandCentersMm.empty() && !e.club.geometry.lengthKnown, "no bag entry: no geometry");
        check(!j.clubLengthKnown && j.bandCentersMm.empty() && j.shaftType.isEmpty(), "job: no geometry");
        check(stW.valid && stW.lengthMm < stM.lengthMm
                  && std::abs(j.standardClubLengthM - stW.lengthMm / 1000.0) < 1e-12,
              "job: the women's standard length (the recorded club set)");
        check(j.priorClubLenN == 0, "job: no length prior");
    }
    // A correction naming the captured club: the recorded club, its prior kept.
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{ QStringLiteral("7-iron"), QString() });
        check(!e.club.corrected && e.club.lengthPrior.present() && e.club.name == QStringLiteral("7 IRON"),
              "correction == captured: the recorded club and prior");
    }
}

// ── Test 3b ───────────────────────────────────────────────────────────────────────────────────
void testDeclaredClub()
{
    std::printf("[Test 3b] the declared club sets the standard length, as today\n");
    rec::Recording r = liveRecording();
    // A captured club with no measured length, so the job reads the standard length (the stages
    // read it only while the length is unknown — today's rule too).
    r.context.club.geometry.lengthMm    = 0.0;
    r.context.club.geometry.lengthKnown = false;
    const int std7  = standardClub(QStringLiteral("7 IRON"), ClubSet::Men, 1.80).lengthMm;
    const int std9  = standardClub(QStringLiteral("9 IRON"), ClubSet::Men, 1.80).lengthMm;
    const int stdPw = standardClub(QStringLiteral("PITCHING WEDGE"), ClubSet::Men, 1.80).lengthMm;
    const int stdDr = standardClub(QStringLiteral("DRIVER"), ClubSet::Men, 1.80).lengthMm;

    // The popover changed the club: its standard length, the captured club's geometry and prior.
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{ QString(), QStringLiteral("9 IRON") });
        const ShotAnalysisJob  j = buildAnalysisJob(r, e, options("reanalysis"));
        check(!e.club.corrected && e.club.name == QStringLiteral("7 IRON"), "popover: not a correction, captured club kept");
        check(e.club.standardLengthMm == std9, "popover: the declared club's standard length");
        check(std::abs(j.standardClubLengthM - std9 / 1000.0) < 1e-12, "job: the declared club's standard length");
        check(j.clubName == QStringLiteral("7 IRON") && j.bandCentersMm == std::vector<double>{ 300.0, 500.0, 700.0 }
                  && j.hoselFromButtMm == 880.0, "job: the captured club's name and geometry");
        check(j.priorClubLenN == 6, "job: the captured club's prior kept");
    }
    // The board's override wins over the popover: the override's bag geometry and standard length.
    {
        const EffectiveContext e = effectiveContext(r.context,
            ClubReview{ QStringLiteral("9 IRON"), QStringLiteral("PITCHING WEDGE") });
        const ShotAnalysisJob  j = buildAnalysisJob(r, e, options("reanalysis"));
        check(e.club.corrected && e.club.name == QStringLiteral("9 IRON") && e.club.standardLengthMm == std9
                  && std9 != stdPw, "override + popover: the override's standard length");
        check(j.clubLengthKnown && std::abs(j.clubLengthM - 0.914) < 1e-12
                  && j.bandCentersMm == std::vector<double>{ 290.0, 490.0 } && j.priorClubLenN == 0,
              "override + popover: the override's bag geometry, no prior");
    }
    // Neither: the captured club's recorded standard length.
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{});
        const ShotAnalysisJob  j = buildAnalysisJob(r, e, options("reanalysis"));
        check(e.club.standardLengthMm == r.context.club.standardLengthMm && std::abs(j.standardClubLengthM - std7 / 1000.0) < 1e-12,
              "neither: the captured club's standard length");
    }
    // Capture's seed is taken as stored (no stub detection until step 6): a seeded "DRIVER" sets the
    // driver's standard length, as today's re-analysis does.
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{ QString(), QStringLiteral(" DRIVER ") });
        check(e.club.standardLengthMm == stdDr && e.club.name == QStringLiteral("7 IRON"),
              "seeded stub: the driver's standard length, as today");
    }
    // A declared club that names no standard club: no standard length (today's st.valid gate).
    {
        const EffectiveContext e = effectiveContext(r.context, ClubReview{ QString(), QStringLiteral("CHIPPER") });
        check(e.club.standardLengthMm == 0.0, "unknown declared club: no standard length");
    }
}

// ── Test 4 ────────────────────────────────────────────────────────────────────────────────────
void testUndeclaredStaysUndeclared()
{
    std::printf("[Test 4] undeclared stays undeclared\n");
    rec::Recording r = liveRecording();
    r.context.club = rec::RecordedClub{};          // capture declared no club
    const EffectiveContext e = effectiveContext(r.context, ClubReview{});
    const ShotAnalysisJob  j = buildAnalysisJob(r, e, options("live"));
    check(e.club.name.isEmpty() && !e.club.corrected, "effective club empty");
    check(j.clubName.isEmpty(), "job club empty — never the DRIVER stub");
    check(!j.clubLengthKnown && j.clubLengthM == ShotAnalysisJob{}.clubLengthM, "length unknown");
    check(j.standardClubLengthM == 0.0, "no standard length for an undeclared club");
    check(j.bandCentersMm.empty() && j.priorClubLenN == 0, "no geometry, no prior");
}

// ── Test 5 ────────────────────────────────────────────────────────────────────────────────────
void testBallRule()
{
    std::printf("[Test 5] the ball rule (D5)\n");
    rec::Recording r = liveRecording();
    r.ball.baseline.path = QStringLiteral("/scratch/FaceCam.ballbase.f32");
    r.ball.baseline.w = 64; r.ball.baseline.h = 48;
    r.ball.baseline.roi = QRectF(0.3, 0.6, 0.4, 0.3);
    const ShotAnalysisJob j = buildAnalysisJob(r, effectiveContext(r.context, ClubReview{}), options("live"));
    check(j.ballTrack.frames.empty(), "valid baseline: the recorded stream is not passed (BallRunner re-runs)");
    check(j.ballBaseline.isValid() && j.ballBaseline.path == r.ball.baseline.path, "the baseline is passed");
    check(j.ballSearchRoi == r.ball.searchRoi, "the search ROI is passed");

    rec::Recording n = liveRecording();
    n.ball.baseline.w = 0;                         // invalid
    const ShotAnalysisJob k = buildAnalysisJob(n, effectiveContext(n.context, ClubReview{}), options("live"));
    check(k.ballTrack.frames.size() == size_t(kFrames), "no valid baseline: the recorded stream is passed");
}

// ── Test 6 ────────────────────────────────────────────────────────────────────────────────────
void testRunOptions()
{
    std::printf("[Test 6] run options\n");
    const rec::Recording r = liveRecording();
    RunOptions o = options("reanalysis");
    o.forceRerun = true;
    o.fullWindow = true;
    o.sessionTypeOverride = 0;
    o.tuningOverrides.insert(QStringLiteral("shaft.ridgeKernelPx"), 5);
    o.poseTrackPath = QStringLiteral("/scratch/pose.json");
    o.reuse.insert(QStringLiteral("pose"), QStringLiteral("forced"));
    o.posePreloadedScope = QStringLiteral("span");
    bool called = false;
    o.progress = [&called](float) { called = true; };
    const ShotAnalysisJob j = buildAnalysisJob(r, effectiveContext(r.context, ClubReview{}), o);
    check(j.path.forced && j.fullWindow, "forced and full window");
    check(j.sessionType == 0, "the session-type override wins");
    check(j.tuningOverrides == o.tuningOverrides && j.poseTrackPath == o.poseTrackPath, "SwingLab knobs");
    check(j.path.reuse == o.reuse && j.posePreloadedScope == QStringLiteral("span"), "reuse gate's products");
    if (j.progress) j.progress(0.5f);
    check(called, "the progress sink");
    check(j.markerSourceId == kInvalidSourceId, "markerSourceId never set");
    check(j.swingDir.isEmpty(), "swingDir not set (no reader)");
}

// ── Test 7 ────────────────────────────────────────────────────────────────────────────────────
void testMappings()
{
    std::printf("[Test 7] code mappings\n");
    check(rec::perspectiveFromCode(2) == rec::Perspective::FaceOn
              && rec::perspectiveFromCode(4) == rec::Perspective::Impact
              && rec::perspectiveFromCode(1) == rec::Perspective::DownTheLine
              && rec::perspectiveFromCode(3) == rec::Perspective::Other
              && rec::perspectiveFromCode(0) == rec::Perspective::None
              && rec::perspectiveFromCode(9) == rec::Perspective::None,
          "perspective codes as streams[].setup.perspective stores them");
    for (int c = 0; c <= 4; ++c)
        if (rec::perspectiveCode(rec::perspectiveFromCode(c)) != c) check(false, "perspective code round trip");
    check(rec::handednessFromName(QStringLiteral("left")) == rec::Handedness::Left
              && rec::handednessFromName(QStringLiteral("Right")) == rec::Handedness::Right
              && rec::handednessFromName(QString()) == rec::Handedness::Unknown
              && rec::handednessFromName(QStringLiteral("ambi")) == rec::Handedness::Unknown,
          "handedness from the athlete record's spelling");
    check(int(rec::Handedness::Right) == 1 && int(rec::Handedness::Left) == 2, "the job's handedness codes");
}

} // namespace

int main()
{
    testLiveEqualsReloaded();
    testCameraRoles();
    testCorrectedClub();
    testDeclaredClub();
    testUndeclaredStaysUndeclared();
    testBallRule();
    testRunOptions();
    testMappings();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
