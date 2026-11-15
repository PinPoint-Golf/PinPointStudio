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

// The one projection (refactor step 1.3b; audit H-01's missing test).
//
// A shot's analysis is shown from ONE QML map, built from the disk form of the analysis block
// (analysisDetailFor, swing_doc.h). The three paths that hand QML that map must agree key by key:
//
//   live    ShotProcessor's join: liveAnalysisDetail(the in-memory analysis, t0, the manifest)
//   reload  SwingDocReader::readSwingJson(dir).analysisDetail (the carousel row)
//   replay  DiskReplaySource::load(dir).analysisDetail() — once from the just-written cache, once parsed
//
// 1. A synthetic swing (analysis_fixture.h, every block's optional fields, plus a down-the-line club
//    track and the coach lines' edges), on the RING CLOCK as the stages produce it live: the three maps
//    are equal, every key the old live map carried is there, and every instant is window time. Its
//    window-time twin projects to the same map.
// 2. The legacy path: the same swing in an old document (absolute times, no `timeBase`) reads to the
//    same map — so every instant the projection surfaces is re-timed, and none twice.
// 3. One real pinned-pose swing (the corpus, read-only, copied to a temp dir; SKIPs without the share):
//    reload == replay key by key; and the live path on its typed form (analysisFromDocument) equals
//    them on every key the typed form carries (the writer-only blocks — clubDtl, addressMarks,
//    bindings, timings — have no typed reader, so `dtl`, `addressMarks`, `bindings` and `timings` are
//    compared reload against replay only). A stamped twin of the (unstamped) document reads the same.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>

#include "../../Export/swing_doc.h"
#include "../../Export/swing_store_repository.h"   // the one door: create() writes the document
#include "../../Export/tests/swing_doc_test_writes.h"   // testdoc::ref — the test folder's ShotRef
#include "../../Export/swing_store.h"
#include "../../Analysis/swing_analysis.h"
#include "../../Export/tests/analysis_fixture.h"
#include "../shot/disk_replay_source.h"

using namespace pinpoint;
using namespace pinpoint::analysis;

