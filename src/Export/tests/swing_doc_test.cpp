// Standalone test for SwingDocWriter (round-trip). Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure

#include "../swing_doc.h"
#include "../swing_paths.h"
#include "../../Analysis/swing_analysis.h"

// Stub — avoids linking swing_paths.cpp (which pulls in the PpLogStream logging deps).
// SwingDocReader::latestSessionDir() (the only sanitise() user) isn't exercised here.
QString pinpoint::SwingPaths::sanitise(const QString &raw) { return raw; }

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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
    QFile f(dir + QStringLiteral("/swing.json"));
    if (!f.open(QIODevice::ReadOnly))  return 1;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();   // release the handle: on Windows an open reader blocks the
                 // QSaveFile atomic replace that updateReview()/writeSwingJson() do below

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
    check(mets.size() == 1, "one metric series");
    const QJsonObject m0 = mets.at(0).toObject();
    check(m0[QStringLiteral("key")].toString() == QStringLiteral("leadWristFlexExt"), "metric key");
    check(m0[QStringLiteral("t_us")].toArray().size() == 3 && m0[QStringLiteral("value")].toArray().size() == 3, "t_us + value arrays (len 3)");
    check(qFuzzyCompare(m0[QStringLiteral("value")].toArray().at(1).toDouble(), 12.5), "value[1] == 12.5");
    check(m0[QStringLiteral("phaseSamples")].toArray().size() == 1, "phaseSamples");
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
        check(pf0[QStringLiteral("kp")].toArray().size() == 17 * 3, "kp flat array 51 long");
        check(qFuzzyCompare(pf0[QStringLiteral("kp")].toArray().at(9 * 3).toDouble(), 0.40),
              "left-wrist x at kp[27]");
        check(qFuzzyCompare(pf0[QStringLiteral("handConf")].toDouble(), double(0.85f)), "handConf");
        check(qFuzzyCompare(pf0[QStringLiteral("lead")].toArray().at(1).toDouble(), 0.57),
              "lead hand y");

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
        const QVariantMap fe = ps.metrics.value(QStringLiteral("leadWristFlexExt")).toMap();
        check(fe.value(QStringLiteral("value")).toString() == QStringLiteral("-8°"), "flat metric value -8 deg");
        check(ps.analysisDetail.value(QStringLiteral("overall")).toInt() == 82, "analysisDetail.overall");
        check(ps.analysisDetail.value(QStringLiteral("series")).toList().size() == 1, "analysisDetail.series len 1");
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
        check(SwingDocWriter::updateReview(dir, 4, QStringLiteral("nice tempo"), &rerr),
              "updateReview ok");
        // The review block lands without disturbing the raw/analysis blocks.
        QFile fr(dir + QStringLiteral("/swing.json"));
        if (!fr.open(QIODevice::ReadOnly)) return 1;
        const QJsonObject r = QJsonDocument::fromJson(fr.readAll()).object();
        fr.close();   // release before the updateReview() rewrite below (Windows replace)
        check(r.contains(QStringLiteral("analysis")), "analysis block survives review write");
        const QJsonObject rv = r[QStringLiteral("review")].toObject();
        check(rv[QStringLiteral("rating")].toInt() == 4, "review.rating == 4");
        check(rv[QStringLiteral("note")].toString() == QStringLiteral("nice tempo"), "review.note");

        const PersistedShot ps = SwingDocReader::readSwingJson(dir);
        check(ps.rating == 4, "reader rating == 4");
        check(ps.note == QStringLiteral("nice tempo"), "reader note");

        // Rewriting replaces (does not append) the review block; clamps rating.
        check(SwingDocWriter::updateReview(dir, 9, QStringLiteral("re-rated"), nullptr),
              "updateReview rewrite ok");
        const PersistedShot ps2 = SwingDocReader::readSwingJson(dir);
        check(ps2.rating == 5, "reader rating clamped to 5");
        check(ps2.note == QStringLiteral("re-rated"), "reader note rewritten");

        // No swing.json → updateReview fails harmlessly (returns false).
        check(!SwingDocWriter::updateReview(QStringLiteral("/tmp/swingdoc_test_nope"), 3, QString(), nullptr),
              "updateReview on missing doc returns false");
    }

    std::printf("\n=== raw-only write (analysis == nullptr) ===\n");
    SwingDocWriter::writeSwingJson(dir, manifest, nullptr);
    QFile f2(dir + QStringLiteral("/swing.json"));
    if (!f2.open(QIODevice::ReadOnly)) return 1;
    const QJsonObject root2 = QJsonDocument::fromJson(f2.readAll()).object();
    f2.close();
    check(!root2.contains(QStringLiteral("analysis")), "no analysis block when null");
    check(root2[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swing/2"), "schema still /2");

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
