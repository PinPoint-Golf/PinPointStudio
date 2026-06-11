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
    manifest[QStringLiteral("streams")] = QJsonArray{ QJsonObject{ {QStringLiteral("kind"), QStringLiteral("video")},
                                                                   {QStringLiteral("alias"), QStringLiteral("face-on")} } };
    manifest[QStringLiteral("clock")]   = QJsonObject{ {QStringLiteral("wallclock"), QStringLiteral("2026-06-08T16:00:00.000")} };
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

    std::printf("=== unified write (raw + analysis) ===\n");
    QString err;
    if (!SwingDocWriter::writeSwingJson(dir, manifest, &a, &err)) {
        std::printf("  [FAIL] write: %s\n", err.toUtf8().constData());
        return 1;
    }
    QFile f(dir + QStringLiteral("/swing.json"));
    if (!f.open(QIODevice::ReadOnly))  return 1;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    check(root[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swing/2"), "schema bumped to pinpoint.swing/2");
    check(root.contains(QStringLiteral("streams")), "raw streams preserved");
    check(root[QStringLiteral("swing")].toObject()[QStringLiteral("id")].toString() == QStringLiteral("swing_0007"), "raw swing block preserved");
    check(root.contains(QStringLiteral("analysis")), "inline analysis block present");
    const QJsonObject an = root[QStringLiteral("analysis")].toObject();
    check(an[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.analysis/2"), "analysis schema /2");
    check(an[QStringLiteral("score")].toInt() == 82, "analysis score = 82");
    check(an[QStringLiteral("tier")].toInt() == int(ReconstructionTier::Mono3DPlusImu), "tier");
    const QJsonArray mets = an[QStringLiteral("metrics")].toArray();
    check(mets.size() == 1, "one metric series");
    const QJsonObject m0 = mets.at(0).toObject();
    check(m0[QStringLiteral("key")].toString() == QStringLiteral("leadWristFlexExt"), "metric key");
    check(m0[QStringLiteral("t_us")].toArray().size() == 3 && m0[QStringLiteral("value")].toArray().size() == 3, "t_us + value arrays (len 3)");
    check(qFuzzyCompare(m0[QStringLiteral("value")].toArray().at(1).toDouble(), 12.5), "value[1] == 12.5");
    check(m0[QStringLiteral("phaseSamples")].toArray().size() == 1, "phaseSamples");
    check(an[QStringLiteral("phases")].toArray().size() == 1, "phases array");
    check(static_cast<qint64>(an[QStringLiteral("phases")].toArray().at(0).toObject()[QStringLiteral("t_us")].toDouble()) == 1010000, "phase t_us preserved");

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
        check(ps.analysisDetail.value(QStringLiteral("phases")).toList().size() == 1, "analysisDetail.phases len 1");
        const QVariantMap p2 = ps.analysisDetail.value(QStringLiteral("pose2d")).toMap();
        check(p2.value(QStringLiteral("frames")).toList().size() == 1, "reloaded pose2d frames");
        const QVariantMap cb = ps.analysisDetail.value(QStringLiteral("club")).toMap();
        check(cb.value(QStringLiteral("valid")).toBool(), "reloaded club.valid");
        check(cb.value(QStringLiteral("samples")).toList().size() == 2, "reloaded club samples");
        const QVariantMap cs1 = cb.value(QStringLiteral("samples")).toList().at(1).toMap();
        check(cs1.value(QStringLiteral("flags")).toInt()
                  == int(ShaftImuBridged | ShaftHeadProjected), "reloaded sample flags");
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
    check(!root2.contains(QStringLiteral("analysis")), "no analysis block when null");
    check(root2[QStringLiteral("schema")].toString() == QStringLiteral("pinpoint.swing/2"), "schema still /2");

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
