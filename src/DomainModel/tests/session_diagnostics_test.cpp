/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// THE SESSION'S DIAGNOSTICS OWNER AND THE RE-FIRE (refactor step 3.5-i; build/refactor/step-3/BRIEF.md §4.4, §6 row 3.5;
// DEC-191). Over the REAL session store, swing door, Athlete, Session and Shot, the real pool and receiver
// (src/Core/work_pool.h), the process's RuleData over the shipped content and the real fireFindings, on a temporary
// library — never the share. Every trash stays in it (setOsTrashForTest(false): the library's own .pinpoint-trash).
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT, NEVER FROM RUNNING THE CODE: BRIEF.md §4.4 steps 1–11 and its four
// "why this cannot lose a finding" rules; §6 row 3.5's test list; DEC-130, DEC-135, DEC-190, DEC-191 (a usual's change
// recomputes that club's shots' derived data only, never their findings; the unread mark persisted).
//
// Most cases read the shots through a FAKE FiringReader (below) that answers what the test says — a club and a launch
// angle laid over one real swing's phase grid (lm_7iron) — so the usuals move exactly as a case needs, and that can HOLD
// one read at a gate so a race is staged on purpose. It resolves each shot's folder through a session store's
// swingFolder, as the real reader does, so "the session unpublished" is that store's own answer. Case R reads real
// documents through the real reader (SwingStoreFiringReader), headless.
//
//   §6  J-2     a club correction re-fires the corrected shot, and both clubs' shots derive again (each exactly once)
//   §6  RV1     REVIEW-1's case: a shot of another club in flight while a usual changes is KEPT
//   §6  SUP     a superseded result is dropped and still settles the batch (one settle, the newer ask kept)
//   §6  TRA     a trashed shot's result is dropped and settles (ItemGone; nothing kept)
//   T1          a session released and loaded again while its re-fire runs keeps none of the old instance's results
//   T2          one new shot gives exactly one re-derive of each shot of its club and none of another club, and the
//               chain stops: a second shot that leaves the usual's value unchanged asks nothing more
//   USU         a usual's change recomputes derived data only: the club's findings unchanged, their derived data
//               computed against the new usual (DEC-191)
//   T3          a read that fails mid-run (the session unpublished) returns unread and leaves the kept findings
//               untouched; the mark is persisted (diagnostics.json's `unread`) and the next load re-asks exactly those
//   T4          a release with kept but unwritten results writes them first; a load re-asks exactly the shots whose
//               stamp is stale or that have no result
//   R           the real reader, headless (§4.4 step 11): the kept findings are fireFindings' on the document's own read;
//               a store that does not publish the session reads nothing
//   Q18         release: a sweep over an athlete's sessions holds at most one at a time; the running session and one in
//               use are refused; the kept state's size per session is printed (Q-18's measurement, in part)
//
//   cmake --build build/tests --target session_diagnostics_test -j 6
//   ctest --test-dir build/tests -R '^session_diagnostics_test$' --output-on-failure

#include "../athlete.h"
#include "../session.h"
#include "../session_diagnostics.h"
#include "../shot.h"
#include "../../Core/work_pool.h"
#include "../../Diagnostics/measures/live_measure_source.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_doc.h"
#include "../../Export/swing_store_repository.h"
#include "../../Buffer/swing_window.h"   // a LoadedRecording owns one

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

using namespace pinpoint;
using pinpoint::analysis::ShotRecord;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static const QString   kName    = QStringLiteral("Test Golfer");
static const QString   kToken   = QStringLiteral("Test-Golfer");
static const AthleteId kAthlete = AthleteId::fromString(u"5a1d9c3e-2b7f-4e8a-9c10-7d2e6f3b4a21");
static const QString   kPat     = QStringLiteral("date-name-type");
static const QString   kA       = QStringLiteral("7 IRON");
static const QString   kB       = QStringLiteral("5 IRON");

static QString fixture(const char *name)
{
    return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
}

// Until `done` or `ms` pass, the event loop runs (the receiver's deliveries, deferred deletes).
static bool pump(const std::function<bool()> &done, int ms = 20000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return true;
}

// ── The fake reader ──────────────────────────────────────────────────────────────────────────────────────────
struct Spec {
    QString       club   = kA;
    double        launch = 15.0;
    DocumentStamp stamp{ true, 100, 1000 };
    ShotReview    review = [] { ShotReview r; r.ok = true; return r; }();   // a document whose review holds no golfer key
    bool          throwReview = false;   // review() throws (a derive that fails: C3)
    bool          realReview  = false;   // the review is the document's, through the real swing door (round 3)
    qint64        timestampMs = 0;       // 0: the default instant below
    bool          hasCapture  = true;    // false: the document records no capture instant (only its mtime)
};

class FakeReader final : public FiringReader
{
public:
    analysis::SwingPhaseGrid base;   // set once, before any job: read-only after
    std::atomic<const SessionRepository *> published{ nullptr };
    RecordingRepository *recordings = nullptr;   // the real swing door, for a spec's realReview (readReview: any thread)

    FiringRead read(const ShotRef &shot, const analysis::CharacteristicPack &) const override
    {
        const int swing = shot.swingNumber();
        Spec spec;
        int  epoch = 0;
        {
            std::unique_lock<std::mutex> l(m_mutex);
            ++m_reads[swing];
            epoch = m_epoch;   // what this read sees is fixed when it starts
            if (m_holdNext.remove(swing)) {
                m_blocked.insert(swing);
                m_cv.wait(l, [&] { return m_opened.contains(swing) || m_openAll; });
                m_opened.remove(swing);
                m_blocked.remove(swing);
            }
            spec = m_specs.value(swing);
        }
        FiringRead out;
        // Resolved AFTER the gate, through the store's swingFolder: a session unpublished while the read waited reads
        // nothing (T3's "fails mid-run").
        const SessionRepository *store = published.load();
        if (!store || store->swingFolder(shot).isEmpty()) {
            out.error = QStringLiteral("the session of %1 is not open").arg(shot.toString());
            return out;
        }
        out.ok             = true;
        out.stamp          = spec.stamp;
        out.timestampMs    = spec.timestampMs ? spec.timestampMs : 1700000000000LL + qint64(epoch) * 1000 + swing;   // which read
        out.captureMs      = spec.hasCapture ? out.timestampMs : 0;   // no instant: the timestamp is the file's mtime
        out.readings.grid  = base;
        out.readings.grid.club = spec.club;
        for (analysis::MetricPhaseGrid &m : out.readings.grid.metrics)
            if (m.key == QLatin1String("lm.launchAngle"))
                for (analysis::PhaseGridValue &v : m.values) v.value = spec.launch;
        out.readings.handedness = QStringLiteral("right");
        out.review = spec.realReview && recordings ? recordings->readReview(shot) : spec.review;
        return out;
    }
    ShotReview review(const ShotRef &shot) const override
    {
        Spec spec;
        {
            std::lock_guard<std::mutex> l(m_mutex);
            ++m_reviewReads[shot.swingNumber()];
            spec = m_specs.value(shot.swingNumber());
        }
        if (spec.throwReview) throw std::runtime_error("the review read failed (the test's)");
        return spec.realReview && recordings ? recordings->readReview(shot) : spec.review;
    }
    int reviewReads(int swing) const { std::lock_guard<std::mutex> l(m_mutex); return m_reviewReads.value(swing); }
    DocumentStamp stamp(const ShotRef &shot) const override
    {
        std::lock_guard<std::mutex> l(m_mutex);
        return m_specs.value(shot.swingNumber()).stamp;
    }

