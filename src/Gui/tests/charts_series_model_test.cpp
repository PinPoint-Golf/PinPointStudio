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

// SessionSeriesModel against a session written by SwingStore's own writer, and (when the share is
// mounted) against the 4 July scratch session the Charts mocks were drawn from.
//
// The synthetic session is four swings:
//   swing_0001  times ABSOLUTE (t0-based), as a live capture writes them
//   swing_0002  times WINDOW-RELATIVE, as a re-analysis writes them — must read identically
//   swing_0003  no analysis block (skipped, counted)
//   swing_0004  absolute again
// and a diagnostics.json ledger with rows for shot 1.

#include "session_series_model.h"

#include "../../Coaching/diagnostic_ledger.h"
#include "../../Metrics/metric_reducer.h"
#include "../../Export/swing_store.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <cmath>

#ifdef Q_OS_MACOS
#include <malloc/malloc.h>
#endif

using namespace pinpoint::charts;

namespace {

constexpr qint64 kT0 = 97'982'181'793;   // a real live t0 (4 July swing 1), so t ≥ t0 is the real test

// One swing document. `absolute` adds t0 to every analysis time, the way a live capture writes.
QJsonObject makeDoc(int index, bool absolute, bool withAnalysis, double scale = 1.0)
{
    const qint64 off = absolute ? kT0 : 0;
    const auto t = [off](qint64 rel) { return QJsonValue(double(rel + off)); };
    QJsonObject root;
    root[QStringLiteral("clock")]  = QJsonObject{ { QStringLiteral("t0_us"), double(kT0) },
                                                  { QStringLiteral("unit"), QStringLiteral("us") } };
    root[QStringLiteral("swing")]  = QJsonObject{ { QStringLiteral("index"), index } };
    root[QStringLiteral("review")] = QJsonObject{ { QStringLiteral("club"), QStringLiteral("7 IRON") } };
    if (!withAnalysis) return root;

    QJsonArray metrics;
    // A curve with a bridged sample, a null value, a series σ and a phase sample with σ.
    metrics.append(QJsonObject{
        { QStringLiteral("key"),   QStringLiteral("clubheadSpeed") },
        { QStringLiteral("label"), QStringLiteral("Clubhead speed") },
        { QStringLiteral("unit"),  QStringLiteral("mph") },
        { QStringLiteral("t_us"),  QJsonArray{ t(100000), t(110000), t(120000) } },
        { QStringLiteral("value"), QJsonArray{ 10.0 * scale, QJsonValue(), 30.0 * scale } },
        { QStringLiteral("valid"), QJsonArray{ 1, 0, 1 } },
        { QStringLiteral("sigma"), 1.5 },
        { QStringLiteral("sigmaKind"), 2 },
        { QStringLiteral("phaseSamples"), QJsonArray{ QJsonObject{
              { QStringLiteral("phase"), 5 }, { QStringLiteral("t_us"), t(120000) },
              { QStringLiteral("value"), 30.0 * scale }, { QStringLiteral("band"), QStringLiteral("good") },
              { QStringLiteral("sigma"), 0.7 }, { QStringLiteral("sigmaKind"), 2 } } } } });
    // A curve whose `valid` is all ones (stored empty), no σ anywhere.
    metrics.append(QJsonObject{
        { QStringLiteral("key"),   QStringLiteral("pelvisRotation") },
        { QStringLiteral("label"), QStringLiteral("Pelvis turn") },
        { QStringLiteral("unit"),  QStringLiteral("°") },
        { QStringLiteral("t_us"),  QJsonArray{ t(100000), t(110000) } },
        { QStringLiteral("value"), QJsonArray{ 1.0, 2.0 } },
        { QStringLiteral("valid"), QJsonArray{ 1, 1 } },
        { QStringLiteral("phaseSamples"), QJsonArray{ QJsonObject{
              { QStringLiteral("phase"), 2 }, { QStringLiteral("t_us"), t(100000) },
              { QStringLiteral("value"), 1.0 }, { QStringLiteral("band"), QString() } } } } });
    // An address-only scalar: empty curve, one phase sample.
    metrics.append(QJsonObject{
        { QStringLiteral("key"),   QStringLiteral("stanceWidth") },
        { QStringLiteral("label"), QStringLiteral("Stance width") },
        { QStringLiteral("unit"),  QStringLiteral("×frame") },
        { QStringLiteral("t_us"),  QJsonArray{} },
        { QStringLiteral("value"), QJsonArray{} },
        { QStringLiteral("phaseSamples"), QJsonArray{ QJsonObject{
              { QStringLiteral("phase"), 0 }, { QStringLiteral("t_us"), t(50000) },
              { QStringLiteral("value"), 0.4 }, { QStringLiteral("band"), QString() } } } } });

    QJsonArray phases{
        QJsonObject{ { QStringLiteral("phase"), 0 }, { QStringLiteral("t_us"), t(50000) },  { QStringLiteral("conf"), 1.0 } },
        QJsonObject{ { QStringLiteral("phase"), 2 }, { QStringLiteral("t_us"), t(100000) }, { QStringLiteral("conf"), 0.9 } },
        QJsonObject{ { QStringLiteral("phase"), 5 }, { QStringLiteral("t_us"), t(120000) }, { QStringLiteral("conf"), 1.0 } } };

    QJsonObject ks{
        { QStringLiteral("impactUs"), t(120000) },
        { QStringLiteral("nodes"), QJsonArray{ QJsonObject{
              { QStringLiteral("segment"), QStringLiteral("pelvis") }, { QStringLiteral("placed"), true },
              { QStringLiteral("tPeakUs"), t(125000) }, { QStringLiteral("beforeImpactMs"), -5.0 },
              { QStringLiteral("peakDps"), 700.0 } } } },
        { QStringLiteral("order"), QJsonArray{ QStringLiteral("pelvis") } },
        { QStringLiteral("verdict"), QStringLiteral("other") } };

    // A pose block, so the document is shaped like a real one; the model must not carry it.
    QJsonArray frames;
    for (int i = 0; i < 50; ++i)
        frames.append(QJsonObject{ { QStringLiteral("t_us"), t(i * 4000) },
                                   { QStringLiteral("kp"), QJsonArray{ 0.1, 0.2, 0.3, 0.4 } } });

    root[QStringLiteral("analysis")] = QJsonObject{
        { QStringLiteral("metrics"), metrics },
        { QStringLiteral("phases"), phases },
        { QStringLiteral("kinematicSequence"), ks },
        { QStringLiteral("pose2d"), QJsonObject{ { QStringLiteral("frames"), frames } } } };
    return root;
}

bool writeSwing(const QString &sessionDir, int index, bool absolute, bool withAnalysis, double scale = 1.0)
{
    const QString dir = sessionDir + QStringLiteral("/swing_%1").arg(index, 4, 10, QLatin1Char('0'));
    QDir().mkpath(dir);
    QString err;
    const bool ok = pinpoint::SwingStore::save(dir, makeDoc(index, absolute, withAnalysis, scale), &err);
    if (!ok) qWarning() << err;
    return ok;
}

// Move a file's mtime on by `secs`, so a rewrite within the same second is still a new stamp.
void bumpMtime(const QString &path, int secs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return;
    f.setFileTime(QFileInfo(path).lastModified().addSecs(secs), QFileDevice::FileModificationTime);
}

pinpoint::analysis::ConditionRow row(const QString &id, pinpoint::analysis::ShotState st,
                                     double value, double z, double lo, double hi, int dir,
                                     const QString &drivingMeasure = QString())
{
    pinpoint::analysis::ConditionRow r;
    r.conditionId = id;
    r.drivingMeasureId = drivingMeasure;
    r.state       = st;
    r.value       = value;
    r.z           = z;
    r.corridorLo  = lo;
    r.corridorHi  = hi;
    r.direction   = dir;
    r.notAssessableReason = st == pinpoint::analysis::ShotState::NotAssessable ? QStringLiteral("x") : QString();
    return r;
}

void writeLedger(const QString &sessionDir, const std::vector<pinpoint::analysis::ShotRecord> &shots)
{
    QJsonObject root;
    root[QStringLiteral("ledger")] = pinpoint::analysis::toJson(shots);
    QFile f(sessionDir + QStringLiteral("/diagnostics.json"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

std::vector<pinpoint::analysis::ShotRecord> ledgerShot1()
{
    using pinpoint::analysis::ShotState;
    pinpoint::analysis::ShotRecord s;
    s.shotId = 1;
    s.club   = QStringLiteral("7 IRON");
    s.rows.push_back(row(QStringLiteral("s_posture"), ShotState::Fired, 12.5, 2.1, 0.0, 10.0, 1));
    s.rows.push_back(row(QStringLiteral("c_posture"), ShotState::Clean, 3.0, 0.2, 0.0, 10.0, 0));
    s.rows.push_back(row(QStringLiteral("posture_too_upright"), ShotState::NotAssessable, 0, 0, 0, 0, 0));
    // The shipped links (build/charts-build/condition_links.md): one reading each for these two…
    s.rows.push_back(row(QStringLiteral("over_the_top"), ShotState::Fired, 4, 1.8, -2, 2, 1,
                         QStringLiteral("m_handPathLoop")));
    s.rows.push_back(row(QStringLiteral("lie_steep_at_impact"), ShotState::Fired, 6, 1.4, -3, 3, 1));
    // …and early extension reads two (pelvis thrust first, spine-angle loss second): the row names
    // the SECOND, so the link must follow the ledger, not the pack's first reading.
    s.rows.push_back(row(QStringLiteral("early_extension"), ShotState::Fired, 9, 2.5, 0, 5, 1,
                         QStringLiteral("m_spineBendLossDown")));
    return { s };
}

qint64 heapInUse()
{
#ifdef Q_OS_MACOS
    malloc_statistics_t st{};
    malloc_zone_statistics(nullptr, &st);
    return qint64(st.size_in_use);
#else
    return -1;
#endif
}

bool sameSeries(const ShotSeries &a, const ShotSeries &b)
{
    if (a.impactUs != b.impactUs || a.metricOrder != b.metricOrder || a.phases.size() != b.phases.size()
        || a.kinematicSequence != b.kinematicSequence || a.club != b.club)
        return false;
    for (const QString &k : a.metricOrder) {
        const MetricSeries &x = *a.metric(k), &y = *b.metric(k);
        if (x.tUs != y.tUs || x.valid != y.valid || x.value.size() != y.value.size()) return false;
        for (size_t i = 0; i < x.value.size(); ++i)
            if (!(x.value[i] == y.value[i] || (std::isnan(x.value[i]) && std::isnan(y.value[i])))) return false;
        if (x.phaseSamples.size() != y.phaseSamples.size()) return false;
    }
    return true;
}

} // namespace

class ChartsSeriesModelTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString       m_session;     // <tmp>/Athlete/2026-10-14_Test_01
    QString       m_other;       // a second, empty session beside it

    // A fresh copy of the four-swing session for one test.
    void stage()
    {
        QDir(m_session).removeRecursively();
        QDir().mkpath(m_session);
        QVERIFY(writeSwing(m_session, 1, true,  true));
        QVERIFY(writeSwing(m_session, 2, false, true));
        QVERIFY(writeSwing(m_session, 3, true,  false));
        QVERIFY(writeSwing(m_session, 4, true,  true));
        writeLedger(m_session, ledgerShot1());
    }

    // A real session: load with both loaders, check against the fixture when it is there, and
    // report the cost. Returns false (after QSKIP) when the session is not reachable.
    void measure(const QString &dir, const QString &label, bool compareFixture);

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);   // the user's own packs stay out of it
        QVERIFY(m_tmp.isValid());
        m_session = m_tmp.path() + QStringLiteral("/Athlete/2026-10-14_Test_01");
        m_other   = m_tmp.path() + QStringLiteral("/Athlete/2026-10-13_Test_01");
        QDir().mkpath(m_other);
    }

