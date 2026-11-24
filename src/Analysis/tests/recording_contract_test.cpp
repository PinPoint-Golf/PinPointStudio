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
//           This leg is in memory; Test 1b is the same comparison through the disk.
//   Test 1b live == written and loaded (step 1.2): the swing written as capture writes it (the one
//           header writer, the analysisContext block) and read by the Recording loader.
//   Test 1c the live job == the re-analysis job for an uncorrected shot (step 1.5, H-02): the club
//           capture seeds into review.club is the declared club on both paths; the height arrives.
//   Test 2  camera roles from the one perspective mapping (face-on first, last impact, first DTL).
//   Test 3  the effective club: a correction → its bag geometry and standard length, no prior;
//           a correction absent from the bag → no geometry, the standard length for the club set;
//           a correction naming the captured club → the recorded club, prior kept.
//   Test 3b the declared club (the popover's review.club; Mark 14 Nov 2026): it sets the STANDARD
//           length, as today's re-analysis does, the geometry staying the captured club's; the
//           board's override wins over it; neither → the captured club's recorded standard length.
//   Test 4  undeclared stays undeclared — never the "DRIVER" stub.
//   Test 5  the ball rule (D5): a valid empty-mat baseline → the recorded stream is not passed.
//   Test 6  the run options, and nothing else, carry the run's knobs.
//   Test 7  the code mappings (perspective, handedness).
//   Test 8  the integrity verdicts (step 1.5, integrityFor): one Recording judged on the ring clock
//           (live) and window-relative (re-analysis) gets the same IMU and capture verdicts; the
//           rule reads the recording's filter, not a setting; the impact camera is coverage-exempt.
//   Test 9  the export job takes every Recording input from the freeze (step 1.5, H-02): baseline,
//           search box, placement, pose-quality tier, IMU filter, lane rate and calibration.
//
//   cmake --build build/tests --target recording_contract_test -j 6
//   ctest --test-dir build/tests -R '^recording_contract_test$' --output-on-failure

#include "../analysis_job_builder.h"
#include "../effective_context.h"
#include "../../Recording/recording.h"
#include "../../Core/club_standards.h"
#include "../../Buffer/swing_window.h"
#include "../../Export/recording_loader.h"
#include "../recording_integrity.h"
#include "../../Export/swing_doc.h"   // imuIntegrityJson / captureIntegrityJson — the verdicts as stored
#include "../../Export/capture_export_facts.h"   // the export's Recording inputs from the freeze (Test 9)
#include "../../Export/swing_exporter.h"
#include "recording_fixture.h"

#include <memory>

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

// The synthetic swing (constants, liveRecording, syntheticContext, writeSyntheticSwing) is
// recording_fixture.h, shared with recording_store_test.
using namespace recording_fixture;

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
    DIFF(a.poseTrackJson == b.poseTrackJson, "poseTrackJson");
    DIFF(a.posePreloaded.frames.size() == b.posePreloaded.frames.size(), "posePreloaded");
    DIFF(a.posePreloadedScope == b.posePreloadedScope, "posePreloadedScope");
    DIFF(a.ballPreloaded.frames.size() == b.ballPreloaded.frames.size(), "ballPreloaded");
    DIFF(a.poseDtlPreloaded.frames.size() == b.poseDtlPreloaded.frames.size(), "poseDtlPreloaded");
    DIFF(a.poseDtlTrackPath == b.poseDtlTrackPath, "poseDtlTrackPath");
    DIFF(a.poseDtlTrackJson == b.poseDtlTrackJson, "poseDtlTrackJson");
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
    DIFF(a.ballTrackJson == b.ballTrackJson, "ballTrackJson");
    DIFF(a.ballSearchRoi == b.ballSearchRoi, "ballSearchRoi");
    DIFF(a.ballBaseline.path == b.ballBaseline.path && a.ballBaseline.w == b.ballBaseline.w
         && a.ballBaseline.h == b.ballBaseline.h && a.ballBaseline.roi == b.ballBaseline.roi
         && a.ballBaseline.bytes == b.ballBaseline.bytes, "ballBaseline");
    DIFF(a.tuningOverrides == b.tuningOverrides, "tuningOverrides");
    DIFF(a.fullWindow == b.fullWindow, "fullWindow");
    DIFF(a.inputs == b.inputs && a.inputs.stamped(), "inputs (the input stamp, step 1.6)");
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