    void set(int swing, const Spec &s) { std::lock_guard<std::mutex> l(m_mutex); m_specs.insert(swing, s); }
    Spec spec(int swing) const { std::lock_guard<std::mutex> l(m_mutex); return m_specs.value(swing); }
    void setEpoch(int e) { std::lock_guard<std::mutex> l(m_mutex); m_epoch = e; }
    void holdNext(int swing) { std::lock_guard<std::mutex> l(m_mutex); m_holdNext.insert(swing); }
    bool blocked(int swing) const { std::lock_guard<std::mutex> l(m_mutex); return m_blocked.contains(swing); }
    int  reads(int swing) const { std::lock_guard<std::mutex> l(m_mutex); return m_reads.value(swing); }
    void open(int swing) { { std::lock_guard<std::mutex> l(m_mutex); m_opened.insert(swing); } m_cv.notify_all(); }
    void openAll() { { std::lock_guard<std::mutex> l(m_mutex); m_openAll = true; m_holdNext.clear(); } m_cv.notify_all(); }

private:
    mutable std::mutex              m_mutex;
    mutable std::condition_variable m_cv;
    QHash<int, Spec>                m_specs;
    mutable QHash<int, int>         m_reads, m_reviewReads;
    mutable QSet<int>               m_holdNext, m_blocked, m_opened;
    int                             m_epoch   = 0;
    bool                            m_openAll = false;
};

// The shipped pack and lm_7iron's grid, read once.
static std::shared_ptr<const RuleDataSnapshot> g_rules;
static analysis::SwingPhaseGrid                g_grid;

// ── One scratch library: the real store and doors, the pool and receiver, the athlete ────────────────────────
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    SessionStore                                   unpublished;   // holds nothing: every swingFolder is ""
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::shared_ptr<FakeReader>                    fake = std::make_shared<FakeReader>();
    std::unique_ptr<WorkReceiver>                  receiver;   // made before the pool, destroyed after it
    std::unique_ptr<WorkPool>                      pool;
    std::unique_ptr<Athlete>                       athlete;    // destroyed first: its sessions' diagnostics write and cancel

    explicit Rig(bool synchronous = false, int debounceMs = 600000, bool realReader = false)
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        fake->base = g_grid;
        fake->published = &store;
        fake->recordings = recordings.get();
        receiver = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        if (!synchronous) pool = std::make_unique<WorkPool>(3);
        athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, dir.path() + QLatin1Char('/') + kToken, SessionOwner{ kAthlete, kName, dir.path() },
                           Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        DiagnosticsDoors d;
        d.pool            = pool.get();
        d.receiver        = receiver.get();
        d.ruleData        = RuleData::installed();
        d.reader          = realReader ? std::shared_ptr<const FiringReader>(std::make_shared<SwingStoreFiringReader>(store, *recordings))
                                       : std::shared_ptr<const FiringReader>(fake);
        d.persist         = true;    // this suite's own scratch library only
        d.synchronous     = synchronous;
        d.writeDebounceMs = debounceMs;
        d.attach          = &SessionDiagnostics::attach;
        athlete->setDiagnosticsDoors(d);
    }
    ~Rig()
    {
        fake->openAll();   // no job may stay at a gate: the pool joins its workers
        athlete.reset();
        pool.reset();
    }

    // A started session with one shot per spec: each swing folder holds a real document (lm_7iron's, so the shot's
    // commands can save) and the fake answers the spec. EVT-01 is reported for each (D19), which asks each one.
    Session *session(const QVector<Spec> &specs, const char *doc = "lm_7iron")
    {
        Session *s = athlete->startSession(QStringLiteral("swing"), kPat);
        if (!s) return nullptr;
        for (const Spec &spec : specs) addShot(s, spec, doc);
        return s;
    }
    Shot *addShot(Session *s, const Spec &spec, const char *doc = "lm_7iron")
    {
        Shot *shot = s->addShot();
        if (!shot) return nullptr;
        QFile::copy(fixture(doc), QDir(shot->folder()).filePath(QStringLiteral("swing.json")));
        fake->set(shot->swingNumber(), spec);
        s->shotDocumentWritten(shot->ref());   // EVT-01
        return shot;
    }
    QString folderOf(const SessionId &id) const { const auto e = store.open(id); return e ? e->folder : QString(); }
    static bool settled(SessionDiagnostics *d, int ms = 20000) { return d && pump([d] { return d->outstanding() == 0; }, ms); }
};

static Spec spec(const QString &club, double launch) { Spec s; s.club = club; s.launch = launch; return s; }

static QByteArray json(const ShotRecord &r)
{
    return QJsonDocument(analysis::toJson(std::vector<ShotRecord>{ r })).toJson(QJsonDocument::Compact);
}

// The asks made since `from`: swing → how many fires, how many derives.
struct AskTally { std::map<int, int> fires, derives; int totalFires = 0, totalDerives = 0; };
static AskTally asksSince(const SessionDiagnostics *d, size_t from)
{
    AskTally t;
    for (size_t i = from; i < d->asks().size(); ++i) {
        const auto &a = d->asks()[i];
        if (a.derive) { ++t.derives[a.swing]; ++t.totalDerives; }
        else          { ++t.fires[a.swing];   ++t.totalFires; }
    }
    return t;
}
static QString tallyText(const AskTally &t)
{
    QStringList f, d;
    for (const auto &[s, n] : t.fires) f << QStringLiteral("%1×%2").arg(s).arg(n);
    for (const auto &[s, n] : t.derives) d << QStringLiteral("%1×%2").arg(s).arg(n);
    return QStringLiteral("fires {%1} derives {%2}").arg(f.join(QLatin1Char(' ')), d.join(QLatin1Char(' ')));
}
static bool exactlyOnce(const std::map<int, int> &m, std::initializer_list<int> want)
{
    if (m.size() != want.size()) return false;
    for (const int w : want) {
        const auto it = m.find(w);
        if (it == m.end() || it->second != 1) return false;
    }
    return true;
}