    // Each case rewrites the session, so none may inherit another's shots from the process-wide
    // cache (decision 5 of the model's header).
    void init() { SessionSeriesModel::clearSharedCache(); }

    void extractsTheAnalysisOnTheReplayClock()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        const auto snap = m.snapshot();
        QCOMPARE(snap->count(), 3);
        QCOMPARE(snap->sessionDir, QDir::cleanPath(m_session));

        const ShotSeries &s = *snap->shots[0];
        QCOMPARE(s.ordinal, 1);
        QCOMPARE(s.shotId, 1);
        QCOMPARE(s.club, QStringLiteral("7 IRON"));
        QCOMPARE(s.metricOrder, (QStringList{ QStringLiteral("clubheadSpeed"), QStringLiteral("pelvisRotation"),
                                              QStringLiteral("stanceWidth") }));

        // The curve: times window-relative, the null a NaN (never 0), the bridged sample kept.
        const MetricSeries *c = s.metric(QStringLiteral("clubheadSpeed"));
        QVERIFY(c);
        QCOMPARE(c->label, QStringLiteral("Clubhead speed"));
        QCOMPARE(c->unit, QStringLiteral("mph"));
        QCOMPARE(c->tUs, (std::vector<qint64>{ 100000, 110000, 120000 }));
        QCOMPARE(c->value.size(), size_t(3));
        QCOMPARE(c->value[0], 10.0);
        QVERIFY(std::isnan(c->value[1]));
        QCOMPARE(c->valid, (std::vector<uint8_t>{ 1, 0, 1 }));
        QVERIFY(c->isValid(0) && !c->isValid(1) && c->isValid(2));
        QCOMPARE(c->sigma, 1.5);
        QCOMPARE(c->sigmaKind, 2);
        QVERIFY(c->hasCurve());
        QCOMPARE(c->phaseSamples.size(), size_t(1));
        QCOMPARE(c->phaseSamples[0].phase, 5);
        QCOMPARE(c->phaseSamples[0].tUs, qint64(120000));
        QCOMPARE(c->phaseSamples[0].value, 30.0);
        QCOMPARE(c->phaseSamples[0].sigma, 0.7);
        QCOMPARE(c->phaseSamples[0].band, QStringLiteral("good"));

