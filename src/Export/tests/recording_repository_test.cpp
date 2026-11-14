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

// THE ONE DOOR TO A SWING FILE (refactor step 1.4; src/DomainModel/recording_repository.h,
// src/Export/swing_store_repository.h): every RecordingRepository method on a temporary folder.
//
//   Test 1  create: the header, the analysis and the seeded review club; no club → no review
//   Test 2  savePose honoured for newly inferred keypoints: create and writeAnalysis drop the pose
//           tracks and their stamps when it is off, and keep them when it is on
//   Test 3  writeAnalysis: the analysis replaced, capture / streams / review / the launch monitor
//           rows carried, both verdicts set from this pass (nullptr withdraws one)
//   Test 4  the empty-document guard: nothing to read → NoDocument, and nothing is created
//   Test 5  editReview merges: a rating never un-excludes; an annotation-only edit that changes
//           nothing writes nothing; cleared annotations remove their keys (and an empty block)
//   Test 6  completeCapture: a reading (rows written; follow-club replaces the captured club, never
//           over a hand-set one) and a stream arrival (pending element, then the landed file)
//   Test 7  a launch-monitor-only shot: create, isDeviceOnly, writeAnalysis with no analysis
//           (rows current → Unchanged; stale → Written, a dry run writes nothing)
//   Test 8  convert and the JSON-era sidecar
//   Test 9  EVERY WRITE REFUSES A READ-ONLY DOCUMENT (HANDOFF from 1.2's review): a newer document
//           schema, or a newer analysisContext block, is never rewritten — the bytes are unchanged
//   Test 10 reanalyseAndPersist / persistReanalysis: the one write-back (the analyzer stubbed below)
//   Test 11 Save pose keypoints governs newly inferred keypoints only: a reused pose (face-on or DTL)
//           is written back byte-equal with it off; a re-inferred one is written with it on, and
//           with it off neither it nor the superseded one stays (Mark, 14 Nov 2026)
//   Test 12 the launch monitor's low point (lowPointFromAttack) takes the EFFECTIVE club and the
//           RECORDED club set (step 1.6): a board correction moves it; a recorded women's set uses the
//           women's length; with neither, the declared club, as before.
//   Test 13 backfillAnalysisContext (the step-1 backfill, D4/DEC-34): a document with no block gets
//           its legacy context with the record's height and club set on the swing's date; a dry run
//           writes nothing; a recorded block, or facts that add nothing, are never written; a height
//           the document recorded wins; a junior is decided on the swing's date
//
//   cmake --build build/tests --target recording_repository_test -j 6
//   ctest --test-dir build/tests -R '^recording_repository_test$' --output-on-failure

#include "../swing_store_repository.h"
#include "../recording_header.h"                  // analysisContextJson (Test 12)
#include "../../Core/club_standards.h"            // standardClub (Test 12)
#include "../../LaunchMonitor/lm_inferred_reads.h" // lmLowPointFromAttack (Test 12)
#include "../swing_doc.h"
#include "../swing_paths.h"
#include "../swing_store.h"
#include "../../Analysis/reanalyse_and_persist.h"
#include "../../Analysis/swing_analysis.h"
#include "../../Analysis/capture_integrity_check.h"
#include "../../IMU/imu_refusion_check.h"
#include "../../LaunchMonitor/launch_monitor_reading.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

// Stub — avoids linking swing_paths.cpp (and its logging deps), as swing_doc_test does.
QString pinpoint::SwingPaths::sanitise(const QString &raw) { return raw; }

// THE ANALYZER, STUBBED: reanalyseAndPersist's analysis half is reanalyzeSwingDir, which needs the
// whole pipeline. This test is about the write half, so the analysis it gets is the one set here.
namespace {
pinpoint::analysis::ReanalyzeResult g_reanalysis;
int g_reanalysisCalls = 0;
}
namespace pinpoint::analysis {
ReanalyzeResult reanalyzeSwingDir(const QString &, const ReanalyzeOptions &)
{
    ++g_reanalysisCalls;
    return g_reanalysis;
}
} // namespace pinpoint::analysis

using namespace pinpoint;

namespace {

int g_fail = 0;
void check(bool ok, const char *label)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_fail;
}

