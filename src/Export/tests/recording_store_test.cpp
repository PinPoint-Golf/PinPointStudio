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

// THE STORE SIDE OF THE RECORDING (refactor step 1.2; docs/developer/information_model.md §2,
// IM-1): the one header writer, the analysisContext block, the Recording loader.
//
//   Test 1  Recording → disk → Recording equal: the synthetic swing (recording_fixture.h) written
//           as capture writes it and loaded back is the same Recording, part by part (times window-
//           relative, t0Us 0, frames from the raw sidecar).
//   Test 2  the analysisContext block: value → JSON → value equal; capture's context from the
//           export job (the club set on the day, the bag canonicalised, the standard length).
//   Test 3  the full and the analysis-only headers are the same header but for the media (streams,
//           and the thumbnail that points into them); the placeholder writes the schema and an empty
//           streams[] only; the device-only shot's header is the same writer's.
//   Test 4  a pre-step-1 document → the legacy context, as today's re-analysis read it: the
//           standard length from the TRIMMED captured club name, only when that names a standard
//           club; the empty-popover and the seeded-DRIVER cases give today's standard length.
//   Test 5  a newer schema → read-only (the document's, or the block's); a block without a schema
//           → read-only and the legacy context.
//   Test 6  the club review as stored (correction = review.clubOverride, declared = review.club).
//
//   cmake --build build/tests --target recording_store_test -j 6
//   ctest --test-dir build/tests -R '^recording_store_test$' --output-on-failure

#include "../recording_header.h"
#include "../recording_loader.h"
#include "../swing_doc.h"
#include "../swing_exporter.h"
#include "../swing_store.h"
#include "../../Analysis/analysis_job_builder.h"
#include "../../Analysis/effective_context.h"
#include "../../Analysis/tests/recording_fixture.h"
#include "../../Buffer/swing_window.h"
#include "../../Core/club_standards.h"
#include "../../Core/club_vocabulary.h"
#include "../../LaunchMonitor/launch_monitor_reading.h"
#include "../../LaunchMonitor/gcquad_csv_parser.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::analysis;
using namespace recording_fixture;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

// ── Field-by-field comparison ─────────────────────────────────────────────────────────────────
bool sameGeometry(const rec::ClubGeometry &a, const rec::ClubGeometry &b)
{
    return a.lengthMm == b.lengthMm && a.lengthKnown == b.lengthKnown && a.shaftType == b.shaftType
        && a.bandCentersMm == b.bandCentersMm && a.hoselFromButtMm == b.hoselFromButtMm
        && a.shaftLengthMm == b.shaftLengthMm && a.handsEndMm == b.handsEndMm;
}

QString contextDiff(const rec::AnalysisContext &a, const rec::AnalysisContext &b)
{
#define DIFF(cond, name) do { if (!(cond)) return QStringLiteral(name); } while (0)
    DIFF(a.handedness == b.handedness, "context.handedness");
    DIFF(a.heightM == b.heightM, "context.heightM");
    DIFF(a.clubSet == b.clubSet, "context.clubSet");
    DIFF(a.club.name == b.club.name, "context.club.name");
    DIFF(sameGeometry(a.club.geometry, b.club.geometry), "context.club.geometry");
    DIFF(a.club.lengthPrior.px == b.club.lengthPrior.px && a.club.lengthPrior.varPx == b.club.lengthPrior.varPx
         && a.club.lengthPrior.n == b.club.lengthPrior.n, "context.club.lengthPrior");
    DIFF(a.club.standardLengthMm == b.club.standardLengthMm, "context.club.standardLengthMm");
    DIFF(a.bag.keys() == b.bag.keys(), "context.bag keys");
    for (auto it = a.bag.cbegin(); it != a.bag.cend(); ++it)
        DIFF(sameGeometry(it.value(), b.bag.value(it.key())), "context.bag geometry");
    return QString();
}