static analysis::ClubFlightUsual usualOf(const SessionDiagnostics *d, const QString &club)
{
    return d->usuals().value(canonicalClubId(club));
}

// ── T2 and USU ─────────────────────────────────────────────────────────────────────────────────────────────
static void testT2()
{
    std::printf("\nT2 / USU — one new shot: exactly one re-derive of each shot of its club, none of another; the chain stops\n");
    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18),
                               spec(kB, 20), spec(kB, 22), spec(kB, 24), spec(kB, 26) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    check(QStringLiteral("T2a the session has its diagnostics, its child"), d && d->parent() == s);
    check(QStringLiteral("T2b nine shots fired and kept"), Rig::settled(d) && d->keptShots().size() == 9);
    if (!d || d->keptShots().size() != 9) return;
    std::printf("    usual launch: %s %.2f, %s %.2f\n", qPrintable(kA), usualOf(d, kA).launch, qPrintable(kB),
                usualOf(d, kB).launch);
    check(QStringLiteral("T2c the usuals come from the kept readings: %1 14, %2 23").arg(kA, kB),
          usualOf(d, kA).launch == 14.0 && usualOf(d, kB).launch == 23.0);

    QHash<int, QByteArray> before;
    for (const int n : d->keptShots()) before.insert(n, json(d->kept(n)->findings));
    const analysis::ClubFlightUsual bUsual = usualOf(d, kB);

    size_t from = d->asks().size();
    Shot *ten = rig.addShot(s, spec(kA, 20));   // A: 10…20 → median 15
    check(QStringLiteral("T2d the new shot settles"), ten && Rig::settled(d));
    AskTally t = asksSince(d, from);
    std::printf("    after shot %d: %s\n", ten ? ten->swingNumber() : -1, qPrintable(tallyText(t)));
    check(QStringLiteral("T2e exactly one fire, the new shot's"), ten && exactlyOnce(t.fires, { ten->swingNumber() }));
    check(QStringLiteral("T2f exactly one derive of each shot of its club (1–5 and the new one), none of the other club"),
          ten && exactlyOnce(t.derives, { 1, 2, 3, 4, 5, ten->swingNumber() }));
    check(QStringLiteral("T2g its club's usual moved to 15, the other club's did not"),
          usualOf(d, kA).launch == 15.0 && usualOf(d, kB).launch == bUsual.launch);

    // USU: a usual's change recomputes derived data only.
    bool same = true, current = true;
    for (int n = 1; n <= 9; ++n) {
        const KeptShot *k = d->kept(n);
        same = same && k && json(k->findings) == before.value(n);
        const analysis::ClubFlightUsual want = usualOf(d, n <= 5 ? kA : kB);
        current = current && k && k->derived && k->derived->usual.launch == want.launch;
    }
    check(QStringLiteral("USUa every earlier shot's findings are exactly as fired (no finding re-fired)"), same);
    check(QStringLiteral("USUb every shot's derived data is computed against its club's usual as it now is"), current);

    from = d->asks().size();
    Shot *eleven = rig.addShot(s, spec(kA, 15));   // A: 10,12,14,15,16,18,20 → median 15, unchanged
    check(QStringLiteral("T2h a second new shot settles"), eleven && Rig::settled(d));
    pump([] { return false; }, 150);               // nothing more arrives later
    t = asksSince(d, from);
    std::printf("    after shot %d: %s\n", eleven ? eleven->swingNumber() : -1, qPrintable(tallyText(t)));
    check(QStringLiteral("T2i the chain stops: one fire (its own), no derive, the usual's value unchanged"),
          eleven && exactlyOnce(t.fires, { eleven->swingNumber() }) && t.totalDerives == 0
              && usualOf(d, kA).launch == 15.0 && d->outstanding() == 0);
}

// ── J-2 ───────────────────────────────────────────────────────────────────────────────────────────────────
static void testJ2()
{
    std::printf("\nJ-2 — a club correction: the shot fires again, both clubs' shots derive again\n");
    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18),
                               spec(kB, 20), spec(kB, 22), spec(kB, 24), spec(kB, 26) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("J-2 setup"), false); return; }
    Shot *one = s->shot(ShotRef(s->id(), 1));
    const size_t from = d->asks().size();
    // The fake answers the corrected club (the real reader reads it from the document the command wrote).
    rig.fake->set(1, spec(kB, 10));
    const WriteResult w = one ? one->correctClub(kB) : WriteResult{};
    check(QStringLiteral("J-2a the correction is saved (EVT-04)"), w.ok());
    check(QStringLiteral("J-2b it settles"), Rig::settled(d));
    const AskTally t = asksSince(d, from);
    std::printf("    %s; usual launch %s %.2f, %s %.2f\n", qPrintable(tallyText(t)), qPrintable(kA), usualOf(d, kA).launch,
                qPrintable(kB), usualOf(d, kB).launch);
    check(QStringLiteral("J-2c exactly one fire, the corrected shot's"), exactlyOnce(t.fires, { 1 }));
    check(QStringLiteral("J-2d every shot of BOTH clubs derives again exactly once"),
          exactlyOnce(t.derives, { 1, 2, 3, 4, 5, 6, 7, 8, 9 }));
    check(QStringLiteral("J-2e the shot is kept under its new club; both usuals moved"),
          d->kept(1) && canonicalClubId(d->kept(1)->findings.club) == canonicalClubId(kB)
              && usualOf(d, kA).launch == 15.0 && usualOf(d, kB).launch == 22.0);
}

// ── REVIEW-1's case ───────────────────────────────────────────────────────────────────────────────────────
static void testReview1()
{
    std::printf("\nRV1 — a shot of another club in flight while a usual changes is kept\n");
    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18),
                               spec(kB, 20), spec(kB, 22), spec(kB, 24), spec(kB, 26) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("RV1 setup"), false); return; }
    rig.fake->setEpoch(7);
    rig.fake->holdNext(7);
    d->fire(ShotRef(s->id(), 7));   // club B's shot 7 in flight (its own command)
    check(QStringLiteral("RV1a shot 7's read is in flight"), pump([&] { return rig.fake->blocked(7); }));
    const quint64 superseded = d->counts().superseded;
    const size_t  from = d->asks().size();
    const WriteResult tw = s->trashShot(ShotRef(s->id(), 2));   // club A's usual changes now
    check(QStringLiteral("RV1b a club-A shot trashed (EVT-03) moves A's usual while 7 is in flight"),
          tw.ok() && usualOf(d, kA).launch == 15.0);
    rig.fake->open(7);
    check(QStringLiteral("RV1c it settles"), Rig::settled(d));
    const AskTally t = asksSince(d, from);
    std::printf("    %s\n", qPrintable(tallyText(t)));
    check(QStringLiteral("RV1d shot 7's result is KEPT (its ask was still its latest): the new read's"),
          d->kept(7) && d->kept(7)->findings.timestampMs == 1700000000000LL + 7 * 1000 + 7
              && d->counts().superseded == superseded);
    check(QStringLiteral("RV1e club A's remaining shots derived again, club B's not"),
          exactlyOnce(t.derives, { 1, 3, 4, 5 }) && t.totalFires == 0);
}