// ── Test 1b ───────────────────────────────────────────────────────────────────────────────────
// The disk half 1.1 deferred: the same swing written to a folder as capture writes one (the one
// header writer with its analysisContext block, a raw-sidecar camera, the IMU samples, the ball
// stream, analysis.bindings) and read back by the Recording loader (step 1.2). The job built from
// the loaded Recording equals the live one field by field, times relative to t0Us.
void testLiveEqualsDisk()
{
    std::printf("[Test 1b] live Recording -> job == written-to-disk-and-loaded Recording -> job\n");
    // The ring's ids are the loader's here (it numbers a document's streams from 0, in order);
    // which id a source gets is not a fact about the swing. The recorded ball stream does not say
    // which camera it belongs to; since step 1.5 the builder stamps it the face-on camera's on every
    // path, so ballTrack.camera is compared like every other field.
    rec::Recording live = liveRecording(kDiskCam, kDiskImu);

    QTemporaryDir dir;
    QString err;
    check(dir.isValid() && writeSyntheticSwing(dir.path(), live, {}, &err), "the swing is written to a folder");
    const LoadedRecording lr = loadRecording(dir.path());
    if (!lr.ok) std::printf("    load error: %s\n", lr.error.toUtf8().constData());
    check(lr.ok && lr.window && lr.recording.window == lr.window.get(), "loaded, with its disk-backed window");
    check(lr.contextRecorded && !lr.readOnly, "the analysisContext block read back, writable");

    const ShotAnalysisJob jLive = buildAnalysisJob(live, effectiveContext(live.context, ClubReview{}), options("live"));
    const ShotAnalysisJob jDisk = buildAnalysisJob(lr.recording, effectiveContext(lr.recording.context, lr.review),
                                                   options("reanalysis"));
    const QString diff = firstDiff(jLive, live.t0Us, jDisk, lr.recording.t0Us);
    if (!diff.isEmpty()) std::printf("    first difference: %s\n", diff.toUtf8().constData());
    check(diff.isEmpty(), "every job field equal, times relative to t0Us");
    check(lr.recording.t0Us == 0 && jDisk.impactUs == kImpact - kWindowStart, "the loaded job is window-relative");
    check(jDisk.path.framesFaceOn == QStringLiteral("raw"), "frames from the raw sidecar");
    check(jDisk.athleteHeightM == 1.80 && jDisk.clubLengthKnown && jDisk.priorClubLenN == 6,
          "height, the club's recorded length and the length prior came through the disk");
}

