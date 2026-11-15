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

// THE REUSE GATE AND THE INPUT STAMP (refactor step 1.6; information_model.md R-ANA-5, R-ANA-7;
// analysis_versions.h, reuse_gate.h). On recording_fixture.h's synthetic swing (a 7 iron with its
// bag: 9 iron measured, pitching wedge unmeasured, driver):
//
//   Test 1  the stamp: analysisInputsFor names the effective club, its length and source (record /
//           bag / standard / default), height, club set, handedness and the session type (a run's
//           override included); it round-trips through analysis.versions.inputs.
//   Test 2  same inputs → everything reused: pose, DTL pose, ball, shaft and ladder; the stamp current.
//   Test 3  a corrected club → the shaft track and ladder re-run ("club changed"), pose and ball reused;
//           the stamp stale on the club.
//   Test 4  handedness changed → stale ("handedness"); the shaft and ladder re-run ("handedness
//           changed": the tracker reads the lead elbow); the reused pose is relabelled, not re-inferred.
//   Test 5  MARK'S POSE RULE: a pose (and DTL pose) stamped by another host's model → reused, its
//           recorded identity carried; a pose stage-version bump → re-run (and so ball and shaft).
//   Test 6  no input stamp (before step 1.6): stale; the shaft reused for the captured club, re-run for
//           a correction. A stamp of another version: stale, its club inputs not trusted.
//   Test 7  what still re-runs: a missing pose stamp, an empty track, SwingLab's --force / --params /
//           --full-window; the session type changed is stale.
//   Test 8  through the disk: the board's override written in the shot's review reaches the job (the
//           loader's review → effectiveContext), and the gate then re-runs the shaft only.
//   Test 9  follow-club (R-REC-2(a), DEC-82): the device's club rewrites analysisContext.club through
//           the repository, so re-analysis uses it and the input stamp marks the shot stale.

#include "../reuse_gate.h"
#include "../analysis_job_builder.h"
#include "../effective_context.h"
#include "../swing_analysis_json.h"
#include "../../Buffer/swing_window.h"   // LoadedRecording owns its window
#include "../../Export/recording_loader.h"
#include "../../Export/swing_store_repository.h"   // follow-club through the repository (Test 9)
#include "../../Export/tests/swing_doc_test_writes.h"   // testdoc::ref — the test folder's ShotRef
#include "../../LaunchMonitor/launch_monitor_reading.h"
#include "recording_fixture.h"

#include <QTemporaryDir>

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

using namespace recording_fixture;

const QString kModelB = QStringLiteral("vitpose-b-wholebody.onnx@360242862");
const QString kModelL = QStringLiteral("vitpose-l-wholebody.onnx@1234567890");   // another host's

ShotAnalysisJob jobFor(const rec::Recording &r, const ClubReview &review, RunOptions o = {})
{
    if (o.origin.isEmpty()) o.origin = QStringLiteral("reanalysis");
    return buildAnalysisJob(r, effectiveContext(r.context, review), o);
}

// A recorded analysis.versions as a stamped re-analysis leaves it: every producer at today's version,
// the pose by `model`, span-bounded, and the input stamp of `stampedBy`.
QJsonObject recordedVersions(const ShotAnalysisJob &stampedBy, const QString &model = kModelB)
{
    AnalysisVersions v;
    v.pose = kPoseStageVersion;   v.poseModel = model;   v.poseScope = QStringLiteral("span");
    v.ball = kBallStageVersion;   v.shaft = kShaftStageVersion;   v.impact = kImpactStageVersion;
    v.poseDtl = kDtlPoseStageVersion;   v.poseDtlModel = model;
    v.inputs = stampedBy.inputs;
    return analysisVersionsToJson(v);
}

ReuseGateFacts factsFor(const QJsonObject &versions, const ShotAnalysisJob &now, bool corrected)
{
    ReuseGateFacts f;
    f.versions = versions;
    f.hasBallBlock = f.hasClubBlock = f.hasPoseDtlBlock = true;
    f.hasFaceOnCamera = true;
    f.hasDtlCamera = true;
    f.inputsNow = now.inputs;
    f.clubCorrectedNow = corrected;
    return f;
}

const ReuseReloaded kAll{ true, true, true, true, true };