// ── Superseded, and trashed ───────────────────────────────────────────────────────────────────────────────
static void testSupersededAndTrashed()
{
    std::printf("\nSUP / TRA — a superseded result and a trashed shot's result are dropped and settle\n");
    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("SUP setup"), false); return; }
    int settles = 0;
    QObject::connect(d, &SessionDiagnostics::batchSettled, d, [&settles] { ++settles; });

    rig.fake->setEpoch(5);
    rig.fake->holdNext(2);
    d->fire(ShotRef(s->id(), 2));
    pump([&] { return rig.fake->blocked(2); });
    rig.fake->setEpoch(6);
    d->fire(ShotRef(s->id(), 2));   // asked again while the first ask is reading
    const quint64 sup = d->counts().superseded;
    rig.fake->open(2);
    check(QStringLiteral("SUPa it settles"), Rig::settled(d));
    check(QStringLiteral("SUPb the first result is dropped as superseded"), d->counts().superseded == sup + 1);
    check(QStringLiteral("SUPc the newer ask's result is kept"),
          d->kept(2) && d->kept(2)->findings.timestampMs == 1700000000000LL + 6 * 1000 + 2);
    check(QStringLiteral("SUPd the batch settled once, after both came back (got %1)").arg(settles), settles == 1);

    settles = 0;
    rig.fake->holdNext(3);
    d->fire(ShotRef(s->id(), 3));
    pump([&] { return rig.fake->blocked(3); });
    const quint64 gone = d->counts().itemGone;
    const WriteResult tw = s->trashShot(ShotRef(s->id(), 3));
    check(QStringLiteral("TRAa the shot is trashed while its ask reads; its kept result goes with it"),
          tw.ok() && !d->kept(3) && d->outstanding() == 1);
    rig.fake->open(3);
    check(QStringLiteral("TRAb it settles"), Rig::settled(d));
    check(QStringLiteral("TRAc its result is dropped (ItemGone), nothing kept, the batch settled"),
          d->counts().itemGone == gone + 1 && !d->kept(3) && settles >= 1);
}

// ── T1 ─────────────────────────────────────────────────────────────────────────────────────────────────────
static void testT1()
{
    std::printf("\nT1 — released and loaded again while its re-fire runs: none of the old instance's results kept\n");
    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("T1 setup"), false); return; }
    const SessionId id = s->id();
    check(QStringLiteral("T1a ended (not running here, so it may be released)"), s->end().ok());
    const quint64 oldInstance = d->instance();

    rig.fake->setEpoch(1);
    rig.fake->holdNext(3);
    d->fireAll();   // the re-fire (as a norm edit asks it)
    // Shots 1 and 2 read (epoch 1) and their results KEPT by this instance, shot 3 at its gate, 4 and 5 waiting.
    const auto keptAt = [](const SessionDiagnostics *x, int n, int epoch) {
        return x && x->kept(n) && x->kept(n)->findings.timestampMs == 1700000000000LL + epoch * 1000 + n;
    };
    check(QStringLiteral("T1b the re-fire reaches shot 3, which reads; shots 1–2 kept from it"),
          pump([&] { return rig.fake->blocked(3) && keptAt(d, 1, 1) && keptAt(d, 2, 1); }));
    rig.fake->setEpoch(2);
    Spec three = rig.fake->spec(3);
    three.stamp.mtimeMs += 1;   // shot 3's document changed: the new instance asks it again
    rig.fake->set(3, three);

    QString why;
    check(QStringLiteral("T1c released"), rig.athlete->releaseSession(id, &why) && !rig.athlete->loadedSession(id));
    Session *again = rig.athlete->session(id);
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    check(QStringLiteral("T1d loaded again: a new Session instance, with a new instance number"),
          again && d2 && d2->instance() != oldInstance && d2->instance() > oldInstance);
    check(QStringLiteral("T1e the new instance settles (its own ask of shot 3 read past the gate)"), Rig::settled(d2));
    const quint64 other = rig.receiver->count(WorkVerdict::OtherInstance);
    rig.fake->open(3);   // the old instance's read of shot 3 finishes now
    pump([&] { return rig.receiver->count(WorkVerdict::OtherInstance) > other; }, 5000);
    pump([] { return false; }, 100);
    check(QStringLiteral("T1f the old instance's result was dropped as another instance's"),
          rig.receiver->count(WorkVerdict::OtherInstance) == other + 1);
    check(QStringLiteral("T1g shot 3 holds the NEW instance's read (epoch 2), never the old one's (epoch 1)"),
          d2 && d2->kept(3) && d2->kept(3)->findings.timestampMs == 1700000000000LL + 2 * 1000 + 3);
    // What the old instance KEPT before its release was written first and is the new one's (shots 1 and 2, read in
    // epoch 1 before the gate); shots 4 and 5, whose asks the release cancelled, hold what the file held (epoch 0).
    check(QStringLiteral("T1h shots 1–2 hold the reads the old instance kept before the release; 4–5 their earlier ones"),
          keptAt(d2, 1, 1) && keptAt(d2, 2, 1) && keptAt(d2, 4, 0) && keptAt(d2, 5, 0));
}

// ── T3 ─────────────────────────────────────────────────────────────────────────────────────────────────────
static void testT3()
{
    std::printf("\nT3 — a read that fails mid-run (the session unpublished) is unread; the kept findings untouched\n");
    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("T3 setup"), false); return; }
    QHash<int, QByteArray> before;
    for (const int n : d->keptShots()) before.insert(n, json(d->kept(n)->findings));
    int unreadSignals = 0;
    QObject::connect(d, &SessionDiagnostics::shotUnread, d, [&unreadSignals](const ShotRef &, const QString &) { ++unreadSignals; });

    rig.fake->holdNext(1);
    d->fireAll();
    pump([&] { return rig.fake->blocked(1); });
    rig.fake->published = &rig.unpublished;   // mid-run: the store no longer publishes the session
    rig.fake->open(1);
    check(QStringLiteral("T3a it settles"), Rig::settled(d));
    check(QStringLiteral("T3b every shot came back unread"), d->unread() == QSet<int>{ 1, 2, 3 } && unreadSignals == 3);
    bool untouched = true;
    for (const int n : before.keys()) untouched = untouched && d->kept(n) && json(d->kept(n)->findings) == before.value(n);
    check(QStringLiteral("T3c their kept findings are untouched"), untouched && d->keptShots().size() == 3);
    check(QStringLiteral("T3d written"), d->flush() && d->counts().writes >= 1);
    const DiagnosticsRead file = rig.store.readDiagnostics(s->id());
    const QJsonArray marks = file.envelope.session.value(QStringLiteral("unread")).toArray();
    check(QStringLiteral("T3e the mark is persisted: diagnostics.json's unread = [1,2,3] (DEC-191)"),
          file.state == DiagnosticsRead::State::Read && marks == QJsonArray{ 1, 2, 3 });

    rig.fake->published = &rig.store;
    s->end();
    rig.athlete->releaseSession(s->id());
    Session *again = rig.athlete->session(s->id());
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    check(QStringLiteral("T3f the next load re-asks exactly the unread shots, and keeps them"),
          d2 && Rig::settled(d2) && exactlyOnce(asksSince(d2, 0).fires, { 1, 2, 3 }) && d2->unread().isEmpty()
              && d2->keptShots().size() == 3);
}