// ── Test 1c ───────────────────────────────────────────────────────────────────────────────────
// H-02's test (step 1.5): THE LIVE JOB == THE RE-ANALYSIS JOB for an uncorrected shot. The live
// capture builds its job from its Recording with the club it seeds into review.club as the declared
// club (capture_context.h); re-analysis reads the swing back — the analysisContext block, height
// included, and that seeded review.club — and builds its job with the same two inputs. Two seeds: the
// session's own club, and an undeclared session whose seed is the stub (its standard length, as today).
void testLiveJobEqualsReanalysisJob()
{
    std::printf("[Test 1c] the live job == the re-analysis job for an uncorrected shot (H-02)\n");
    struct Case { const char *name; QString capturedClub; QString seed; };
    const Case cases[] = {
        { "the session's club", QStringLiteral("7 IRON"), QStringLiteral("7 IRON") },
        { "an undeclared session, the stub seeded", QString(), QStringLiteral("DRIVER") },
    };
    for (const Case &c : cases) {
        rec::Recording live = liveRecording(kDiskCam, kDiskImu);
        live.context.club.name = c.capturedClub;
        if (c.capturedClub.isEmpty()) {
            live.context.club.geometry    = {};
            live.context.club.lengthPrior = {};
            live.context.club.standardLengthMm = 0.0;
        }
        ClubReview liveReview;
        liveReview.declared = c.seed;

        QTemporaryDir dir;
        QString err;
        const QJsonObject review{ { QStringLiteral("review"), QJsonObject{ { QStringLiteral("club"), c.seed } } } };
        check(dir.isValid() && writeSyntheticSwing(dir.path(), live, review, &err), "the swing is written as capture writes it");
        const LoadedRecording lr = loadRecording(dir.path());
        check(lr.ok && lr.contextRecorded && lr.review.declared == c.seed && lr.review.correction.isEmpty(),
              "read back: the recorded context and the seeded review club, no correction");

        const ShotAnalysisJob jLive = buildAnalysisJob(live, effectiveContext(live.context, liveReview), options("live"));
        const ShotAnalysisJob jRe   = buildAnalysisJob(lr.recording, effectiveContext(lr.recording.context, lr.review),
                                                       options("reanalysis"));
        const QString diff = firstDiff(jLive, live.t0Us, jRe, lr.recording.t0Us);
        if (!diff.isEmpty()) std::printf("    %s: first difference: %s\n", c.name, diff.toUtf8().constData());
        check(diff.isEmpty(), c.name);
        check(jLive.athleteHeightM == 1.80 && jRe.athleteHeightM == 1.80, "the height reaches both (H-02)");
        check(jLive.windowT0Us && *jLive.windowT0Us == live.t0Us && jRe.windowT0Us && *jRe.windowT0Us == 0,
              "each job carries its window's start (the pipeline's one rebase)");
        check(jLive.ballTrack.camera == kDiskCam && jRe.ballTrack.camera == kDiskCam,
              "the recorded ball stream is the face-on camera's on both paths");
        if (c.capturedClub.isEmpty())
            check(!jLive.clubLengthKnown && jLive.standardClubLengthM > 0.0
                      && jLive.standardClubLengthM == jRe.standardClubLengthM,
                  "undeclared: no length recorded, the seeded stub's standard length on both");
    }
}

// ── Test 8 ────────────────────────────────────────────────────────────────────────────────────
// The integrity rule (step 1.5): ONE Recording, judged live and on re-analysis, gets the same two
// verdicts. The live twin is the loaded Recording on the ring clock — the same frames, every time
// shifted by the window's start, t0Us = that start — which is how the live capture holds it.
struct BorrowedSource final : SwingPayloadSource {
    const SwingWindow *w = nullptr;
    SourceRing::ReadHandle payloadOf(SourceId id, uint64_t seq) const noexcept override
    {
        return w->payloadOf(IndexEntry{ 0, id, seq });
    }
    const FormatDescriptor &formatOf(SourceId id) const noexcept override { return w->formatOf(id); }
    bool validate(SourceId, const SourceRing::ReadHandle &) const noexcept override { return true; }
};

