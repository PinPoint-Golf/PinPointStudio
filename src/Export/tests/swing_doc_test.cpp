// Standalone test for SwingDocWriter (round-trip). Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.1/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure

#include "../swing_doc.h"
#include "../swing_paths.h"
#include "../swing_store.h"
#include "../../LaunchMonitor/gcquad_csv_parser.h"
#include "../../Analysis/imu_refusion_check.h"   // ImuRefusionVerdict (header-only)
#include "../../Analysis/capture_integrity_check.h"   // CaptureIntegrityVerdict (header-only)
#include "../../Analysis/swing_analysis.h"
#include "../../Analysis/recorded_products.h"   // the club / ladder readers this file round-trips
#include "../../Analysis/dtl_shaft_json.h"      // dtlShaftTrackToJson — analysis.clubDtl's builder

// Stub — avoids linking swing_paths.cpp (which pulls in the PpLogStream logging deps).
// SwingDocReader::latestSessionDir() (the only sanitise() user) isn't exercised here.
QString pinpoint::SwingPaths::sanitise(const QString &raw) { return raw; }

#include <cmath>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QJsonObject>
#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// The document as it sits on disk — what a re-analysis re-reads and hands back to
// writeSwingJson as its manifest.
static QJsonObject readManifest(const QString &swingDir)
{
    return SwingStore::load(swingDir);
}