// ── T4 ─────────────────────────────────────────────────────────────────────────────────────────────────────
static void testT4()
{
    std::printf("\nT4 — a release writes what is kept first; a load re-asks exactly the stale or missing shots\n");
    Rig rig;   // the debounce never fires within the test: only the release writes
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), kPat);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!d) { check(QStringLiteral("T4 setup"), false); return; }
    rig.fake->holdNext(5);
    for (int i = 0; i < 5; ++i) rig.addShot(s, spec(kA, 10 + 2 * i));
    // (The derives the fourth shot's usual asks wait behind shot 5's read in the lane: one lane, one at a time.)
    check(QStringLiteral("T4a four kept, the fifth still reading"),
          pump([&] { return rig.fake->blocked(5) && d->keptShots().size() == 4; }));
    check(QStringLiteral("T4b nothing written yet"),
          rig.store.readDiagnostics(s->id()).state == DiagnosticsRead::State::Missing && d->dirty());
    s->end();
    check(QStringLiteral("T4c released"), rig.athlete->releaseSession(s->id()));
    const DiagnosticsRead file = rig.store.readDiagnostics(s->id());
    const std::vector<ShotRecord> rows = analysis::fromJson(file.envelope.ledger);
    check(QStringLiteral("T4d the release wrote the four kept results first"),
          file.state == DiagnosticsRead::State::Read && rows.size() == 4
              && file.envelope.session.value(QStringLiteral("gradedFrom")).toObject().size() == 4);

    Spec two = rig.fake->spec(2);
    two.stamp.mtimeMs += 1;   // shot 2's document rewritten since: its stamp is stale
    rig.fake->set(2, two);
    Session *again = rig.athlete->session(s->id());
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    check(QStringLiteral("T4e loaded again; it settles"), d2 && Rig::settled(d2));
    const AskTally t = d2 ? asksSince(d2, 0) : AskTally{};
    std::printf("    %s\n", qPrintable(tallyText(t)));
    check(QStringLiteral("T4f the load fired exactly shot 2 (stale) and shot 5 (no result)"), exactlyOnce(t.fires, { 2, 5 }));
    check(QStringLiteral("T4g every shot is kept"), d2 && d2->keptShots().size() == 5);
    rig.fake->open(5);
    pump([] { return false; }, 100);
}

// ── R: the real reader, headless ───────────────────────────────────────────────────────────────────────────
static void testRealReader()
{
    std::printf("\nR — the real reader, headless: the kept findings are fireFindings' on the document's own read\n");
    Rig rig(/*synchronous=*/true, 0, /*realReader=*/true);
    Session *s = rig.session({ Spec{}, Spec{} });   // lm_7iron's document in both
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    check(QStringLiteral("Ra headless: every ask came back inline"), d && d->outstanding() == 0 && d->keptShots().size() == 2);
    if (!d || d->keptShots().size() != 2) return;
    const KeptShot *k = d->kept(1);
    Shot *shot = s->shot(ShotRef(s->id(), 1));
    const QString folder = shot ? shot->folder() : QString();
    // The read detectShot makes, by hand, against the same snapshot.
    analysis::FiringReadings r;
    r.grid = analysis::readLivePhaseGrid(folder, d->snapshot()->inputs.pack->pack());
    const SwingSummary sum = SwingDocReader::readSwingSummary(folder);
    r.handedness    = sum.handedness;
    r.dataWarning   = sum.dataWarning;
    r.captureBroken = sum.dataWarningDetail.value(QStringLiteral("capture")).toBool();
    r.imuBroken     = sum.dataWarningDetail.value(QStringLiteral("imu")).toBool();
    analysis::FiredShot direct = analysis::fireFindings(std::move(r), d->snapshot()->inputs);
    direct.record.shotId      = 1;
    direct.record.timestampMs = k->findings.timestampMs;
    check(QStringLiteral("Rb the kept findings equal fireFindings on the same read and snapshot"),
          k && direct.ok && json(k->findings) == json(direct.record) && !k->findings.rows.empty());
    check(QStringLiteral("Rc it is the 7 iron the document declares, with its launch-monitor readings"),
          canonicalClubId(k->findings.club) == canonicalClubId(kA) && k->hasLaunchMonitor && k->document.exists);
    check(QStringLiteral("Rd its derived data is there (layer 3)"), k->derived.has_value());
    const SwingStoreFiringReader off(rig.unpublished, *rig.recordings);
    const FiringRead none = off.read(ShotRef(s->id(), 1), d->snapshot()->inputs.pack->pack());
    check(QStringLiteral("Re a store that does not publish the session reads nothing (unread, with the reason)"),
          !none.ok && !none.error.isEmpty());
}

// ── Q18: release in a sweep ────────────────────────────────────────────────────────────────────────────────
static void testRelease()
{
    std::printf("\nQ18 — release: a sweep holds at most one session at a time\n");
    Rig rig;
    QList<SessionId> ids;
    for (int i = 0; i < 4; ++i) {
        Session *s = rig.session({ spec(kA, 10), spec(kA, 12) });
        if (!s || !Rig::settled(s->diagnostics())) { check(QStringLiteral("Q18 setup"), false); return; }
        ids << s->id();
        if (i < 3) s->end();   // the last one stays running here
    }
    QString why;
    check(QStringLiteral("Q18a the session running here is refused"),
          !rig.athlete->releaseSession(ids.last(), &why) && !why.isEmpty());
    Session *used = rig.athlete->loadedSession(ids.first());
    used->addUse();
    check(QStringLiteral("Q18b a session in use is refused"), !rig.athlete->releaseSession(ids.first(), &why));
    used->dropUse();
    check(QStringLiteral("Q18c releaseUnused lets the three ended ones go"), rig.athlete->releaseUnused() == 3
              && rig.athlete->loadedSessions().size() == 1);
    pump([] { return false; }, 50);

    int most = 0;
    qint64 bytes = 0;
    for (int i = 0; i < 3; ++i) {
        Session *s = rig.athlete->session(ids[i]);
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        Rig::settled(d);
        if (d)
            for (const int n : d->keptShots()) bytes += json(d->kept(n)->findings).size();
        most = std::max(most, int(rig.athlete->loadedSessions().size()) - 1);   // less the running one
        rig.athlete->releaseSession(ids[i]);
    }
    pump([] { return false; }, 50);
    check(QStringLiteral("Q18d the sweep held at most one released session at a time (held %1)").arg(most), most == 1
              && rig.athlete->loadedSessions().size() == 1);
    std::printf("    Q-18: kept findings of a 2-shot session, as JSON: %lld bytes per session (%lld per shot)\n",
                (long long)(bytes / 3), (long long)(bytes / 6));
}