QString recordingDiff(const rec::Recording &a, const rec::Recording &b)
{
    DIFF(a.t0Us == b.t0Us, "t0Us");
    DIFF(a.sources.cameras.size() == b.sources.cameras.size(), "cameras.size");
    for (size_t i = 0; i < a.sources.cameras.size(); ++i) {
        const rec::CameraStream &x = a.sources.cameras[i], &y = b.sources.cameras[i];
        DIFF(x.source == y.source && x.perspective == y.perspective && x.mirrored == y.mirrored
             && x.fixedInPlace == y.fixedInPlace && x.frames == y.frames, "cameras[i]");
    }
    DIFF(a.sources.imuLanes == b.sources.imuLanes, "imuLanes");
    DIFF(a.capture.sessionType == b.capture.sessionType, "capture.sessionType");
    DIFF(a.capture.shotSource == b.capture.shotSource, "capture.shotSource");
    DIFF(a.capture.impactUs == b.capture.impactUs, "capture.impactUs");
    DIFF(a.capture.motionCaptureQuality == b.capture.motionCaptureQuality, "capture.motionCaptureQuality");
    DIFF(a.ball.track.camera == b.ball.track.camera, "ball.track.camera");
    DIFF(a.ball.track.frames.size() == b.ball.track.frames.size(), "ball.track.frames.size");
    for (size_t i = 0; i < a.ball.track.frames.size(); ++i) {
        const BallSample2D &x = a.ball.track.frames[i], &y = b.ball.track.frames[i];
        DIFF(x.t_us == y.t_us && x.found == y.found && x.center == y.center && x.radiusNorm == y.radiusNorm
             && x.conf == y.conf, "ball.track.frames[i]");
    }
    DIFF(a.ball.track.launchTUs == b.ball.track.launchTUs, "ball.track.launchTUs");
    DIFF(a.ball.track.launchCenter == b.ball.track.launchCenter, "ball.track.launchCenter");
    DIFF(a.ball.searchRoi == b.ball.searchRoi, "ball.searchRoi");
    DIFF(a.ball.baseline.isValid() == b.ball.baseline.isValid(), "ball.baseline");
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
    DIFF(a.integrity.hostOrientationFilter == b.integrity.hostOrientationFilter, "integrity.hostOrientationFilter");
    DIFF(a.integrity.windowStartUs == b.integrity.windowStartUs, "integrity.windowStartUs");
    DIFF(a.integrity.windowEndUs == b.integrity.windowEndUs, "integrity.windowEndUs");
    DIFF(a.integrity.offClockCameras == b.integrity.offClockCameras, "integrity.offClockCameras");
#undef DIFF
    return contextDiff(a.context, b.context);
}

// ── Test 1 ────────────────────────────────────────────────────────────────────────────────────
void testRecordingRoundTrip()
{
    std::printf("[Test 1] Recording -> disk -> Recording\n");
    rec::Recording live = liveRecording(kDiskCam, kDiskImu);
    // The recorded ball stream carries no camera; the loader leaves it unset, as re-analysis
    // always has (recording_contract_test Test 1b says why).
    live.ball.track.camera = kInvalidSourceId;

    QTemporaryDir dir;
    QString err;
    check(dir.isValid() && writeSyntheticSwing(dir.path(), live, {}, &err), "written");
    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok, "loaded");
    if (!lr.ok) { std::printf("    %s\n", lr.error.toUtf8().constData()); return; }

    // What a store gives back: the same facts on the loaded window's own clock (t0 0; the entries
    // were written window-relative), frames from the raw sidecar.
    rec::Recording expected = live;
    expected.t0Us = 0;
    for (rec::CameraStream &c : expected.sources.cameras) c.frames = QStringLiteral("raw");

    const QString diff = recordingDiff(expected, lr.recording);
    if (!diff.isEmpty()) std::printf("    first difference: %s\n", diff.toUtf8().constData());
    check(diff.isEmpty(), "every part of the Recording equal");
    check(lr.recording.window == lr.window.get() && lr.window, "the Recording points into the loaded window");
    check(lr.window && lr.window->startTimestampUs() == 1 && lr.window->endTimestampUs() == kWindowEnd - kWindowStart - 1,
          "the window spans the samples written (window-relative)");
    check(lr.usedRaw && lr.contextRecorded && !lr.readOnly, "raw sidecar used; context recorded; writable");
    check(lr.analysisOriginUs == kWindowStart, "an unstamped analysis block reads against clock.t0_us");
    check(lr.review.correction.isEmpty() && lr.review.declared.isEmpty(), "no review block, no club edits");

    // Reading never writes: the document is byte-for-byte what was written.
    const QJsonObject before = SwingStore::load(dir.path());
    (void)loadRecording(dir.path());
    check(SwingStore::load(dir.path()) == before, "loading leaves the document unchanged");
}