int main()
{
    const QString dir = QStringLiteral("/tmp/swingdoc_test");
    QDir().mkpath(dir);

    QJsonObject manifest;
    manifest[QStringLiteral("schema")]  = QStringLiteral("pinpoint.swing/1");
    manifest[QStringLiteral("swing")]   = QJsonObject{ {QStringLiteral("index"), 7},
                                                       {QStringLiteral("id"), QStringLiteral("swing_0007")} };
    manifest[QStringLiteral("streams")] = QJsonArray{
        QJsonObject{ {QStringLiteral("kind"), QStringLiteral("video")},
                     {QStringLiteral("alias"), QStringLiteral("face-on")},
                     // ⚠ `file` is not decoration in this fixture. SwingExporter writes it
                     // for every video element unconditionally, and hasVideo now requires it
                     // — a video stream with no file is not playable video, which is what a
                     // clip that has been asked for and not yet arrived looks like.
                     {QStringLiteral("file"), QStringLiteral("face-on.mp4")},
                     {QStringLiteral("setup"), QJsonObject{ {QStringLiteral("perspective"), 2},
                                                            {QStringLiteral("perspectiveName"), QStringLiteral("FaceOn")},
                                                            {QStringLiteral("mirrored"), false},
                                                            {QStringLiteral("fixedInPlace"), true} }} },
        QJsonObject{ {QStringLiteral("kind"), QStringLiteral("imu")},
                     {QStringLiteral("alias"), QStringLiteral("Hand")},
                     {QStringLiteral("device"), QJsonObject{ {QStringLiteral("outputRateHz"), 200},
                                                             {QStringLiteral("fusionMode"), QStringLiteral("6axis")},
                                                             {QStringLiteral("orientationFilter"), QStringLiteral("Madgwick")},
                                                             {QStringLiteral("placementSlot"), QStringLiteral("B")} }} } };
    manifest[QStringLiteral("clock")]   = QJsonObject{ {QStringLiteral("wallclock"), QStringLiteral("2026-06-08T16:00:00.000")} };
    // WHO swung it. The exporter has written this block since its first version; the reader dropped
    // it, which is the read-back gap norm cohorts closed — the uuid plus the wallclock are what a
    // cohort is derived from, and without the uuid an offline re-analysis would resolve a different
    // one from the live path on the same swing.
    manifest[QStringLiteral("athlete")] = QJsonObject{ {QStringLiteral("name"), QStringLiteral("A Golfer")},
                                                       {QStringLiteral("uuid"), QStringLiteral("uuid-1234")} };
    manifest[QStringLiteral("capture")] = QJsonObject{
        {QStringLiteral("sessionType"), 1},
        {QStringLiteral("shotSource"),  1},
        {QStringLiteral("swingDetectionSensitivity"), QStringLiteral("Medium")},
        {QStringLiteral("latencyUs"), QJsonObject{ {QStringLiteral("imuBle"), 30000},
                                                   {QStringLiteral("audioDevice"), 20000} }},
        {QStringLiteral("host"), QJsonObject{ {QStringLiteral("app"), QStringLiteral("PinPointStudio")},
                                              {QStringLiteral("gitSha"), QStringLiteral("abc1234")} }} };
    manifest[QStringLiteral("thumbnail")] = QJsonObject{ {QStringLiteral("file"), QStringLiteral("thumb.jpg")},
                                                         {QStringLiteral("t_us"), static_cast<qint64>(10000)} };

    SwingAnalysis a;
    a.tier          = int(ReconstructionTier::Mono3DPlusImu);
    a.score.overall = 82;
    MetricSeries m;
    m.key = QStringLiteral("leadWristFlexExt"); m.label = QStringLiteral("Lead-wrist flex/ext"); m.unit = QStringLiteral("°");
    m.t_us  = { 1000000, 1005000, 1010000 };
    m.value = { 0.0, 12.5, -8.0 };
    m.phaseSamples.push_back({ Phase::Impact, 1010000, -8.0, QStringLiteral("green") });
    a.series.push_back(m);
    // A Summary scalar carrying a measurement uncertainty: empty curve, one
    // Impact phaseSample, non-degree unit. Covers both additions at once — the
    // optional `sigma` field and the unit-aware flat-metric formatting (this
    // used to render as "3°" because the reader hardcoded degrees).
    MetricSeries tr;
    tr.key   = QStringLiteral("tempoRatio");
    tr.label = QStringLiteral("Tempo ratio");
    tr.unit  = QStringLiteral(":1");
    tr.sigma = 0.25;
    tr.phaseSamples.push_back({ Phase::Impact, 1010000, 3.0, QString() });
    a.series.push_back(tr);
    a.phases.push_back({ Phase::Impact, 1010000, 1.0f });
    a.phases.push_back({ Phase::Top, 700000, 0.9f, SegmentRole::LeadHand });

    // Segmentation block (v3 G2): swing bounds + ladder meta.
    a.segmentation.swingStartUs = 250000;
    a.segmentation.swingEndUs   = 1400000;
    a.segmentation.conf         = 0.84f;
    a.segmentation.version      = 2;

    // ShaftTracker blocks (S3): one pose frame + a valid 2-sample club track.
    PoseFrame2D pf;
    pf.t_us = 1000000;
    pf.kp[9]   = QPointF(0.40, 0.55); pf.conf[9]  = 0.9f;   // left wrist
    pf.kp[10]  = QPointF(0.42, 0.56); pf.conf[10] = 0.8f;   // right wrist
    pf.leadHand  = QPointF(0.41, 0.57);
    pf.trailHand = QPointF(0.43, 0.58);
    pf.handConf  = 0.85f;
    a.pose2d.camera = 3;
    a.pose2d.frames.push_back(pf);

    a.shaft.camera        = 3;
    a.shaft.valid         = true;
    a.shaft.coverage      = 0.95f;
    a.shaft.imuVisionCorr = 0.97f;
    a.shaft.frameWidth    = 1920;
    a.shaft.frameHeight   = 1080;
    a.shaft.samples.push_back({ 1000000, QPointF(960.0, 540.0), QPointF(960.0, 810.0),
                                1.5708, 0.0, 270.0, 0.9f, ShaftMeasured });
    a.shaft.samples.push_back({ 1010000, QPointF(950.0, 545.0), QPointF(700.0, 700.0),
                                2.2, 30.0, 268.0, 0.8f,
                                uint8_t(ShaftImuBridged | ShaftHeadProjected) });
    // R7 dual output: pure-model predicted series + model/vision residual.
    a.shaft.modelVisionResidualDeg = 4.2f;
    a.shaft.predicted.push_back({ 1000000, QPointF(960.0, 540.0), QPointF(965.0, 800.0),
                                  1.55, 0.0, 270.0, 0.7f, ShaftKinematicPredicted });
    a.shaft.predicted.push_back({ 1010000, QPointF(950.0, 545.0), QPointF(720.0, 690.0),
                                  2.15, 0.0, 268.0, 0.6f, ShaftKinematicPredicted });

    // Ball track (v3.4): one found sample (drawn) + one post-launch gap sample.
    a.ball.camera       = 3;
    a.ball.launchTUs    = 1010000;
    a.ball.launchCenter = QPointF(0.50, 0.80);
    a.ball.frames.push_back({ 1000000, true,  QPointF(0.50, 0.80), 0.02f, 0.9f });
    a.ball.frames.push_back({ 1010000, false, QPointF(0.50, 0.80), 0.0f,  0.0f });

    // IMU binding with the calibration-status snapshot (corpus provenance).
    BindingRecord bind;
    bind.serial = QStringLiteral("WT901-1234");
    bind.role   = SegmentRole::LeadHand;
    bind.alignA = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    bind.mountM = QQuaternion(0.5f, -0.5f, -0.5f, -0.5f);
    bind.anatCalibrated       = true;
    bind.calibrated           = true;
    bind.mountDeviationDeg    = 3.2;
    bind.mountGravityErrorDeg = 5.1;
    bind.calibratedAtUtc      = QStringLiteral("2026-06-11T09:12:00.123Z");
    bind.calibAgeSec          = 412.5;
    a.bindings.push_back(bind);

    std::printf("=== unified write (raw + analysis) ===\n");
    QString err;
    if (!SwingDocWriter::writeSwingJson(dir, manifest, &a, &err)) {
        std::printf("  [FAIL] write: %s\n", err.toUtf8().constData());
        return 1;
    }
    check(SwingStore::hasDocument(dir), "document written");
    const QJsonObject root = SwingStore::load(dir);

    check(root[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swing/2"), "schema bumped to pinpoint.swing/2");
    check(root.contains(QStringLiteral("streams")), "raw streams preserved");
    check(root[QStringLiteral("swing")].toObject()[QStringLiteral("id")].toString() == QStringLiteral("swing_0007"), "raw swing block preserved");
    check(root.contains(QStringLiteral("analysis")), "inline analysis block present");
    const QJsonObject an = root[QStringLiteral("analysis")].toObject();
    check(an[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.analysis/3"), "analysis schema /3");
    // /3: "score" is now a ScoreBreakdown object (design §B.0a); overall preserved, default kind=adherence.
    const QJsonObject scoreObj = an[QStringLiteral("score")].toObject();
    check(scoreObj[QStringLiteral("overall")].toInt() == 82, "analysis score.overall = 82");
    check(scoreObj[QStringLiteral("kind")].toString() == QStringLiteral("adherence"), "analysis score.kind = adherence");
    check(an[QStringLiteral("tier")].toInt() == int(ReconstructionTier::Mono3DPlusImu), "tier");
    const QJsonArray mets = an[QStringLiteral("metrics")].toArray();
    check(mets.size() == 2, "two metric series");
    const QJsonObject m0 = mets.at(0).toObject();
    check(m0[QStringLiteral("key")].toString() == QStringLiteral("leadWristFlexExt"), "metric key");
    check(m0[QStringLiteral("t_us")].toArray().size() == 3 && m0[QStringLiteral("value")].toArray().size() == 3, "t_us + value arrays (len 3)");
    check(qFuzzyCompare(m0[QStringLiteral("value")].toArray().at(1).toDouble(), 12.5), "value[1] == 12.5");
    check(m0[QStringLiteral("phaseSamples")].toArray().size() == 1, "phaseSamples");
    // sigma is OPTIONAL and absent-by-default: a metric that never set one must
    // not gain the key, or every pre-existing swing.json stops round-tripping
    // byte-identically.
    check(!m0.contains(QStringLiteral("sigma")), "no sigma key when the producer set none");
    const QJsonObject m1 = mets.at(1).toObject();
    check(m1[QStringLiteral("key")].toString() == QStringLiteral("tempoRatio"), "summary metric key");
    check(m1[QStringLiteral("t_us")].toArray().isEmpty() && m1[QStringLiteral("value")].toArray().isEmpty(),
          "summary scalar has an empty curve");
    check(m1.contains(QStringLiteral("sigma"))
              && qFuzzyCompare(m1[QStringLiteral("sigma")].toDouble(), 0.25),
          "sigma serialized when present");
    check(an[QStringLiteral("phases")].toArray().size() == 2, "phases array");
    check(static_cast<qint64>(an[QStringLiteral("phases")].toArray().at(0).toObject()[QStringLiteral("t_us")].toDouble()) == 1010000, "phase t_us preserved");
    check(an[QStringLiteral("phases")].toArray().at(1).toObject()[QStringLiteral("segment")].toInt()
              == int(SegmentRole::LeadHand), "phase provenance preserved");

    std::printf("\n=== capture / setup / device passthrough ===\n");
    {
        check(root.contains(QStringLiteral("capture")), "capture block preserved");
        const QJsonObject cap = root[QStringLiteral("capture")].toObject();
        check(cap[QStringLiteral("sessionType")].toInt() == 1, "capture.sessionType");
        check(cap[QStringLiteral("latencyUs")].toObject()[QStringLiteral("imuBle")].toInt() == 30000,
              "capture.latencyUs.imuBle");
        check(cap[QStringLiteral("host")].toObject()[QStringLiteral("gitSha")].toString()
                  == QStringLiteral("abc1234"), "capture.host.gitSha");
        const QJsonArray strs = root[QStringLiteral("streams")].toArray();
        check(strs.at(0).toObject()[QStringLiteral("setup")].toObject()[QStringLiteral("perspective")].toInt() == 2,
              "video stream setup.perspective");
        check(strs.at(1).toObject()[QStringLiteral("device")].toObject()[QStringLiteral("outputRateHz")].toInt() == 200,
              "imu stream device.outputRateHz");
    }

    std::printf("\n=== bindings calibration status ===\n");
    {
        check(an.contains(QStringLiteral("bindings")), "bindings block present");
        const QJsonArray binds = an[QStringLiteral("bindings")].toArray();
        check(binds.size() == 1, "one binding");
        const QJsonObject b0 = binds.at(0).toObject();
        check(b0[QStringLiteral("serial")].toString() == QStringLiteral("WT901-1234"), "binding serial");
        check(b0[QStringLiteral("role")].toInt() == int(SegmentRole::LeadHand), "binding role");
        check(b0[QStringLiteral("calibrated")].toBool(), "binding calibrated");
        check(b0[QStringLiteral("anatCalibrated")].toBool(), "binding anatCalibrated");
        check(qFuzzyCompare(b0[QStringLiteral("mountDeviationDeg")].toDouble(), 3.2), "mountDeviationDeg");
        check(qFuzzyCompare(b0[QStringLiteral("mountGravityErrorDeg")].toDouble(), 5.1), "mountGravityErrorDeg");
        check(b0[QStringLiteral("calibratedAt")].toString() == QStringLiteral("2026-06-11T09:12:00.123Z"),
              "calibratedAt ISO string");
        check(qFuzzyCompare(b0[QStringLiteral("calibAgeSec")].toDouble(), 412.5), "calibAgeSec");
        check(qFuzzyCompare(b0[QStringLiteral("mountM")].toArray().at(0).toDouble(), 0.5), "mountM w");
    }

    std::printf("\n=== segmentation block (v3 G2) ===\n");
    {
        check(an.contains(QStringLiteral("segmentation")), "segmentation block present");
        const QJsonObject sg = an[QStringLiteral("segmentation")].toObject();
        check(static_cast<qint64>(sg[QStringLiteral("swingStartUs")].toDouble()) == 250000,
              "swingStartUs preserved");
        check(static_cast<qint64>(sg[QStringLiteral("swingEndUs")].toDouble()) == 1400000,
              "swingEndUs preserved");
        check(qFuzzyCompare(sg[QStringLiteral("conf")].toDouble(), double(0.84f)),
              "segmentation conf preserved");
        check(sg[QStringLiteral("version")].toInt() == 2, "segmentation version preserved");
    }

    std::printf("\n=== ShaftTracker blocks (pose2d + club) ===\n");
    {
        check(an.contains(QStringLiteral("pose2d")), "pose2d block present");
        const QJsonObject p2 = an[QStringLiteral("pose2d")].toObject();
        check(p2[QStringLiteral("camera")].toInt() == 3, "pose2d.camera");
        const QJsonArray pframes = p2[QStringLiteral("frames")].toArray();
        check(pframes.size() == 1, "pose2d one frame");
        const QJsonObject pf0 = pframes.at(0).toObject();
        check(p2[QStringLiteral("keypointCount")].toInt() == 133, "pose2d.keypointCount 133");
        check(pf0[QStringLiteral("kp")].toArray().size() == 133 * 3,
              "kp flat array 399 long (COCO-WholeBody)");
        check(qFuzzyCompare(pf0[QStringLiteral("kp")].toArray().at(9 * 3).toDouble(), 0.40),
              "left-wrist x at kp[27]");
        check(qFuzzyCompare(pf0[QStringLiteral("handConf")].toDouble(), double(0.85f)), "handConf");
        check(qFuzzyCompare(pf0[QStringLiteral("lead")].toArray().at(1).toDouble(), 0.57),
              "lead hand y");
        // Byte-additivity: an analysis with an empty smoothed vector must NOT emit
        // the "smoothed" key (Motion-overlay Phase 2 additive contract).
        check(!p2.contains(QStringLiteral("smoothed")),
              "pose2d without smoothed omits the block (byte-additive)");

        check(an.contains(QStringLiteral("club")), "club block present");
        const QJsonObject cb = an[QStringLiteral("club")].toObject();
        check(cb[QStringLiteral("valid")].toBool(), "club.valid");
        check(cb[QStringLiteral("frameWidth")].toInt() == 1920, "club.frameWidth");
        const QJsonArray cs = cb[QStringLiteral("samples")].toArray();
        check(cs.size() == 2, "club two samples");
        const QJsonObject c0 = cs.at(0).toObject();
        check(qFuzzyCompare(c0[QStringLiteral("grip")].toArray().at(0).toDouble(), 960.0 / 1920.0),
              "grip x normalized by frame width");
        check(qFuzzyCompare(c0[QStringLiteral("head")].toArray().at(1).toDouble(), 810.0 / 1080.0),
              "head y normalized by frame height");
        check(c0[QStringLiteral("flags")].toInt() == int(ShaftMeasured), "sample 0 flags");
        check(cs.at(1).toObject()[QStringLiteral("flags")].toInt()
                  == int(ShaftImuBridged | ShaftHeadProjected), "sample 1 flags");
        const double mvr = cb[QStringLiteral("modelVisionResidualDeg")].toDouble();
        check(mvr > 4.19 && mvr < 4.21, "club.modelVisionResidualDeg");
        const QJsonArray cp = cb[QStringLiteral("predicted")].toArray();
        check(cp.size() == 2, "club two predicted samples");
        check(cp.at(0).toObject()[QStringLiteral("flags")].toInt() == int(ShaftKinematicPredicted),
              "predicted flag ShaftKinematicPredicted");
        check(qFuzzyCompare(cp.at(0).toObject()[QStringLiteral("grip")].toArray().at(0).toDouble(),
                            960.0 / 1920.0), "predicted grip normalized by frame width");

        // Face-on swing plane (shaft_plane.h). This fixture's shaft track never ran
        // the producer, and the block must still be written: a reader has to be able
        // to tell "the producer ran and nothing fitted" from "this file predates it",
        // and only an always-present block with valid=false can say the first.
        check(cb.contains(QStringLiteral("plane")), "club.plane present even with no fit");
        const QJsonObject pl = cb[QStringLiteral("plane")].toObject();
        check(!pl[QStringLiteral("valid")].toBool(), "club.plane.valid false when nothing fitted");
        check(pl[QStringLiteral("channel")].toInt() == -1, "club.plane.channel -1 = no channel");
        check(pl.contains(QStringLiteral("measured")) && pl.contains(QStringLiteral("synth")),
              "club.plane carries BOTH channels, never merged");
        // The split-half trap, pinned at the serialization boundary: the synth
        // channel's split-half fields must stay absent (-1) whatever it carries,
        // because odd/even samples of a Hermite would fake excellent quality.
        const QJsonObject sy = pl[QStringLiteral("synth")].toObject();
        check(sy[QStringLiteral("splitHalfBackDeg")].toDouble() == -1.0
              && sy[QStringLiteral("splitHalfDownDeg")].toDouble() == -1.0,
              "club.plane.synth carries no split-half");
    }

    std::printf("\n=== ball block (v3.4) ===\n");
    {
        check(an.contains(QStringLiteral("ball")), "ball block present");
        const QJsonObject bb = an[QStringLiteral("ball")].toObject();
        check(bb[QStringLiteral("camera")].toInt() == 3, "ball.camera");
        check(bb[QStringLiteral("valid")].toBool(), "ball.valid");
        const QJsonArray bs = bb[QStringLiteral("samples")].toArray();
        check(bs.size() == 2, "ball two samples");
        const QJsonObject b0 = bs.at(0).toObject();
        check(b0[QStringLiteral("found")].toBool(), "ball sample 0 found");
        check(qFuzzyCompare(b0[QStringLiteral("x")].toDouble(), 0.50), "ball sample 0 x normalized");
        check(qFuzzyCompare(b0[QStringLiteral("y")].toDouble(), 0.80), "ball sample 0 y normalized");
        check(qFuzzyCompare(b0[QStringLiteral("r")].toDouble(), double(0.02f)), "ball sample 0 radiusNorm");
        check(!bs.at(1).toObject()[QStringLiteral("found")].toBool(),
              "ball sample 1 post-launch not found");
    }

    std::printf("\n=== pose2d smoothed companion track (Motion overlay) ===\n");
    {
        // Absolute-domain manifest so we also verify smoothed t_us is written
        // window-relative (re-based by clock.t0_us) exactly like frames'.
        const qint64 T0 = 176400665083LL;
        QJsonObject mSm = manifest;
        mSm[QStringLiteral("clock")] = QJsonObject{ {QStringLiteral("t0_us"), double(T0)},
                                                    {QStringLiteral("wallclock"),
                                                     QStringLiteral("2026-06-08T16:00:00.000")} };
        SwingAnalysis aSm;
        aSm.tier = int(ReconstructionTier::Angles2D);
        aSm.pose2d.camera = 3;
        // Two RAW pose frames …
        PoseFrame2D r0; r0.t_us = T0 + 1000000; r0.kp[5] = QPointF(0.30, 0.40); r0.conf[5] = 0.9f;
        PoseFrame2D r1; r1.t_us = T0 + 1010000; r1.kp[5] = QPointF(0.31, 0.41); r1.conf[5] = 0.8f;
        aSm.pose2d.frames = { r0, r1 };
        // … and the parallel SMOOTHED companion + per-kp honesty aux.
        PoseFrame2D s0; s0.t_us = T0 + 1000000; s0.kp[5] = QPointF(0.305, 0.405); s0.conf[5] = 0.95f;
        PoseFrame2D s1; s1.t_us = T0 + 1010000; s1.kp[5] = QPointF(0.312, 0.412); s1.conf[5] = 0.85f;
        aSm.pose2d.smoothed = { s0, s1 };
        PoseKpAux x0; x0.tier[5] = uint8_t(PoseTier::Meas); x0.sigma[5] = 2.5f;
        PoseKpAux x1; x1.tier[5] = uint8_t(PoseTier::Pred); x1.sigma[5] = 4.0f;
        aSm.pose2d.smoothedAux = { x0, x1 };

        const QString dirS = dir + QStringLiteral("_smoothed");
        QDir().mkpath(dirS);
        QString serr;
        check(SwingDocWriter::writeSwingJson(dirS, mSm, &aSm, &serr), "smoothed write ok");

        check(SwingStore::hasDocument(dirS), "document written");
        const QJsonObject rs = SwingStore::load(dirS);
        const QJsonObject p2 =
            rs[QStringLiteral("analysis")].toObject()[QStringLiteral("pose2d")].toObject();
        check(p2.contains(QStringLiteral("smoothed")), "smoothed block present when populated");
        check(p2[QStringLiteral("frames")].toArray().size() == 2,
              "raw frames still present alongside smoothed");
        const QJsonArray sm = p2[QStringLiteral("smoothed")].toArray();
        check(sm.size() == 2, "smoothed two frames");
        const QJsonObject sm0 = sm.at(0).toObject();
        // t_us re-based to window-relative like frames.
        check(static_cast<qint64>(sm0[QStringLiteral("t_us")].toDouble()) == 1000000,
              "smoothed[0] t_us window-relative");
        check(static_cast<qint64>(sm.at(1).toObject()[QStringLiteral("t_us")].toDouble()) == 1010000,
              "smoothed[1] t_us window-relative");
        check(sm0[QStringLiteral("kp")].toArray().size() == 133 * 3,
              "smoothed kp flat 399 long (COCO-WholeBody)");
        check(qFuzzyCompare(sm0[QStringLiteral("kp")].toArray().at(5 * 3).toDouble(), 0.305),
              "smoothed kp[5].x carried");
        check(qFuzzyCompare(sm0[QStringLiteral("kp")].toArray().at(5 * 3 + 2).toDouble(), double(0.95f)),
              "smoothed kp[5] conf carried");
        check(sm0[QStringLiteral("tier")].toArray().size() == 133, "smoothed tier 133 long");
        check(sm0[QStringLiteral("tier")].toArray().at(5).toInt() == int(PoseTier::Meas),
              "smoothed[0] tier[5] = Meas");
        check(sm.at(1).toObject()[QStringLiteral("tier")].toArray().at(5).toInt() == int(PoseTier::Pred),
              "smoothed[1] tier[5] = Pred");
        check(sm0[QStringLiteral("sigma")].toArray().size() == 133, "smoothed sigma 133 long");
        check(qFuzzyCompare(sm0[QStringLiteral("sigma")].toArray().at(5).toDouble(), double(2.5f)),
              "smoothed sigma[5] carried");
        // No hand fields on the smoothed frames (hands are not smoothed).
        check(!sm0.contains(QStringLiteral("lead")) && !sm0.contains(QStringLiteral("handConf")),
              "smoothed frame has no hand fields");

        // Reader passthrough: the whole-object toVariantMap carries smoothed for free.
        const PersistedShot ps = SwingDocReader::readSwingJson(dirS);
        check(ps.ok, "smoothed reader ok");
        const QVariantMap rp2 = ps.analysisDetail.value(QStringLiteral("pose2d")).toMap();
        const QVariantList rsm = rp2.value(QStringLiteral("smoothed")).toList();
        check(rsm.size() == 2, "reader analysisDetail.pose2d.smoothed len 2");
        const QVariantMap rsm0 = rsm.at(0).toMap();
        check(rsm0.value(QStringLiteral("kp")).toList().size() == 133 * 3,
              "reader smoothed kp flat 399");
        check(rsm0.value(QStringLiteral("tier")).toList().at(5).toInt() == int(PoseTier::Meas),
              "reader smoothed tier[5] = Meas");
        check(qFuzzyCompare(rsm0.value(QStringLiteral("sigma")).toList().at(5).toDouble(), double(2.5f)),
              "reader smoothed sigma[5] carried");
    }

    std::printf("\n=== reader round-trip ===\n");
    {
        const PersistedShot ps = SwingDocReader::readSwingJson(dir);
        check(ps.ok, "read ok");
        check(ps.ordinal == 7, "ordinal == 7");
        check(ps.hasVideo, "hasVideo true");
        check(!ps.thumbnailPath.isEmpty(), "thumbnail path resolved");
        check(ps.timestampLabel == QStringLiteral("16:00:00"), "timestamp from wallclock");
        check(ps.score == 82, "score == 82");
        // The athlete block survives read-back. It is what a norm cohort is resolved through: the
        // uuid finds the record that holds the date of birth, wallclockMs says which day, and the
        // age band is derived from the two. Dropped here for as long as the reader existed, so an
        // offline re-analysis and the live path could have graded one swing two ways with nothing
        // reporting the disagreement.
        check(ps.athleteUuid == QStringLiteral("uuid-1234"), "the athlete uuid survives read-back");
        check(ps.athleteName == QStringLiteral("A Golfer"), "…and the display name with it");
        check(ps.wallclockMs != 0, "…alongside the absolute instant the band is read at");
        const QVariantMap fe = ps.metrics.value(QStringLiteral("leadWristFlexExt")).toMap();
        check(fe.value(QStringLiteral("value")).toString() == QStringLiteral("-8°"), "flat metric value -8 deg");
        // Non-degree metrics format in their OWN unit — this reader used to
        // hardcode "°" and rendered every ratio/speed/×frame value as degrees.
        const QVariantMap tro = ps.metrics.value(QStringLiteral("tempoRatio")).toMap();
        check(tro.value(QStringLiteral("value")).toString() == QStringLiteral("3.00:1"),
              "flat metric formats in its own unit, not degrees");
        check(ps.analysisDetail.value(QStringLiteral("overall")).toInt() == 82, "analysisDetail.overall");
        check(ps.analysisDetail.value(QStringLiteral("series")).toList().size() == 2, "analysisDetail.series len 2");
        check(ps.analysisDetail.value(QStringLiteral("phases")).toList().size() == 2, "analysisDetail.phases len 2");
        const QVariantMap sg = ps.analysisDetail.value(QStringLiteral("segmentation")).toMap();
        check(sg.value(QStringLiteral("swingStartUs")).toLongLong() == 250000
                  && sg.value(QStringLiteral("swingEndUs")).toLongLong() == 1400000,
              "analysisDetail.segmentation bounds reload");
        const QVariantMap p2 = ps.analysisDetail.value(QStringLiteral("pose2d")).toMap();
        check(p2.value(QStringLiteral("frames")).toList().size() == 1, "reloaded pose2d frames");
        const QVariantMap cb = ps.analysisDetail.value(QStringLiteral("club")).toMap();
        check(cb.value(QStringLiteral("valid")).toBool(), "reloaded club.valid");
        check(cb.value(QStringLiteral("samples")).toList().size() == 2, "reloaded club samples");
        const QVariantMap cs1 = cb.value(QStringLiteral("samples")).toList().at(1).toMap();
        check(cs1.value(QStringLiteral("flags")).toInt()
                  == int(ShaftImuBridged | ShaftHeadProjected), "reloaded sample flags");
        check(cb.value(QStringLiteral("predicted")).toList().size() == 2, "reloaded predicted samples");
        const double mvr2 = cb.value(QStringLiteral("modelVisionResidualDeg")).toDouble();
        check(mvr2 > 4.19 && mvr2 < 4.21, "reloaded modelVisionResidualDeg");
        const QVariantMap rbb = ps.analysisDetail.value(QStringLiteral("ball")).toMap();
        check(rbb.value(QStringLiteral("samples")).toList().size() == 2, "reloaded ball samples");
        check(rbb.value(QStringLiteral("samples")).toList().at(0).toMap()
                  .value(QStringLiteral("found")).toBool(), "reloaded ball sample 0 found");
        const QVariantList rbinds = ps.analysisDetail.value(QStringLiteral("bindings")).toList();
        check(rbinds.size() == 1, "reloaded bindings len 1");
        const QVariantMap rb0 = rbinds.at(0).toMap();
        check(rb0.value(QStringLiteral("calibrated")).toBool(), "reloaded binding calibrated");
        check(qFuzzyCompare(rb0.value(QStringLiteral("calibAgeSec")).toDouble(), 412.5),
              "reloaded binding calibAgeSec");
    }

    std::printf("\n=== review write-through round-trip ===\n");
    {
        QString rerr;
        check(SwingDocWriter::updateReview(dir, 4, QStringLiteral("nice tempo"),
                                           QStringLiteral("7 IRON"), &rerr),
              "updateReview ok");
        // The review block lands without disturbing the raw/analysis blocks.
        const QJsonObject r = SwingStore::load(dir);
        check(!r.isEmpty(), "document readable");
        // Every rewriter (review, LM, origin) writes the binary format: one that wrote JSON back
        // would re-inflate the swing ~10x on its next edit and leave two documents to disagree.
        check(SwingStore::info(dir).format == SwingStore::Format::Ppsw && !QFile::exists(dir + QStringLiteral("/swing.json")),
              "the document is still swing.ppsw after updateReview");
        check(r.contains(QStringLiteral("analysis")), "analysis block survives review write");
        const QJsonObject rv = r[QStringLiteral("review")].toObject();
        check(rv[QStringLiteral("rating")].toInt() == 4, "review.rating == 4");
        check(rv[QStringLiteral("note")].toString() == QStringLiteral("nice tempo"), "review.note");
        check(rv[QStringLiteral("club")].toString() == QStringLiteral("7 IRON"), "review.club");

        const PersistedShot ps = SwingDocReader::readSwingJson(dir);
        check(ps.rating == 4, "reader rating == 4");
        check(ps.note == QStringLiteral("nice tempo"), "reader note");
        check(ps.club == QStringLiteral("7 IRON"), "reader club");

        // Rewriting replaces (does not append) the review block; clamps rating.
        check(SwingDocWriter::updateReview(dir, 9, QStringLiteral("re-rated"),
                                           QStringLiteral("PUTTER"), nullptr),
              "updateReview rewrite ok");
        const PersistedShot ps2 = SwingDocReader::readSwingJson(dir);
        check(ps2.rating == 5, "reader rating clamped to 5");
        check(ps2.note == QStringLiteral("re-rated"), "reader note rewritten");
        check(ps2.club == QStringLiteral("PUTTER"), "reader club rewritten");

        // No swing.json → updateReview fails harmlessly (returns false).
        check(!SwingDocWriter::updateReview(QStringLiteral("/tmp/swingdoc_test_nope"), 3, QString(),
                                            QStringLiteral("DRIVER"), nullptr),
              "updateReview on missing doc returns false");
    }

    std::printf("\n=== down-the-line blocks (poseDtl + clubDtl) ===\n");
    {
        // A two-camera analysis in the ABSOLUTE domain, so the relative re-basing of both
        // new blocks is exercised alongside the shape.
        const qint64 T0 = 176400665083LL;
        QJsonObject mD = manifest;
        mD[QStringLiteral("clock")] = QJsonObject{ {QStringLiteral("t0_us"), double(T0)} };
        QJsonArray streams = mD[QStringLiteral("streams")].toArray();
        streams.append(QJsonObject{ {QStringLiteral("kind"),   QStringLiteral("video")},
                                    {QStringLiteral("alias"),  QStringLiteral("DTL")},
                                    {QStringLiteral("file"),   QStringLiteral("dtl.mp4")},
                                    {QStringLiteral("source"), QJsonObject{ {QStringLiteral("serial"),
                                                                             QStringLiteral("DTLSER")} }} });
        mD[QStringLiteral("streams")] = streams;

        SwingAnalysis aD;
        aD.tier = int(ReconstructionTier::Angles2D);
        aD.versions.pose = 1; aD.versions.poseModel = QStringLiteral("m@1"); aD.versions.poseScope = QStringLiteral("span");
        PoseFrame2D f0; f0.t_us = T0 + 1000000; f0.kp[5] = QPointF(0.30, 0.40); f0.conf[5] = 0.9f;
        f0.leadHand = QPointF(0.5, 0.6); f0.handConf = 0.7f;
        PoseFrame2D f1 = f0; f1.t_us = T0 + 1010000;
        aD.pose2d.camera = 3;
        aD.pose2d.frames = { f0, f1 };
        aD.pose2d.smoothed = { f0, f1 };
        aD.pose2d.smoothedAux = { PoseKpAux{}, PoseKpAux{} };
        aD.pose2d.smoothedSynth = { f0 };
        aD.poseDtl = aD.pose2d;
        aD.poseDtl.camera = 4;
        aD.poseDtl.frames.push_back(f1);   // a different track, not a copy of the face-on one
        aD.poseDtl.frames.back().t_us = T0 + 1020000;
        aD.versions.poseDtl = kDtlPoseStageVersion; aD.versions.poseDtlModel = QStringLiteral("m@1");

        DtlShaftTrack2D &t = aD.shaftDtl;
        t.valid = true; t.camera = 4; t.frameWidth = 1000; t.frameHeight = 500;
        DtlSample pub; pub.t_us = T0 + 1000000; pub.tier = DtlTier::Ray;
        pub.gripPx = QPointF(500, 250); pub.headPx = QPointF(600, 450); pub.thetaRad = 1.1;
        pub.lenPx = 220; pub.conf = 0.8f; pub.band = 0;
        DtlSample gap; gap.t_us = T0 + 1010000; gap.tier = DtlTier::EndOn;
        gap.gripPx = QPointF(dtl::kNan, dtl::kNan); gap.reason = QStringLiteral("end-on");
        t.samples = { pub, gap };
        DtlBand b; b.lo = 0; b.hi = 0; b.loUs = T0 + 1000000; b.hiUs = T0 + 1000000; b.name = QStringLiteral("address");
        t.bands = { b };
        t.ball.found = true; t.ball.x = 610; t.ball.y = 470; t.ball.source = QStringLiteral("bright");
        t.configJson = QJsonObject{ {QStringLiteral("enabled"), true} };
        t.configHash = QStringLiteral("cafe");
        t.streamSerial = QStringLiteral("DTLSER");
        aD.versions.shaftDtl = kDtlShaftStageVersion;

        const QString dirD = dir + QStringLiteral("_dtl");
        QDir().mkpath(dirD);
        QString derr;
        check(SwingDocWriter::writeSwingJson(dirD, mD, &aD, &derr), "dtl write ok");
        const QJsonObject an = readManifest(dirD)[QStringLiteral("analysis")].toObject();

        // pose2d is poseTrackToJson verbatim (the factored builder), and poseDtl is the same
        // builder over the DTL track — one shape, two blocks.
        check(an[QStringLiteral("pose2d")].toObject() == poseTrackToJson(aD.pose2d, T0),
              "pose2d == poseTrackToJson(pose2d)");
        check(an.contains(QStringLiteral("poseDtl")), "poseDtl block present");
        const QJsonObject pd = an[QStringLiteral("poseDtl")].toObject();
        check(pd == poseTrackToJson(aD.poseDtl, T0), "poseDtl == poseTrackToJson(poseDtl)");
        check(pd[QStringLiteral("camera")].toInt() == 4, "poseDtl.camera");
        check(pd[QStringLiteral("frames")].toArray().size() == 3, "poseDtl frames 3");
        check(qint64(pd[QStringLiteral("frames")].toArray().at(2).toObject()[QStringLiteral("t_us")].toDouble())
                  == 1020000, "poseDtl t_us window-relative");
        check(pd[QStringLiteral("smoothed")].toArray().size() == 2, "poseDtl smoothed carried");
        check(pd[QStringLiteral("synth")].toArray().size() == 1, "poseDtl synth carried");

        const QJsonObject vv = an[QStringLiteral("versions")].toObject();
        check(vv[QStringLiteral("poseDtl")].toObject()[QStringLiteral("code")].toInt() == kDtlPoseStageVersion,
              "versions.poseDtl.code");
        check(vv[QStringLiteral("poseDtl")].toObject()[QStringLiteral("model")].toString() == QStringLiteral("m@1"),
              "versions.poseDtl.model");
        check(vv[QStringLiteral("shaftDtl")].toObject()[QStringLiteral("code")].toInt() == kDtlShaftStageVersion,
              "versions.shaftDtl.code");

        check(an.contains(QStringLiteral("clubDtl")), "clubDtl block present");
        const QJsonObject cd = an[QStringLiteral("clubDtl")].toObject();
        check(cd == dtlShaftTrackToJson(t, T0, t.configJson, t.configHash,
                                        QStringLiteral("DTL"), QStringLiteral("dtl.mp4")),
              "clubDtl == dtlShaftTrackToJson (the club_dtl.json bytes)");
        check(cd[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.clubDtl/1"), "clubDtl schema");
        check(cd[QStringLiteral("stream")].toObject()[QStringLiteral("alias")].toString() == QStringLiteral("DTL"),
              "clubDtl stream named from the recorded serial");
        const QJsonArray cf = cd[QStringLiteral("frames")].toArray();
        check(cf.size() == 2, "clubDtl frames 2 (absences included)");
        const QJsonObject c0 = cf.at(0).toObject(), c1 = cf.at(1).toObject();
        check(qint64(c0[QStringLiteral("t_us")].toDouble()) == 1000000, "clubDtl t_us window-relative");
        check(c0[QStringLiteral("tier")].toString() == QStringLiteral("RAY"), "clubDtl tier name");
        check(near(c0[QStringLiteral("head")].toArray().at(0).toDouble(), 0.6, 1e-12)
                  && near(c0[QStringLiteral("head")].toArray().at(1).toDouble(), 0.9, 1e-12),
              "clubDtl head normalised to the DTL frame");
        check(c1[QStringLiteral("theta")].isNull() && c1[QStringLiteral("grip")].isNull(),
              "clubDtl NaN written as null");
        check(c1[QStringLiteral("tier")].toString() == QStringLiteral("END_ON"), "clubDtl absence tier");
        const QJsonObject sum = cd[QStringLiteral("summary")].toObject();
        check(sum[QStringLiteral("configHash")].toString() == QStringLiteral("cafe"), "clubDtl configHash echoed");
        check(sum[QStringLiteral("ball")].toObject()[QStringLiteral("source")].toString() == QStringLiteral("bright"),
              "clubDtl ball source");
        check(cd[QStringLiteral("bands")].toArray().size() == 1, "clubDtl bands");

        // A single-camera analysis carries none of it: no blocks, no version keys.
        SwingAnalysis aS = aD;
        aS.poseDtl = PoseTrack2D{};
        aS.shaftDtl = DtlShaftTrack2D{};
        aS.versions.poseDtl = 0; aS.versions.shaftDtl = 0;
        check(SwingDocWriter::writeSwingJson(dirD, mD, &aS, &derr), "single-camera write ok");
        const QJsonObject anS = readManifest(dirD)[QStringLiteral("analysis")].toObject();
        check(!anS.contains(QStringLiteral("poseDtl")) && !anS.contains(QStringLiteral("clubDtl")),
              "single-camera: no poseDtl / clubDtl");
        const QJsonObject vS = anS[QStringLiteral("versions")].toObject();
        check(!vS.contains(QStringLiteral("poseDtl")) && !vS.contains(QStringLiteral("shaftDtl"))
                  && vS.size() == 4, "single-camera: versions block unchanged (pose/ball/shaft/impact)");
        check(anS[QStringLiteral("pose2d")].toObject() == an[QStringLiteral("pose2d")].toObject(),
              "pose2d identical with or without the DTL blocks");

        // An invalid DTL track that RAN is still written — its reasons are the record of why
        // the tile shows no shaft.
        SwingAnalysis aI = aD;
        aI.shaftDtl.valid = false;
        check(SwingDocWriter::writeSwingJson(dirD, mD, &aI, &derr), "invalid-track write ok");
        check(readManifest(dirD)[QStringLiteral("analysis")].toObject().contains(QStringLiteral("clubDtl")),
              "clubDtl written for an invalid track that ran");
        QDir(dirD).removeRecursively();
    }

    std::printf("\n=== analysis t_us normalised to window-relative ===\n");
    {
        // Live capture: absolute analysis t_us (clock domain) → written relative.
        const qint64 T0 = 176400665083LL;
        QJsonObject mAbs = manifest;
        mAbs[QStringLiteral("clock")] = QJsonObject{ {QStringLiteral("t0_us"), double(T0)} };
        SwingAnalysis aAbs;
        aAbs.tier = int(ReconstructionTier::Angles2D);
        aAbs.phases.push_back({ Phase::Impact, T0 + 1010000, 1.0f });
        aAbs.segmentation.swingStartUs = T0 + 250000;
        aAbs.segmentation.swingEndUs   = T0 + 1400000;
        aAbs.segmentation.version = 2;
        aAbs.shaft.camera = 3; aAbs.shaft.valid = true; aAbs.shaft.coverage = 0.9f;
        aAbs.shaft.frameWidth = 1920; aAbs.shaft.frameHeight = 1080;
        aAbs.shaft.samples.push_back({ T0 + 1000000, QPointF(960, 540), QPointF(960, 810),
                                       1.57, 0.0, 270.0, 0.9f, ShaftMeasured });
        const QString dirN = dir + QStringLiteral("_norm");
        QDir().mkpath(dirN);
        SwingDocWriter::writeSwingJson(dirN, mAbs, &aAbs, nullptr);
        if (SwingStore::hasDocument(dirN)) {
            const QJsonObject rn = SwingStore::load(dirN);
            const QJsonObject an = rn[QStringLiteral("analysis")].toObject();
            check(qint64(an[QStringLiteral("phases")].toArray().at(0).toObject()[QStringLiteral("t_us")].toDouble()) == 1010000,
                  "absolute phase t_us → window-relative");
            check(qint64(an[QStringLiteral("segmentation")].toObject()[QStringLiteral("swingStartUs")].toDouble()) == 250000,
                  "absolute swingStartUs → window-relative");
            check(qint64(an[QStringLiteral("club")].toObject()[QStringLiteral("samples")].toArray().at(0)
                            .toObject()[QStringLiteral("t_us")].toDouble()) == 1000000,
                  "absolute club sample t_us → window-relative");
        }

        // Re-analysis: already-relative t_us (≪ t0) pass through unchanged.
        SwingAnalysis aRel;
        aRel.phases.push_back({ Phase::Impact, 1010000, 1.0f });
        SwingDocWriter::writeSwingJson(dirN, mAbs, &aRel, nullptr);
        if (SwingStore::hasDocument(dirN)) {
            const QJsonObject rr = SwingStore::load(dirN);
            check(qint64(rr[QStringLiteral("analysis")].toObject()[QStringLiteral("phases")].toArray()
                            .at(0).toObject()[QStringLiteral("t_us")].toDouble()) == 1010000,
                  "relative phase t_us passed through (idempotent)");
        }
    }

    std::printf("\n=== raw-only write (analysis == nullptr) ===\n");
    SwingDocWriter::writeSwingJson(dir, manifest, nullptr);
    check(SwingStore::hasDocument(dir), "document written");
    const QJsonObject root2 = SwingStore::load(dir);
    check(!root2.contains(QStringLiteral("analysis")), "no analysis block when null");
    check(root2[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swing/2"), "schema still /2");

    // ── the summary ─────────────────────────────────────────────────────────
    // The scalars the session picker needs, so it never decodes a multi-MB pose track on the GUI
    // thread to build a row. A swing.ppsw carries them INSIDE, as the `summary` block in its root
    // chunk, written in the same atomic rename as everything else; a JSON-era swing.json keeps the
    // swing_summary.json sidecar. Everything below guards that both stay truthful — a summary that
    // silently goes stale, or one that is never actually read, reintroduces the freeze it exists
    // to prevent.
    std::printf("\n=== summary block: written with the document ===\n");
    {
        check(QFile::exists(dir + QStringLiteral("/swing.ppsw")), "the writers write swing.ppsw");
        check(!QFile::exists(dir + QStringLiteral("/swing.json")), "…and never swing.json");
        check(!QFile::exists(dir + QStringLiteral("/swing_summary.json")),
              "…and no sidecar: the summary is inside the document");
        const QJsonObject block = SwingStore::loadSummaryBlock(dir);
        // /4 (2026-09-16): + metrics, rating, note, lmDeviceKind, dataWarningDetail — the rest of what
        // a carousel row shows, so loading a session never fat-parses.
        check(block[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swingsummary/4"),
              "summary block schema tag");
        check(!block.contains(QStringLiteral("source")),
              "…with no freshness guard: it cannot be older than the file it is in");
    }

    std::printf("\n=== summary block: PARITY with the full reader ===\n");
    {
        const PersistedShot fat  = SwingDocReader::readSwingJson(dir);
        const SwingSummary  lean = SwingDocReader::readSwingSummary(dir);
        // Without this the whole section could pass while every read silently fell back to
        // the fat parse — i.e. correct data, and the stall quietly back.
        check(lean.fromSidecar, "parity exercised the SUMMARY BLOCK, not a fat fallback");
        check(lean.ok == fat.ok,                         "parity ok");
        check(lean.ordinal == fat.ordinal,               "parity ordinal");
        check(lean.timestampLabel == fat.timestampLabel, "parity timestampLabel");
        check(lean.wallclockMs == fat.wallclockMs,       "parity wallclockMs");
        check(lean.club == fat.club,                     "parity club");
        check(lean.hasVideo == fat.hasVideo,             "parity hasVideo");
        check(lean.thumbnailPath == fat.thumbnailPath,   "parity thumbnailPath");
        check(lean.score == fat.score,                   "parity score");
        // The /4 row fields. A carousel row is rebuilt from whichever of these two reads answered, and
        // its card fills every required property from a model role — so a summary that dropped these
        // would blank the metric chips, the stars and the warning tooltip on every reloaded shot, which
        // is exactly the regression the cheap session load could otherwise introduce.
        check(lean.metrics == fat.metrics,                     "parity metrics (the card's chips)");
        check(lean.rating == fat.rating,                       "parity rating");
        check(lean.note == fat.note,                           "parity note");
        check(lean.lmDeviceKind == fat.lmDeviceKind,           "parity lmDeviceKind");
        check(lean.dataWarning == fat.dataWarning,             "parity dataWarning");
        check(lean.dataWarningDetail == fat.dataWarningDetail, "parity dataWarningDetail");
    }

    std::printf("\n=== summary block: review write-through keeps it fresh ===\n");
    {
        check(SwingDocWriter::updateReview(dir, 3, QStringLiteral("after-index"),
                                           QStringLiteral("5 WOOD"), nullptr),
              "updateReview ok");
        const SwingSummary s = SwingDocReader::readSwingSummary(dir);
        check(s.fromSidecar, "the block is still the cheap path after updateReview");
        check(s.club == QStringLiteral("5 WOOD") && s.rating == 3, "…and carries the new club and stars");
    }

    std::printf("\n=== summary block: an unknown schema is a miss, not an error ===\n");
    {
        // Written straight through the store, around the writer, the way a future build's block
        // would arrive. Must name a version this build does NOT write, or the case tests nothing.
        QJsonObject doc = SwingStore::load(dir);
        QJsonObject blk = doc[QStringLiteral("summary")].toObject();
        blk[QStringLiteral("schema")] = QStringLiteral("pinpoint.swingsummary/99");
        doc[QStringLiteral("summary")] = blk;
        check(SwingStore::save(dir, doc), "store write of a future-schema block");
        const SwingSummary s = SwingDocReader::readSwingSummary(dir);
        check(!s.fromSidecar && s.ok, "unknown schema → derived from the document, still good data");
        check(!SwingDocReader::readSwingSummary(dir, /*writeSidecar=*/false).ok,
              "…and a caller that must not block gets !ok rather than a fat parse");
        check(SwingDocWriter::updateReview(dir, 3, QStringLiteral("after-index"),
                                           QStringLiteral("5 WOOD"), nullptr),
              "a real write");
        check(SwingDocReader::readSwingSummary(dir).fromSidecar, "…refreshes the block");
    }

    // ── A JSON-era swing: read indefinitely, sidecar-indexed, migrated by its first rewrite ────
    // Built from the document above so it carries every block the parity checks read.
    const QString dirJ = QStringLiteral("/tmp/swingdoc_test_json");
    QDir(dirJ).removeRecursively();
    QDir().mkpath(dirJ);
    {
        QJsonObject doc = SwingStore::load(dir);
        doc.remove(QStringLiteral("summary"));   // a JSON-era document never had one
        QFile o(dirJ + QStringLiteral("/swing.json"));
        check(o.open(QIODevice::WriteOnly) && o.write(SwingStore::toJsonText(doc)) > 0,
              "legacy swing.json fixture written");
    }
    const QString sumPath = dirJ + QStringLiteral("/swing_summary.json");

    std::printf("\n=== JSON-era sidecar: written by the first indexing read ===\n");
    {
        check(SwingStore::info(dirJ).format == SwingStore::Format::Json, "the store reads it as JSON");
        check(!QFile::exists(sumPath), "no sidecar before anything indexed it");
        const SwingSummary first = SwingDocReader::readSwingSummary(dirJ);
        check(first.ok && !first.fromSidecar, "the first read parses the document");
        check(QFile::exists(sumPath), "…and writes the sidecar");

        QFile f(sumPath);
        if (!f.open(QIODevice::ReadOnly)) return 1;
        const QJsonObject s = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        check(s[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swingsummary/4"),
              "sidecar schema tag");
        const QFileInfo srcInfo(dirJ + QStringLiteral("/swing.json"));
        const QJsonObject src = s[QStringLiteral("source")].toObject();
        check(qint64(src[QStringLiteral("size")].toDouble()) == srcInfo.size(),
              "sidecar stamps the source size");
        check(qint64(src[QStringLiteral("mtime_ms")].toDouble())
                  == srcInfo.lastModified().toMSecsSinceEpoch(),
              "sidecar stamps the source mtime");
    }

    std::printf("\n=== JSON-era sidecar: PARITY with the full reader ===\n");
    {
        const PersistedShot fat  = SwingDocReader::readSwingJson(dirJ);
        const SwingSummary  lean = SwingDocReader::readSwingSummary(dirJ);
        check(lean.fromSidecar, "parity exercised the SIDECAR path, not a fat fallback");
        check(lean.ok == fat.ok,                         "parity ok");
        check(lean.ordinal == fat.ordinal,               "parity ordinal");
        check(lean.timestampLabel == fat.timestampLabel, "parity timestampLabel");
        check(lean.wallclockMs == fat.wallclockMs,       "parity wallclockMs");
        check(lean.club == fat.club,                     "parity club");
        check(lean.hasVideo == fat.hasVideo,             "parity hasVideo");
        check(lean.thumbnailPath == fat.thumbnailPath,   "parity thumbnailPath");
        check(lean.score == fat.score,                   "parity score");
        // The /4 row fields. A carousel row is rebuilt from whichever of these two reads answered, and
        // its card fills every required property from a model role — so a summary that dropped these
        // would blank the metric chips, the stars and the warning tooltip on every reloaded shot, which
        // is exactly the regression the cheap session load could otherwise introduce.
        check(lean.metrics == fat.metrics,                     "parity metrics (the card's chips)");
        check(lean.rating == fat.rating,                       "parity rating");
        check(lean.note == fat.note,                           "parity note");
        check(lean.lmDeviceKind == fat.lmDeviceKind,           "parity lmDeviceKind");
        check(lean.dataWarning == fat.dataWarning,             "parity dataWarning");
        check(lean.dataWarningDetail == fat.dataWarningDetail, "parity dataWarningDetail");

        // Delete it: the fallback must produce identical values AND self-heal.
        QFile::remove(sumPath);
        const SwingSummary rebuilt = SwingDocReader::readSwingSummary(dirJ);
        check(!rebuilt.fromSidecar, "deleted sidecar → fat fallback");
        check(rebuilt.ok == fat.ok && rebuilt.ordinal == fat.ordinal
                  && rebuilt.timestampLabel == fat.timestampLabel
                  && rebuilt.wallclockMs == fat.wallclockMs && rebuilt.club == fat.club
                  && rebuilt.hasVideo == fat.hasVideo
                  && rebuilt.thumbnailPath == fat.thumbnailPath
                  && rebuilt.score == fat.score,
              "fat-fallback parity (identical to the sidecar path)");
        check(QFile::exists(sumPath), "fallback self-heals: sidecar rewritten");
        check(SwingDocReader::readSwingSummary(dirJ).fromSidecar, "healed sidecar is used next time");
    }

    std::printf("\n=== JSON-era sidecar: stale detection ===\n");
    {
        const auto patchSidecar = [&](const char *key, const QJsonValue &v) {
            QFile f(sumPath);
            if (!f.open(QIODevice::ReadOnly)) return;
            QJsonObject s = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            if (QLatin1String(key) == QLatin1String("schema")) {
                s[QStringLiteral("schema")] = v;
            } else {
                QJsonObject src = s[QStringLiteral("source")].toObject();
                src[QString::fromLatin1(key)] = v;
                s[QStringLiteral("source")] = src;
            }
            QFile o(sumPath);
            if (!o.open(QIODevice::WriteOnly)) return;
            o.write(QJsonDocument(s).toJson(QJsonDocument::Compact));
        };

        patchSidecar("size", QJsonValue(1.0));
        check(!SwingDocReader::readSwingSummary(dirJ).fromSidecar, "wrong source.size → stale");
        check(SwingDocReader::readSwingSummary(dirJ).fromSidecar,  "…and is rewritten correctly");

        patchSidecar("mtime_ms", QJsonValue(1.0));
        check(!SwingDocReader::readSwingSummary(dirJ).fromSidecar, "wrong source.mtime_ms → stale");
        check(SwingDocReader::readSwingSummary(dirJ).fromSidecar,  "…and is rewritten correctly");

        patchSidecar("schema", QJsonValue(QStringLiteral("pinpoint.swingsummary/99")));
        check(!SwingDocReader::readSwingSummary(dirJ).fromSidecar, "unknown schema → miss, not error");
        check(SwingDocReader::readSwingSummary(dirJ).ok,           "…and still returns good data");
    }

    std::printf("\n=== summary: missing and orphan documents ===\n");
    {
        const SwingSummary none = SwingDocReader::readSwingSummary(
            QStringLiteral("/tmp/swingdoc_test_nope"));
        check(!none.ok, "no document → !ok");
        check(!QFile::exists(QStringLiteral("/tmp/swingdoc_test_nope/swing_summary.json")),
              "no sidecar created for a missing document");

        // An orphan sidecar (swing.json trashed under it) must never be trusted: a
        // recreated swing_NNNN dir would otherwise show the previous occupant's data.
        const QString orphanDir = QStringLiteral("/tmp/swingdoc_test_orphan");
        QDir().mkpath(orphanDir);
        QFile::copy(sumPath, orphanDir + QStringLiteral("/swing_summary.json"));
        check(!SwingDocReader::readSwingSummary(orphanDir).ok, "orphan sidecar → !ok (fail closed)");
        QDir(orphanDir).removeRecursively();
    }

    std::printf("\n=== JSON-era swing: the first rewrite migrates it ===\n");
    {
        const PersistedShot before = SwingDocReader::readSwingJson(dirJ);
        check(SwingDocWriter::updateReview(dirJ, 2, QStringLiteral("migrated"),
                                           QStringLiteral("9 IRON"), nullptr),
              "updateReview on a JSON-era swing");
        check(QFile::exists(dirJ + QStringLiteral("/swing.ppsw")), "…writes swing.ppsw");
        check(!QFile::exists(dirJ + QStringLiteral("/swing.json")), "…and retires swing.json");
        check(!QFile::exists(sumPath), "…and its sidecar");
        const PersistedShot after = SwingDocReader::readSwingJson(dirJ);
        check(after.ok && after.club == QStringLiteral("9 IRON") && after.rating == 2,
              "…carrying the edit");
        check(after.metrics == before.metrics && after.score == before.score,
              "…and everything it did not edit");
        check(SwingDocReader::readSwingSummary(dirJ).fromSidecar, "…indexed by its summary block");
    }
    QDir(dirJ).removeRecursively();

    std::printf("\n=== summary sidecar: default parity (no review block) ===\n");
    {
        // A doc that never had a review block: both paths must land on the "DRIVER" stub.
        const QString d2 = QStringLiteral("/tmp/swingdoc_test_noreview");
        QDir().mkpath(d2);
        QString werr;
        check(SwingDocWriter::writeSwingJson(d2, manifest, nullptr, &werr), "write bare doc");
        const PersistedShot fat  = SwingDocReader::readSwingJson(d2);
        const SwingSummary  lean = SwingDocReader::readSwingSummary(d2);
        check(lean.fromSidecar, "bare doc indexed at write time");
        check(lean.club == QStringLiteral("DRIVER") && fat.club == lean.club,
              "no review block → DRIVER on both paths");
        check(lean.score == fat.score, "no analysis block → score parity");
        QDir(d2).removeRecursively();
    }

    std::printf("\n=== club: the capture-time pick survives a document round-trip ===\n");
    {
        // THE REGRESSION THIS SECTION EXISTS FOR. The camera path recorded the session's
        // active club nowhere a reader looked: writeSwingJson wrote no review block, and
        // summaryFromRoot defaulted to the stub, so every shot the user never rated read
        // back as DRIVER however carefully they had picked a club. Both halves are checked
        // — what the writer stores, and what each reader resolves.
        const QString d5 = QStringLiteral("/tmp/swingdoc_test_club");
        QDir().mkpath(d5);
        QString werr;
        check(SwingDocWriter::writeSwingJson(d5, manifest, nullptr, &werr,
                                             QStringLiteral("7 IRON")),
              "write with a capture club");

        check(SwingStore::hasDocument(d5), "document written");
        const QJsonObject croot = SwingStore::load(d5);
        check(croot[QStringLiteral("review")].toObject()[QStringLiteral("club")].toString()
                  == QStringLiteral("7 IRON"),
              "capture club seeded into review.club");

        const PersistedShot cfat  = SwingDocReader::readSwingJson(d5);
        const SwingSummary  clean = SwingDocReader::readSwingSummary(d5);
        check(clean.club == QStringLiteral("7 IRON") && cfat.club == clean.club,
              "capture club on both read paths");
        QDir(d5).removeRecursively();

        // A pre-review document that carries only capture.club.name — every camera swing
        // written between that field landing and this fix. The stub must not win.
        const QString d6 = QStringLiteral("/tmp/swingdoc_test_capname");
        QDir().mkpath(d6);
        QJsonObject capManifest = manifest;
        QJsonObject cap = capManifest[QStringLiteral("capture")].toObject();
        cap[QStringLiteral("club")] = QJsonObject{
            { QStringLiteral("name"),     QStringLiteral("GAP WEDGE") },
            { QStringLiteral("lengthMm"), 890 } };
        capManifest[QStringLiteral("capture")] = cap;
        check(SwingDocWriter::writeSwingJson(d6, capManifest, nullptr, &werr), "write, no club arg");
        check(SwingDocReader::readSwingSummary(d6).club == QStringLiteral("GAP WEDGE")
                  && SwingDocReader::readSwingJson(d6).club == QStringLiteral("GAP WEDGE"),
              "capture.club.name beats the stub on both paths");

        // …and the user's own correction still outranks what capture recorded.
        check(SwingDocWriter::updateReview(d6, 0, QString(), QStringLiteral("PUTTER"), nullptr),
              "correct the club");
        check(SwingDocReader::readSwingSummary(d6).club == QStringLiteral("PUTTER"),
              "review.club beats capture.club.name");

        // Re-analysis hands the WHOLE existing document back as the manifest. Seeding a
        // club there must not cost the user the rating and note they already wrote.
        check(SwingDocWriter::updateReview(d6, 5, QStringLiteral("keep me"),
                                           QStringLiteral("PUTTER"), nullptr),
              "rate and annotate it");
        check(SwingStore::hasDocument(d6), "document written");
        const QJsonObject reManifest = SwingStore::load(d6);
        check(SwingDocWriter::writeSwingJson(d6, reManifest, nullptr, &werr,
                                             QStringLiteral("3 WOOD")),
              "re-write with a different capture club");
        const PersistedShot kept = SwingDocReader::readSwingJson(d6);
        check(kept.club == QStringLiteral("PUTTER"), "existing review.club is not overwritten");
        check(kept.rating == 5 && kept.note == QStringLiteral("keep me"),
              "…and the rating and note survive with it");
        QDir(d6).removeRecursively();
    }

    std::printf("\n=== launch monitor: late reading folded into an analysed swing ===\n");
    {
        // END TO END, from the bytes a launch monitor actually writes. The reading is
        // parsed from the real LastShot.CSV layout rather than hand-built, so a change
        // that breaks the connection between the parser and the writer fails here even
        // though each half still passes its own test.
        const QString d3 = QStringLiteral("/tmp/swingdoc_test_lm");
        QDir().mkpath(d3);

        QJsonObject m3 = manifest;
        m3[QStringLiteral("capture")] = QJsonObject{ { QStringLiteral("impactUs"), 1234567 } };

        SwingAnalysis an;
        an.tier = int(ReconstructionTier::Angles2D);
        MetricSeries own;
        own.key = QStringLiteral("clubheadSpeed");
        own.label = QStringLiteral("Clubhead speed");
        own.unit = QStringLiteral("mph");
        own.phaseSamples.push_back({ Phase::Impact, 1234567, 84.0, QString() });
        an.series.push_back(own);
        QString werr;
        check(SwingDocWriter::writeSwingJson(d3, m3, &an, &werr), "write an analysed doc");

        const QByteArray csv =
            "Shot ID, Club, Club head Speed (m/s), Ball Speed (m/s), Total Spin (rpm), "
            "Carry (m), Vert Path (deg), Loft (deg), Horiz Impact (mm)\r\n"
            "283, Irn, 38.978607, 49.923466, 7614, 137.551910, -6.255224, 29.739687, 2.136441\r\n";
        auto reading = pinpoint::lm::parseLastShotCsv(csv);
        check(reading.has_value(), "the device's own bytes parse");
        reading->deviceKind = QStringLiteral("gcquad");
        reading->sourcePath = QStringLiteral("/tmp/LastShot.csv");
        reading->readAtMs   = 1700000000000LL;

        QString lerr;
        check(SwingDocWriter::updateLaunchMonitor(d3, *reading, &lerr),
              "the reading folds into the existing document");

        check(SwingStore::hasDocument(d3), "document written");
        const QJsonObject root = SwingStore::load(d3);

        const QJsonObject raw = root[QStringLiteral("launchMonitor")].toObject();
        check(raw[QStringLiteral("deviceShotId")].toString() == QStringLiteral("283"),
              "the raw block records the device's own shot id");
        check(raw[QStringLiteral("deviceClub")].toString() == QStringLiteral("Irn"),
              "…and the club it thought was in use");
        check(qAbs(raw[QStringLiteral("clubheadSpeed")].toDouble() - 87.1927) < 0.01,
              "…with values already in catalogue units");

        const QJsonArray mets = root[QStringLiteral("analysis")].toObject()
                                    [QStringLiteral("metrics")].toArray();
        int bare = 0, measured = 0, atImpact = 0, emptyCurve = 0;
        double bareVal = 0, measuredVal = 0;
        for (const QJsonValue &v : mets) {
            const QJsonObject mo = v.toObject();
            const QString key = mo[QStringLiteral("key")].toString();
            const QJsonArray ps = mo[QStringLiteral("phaseSamples")].toArray();
            if (key == QStringLiteral("clubheadSpeed")) {
                ++bare; bareVal = ps.at(0).toObject()[QStringLiteral("value")].toDouble();
            }
            if (!key.startsWith(QStringLiteral("lm."))) continue;
            ++measured;
            if (mo[QStringLiteral("t_us")].toArray().isEmpty()
                && mo[QStringLiteral("value")].toArray().isEmpty()) ++emptyCurve;
            if (ps.size() == 1
                && ps.at(0).toObject()[QStringLiteral("phase")].toInt() == int(Phase::Impact)
                && ps.at(0).toObject()[QStringLiteral("t_us")].toInt() == 1234567) ++atImpact;
            if (key == QStringLiteral("lm.clubheadSpeed"))
                measuredVal = ps.at(0).toObject()[QStringLiteral("value")].toDouble();
        }
        // 7 metric columns (club speed, ball speed, spin, carry, vert path, loft, horiz
        // impact) plus smashFactor and spinLoft, both derived from them. NOT spinAxis —
        // this row carries no side/back spin split, and a value we cannot derive must be
        // absent rather than zero.
        check(measured == 9, "every reading became an lm. metric");
        check(emptyCurve == measured, "…each an empty curve, since a monitor reports one number");
        check(atImpact == measured, "…anchored at Impact, at the document's own impactUs");

        // THE WHOLE POINT. Our estimate is untouched and sits beside the measurement,
        // disagreeing — which is what makes the pair worth storing.
        check(bare == 1, "our own clubheadSpeed survives, exactly once");
        check(qAbs(bareVal - 84.0) < 1e-9, "…with the value our pipeline produced");
        check(qAbs(measuredVal - 87.1927) < 0.01, "…and the device's differs from it");

        // Re-applying must replace, not duplicate — the connector can legitimately be
        // asked to write the same swing twice.
        check(SwingDocWriter::updateLaunchMonitor(d3, *reading, &lerr), "re-apply succeeds");
        check(SwingStore::hasDocument(d3), "document written");
        const QJsonArray again = SwingStore::load(d3)
                                     [QStringLiteral("analysis")].toObject()
                                     [QStringLiteral("metrics")].toArray();
        check(again.size() == mets.size(), "re-applying replaces rather than appending");

        // ── Re-analysis must not evict the readings ─────────────────────────
        //
        // The whole existing document goes back in as the manifest, which is exactly what
        // reanalysis_controller hands over. serializeAnalysis rebuilds metrics[] from the
        // stages, and no stage produces an `lm.` row — the pairing wrote them straight into
        // the document — so without the carry-forward this call deletes every reading while
        // leaving the raw `launchMonitor` block untouched. That is the worst shape a loss can
        // take: a file that still looks complete, and a board that reads as a launch monitor
        // which was never connected.
        check(SwingStore::hasDocument(d3), "document written");
        const QJsonObject paired = SwingStore::load(d3);
        check(SwingDocWriter::writeSwingJson(d3, paired, &an, &werr),
              "re-analyse a swing that already carries a reading");

        check(SwingStore::hasDocument(d3), "document written");
        const QJsonObject after = SwingStore::load(d3);

        int lmAfter = 0, bareAfter = 0;
        double lmValAfter = 0;
        for (const QJsonValue &v : after[QStringLiteral("analysis")].toObject()
                                       [QStringLiteral("metrics")].toArray()) {
            const QJsonObject mo = v.toObject();
            const QString key = mo[QStringLiteral("key")].toString();
            if (key == QStringLiteral("clubheadSpeed")) ++bareAfter;
            if (!key.startsWith(QStringLiteral("lm."))) continue;
            ++lmAfter;
            if (key == QStringLiteral("lm.clubheadSpeed"))
                lmValAfter = mo[QStringLiteral("phaseSamples")].toArray().at(0).toObject()
                               [QStringLiteral("value")].toDouble();
        }
        check(lmAfter == measured, "re-analysis keeps every lm. reading");
        check(qAbs(lmValAfter - 87.1927) < 0.01, "…with the device's value unchanged");
        check(bareAfter == 1, "…and our own estimate still lands exactly once, not twice");
        check(after.contains(QStringLiteral("launchMonitor")), "…beside the raw block it came from");

        // A shot whose analysis failed still keeps what the device said.
        const QString d4 = QStringLiteral("/tmp/swingdoc_test_lm_noanalysis");
        QDir().mkpath(d4);
        check(SwingDocWriter::writeSwingJson(d4, m3, nullptr, &werr), "write a doc with no analysis");
        check(SwingDocWriter::updateLaunchMonitor(d4, *reading, &lerr), "…the reading still lands");
        check(SwingStore::hasDocument(d4), "document written");
        const QJsonObject r4 = SwingStore::load(d4);
        check(r4.contains(QStringLiteral("launchMonitor")), "…as a raw block");
        check(!r4.contains(QStringLiteral("analysis")), "…without inventing an analysis block");

        QDir(d3).removeRecursively();
        QDir(d4).removeRecursively();
    }

    std::printf("\n=== launch monitor: a shot only the device saw ===\n");
    {
        // No camera, no IMU, no buffer window — so no analysis and no export. The whole
        // document comes from the reading.
        const QString d5 = QStringLiteral("/tmp/swingdoc_test_lm_only");
        QDir().mkpath(d5);

        const QByteArray csv =
            "Shot ID, Club, Club head Speed (m/s), Ball Speed (m/s), Total Spin (rpm), Carry (m)\r\n"
            "901, Drv, 45.0, 66.0, 2600, 230.0\r\n";
        auto reading = pinpoint::lm::parseLastShotCsv(csv);
        check(reading.has_value(), "the device's bytes parse");
        reading->deviceKind = QStringLiteral("gcquad");
        reading->readAtMs   = 1700000000000LL;

        SwingDocWriter::DeviceOnlyMeta meta;
        meta.swingId     = QStringLiteral("swing_0001");
        meta.swingIndex  = 1;
        meta.sessionId   = QStringLiteral("2026-08-04_Test_Swing_01");
        meta.athleteName = QStringLiteral("Test Athlete");
        meta.athleteUuid = QStringLiteral("uuid-1234");
        meta.club        = QStringLiteral("DRIVER");
        meta.wallclockMs = 1700000000000LL;

        QString derr;
        check(SwingDocWriter::writeDeviceOnlySwing(d5, *reading, meta, &derr),
              "a device-only swing.json is written");

        check(SwingStore::hasDocument(d5), "document written");
        const QJsonObject root = SwingStore::load(d5);

        check(root[QStringLiteral("streams")].toArray().isEmpty(),
              "streams is empty — the fact, not a failure");
        check(!root.contains(QStringLiteral("thumbnail")),
              "no thumbnail block, rather than one pointing at a file never written");
        check(root[QStringLiteral("capture")].toObject()[QStringLiteral("shotSource")].toString()
                  == QStringLiteral("launchMonitor"),
              "the capture block names what saw the shot");
        check(root[QStringLiteral("capture")].toObject()[QStringLiteral("impactUs")].toInt() == -1,
              "impactUs stays UNKNOWN — the device says a ball was struck, never when");

        const QJsonObject an = root[QStringLiteral("analysis")].toObject();
        check(!an.contains(QStringLiteral("score")), "no score — nothing scored it");
        check(!an.contains(QStringLiteral("pose2d")) && !an.contains(QStringLiteral("club")),
              "…and no pose or club block to mistake for an empty analysis");

        // THE MANUFACTURED IMPACT, and why it has to be there: buildPhaseGrid returns an
        // empty grid on an empty phases[], so without this every reading would persist
        // and then resolve to nothing.
        const QJsonArray phases = an[QStringLiteral("phases")].toArray();
        check(phases.size() == 1, "exactly one phase event");
        check(phases.at(0).toObject()[QStringLiteral("phase")].toInt() == int(Phase::Impact),
              "…and it is Impact");

        // The readings are present and anchored where a measure will look for them.
        int atImpact = 0, lm = 0;
        for (const QJsonValue &v : an[QStringLiteral("metrics")].toArray()) {
            const QJsonObject mo = v.toObject();
            if (!mo[QStringLiteral("key")].toString().startsWith(QStringLiteral("lm."))) continue;
            ++lm;
            const QJsonArray ps = mo[QStringLiteral("phaseSamples")].toArray();
            if (ps.size() == 1
                && ps.at(0).toObject()[QStringLiteral("phase")].toInt() == int(Phase::Impact))
                ++atImpact;
        }
        // 4 metric columns (club speed, ball speed, spin, carry) + smashFactor derived
        // from the speed pair. NOT spinAxis (needs the side/back split) and NOT spinLoft
        // (needs loft and vert path) — a value we cannot derive is absent, never zero.
        check(lm == 5, "every reading became an lm. metric");
        check(atImpact == lm, "…each anchored at the manufactured Impact");

        // It reads back as an ordinary shot, with no video.
        const PersistedShot ps = SwingDocReader::readSwingJson(d5);
        check(ps.ok, "it reloads as a shot");
        check(!ps.hasVideo, "…knowing it has no video");
        check(ps.club == QStringLiteral("DRIVER"), "…carrying the app's club, not the device's");
        check(ps.athleteUuid == QStringLiteral("uuid-1234"), "…and the athlete a cohort resolves through");
        check(ps.score == 0, "…and no score");
        // WHICH DEVICE MEASURED IT, surviving the round trip. The session board captions a
        // reviewed session from this and nothing else — the connector attached today says
        // nothing about a session hit last week, and may not be attached at all.
        check(ps.lmDeviceKind == QStringLiteral("gcquad"), "…and the device that measured it");

        const SwingSummary sum = SwingDocReader::readSwingSummary(d5);
        check(sum.ok && sum.fromSidecar, "the picker sidecar was written at the same time");
        check(!sum.hasVideo, "…and it agrees there is no video");

        QDir(d5).removeRecursively();
    }

    // ── The IMU data-integrity block: written, read, and — the point — REMOVED ──
    //
    // This block used to be write-once. ShotProcessor put it in at capture and
    // nothing ever revisited it: writeSwingJson replaces only schema/review/
    // analysis, so a verdict reached by a build that could not yet tell a
    // host-fused lane from a wG3 rode through every re-analysis forever. That is
    // what badged eleven good wrist swings ⚠. applyImuIntegrity is the seam that
    // makes a re-analysis able to withdraw a claim, so removal is tested first.
    {
        std::printf("\n-- imuIntegrity: verdict lifecycle --\n");
        const QString d6 = dir + QStringLiteral("/integrity");
        QDir().mkpath(d6);

        ImuRefusionVerdict pass;                        // defaults: ok, nothing checked
        pass.sourcesChecked = 1;
        pass.worstMaxDeg    = 0.02;

        ImuRefusionVerdict fail;
        fail.ok             = false;
        fail.sourcesChecked = 1;
        fail.worstMaxDeg    = 17.5;

        // A wG3-only window: the check ran and found nothing it could ask.
        ImuRefusionVerdict notCheckable;                // sourcesChecked stays 0
        check(!notCheckable.warns(), "a verdict with nothing checked does not warn");

        // A failing verdict round-trips into the reader's dataWarning.
        QJsonObject m1 = manifest;
        applyImuIntegrity(m1, &fail);
        check(m1.contains(QStringLiteral("imuIntegrity")), "a failing verdict writes the block");
        QString e6;
        check(SwingDocWriter::writeSwingJson(d6, m1, &a, &e6), "…and the document writes");
        check(SwingDocReader::readSwingJson(d6).dataWarning, "…and reads back as a data warning");

        // Re-analysis reaches a PASS on the same swing: the warning must clear.
        QJsonObject m2 = readManifest(d6);
        check(m2.contains(QStringLiteral("imuIntegrity")), "the stale block is there to be replaced");
        applyImuIntegrity(m2, &pass);
        check(SwingDocWriter::writeSwingJson(d6, m2, &a, &e6), "the re-analysis writes back");
        check(!SwingDocReader::readSwingJson(d6).dataWarning, "…and the warning is gone");

        // Re-analysis that could NOT establish a verdict must withdraw the claim
        // entirely, not leave the old one standing and not fabricate a pass.
        QJsonObject m3 = readManifest(d6);
        applyImuIntegrity(m3, &fail);
        check(SwingDocWriter::writeSwingJson(d6, m3, &a, &e6), "a failing verdict is written again");
        check(SwingDocReader::readSwingJson(d6).dataWarning, "…and warns again");

        QJsonObject m4 = readManifest(d6);
        applyImuIntegrity(m4, nullptr);
        check(!m4.contains(QStringLiteral("imuIntegrity")), "nullptr REMOVES the block");
        check(SwingDocWriter::writeSwingJson(d6, m4, &a, &e6), "…the document writes without it");
        check(!SwingDocReader::readSwingJson(d6).dataWarning, "…and no warning survives");

        // sourcesChecked == 0 is the same no-claim outcome as nullptr — a wG3-only
        // swing must not persist as "checked and passed".
        QJsonObject m5 = manifest;
        applyImuIntegrity(m5, &fail);
        applyImuIntegrity(m5, &notCheckable);
        check(!m5.contains(QStringLiteral("imuIntegrity")),
              "a verdict with sourcesChecked==0 removes it too");

        // A legacy document that never carried the block stays clean.
        QJsonObject m6 = manifest;
        applyImuIntegrity(m6, nullptr);
        check(!m6.contains(QStringLiteral("imuIntegrity")), "removal on an absent key is a no-op");

        QDir(d6).removeRecursively();
    }

    // captureIntegrity: the same lifecycle for the frame-timestamp verdict, plus the
    // one thing that is new — dataWarningDetail carries the facts of BOTH blocks and the
    // summary sidecar carries the flag, because the session ledger judges from the
    // sidecar and must never count a shot whose recording is known broken.
    {
        std::printf("\n-- captureIntegrity: verdict lifecycle + detail --\n");
        const QString d7 = dir + QStringLiteral("/capture-integrity");
        QDir().mkpath(d7);

        pinpoint::CaptureIntegrityVerdict fail;
        fail.ok = false; fail.camerasChecked = 1; fail.holes = 3; fail.framesLost = 117;
        fail.worstHoleMs = 594.4; fail.postImpact = true;
        pinpoint::CaptureIntegrityVerdict pass;
        pass.camerasChecked = 1;
        pinpoint::CaptureIntegrityVerdict notCheckable;   // camerasChecked stays 0

        QJsonObject m1 = manifest;
        applyCaptureIntegrity(m1, &fail);
        check(m1.contains(QStringLiteral("captureIntegrity")), "a failing verdict writes the block");
        QString e7;
        check(SwingDocWriter::writeSwingJson(d7, m1, &a, &e7), "…and the document writes");
        const PersistedShot p1 = SwingDocReader::readSwingJson(d7);
        check(p1.dataWarning, "…and reads back as a data warning");
        check(p1.dataWarningDetail.value(QStringLiteral("capture")).toBool()
                  && !p1.dataWarningDetail.value(QStringLiteral("imu")).toBool()
                  && p1.dataWarningDetail.value(QStringLiteral("framesLost")).toInt() == 117
                  && p1.dataWarningDetail.value(QStringLiteral("postImpact")).toBool()
                  && !p1.dataWarningDetail.value(QStringLiteral("preImpact")).toBool(),
              "…with the capture facts in the detail");
        check(SwingDocReader::readSwingSummary(d7).dataWarning,
              "…and the summary sidecar carries the flag");
        check(SwingDocReader::readSwingSummary(d7).fromSidecar
                  && SwingDocReader::readSwingSummary(d7).dataWarning,
              "…on the cheap sidecar path too");

        // Both blocks failing: one warning, both facts.
        QJsonObject mBoth = readManifest(d7);
        ImuRefusionVerdict imuFail; imuFail.ok = false; imuFail.sourcesChecked = 1; imuFail.worstMaxDeg = 9.0;
        applyImuIntegrity(mBoth, &imuFail);
        check(SwingDocWriter::writeSwingJson(d7, mBoth, &a, &e7), "both blocks written");
        const QVariantMap both = SwingDocReader::readSwingJson(d7).dataWarningDetail;
        check(both.value(QStringLiteral("capture")).toBool() && both.value(QStringLiteral("imu")).toBool()
                  && both.value(QStringLiteral("worstMaxDeg")).toDouble() == 9.0,
              "…and the detail names both");

        // Re-analysis reaches a pass on the capture check and withdraws the IMU claim.
        QJsonObject m2 = readManifest(d7);
        applyCaptureIntegrity(m2, &pass);
        applyImuIntegrity(m2, nullptr);
        check(SwingDocWriter::writeSwingJson(d7, m2, &a, &e7), "the re-analysis writes back");
        const PersistedShot p2 = SwingDocReader::readSwingJson(d7);
        check(!p2.dataWarning && p2.dataWarningDetail.isEmpty(), "…and the warning is gone");
        check(!SwingDocReader::readSwingSummary(d7).dataWarning, "…from the sidecar as well");

        QJsonObject m3 = readManifest(d7);
        applyCaptureIntegrity(m3, &notCheckable);
        check(!m3.contains(QStringLiteral("captureIntegrity")),
              "a verdict with camerasChecked==0 removes the block (no claim, not a pass)");
        QJsonObject m4 = readManifest(d7);
        applyCaptureIntegrity(m4, nullptr);
        check(!m4.contains(QStringLiteral("captureIntegrity")), "nullptr removes it too");

        QDir(d7).removeRecursively();
    }

    // ── H-b: the `origin` block on a streams[] element ────────────────────
    //
    // The swing.json half of the link the PPCP ledger holds. The ledger keys on
    // the opaque PPCP identity and remembers which swing it landed in; this
    // remembers the opaque identity beside the stream it became. Either can be
    // rebuilt from the other, which is why they are LINKED and not merged.
    {
        std::printf("\n-- H-b: stream origin, written, read and forward-compatible --\n");
        const QString dO = QStringLiteral("/tmp/swingdoc_test_origin");
        QDir(dO).removeRecursively();
        QDir().mkpath(dO);

        SwingAnalysis aO;
        QString eO;
        QJsonObject mO = manifest;
        check(SwingDocWriter::writeSwingJson(dO, mO, &aO, &eO), "a document to attach origin to");

        // A clip has been ASKED for and has not arrived. There is no file yet, so
        // the element must not claim one.
        SwingDocWriter::StreamOrigin req;
        req.transport = QStringLiteral("ppcp");
        req.peerId    = QStringLiteral("peer:1f2e4491-b207-4265-af87-74287160f0b4");
        req.sessionId = QStringLiteral("ses:live-1");
        req.captureId = QStringLiteral("cap:abc");
        req.streamId  = QStringLiteral("st:0123456789abcdef:video");
        req.transfer  = QStringLiteral("requested");
        check(SwingDocWriter::updateStreamOrigin(dO, QStringLiteral("phoneWide"), req, &eO),
              "a pending clip gets a streams[] element");

        SwingDocWriter::StreamOrigin back =
            SwingDocReader::streamOrigin(dO, QStringLiteral("phoneWide"));
        check(back.transport == QStringLiteral("ppcp"),        "transport round-trips");
        check(back.peerId    == req.peerId,                    "the MINTING peer round-trips");
        check(back.captureId == QStringLiteral("cap:abc"),     "the opaque capture id round-trips");
        check(back.streamId  == req.streamId,                  "the hashed stream id round-trips");
        check(back.transfer  == QStringLiteral("requested"),   "our view of the exchange");
        check(back.completeness.isEmpty(),
              "and NO completeness yet — that is the owner's word, never inferred (I10)");

        {
            const QJsonObject root = readManifest(dO);
            const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
            bool pendingHasNoFile = false;
            for (const QJsonValue &v : streams) {
                const QJsonObject el = v.toObject();
                if (el.value(QStringLiteral("alias")).toString() != QStringLiteral("phoneWide"))
                    continue;
                pendingHasNoFile = !el.contains(QStringLiteral("file"));
            }
            check(pendingHasNoFile, "a pending element carries no `file` — readers skip it");
        }

        // The clip lands. Same element, replaced in place, not a second one.
        SwingDocWriter::StreamOrigin done = req;
        done.transfer     = QStringLiteral("complete");
        done.completeness = QStringLiteral("complete");
        done.committedAt  = QStringLiteral("2026-09-01T13:46:55.221Z");
        check(SwingDocWriter::updateStreamOrigin(dO, QStringLiteral("phoneWide"), done, &eO),
              "the landed clip updates the same element");
        {
            const QJsonArray streams = readManifest(dO).value(QStringLiteral("streams")).toArray();
            int phoneElements = 0;
            for (const QJsonValue &v : streams)
                if (v.toObject().value(QStringLiteral("alias")).toString()
                    == QStringLiteral("phoneWide")) ++phoneElements;
            check(phoneElements == 1, "idempotent — one element, not one per state change");
        }
        back = SwingDocReader::streamOrigin(dO, QStringLiteral("phoneWide"));
        check(back.transfer == QStringLiteral("complete"),   "transfer advanced");
        check(back.completeness == QStringLiteral("complete"), "the owner's assertion is carried");
        check(!back.committedAt.isEmpty(),                   "committedAt recorded");

        // `absent` is an ANSWER, not a failure (I10, 7.3b). It carries a reason.
        SwingDocWriter::StreamOrigin gone = req;
        gone.transfer     = QStringLiteral("absent");
        gone.completeness = QStringLiteral("absent");
        gone.absentReason = QStringLiteral("outside_buffer");
        check(SwingDocWriter::updateStreamOrigin(dO, QStringLiteral("phoneDtl"), gone, &eO),
              "a second phone answers absent");
        back = SwingDocReader::streamOrigin(dO, QStringLiteral("phoneDtl"));
        check(back.absentReason == QStringLiteral("outside_buffer"), "the reason round-trips");
        check(SwingDocReader::streamOrigin(dO, QStringLiteral("face-on")).isEmpty(),
              "a directly attached camera has no origin, and that is correct");
        check(SwingDocReader::streamOrigin(dO, QStringLiteral("nope")).isEmpty(),
              "an unknown alias answers empty rather than inventing one");

        // ⚠ A pending clip must not make a videoless swing claim video: the
        // carousel would offer a replay of bytes that are not there.
        {
            const QString dP = QStringLiteral("/tmp/swingdoc_test_origin_novideo");
            QDir(dP).removeRecursively();
            QDir().mkpath(dP);
            QJsonObject noVid = manifest;
            noVid[QStringLiteral("streams")] = QJsonArray{};   // a device-only shot
            SwingAnalysis aP;
            QString eP;
            check(SwingDocWriter::writeSwingJson(dP, noVid, &aP, &eP), "a swing with no streams");
            check(!SwingDocReader::readSwingJson(dP).hasVideo, "…has no video, before");
            check(SwingDocWriter::updateStreamOrigin(dP, QStringLiteral("phoneWide"), req, &eP),
                  "…a clip is asked for");
            check(!SwingDocReader::readSwingJson(dP).hasVideo,
                  "…and it STILL has no video: a pending element carries no file");
            check(!SwingDocReader::streamOrigin(dP, QStringLiteral("phoneWide")).isEmpty(),
                  "…though the swing does remember a clip is owed");
            QDir(dP).removeRecursively();
        }

        // ⭐ THE ACCEPTANCE CRITERION: a reader that knows nothing of `origin`
        // still loads the swing. Every existing consumer selects by `kind` and
        // reads named keys, so an added key is invisible to it.
        const PersistedShot shot = SwingDocReader::readSwingJson(dO);
        check(!shot.swingDir.isEmpty(), "a document carrying origin still loads");
        check(shot.hasVideo, "…and its ordinary video stream is still found");

        QDir(dO).removeRecursively();
    }

    // ── The sibling of CT-I37: origin is RECORDED, never INTERPRETED ──────
    //
    // Where a stream's bytes came from is not a property of the swing. A measure
    // that varied by transport would be measuring the network, and the same rule
    // already governs the `capture.host` block beside it. So no analysis code may
    // key on it.
    //
    // ⚠ THE CHECK IS ON THE JSON KEY, NOT THE ENGLISH WORD. `src/Analysis` is full
    // of legitimate "origin" — a coordinate origin, an "original" channel — so a
    // bare grep would be a false-positive machine and would be deleted by whoever
    // it first annoyed. The quoted literal is what a reader would have to write.
    {
        std::printf("\n-- origin is provenance: nothing in src/Analysis reads it --\n");
        const QString analysisDir = QStringLiteral(PP_SRC_DIR) + QStringLiteral("/Analysis");
        QStringList offenders;
        QDirIterator it(analysisDir, QStringList{ QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                        QDir::Files, QDirIterator::Subdirectories);
        int scanned = 0;
        while (it.hasNext()) {
            const QString f = it.next();
            QFile in(f);
            if (!in.open(QIODevice::ReadOnly)) continue;
            ++scanned;
            int lineNo = 0;
            while (!in.atEnd()) {
                const QString line = QString::fromUtf8(in.readLine());
                ++lineNo;
                if (line.contains(QStringLiteral("\"origin\"")))
                    offenders << (f + QStringLiteral(":%1").arg(lineNo));
            }
        }
        check(scanned > 0, "the analysis tree was actually scanned");
        if (!offenders.isEmpty())
            std::printf("      offenders:\n        %s\n",
                        qPrintable(offenders.join(QStringLiteral("\n        "))));
        check(offenders.isEmpty(), "src/Analysis never reads the `origin` key");
    }

    // ═══ Recorded products round-trip (recorded_products.h, 2026-09-17) ══════════════
    //
    // The club block and the ladder come back from the document as the structs that wrote
    // them, field for field, because the re-analysis reuse path (swing_reanalyzer.cpp) hands
    // exactly these to the Shaft stage in place of running the tracker. A field that drifts
    // between writer and reader is a track that silently changes on a metrics-only sweep.
    std::printf("=== recorded products: club + ladder round-trip ===\n");
    {
        SwingAnalysis b;
        b.tier = 0;
        b.phases.push_back({ Phase::Address, 300000, 0.95f, SegmentRole::Unknown });
        b.phases.push_back({ Phase::Top, 900000, 0.90f, SegmentRole::LeadHand });
        b.phases.push_back({ Phase::Impact, 1200000, 1.0f, SegmentRole::Unknown });
        b.segmentation.events       = b.phases;
        b.segmentation.swingStartUs = 250000;
        b.segmentation.swingEndUs   = 1500000;
        b.segmentation.conf         = 0.9f;
        b.segmentation.version      = 5;   // fused ⇒ timing is written
        b.phases[1].timing = TimingClass::Proxy;
        b.segmentation.events[1].timing = TimingClass::Proxy;
        FusionDecision fd; fd.phase = Phase::Top; fd.winner = SegmentRole::LeadHand;
        fd.loser = SegmentRole::Club; fd.deltaUs = 4200; fd.reason = FusionReason::Inserted;
        b.segmentation.fusion.push_back(fd);

        b.shaft.camera = 3; b.shaft.valid = true; b.shaft.coverage = 0.91f; b.shaft.imuVisionCorr = 0.5f;
        b.shaft.frameWidth = 1280; b.shaft.frameHeight = 1024;
        b.shaft.measuredClubLenPx = 301.5f; b.shaft.modelVisionResidualDeg = 2.5f;
        for (int i = 0; i < 6; ++i) {
            ShaftSample2D s;
            s.t_us = 300000 + i * 8333; s.gripPx = QPointF(600 + 5 * i, 650 - 3 * i);
            s.headPx = QPointF(400 + 7 * i, 900 - 2 * i); s.thetaRad = 1.9 + 0.01 * i;
            s.thetaDotRadS = -0.5 * i; s.visibleLenPx = 280 + i; s.conf = 0.7f;
            s.flags = uint16_t(i % 2 ? ShaftMeasured : (ShaftCoasted | ShaftBallAnchored));
            s.headConf = 0.4f; s.headSigmaPx = 2.5f; s.lineConf = (i == 2) ? 0.3f : -1.f;
            b.shaft.samples.push_back(s);
        }
        ShaftSample2D pr = b.shaft.samples[0]; pr.flags = ShaftKinematicPredicted; b.shaft.predicted.push_back(pr);
        ShaftSample2D sy = b.shaft.samples[1]; sy.flags = ShaftSynthesized; sy.t_us += 4167; b.shaft.synth.push_back(sy);
        ShaftPosition p1; p1.p = 1; p1.t_us = 300000; p1.gripPx = QPointF(600, 650); p1.headPx = QPointF(400, 900);
        p1.thetaRad = 1.9; p1.lenPx = 280; p1.conf = 0.8f; p1.sigmaThetaDeg = 1.5f; p1.sigmaLenPx = 4.f;
        p1.stackN = 3; p1.source = uint8_t(PositionSource::MilestoneFit);
        ShaftPosition p7 = p1; p7.p = 7; p7.t_us = 1200000; p7.source = uint8_t(PositionSource::TrackSample);
        p7.timing = TimingClass::Proxy;
        b.shaft.positions = { p1, p7 };
        b.shaft.lengths.ballPx = 290; b.shaft.lengths.fusedPx = 295.5; b.shaft.lengths.fusedSigmaPx = 6.5;
        b.shaft.lengths.fusedConf = 0.77; b.shaft.lengths.ladderRung = 2; b.shaft.lengths.nEstimators = 3;
        b.shaft.plane.valid = true; b.shaft.plane.channel = 0;
        b.shaft.plane.measured.fitted = true; b.shaft.plane.measured.ratioDown = 0.74;
        b.shaft.plane.measured.nodeDownDeg = -1.2; b.shaft.plane.measured.rejectBack = 2;
        b.shaft.plane.synth.anchorsDown = 5; b.shaft.plane.synth.anchorConfMin = 0.35f;

        QTemporaryDir td;
        const QString dir2 = td.path() + QStringLiteral("/swing_0009");
        QDir().mkpath(dir2);
        QString err2;
        if (!SwingDocWriter::writeSwingJson(dir2, manifest, &b, &err2)) {
            std::printf("  [FAIL] write: %s\n", err2.toUtf8().constData());
            ++g_fail;
        } else {
            check(SwingStore::hasDocument(dir2), "document written");
            const QJsonObject an2 = SwingStore::load(dir2)[QStringLiteral("analysis")].toObject();
            const ShaftTrack2D t = shaftTrackFromAnalysisJson(an2[QStringLiteral("club")].toObject(), 3);
            check(t.valid && t.camera == 3 && t.frameWidth == 1280 && t.frameHeight == 1024, "club: header fields");
            check(near(t.coverage, 0.91, 1e-6) && near(t.measuredClubLenPx, 301.5, 1e-3)
                      && near(t.modelVisionResidualDeg, 2.5, 1e-6), "club: scalars");
            check(t.samples.size() == 6 && t.predicted.size() == 1 && t.synth.size() == 1 && t.positions.size() == 2,
                  "club: sample / predicted / synth / position counts");
            bool samplesExact = true;
            for (size_t i = 0; i < 6; ++i) {
                const ShaftSample2D &w = b.shaft.samples[i], &r = t.samples[i];
                // Times are written window-relative; the fixture window starts at 0.
                if (w.t_us != r.t_us || !near(w.gripPx.x(), r.gripPx.x(), 1e-6) || !near(w.headPx.y(), r.headPx.y(), 1e-6)
                    || !near(w.thetaRad, r.thetaRad, 1e-12) || !near(w.thetaDotRadS, r.thetaDotRadS, 1e-12)
                    || !near(w.visibleLenPx, r.visibleLenPx, 1e-9) || w.flags != r.flags
                    || !near(w.headConf, r.headConf, 1e-6) || !near(w.lineConf, r.lineConf, 1e-6))
                    samplesExact = false;
            }
            check(samplesExact, "club: every sample field round-trips (grip/head de-normalised by the frame)");
            check(t.positions[1].p == 7 && t.positions[0].stackN == 3
                      && t.positions[0].source == uint8_t(PositionSource::MilestoneFit)
                      && near(t.positions[0].sigmaThetaDeg, 1.5, 1e-6), "club: positions round-trip");
            check(t.positions[1].timing == TimingClass::Proxy && t.positions[0].timing == TimingClass::Measured,
                  "club: the anchors' timing class round-trips (the follow-through gate reads it)");
            check(near(t.lengths.fusedPx, 295.5, 1e-9) && t.lengths.ladderRung == 2 && t.lengths.nEstimators == 3
                      && near(t.lengths.ballPx, 290, 1e-9), "club: lengths round-trip");
            check(t.plane.valid && t.plane.channel == 0 && t.plane.measured.fitted
                      && near(t.plane.measured.ratioDown, 0.74, 1e-9) && t.plane.measured.rejectBack == 2
                      && t.plane.synth.anchorsDown == 5 && near(t.plane.synth.anchorConfMin, 0.35, 1e-6),
                  "club: plane round-trip");
            const std::optional<Segmentation> seg = segmentationFromAnalysisJson(an2);
            check(seg.has_value() && seg->events.size() == 3, "ladder: three phases back");
            check(seg && seg->events[1].phase == Phase::Top && seg->events[1].t_us == 900000
                      && seg->events[1].provenance == SegmentRole::LeadHand && seg->events[1].timing == TimingClass::Proxy,
                  "ladder: phase, instant, provenance and timing round-trip");
            check(seg && seg->swingStartUs == 250000 && seg->swingEndUs == 1500000 && near(seg->conf, 0.9, 1e-6)
                      && seg->version == 5, "ladder: bounds, conf and version round-trip");
            check(seg && seg->fusion.size() == 1 && seg->fusion[0].winner == SegmentRole::LeadHand
                      && seg->fusion[0].deltaUs == 4200, "ladder: fusion decisions round-trip");
            check(!shaftTrackFromAnalysisJson(QJsonObject(), 3).valid, "an absent club block reads as no track");
            check(!segmentationFromAnalysisJson(QJsonObject()).has_value(), "no phases reads as no ladder");
        }
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