// ── Round 2 (review 1) ────────────────────────────────────────────────────────────────────────────────────

static ShotReview reviewSaying(const QString &intent, const QString &correctedTo)
{
    ShotReview r;
    r.ok = true;
    r.hasShotIntent = true;
    r.shotIntent = intent;
    r.hasLabelCorrections = true;
    r.labelCorrections = { LabelCorrection{ QStringLiteral("direction"), QString(), correctedTo } };
    return r;
}

// The derived outcome as text: its labels (id, corrected, intended), its phrase, the carry it reads, the factors.
static QByteArray outcomeKey(const ShotDerived &d)
{
    QByteArray out = (d.data.outcome.ready ? "ready;" : "notready;") + d.data.outcome.phrase.toUtf8() + ';';
    for (const analysis::ShotOutcomeLabel &l : d.data.outcome.labels)
        out += l.id.toUtf8() + (l.corrected ? "*" : "") + (l.intended ? "!" : "") + ',';
    for (const analysis::ShotFactor *f : { &d.data.factors.strike, &d.data.factors.direction, &d.data.factors.flight })
        out += '|' + f->values.join(QLatin1Char(',')).toUtf8();
    return out + "|intent=" + d.intent.toUtf8();
}
static bool hasCorrectedLabel(const ShotDerived &d, const QString &id)
{
    for (const analysis::ShotOutcomeLabel &l : d.data.outcome.labels)
        if (l.id == id && l.corrected) return true;
    return false;
}

// C1: a load derives each kept shot with the golfer's word, read on the worker.
static void testWordAfterLoad()
{
    std::printf("\nWORD — a shot with a correction and an intent, released and loaded again, derives the same outcome\n");
    Rig rig;
    QVector<Spec> specs{ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18) };
    specs[2].review = reviewSaying(QStringLiteral("slice"), QStringLiteral("slice"));
    Session *s = rig.session(specs);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d) || !d->kept(3) || !d->kept(3)->derived) { check(QStringLiteral("WORD setup"), false); return; }
    const QByteArray before = outcomeKey(*d->kept(3)->derived);
    std::printf("    before: %s\n", before.constData());
    check(QStringLiteral("WORDa the fire's derive used the word: intent slice, the slice corrected"),
          d->kept(3)->derived->intent == QLatin1String("slice") && hasCorrectedLabel(*d->kept(3)->derived, QStringLiteral("slice")));
    s->end();
    const SessionId id = s->id();
    rig.athlete->releaseSession(id);
    Session *again = rig.athlete->session(id);
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    check(QStringLiteral("WORDb loaded again; it settles"), Rig::settled(d2));
    const KeptShot *k = d2 ? d2->kept(3) : nullptr;
    const AskTally t = d2 ? asksSince(d2, 0) : AskTally{};
    std::printf("    after:  %s; %s\n", k && k->derived ? outcomeKey(*k->derived).constData() : "(none)", qPrintable(tallyText(t)));
    check(QStringLiteral("WORDc shot 3 was derived, not fired, and its review read on the worker"),
          t.fires.empty() && t.derives.count(3) == 1 && rig.fake->reviewReads(3) >= 1);
    check(QStringLiteral("WORDd its derived outcome is exactly the one before the release"),
          k && k->derived && outcomeKey(*k->derived) == before);
}

// C2: the J-33 fallback — the golfer's input only in diagnostics.json's older keys reads as it does from its new homes.
static void testLegacyGolferInput()
{
    std::printf("\nLEG — golfer input only in diagnostics.json's older keys = the same input in its new homes (J-33)\n");
    Rig rig;
    // NEW HOMES: shot 3's review holds its intent and correction, session_input.json the run from shot 4.
    QVector<Spec> specs{ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18) };
    QVector<Spec> withWord = specs;
    withWord[2].review = reviewSaying(QStringLiteral("slice"), QStringLiteral("slice"));
    Session *n = rig.session(withWord);
    SessionDiagnostics *dn = n ? n->diagnostics() : nullptr;
    if (!Rig::settled(dn)) { check(QStringLiteral("LEG setup (new homes)"), false); return; }
    n->setIntentRun({ IntentRun{ 4, QStringLiteral("fade") } });   // EVT-08
    Rig::settled(dn);

    // THE OLDER SHAPE: the same session's facts, but the reviews hold no golfer key and there is no session_input.json;
    // diagnostics.json's older keys hold them (a session not migrated by M3).
    Session *o = rig.session(specs);
    SessionDiagnostics *dold = o ? o->diagnostics() : nullptr;
    if (!Rig::settled(dold)) { check(QStringLiteral("LEG setup (older shape)"), false); return; }
    o->end();
    const SessionId oid = o->id();
    const QString folder = rig.folderOf(oid);
    rig.athlete->releaseSession(oid);   // its diagnostics.json written, then the older keys put into it (scratch)
    {
        const QString path = SessionFiles::diagnosticsFile(folder);
        QFile f(path);
        QJsonObject root = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
        f.close();
        root.insert(QStringLiteral("shotIntent"), QJsonObject{ { QStringLiteral("3"), QStringLiteral("slice") } });
        root.insert(QStringLiteral("corrections"), QJsonObject{ { QStringLiteral("3"), QJsonArray{ QJsonObject{
            { QStringLiteral("family"), QStringLiteral("direction") }, { QStringLiteral("from"), QString() },
            { QStringLiteral("to"), QStringLiteral("slice") } } } } });
        root.insert(QStringLiteral("intentFrom"), QJsonObject{ { QStringLiteral("fromShot"), 4 },
                                                               { QStringLiteral("shape"), QStringLiteral("fade") } });
        QFile w(path);
        check(QStringLiteral("LEGa the older keys written into the scratch file"),
              !root.isEmpty() && w.open(QIODevice::WriteOnly | QIODevice::Truncate)
                  && w.write(QJsonDocument(root).toJson()) > 0);
    }
    Session *again = rig.athlete->session(oid);
    SessionDiagnostics *dl = again ? again->diagnostics() : nullptr;
    check(QStringLiteral("LEGb loaded; it settles"), Rig::settled(dl));
    bool same = true;
    for (int sw = 1; sw <= 5; ++sw) {
        const KeptShot *a = dn->kept(sw);
        const KeptShot *b = dl ? dl->kept(sw) : nullptr;
        const bool eq = a && b && a->derived && b->derived && outcomeKey(*a->derived) == outcomeKey(*b->derived);
        if (!eq)
            std::printf("    shot %d differs: new %s / older %s\n", sw,
                        a && a->derived ? outcomeKey(*a->derived).constData() : "-",
                        b && b->derived ? outcomeKey(*b->derived).constData() : "-");
        same = same && eq;
    }
    check(QStringLiteral("LEGc every shot's derived data is the same from the older keys as from the new homes"), same);
    check(QStringLiteral("LEGd from the older keys: shot 3 slice and corrected, shots 4–5 fade (the run), 1–2 none"),
          dl && dl->kept(3) && dl->kept(3)->derived && dl->kept(3)->derived->intent == QLatin1String("slice")
              && hasCorrectedLabel(*dl->kept(3)->derived, QStringLiteral("slice"))
              && dl->kept(4)->derived->intent == QLatin1String("fade") && dl->kept(5)->derived->intent == QLatin1String("fade")
              && dl->kept(2)->derived->intent.isEmpty() && dl->kept(1)->derived->intent.isEmpty());
    // A review that holds the key wins over the older copy (field by field): shot 3 cleared in its review → no intent.
    ShotReview cleared;
    cleared.ok = true;
    cleared.hasShotIntent = true;
    check(QStringLiteral("LEGe the rule: a review holding the key wins, even empty; an absent key falls back"),
          [&] {
              QHash<int, QString> in{ { 3, QStringLiteral("slice") } };
              QHash<int, std::vector<analysis::ShotCorrection>> c;
              golfer_input::applyReview(cleared, 3, in, c);
              QHash<int, QString> in2{ { 3, QStringLiteral("slice") } };
              ShotReview none; none.ok = true;
              golfer_input::applyReview(none, 3, in2, c);
              return !in.contains(3) && in2.value(3) == QLatin1String("slice");
          }());
}