bool allReused(const QMap<QString, QString> &r)
{
    for (const char *k : { "pose", "ball", "shaft", "ladder", "poseDtl" })
        if (r.value(QString::fromLatin1(k)) != QLatin1String("reused")) return false;
    return r.size() == 5;
}

void printReasons(const QMap<QString, QString> &r)
{
    for (auto it = r.cbegin(); it != r.cend(); ++it)
        std::printf("      %s: %s\n", it.key().toUtf8().constData(), it.value().toUtf8().constData());
}

// ── Test 1 ────────────────────────────────────────────────────────────────────────────────────
void testStamp()
{
    std::printf("[Test 1] the input stamp\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob captured = jobFor(r, {});
    const AnalysisInputs &in = captured.inputs;
    check(in.version == kAnalysisInputsVersion && in.club == QStringLiteral("7 IRON")
              && in.clubLengthSource == QStringLiteral("record") && in.clubLengthMm == 940.0,
          "the captured club: its recorded length (record)");
    check(in.heightM == 1.80 && in.clubSet == ClubSet::Men && in.handedness == rec::Handedness::Right
              && in.sessionType == 1,
          "height, club set, handedness, the session type");

    const ShotAnalysisJob bag = jobFor(r, ClubReview{ QStringLiteral("9 iron"), QString() });
    check(bag.inputs.club == QStringLiteral("9 IRON") && bag.inputs.clubLengthSource == QStringLiteral("bag")
              && bag.inputs.clubLengthMm == 914.0,
          "a corrected club found in the bag: the bag's length (bag)");

    const ShotAnalysisJob stdJob = jobFor(r, ClubReview{ QStringLiteral("PITCHING WEDGE"), QString() });
    const int stdPw = standardClub(QStringLiteral("PITCHING WEDGE"), ClubSet::Men, 1.80).lengthMm;
    check(stdJob.inputs.clubLengthSource == QStringLiteral("standard") && stdJob.inputs.clubLengthMm == stdPw,
          "a corrected club the bag never measured: its standard length (standard)");

    rec::Recording u = r;
    u.context.club = rec::RecordedClub{};   // undeclared, nothing recorded
    const ShotAnalysisJob def = jobFor(u, {});
    check(def.inputs.club.isEmpty() && def.inputs.clubLengthSource == QStringLiteral("default")
              && def.inputs.clubLengthMm == std::round(ShotAnalysisJob{}.clubLengthM * 1000.0),
          "an undeclared club: undeclared, the job's default length (default)");

    RunOptions o;
    o.sessionTypeOverride = 0;
    check(jobFor(r, {}, o).inputs.sessionType == 0, "a run's session-type override is what the stamp names");

    const QJsonObject j = analysisInputsToJson(bag.inputs);
    check(analysisInputsFromJson(j) == bag.inputs && analysisInputsToJson(analysisInputsFromJson(j)) == j,
          "the stamp round-trips through analysis.versions.inputs");
    check(analysisInputsToJson(AnalysisInputs{}).isEmpty() && !analysisInputsFromJson(QJsonObject{}).stamped(),
          "unstamped: nothing written, nothing read");
    AnalysisVersions none;
    none.inputs = bag.inputs;
    const QJsonObject vOnly = analysisVersionsToJson(none);
    check(vOnly.keys() == QStringList{ QStringLiteral("inputs") },
          "an analysis with no pose stamp writes versions: { inputs } alone");
}

// ── Test 2 ────────────────────────────────────────────────────────────────────────────────────
void testSameInputs()
{
    std::printf("[Test 2] same inputs -> everything reused\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob job = jobFor(r, {});
    const ReuseGate gate(factsFor(recordedVersions(job), job, false));
    check(gate.mayReloadPose() && gate.mayReloadPoseDtl() && gate.mayReloadBall(true) && gate.mayReloadShaft(true),
          "pose, DTL pose, ball and shaft may all be reloaded");
    const auto reasons = gate.reasons(kAll);
    if (!allReused(reasons)) printReasons(reasons);
    check(allReused(reasons), "analysis.path.reuse: every product \"reused\"");
    check(analysisInputsStaleness(recordedVersions(job), job.inputs).isEmpty(), "the stamp is current");
}

// ── Test 3 ────────────────────────────────────────────────────────────────────────────────────
void testCorrectedClub()
{
    std::printf("[Test 3] a corrected club -> shaft and ladder re-run, pose and ball reused\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob before = jobFor(r, {});
    const ClubReview corrected{ QStringLiteral("9 IRON"), QString() };
    const ShotAnalysisJob now = jobFor(r, corrected);
    const ReuseGate gate(factsFor(recordedVersions(before), now, true));
    check(gate.mayReloadPose() && gate.mayReloadPoseDtl() && gate.mayReloadBall(true),
          "pose, DTL pose and ball reused (they do not depend on the club)");
    check(!gate.clubInputsMatch() && !gate.mayReloadShaft(true), "the shaft track may NOT be reloaded");
    const auto reasons = gate.reasons(ReuseReloaded{ true, true, true, false, false });
    check(reasons.value(QStringLiteral("pose")) == QLatin1String("reused")
              && reasons.value(QStringLiteral("ball")) == QLatin1String("reused")
              && reasons.value(QStringLiteral("poseDtl")) == QLatin1String("reused"),
          "reasons: pose, ball, DTL pose reused");
    check(reasons.value(QStringLiteral("shaft")) == QLatin1String("club changed")
              && reasons.value(QStringLiteral("ladder")) == QLatin1String("club changed"),
          "reasons: shaft and ladder \"club changed\"");
    const QString stale = analysisInputsStaleness(recordedVersions(before), now.inputs);
    std::printf("      staleness: %s\n", stale.toUtf8().constData());
    check(stale == QStringLiteral("club 7 IRON → 9 IRON"), "stale on the club");

    // The popover's club alone never changes the club inputs while the length is recorded.
    const ShotAnalysisJob declared = jobFor(r, ClubReview{ QString(), QStringLiteral("DRIVER") });
    check(ReuseGate(factsFor(recordedVersions(before), declared, false)).mayReloadShaft(true),
          "a declared club with a recorded length: the shaft still reused");
}

// ── Test 4 ────────────────────────────────────────────────────────────────────────────────────
void testHandedness()
{
    std::printf("[Test 4] handedness changed -> stale; the shaft re-runs, the pose is relabelled\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob before = jobFor(r, {});
    rec::Recording l = r;
    l.context.handedness = rec::Handedness::Left;
    const ShotAnalysisJob now = jobFor(l, {});
    check(analysisInputsStaleness(recordedVersions(before), now.inputs) == QStringLiteral("handedness"),
          "stale: \"handedness\"");
    rec::Recording h = r;
    h.context.heightM = 1.70;
    h.context.clubSet = ClubSet::Women;
    check(analysisInputsStaleness(recordedVersions(before), jobFor(h, {}).inputs) == QStringLiteral("height, club set"),
          "stale: height and club set (a backfilled context)");
    const ReuseGate gate(factsFor(recordedVersions(before), now, false));
    check(gate.clubInputsMatch() && !gate.leadSideMatches(), "the club inputs match; the lead side does not");
    check(gate.mayReloadPose() && gate.mayReloadPoseDtl() && gate.mayReloadBall(true),
          "pose, DTL pose and ball reused (Mark's pose rule: no re-inference)");
    check(gate.poseLeadSwapped(), "...the reused pose's lead/trail hands are relabelled");
    check(!gate.mayReloadShaft(true)
              && gate.reasons(ReuseReloaded{ true, true, true, false, false }).value(QStringLiteral("shaft"))
                     == QLatin1String("handedness changed"),
          "the shaft track and ladder re-run (\"handedness changed\": the tracker reads the lead elbow)");
    // Unknown and right lead with the same hand everywhere: stale, but nothing re-runs.
    rec::Recording u = r;
    u.context.handedness = rec::Handedness::Unknown;
    const ReuseGate gu(factsFor(recordedVersions(before), jobFor(u, {}), false));
    check(gu.leadSideMatches() && !gu.poseLeadSwapped() && gu.mayReloadShaft(true),
          "right -> unknown: the same lead hand, the shaft reused, no relabel");
    // No stamp: made with the recorded handedness, which is recorded once.
    QJsonObject legacy = recordedVersions(before);
    legacy.remove(QStringLiteral("inputs"));
    const ReuseGate gl(factsFor(legacy, now, false));
    check(gl.leadSideMatches() && !gl.poseLeadSwapped(), "no stamp: the lead side taken as recorded");
}

// ── Test 5 ────────────────────────────────────────────────────────────────────────────────────
void testPoseRule()
{
    std::printf("[Test 5] Mark's pose rule\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob job = jobFor(r, {});
    const ReuseGate other(factsFor(recordedVersions(job, kModelL), job, false));
    check(other.mayReloadPose() && other.mayReloadPoseDtl(),
          "a pose and DTL pose stamped by another host's model: reused");
    check(other.recordedPoseModel() == kModelL && other.recordedPoseDtlModel() == kModelL,
          "...their recorded model identity carried (provenance)");
    check(allReused(other.reasons(kAll)), "...and nothing downstream re-runs for it");

    QJsonObject bumped = recordedVersions(job);
    QJsonObject vp = bumped.value(QStringLiteral("pose")).toObject();
    vp[QStringLiteral("code")] = kPoseStageVersion - 1;
    bumped[QStringLiteral("pose")] = vp;
    QJsonObject vpd = bumped.value(QStringLiteral("poseDtl")).toObject();
    vpd[QStringLiteral("code")] = kDtlPoseStageVersion - 1;
    bumped[QStringLiteral("poseDtl")] = vpd;
    const ReuseGate gate(factsFor(bumped, job, false));
    check(!gate.mayReloadPose() && !gate.mayReloadPoseDtl(), "a pose stage-version bump: pose and DTL pose re-run");
    const auto reasons = gate.reasons(ReuseReloaded{});
    check(reasons.value(QStringLiteral("pose")) == QStringLiteral("version %1, want %2").arg(kPoseStageVersion - 1).arg(kPoseStageVersion)
              && reasons.value(QStringLiteral("ball")) == QLatin1String("needs a reloaded pose")
              && reasons.value(QStringLiteral("shaft")) == QLatin1String("needs a reloaded ball"),
          "reasons: the version, then ball and shaft follow it");
    check(!gate.mayReloadBall(false) && !gate.mayReloadShaft(false), "ball and shaft re-run on a re-run pose");
}

// ── Test 6 ────────────────────────────────────────────────────────────────────────────────────
void testNoStamp()
{
    std::printf("[Test 6] no input stamp (before step 1.6), and a stamp of another version\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob job = jobFor(r, {});
    QJsonObject legacy = recordedVersions(job);
    legacy.remove(QStringLiteral("inputs"));
    check(analysisInputsStaleness(legacy, job.inputs) == QStringLiteral("no input stamp"), "stale: no input stamp");
    check(ReuseGate(factsFor(legacy, job, false)).mayReloadShaft(true),
          "the captured club: the shaft reused (it was computed with that club)");
    const ShotAnalysisJob corrected = jobFor(r, ClubReview{ QStringLiteral("9 IRON"), QString() });
    const ReuseGate gc(factsFor(legacy, corrected, true));
    check(!gc.mayReloadShaft(true) && gc.reasons(ReuseReloaded{ true, true, true, false, false })
                                          .value(QStringLiteral("shaft")) == QLatin1String("club changed"),
          "a correction: the shaft re-runs (\"club changed\")");

    QJsonObject other = recordedVersions(job);
    QJsonObject in = other.value(QStringLiteral("inputs")).toObject();
    in[QStringLiteral("version")] = kAnalysisInputsVersion + 1;
    other[QStringLiteral("inputs")] = in;
    check(analysisInputsStaleness(other, job.inputs)
              == QStringLiteral("input stamp version %1, want %2").arg(kAnalysisInputsVersion + 1).arg(kAnalysisInputsVersion),
          "another stamp version: stale");
    check(!ReuseGate(factsFor(other, job, false)).clubInputsMatch(), "...and its club inputs are not trusted");
}

// ── Test 7 ────────────────────────────────────────────────────────────────────────────────────
void testWhatStillReruns()
{
    std::printf("[Test 7] what still re-runs\n");
    const rec::Recording r = liveRecording();
    const ShotAnalysisJob job = jobFor(r, {});
    QJsonObject noPose = recordedVersions(job);
    noPose.remove(QStringLiteral("pose"));
    const ReuseGate g1(factsFor(noPose, job, false));
    check(!g1.mayReloadPose() && g1.reasons(ReuseReloaded{}).value(QStringLiteral("pose")) == QLatin1String("no record"),
          "a missing pose stamp: re-run (\"no record\")");
    const ReuseGate g2(factsFor(recordedVersions(job), job, false));
    check(g2.mayReloadPose() && g2.reasons(ReuseReloaded{}).value(QStringLiteral("pose"))
                                    == QLatin1String("recorded track is empty"),
          "an empty recorded track: re-run (\"recorded track is empty\")");

    ReuseGateFacts forced = factsFor(recordedVersions(job), job, false);
    forced.forceRerun = true;
    const ReuseGate g3(forced);
    const auto fr = g3.reasons(ReuseReloaded{});
    check(!g3.mayReloadPose() && !g3.mayReloadPoseDtl() && !g3.mayReloadBall(true) && !g3.mayReloadShaft(true)
              && fr.size() == 5 && fr.value(QStringLiteral("shaft")) == QLatin1String("forced"),
          "SwingLab --force: nothing reused, every reason \"forced\"");

    ReuseGateFacts params = factsFor(recordedVersions(job), job, false);
    params.tuningOverrides.insert(QStringLiteral("pose.coarseStride"), 3);
    const ReuseGate g4(params);
    check(!g4.mayReloadPose() && g4.reasons(ReuseReloaded{}).value(QStringLiteral("pose")) == QLatin1String("tuning override"),
          "SwingLab --params (pose.*): pose re-runs");
    ReuseGateFacts shaftKnob = factsFor(recordedVersions(job), job, false);
    shaftKnob.tuningOverrides.insert(QStringLiteral("shaft.lenGripDownM"), 0.1);
    check(ReuseGate(shaftKnob).mayReloadPose() && !ReuseGate(shaftKnob).mayReloadShaft(true),
          "SwingLab --params (any other knob): pose reused, shaft re-runs");

    ReuseGateFacts full = factsFor(recordedVersions(job), job, false);
    full.fullWindow = true;
    const ReuseGate g5(full);
    check(!g5.mayReloadPose() && g5.reasons(ReuseReloaded{}).value(QStringLiteral("pose")) == QLatin1String("scope span, want full"),
          "SwingLab --full-window over a span pose: re-run");

    RunOptions swing;
    swing.sessionTypeOverride = 0;
    check(analysisInputsStaleness(recordedVersions(job), jobFor(r, {}, swing).inputs) == QStringLiteral("session type"),
          "a session type changed: stale (\"session type\")");
}

// ── Test 8 ────────────────────────────────────────────────────────────────────────────────────
void testThroughTheDisk()
{
    std::printf("[Test 8] through the disk: the board's override reaches the job\n");
    const rec::Recording live = liveRecording(kDiskCam, kDiskImu);
    const ShotAnalysisJob liveJob = jobFor(live, {});   // what the shot was analysed with
    const QJsonObject extra{
        { QStringLiteral("review"), QJsonObject{ { QStringLiteral("club"), QStringLiteral("7 IRON") },
                                                 { QStringLiteral("clubOverride"), QStringLiteral("9 IRON") } } },
    };
    QTemporaryDir dir;
    QString err;
    check(dir.isValid() && writeSyntheticSwing(dir.path(), live, extra, &err), "the swing is written, corrected on the board");
    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok && lr.review.correction == QStringLiteral("9 IRON"), "the loader hands over the board's override");
    const EffectiveContext e = effectiveContext(lr.recording.context, lr.review);
    const ShotAnalysisJob now = buildAnalysisJob(lr.recording, e, RunOptions{});
    check(e.club.corrected && now.clubName == QStringLiteral("9 IRON") && now.clubLengthKnown
              && std::abs(now.clubLengthM - 0.914) < 1e-12,
          "the re-analysis job uses the corrected club (the bag's 9 iron)");
    check(now.inputs.club == QStringLiteral("9 IRON") && now.inputs.clubLengthSource == QStringLiteral("bag"),
          "...and its stamp says so");
    const ReuseGate gate(factsFor(recordedVersions(liveJob), now, e.club.corrected));
    check(gate.mayReloadPose() && gate.mayReloadBall(true) && !gate.mayReloadShaft(true),
          "the gate: pose and ball reused, the shaft re-run");
}

// ── Test 9 ────────────────────────────────────────────────────────────────────────────────────
void testFollowClub()
{
    std::printf("[Test 9] follow-club: re-analysis uses the followed club, and the shot is stale\n");
    const rec::Recording live = liveRecording(kDiskCam, kDiskImu);
    const ShotAnalysisJob liveJob = jobFor(live, {});   // what the shot was analysed with (7 iron)
    const QJsonObject extra{
        { QStringLiteral("analysis"), QJsonObject{ { QStringLiteral("versions"), recordedVersions(liveJob) } } },
    };
    QTemporaryDir dir;
    QString err;
    check(dir.isValid() && writeSyntheticSwing(dir.path(), live, extra, &err), "the swing is written, analysed as a 7 iron");

    lm::LaunchMonitorReading reading;
    reading.deviceShotId  = QStringLiteral("3");
    reading.deviceKind    = QStringLiteral("premier");
    reading.clubheadSpeed = 80.0;
    reading.ballSpeed     = 110.0;
    const QJsonObject followed{
        { QStringLiteral("name"), QStringLiteral("9 IRON") }, { QStringLiteral("lengthMm"), 914.0 },
        { QStringLiteral("shaftType"), QStringLiteral("steel") },
        { QStringLiteral("bandCentersMm"), QJsonArray{ 290.0, 490.0 } },
        { QStringLiteral("hoselFromButtMm"), 855.0 }, { QStringLiteral("shaftLengthMm"), 590.0 },
        { QStringLiteral("handsEndMm"), 180.0 },
    };
    SwingStoreRecordingRepository repo(testdoc::shots());   // a test folder is a shot
    bool replaced = false;
    check(repo.completeCapture(testdoc::ref(dir.path()), reading, followed, &replaced).ok() && replaced,
          "the launch monitor names a 9 iron: follow-club replaces the captured club");

    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok && lr.contextRecorded, "reloaded, the analysisContext block still recorded");
    const rec::RecordedClub &c = lr.recording.context.club;
    check(c.name == QStringLiteral("9 IRON") && c.geometry.lengthKnown && c.geometry.lengthMm == 914.0
              && c.geometry.bandCentersMm == std::vector<double>{ 290.0, 490.0 },
          "analysisContext.club is the followed club, with its geometry");
    check(!c.lengthPrior.present()
              && c.standardLengthMm == standardClub(QStringLiteral("9 IRON"), ClubSet::Men, 1.80).lengthMm,
          "...no length prior (it was the 7 iron's), its standard length for the recorded set");
    check(lr.recording.context.heightM == 1.80 && lr.recording.context.bag.size() == live.context.bag.size(),
          "...the athlete facts and the bag snapshot untouched");
    const ShotAnalysisJob now = buildAnalysisJob(lr.recording, effectiveContext(lr.recording.context, lr.review),
                                                 RunOptions{});
    check(now.clubName == QStringLiteral("9 IRON") && now.clubLengthKnown && std::abs(now.clubLengthM - 0.914) < 1e-12
              && now.priorClubLenN == 0,
          "the re-analysis job uses the followed club");
    const QString stale = analysisInputsStaleness(lr.analysisIn.value(QStringLiteral("versions")).toObject(), now.inputs);
    std::printf("      staleness: %s\n", stale.toUtf8().constData());
    check(stale == QStringLiteral("club 7 IRON → 9 IRON"), "the input stamp marks the shot stale on the club");
    const ReuseGate gate(factsFor(lr.analysisIn.value(QStringLiteral("versions")).toObject(), now, false));
    check(gate.mayReloadPose() && !gate.mayReloadShaft(true), "the gate: pose reused, the shaft re-run");
}

} // namespace

int main()
{
    testStamp();
    testSameInputs();
    testCorrectedClub();
    testHandedness();
    testPoseRule();
    testNoStamp();
    testWhatStillReruns();
    testThroughTheDisk();
    testFollowClub();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