// ── Test 2 ────────────────────────────────────────────────────────────────────────────────────
SwingExportJob exportJob()
{
    SwingExportJob j;
    j.swingId = QStringLiteral("swing_0007"); j.swingIndex = 7;
    j.athleteName = QStringLiteral("Jess"); j.athleteUuid = QStringLiteral("uuid-jess");
    j.handedness = QStringLiteral("Left"); j.athleteHeightM = 1.65;
    j.sessionId = QStringLiteral("2026-11-14_Jess_Swing_01");
    j.sessionType = 0; j.shotSource = 4; j.impactUs = 1'234'567;
    j.swingDetectionSensitivity = QStringLiteral("Medium"); j.motionCaptureQuality = QStringLiteral("High");
    j.micTravelUs = 2915;
    j.clubName = QStringLiteral("7 IRON"); j.clubLengthM = 0.0;          // a club with no recorded length
    j.bandCentersMm = { 300.0, 500.0 }; j.shaftType = QStringLiteral("graphite");
    j.priorClubLenPx = 305.0; j.priorClubLenVarPx = 16.0; j.priorClubLenN = 3;
    j.wallclockAnchorUtc = QDateTime::fromMSecsSinceEpoch(1'763'100'000'000LL, QTimeZone::UTC);
    return j;
}

void testContextBlock()
{
    std::printf("[Test 2] the analysisContext block\n");
    const rec::AnalysisContext c = syntheticContext();
    const QJsonObject block = analysisContextJson(c);
    check(block.value(QStringLiteral("schema")).toString() == QString::fromLatin1(kAnalysisContextSchema),
          "schema-tagged pinpoint.analysisContext/1");
    check(!block.value(QStringLiteral("athlete")).toObject().contains(QStringLiteral("sex"))
              && !block.value(QStringLiteral("athlete")).toObject().contains(QStringLiteral("dob")),
          "no sex, no date of birth (the cohort recorded is the club set, IM-7)");
    const QString d = contextDiff(c, analysisContextFromJson(block));
    if (!d.isEmpty()) std::printf("    first difference: %s\n", d.toUtf8().constData());
    check(d.isEmpty(), "value -> JSON -> value equal (bag and prior included)");

    // Capture's context from the export job: the women's set for a female adult on the day, the
    // bag canonicalised, the standard length of the captured club for that set and height.
    SwingExportJob j = exportJob();
    const QVariantMap bag{
        { QStringLiteral("7-iron"), QVariantMap{ { QStringLiteral("lengthMm"), 0 },
                                                 { QStringLiteral("bandCentersMm"), QVariantList{ 300.0, 500.0 } } } },
        { QStringLiteral("9 IRON"), QVariantMap{ { QStringLiteral("lengthMm"), 900 },
                                                 { QStringLiteral("shaftType"), QStringLiteral("steel") },
                                                 { QStringLiteral("hoselFromButtMm"), 840.0 } } },
    };
    const QDate on(2026, 11, 14);
    const rec::AnalysisContext cap = captureAnalysisContext(j, QStringLiteral("female"),
                                                            QStringLiteral("1990-05-01"), on, bag);
    check(cap.handedness == rec::Handedness::Left && cap.heightM == 1.65 && cap.clubSet == ClubSet::Women,
          "handedness, height, the women's set");
    check(cap.club.name == QStringLiteral("7 IRON") && !cap.club.geometry.lengthKnown
              && cap.club.geometry.bandCentersMm == std::vector<double>{ 300.0, 500.0 },
          "the captured club with the export job's geometry");
    check(cap.club.lengthPrior.present() && cap.club.lengthPrior.n == 3, "the length prior the live fuse used");
    check(cap.club.standardLengthMm == standardClub(QStringLiteral("7 IRON"), ClubSet::Women, 1.65).lengthMm,
          "the standard length for the women's set");
    check(cap.bag.keys() == QStringList({ QStringLiteral("7 IRON"), QStringLiteral("9 IRON") })
              && cap.bag.value(QStringLiteral("9 IRON")).lengthKnown
              && cap.bag.value(QStringLiteral("9 IRON")).hoselFromButtMm == 840.0
              && !cap.bag.value(QStringLiteral("7 IRON")).lengthKnown,
          "the bag keyed by canonical id, lengthKnown from the record");
    check(captureAnalysisContext(j, QStringLiteral("male"), QStringLiteral("2012-01-01"), on, {}).clubSet == ClubSet::Junior,
          "under 18 on the day: the junior set");
    j.clubName.clear(); j.bandCentersMm.clear(); j.priorClubLenN = 0;
    const rec::AnalysisContext none = captureAnalysisContext(j, QString(), QString(), on, {});
    check(none.club.name.isEmpty() && none.club.standardLengthMm == 0.0 && none.clubSet == ClubSet::Men,
          "no club: undeclared (never the stub), no standard length; no sex: the men's set");
}