namespace {

// ── Key-by-key comparison ────────────────────────────────────────────────────────────────────────
// QML sees numbers, so a number is compared by value whatever its QVariant kind (a parsed document
// gives qlonglong where an in-memory one gave double); NaN equals NaN (the coach lines' unmeasured
// points). Everything else by kind and value, maps by key, lists by index.
bool isNumber(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Double: case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

bool isNullish(const QVariant &v)
{
    return !v.isValid() || v.typeId() == QMetaType::Nullptr;
}

void diffs(const QVariant &a, const QVariant &b, const QString &at, QStringList &out, double relTol = 0.0)
{
    if (out.size() >= 12)
        return;
    if (isNumber(a) && isNumber(b)) {
        const double x = a.toDouble(), y = b.toDouble();
        if (!(x == y || (std::isnan(x) && std::isnan(y))
              || (relTol > 0.0 && std::abs(x - y) <= relTol * std::max(1.0, std::abs(x)))))
            out << QStringLiteral("%1: %2 vs %3").arg(at).arg(x, 0, 'g', 17).arg(y, 0, 'g', 17);
        return;
    }
    if (isNullish(a) && isNullish(b))
        return;
    if (a.typeId() == QMetaType::QVariantMap && b.typeId() == QMetaType::QVariantMap) {
        const QVariantMap ma = a.toMap(), mb = b.toMap();
        QStringList keys = ma.keys();
        for (const QString &k : mb.keys())
            if (!ma.contains(k)) keys << k;
        for (const QString &k : keys) {
            const QString p = at + QLatin1Char('.') + k;
            if (!ma.contains(k))      out << p + QStringLiteral(": only in the second");
            else if (!mb.contains(k)) out << p + QStringLiteral(": only in the first");
            else                      diffs(ma.value(k), mb.value(k), p, out, relTol);
        }
        return;
    }
    if (a.typeId() == QMetaType::QVariantList && b.typeId() == QMetaType::QVariantList) {
        const QVariantList la = a.toList(), lb = b.toList();
        if (la.size() != lb.size()) {
            out << QStringLiteral("%1: %2 vs %3 entries").arg(at).arg(la.size()).arg(lb.size());
            return;
        }
        for (int i = 0; i < la.size(); ++i)
            diffs(la.at(i), lb.at(i), QStringLiteral("%1[%2]").arg(at).arg(i), out, relTol);
        return;
    }
    if (a.typeId() != b.typeId() || a != b)
        out << QStringLiteral("%1: %2 (%3) vs %4 (%5)").arg(at, a.toString(), QString::fromLatin1(a.typeName()),
                                                         b.toString(), QString::fromLatin1(b.typeName()));
}

QStringList mapDiffs(const QVariantMap &a, const QVariantMap &b, const QStringList &skipKeys = {},
                     double relTol = 0.0)
{
    QVariantMap x = a, y = b;
    for (const QString &k : skipKeys) { x.remove(k); y.remove(k); }
    QStringList out;
    diffs(x, y, QStringLiteral("$"), out, relTol);
    return out;
}

#define COMPARE_MAPS(a, b, what) do { \
        const QStringList d_ = mapDiffs((a), (b)); \
        QVERIFY2(d_.isEmpty(), qPrintable(QStringLiteral(what ": ") + d_.join(QStringLiteral("; ")))); \
    } while (0)

// ── The synthetic swing ──────────────────────────────────────────────────────────────────────────
constexpr int64_t kT0 = analysis_fixture::kT0;

// analysis_fixture's swing plus what that fixture leaves out because the round-trip test needs no
// typed reader for it: a down-the-line club track (measured and HELD frames, a found ball) and the
// coach lines' edges (both views and one DTL P-frame). On the clock `base` (0 = window time).
SwingAnalysis makeSwing(int64_t base)
{
    const auto T = [base](int64_t rel) { return base + rel; };
    SwingAnalysis a = analysis_fixture::makeAnalysis(base);

    DtlShaftTrack2D &d = a.shaftDtl;
    d.valid = true; d.camera = 5; d.frameWidth = 1024; d.frameHeight = 512;
    d.streamSerial = QStringLiteral("DTL-1");
    for (int i = 0; i < 4; ++i) {
        DtlSample s;
        s.t_us = T(2000 + i * 4096);
        s.gripPx = QPointF(256 + i * 8, 128 + i * 4);
        s.headPx = QPointF(512 + i * 16, 384 - i * 8);
        s.thetaRad = 0.25 * i; s.lenPx = 200.5; s.conf = 0.75f;
        s.tier = i == 2 ? DtlTier::Held : DtlTier::Ray;
        d.samples.push_back(s);
    }
    DtlBand band; band.lo = 0; band.hi = 3; band.loUs = T(2000); band.hiUs = T(14288); band.name = QStringLiteral("P1-P4");
    d.bands = { band };
    d.ball.found = true; d.ball.x = 512; d.ball.y = 448; d.ball.radiusPx = 8; d.ball.source = QStringLiteral("bright");
    a.versions.shaftDtl = 1;

    using namespace pinpoint::analysis::addressmarks;
    const auto marks = [&T](int64_t rel, double off) {
        ViewMarks v;
        v.found = true; v.frameTUs = T(rel);
        v.rowY = 0.5 + off; v.hipLeftX = 0.375; v.hipRightX = 0.625; v.leftX = 0.25; v.rightX = 0.75;
        v.buttX = 0.875; v.side = 1; v.rows = 6;
        return v;
    };
    a.addressMarks.faceOn = marks(800, 0.0);
    a.addressMarks.dtl    = marks(800, 0.0625);
    DtlPhaseMarks pm; pm.phase = int(Phase::Top); pm.eventTUs = T(6000); pm.v = marks(6000, 0.125);
    a.addressMarks.dtlPhases = { pm };
    a.addressMarks.phaseNeckFrac = 1.0;
    a.versions.addressMarks = 3;
    return a;
}

// A capture manifest: the face-on and DTL streams (window-relative frame stamps, files the replay
// checks exist) and an IMU integrity verdict that fails, so the whole-shot gate records are non-empty.
QJsonObject makeManifest(int64_t t0)
{
    const auto stream = [](const QString &alias, const QString &file, const QString &serial, int perspective) {
        QJsonArray tUs;
        for (int i = 0; i < 16; ++i) tUs.append(double(i * 1024));
        return QJsonObject{
            { QStringLiteral("kind"),     QStringLiteral("video") },
            { QStringLiteral("alias"),    alias },
            { QStringLiteral("file"),     file },
            { QStringLiteral("source"),   QJsonObject{ { QStringLiteral("serial"), serial },
                                                       { QStringLiteral("width"), 1024 },
                                                       { QStringLiteral("height"), 512 } } },
            { QStringLiteral("setup"),    QJsonObject{ { QStringLiteral("perspective"), perspective } } },
            { QStringLiteral("playback"), QJsonObject{ { QStringLiteral("fps"), 30.0 } } },
            { QStringLiteral("frames"),   QJsonObject{ { QStringLiteral("t_us"), tUs } } } };
    };
    return QJsonObject{
        { QStringLiteral("schema"),  QStringLiteral("pinpoint.swing/2") },
        { QStringLiteral("swing"),   QJsonObject{ { QStringLiteral("index"), 1 } } },
        { QStringLiteral("clock"),   QJsonObject{ { QStringLiteral("t0_us"), qint64(t0) } } },
        { QStringLiteral("window"),  QJsonObject{ { QStringLiteral("start_us"), 0 }, { QStringLiteral("end_us"), 16384 } } },
        { QStringLiteral("streams"), QJsonArray{ stream(QStringLiteral("Face-On"), QStringLiteral("fo.mp4"), QStringLiteral("FO-1"), 2),
                                                 stream(QStringLiteral("Down-the-Line"), QStringLiteral("dtl.mp4"), QStringLiteral("DTL-1"), 1) } },
        { QStringLiteral("imuIntegrity"), QJsonObject{ { QStringLiteral("sourcesChecked"), 2 },
                                                        { QStringLiteral("refusionOk"), false },
                                                        { QStringLiteral("worstMaxDeg"), 1.5 },
                                                        { QStringLiteral("thresholdDeg"), 0.5 } } } };
}

// The document, written through the one door (RecordingRepository::create — the live join's write from
// step 1.5): the manifest as the header, the analysis, the session's club.
bool createSwing(const QString &dir, const QJsonObject &manifest, const SwingAnalysis &a, QString *err)
{
    SwingStoreRecordingRepository repo(testdoc::shots());   // a test folder is a shot
    NewRecording rec;
    rec.header     = manifest;
    rec.analysis   = &a;
    rec.reviewClub = QStringLiteral("7i");
    const WriteResult r = repo.create(testdoc::ref(dir), rec, PersistOptions{});
    if (err) *err = r.error;
    return r.status == WriteResult::Status::Written;
}

// The replay checks only that a stream's file exists before it queues it on a player.
bool touchStreamFiles(const QString &dir, const QJsonObject &root)
{
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QString f = v.toObject().value(QStringLiteral("file")).toString();
        if (f.isEmpty() || v.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("video"))
            continue;
        QFile out(dir + QLatin1Char('/') + f);
        if (!out.exists() && !out.open(QIODevice::WriteOnly))
            return false;
    }
    return true;
}

