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

// The stored score reads the same through every path (audit H-01 / H-08, Stage 0.6).
//
// `analysis.score` is a bare int in pinpoint.analysis/2 documents and a ScoreBreakdown object
// ({kind, overall, ...}) from /3 on. The replay (DiskReplaySource::load) used to read it with
// `.toInt()`, so every /3 swing replayed with overall 0 while the summary and the full reader got
// the right number; so did the data viewer's Properties "Score" row (SwingDataSource). Every reader now
// takes it through pinpoint::overallScoreFromAnalysis(); this suite writes one document of each shape and
// checks each path gives the stored overall.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "../../Export/swing_doc.h"
#include "../../Export/swing_store.h"
#include "../review/swing_data_source.h"
#include "../shot/disk_replay_source.h"

namespace {

// A minimal swing a replay will open: one video stream whose file exists and has frame stamps
// (load() refuses a swing with no playable stream before it reads the analysis), and an analysis
// block carrying `score` in whichever shape the case writes.
QJsonObject swingDoc(const QJsonValue &score)
{
    const double t0 = 1'000'000.0;
    QJsonArray tUs;
    for (int i = 0; i < 10; ++i)
        tUs.append(t0 + i * 10'000.0);
    const QJsonObject stream{
        { QStringLiteral("kind"),     QStringLiteral("video") },
        { QStringLiteral("alias"),    QStringLiteral("cam0") },
        { QStringLiteral("file"),     QStringLiteral("cam0.mp4") },
        { QStringLiteral("playback"), QJsonObject{ { QStringLiteral("fps"), 30.0 } } },
        { QStringLiteral("frames"),   QJsonObject{ { QStringLiteral("t_us"), tUs } } } };
    const QJsonObject analysis{
        { QStringLiteral("schema"),  QStringLiteral("pinpoint.analysis/3") },
        { QStringLiteral("tier"),    1 },
        { QStringLiteral("score"),   score },
        { QStringLiteral("metrics"), QJsonArray{} },
        { QStringLiteral("phases"),  QJsonArray{} } };
    return QJsonObject{
        { QStringLiteral("schema"),   QStringLiteral("pinpoint.swing/2") },
        { QStringLiteral("swing"),    QJsonObject{ { QStringLiteral("index"), 1 } } },
        { QStringLiteral("clock"),    QJsonObject{ { QStringLiteral("t0_us"), t0 } } },
        { QStringLiteral("streams"),  QJsonArray{ stream } },
        { QStringLiteral("analysis"), analysis } };
}

// The value of one row of SwingDataSource::metadata() (the data viewer's Properties panel): groups of
// { group, rows: [{k, v}] }. A null QString when the group or the row is missing.
QString metadataRow(const QVariantList &metadata, const QString &group, const QString &key)
{
    for (const QVariant &g : metadata) {
        const QVariantMap gm = g.toMap();
        if (gm.value(QStringLiteral("group")).toString() != group) continue;
        for (const QVariant &r : gm.value(QStringLiteral("rows")).toList()) {
            const QVariantMap rm = r.toMap();
            if (rm.value(QStringLiteral("k")).toString() == key)
                return rm.value(QStringLiteral("v")).toString();
        }
    }
    return {};
}

} // namespace

class ReplayScoreTest : public QObject
{
    Q_OBJECT

private slots:
    void storedScoreReadsTheSameEverywhere_data()
    {
        QTest::addColumn<QJsonValue>("score");
        QTest::addColumn<int>("overall");
        QTest::newRow("/2 bare int") << QJsonValue(6) << 6;
        QTest::newRow("/3 ScoreBreakdown object")
            << QJsonValue(QJsonObject{ { QStringLiteral("kind"),    QStringLiteral("adherence") },
                                       { QStringLiteral("overall"), 73 } })
            << 73;
        QTest::newRow("/3 resemblance object")
            << QJsonValue(QJsonObject{ { QStringLiteral("kind"),     QStringLiteral("resemblance") },
                                       { QStringLiteral("overall"),  41 },
                                       { QStringLiteral("pattern"),  QStringLiteral("unknown") },
                                       { QStringLiteral("blended"),  false } })
            << 41;
    }