// ── Test 3 ────────────────────────────────────────────────────────────────────────────────────
void testOneHeader()
{
    std::printf("[Test 3] one header writer\n");
    SwingExportJob j = exportJob();
    j.analysisContext = captureAnalysisContext(j, QStringLiteral("female"), QStringLiteral("1990-05-01"),
                                               QDate(2026, 11, 14), {});
    constexpr int64_t t0 = 5'000'000, duration = 3'000'000;

    // The exporter's header (with its streams and thumbnail) and the analysis-only shot's.
    RecordingHeader full = recordingHeaderFor(j, t0, duration);
    full.thumbnail = RecordingHeader::Thumbnail{ QStringLiteral("thumb.jpg"), 1'200'000 };
    const QJsonArray streams{ QJsonObject{ { QStringLiteral("kind"), QStringLiteral("video") } } };
    QJsonObject a = recordingHeaderJson(full, streams);
    QJsonObject b = recordingHeaderJson(recordingHeaderFor(j, t0, duration), QJsonArray{});
    check(a.value(QStringLiteral("streams")).toArray().size() == 1 && b.value(QStringLiteral("streams")).toArray().isEmpty(),
          "the full header has the streams, the analysis-only one an empty streams[]");
    a.remove(QStringLiteral("streams")); a.remove(QStringLiteral("thumbnail"));
    b.remove(QStringLiteral("streams"));
    check(!b.contains(QStringLiteral("thumbnail")), "no thumbnail without media");
    check(a == b, "identical but for the media (streams, thumbnail)");
    check(a.value(QStringLiteral("schema")).toString() == QString::fromLatin1(kSwingDocSchema)
              && swingDocSchemaVersionOf(a) == kSwingDocSchemaVersion,
          "the one schema constant, pinpoint.swing/2");
    check(a.value(QStringLiteral("athlete")).toObject().value(QStringLiteral("heightM")).toDouble() == 1.65,
          "athlete.heightM on both paths (it was the analysis-only path's alone)");
    check(a.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toInteger() == t0
              && a.value(QStringLiteral("window")).toObject().value(QStringLiteral("end_us")).toInteger() == duration
              && a.value(QStringLiteral("clock")).toObject().value(QStringLiteral("wallclock")).toString()
                     == j.wallclockAnchorUtc.addMSecs(-duration / 1000).toString(Qt::ISODateWithMs),
          "clock and window: the window's start and length, the wallclock at its start");
    check(a.value(QStringLiteral("capture")).toObject().value(QStringLiteral("club")).toObject()
              .value(QStringLiteral("name")).toString() == QStringLiteral("7 IRON"),
          "capture.club kept for provenance and older readers");
    const AnalysisContextRead read = readAnalysisContext(a);
    check(read.recorded && !read.readOnly && contextDiff(*j.analysisContext, read.context).isEmpty(),
          "the header's analysisContext reads back as written");
    SwingExportJob noCtx = exportJob();
    check(!recordingHeaderJson(recordingHeaderFor(noCtx, t0, duration), {}).contains(QStringLiteral("analysisContext")),
          "no context known: no block (the document reads as the legacy context)");

    // The phone-clip placeholder: nothing known, nothing invented.
    const QJsonObject placeholder = recordingHeaderJson(RecordingHeader{}, QJsonArray{});
    check(placeholder.keys() == QStringList({ QStringLiteral("schema"), QStringLiteral("streams") })
              && placeholder.value(QStringLiteral("streams")).toArray().isEmpty(),
          "the placeholder: schema and an empty streams[] only");

    // The device-only shot: the same writer, the canonical keys (session.dir, window.start_us/end_us).
    QTemporaryDir dir;
    const QString d = dir.path() + QStringLiteral("/swing_0001");
    const QByteArray csv = "Shot ID, Club, Club head Speed (m/s), Ball Speed (m/s)\r\n901, Drv, 45.0, 66.0\r\n";
    auto reading = lm::parseLastShotCsv(csv);
    check(reading.has_value(), "a launch-monitor reading parses");
    if (!reading) return;
    SwingDocWriter::DeviceOnlyMeta meta;
    meta.swingId = QStringLiteral("swing_0001"); meta.swingIndex = 1;
    meta.sessionId = QStringLiteral("2026-11-14_Test_Swing_01");
    meta.athleteName = QStringLiteral("Test"); meta.athleteUuid = QStringLiteral("uuid-t");
    meta.wallclockMs = 1'763'100'000'000LL;
    check(SwingDocWriter::writeDeviceOnlySwing(d, *reading, meta), "a device-only shot is written");
    const QJsonObject dev = SwingStore::load(d);
    check(dev.value(QStringLiteral("schema")).toString() == QString::fromLatin1(kSwingDocSchema)
              && dev.value(QStringLiteral("session")).toObject().value(QStringLiteral("dir")).toString() == meta.sessionId
              && dev.value(QStringLiteral("window")).toObject().contains(QStringLiteral("end_us"))
              && dev.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toInteger() == 0
              && !dev.value(QStringLiteral("athlete")).toObject().contains(QStringLiteral("handedness"))
              && !dev.contains(QStringLiteral("analysisContext")),
          "device-only: the one writer's header, with only what the shot knows");
}