QVariantMap replayMap(const QString &dir)
{
    DiskReplaySource replay;
    if (!replay.load(dir, 0.25, false))
        return {};
    const QVariantMap m = replay.analysisDetail();
    replay.unload();
    return m;
}

qint64 phaseUs(const QVariantMap &detail, Phase phase)
{
    for (const QVariant &v : detail.value(QStringLiteral("phases")).toList())
        if (v.toMap().value(QStringLiteral("phase")).toInt() == int(phase))
            return v.toMap().value(QStringLiteral("t_us")).toLongLong();
    return -1;
}

// The corpus swing: a two-camera pinned-pose swing (face-on + DTL; corpus/pose2), read-only.
QString corpusSwing()
{
    if (qEnvironmentVariableIsSet("PINPOINT_PROJECTION_SWING"))
        return qEnvironmentVariable("PINPOINT_PROJECTION_SWING");
    return QStringLiteral("/mnt/swingdata/corpus/swings/2026-07-03_Mark-Liversedge_Wrist_01/swing_0002");
}

} // namespace

class AnalysisDetailProjectionTest : public QObject
{
    Q_OBJECT

private slots:
    // 1. Live == reload == replay on the synthetic swing, in window time.
    void liveReloadAndReplayAgree()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.filePath(QStringLiteral("swing_0001"));
        QVERIFY(QDir().mkpath(dir));