// A camera shot's header: schema, clock, capture (club + impact), one video stream.
QJsonObject cameraHeader(const QString &capturedClub = QStringLiteral("DRIVER"))
{
    return QJsonObject{
        { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/2") },
        { QStringLiteral("clock"),  QJsonObject{ { QStringLiteral("t0_us"), 0 },
                                                 { QStringLiteral("unit"), QStringLiteral("us") } } },
        { QStringLiteral("capture"), QJsonObject{
              { QStringLiteral("impactUs"), 1000000 },
              { QStringLiteral("club"), QJsonObject{ { QStringLiteral("name"), capturedClub },
                                                     { QStringLiteral("lengthMm"), 1143 } } } } },
        { QStringLiteral("streams"), QJsonArray{ QJsonObject{
              { QStringLiteral("kind"), QStringLiteral("video") },
              { QStringLiteral("alias"), QStringLiteral("FaceOn") },
              { QStringLiteral("file"), QStringLiteral("FaceOn.mp4") } } } },
    };
}

// An analysis with a pose track (and its stamp) and a score.
analysis::SwingAnalysis poseAnalysis(int score)
{
    analysis::SwingAnalysis a;
    a.timeBase      = analysis::TimeBase::Window;
    a.score.overall = score;
    a.versions.pose = 1;
    analysis::PoseFrame2D pf;
    pf.t_us = 1000000;
    pf.kp[9] = QPointF(0.4, 0.5);
    pf.conf[9] = 0.9f;
    a.pose2d.camera = 0;
    a.pose2d.frames.push_back(pf);
    a.phases.push_back({ analysis::Phase::Impact, 1000000, 1.0f });
    return a;
}

lm::LaunchMonitorReading reading(double clubheadMph)
{
    lm::LaunchMonitorReading r;
    r.deviceShotId  = QStringLiteral("7");
    r.deviceKind    = QStringLiteral("fsx2020");
    r.clubheadSpeed = clubheadMph;
    r.ballSpeed     = 130.0;
    r.launchAngle   = 14.0;
    return r;
}

QJsonObject doc(const QString &dir) { return SwingStore::load(dir); }
QJsonObject review(const QString &dir) { return doc(dir).value(QStringLiteral("review")).toObject(); }

bool hasMetric(const QJsonObject &root, const QString &key)
{
    for (const QJsonValue &v : root.value(QStringLiteral("analysis")).toObject()
                                   .value(QStringLiteral("metrics")).toArray())
        if (v.toObject().value(QStringLiteral("key")).toString() == key)
            return true;
    return false;
}

QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// ── Test 1 ────────────────────────────────────────────────────────────────────────────────────
void testCreate(RecordingRepository &repo)
{
    std::printf("[Test 1] create\n");
    QTemporaryDir t;
    const analysis::SwingAnalysis a = poseAnalysis(71);
    NewRecording rec;
    rec.header     = cameraHeader();
    rec.analysis   = &a;
    rec.reviewClub = QStringLiteral("7 IRON");
    const WriteResult w = repo.create(t.path(), rec);
    check(w.status == WriteResult::Status::Written && w.ok() && w.changed(), "the first write is Written");
    const QJsonObject root = doc(t.path());
    check(root.value(QStringLiteral("schema")).toString() == QLatin1String("pinpoint.swing/2"), "the schema stamped");
    check(root.value(QStringLiteral("streams")).toArray().size() == 1, "the streams written");
    check(root.value(QStringLiteral("analysis")).toObject().contains(QStringLiteral("pose2d")), "the analysis written");
    check(review(t.path()).value(QStringLiteral("club")).toString() == QLatin1String("7 IRON")
              && review(t.path()).value(QStringLiteral("rating")).toInt() == 0,
          "review.club seeded with the session's club");
    check(root.contains(QStringLiteral("summary")), "the summary block rides inside");
    check(!SwingDocWriter::takeJustWritten(t.path()).isEmpty(), "the just-written cache holds it (one reader)");

    QTemporaryDir t2;
    NewRecording bare;
    bare.header = cameraHeader();
    check(repo.create(t2.path(), bare).ok(), "a header-only create");
    check(!doc(t2.path()).contains(QStringLiteral("review")), "no club: no review block");
    check(!doc(t2.path()).contains(QStringLiteral("analysis")), "no analysis: no analysis block");
}

// ── Test 2 ────────────────────────────────────────────────────────────────────────────────────
void testSavePose(RecordingRepository &repo)
{
    std::printf("[Test 2] savePose honoured\n");
    const analysis::SwingAnalysis a = poseAnalysis(60);
    const auto hasPose = [](const QString &dir) {
        const QJsonObject an = doc(dir).value(QStringLiteral("analysis")).toObject();
        return an.contains(QStringLiteral("pose2d"));
    };
    const auto hasPoseStamp = [](const QString &dir) {
        return doc(dir).value(QStringLiteral("analysis")).toObject()
                   .value(QStringLiteral("versions")).toObject().contains(QStringLiteral("pose"));
    };

    QTemporaryDir t;
    NewRecording rec;
    rec.header   = cameraHeader();
    rec.analysis = &a;
    PersistOptions off;
    off.savePose = false;
    check(repo.create(t.path(), rec, off).ok(), "create with savePose off");
    check(!hasPose(t.path()) && !hasPoseStamp(t.path()), "...no pose track and no pose stamp");

    AnalysisOutcome outcome;
    outcome.analysis = &a;
    check(repo.writeAnalysis(t.path(), outcome, PersistOptions{}).ok(), "write-back with savePose on");
    check(hasPose(t.path()) && hasPoseStamp(t.path()), "...the pose track and its stamp written");
    check(repo.writeAnalysis(t.path(), outcome, off).ok(), "write-back with savePose off");
    check(!hasPose(t.path()) && !hasPoseStamp(t.path()),
          "...the pose track and its stamp dropped (re-analysis honours Save pose keypoints)");
}

// ── Test 3 ────────────────────────────────────────────────────────────────────────────────────
void testWriteAnalysis(RecordingRepository &repo)
{
    std::printf("[Test 3] writeAnalysis\n");
    QTemporaryDir t;
    const analysis::SwingAnalysis first = poseAnalysis(40);
    NewRecording rec;
    rec.header = cameraHeader();
    rec.header[QStringLiteral("captureIntegrity")] = QJsonObject{ { QStringLiteral("holes"), 3 } };
    rec.analysis   = &first;
    rec.reviewClub = QStringLiteral("DRIVER");
    check(repo.create(t.path(), rec).ok(), "a captured shot");
    ReviewEdit e;
    e.rating = 4;
    e.note   = QStringLiteral("good tempo");
    check(repo.editReview(t.path(), e).ok(), "rated and noted");
    check(repo.completeCapture(t.path(), reading(92.0)).ok(), "a launch monitor reading folded in");
    check(hasMetric(doc(t.path()), QStringLiteral("lm.clubheadSpeed")), "...its lm. row is in the analysis");

    const analysis::SwingAnalysis second = poseAnalysis(88);
    ImuRefusionVerdict imu;
    imu.ok             = false;
    imu.sourcesChecked = 1;
    imu.worstMaxDeg    = 2.5;
    AnalysisOutcome outcome;
    outcome.analysis     = &second;
    outcome.imuIntegrity = &imu;   // captureIntegrity nullptr: this pass reached no verdict
    const WriteResult w = repo.writeAnalysis(t.path(), outcome);
    check(w.status == WriteResult::Status::Written, "the write-back is Written");
    const QJsonObject root = doc(t.path());
    check(overallScoreFromAnalysis(root.value(QStringLiteral("analysis")).toObject()) == 88, "the analysis replaced");
    check(review(t.path()).value(QStringLiteral("rating")).toInt() == 4
              && review(t.path()).value(QStringLiteral("note")).toString() == QLatin1String("good tempo")
              && review(t.path()).value(QStringLiteral("club")).toString() == QLatin1String("DRIVER"),
          "the review rides through untouched");
    check(root.value(QStringLiteral("streams")).toArray().size() == 1
              && root.value(QStringLiteral("capture")).toObject().value(QStringLiteral("impactUs")).toInt() == 1000000,
          "capture and streams ride through");
    check(root.contains(QStringLiteral("launchMonitor")) && hasMetric(root, QStringLiteral("lm.clubheadSpeed")),
          "the launch monitor block and its rows are carried across");
    check(root.value(QStringLiteral("imuIntegrity")).toObject().value(QStringLiteral("sourcesChecked")).toInt() == 1,
          "this pass's IMU verdict written");
    check(!root.contains(QStringLiteral("captureIntegrity")),
          "the capture-time capture verdict withdrawn (this pass reached none)");
    // The whole-shot gate records are appended once, in the writer: written twice, still one set.
    const QJsonArray gates1 = root.value(QStringLiteral("analysis")).toObject().value(QStringLiteral("gates")).toArray();
    check(repo.writeAnalysis(t.path(), outcome).ok(), "the same write-back again");
    const QJsonArray gates2 = doc(t.path()).value(QStringLiteral("analysis")).toObject()
                                  .value(QStringLiteral("gates")).toArray();
    check(gates1 == gates2, "the gate records do not accumulate");
}

// ── Test 4 ────────────────────────────────────────────────────────────────────────────────────
void testEmptyDocumentGuard(RecordingRepository &repo)
{
    std::printf("[Test 4] the empty-document guard\n");
    QTemporaryDir t;
    const analysis::SwingAnalysis a = poseAnalysis(50);
    AnalysisOutcome outcome;
    outcome.analysis = &a;
    const WriteResult w = repo.writeAnalysis(t.path(), outcome);
    check(w.status == WriteResult::Status::NoDocument && !w.ok() && !w.error.isEmpty(),
          "no document to read: NoDocument, with the reason");
    check(!SwingStore::hasDocument(t.path()), "...and no partial document was written");
    ReviewEdit e;
    e.rating = 3;
    check(repo.editReview(t.path(), e).status == WriteResult::Status::NoDocument
              && !SwingStore::hasDocument(t.path()),
          "a review edit on a folder with no document writes nothing");
}

// ── Test 5 ────────────────────────────────────────────────────────────────────────────────────
void testEditReview(RecordingRepository &repo)
{
    std::printf("[Test 5] editReview merges\n");
    QTemporaryDir t;
    NewRecording rec;
    rec.header = cameraHeader();
    check(repo.create(t.path(), rec).ok(), "a shot nobody reviewed");

    ReviewEdit board;
    board.excluded     = true;
    board.clubOverride = QStringLiteral(" 7 IRON ");
    check(repo.editReview(t.path(), board).status == WriteResult::Status::Written, "the board's annotations");
    check(review(t.path()).value(QStringLiteral("excluded")).toBool()
              && review(t.path()).value(QStringLiteral("clubOverride")).toString() == QLatin1String("7 IRON")
              && !review(t.path()).contains(QStringLiteral("club")),
          "written (trimmed), and the club NOT touched (no stub acquired)");

    ReviewEdit stars;
    stars.rating = 9;
    stars.note   = QStringLiteral("n");
    stars.club   = QStringLiteral("DRIVER");
    check(repo.editReview(t.path(), stars).ok(), "a rating edit");
    check(review(t.path()).value(QStringLiteral("rating")).toInt() == 5, "the rating clamped to 5");
    check(review(t.path()).value(QStringLiteral("excluded")).toBool()
              && review(t.path()).value(QStringLiteral("clubOverride")).toString() == QLatin1String("7 IRON"),
          "a star rating never un-excludes the shot (merged, not replaced)");

    const QByteArray before = bytesOf(SwingStore::ppswPath(t.path()));
    check(repo.editReview(t.path(), board).status == WriteResult::Status::Unchanged,
          "the same annotations again: Unchanged");
    check(bytesOf(SwingStore::ppswPath(t.path())) == before, "...and the file was not rewritten");
    check(repo.editReview(t.path(), stars).status == WriteResult::Status::Written,
          "the same rating again still rewrites (it always has: it re-indexes)");

    ReviewEdit clear;
    clear.excluded     = false;
    clear.clubOverride = QString();
    check(repo.editReview(t.path(), clear).ok(), "the annotations cleared");
    check(!review(t.path()).contains(QStringLiteral("excluded"))
              && !review(t.path()).contains(QStringLiteral("clubOverride"))
              && review(t.path()).value(QStringLiteral("club")).toString() == QLatin1String("DRIVER"),
          "their keys removed, the popover's fields kept");

    QTemporaryDir t2;
    check(repo.create(t2.path(), rec).ok(), "another shot nobody reviewed");
    check(repo.editReview(t2.path(), board).ok() && repo.editReview(t2.path(), clear).ok(),
          "annotated, then cleared");
    check(!doc(t2.path()).contains(QStringLiteral("review")),
          "a block that held only annotations goes away when they are cleared");
}

// ── Test 6 ────────────────────────────────────────────────────────────────────────────────────
void testCompleteCapture(RecordingRepository &repo)
{
    std::printf("[Test 6] completeCapture\n");
    QTemporaryDir t;
    const analysis::SwingAnalysis a = poseAnalysis(55);
    NewRecording rec;
    rec.header     = cameraHeader(QStringLiteral("DRIVER"));
    rec.analysis   = &a;
    rec.reviewClub = QStringLiteral("DRIVER");   // capture's seed
    check(repo.create(t.path(), rec).ok(), "a captured shot, seeded with the session's club");

    const QJsonObject premierClub{ { QStringLiteral("name"), QStringLiteral("7 IRON") },
                                   { QStringLiteral("lengthMm"), 940 } };
    bool replaced = false;
    check(repo.completeCapture(t.path(), reading(85.0), premierClub, &replaced).ok() && replaced,
          "a reading naming another club: follow-club replaces the captured club (IM-4)");
    const QJsonObject root = doc(t.path());
    check(root.value(QStringLiteral("capture")).toObject().value(QStringLiteral("club")).toObject()
                  .value(QStringLiteral("name")).toString() == QLatin1String("7 IRON")
              && review(t.path()).value(QStringLiteral("club")).toString() == QLatin1String("7 IRON"),
          "...capture.club and the seeded review.club move together");
    check(hasMetric(root, QStringLiteral("lm.clubheadSpeed")) && root.contains(QStringLiteral("launchMonitor")),
          "...and the reading's rows and raw block are written");

    ReviewEdit hand;
    hand.rating = 0;
    hand.note   = QString();
    hand.club   = QStringLiteral("PW");
    check(repo.editReview(t.path(), hand).ok(), "the golfer sets the club by hand");
    const QJsonObject driverClub{ { QStringLiteral("name"), QStringLiteral("DRIVER") } };
    check(repo.completeCapture(t.path(), reading(85.0), driverClub, &replaced).ok() && !replaced,
          "follow-club never replaces over a hand-set club");

    // A stream arrival: pending (no file), then landed.
    StreamArrival pending;
    pending.alias            = QStringLiteral("phoneWide");
    pending.origin.transport = QStringLiteral("ppcp");
    pending.origin.transfer  = QStringLiteral("requested");
    check(repo.completeCapture(t.path(), pending).ok(), "a clip asked for");
    const auto phone = [&t] {
        for (const QJsonValue &v : doc(t.path()).value(QStringLiteral("streams")).toArray())
            if (v.toObject().value(QStringLiteral("alias")).toString() == QLatin1String("phoneWide"))
                return v.toObject();
        return QJsonObject();
    };
    check(!phone().isEmpty() && !phone().contains(QStringLiteral("file")),
          "...its element recorded with no file (inert to every reader)");
    StreamArrival landed = pending;
    landed.origin.transfer = QStringLiteral("complete");
    landed.fileName        = QStringLiteral("phoneWide.mov");
    landed.frameTUs        = { 0, 4166, 8333 };
    landed.playbackFps     = 240.0;
    landed.framesClock     = QString::fromLatin1(SwingStore::kFramesClockClip);
    check(repo.completeCapture(t.path(), landed).ok(), "the clip lands");
    check(phone().value(QStringLiteral("file")).toString() == QLatin1String("phoneWide.mov")
              && phone().value(QStringLiteral("frames")).toObject().value(QStringLiteral("t_us")).toArray().size() == 3
              && !SwingStore::streamOnWindowClock(phone()),
          "...the same element names the file, its frames and their clock");
    check(doc(t.path()).value(QStringLiteral("streams")).toArray().size() == 2, "...one element, not two");
    StreamArrival noAlias;
    check(repo.completeCapture(t.path(), noAlias).status == WriteResult::Status::Failed,
          "an arrival with no alias is refused");
}

// ── Test 7 ────────────────────────────────────────────────────────────────────────────────────
void testDeviceOnly(RecordingRepository &repo)
{
    std::printf("[Test 7] a launch-monitor-only shot\n");
    QTemporaryDir t;
    DeviceOnlyMeta meta;
    meta.swingId     = QStringLiteral("swing_0003");
    meta.swingIndex  = 3;
    meta.sessionId   = QStringLiteral("2026-11-14_session_01");
    meta.athleteName = QStringLiteral("Test Golfer");
    meta.club        = QStringLiteral("7 IRON");
    meta.wallclockMs = 1794000000000;
    check(repo.create(t.path(), reading(80.0), meta).ok(), "created from the reading alone");
    check(repo.isDeviceOnly(t.path()), "isDeviceOnly");
    check(hasMetric(doc(t.path()), QStringLiteral("lm.clubheadSpeed")), "its lm. rows written");
    check(repo.create(QTemporaryDir().path() + QStringLiteral("/x"), lm::LaunchMonitorReading{}, meta).status
              == WriteResult::Status::Failed,
          "a reading with no values is refused");

    check(repo.writeAnalysis(t.path(), AnalysisOutcome{}).status == WriteResult::Status::Unchanged,
          "re-analysed: its rows are current, nothing written");
    // Make them stale: a document whose rows were written before a derivation existed.
    QJsonObject root = doc(t.path());
    QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    an[QStringLiteral("metrics")] = QJsonArray{};
    root[QStringLiteral("analysis")] = an;
    check(SwingStore::save(t.path(), root), "its rows emptied (an older document)");
    const QByteArray before = bytesOf(SwingStore::ppswPath(t.path()));
    PersistOptions dry;
    dry.dryRun = true;
    check(repo.writeAnalysis(t.path(), AnalysisOutcome{}, dry).status == WriteResult::Status::Written,
          "a dry run says it WOULD write");
    check(bytesOf(SwingStore::ppswPath(t.path())) == before, "...and writes nothing");
    check(repo.writeAnalysis(t.path(), AnalysisOutcome{}).status == WriteResult::Status::Written
              && hasMetric(doc(t.path()), QStringLiteral("lm.clubheadSpeed")),
          "re-analysed: the rows derived again from the recorded reading");

    QTemporaryDir camera;
    NewRecording rec;
    rec.header = cameraHeader();
    check(repo.create(camera.path(), rec).ok() && !repo.isDeviceOnly(camera.path()),
          "a camera shot is not device-only");
    check(!repo.isDeviceOnly(QTemporaryDir().path()), "nor is a folder with no document");
}

// ── Test 8 ────────────────────────────────────────────────────────────────────────────────────
void writeJsonDoc(const QString &dir, const QJsonObject &root)
{
    QDir().mkpath(dir);
    QFile f(SwingStore::jsonPath(dir));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}

void testConvertAndSidecar(RecordingRepository &repo)
{
    std::printf("[Test 8] convert and the JSON-era sidecar\n");
    QTemporaryDir t;
    QJsonObject legacy = cameraHeader();
    legacy[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/1");
    writeJsonDoc(t.path(), legacy);

    const PersistedShot ps = SwingDocReader::readSwingJson(t.path());
    check(ps.ok, "a JSON-era swing reads");
    check(repo.writeSummarySidecar(ps).status == WriteResult::Status::Written
              && QFile::exists(SwingStore::legacySummaryPath(t.path())),
          "its summary sidecar is written");

    const ConversionResult c = repo.convert(t.path(), /*deleteJson=*/true);
    check(c.outcome == ConversionResult::Outcome::Converted && c.jsonBytes > 0 && c.ppswBytes > 0,
          "converted, proven");
    check(SwingStore::info(t.path()).format == SwingStore::Format::Ppsw
              && !QFile::exists(SwingStore::jsonPath(t.path()))
              && !QFile::exists(SwingStore::legacySummaryPath(t.path())),
          "...swing.ppsw in place, the JSON and its sidecar gone");
    check(repo.convert(t.path(), true).outcome == ConversionResult::Outcome::AlreadyConverted, "again: already");
    check(repo.convert(QTemporaryDir().path(), true).outcome == ConversionResult::Outcome::NoDocument,
          "no document: NoDocument");
    check(repo.writeSummarySidecar(SwingDocReader::readSwingJson(t.path())).status
              == WriteResult::Status::Unchanged,
          "a swing.ppsw has no sidecar: Unchanged");
}

// ── Test 9 ────────────────────────────────────────────────────────────────────────────────────
void testReadOnlyRefused(RecordingRepository &repo)
{
    std::printf("[Test 9] every write refuses a read-only (newer) document\n");
    const analysis::SwingAnalysis a = poseAnalysis(77);

    const auto refusesAll = [&](const QString &label, const QJsonObject &newer) {
        QTemporaryDir t;
        check(SwingStore::save(t.path(), newer), qPrintable(label + QStringLiteral(": saved by a newer build")));
        const QByteArray before = bytesOf(SwingStore::ppswPath(t.path()));
        const auto refused = [&](const WriteResult &w, const char *what) {
            check(w.status == WriteResult::Status::ReadOnly && !w.error.isEmpty()
                      && bytesOf(SwingStore::ppswPath(t.path())) == before,
                  qPrintable(label + QStringLiteral(": ") + QLatin1String(what) + QStringLiteral(" refused, bytes unchanged")));
        };
        NewRecording rec;
        rec.header   = cameraHeader();
        rec.analysis = &a;
        refused(repo.create(t.path(), rec), "create over it");
        DeviceOnlyMeta meta;
        meta.swingId = QStringLiteral("swing_0001");
        refused(repo.create(t.path(), reading(80.0), meta), "a device-only create over it");
        AnalysisOutcome outcome;
        outcome.analysis = &a;
        refused(repo.writeAnalysis(t.path(), outcome), "writeAnalysis");
        PersistOptions dry;
        dry.dryRun = true;
        refused(repo.writeAnalysis(t.path(), outcome, dry), "a dry-run writeAnalysis");
        refused(repo.writeAnalysis(t.path(), AnalysisOutcome{}), "writeAnalysis with no analysis (the rows)");
        ReviewEdit stars;
        stars.rating = 5;
        refused(repo.editReview(t.path(), stars), "editReview (a rating)");
        ReviewEdit board;
        board.excluded = true;
        refused(repo.editReview(t.path(), board), "editReview (an annotation)");
        bool replaced = true;
        refused(repo.completeCapture(t.path(), reading(90.0), QJsonObject{ { QStringLiteral("name"), QStringLiteral("PW") } },
                                     &replaced),
                "completeCapture (a reading)");
        check(!replaced, qPrintable(label + QStringLiteral(": ...and no club was replaced")));
        StreamArrival arrival;
        arrival.alias = QStringLiteral("phoneWide");
        refused(repo.completeCapture(t.path(), arrival), "completeCapture (a stream arrival)");
        AthleteRecordFacts facts;
        facts.heightM = 1.80;
        refused(repo.backfillAnalysisContext(t.path(), facts), "backfillAnalysisContext");
        refused(repo.backfillAnalysisContext(t.path(), facts, dry), "a dry-run backfillAnalysisContext");
    };

    QJsonObject newerSchema = cameraHeader();
    newerSchema[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/9");
    refusesAll(QStringLiteral("a newer document schema"), newerSchema);

    QJsonObject newerBlock = cameraHeader();
    newerBlock[QStringLiteral("analysisContext")] =
        QJsonObject{ { QStringLiteral("schema"), QStringLiteral("pinpoint.analysisContext/7") } };
    refusesAll(QStringLiteral("a newer analysisContext block"), newerBlock);

    // The same rule for the conversion of a JSON-era document: the JSON is left exactly as it was.
    QTemporaryDir j;
    writeJsonDoc(j.path(), newerSchema);
    const QByteArray json = bytesOf(SwingStore::jsonPath(j.path()));
    const ConversionResult c = repo.convert(j.path(), true);
    check(c.outcome == ConversionResult::Outcome::Failed && !c.error.isEmpty(), "convert of a newer JSON document: Failed");
    check(bytesOf(SwingStore::jsonPath(j.path())) == json && !QFile::exists(SwingStore::ppswPath(j.path())),
          "...the JSON untouched, no swing.ppsw written");

    // And this build's own documents stay writable.
    QTemporaryDir ok;
    check(SwingStore::save(ok.path(), cameraHeader()), "a document of this build's schema");
    ReviewEdit stars;
    stars.rating = 2;
    check(repo.editReview(ok.path(), stars).status == WriteResult::Status::Written, "...is written");
}

// ── Test 10 ───────────────────────────────────────────────────────────────────────────────────
void testReanalyseAndPersist(RecordingRepository &repo)
{
    std::printf("[Test 10] reanalyseAndPersist / persistReanalysis\n");
    using namespace pinpoint::analysis;
    QTemporaryDir t;
    const SwingAnalysis captured = poseAnalysis(30);
    NewRecording rec;
    rec.header     = cameraHeader();
    rec.header[QStringLiteral("imuIntegrity")] = QJsonObject{ { QStringLiteral("sourcesChecked"), 2 } };
    rec.analysis   = &captured;
    rec.reviewClub = QStringLiteral("DRIVER");
    check(repo.create(t.path(), rec).ok(), "a captured shot");

    g_reanalysis = ReanalyzeResult{};
    g_reanalysis.ok = true;
    g_reanalysis.analysis.score  = 91;
    g_reanalysis.analysis.detail = std::make_shared<SwingAnalysis>(poseAnalysis(91));
    CaptureIntegrityVerdict cv;
    cv.camerasChecked = 1;
    g_reanalysis.captureIntegrity = cv;   // imuIntegrity nullopt: this pass could not check one
    PersistOptions off;
    off.savePose = false;
    g_reanalysisCalls = 0;
    const PersistedReanalysis p = reanalyseAndPersist(repo, t.path(), off);
    check(g_reanalysisCalls == 1 && p.ok() && p.analysed && !p.deviceOnly && p.score == 91, "re-analysed and persisted");
    const QJsonObject root = doc(t.path());
    check(overallScoreFromAnalysis(root.value(QStringLiteral("analysis")).toObject()) == 91, "the new analysis on disk");
    check(!root.value(QStringLiteral("analysis")).toObject().contains(QStringLiteral("pose2d")),
          "Save pose keypoints off, the pose newly inferred: not written back");
    check(root.contains(QStringLiteral("captureIntegrity")) && !root.contains(QStringLiteral("imuIntegrity")),
          "this pass's verdicts: the capture one set, the unreachable IMU one withdrawn");
    check(review(t.path()).value(QStringLiteral("club")).toString() == QLatin1String("DRIVER"), "the review kept");

    g_reanalysis = ReanalyzeResult{};
    g_reanalysis.error = QStringLiteral("no frames");
    const QByteArray before = bytesOf(SwingStore::ppswPath(t.path()));
    const PersistedReanalysis failed = reanalyseAndPersist(repo, t.path(), PersistOptions{});
    check(!failed.ok() && !failed.analysed && failed.analysisError == QLatin1String("no frames")
              && bytesOf(SwingStore::ppswPath(t.path())) == before,
          "an analysis that fails writes nothing");

    QTemporaryDir lmOnly;
    DeviceOnlyMeta meta;
    meta.swingId = QStringLiteral("swing_0002");
    check(repo.create(lmOnly.path(), reading(70.0), meta).ok(), "a launch-monitor-only shot");
    g_reanalysisCalls = 0;
    const PersistedReanalysis d = reanalyseAndPersist(repo, lmOnly.path(), PersistOptions{});
    check(d.ok() && d.deviceOnly && g_reanalysisCalls == 0 && d.write.status == WriteResult::Status::Unchanged,
          "...is not analysed: its rows are checked (current, Unchanged)");

    QTemporaryDir gone;
    ReanalyzeResult r;
    r.ok = true;
    r.analysis.detail = std::make_shared<SwingAnalysis>(poseAnalysis(10));
    const PersistedReanalysis g = persistReanalysis(repo, gone.path(), &r, PersistOptions{});
    check(!g.ok() && g.analysed && g.write.status == WriteResult::Status::NoDocument
              && !SwingStore::hasDocument(gone.path()),
          "the document went missing mid-batch: NoDocument, nothing written");
}

// ── Test 11 ───────────────────────────────────────────────────────────────────────────────────
// Save pose keypoints governs NEWLY INFERRED keypoints only (Mark, 14 Nov 2026): a pose this pass
// reused from the document is written back unchanged whatever the setting says.
analysis::SwingAnalysis withDtl(int score, double x)
{
    analysis::SwingAnalysis a = poseAnalysis(score);
    a.pose2d.frames.front().kp[9] = QPointF(x, 0.5);
    a.versions.poseDtl = 1;
    analysis::PoseFrame2D pf;
    pf.t_us = 1000000;
    pf.kp[10] = QPointF(x, 0.6);
    pf.conf[10] = 0.8f;
    a.poseDtl.camera = 1;
    a.poseDtl.frames.push_back(pf);
    return a;
}

void testPoseReuseKeepsKeypoints(RecordingRepository &repo)
{
    std::printf("[Test 11] savePose governs newly inferred keypoints only\n");
    using namespace pinpoint::analysis;
    const auto block = [](const QString &dir, const char *k) {
        return doc(dir).value(QStringLiteral("analysis")).toObject().value(QLatin1String(k)).toObject();
    };
    const auto stamp = [](const QString &dir, const char *k) {
        return doc(dir).value(QStringLiteral("analysis")).toObject().value(QStringLiteral("versions")).toObject()
                   .value(QLatin1String(k)).toObject();
    };
    QTemporaryDir t;
    const SwingAnalysis captured = withDtl(30, 0.40);
    NewRecording rec;
    rec.header   = cameraHeader();
    rec.analysis = &captured;
    check(repo.create(t.path(), rec).ok(), "a captured shot with a face-on and a DTL pose");
    const QJsonObject pose0 = block(t.path(), "pose2d"), dtl0 = block(t.path(), "poseDtl");
    const QJsonObject poseStamp0 = stamp(t.path(), "pose"), dtlStamp0 = stamp(t.path(), "poseDtl");
    check(!pose0.isEmpty() && !dtl0.isEmpty() && !poseStamp0.isEmpty() && !dtlStamp0.isEmpty(),
          "...both tracks and their stamps on disk");

    PersistOptions off;
    off.savePose = false;
    AnalysisOutcome outcome;

    // Both reused (inference not re-run): kept byte-equal with the setting off.
    SwingAnalysis reused = withDtl(91, 0.40);   // the reloaded tracks, as the reuse gate hands them on
    reused.path.reuse.insert(QStringLiteral("pose"), QStringLiteral("reused"));
    reused.path.reuse.insert(QStringLiteral("poseDtl"), QStringLiteral("reused"));
    outcome.analysis = &reused;
    check(repo.writeAnalysis(t.path(), outcome, off).ok(), "a re-analysis that reused both poses, setting off");
    check(block(t.path(), "pose2d") == pose0 && block(t.path(), "poseDtl") == dtl0,
          "...both tracks written back byte-equal: never deleted");
    check(stamp(t.path(), "pose") == poseStamp0 && stamp(t.path(), "poseDtl") == dtlStamp0,
          "...and their stamps kept, so the next re-analysis reuses them again");
    check(overallScoreFromAnalysis(doc(t.path()).value(QStringLiteral("analysis")).toObject()) == 91,
          "...while the analysis itself is the new one");

    // The face-on pose reused, the DTL pose re-inferred (its stage version moved): setting off.
    SwingAnalysis mixed = withDtl(92, 0.40);
    mixed.poseDtl.frames.front().kp[10] = QPointF(0.77, 0.6);
    mixed.path.reuse.insert(QStringLiteral("pose"), QStringLiteral("reused"));
    mixed.path.reuse.insert(QStringLiteral("poseDtl"), QStringLiteral("version 1, want 2"));
    outcome.analysis = &mixed;
    check(repo.writeAnalysis(t.path(), outcome, off).ok(), "a re-analysis that re-inferred the DTL pose, setting off");
    check(block(t.path(), "pose2d") == pose0, "...the reused face-on track kept byte-equal");
    check(block(t.path(), "poseDtl").isEmpty() && stamp(t.path(), "poseDtl").isEmpty(),
          "...the newly inferred DTL track not written, and the superseded one gone with its stamp");

    // Re-inferred (the pose stage version moved), setting ON: the new pose is written.
    SwingAnalysis fresh = withDtl(93, 0.55);
    fresh.path.reuse.insert(QStringLiteral("pose"), QStringLiteral("version 1, want 2"));
    fresh.path.reuse.insert(QStringLiteral("poseDtl"), QStringLiteral("no record"));
    outcome.analysis = &fresh;
    check(repo.writeAnalysis(t.path(), outcome, PersistOptions{}).ok(), "a re-analysis that re-inferred both, setting on");
    const QJsonObject pose1 = block(t.path(), "pose2d");
    check(!pose1.isEmpty() && pose1 != pose0 && !block(t.path(), "poseDtl").isEmpty(),
          "...the newly inferred tracks written");

    // Re-inferred, setting OFF: the new keypoints are not kept, and the superseded ones go.
    outcome.analysis = &fresh;
    check(repo.writeAnalysis(t.path(), outcome, off).ok(), "the same re-inference, setting off");
    check(block(t.path(), "pose2d").isEmpty() && block(t.path(), "poseDtl").isEmpty()
              && stamp(t.path(), "pose").isEmpty() && stamp(t.path(), "poseDtl").isEmpty(),
          "...no pose and no pose stamps (as a live shot with the setting off)");
}

// ── Test 12 ───────────────────────────────────────────────────────────────────────────────────
double lowPointOf(const QString &dir)
{
    for (const QJsonValue &v : doc(dir).value(QStringLiteral("analysis")).toObject()
                                   .value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("key")).toString() == QLatin1String("lowPointFromAttack"))
            return m.value(QStringLiteral("phaseSamples")).toArray().first().toObject()
                       .value(QStringLiteral("value")).toDouble();
    }
    return std::nan("");
}

double expectedLowPoint(double attackDeg, const QString &club, ClubSet set)
{
    const ClubStandard st = standardClub(club, set);
    return analysis::lmLowPointFromAttack(attackDeg, st.lengthMm / 1000.0 + analysis::kArcArmM).value;
}

void testLowPointTakesTheEffectiveClub(RecordingRepository &repo)
{
    std::printf("[Test 12] the LM low point takes the effective club and the recorded club set\n");
    lm::LaunchMonitorReading r = reading(80.0);
    r.attackAngle = -4.0;
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };

    // A device-only shot declared as a 7 iron, then corrected on the board to a pitching wedge.
    QTemporaryDir t;
    DeviceOnlyMeta meta;
    meta.swingId = QStringLiteral("swing_0004");
    meta.swingIndex = 4;
    meta.sessionId = QStringLiteral("2026-11-14_session_01");
    meta.club = QStringLiteral("7 IRON");
    check(repo.create(t.path(), r, meta).ok(), "a device-only shot, declared 7 iron");
    const double lp7 = lowPointOf(t.path());
    check(near(lp7, expectedLowPoint(-4.0, QStringLiteral("7 IRON"), ClubSet::Men)),
          "...its low point from the declared 7 iron's standard length (as before)");
    ReviewEdit board;
    board.clubOverride = QStringLiteral("PITCHING WEDGE");
    check(repo.editReview(t.path(), board).ok(), "corrected on the board to a pitching wedge");
    // Make the rows stale so the re-analysis derives them again (an edit of the review rewrites none).
    QJsonObject root = doc(t.path());
    QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    an[QStringLiteral("metrics")] = QJsonArray{};
    root[QStringLiteral("analysis")] = an;
    check(SwingStore::save(t.path(), root), "its rows emptied");
    check(repo.writeAnalysis(t.path(), AnalysisOutcome{}).status == WriteResult::Status::Written,
          "re-analysed: the rows derived again");
    const double lpPw = lowPointOf(t.path());
    check(near(lpPw, expectedLowPoint(-4.0, QStringLiteral("PITCHING WEDGE"), ClubSet::Men)) && !near(lpPw, lp7),
          "...the low point now from the CORRECTED club's standard length");

    // A camera shot whose recorded analysis context says the women's set.
    QTemporaryDir w;
    NewRecording rec;
    rec.header = cameraHeader(QStringLiteral("7 IRON"));
    recording::AnalysisContext ctx;
    ctx.handedness = recording::Handedness::Right;
    ctx.heightM    = 1.65;
    ctx.clubSet    = ClubSet::Women;
    ctx.club.name  = QStringLiteral("7 IRON");
    ctx.club.standardLengthMm = standardClub(ctx.club.name, ctx.clubSet).lengthMm;
    rec.header[QStringLiteral("analysisContext")] = analysisContextJson(ctx);
    const analysis::SwingAnalysis a = poseAnalysis(50);   // the rows go into an analysis block
    rec.analysis = &a;
    check(repo.create(w.path(), rec).ok(), "a camera shot, women's set recorded");
    check(repo.completeCapture(w.path(), r).ok(), "a reading folded in");
    const double lpW = lowPointOf(w.path());
    check(near(lpW, expectedLowPoint(-4.0, QStringLiteral("7 IRON"), ClubSet::Women)) && !near(lpW, lp7),
          "...its low point from the women's 7 iron");
}