// ── Test 4 ────────────────────────────────────────────────────────────────────────────────────
// A document as everything before step 1.2 wrote it: no analysisContext block.
QJsonObject legacyDoc(const QString &capturedName, double lengthMm, const QJsonObject &review = {})
{
    QJsonObject club{ { QStringLiteral("name"), capturedName }, { QStringLiteral("lengthMm"), lengthMm },
                      { QStringLiteral("shaftType"), QStringLiteral("steel") },
                      { QStringLiteral("bandCentersMm"), QJsonArray{ 310.0, 510.0 } },
                      { QStringLiteral("hoselFromButtMm"), 860.0 },
                      { QStringLiteral("lengthPrior"), QJsonObject{ { QStringLiteral("px"), 300.0 },
                                                                    { QStringLiteral("varPx"), 9.0 },
                                                                    { QStringLiteral("n"), 4 } } } };
    QJsonObject root{
        { QStringLiteral("schema"),  QStringLiteral("pinpoint.swing/2") },
        { QStringLiteral("athlete"), QJsonObject{ { QStringLiteral("handedness"), QStringLiteral("Left") },
                                                  { QStringLiteral("heightM"), 1.78 } } },
        { QStringLiteral("session"), QJsonObject{ { QStringLiteral("dir"), QStringLiteral("2026-07-04_Golfer_Wrist_01") } } },
        { QStringLiteral("capture"), QJsonObject{ { QStringLiteral("club"), club } } },
    };
    if (!review.isEmpty()) root[QStringLiteral("review")] = review;
    return root;
}

// Today's re-analysis rule, exactly as swing_reanalyzer.cpp applied it before step 1.2 (the men's set:
// no document carries sex or dob): the declared club (review.club trimmed, else capture.club.name
// trimmed), its standard length when valid.
double todaysStandardLengthM(const QJsonObject &root, double heightM)
{
    const ClubStandard st = standardClub(swingDocDeclaredClub(root), ClubSet::Men, heightM);
    return st.valid ? st.lengthMm / 1000.0 : 0.0;
}

double jobStandardLengthM(const QJsonObject &root)
{
    rec::Recording r;
    r.context = readAnalysisContext(root).context;
    return buildAnalysisJob(r, effectiveContext(r.context, clubReviewOf(root)), RunOptions{}).standardClubLengthM;
}