        const SwingAnalysis a = makeSwing(kT0);   // the ring clock, as the stages produce it live
        QCOMPARE(int(a.timeBase), int(TimeBase::Clock));
        const QJsonObject manifest = makeManifest(kT0);

        const QVariantMap live = liveAnalysisDetail(a, kT0, manifest);

        QString err;
        QVERIFY2(createSwing(dir, manifest, a, &err), qPrintable(err));
        QVERIFY(touchStreamFiles(dir, manifest));

        const PersistedShot ps = SwingDocReader::readSwingJson(dir);
        QVERIFY(ps.ok);
        const QVariantMap replayCached = replayMap(dir);   // takes the just-written document
        const QVariantMap replayParsed = replayMap(dir);   // the cache is spent: parsed from the file
        QVERIFY(!replayCached.isEmpty());

        COMPARE_MAPS(live, ps.analysisDetail, "live vs reload");
        COMPARE_MAPS(live, replayCached, "live vs replay (cached document)");
        COMPARE_MAPS(live, replayParsed, "live vs replay (parsed document)");

        // Every key the old live map (toAnalysisDetail) carried is there — the maps are not equal by
        // being empty — plus the disk form's bindings and timings, which only the reload carried.
        for (const char *k : { "tier", "overall", "series", "phases", "resemblance", "pattern", "blended",
                               "interval", "perRegion", "perPhase", "kinematicSequence", "gatesVersion",
                               "gates", "segmentation", "pose2d", "club", "ball", "impact", "addressMarks",
                               "dtl", "bindings", "timings" })
            QVERIFY2(live.contains(QString::fromLatin1(k)), k);
        const QVariantMap dtl = live.value(QStringLiteral("dtl")).toMap();
        for (const char *k : { "pose2d", "club", "ball", "addressMarks", "addressMarksPhases", "ruler" })
            QVERIFY2(dtl.contains(QString::fromLatin1(k)), k);

        // Window time, on every block the overlays scrub.
        QCOMPARE(phaseUs(live, Phase::Impact), qint64(11000));
        QCOMPARE(live.value(QStringLiteral("series")).toList().value(0).toMap()
                     .value(QStringLiteral("t_us")).toList().value(2).toLongLong(), qlonglong(11000));
        QCOMPARE(live.value(QStringLiteral("pose2d")).toMap().value(QStringLiteral("frames")).toList()
                     .value(0).toMap().value(QStringLiteral("t_us")).toLongLong(), qlonglong(1000));
        QCOMPARE(live.value(QStringLiteral("club")).toMap().value(QStringLiteral("samples")).toList()
                     .value(0).toMap().value(QStringLiteral("t_us")).toLongLong(), qlonglong(2000));
        QCOMPARE(live.value(QStringLiteral("ball")).toMap().value(QStringLiteral("launchTUs")).toLongLong(), qlonglong(11200));
        QCOMPARE(live.value(QStringLiteral("impact")).toMap().value(QStringLiteral("ballLeaveTUs")).toLongLong(), qlonglong(11100));
        QCOMPARE(live.value(QStringLiteral("kinematicSequence")).toMap().value(QStringLiteral("impactUs")).toLongLong(), qlonglong(11000));
        QCOMPARE(live.value(QStringLiteral("segmentation")).toMap().value(QStringLiteral("swingEndUs")).toLongLong(), qlonglong(14000));
        QCOMPARE(live.value(QStringLiteral("addressMarks")).toMap().value(QStringLiteral("t_us")).toLongLong(), qlonglong(800));
        QCOMPARE(dtl.value(QStringLiteral("club")).toMap().value(QStringLiteral("samples")).toList()
                     .value(0).toMap().value(QStringLiteral("t_us")).toLongLong(), qlonglong(2000));
        QCOMPARE(dtl.value(QStringLiteral("ball")).toMap().value(QStringLiteral("samples")).toList()
                     .value(1).toMap().value(QStringLiteral("t_us")).toLongLong(), qlonglong(11000));   // withdrawn at impact

