// Standalone test for SwingDocWriter. Build:
//   QT=~/Qt/6.11.0/gcc_64
//   g++ -std=c++17 -fPIC -I$QT/include -I$QT/include/QtCore -I$QT/include/QtGui -Isrc/Buffer \
//       src/Export/swing_doc_test.cpp src/Export/swing_doc.cpp -o /tmp/sd_test \
//       -L$QT/lib -lQt6Gui -lQt6Core -Wl,-rpath,$QT/lib && /tmp/sd_test

#include "swing_doc.h"
#include "../Analysis/swing_analysis.h"

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