// C3: a derive that fails is not asked again until one of its inputs moves — async and headless.
static void testDeriveFailure(bool headless)
{
    std::printf("\nFAIL (%s) — a derive that throws is marked, not re-asked, until an input moves\n",
                headless ? "headless" : "pool");
    Rig rig(headless);
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("FAIL setup"), false); return; }
    s->end();
    const SessionId id = s->id();
    Spec two = rig.fake->spec(2);
    two.throwReview = true;
    rig.fake->set(2, two);
    rig.athlete->releaseSession(id);
    Session *again = rig.athlete->session(id);   // its load derives 2, whose review read throws
    SessionDiagnostics *d2 = again ? again->diagnostics() : nullptr;
    check(QStringLiteral("FAILa loaded; it settles (no loop)"), Rig::settled(d2, 5000));
    pump([] { return false; }, 200);
    AskTally t = d2 ? asksSince(d2, 0) : AskTally{};
    std::printf("    %s\n", qPrintable(tallyText(t)));
    check(QStringLiteral("FAILb shot 2 was derived exactly once, marked failed, and nothing is outstanding"),
          d2 && t.derives.count(2) == 1 && t.derives[2] == 1 && d2->counts().deriveFailures == 1 && d2->outstanding() == 0
              && d2->kept(2) && d2->kept(2)->deriveFailed);
    again->setIntentRun({ IntentRun{ 1, QStringLiteral("fade") } });   // an input moves: the layer-3 generation
    check(QStringLiteral("FAILc after an input moves it settles"), Rig::settled(d2, 5000));
    pump([] { return false; }, 200);
    t = asksSince(d2, 0);
    std::printf("    %s\n", qPrintable(tallyText(t)));
    check(QStringLiteral("FAILd shot 2 was asked once more (and failed again), the others derived"),
          t.derives[2] == 2 && d2->counts().deriveFailures == 2 && t.derives[1] == 2 && t.derives[3] == 2);
}

// C4: the cohort on the shot's date (DEC-171); a record change re-fires every shot.
static void testCohortDate()
{
    std::printf("\nCOH — the norm cohort on the shot's own date; a record change re-fires\n");
    const QDate dob(1971, 11, 16);
    const QDate day(2026, 11, 15);
    const qint64 before = QDateTime(QDate(2026, 11, 15), QTime(12, 0)).toMSecsSinceEpoch();
    const qint64 after  = QDateTime(QDate(2026, 11, 16), QTime(12, 0)).toMSecsSinceEpoch();
    check(QStringLiteral("COHa the shot's date is its capture instant's local date, else the session's day"),
          shotCohortDate(before, day) == QDate(2026, 11, 15) && shotCohortDate(after, day) == QDate(2026, 11, 16)
              && shotCohortDate(0, day) == day);
    const analysis::Cohort c1 = cohortOnShotDate(dob, QStringLiteral("female"), before, day);
    const analysis::Cohort c2 = cohortOnShotDate(dob, QStringLiteral("female"), after, day);
    check(QStringLiteral("COHb a session that spans her 55th birthday: the shot before is 18–54, the shot after 55–64"),
          c1 == analysis::cohortFor(dob, QStringLiteral("female"), QDate(2026, 11, 15)) && c1 != c2
              && c1.age == analysis::AgeBand::Adult18_54 && c2.age == analysis::AgeBand::Adult55_64);

    Rig rig;
    Session *s = rig.session({ spec(kA, 10), spec(kA, 12), spec(kA, 14) });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d)) { check(QStringLiteral("COH setup"), false); return; }
    const QString stamp = d->kept(1)->findingsStamp;
    const size_t from = d->asks().size();
    AthleteRecord r = rig.athlete->record();
    r.sex = QStringLiteral("female");
    r.dob = QStringLiteral("1971-11-16");
    rig.athlete->setStored(r, rig.athlete->folder(), rig.athlete->sessionOwner(), Athlete::Source::Library, false);
    check(QStringLiteral("COHc it settles"), Rig::settled(d));
    const AskTally t = asksSince(d, from);
    check(QStringLiteral("COHd a sex / date-of-birth change re-fires every shot, under a new findings stamp"),
          exactlyOnce(t.fires, { 1, 2, 3 }) && d->kept(1)->findingsStamp != stamp);
    const size_t from2 = d->asks().size();
    r.notes = QStringLiteral("a note");   // a record edit that leaves the cohort's facts
    rig.athlete->setStored(r, rig.athlete->folder(), rig.athlete->sessionOwner(), Athlete::Source::Library, false);
    pump([] { return false; }, 100);
    check(QStringLiteral("COHe an edit that leaves sex and date of birth asks nothing"), d->asks().size() == from2);
}

// ── Round 3 (review 2) ────────────────────────────────────────────────────────────────────────────────────