        // All-valid is stored empty (= every sample valid); σ absent is NaN / −1, not 0.
        const MetricSeries *p = s.metric(QStringLiteral("pelvisRotation"));
        QVERIFY(p);
        QVERIFY(p->valid.empty());
        QVERIFY(p->isValid(0) && p->isValid(1));
        QVERIFY(std::isnan(p->sigma));
        QCOMPARE(p->sigmaKind, -1);
        QVERIFY(std::isnan(p->phaseSamples[0].sigma));

        // An instant-only reading: no curve, one sample at Address.
        const MetricSeries *w = s.metric(QStringLiteral("stanceWidth"));
        QVERIFY(w);
        QVERIFY(!w->hasCurve());
        QCOMPARE(w->phaseSamples.size(), size_t(1));
        QCOMPARE(w->phaseSamples[0].tUs, qint64(50000));

        // Phases and impact on the same clock; the sequence re-timed by the reload's function.
        QCOMPARE(s.impactUs, qint64(120000));
        QCOMPARE(s.phases.size(), size_t(3));
        QCOMPARE(s.phases[1].phase, 2);
        QCOMPARE(s.phases[1].tUs, qint64(100000));
        QCOMPARE(s.phases[1].conf, 0.9);
        QCOMPARE(s.kinematicSequence.value(QStringLiteral("impactUs")).toLongLong(), qlonglong(120000));
        const QVariantMap node = s.kinematicSequence.value(QStringLiteral("nodes")).toList().value(0).toMap();
        QCOMPARE(node.value(QStringLiteral("tPeakUs")).toLongLong(), qlonglong(125000));
        QCOMPARE(s.kinematicSequence.value(QStringLiteral("verdict")).toString(), QStringLiteral("other"));

        // swing_0002 was written window-relative: it must read identically (relUs passes it through).
        const ShotSeries &r = *snap->shots[1];
        QCOMPARE(r.shotId, 2);
        QCOMPARE(r.impactUs, qint64(120000));
        QCOMPARE(r.metric(QStringLiteral("clubheadSpeed"))->tUs, c->tUs);
        QCOMPARE(r.kinematicSequence.value(QStringLiteral("impactUs")).toLongLong(), qlonglong(120000));