    void storedScoreReadsTheSameEverywhere()
    {
        QFETCH(QJsonValue, score);
        QFETCH(int, overall);

        // Both on-disk formats: a JSON-era swing.json (where every /2 document lives) and the store's
        // swing.ppsw (every swing written today), whose summary block is written with the document.
        for (const bool ppsw : { false, true }) {
            const QString fmt = ppsw ? QStringLiteral("ppsw") : QStringLiteral("json");
            QTemporaryDir tmp;
            QVERIFY(tmp.isValid());
            const QString dir = tmp.filePath(QStringLiteral("swing_0001"));
            QVERIFY(QDir().mkpath(dir));
            const QJsonObject root = swingDoc(score);
            if (ppsw) {
                QString err;
                QVERIFY2(pinpoint::SwingStore::save(dir, root, &err), qPrintable(err));
            } else {
                QFile j(dir + QStringLiteral("/swing.json"));
                QVERIFY(j.open(QIODevice::WriteOnly));
                QVERIFY(j.write(QJsonDocument(root).toJson()) > 0);
            }
            {
                // The replay checks only that the file exists before it queues it on a player.
                QFile v(dir + QStringLiteral("/cam0.mp4"));
                QVERIFY(v.open(QIODevice::WriteOnly));
            }

            // The rule itself.
            QCOMPARE(pinpoint::overallScoreFromAnalysis(root[QStringLiteral("analysis")].toObject()), overall);

            // The summary path (summaryFromRoot): read twice, so a JSON-era swing is read once by the
            // fallback parse and once from the sidecar that parse wrote; a swing.ppsw from its block.
            for (int pass = 0; pass < 2; ++pass) {
                const pinpoint::SwingSummary sum = pinpoint::SwingDocReader::readSwingSummary(dir);
                QVERIFY2(sum.ok, qPrintable(fmt));
                QCOMPARE(sum.score, overall);
            }

            // The full reader: the row's score and the analysisDetail's overall.
            const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(dir);
            QVERIFY2(ps.ok, qPrintable(fmt));
            QCOMPARE(ps.score, overall);
            QCOMPARE(ps.analysisDetail.value(QStringLiteral("overall")).toInt(), overall);

            // The replay path — the one that read a /3 object as 0.
            DiskReplaySource replay;
            QVERIFY2(replay.load(dir, 0.25, false), qPrintable(fmt));
            QVERIFY(replay.analysisDetail().contains(QStringLiteral("overall")));
            QCOMPARE(replay.analysisDetail().value(QStringLiteral("overall")).toInt(), overall);
            replay.unload();

            // The data viewer's Properties panel (SwingDataSource::metadata, Analysis → Score) — the one
            // place the stored score is on screen, and it too read a /3 object as 0.
            pinpoint::SwingDataSource data;
            data.setSwingDir(dir);                       // the reload is queued, so let it run
            QTRY_VERIFY_WITH_TIMEOUT(data.loaded(), 5000);
            QCOMPARE(metadataRow(data.metadata(), QStringLiteral("Analysis"), QStringLiteral("Score")),
                     QString::number(overall));
        }
    }

    // No score at all (a device-only swing writes none) reads as 0 on every path, as before.
    void absentScoreReadsZero()
    {
        QCOMPARE(pinpoint::overallScoreFromAnalysis(QJsonObject{}), 0);
        QCOMPARE(pinpoint::overallScoreFromAnalysis(
                     QJsonObject{ { QStringLiteral("score"), QJsonObject{} } }), 0);
    }
};

QTEST_GUILESS_MAIN(ReplayScoreTest)
#include "replay_score_test.moc"