void testLegacyContext()
{
    std::printf("[Test 4] a pre-step-1 document -> the legacy context\n");
    const QJsonObject root = legacyDoc(QStringLiteral(" 7 IRON "), 0.0);
    const AnalysisContextRead read = readAnalysisContext(root);
    check(!read.recorded && !read.readOnly, "no block: the legacy context, writable");
    const rec::AnalysisContext &c = read.context;
    check(c.handedness == rec::Handedness::Left && c.heightM == 1.78 && c.clubSet == ClubSet::Men,
          "handedness and height from athlete.*; the men's set (no sex or dob recorded)");
    check(c.club.name == QStringLiteral("7 IRON"), "the captured name, trimmed");
    check(!c.club.geometry.lengthKnown && c.club.geometry.bandCentersMm == std::vector<double>{ 310.0, 510.0 }
              && c.club.geometry.hoselFromButtMm == 860.0 && c.club.lengthPrior.n == 4,
          "capture.club's geometry and recorded prior");
    check(c.club.standardLengthMm == standardClub(QStringLiteral("7 IRON"), ClubSet::Men, 1.78).lengthMm,
          "the recorded standard length from the trimmed captured name");
    check(c.bag.isEmpty(), "no bag recorded");

    // The empty-popover case (no review.club): today's standard length, through the one builder.
    check(std::abs(jobStandardLengthM(root) - todaysStandardLengthM(root, 1.78)) < 1e-12
              && jobStandardLengthM(root) > 0.0,
          "empty popover: the job's standard length is today's (the captured club's)");
    const QJsonObject emptyReview = legacyDoc(QStringLiteral("7 IRON"), 0.0,
                                              QJsonObject{ { QStringLiteral("club"), QStringLiteral("  ") } });
    check(std::abs(jobStandardLengthM(emptyReview) - todaysStandardLengthM(emptyReview, 1.78)) < 1e-12,
          "a blank review.club reads as no popover club, as today");
    // A captured name that is not a standard club: no standard length, today and now.
    for (const char *name : { "CHIPPER", "7-iron" }) {
        const QJsonObject r = legacyDoc(QString::fromLatin1(name), 0.0);
        check(readAnalysisContext(r).context.club.standardLengthMm == 0.0 && jobStandardLengthM(r) == 0.0
                  && todaysStandardLengthM(r, 1.78) == 0.0,
              name[0] == 'C' ? "an unknown captured club: none, as today" : "a non-canonical spelling: none, as today");
    }
    // The seeded-DRIVER case: capture seeded review.club with the stub; it is taken as stored.
    const QJsonObject seeded = legacyDoc(QStringLiteral("7 IRON"), 0.0,
                                         QJsonObject{ { QStringLiteral("club"), QStringLiteral("DRIVER") } });
    check(std::abs(jobStandardLengthM(seeded) - todaysStandardLengthM(seeded, 1.78)) < 1e-12
              && std::abs(jobStandardLengthM(seeded) - standardClub(QStringLiteral("DRIVER"), ClubSet::Men).lengthMm / 1000.0) < 1e-12,
          "seeded DRIVER: the driver's standard length, as today");
    // A recorded length: no standard length reaches the job (as today).
    check(jobStandardLengthM(legacyDoc(QStringLiteral("7 IRON"), 940.0)) == 0.0, "a recorded length: no standard length");
    // Undeclared stays undeclared.
    QJsonObject bare = legacyDoc(QString(), 0.0);
    bare.remove(QStringLiteral("capture"));
    check(readAnalysisContext(bare).context.club.name.isEmpty()
              && readAnalysisContext(bare).context.club.standardLengthMm == 0.0,
          "no capture.club: undeclared, never the stub");
    // The club set as today's re-analysis derived it, should a document carry sex / dob.
    QJsonObject female = legacyDoc(QStringLiteral("7 IRON"), 0.0);
    female[QStringLiteral("athlete")] = QJsonObject{ { QStringLiteral("sex"), QStringLiteral("female") },
                                                     { QStringLiteral("dob"), QStringLiteral("1990-01-01") } };
    check(readAnalysisContext(female).context.clubSet == ClubSet::Women, "athlete.sex female: the women's set");
    female[QStringLiteral("athlete")] = QJsonObject{ { QStringLiteral("dob"), QStringLiteral("2010-01-01") } };
    check(readAnalysisContext(female).context.clubSet == ClubSet::Junior,
          "under 18 on the session's date: the junior set");
    // Through the loader: the synthetic swing written WITHOUT its block (as before step 1.2) loads
    // with the legacy context built from athlete.* and capture.club.
    QTemporaryDir dir;
    QJsonObject old = legacyDoc(QStringLiteral(" 7 IRON "), 0.0);
    check(writeSyntheticSwing(dir.path(), liveRecording(kDiskCam, kDiskImu),
                              QJsonObject{ { QStringLiteral("athlete"), old.value(QStringLiteral("athlete")) },
                                           { QStringLiteral("capture"), old.value(QStringLiteral("capture")) } }),
          "a pre-step-1 swing written");
    QJsonObject doc = SwingStore::load(dir.path());
    doc.remove(QStringLiteral("analysisContext"));
    check(SwingStore::save(dir.path(), doc), "...without its analysisContext block");
    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok && !lr.contextRecorded && !lr.readOnly
              && contextDiff(lr.recording.context, legacyAnalysisContext(doc)).isEmpty()
              && lr.recording.context.club.name == QStringLiteral("7 IRON"),
          "the loader gives the legacy context");

    // An empty document does not load (as before).
    QTemporaryDir empty;
    QJsonObject noStreams = legacyDoc(QStringLiteral("7 IRON"), 0.0);
    noStreams[QStringLiteral("streams")] = QJsonArray{};
    check(SwingStore::save(empty.path(), noStreams), "a document with no streams saved");
    const LoadedRecording none = loadRecording(empty.path());
    check(!none.ok && !none.error.isEmpty(), "a document with no camera stream does not load (as before)");
}