void testIntegrityLiveEqualsReanalysis()
{
    std::printf("[Test 8] the integrity verdicts: live == re-analysis on one Recording\n");
    rec::Recording live = liveRecording(kDiskCam, kDiskImu);
    QTemporaryDir dir;
    QString err;
    check(dir.isValid() && writeSyntheticSwing(dir.path(), live, {}, &err), "the swing is written");
    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok && lr.window, "loaded");
    if (!lr.ok || !lr.window) return;

    // Both windows lose the same three camera frames after impact (a host stall): a hole to judge.
    const auto windowOn = [&lr](int64_t shift) {
        auto src = std::make_unique<BorrowedSource>();
        src->w = lr.window.get();
        std::vector<IndexEntry> entries;
        for (const IndexEntry &e : lr.window->entries()) {
            if (e.source_id == kDiskCam && e.source_sequence >= 7 && e.source_sequence <= 9) continue;
            IndexEntry x = e;
            x.timestamp_us += shift;
            entries.push_back(x);
        }
        return std::make_unique<SwingWindow>(std::move(src), std::move(entries),
                                             lr.window->startTimestampUs() + shift,
                                             lr.window->endTimestampUs() + shift);
    };
    const std::unique_ptr<SwingWindow> relWindow  = windowOn(0);
    const std::unique_ptr<SwingWindow> ringWindow = windowOn(kWindowStart);
    rec::Recording onDisk = lr.recording;
    onDisk.window = relWindow.get();
    rec::Recording onRing = lr.recording;
    onRing.window = ringWindow.get();
    onRing.t0Us   = kWindowStart;

    const IntegrityVerdicts re = integrityFor(onDisk);
    const IntegrityVerdicts lv = integrityFor(onRing);
    check(re.capture && re.capture->holes == 1 && re.capture->postImpact && re.capture->firstHoleUs > 0.0,
          "the stall is judged: one hole, after impact");
    check(re.imu.has_value() && lv.imu.has_value(), "the recording says Madgwick: the re-fusion check runs on both");
    check(re.capture.has_value() && lv.capture.has_value() && re.capture->camerasChecked == 1,
          "the capture check ran on the one camera");
    check(re.imu && lv.imu && imuIntegrityJson(*re.imu) == imuIntegrityJson(*lv.imu), "the IMU verdicts are equal");
    check(re.capture && lv.capture && captureIntegrityJson(*re.capture) == captureIntegrityJson(*lv.capture),
          "the capture verdicts are equal (holes, coverage, frames before impact)");

    // The rule reads the Recording, never a setting: an ESKF recording makes no IMU claim.
    rec::Recording eskf = lr.recording;
    eskf.integrity.hostOrientationFilter = QStringLiteral("eskf");
    check(!integrityFor(eskf).imu.has_value(), "ESKF recorded: no IMU claim");
    // An impact camera is judged for holes only, never its coverage of the window.
    rec::Recording imp = lr.recording;
    imp.sources.cameras.front().perspective = rec::Perspective::Impact;
    imp.integrity.windowEndUs = imp.integrity.windowEndUs * 10;   // a window its frames cannot cover
    rec::Recording faceOn = lr.recording;
    faceOn.integrity.windowEndUs = faceOn.integrity.windowEndUs * 10;
    check(integrityFor(faceOn).capture && integrityFor(faceOn).capture->earlyEnd
              && integrityFor(imp).capture && !integrityFor(imp).capture->earlyEnd,
          "the impact camera is exempt from coverage, the face-on camera is not");
}