        // relUs itself, both branches.
        QCOMPARE(SessionSeriesModel::relUs(QJsonValue(double(kT0 + 5)), kT0), qint64(5));
        QCOMPARE(SessionSeriesModel::relUs(QJsonValue(5.0), kT0), qint64(5));
    }

    void skipsAShotWithNoAnalysisAndCountsIt()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        QCOMPARE(m.shotCount(), 3);
        QCOMPARE(m.skippedCount(), 1);
        const QVariantMap sk = m.skipped().value(0).toMap();
        QCOMPARE(sk.value(QStringLiteral("shotId")).toInt(), 3);
        QVERIFY(sk.value(QStringLiteral("reason")).toString().startsWith(QStringLiteral("not analysed")));
        // Ordinals are dense over the shots that are in: 1, 2, 3 for folders 1, 2, 4.
        const auto snap = m.snapshot();
        QCOMPARE(snap->shots[2]->ordinal, 3);
        QCOMPARE(snap->shots[2]->shotId, 4);
    }

    void joinsTheLedgerAndNamesConditionsFromTheCatalogue()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        const auto snap = m.snapshot();
        const ShotDiagnostics &d = snap->shots[0]->diagnostics;
        QCOMPARE(d.club, QStringLiteral("7 IRON"));
        QCOMPARE(d.fired, (QStringList{ QStringLiteral("early_extension"), QStringLiteral("lie_steep_at_impact"),
                                        QStringLiteral("over_the_top"), QStringLiteral("s_posture") }));
        QCOMPARE(d.clean, QStringList{ QStringLiteral("c_posture") });
        QCOMPARE(d.values.size(), 5);                      // not-assessable rows carry no value
        const ShotDiagnostics::Value v = d.values.value(QStringLiteral("s_posture"));
        QCOMPARE(v.value, 12.5);
        QCOMPARE(v.z, 2.1);
        QCOMPARE(v.hi, 10.0);
        QCOMPARE(v.dir, 1);
        QVERIFY(snap->shots[1]->diagnostics.fired.isEmpty());   // no row for shot 2

        QCOMPARE(snap->conditions.size(), 4);
        const ConditionInfo ci = snap->conditions.value(QStringLiteral("s_posture"));
        QCOMPARE(ci.name, QStringLiteral("S-posture"));          // the shipped label, not the id
        QCOMPARE(ci.group, QStringLiteral("setup"));

        // The condition → reading link (condition_reading.h), as shipped.
        const int kAddress = 0, kTop = 2, kImpact = 5;           // P1, P4, P7 (Phase enum ids)
        const auto link = [&](const char *id) { return snap->conditions.value(QLatin1String(id)); };
        QCOMPARE(ci.metricKey, QStringLiteral("lumbarExtension"));
        QCOMPARE(ci.phase, kAddress);
        QCOMPARE(link("over_the_top").metricKey, QStringLiteral("handPathLoop"));
        QCOMPARE(link("over_the_top").phase, kTop);
        QCOMPARE(link("lie_steep_at_impact").name, QStringLiteral("Returns steep"));
        QCOMPARE(link("lie_steep_at_impact").metricKey, QStringLiteral("shaftLie"));
        QCOMPARE(link("lie_steep_at_impact").phase, kImpact);
        // Two readings: the one the ledger's rows name, not the pack's first (pelvisThrust).
        QCOMPARE(link("early_extension").metricKey, QStringLiteral("spineForwardBend"));
        QCOMPARE(link("early_extension").phase, kImpact);

        // The reducer travels with the link (ReducerKind values), so the panel can say how the
        // number was read: AT P4, the CHANGE to P7, the PEAK before P7.
        const int kAt = int(pinpoint::analysis::ReducerKind::At);
        const int kDelta = int(pinpoint::analysis::ReducerKind::Delta);
        const int kExtremum = int(pinpoint::analysis::ReducerKind::Extremum);
        QCOMPARE(kAt, 0);
        QCOMPARE(kDelta, 1);
        QCOMPARE(kExtremum, 3);                                  // the values the types header names
        QCOMPARE(link("over_the_top").reducer, kAt);             // handPathLoop@P4, at
        QCOMPARE(link("lie_steep_at_impact").reducer, kDelta);   // shaftLie@P7, delta
        QCOMPARE(link("early_extension").reducer, kExtremum);    // spineForwardBend@P7, extremum
        QCOMPARE(link("s_posture").reducer, kAt);

        // Per shot: which reading drove each fired condition, and what the ledger named.
        const ShotDiagnostics::Reading ee = d.drivenBy.value(QStringLiteral("early_extension"));
        QCOMPARE(ee.measureId, QStringLiteral("m_spineBendLossDown"));
        QCOMPARE(ee.metricKey, QStringLiteral("spineForwardBend"));
        QCOMPARE(ee.phase, kImpact);
        QCOMPARE(ee.reducer, kExtremum);
        const ShotDiagnostics::Reading ls = d.drivenBy.value(QStringLiteral("lie_steep_at_impact"));
        QVERIFY(ls.measureId.isEmpty());                         // the row named none…
        QCOMPARE(ls.metricKey, QStringLiteral("shaftLie"));      // …so the first reading with a key
        QCOMPARE(ls.reducer, kDelta);
        QCOMPARE(d.drivenBy.size(), 4);                          // fired only: not c_posture
        QVERIFY(!d.drivenBy.contains(QStringLiteral("c_posture")));
    }

    // K4 (trunk_imu_sequence_grf_design.md §10.6): the ledger's per-signal verdicts (schema 5)
    // reach the panel as the sequence checks, read off the rows, never re-derived.
    void carriesTheSequenceChecks()
    {
        stage();
        using pinpoint::analysis::ShotState;
        std::vector<pinpoint::analysis::ShotRecord> shots = ledgerShot1();
        auto sig = [](const char *id, ShotState st, float p, bool q, const char *why = "") {
            pinpoint::analysis::SignalRow g;
            g.signalId = QLatin1String(id); g.state = st; g.pFire = p; g.quantified = q;
            g.reason = QString::fromUtf8(why);
            return g;
        };
        shots[0].signalRows = { sig("sig_sequenceOrder", ShotState::Clean, 0.1f, true),
                                sig("sig_armBeforeThorax", ShotState::Fired, 0.9f, true),
                                sig("sig_thoraxGainLow", ShotState::NotAssessable, -1.f, false,
                                    "chest peak not placed from these cameras") };
        writeLedger(m_session, shots);
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        const auto snap = m.snapshot();
        const QVariantMap c = snap->shots[0]->diagnostics.sequenceChecks;
        QCOMPARE(c.value(QStringLiteral("line")).toString(), QStringLiteral("Sequence checks: 1 of 2"));
        QCOMPARE(c.value(QStringLiteral("checks")).toList().size(), 10);
        QCOMPARE(c.value(QStringLiteral("checks")).toList().value(5).toMap().value(QStringLiteral("reason")).toString(),
                 QStringLiteral("chest peak not placed from these cameras"));   // F4's —, and why
        QVERIFY(snap->shots[1]->diagnostics.sequenceChecks.isEmpty());   // no row: nothing drawn
    }

    void unchangedDocumentsAreNotReadAgain()
    {
        stage();
        std::atomic<int> reads{ 0 };
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.setDocLoader([&reads](const QString &dir, QString *err) {
            ++reads;
            return pinpoint::SwingStore::loadAnalysis(dir, err);
        });
        m.activateSession(m_session);
        QCOMPARE(reads.load(), 4);                 // the no-analysis document is read once too
        const auto first = m.snapshot();

        m.activateSession(m_session);              // re-activating the same session
        QCOMPARE(reads.load(), 4);
        QCOMPARE(m.snapshot()->revision, first->revision);
        QCOMPARE(m.snapshot()->shots, first->shots);

        m.activateSession(m_other);                // away to another session and back
        QCOMPARE(m.shotCount(), 0);
        m.activateSession(m_session);
        QCOMPARE(reads.load(), 4);
        QCOMPARE(m.snapshot()->shots, first->shots);

        m.ingestShot(1, m_session + QStringLiteral("/swing_0001"));   // re-ingesting an unchanged shot
        QCOMPARE(reads.load(), 4);
        QCOMPARE(m.snapshot()->shots, first->shots);
        QCOMPARE(m.loadStats().docReads, 4);
        QVERIFY(m.loadStats().cacheHits >= 9);
    }

    // The panel is rebuilt on every Capture↔Replay flip, and the model with it: the new instance
    // must find the session in the cache, not on disk.
    void aRebuiltPanelReadsNothing()
    {
        stage();
        std::shared_ptr<const SessionSnapshot> first;
        {
            SessionSeriesModel a;
            a.setSynchronous(true);
            a.activateSession(m_session);
            first = a.snapshot();
        }
        std::atomic<int> reads{ 0 };
        SessionSeriesModel b;
        b.setSynchronous(true);
        b.setDocLoader([&reads](const QString &dir, QString *err) {
            ++reads;
            return pinpoint::SwingStore::loadAnalysis(dir, err);
        });
        b.activateSession(m_session);
        QCOMPARE(reads.load(), 0);
        QCOMPARE(b.shotCount(), 3);
        QCOMPARE(b.skippedCount(), 1);
        QCOMPARE(b.snapshot()->shots, first->shots);        // the very same immutable shots
    }

    // Three panels on three sessions: the third activation evicts the first session from the
    // process-wide cache (it keeps two). The first instance must not lose its shots for that — its
    // next ingest and refresh publish ALL of them (the "Shot 1 of 1" defect).
    void anotherInstanceCannotEvictThisOnesShots()
    {
        stage();
        const QString b = m_tmp.path() + QStringLiteral("/Athlete/2026-10-12_Test_01");
        const QString c = m_tmp.path() + QStringLiteral("/Athlete/2026-10-11_Test_01");
        for (const QString &d : { b, c }) {
            QDir(d).removeRecursively();
            QVERIFY(writeSwing(d, 1, true, true));
            QVERIFY(writeSwing(d, 2, false, true));
        }
        SessionSeriesModel ma, mb, mc;
        for (SessionSeriesModel *m : { &ma, &mb, &mc }) m->setSynchronous(true);
        ma.activateSession(m_session);
        const auto before = ma.snapshot();
        QCOMPARE(before->count(), 3);
        mb.activateSession(b);
        mc.activateSession(c);                                      // m_session leaves the shared cache
        QCOMPARE(mb.shotCount(), 2);
        QCOMPARE(mc.shotCount(), 2);

        ma.refreshDiagnostics();
        QCOMPARE(ma.snapshot()->count(), 3);

        QVERIFY(writeSwing(m_session, 5, true, true));
        ma.ingestShot(5, m_session + QStringLiteral("/swing_0005"));
        const auto after = ma.snapshot();
        QCOMPARE(after->count(), 4);                                // all of its shots, plus the new one
        QCOMPARE(ma.skippedCount(), 1);                             // still only the unanalysed swing_0003
        for (int i = 0; i < 3; ++i) QCOMPARE(after->shots[size_t(i)], before->shots[size_t(i)]);
        QCOMPARE(after->shots[3]->shotId, 5);
        QCOMPARE(mb.shotCount(), 2);                                // and the others are untouched
        QCOMPARE(mc.shotCount(), 2);
        QDir(b).removeRecursively();
        QDir(c).removeRecursively();
    }

    void reanalysisReplacesTheShotAndBumpsTheRevision()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        const auto before = m.snapshot();

        QVERIFY(writeSwing(m_session, 2, false, true, 2.0));       // re-analysed: values doubled
        bumpMtime(pinpoint::SwingStore::ppswPath(m_session + QStringLiteral("/swing_0002")), 5);
        QSignalSpy changed(&m, &SessionSeriesModel::snapshotChanged);
        m.regradeShot(m_session + QStringLiteral("/swing_0002"));
        QCOMPARE(changed.count(), 1);

        const auto after = m.snapshot();
        QVERIFY(after->revision > before->revision);
        QCOMPARE(after->count(), 3);
        QCOMPARE(after->shots[0], before->shots[0]);                // untouched shots keep their pointer
        QCOMPARE(after->shots[2], before->shots[2]);
        QVERIFY(after->shots[1] != before->shots[1]);               // the re-analysed one is new
        QCOMPARE(after->shots[1]->metric(QStringLiteral("clubheadSpeed"))->value[0], 20.0);
        QCOMPARE(before->shots[1]->metric(QStringLiteral("clubheadSpeed"))->value[0], 10.0);   // immutable
        QCOMPARE(after->shots[1]->ordinal, 2);
    }

    void aLiveShotAppendsAndStrikeOrderHolds()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        const auto before = m.snapshot();

        QVERIFY(writeSwing(m_session, 5, true, true));
        QSignalSpy changed(&m, &SessionSeriesModel::snapshotChanged);
        m.ingestShot(99, m_session + QStringLiteral("/swing_0005"));   // the processor's id: the folder wins
        QCOMPARE(changed.count(), 1);
        auto snap = m.snapshot();
        QCOMPARE(snap->count(), 4);
        QCOMPARE(snap->shots[3]->ordinal, 4);
        QCOMPARE(snap->shots[3]->shotId, 5);
        for (int i = 0; i < 3; ++i) QCOMPARE(snap->shots[i], before->shots[i]);

        // A shot that lands BETWEEN two others (swing_0003 analysed late): it takes its place in
        // strike order and the shots after it are renumbered, as new objects.
        QVERIFY(writeSwing(m_session, 3, true, true));
        bumpMtime(pinpoint::SwingStore::ppswPath(m_session + QStringLiteral("/swing_0003")), 5);
        m.regradeShot(m_session + QStringLiteral("/swing_0003"));
        snap = m.snapshot();
        QCOMPARE(snap->count(), 5);
        QCOMPARE(m.skippedCount(), 0);
        QList<int> ids;
        for (const auto &s : snap->shots) ids << s->shotId;
        QCOMPARE(ids, (QList<int>{ 1, 2, 3, 4, 5 }));
        for (int i = 0; i < 5; ++i) QCOMPARE(snap->shots[i]->ordinal, i + 1);
        QCOMPARE(snap->shots[0], before->shots[0]);
        QCOMPARE(snap->shots[1], before->shots[1]);
        QVERIFY(snap->shots[3] != before->shots[2]);               // shot 4: ordinal 3 → 4, a new object
        QCOMPARE(before->shots[2]->ordinal, 3);                     // ...and the old one is unchanged
    }

    void ignoresShotsOutsideTheSessionAndAfterClose()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        QVERIFY(writeSwing(m_other, 1, true, true));
        m.ingestShot(1, m_other + QStringLiteral("/swing_0001"));
        QCOMPARE(m.shotCount(), 3);

        m.closeSession();
        QVERIFY(m.closed());
        QVERIFY(writeSwing(m_session, 5, true, true));
        m.ingestShot(5, m_session + QStringLiteral("/swing_0005"));
        QCOMPARE(m.shotCount(), 3);                                 // refused after close
        m.regradeShot(m_session + QStringLiteral("/swing_0005"));   // the same shot measured: accepted
        QCOMPARE(m.shotCount(), 4);
        QDir(m_other + QStringLiteral("/swing_0001")).removeRecursively();
    }

    void refreshDiagnosticsRejoinsOnlyWhatMoved()
    {
        stage();
        SessionSeriesModel m;
        m.setSynchronous(true);
        m.activateSession(m_session);
        const auto before = m.snapshot();

        m.refreshDiagnostics();                                     // ledger unchanged: nothing moves
        QCOMPARE(m.snapshot(), before);

        auto shots = ledgerShot1();
        pinpoint::analysis::ShotRecord s2;
        s2.shotId = 2;
        s2.rows.push_back(row(QStringLiteral("alignment_open"), pinpoint::analysis::ShotState::Fired, 9, 1.5, -5, 5, 1));
        shots.push_back(s2);
        writeLedger(m_session, shots);
        bumpMtime(m_session + QStringLiteral("/diagnostics.json"), 5);
        m.refreshDiagnostics();
        const auto after = m.snapshot();
        QVERIFY(after->revision > before->revision);
        QCOMPARE(after->shots[0], before->shots[0]);
        QVERIFY(after->shots[1] != before->shots[1]);
        QCOMPARE(after->shots[1]->diagnostics.fired, QStringList{ QStringLiteral("alignment_open") });
        QCOMPARE(after->conditions.size(), 5);
        QCOMPARE(after->conditions.value(QStringLiteral("alignment_open")).metricKey, QStringLiteral("shoulderLineYaw"));
    }

    void deliversOffTheGuiThread()
    {
        stage();
        SessionSeriesModel m;                                       // asynchronous, as in the app
        QSignalSpy busy(&m, &SessionSeriesModel::busyChanged);
        QSignalSpy changed(&m, &SessionSeriesModel::snapshotChanged);
        m.activateSession(m_session);
        QVERIFY(m.busy());
        QCOMPARE(m.shotCount(), 0);                                 // the interim: a new session, empty
        QVERIFY(m.waitForIdle(30000));
        QVERIFY(!m.busy());
        QCOMPARE(m.shotCount(), 3);
        QCOMPARE(busy.count(), 2);
        QCOMPARE(changed.count(), 2);                               // the interim, then the session

        // A queued refresh runs after the caller returns, and coalesces.
        m.refreshDiagnostics();
        m.refreshDiagnostics();
        QVERIFY(m.waitForIdle(30000));
        QCOMPARE(m.shotCount(), 3);

        // A delivery for a superseded activation is dropped.
        m.activateSession(m_other);
        m.activateSession(m_session);
        QVERIFY(m.waitForIdle(30000));
        QCOMPARE(m.sessionDir(), QDir::cleanPath(m_session));
        QCOMPARE(m.shotCount(), 3);
    }

    // ── Real data (QSKIP without the share) ───────────────────────────────────────────────

    void realSession4July()
    {
        measure(QStringLiteral("/mnt/swingdata/scratch/typeset-harness/slots/try/session/"
                               "2026-07-04_Mark-Liversedge_Wrist_01"),
                QStringLiteral("4 July (scratch)"), true);
    }

    void realLargeSession()
    {
        const QString dir = qEnvironmentVariable("PP_CHARTS_LARGE_SESSION");
        if (dir.isEmpty()) QSKIP("PP_CHARTS_LARGE_SESSION not set");
        measure(dir, QFileInfo(dir).fileName(), false);
    }
};