// ── Test 13 ───────────────────────────────────────────────────────────────────────────────────
// A pre-step-1 camera shot: no analysisContext block, its session folder dated 20 Oct 2026.
QJsonObject legacyShot(const QString &club = QStringLiteral("7 IRON"))
{
    QJsonObject h = cameraHeader(club);
    h[QStringLiteral("athlete")] = QJsonObject{ { QStringLiteral("name"), QStringLiteral("Jess Ratcliffe") },
                                                { QStringLiteral("handedness"), QStringLiteral("Right") } };
    h[QStringLiteral("session")] = QJsonObject{ { QStringLiteral("dir"), QStringLiteral("2026-10-20_Jess-Ratcliffe_Wrist_01") } };
    return h;
}

void testBackfillAnalysisContext(RecordingRepository &repo)
{
    std::printf("[Test 13] the step-1 backfill: height and club set into a document with no block\n");
    const auto ctxBlock = [](const QString &dir) { return doc(dir).value(QStringLiteral("analysisContext")).toObject(); };
    PersistOptions dry;
    dry.dryRun = true;

    QTemporaryDir t;
    check(SwingStore::save(t.path(), legacyShot()), "a pre-step-1 shot (no analysisContext)");
    const QByteArray before = bytesOf(SwingStore::ppswPath(t.path()));
    AthleteRecordFacts jess;
    jess.heightM = 1.60;
    jess.sex     = QStringLiteral("female");
    const WriteResult d = repo.backfillAnalysisContext(t.path(), jess, dry);
    check(d.status == WriteResult::Status::Written && bytesOf(SwingStore::ppswPath(t.path())) == before,
          "a dry run says Written (would be) and leaves the bytes as they were");
    check(repo.backfillAnalysisContext(t.path(), jess).status == WriteResult::Status::Written, "the backfill is Written");
    const QJsonObject b = ctxBlock(t.path());
    const QJsonObject ba = b.value(QStringLiteral("athlete")).toObject();
    const QJsonObject bc = b.value(QStringLiteral("club")).toObject();
    check(b.value(QStringLiteral("schema")).toString() == QLatin1String("pinpoint.analysisContext/1"), "...the block schema-tagged");
    check(std::abs(ba.value(QStringLiteral("heightM")).toDouble() - 1.60) < 1e-12
              && ba.value(QStringLiteral("clubSet")).toString() == QLatin1String("women")
              && ba.value(QStringLiteral("handedness")).toString() == QLatin1String("Right"),
          "...the record's height and the women's set; handedness as the document said");
    check(bc.value(QStringLiteral("name")).toString() == QLatin1String("7 IRON")
              && bc.value(QStringLiteral("lengthMm")).toDouble() == 1143.0
              && bc.value(QStringLiteral("standardLengthMm")).toDouble()
                     == double(standardClub(QStringLiteral("7 IRON"), ClubSet::Women, 1.60).lengthMm),
          "...the captured club as recorded, its standard length for the women's set");
    check(b.value(QStringLiteral("bag")).toObject().isEmpty(), "...no bag (none was recorded)");
    const AnalysisContextRead read = readAnalysisContext(doc(t.path()));
    check(read.recorded && !read.readOnly && read.context.clubSet == ClubSet::Women, "...read back as the recorded context");
    const QByteArray after = bytesOf(SwingStore::ppswPath(t.path()));
    check(repo.backfillAnalysisContext(t.path(), jess).status == WriteResult::Status::Unchanged
              && bytesOf(SwingStore::ppswPath(t.path())) == after,
          "again: Unchanged, a recorded block is never rewritten");

    // A height the document recorded at capture wins over the record's.
    QTemporaryDir h;
    QJsonObject withHeight = legacyShot();
    QJsonObject ath = withHeight.value(QStringLiteral("athlete")).toObject();
    ath[QStringLiteral("heightM")] = 1.75;
    withHeight[QStringLiteral("athlete")] = ath;
    check(SwingStore::save(h.path(), withHeight), "a shot whose document recorded a height");
    check(repo.backfillAnalysisContext(h.path(), jess).ok()
              && ctxBlock(h.path()).value(QStringLiteral("athlete")).toObject().value(QStringLiteral("heightM")).toDouble() == 1.75,
          "...keeps its own 1.75 m, not the record's");

    // Facts that add nothing (no height, a man's set as the legacy context already reads): no block.
    QTemporaryDir n;
    check(SwingStore::save(n.path(), legacyShot()), "another pre-step-1 shot");
    const QByteArray nb = bytesOf(SwingStore::ppswPath(n.path()));
    AthleteRecordFacts nothing;
    nothing.sex = QStringLiteral("male");
    check(repo.backfillAnalysisContext(n.path(), nothing).status == WriteResult::Status::Unchanged
              && bytesOf(SwingStore::ppswPath(n.path())) == nb,
          "facts that add nothing: Unchanged, no block frozen at \"unknown\"");

    // A junior is decided on the SWING'S date: 17 on 20 Oct 2026.
    QTemporaryDir j;
    check(SwingStore::save(j.path(), legacyShot()), "a junior's pre-step-1 shot");
    AthleteRecordFacts junior;
    junior.heightM = 1.50;
    junior.sex     = QStringLiteral("male");
    junior.dobIso  = QStringLiteral("2009-06-01");
    check(repo.backfillAnalysisContext(j.path(), junior).ok()
              && ctxBlock(j.path()).value(QStringLiteral("athlete")).toObject().value(QStringLiteral("clubSet")).toString()
                     == QLatin1String("junior")
              && ctxBlock(j.path()).value(QStringLiteral("club")).toObject().value(QStringLiteral("standardLengthMm")).toDouble()
                     == double(standardClub(QStringLiteral("7 IRON"), ClubSet::Junior, 1.50).lengthMm),
          "...the junior set, its standard length from the height");
    // No session folder: the capture's wallclock date. 18 on 2 Jun 2027 → the men's set.
    QTemporaryDir w;
    QJsonObject noSession = legacyShot();
    noSession.remove(QStringLiteral("session"));
    QJsonObject clock = noSession.value(QStringLiteral("clock")).toObject();
    clock[QStringLiteral("wallclock")] = QStringLiteral("2027-06-02T09:00:00.000Z");
    noSession[QStringLiteral("clock")] = clock;
    check(SwingStore::save(w.path(), noSession), "a shot with no session folder, captured 2 Jun 2027");
    check(repo.backfillAnalysisContext(w.path(), junior).ok()
              && ctxBlock(w.path()).value(QStringLiteral("athlete")).toObject().value(QStringLiteral("clubSet")).toString()
                     == QLatin1String("men"),
          "...18 on its wallclock date: the men's set");

    // No document: NoDocument, nothing created.
    QTemporaryDir e;
    check(repo.backfillAnalysisContext(e.path(), jess).status == WriteResult::Status::NoDocument
              && !SwingStore::hasDocument(e.path()),
          "no document: NoDocument, nothing written");
}

} // namespace

int main()
{
    std::printf("=== RecordingRepository (step 1.4) ===\n");
    SwingStoreRecordingRepository repo;
    testCreate(repo);
    testSavePose(repo);
    testWriteAnalysis(repo);
    testEmptyDocumentGuard(repo);
    testEditReview(repo);
    testCompleteCapture(repo);
    testDeviceOnly(repo);
    testConvertAndSidecar(repo);
    testReadOnlyRefused(repo);
    testReanalyseAndPersist(repo);
    testPoseReuseKeepsKeypoints(repo);
    testLowPointTakesTheEffectiveClub(repo);
    testBackfillAnalysisContext(repo);
    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