// ── Test 5 ────────────────────────────────────────────────────────────────────────────────────
void testNewerSchema()
{
    std::printf("[Test 5] a newer schema -> read-only\n");
    QJsonObject root = legacyDoc(QStringLiteral("7 IRON"), 940.0);
    root[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/3");
    AnalysisContextRead r = readAnalysisContext(root);
    check(r.readOnly && !r.readOnlyReason.isEmpty(), "a newer document schema: read-only, with the reason");
    check(r.context.club.name == QStringLiteral("7 IRON"), "...and still read");

    QJsonObject block = analysisContextJson(syntheticContext());
    block[QStringLiteral("schema")] = QStringLiteral("pinpoint.analysisContext/2");
    root = legacyDoc(QStringLiteral("9 IRON"), 0.0);
    root[QStringLiteral("analysisContext")] = block;
    r = readAnalysisContext(root);
    check(r.readOnly && r.recorded && contextDiff(syntheticContext(), r.context).isEmpty(),
          "a newer analysisContext schema: read-only, the known fields read");

    block.remove(QStringLiteral("schema"));
    root[QStringLiteral("analysisContext")] = block;
    r = readAnalysisContext(root);
    check(r.readOnly && !r.recorded && r.context.club.name == QStringLiteral("9 IRON"),
          "a block with no schema: read-only, the legacy context");

    root[QStringLiteral("analysisContext")] = analysisContextJson(syntheticContext());
    r = readAnalysisContext(root);
    check(!r.readOnly && r.recorded, "this build's schema: writable");
    check(swingDocSchemaVersionOf(QJsonObject{ { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/1") } }) == 1
              && swingDocSchemaVersionOf(QJsonObject{}) == 0
              && swingDocSchemaVersionOf(QJsonObject{ { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/x") } }) == 0,
          "the schema version read from the tag");

    // Through the loader: a newer document loads, read-only.
    QTemporaryDir dir;
    check(writeSyntheticSwing(dir.path(), liveRecording(kDiskCam, kDiskImu),
                              QJsonObject{ { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/9") } }),
          "a newer document written");
    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok && lr.readOnly, "the loader marks it read-only");
}

// ── Test 6 ────────────────────────────────────────────────────────────────────────────────────
void testClubReview()
{
    std::printf("[Test 6] the club review as stored\n");
    const QJsonObject root = legacyDoc(QStringLiteral("DRIVER"), 1150.0,
        QJsonObject{ { QStringLiteral("club"), QStringLiteral(" 7 IRON") }, { QStringLiteral("clubOverride"), QStringLiteral("9-iron") } });
    const ClubReview r = clubReviewOf(root);
    check(r.correction == QStringLiteral("9-iron") && r.declared == QStringLiteral(" 7 IRON"),
          "correction = review.clubOverride, declared = review.club, as stored");
    check(clubReviewOf(QJsonObject{}).correction.isEmpty() && clubReviewOf(QJsonObject{}).declared.isEmpty(),
          "no review: neither");

    QTemporaryDir dir;
    check(writeSyntheticSwing(dir.path(), liveRecording(kDiskCam, kDiskImu),
                              QJsonObject{ { QStringLiteral("review"), root.value(QStringLiteral("review")) } }),
          "a swing with a review written");
    const LoadedRecording lr = loadRecording(dir.path());
    check(lr.ok && lr.review.correction == r.correction && lr.review.declared == r.declared,
          "the loader hands the review over beside the Recording");
}

} // namespace

int main()
{
    testRecordingRoundTrip();
    testContextBlock();
    testOneHeader();
    testLegacyContext();
    testNewerSchema();
    testClubReview();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