// ── Test 9 ────────────────────────────────────────────────────────────────────────────────────
// The export takes the Recording's inputs FROM THE FREEZE (step 1.5, review round 1 — H-02). The
// export job is first filled as a second read 12–37 s later would fill it (the detector's re-learned
// baseline, a moved search box, changed settings), then given the frozen facts through the seam the
// live export calls (capture_export_facts.h); every one of them wins, and the one header writer then
// records the frozen pose-quality tier. (The exported FILE is checked end to end, through the real
// exporter and loader, in swing_window_parity_test Test 8 — this target does not link the encoder.)
void testExportTakesFrozenInputs()
{
    std::printf("[Test 9] the export job takes the Recording's inputs from the freeze\n");
    SwingExportJob job;
    SwingExportCamera cam;
    cam.sourceId = kDiskCam;
    cam.perspective = 1; cam.mirrored = false; cam.fixedInPlace = false;     // re-read later
    cam.ballSearchRoi = QRectF(0.1, 0.1, 0.2, 0.2);
    cam.ballBaselineBlob = QByteArray(12, char(0x11));
    cam.ballBaselineW = 1; cam.ballBaselineH = 3; cam.ballBaselineRoi = QRectF(0.1, 0.1, 0.2, 0.2);
    job.cameras.push_back(cam);
    job.motionCaptureQuality = QStringLiteral("Medium");
    SwingImuDeviceInfo witmotion;
    witmotion.orientationFilter = QStringLiteral("ESKF");
    witmotion.outputRateHz = 100;
    witmotion.alignA = QQuaternion(1, 0, 0, 0);
    job.imuDeviceBySerial.insert(QStringLiteral("IMU-1"), witmotion);
    job.imuDeviceBySerial.insert(QStringLiteral("HM#palm"), SwingImuDeviceInfo{});   // fuses on-device

    const rec::Recording frozen = liveRecording(kDiskCam, kDiskImu);
    CaptureCameraFacts f;
    f.source = kDiskCam; f.perspective = 2; f.mirrored = true; f.fixedInPlace = true;
    f.searchRoi = frozen.ball.searchRoi;
    f.baselineBlob = QByteArray(4 * 3 * int(sizeof(float)), char(0x42));
    f.baselineW = 4; f.baselineH = 3; f.baselineRoi = frozen.ball.searchRoi;
    f.baselineNoise0 = 2.0;
    const std::vector<CaptureCameraFacts> facts{ f };
    if (const CaptureCameraFacts *ff = captureCameraFactsFor(facts, job.cameras.front().sourceId))
        applyCaptureCameraFacts(job.cameras.front(), *ff);
    applyCaptureSettings(job, frozen.capture.motionCaptureQuality, QStringLiteral("Madgwick"));
    applyCaptureImuFacts(job.imuDeviceBySerial[QStringLiteral("IMU-1")], 200, frozen.imuBindings.front());

    const SwingExportCamera &c = job.cameras.front();
    check(c.perspective == 2 && c.mirrored && c.fixedInPlace, "placement, mirroring, fixed in place: frozen");
    check(c.ballSearchRoi == frozen.ball.searchRoi, "the search box: frozen");
    check(c.ballBaselineBlob == f.baselineBlob && c.ballBaselineW == 4 && c.ballBaselineH == 3
              && c.ballBaselineRoi == f.baselineRoi && c.ballBaselineNoise0 == 2.0,
          "the empty-mat baseline: the pre-shot one the analysis ran on, not the re-learned one");
    check(job.motionCaptureQuality == QStringLiteral("High"), "the pose-quality tier: frozen");
    check(job.imuDeviceBySerial.value(QStringLiteral("IMU-1")).orientationFilter == QStringLiteral("Madgwick")
              && job.imuDeviceBySerial.value(QStringLiteral("HM#palm")).orientationFilter.isEmpty(),
          "the host IMU filter: frozen on host-fused lanes, none on a HackMotion unit");
    const SwingImuDeviceInfo &d = job.imuDeviceBySerial[QStringLiteral("IMU-1")];
    check(d.outputRateHz == 200 && d.hasCalibration && d.alignA == frozen.imuBindings.front().alignA
              && d.calibAgeSec == frozen.imuBindings.front().calibAgeSec
              && d.role == int(frozen.imuBindings.front().role),
          "the IMU lane's rate, calibration and role: frozen");
    const RecordingHeader h = recordingHeaderFor(job, kWindowStart, kWindowEnd - kWindowStart);
    check(h.capture.value(QStringLiteral("motionCaptureQuality")).toString() == QStringLiteral("High"),
          "the one header writer records the frozen tier");
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

// ── Test 5b ───────────────────────────────────────────────────────────────────────────────────
// The persisted baseline blob is read by the Recording loader, not the ball stage (refactor step 7.2,
// P11): the loaded ref carries the file's bytes, which the stage takes (ballBaselineFromBytes,
// ball_baseline_io_test). A wrong-sized blob still makes a valid ref, as before (the stage rejects it by
// size and self-seeds, and the recorded stream stays withheld: D5); a missing blob makes none.
void testBaselineBlobReadByLoader()
{
    std::printf("[Test 5b] the baseline blob is read by the Recording loader\n");
    const QByteArray blob(4 * 3 * int(sizeof(float)), char(0x42));
    auto writeWithBaseline = [](const QString &dir, const QByteArray *bytes) {
        const rec::Recording live = liveRecording(kDiskCam, kDiskImu);
        QString err;
        if (!writeSyntheticSwing(dir, live, {}, &err)) return false;
        QJsonObject root = SwingStore::load(dir);
        QJsonArray streams = root[QStringLiteral("streams")].toArray();
        QJsonObject video = streams.at(0).toObject();
        QJsonObject setup = video[QStringLiteral("setup")].toObject();
        QJsonObject bd = setup[QStringLiteral("ballDetection")].toObject();
        bd[QStringLiteral("baseline")] = QJsonObject{
            { QStringLiteral("file"), QStringLiteral("FaceOn.ballbase.f32") },
            { QStringLiteral("w"), 4 }, { QStringLiteral("h"), 3 },
            { QStringLiteral("roi"), QJsonArray{ 0.3, 0.6, 0.4, 0.3 } },
            { QStringLiteral("noise0"), 2.0 } };
        setup[QStringLiteral("ballDetection")] = bd;
        video[QStringLiteral("setup")] = setup;
        streams[0] = video;
        root[QStringLiteral("streams")] = streams;
        if (!SwingStore::save(dir, root, &err)) return false;
        if (bytes) {
            QFile f(dir + QStringLiteral("/FaceOn.ballbase.f32"));
            if (!f.open(QIODevice::WriteOnly) || f.write(*bytes) != bytes->size()) return false;
        }
        return true;
    };

    QTemporaryDir dir;
    check(dir.isValid() && writeWithBaseline(dir.path(), &blob), "a swing with a baseline blob is written");
    const LoadedRecording lr = loadRecording(dir.path());
    const BallBaselineRef &b = lr.recording.ball.baseline;
    check(lr.ok && b.isValid() && b.bytes == blob && b.w == 4 && b.h == 3 && b.noise0 == 2.0,
          "the loaded ref carries the blob's bytes, read by the loader");
    check(b.path.endsWith(QStringLiteral("/FaceOn.ballbase.f32")), "its path kept as provenance");
    const ShotAnalysisJob j = buildAnalysisJob(lr.recording, effectiveContext(lr.recording.context, lr.review),
                                               options("reanalysis"));
    check(j.ballBaseline.bytes == blob && j.ballTrack.frames.empty(),
          "the job hands the bytes to the stage; the recorded stream is withheld (D5)");

    QTemporaryDir shortDir;
    const QByteArray truncated = blob.left(blob.size() - int(sizeof(float)));
    check(shortDir.isValid() && writeWithBaseline(shortDir.path(), &truncated), "a swing with a short blob");
    const LoadedRecording ls = loadRecording(shortDir.path());
    check(ls.ok && ls.recording.ball.baseline.isValid() && ls.recording.ball.baseline.bytes == truncated,
          "a short blob still makes a valid ref (the stage rejects it by size and self-seeds, as before)");

    QTemporaryDir noBlob;
    check(noBlob.isValid() && writeWithBaseline(noBlob.path(), nullptr), "a swing whose blob is missing");
    const LoadedRecording ln = loadRecording(noBlob.path());
    check(ln.ok && !ln.recording.ball.baseline.isValid(), "a missing blob makes no ref (unchanged)");
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
    o.poseTrackJson = QJsonObject{ { QStringLiteral("frames"), QJsonArray{} } };
    o.ballTrackPath = QStringLiteral("/scratch/ball.json");
    o.ballTrackJson = std::nullopt;                // named but unreadable
    o.reuse.insert(QStringLiteral("pose"), QStringLiteral("forced"));
    o.posePreloadedScope = QStringLiteral("span");
    bool called = false;
    o.progress = [&called](float) { called = true; };
    const ShotAnalysisJob j = buildAnalysisJob(r, effectiveContext(r.context, ClubReview{}), o);
    check(j.path.forced && j.fullWindow, "forced and full window");
    check(j.sessionType == 0, "the session-type override wins");
    check(j.tuningOverrides == o.tuningOverrides && j.poseTrackPath == o.poseTrackPath, "SwingLab knobs");
    check(j.poseTrackJson == o.poseTrackJson && j.ballTrackPath == o.ballTrackPath && !j.ballTrackJson,
          "an injected file's content rides with its path, as its namer read it (step 7.2)");
    check(j.path.reuse == o.reuse && j.posePreloadedScope == QStringLiteral("span"), "reuse gate's products");
    if (j.progress) j.progress(0.5f);
    check(called, "the progress sink");
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
    testLiveEqualsDisk();
    testLiveJobEqualsReanalysisJob();
    testIntegrityLiveEqualsReanalysis();
    testExportTakesFrozenInputs();
    testCameraRoles();
    testCorrectedClub();
    testDeclaredClub();
    testUndeclaredStaysUndeclared();
    testBallRule();
    testBaselineBlobReadByLoader();
    testRunOptions();
    testMappings();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