        // The whole-shot records ride with the stages' (the failed IMU re-fusion check → shot.refusionParity)
        // EXACTLY ONCE on every path: the disk form already carries them, so a second append of the
        // manifest's records onto the live map (what the join did before the one projection) would
        // double them. The stages' own record (the fixture's one) is there once too.
        const auto gateCount = [](const QVariantMap &m, const char *gate) {
            int n = 0;
            for (const QVariant &g : m.value(QStringLiteral("gates")).toList())
                n += g.toMap().value(QStringLiteral("gate")).toString() == QLatin1String(gate) ? 1 : 0;
            return n;
        };
        for (const QVariantMap *m : { &live, &ps.analysisDetail, &replayCached, &replayParsed }) {
            QCOMPARE(gateCount(*m, "shot.refusionParity"), 1);
            QCOMPARE(gateCount(*m, "sequence.sightedBand"), 1);
            QCOMPARE(m->value(QStringLiteral("gates")).toList().size(), 2);
        }

        // The same swing already in window time projects to the same map (the rebase 1.5 wires).
        COMPARE_MAPS(live, liveAnalysisDetail(makeSwing(0), kT0, manifest), "ring clock vs window time");
    }

    // 2. An old document (absolute instants, no `timeBase`) reads to the same map as a new one.
    void legacyAbsoluteDocumentReadsTheSame()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dirNew = tmp.filePath(QStringLiteral("new/swing_0001"));
        const QString dirAbs = tmp.filePath(QStringLiteral("abs/swing_0001"));
        const QString dirOld = tmp.filePath(QStringLiteral("old/swing_0001"));
        for (const QString &d : { dirNew, dirAbs, dirOld })
            QVERIFY(QDir().mkpath(d));
        const QJsonObject manifest = makeManifest(kT0);
        QString err;

        const SwingAnalysis a = makeSwing(kT0);
        QVERIFY2(createSwing(dirNew, manifest, a, &err), qPrintable(err));

        // The old shape: every instant on the ring clock (the analysis declared Window, so the writer
        // keeps them as they are) and no `timeBase`, as every document before step 1.3a — written as a
        // JSON-era swing.json, the format most of them are in.
        SwingAnalysis abs = makeSwing(kT0);
        abs.timeBase = TimeBase::Window;
        QVERIFY2(createSwing(dirAbs, manifest, abs, &err), qPrintable(err));
        QJsonObject root = SwingDocWriter::takeJustWritten(dirAbs);
        QVERIFY(!root.isEmpty());
        QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
        QCOMPARE(an.value(QStringLiteral("phases")).toArray().last().toObject()
                     .value(QStringLiteral("t_us")).toDouble(), double(kT0 + 11000));   // absolute, as of old
        an.remove(QStringLiteral("timeBase"));
        root.insert(QStringLiteral("analysis"), an);
        root.remove(QStringLiteral("summary"));
        {
            QFile j(dirOld + QStringLiteral("/swing.json"));
            QVERIFY(j.open(QIODevice::WriteOnly));
            QVERIFY(j.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
        }
        QCOMPARE(analysisReadOriginUs(SwingStore::load(dirOld)), qint64(kT0));
        QVERIFY(touchStreamFiles(dirNew, manifest));
        QVERIFY(touchStreamFiles(dirOld, manifest));

        const QVariantMap fresh = SwingDocReader::readSwingJson(dirNew).analysisDetail;
        COMPARE_MAPS(fresh, SwingDocReader::readSwingJson(dirOld).analysisDetail, "reload: stamped vs legacy");
        COMPARE_MAPS(fresh, replayMap(dirOld), "replay: stamped vs legacy");
        // Reading never rewrites the document.
        QCOMPARE(SwingStore::load(dirOld).value(QStringLiteral("analysis")).toObject()
                     .contains(QStringLiteral("timeBase")), false);
    }

    // 3. One real pinned-pose swing.
    void realSwingAgrees()
    {
        const QString src = corpusSwing();
        if (!QFile::exists(SwingStore::ppswPath(src)))
            QSKIP("the corpus swing is not reachable (needs /mnt/swingdata, or PINPOINT_PROJECTION_SWING)");
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.filePath(QStringLiteral("swing_0002"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(QFile::copy(SwingStore::ppswPath(src), SwingStore::ppswPath(dir)));   // read-only on the share

        QString err;
        const QJsonObject root = SwingStore::load(dir, &err);
        QVERIFY2(!root.isEmpty(), qPrintable(err));
        QVERIFY(touchStreamFiles(dir, root));
        QVERIFY(root.value(QStringLiteral("analysis")).toObject().contains(QStringLiteral("pose2d")));

        const QVariantMap reload = SwingDocReader::readSwingJson(dir).analysisDetail;
        const QVariantMap replay = replayMap(dir);
        QVERIFY(!replay.isEmpty());
        COMPARE_MAPS(reload, replay, "real swing: reload vs replay");
        qInfo("real swing %s: origin %lld, %d keys: %s", qPrintable(src), qlonglong(analysisReadOriginUs(root)),
              int(reload.size()), qPrintable(reload.keys().join(QLatin1Char(' '))));

        // The live path on the swing's typed form. The blocks with no typed reader are compared above only.
        const QStringList writerOnly{ QStringLiteral("dtl"), QStringLiteral("addressMarks"),
                                      QStringLiteral("bindings"), QStringLiteral("timings") };
        //
        // ONE STAND-IN STEP IS NOT EXACT, and it is not the live path's: analysisFromDocument takes the
        // club block's normalised grip/head back to pixels (× a 1920 × 1080 frame) and the writer
        // normalises them again, which moves the last bit of a few coordinates. Live, the analysis is
        // in pixels and normalised once, by the same writer the document got (the synthetic swing
        // above, with power-of-two frames, is held exactly). So this one comparison allows 1e-12
        // relative on numbers, and says how many it needed; every key, every list length and every
        // other number must match.
        const SwingAnalysis typed = analysisFromDocument(root);
        const QVariantMap liveTyped = liveAnalysisDetail(typed, 0, root);
        const QStringList d = mapDiffs(liveTyped, reload, writerOnly, 1e-12);
        QVERIFY2(d.isEmpty(), qPrintable(QStringLiteral("real swing: live (typed) vs reload: ") + d.join(QStringLiteral("; "))));
        const QStringList exact = mapDiffs(liveTyped, reload, writerOnly);
        qInfo("real swing: live (typed) vs reload — %s", exact.isEmpty() ? "exact"
              : qPrintable(QStringLiteral("equal within 1e-12; last-bit differences, first ones: ") + exact.first()));

        // The same document stamped `timeBase: "window"` (its instants are already window time) reads the same.
        QJsonObject stamped = root;
        QJsonObject an = stamped.value(QStringLiteral("analysis")).toObject();
        an.insert(QStringLiteral("timeBase"), QStringLiteral("window"));
        stamped.insert(QStringLiteral("analysis"), an);
        COMPARE_MAPS(reload, analysisDetailForDocument(stamped), "real swing: unstamped vs stamped");
    }
};

QTEST_GUILESS_MAIN(AnalysisDetailProjectionTest)
#include "analysis_detail_projection_test.moc"