void ChartsSeriesModelTest::measure(const QString &dir, const QString &label, bool compareFixture)
{
    if (!QFileInfo(dir).isDir()) QSKIP("session not reachable (is /mnt/swingdata mounted?)");

    QString report;
    const auto line = [&report](const QString &s) { report += s + QLatin1Char('\n'); };
    // Written however the comparisons below end, so a failing run still reports what it measured.
    struct Flush {
        QString &r;
        ~Flush()
        {
            const QString out = qEnvironmentVariable("PP_CHARTS_MEASURE_OUT");
            if (out.isEmpty() || r.isEmpty()) return;
            QFile f(out);
            if (f.open(QIODevice::Append | QIODevice::Text)) f.write(r.toUtf8());
        }
    } flush{ report };

    for (const bool partial : { false, true }) {
        SessionSeriesModel::clearSharedCache();         // each pass reads every document
        SessionSeriesModel m;
        m.setSynchronous(true);
        // The default reads SwingStore::loadAnalysis; the comparison pass reads whole documents.
        if (!partial) m.setDocLoader([](const QString &d, QString *e) { return pinpoint::SwingStore::load(d, e); });
        const qint64 heap0 = heapInUse();
        QElapsedTimer t;
        t.start();
        m.activateSession(dir);
        const qint64 ms = t.elapsed();
        const qint64 heap1 = heapInUse();
        const auto snap = m.snapshot();
        QVERIFY(snap->count() > 0);
        const SessionSeriesModel::LoadStats st = m.loadStats();
        qint64 approx = 0, samples = 0;
        int curves = 0;
        for (const auto &s : snap->shots) {
            approx += SessionSeriesModel::approxBytes(*s);
            for (const MetricSeries &ms2 : s->metrics) { samples += qint64(ms2.tUs.size()); curves += ms2.hasCurve(); }
        }
        const int n = snap->count();
        line(QStringLiteral("%1 [%2 load]: %3 shots, %4 skipped; %5 ms total = %6 ms/shot "
                            "(load %7 ms/shot, extract %8 ms/shot)")
                 .arg(label, partial ? QStringLiteral("SwingStore::loadAnalysis (default)") : QStringLiteral("SwingStore::load, whole document"))
                 .arg(n).arg(m.skippedCount()).arg(ms).arg(double(ms) / n, 0, 'f', 1)
                 .arg(double(st.loadNs) / 1e6 / st.docReads, 0, 'f', 1)
                 .arg(double(st.extractNs) / 1e6 / st.docReads, 0, 'f', 1));
        line(QStringLiteral("    resident: approx %1 KB/shot (estimator), heap delta %2 KB/shot (malloc in use); "
                            "%3 curves/shot, %4 samples/shot")
                 .arg(approx / n / 1024).arg(heap0 >= 0 ? (heap1 - heap0) / n / 1024 : -1)
                 .arg(curves / n).arg(samples / n));
        for (const QVariant &v : m.skipped())
            line(QStringLiteral("    skipped: %1 — %2").arg(v.toMap().value(QStringLiteral("swingDir")).toString(),
                                                         v.toMap().value(QStringLiteral("reason")).toString()));

        // The condition links this session's fired conditions resolve to, with how often each fired
        // and which driving measures its rows named (default pass only).
        if (partial) {
            const auto ptag = [](int ph) {
                switch (ph) {
                case 0: return QStringLiteral("P1"); case 12: return QStringLiteral("P2");
                case 8: return QStringLiteral("P3"); case 2: return QStringLiteral("P4");
                case 13: return QStringLiteral("P5"); case 9: return QStringLiteral("P6");
                case 5: return QStringLiteral("P7"); case 14: return QStringLiteral("P8");
                case 11: return QStringLiteral("P9"); case 7: return QStringLiteral("P10");
                default: return QStringLiteral("phase%1").arg(ph);
                }
            };
            QStringList ids = snap->conditions.keys();
            ids.sort();
            int linked = 0;
            for (const QString &cid : ids) {
                const ConditionInfo ci = snap->conditions.value(cid);
                int shots = 0;
                QMap<QString, int> driving;
                for (const auto &sh : snap->shots) {
                    if (!sh->diagnostics.fired.contains(cid)) continue;
                    ++shots;
                    const QString mid = sh->diagnostics.drivenBy.value(cid).measureId;
                    ++driving[mid.isEmpty() ? QStringLiteral("(none named)") : mid];
                }
                QStringList dv;
                for (auto it = driving.constBegin(); it != driving.constEnd(); ++it)
                    dv << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
                linked += ci.metricKey.isEmpty() ? 0 : 1;
                static const char *const kReducers[] = { "at", "delta", "rate", "extremum" };
                line(QStringLiteral("    link | %1 | %2 | %3@%4 | %5 | %6 shots | %7")
                         .arg(cid, ci.name, ci.metricKey.isEmpty() ? QStringLiteral("-") : ci.metricKey,
                              ci.phase < 0 ? QStringLiteral("-") : ptag(ci.phase),
                              ci.reducer >= 0 && ci.reducer < 4 ? QLatin1String(kReducers[ci.reducer])
                                                                : QLatin1String("-"))
                         .arg(shots).arg(dv.join(QStringLiteral(", "))));
            }
            line(QStringLiteral("    links: %1 of %2 fired conditions carry a key").arg(linked).arg(ids.size()));
            // The shipped pack's early extension on a real session: pelvis thrust, the PEAK before P7.
            if (snap->conditions.contains(QStringLiteral("early_extension"))) {
                const ConditionInfo ee = snap->conditions.value(QStringLiteral("early_extension"));
                QCOMPARE(ee.metricKey, QStringLiteral("pelvisThrust"));
                QCOMPARE(ee.phase, 5);
                QCOMPARE(ee.reducer, int(pinpoint::analysis::ReducerKind::Extremum));
            }
        }

        // The second pass must read the same shots as the first.
        static std::shared_ptr<const SessionSnapshot> fullSnap;
        if (!partial) {
            fullSnap = snap;
        } else {
            QCOMPARE(snap->count(), fullSnap->count());
            for (int i = 0; i < n; ++i)
                QVERIFY2(sameSeries(*snap->shots[size_t(i)], *fullSnap->shots[size_t(i)]),
                         qPrintable(snap->shots[size_t(i)]->swingDir));
            fullSnap.reset();
        }

        // Against the fixture the mocks were drawn from (same session, harness dumps).
        if (compareFixture && !partial) {
            QFile f(qEnvironmentVariable("PP_CHARTS_FIXTURE"));
            if (f.open(QIODevice::ReadOnly)) {
                const QJsonArray fx = QJsonDocument::fromJson(f.readAll()).object()
                                          .value(QStringLiteral("shots")).toArray();
                QCOMPARE(n, int(fx.size()));
                int same = 0;
                for (int i = 0; i < n; ++i) {
                    const QJsonObject fs = fx.at(i).toObject();
                    const ShotSeries &s = *snap->shots[size_t(i)];
                    QCOMPARE(s.ordinal, fs.value(QStringLiteral("ordinal")).toInt());
                    QCOMPARE(s.impactUs, qint64(fs.value(QStringLiteral("impactUs")).toDouble()));
                    QCOMPARE(int(s.phases.size()), int(fs.value(QStringLiteral("phases")).toArray().size()));
                    const QJsonObject fm = fs.value(QStringLiteral("metrics")).toObject();
                    QCOMPARE(s.metrics.size(), fm.size());
                    const QJsonObject cs = fm.value(QStringLiteral("clubheadSpeed")).toObject();
                    const MetricSeries *c = s.metric(QStringLiteral("clubheadSpeed"));
                    QVERIFY(c);
                    const QJsonArray ft = cs.value(QStringLiteral("t_us")).toArray();
                    QCOMPARE(c->tUs.size(), size_t(ft.size()));
                    QCOMPARE(c->tUs.front(), qint64(ft.first().toDouble()));
                    QCOMPARE(c->tUs.back(),  qint64(ft.last().toDouble()));
                    same += s.diagnostics.fired == [&] {
                        QStringList l;
                        for (const QJsonValue &x : fs.value(QStringLiteral("diagnostics")).toObject()
                                                       .value(QStringLiteral("fired")).toArray())
                            l << x.toString();
                        return l;
                    }();
                }
                line(QStringLiteral("    fixture: %1 shots match on impact, phases, metric keys and clubhead-speed "
                                    "times; fired conditions identical on %2 of %1 (the ledger may have been "
                                    "regraded since the fixture was built)").arg(n).arg(same));
            }
        }
    }
}

QTEST_GUILESS_MAIN(ChartsSeriesModelTest)
#include "charts_series_model_test.moc"