// The cohort is dated by the capture instant, never the file's mtime.
static void testCohortNoInstant()
{
    std::printf("\nCOH2 — a document with no capture instant takes the session day's cohort, never its mtime's\n");
    Rig rig;
    // Today's session; a record whose 55th birthday is the session's day: the day before is another cohort.
    const QDate today = QDate::currentDate();
    AthleteRecord r = rig.athlete->record();
    r.sex = QStringLiteral("female");
    r.dob = today.addYears(-55).toString(Qt::ISODate);
    rig.athlete->setStored(r, rig.athlete->folder(), rig.athlete->sessionOwner(), Athlete::Source::Library, false);
    const qint64 yesterday = QDateTime(today.addDays(-1), QTime(12, 0)).toMSecsSinceEpoch();
    Spec noInstant = spec(kA, 10);
    noInstant.timestampMs = yesterday;   // the file's mtime, the day before
    noInstant.hasCapture  = false;
    Spec instant = spec(kA, 12);
    instant.timestampMs = yesterday;     // a capture instant the day before
    Session *s = rig.session({ noInstant, instant });
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d) || !d->kept(1) || !d->kept(2)) { check(QStringLiteral("COH2 setup"), false); return; }
    const auto e = s->entry();
    const QDate day = e ? e->identity.localDay() : QDate();
    const QDate dob = QDate::fromString(r.dob, Qt::ISODate);
    const analysis::Cohort onDay  = analysis::cohortFor(dob, r.sex, day);
    const analysis::Cohort onMtime = analysis::cohortFor(dob, r.sex, today.addDays(-1));
    check(QStringLiteral("COH2a the two dates are in different cohorts (55–64 on the session's day, 18–54 the day before)"),
          day == today && onDay != onMtime);
    check(QStringLiteral("COH2b the shot with no capture instant fired for the session day's cohort, not its mtime's"),
          d->kept(1)->cohort == onDay);
    check(QStringLiteral("COH2c the shot whose capture instant is the day before fired for that day's cohort"),
          d->kept(2)->cohort == onMtime);
}

// A label correction or an intent change after a kept result: a fire, the new word kept, a derive in flight superseded.
static void testWordCommands()
{
    std::printf("\nCMD — a correction or an intent change fires again, keeps the new word, supersedes a derive in flight\n");
    Rig rig;
    QVector<Spec> specs{ spec(kA, 10), spec(kA, 12), spec(kA, 14), spec(kA, 16), spec(kA, 18) };
    for (Spec &sp : specs) sp.realReview = true;   // every review is the document's (the commands write it)
    Session *s = rig.session(specs);
    SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
    if (!Rig::settled(d) || !d->kept(2) || !d->kept(2)->derived) { check(QStringLiteral("CMD setup"), false); return; }
    check(QStringLiteral("CMDa kept with no word yet"),
          d->kept(2)->derived->corrections.empty() && d->kept(2)->derived->intent.isEmpty());

    // A derive of shot 2 in flight: the lane is held by shot 1's read; an intent run asks every other shot a derive.
    rig.fake->holdNext(1);
    d->fire(ShotRef(s->id(), 1));
    pump([&] { return rig.fake->blocked(1); });
    s->setIntentRun({ IntentRun{ 9, QStringLiteral("draw") } });   // EVT-08: covers no shot taken, moves the generation
    const size_t from = d->asks().size() - 4;
    bool deriveQueued = false;
    for (size_t i = from; i < d->asks().size(); ++i)
        deriveQueued = deriveQueued || (d->asks()[i].swing == 2 && d->asks()[i].derive);
    check(QStringLiteral("CMDb a derive of shot 2 is waiting in the lane"), deriveQueued && d->outstanding() >= 2);
    const quint64 sup = d->counts().superseded;
    const size_t  mark = d->asks().size();
    Shot *two = s->shot(ShotRef(s->id(), 2));
    LabelCorrectionNote note;
    note.family    = QStringLiteral("direction");
    note.corrected = QStringLiteral("slice");
    const WriteResult w = two ? two->correctLabel({ LabelCorrection{ QStringLiteral("direction"), QString(), QStringLiteral("slice") } }, note)
                              : WriteResult{};
    check(QStringLiteral("CMDc the correction is saved (EVT-06)"), w.ok());
    rig.fake->open(1);
    check(QStringLiteral("CMDd it settles"), Rig::settled(d));
    const AskTally t = asksSince(d, mark);
    std::printf("    %s; superseded +%llu\n", qPrintable(tallyText(t)), (unsigned long long)(d->counts().superseded - sup));
    const KeptShot *k = d->kept(2);
    check(QStringLiteral("CMDe the correction fired shot 2 once and superseded the derive in flight"),
          t.fires.count(2) == 1 && t.fires.at(2) == 1 && d->counts().superseded >= sup + 1);
    check(QStringLiteral("CMDf the new word is in the kept review and the derived outcome"),
          k && k->review && k->review->labelCorrections.size() == 1 && k->derived && k->derived->corrections.size() == 1
              && hasCorrectedLabel(*k->derived, QStringLiteral("slice")));

    const size_t mark2 = d->asks().size();
    const WriteResult wi = two ? two->setShotIntent(QStringLiteral("slice")) : WriteResult{};
    check(QStringLiteral("CMDg the intent change is saved (EVT-07) and settles"), wi.ok() && Rig::settled(d));
    const AskTally t2 = asksSince(d, mark2);
    k = d->kept(2);
    check(QStringLiteral("CMDh it fired shot 2 once; its intent is in the kept review and the derived data"),
          exactlyOnce(t2.fires, { 2 }) && k && k->review && k->review->shotIntent == QLatin1String("slice")
              && k->derived && k->derived->intent == QLatin1String("slice") && k->derived->corrections.size() == 1);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("session_diagnostics_test\n");
    RuleData &rules = installProcessRuleData();
    g_rules = rules.snapshot();
    QTemporaryDir stage;
    const QString swing = stage.path() + QStringLiteral("/live_swings/lm_7iron");
    QDir().mkpath(swing);
    QFile::copy(fixture("lm_7iron"), swing + QStringLiteral("/swing.json"));
    if (g_rules->pack) g_grid = analysis::readLivePhaseGrid(swing, g_rules->pack->pack());
    check(QStringLiteral("the shipped pack and the fixture's grid are read"), g_rules->pack && !g_grid.isEmpty());
    if (g_grid.isEmpty()) return 1;

    testT2();
    testJ2();
    testReview1();
    testSupersededAndTrashed();
    testT1();
    testT3();
    testT4();
    testRealReader();
    testRelease();
    testWordAfterLoad();
    testLegacyGolferInput();
    testDeriveFailure(false);
    testDeriveFailure(true);
    testCohortDate();
    testCohortNoInstant();
    testWordCommands();

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
