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

#include "athlete_diagnostics.h"
#include "session.h"
#include "session_diagnostics.h"

#include "athlete_repository.h"
#include "summary_rules.h"   // firingInputsOf, summaryCohortFacts: the summary fingerprint's inputs

#include "../Coaching/consistency_grade.h"      // the marks' reading and grade (Rules ring; J-34)
#include "../Coaching/shot_outcomes_pack.h"
#include "../Coaching/swing_areas_pack.h"       // areaConditionInfoForPack: a yes/no condition is ungraded
#include "../Coaching/tendencies_pack.h"
#include "../Coaching/variability_measures.h"   // measureScaleType, isOutcomeMeasure, normCorridorHalfWidth (J-52)
#include "../Diagnostics/engine/fire_findings.h"   // contentStamp: the one rule
#include "../Diagnostics/engine/session_reduction.h"   // SessionReductionInput: a session's judged rows (DEC-195)
#include "../Coaching/work_ons_pack.h"
#include "../Diagnostics/packs/swing_themes_pack.h"
#include "../Metrics/variability/variability_constants.h"   // kNWindow
#include "../Core/pp_debug.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMap>
#include <QPointer>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <condition_variable>
#include <iterator>
#include <limits>
#include <mutex>

namespace pinpoint {

using analysis::SessionPlace;

AthleteDiagnostics *AthleteDiagnostics::of(Athlete *athlete)
{
    if (!athlete)
        return nullptr;
    if (auto *d = athlete->findChild<AthleteDiagnostics *>(QString(), Qt::FindDirectChildrenOnly))
        return d;
    return new AthleteDiagnostics(athlete);
}

namespace {

// THE VERSION OF THE CONSISTENCY MARKS' RULE (the window, the filters, the scale): in the marks' fingerprint, so a new rule
// grades again. Moved here with the marks from WorkOnsController (refactor 3.7-f), unchanged.
constexpr int kConsistencyMarksRule = 3;     // 2: every shot in, data-warning shots too (R13), outcomes ungraded (R4);
                                             // 3 (16 Nov 2026, refactor 3.6, Stage 0.5's follow-up): each shot read as
                                             // the launch monitor board annotated it

// The themes' input kind in their fingerprint (refactor 3.7-f): the summaries' per-shot facts, no longer each session's
// ledger, so a swing_themes.json reduced from the ledgers (the board not applied) is worked out again once.
constexpr const char *kThemesInput = "summary-facts:1";

QString hex16(QCryptographicHash &h) { return QString::fromLatin1(h.result().toHex().left(16)); }

// THE REPLICATES' FAN-OUT ON THE ONE POOL (J-53): `n` indices drained by the calling job's own worker and by helpers queued
// on the pool's other lanes, at low priority. The caller never waits for a helper that has not started: once it has drained
// everything it closes the fan-out, and waits only for the helpers already inside `body` (a late one finds nothing to do,
// and touches only the shared state it holds).
} // namespace

AthleteLevel *athleteLevelOfSession(const QList<Athlete *> &athletes, const QString &sessionDir)
{
    if (sessionDir.isEmpty())
        return nullptr;
    const QString parent = QDir::cleanPath(QFileInfo(sessionDir).absolutePath());
    for (Athlete *a : athletes)
        if (a && !a->folder().isEmpty() && QDir::cleanPath(QFileInfo(a->folder()).absoluteFilePath()) == parent)
            return AthleteDiagnostics::of(a);
    return nullptr;
}

analysis::ThemeParallelFor athleteLaneParallelFor(WorkPool *pool)
{
    if (!pool) return {};
    return [pool](int n, const std::function<void(int)> &body) {
        struct FanOut {
            std::atomic<int>                  next{ 0 };
            int                               n = 0;
            const std::function<void(int)>   *body = nullptr;
            std::mutex                        m;
            std::condition_variable           cv;
            int                               active = 0;
            bool                              closed = false;
        };
        auto f = std::make_shared<FanOut>();
        f->n    = n;
        f->body = &body;
        const auto drain = [](FanOut &s) {
            for (int i = s.next.fetch_add(1); i < s.n; i = s.next.fetch_add(1)) (*s.body)(i);
        };
        const int helpers = std::max(0, std::min(n - 1, pool->threadCount() - 1));
        std::vector<quint64> lanes;
        lanes.reserve(size_t(helpers));
        for (int h = 0; h < helpers; ++h) {
            lanes.push_back(nextWorkNumber());
            pool->submit(lanes.back(), [f, drain] {
                {
                    std::lock_guard<std::mutex> lock(f->m);
                    if (f->closed) return;
                    ++f->active;
                }
                drain(*f);
                {
                    std::lock_guard<std::mutex> lock(f->m);
                    --f->active;
                }
                f->cv.notify_all();
            }, WorkPriority::Low);
        }
        drain(*f);
        {
            std::unique_lock<std::mutex> lock(f->m);
            f->closed = true;
            f->cv.wait(lock, [&f] { return f->active == 0; });
        }
        // Each helper's lane let go once the fan-out has closed: a helper not started is dropped unrun, an idle lane is
        // forgotten at once, so no empty lane is left in the pool for good (REVIEW-f R1-m2: the pool keeps a lane until it
        // is cancelled, and scans them all under the mutex the GUI thread's submits take).
        for (const quint64 lane : lanes) pool->cancelLane(lane);
    };
}

namespace {

analysis::ThemeParallelFor poolParallelFor(WorkPool *pool) { return athleteLaneParallelFor(pool); }

// A tool's or a test's inline run (no pool): the process's own thread pool, as athlete_dump always fanned out.
analysis::ThemeParallelFor inlineParallelFor()
{
    QThreadPool *pool = QThreadPool::globalInstance();
    return [pool](int n, const std::function<void(int)> &body) {
        std::atomic<int> next{ 0 };
        const auto drain = [&]() { for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) body(i); };
        const int helpers = std::max(0, std::min(n - 1, pool->maxThreadCount() - 1));
        QSemaphore done;
        for (int h = 0; h < helpers; ++h) pool->start([&]() { drain(); done.release(); });
        drain();
        done.acquire(helpers);
    };
}

// What a job carries back (values only).
struct TendenciesOut { analysis::TendencySet set; QString fingerprint, content; qint64 ms = 0; };
struct ThemesOut     { analysis::SwingThemes themes; QString fingerprint, content; bool layerOne = false; qint64 ms = 0; };
struct SummariesOut  { std::vector<AthleteSessionSummary> read; quint64 gen = 0; };
struct MarksOut      { QHash<QString, variability::VariabilityResult> marks; QString fingerprint, scope; int sessions = 0; int graded = 0;
                       int requests = 0; qint64 ms = 0; };

// One marks request, its pack-made parts decided on the GUI thread.
struct MarksReq {
    QString conditionId, measureId;
    bool    ungraded = false;
    variability::ScaleType scaleType = variability::ScaleType::Interval;
};
struct MarksSession {
    QString name;
    qint64  startMs = 0;
    std::vector<analysis::ThemeShot> shots;
};

} // namespace

AthleteDiagnostics::AthleteDiagnostics(Athlete *athlete)
    : QObject(athlete), m_athlete(athlete), m_instance(nextWorkNumber())
{
    const auto index = [this](const SessionId &) { markStale(); };
    connect(athlete, &Athlete::sessionStarted, this, index, Qt::DirectConnection);   // EVT-17
    connect(athlete, &Athlete::sessionResumed, this, index, Qt::DirectConnection);   // EVT-23
    connect(athlete, &Athlete::sessionTrashed, this, index, Qt::DirectConnection);   // EVT-20
    connect(athlete, &Athlete::recordChanged, this, [this] { markStale(); }, Qt::DirectConnection);   // EVT-14's interim
    connect(athlete, &Athlete::sessionHeld, this, &AthleteDiagnostics::hearSession, Qt::DirectConnection);
    for (Session *s : athlete->loadedSessions())
        hearSession(s);
    m_releaseTimer.setInterval(30'000);
    connect(&m_releaseTimer, &QTimer::timeout, this, &AthleteDiagnostics::retryReleases);
}

AthleteDiagnostics::~AthleteDiagnostics()
{
    // The running jobs hold values only (R-CON-2/3); told to stop, their waiting ones dropped. A result still posted finds
    // no owner at the receiver (OwnerGone).
    for (const auto &c : std::as_const(m_cancel)) if (c) c->store(true);
    if (m_pool) m_pool->cancelLane(m_instance);
}

SessionRepository *AthleteDiagnostics::repository() const
{
    return m_athlete ? m_athlete->m_sessions : nullptr;
}

void AthleteDiagnostics::hearSession(Session *session)
{
    if (!session || m_heard.contains(session->id()))
        return;
    m_heard.insert(session->id());
    const SessionId id = session->id();
    const auto again = [this, id] { markStale(id); };
    connect(session, &Session::sessionEnded, this, again, Qt::DirectConnection);           // EVT-09
    connect(session, &Session::shotTrashed, this, again, Qt::DirectConnection);            // EVT-03
    connect(session, &Session::clubBaselineRewritten, this, again, Qt::DirectConnection);  // EVT-16
    // The summary written (SessionDiagnostics, its one writer: DEC-55) is the session level's result landing.
    if (SessionDiagnostics *d = session->diagnostics()) {
        connect(d, &SessionDiagnostics::written, this, again, Qt::DirectConnection);
        if (d->counts().summaryWrites > 0)
            markStale(id);   // it wrote before this level heard it (a write while it loaded)
    }
    // A released Session leaves the map: a later one of that id is heard afresh.
    connect(session, &QObject::destroyed, this, [this, id] { m_heard.remove(id); });
}

void AthleteDiagnostics::markStale(const SessionId &session)
{
    const bool was = stale();
    if (session.isNull())
        m_indexStale = true;
    else
        m_dirty.insert(session);
    ++m_readGen;
    ++m_counts.staleMarks;
    if (!was)
        emit changed();
}

namespace {

// A session's summary from its file as read (strict: this build's rule), placed. Any thread: a pure rule over values (the
// refusals said in the app log, which any thread may write).
AthleteSessionSummary summaryFrom(const SessionIndexEntry &e, const JsonFileRead &f)
{
    AthleteSessionSummary out;
    out.id     = e.id();
    out.place  = SessionPlace{ e.identity.folderName(), e.identity.localDay(), e.identity.nOfDay() };
    out.status = e.status;
    if (!f.exists)
        return out;
    out.fromNewerBuild = f.ok && analysis::sessionResultsFromNewerBuild(f.root);   // 4-e: said apart from a stale one
    if (!f.ok) {
        // Said in the app log (a summary left out is never silent: J-51), and counted by notSummedUp().
        ppWarn() << "[AthleteDiagnostics] session summary unreadable, left out:" << out.place.folder << "-" << f.error;
        return out;
    }
    bool ok = false;
    analysis::SessionResults r = analysis::sessionResultsFromJson(f.root, analysis::SessionResultsRead::Strict, &ok);
    if (!ok) {
        // An older rule's summary: not this build's answer (the catch-up writes it again). A newer build's: left out.
        ppInfo() << (out.fromNewerBuild ? "[AthleteDiagnostics] session summary of a newer build, left out:"
                                        : "[AthleteDiagnostics] session summary of another rule, left out until it is summed up again:")
                 << out.place.folder;
        return out;
    }
    out.read    = true;
    out.results = std::move(r);
    return out;
}

} // namespace

AthleteSessionSummary AthleteDiagnostics::readSummary(const SessionIndexEntry &e)
{
    // Read on the lane already (readAhead) and not stale since: taken as read.
    if (auto it = m_prefetched.find(e.id()); it != m_prefetched.end()) {
        AthleteSessionSummary out = std::move(*it);
        m_prefetched.erase(it);
        out.status = e.status;
        return out;
    }
    SessionRepository *repo = repository();
    if (!repo)
        return summaryFrom(e, JsonFileRead{ });
    const JsonFileRead f = repo->readWorkOns(e.id());
    ++m_counts.summaryReads;
    return summaryFrom(e, f);
}

// What ensure() would read now: with the index stale, every session it lists that is not held here clean; else the
// dirty ones.
std::vector<SessionIndexEntry> AthleteDiagnostics::entriesToRead() const
{
    std::vector<SessionIndexEntry> out;
    if (m_indexStale) {
        QSet<SessionId> clean;
        for (const AthleteSessionSummary &s : m_summaries)
            if (!m_dirty.contains(s.id)) clean.insert(s.id);
        for (const SessionIndexEntry &e : m_athlete->index())
            if (!e.id().isNull() && !clean.contains(e.id())) out.push_back(e);
    } else {
        for (const SessionIndexEntry &e : m_athlete->index())
            if (m_dirty.contains(e.id())) out.push_back(e);
    }
    return out;
}

SessionId AthleteDiagnostics::sessionIdOf(const QString &folderName)
{
    for (const SessionIndexEntry &e : m_athlete->index())
        if (e.identity.folderName() == folderName) return e.id();
    return {};
}

// THE SUMMARIES READ ON THE LANE (REVIEW-f R1-m9 a; J-57). On the GUI thread: the plan (which sessions, each read resolved
// by the repository into a read any thread may run). On the lane: the reads and the parse. Back on the GUI thread: the
// apply (ensure() takes what was read) and changed(). A staleness meanwhile asks again (latest wins).
AthleteDiagnostics::Ask AthleteDiagnostics::readAhead()
{
    if (!stale())
        return Ask::Current;
    const DiagnosticsDoors &d = m_athlete->diagnosticsDoors();
    SessionRepository *repo = repository();
    if (d.synchronous || !d.pool || !d.receiver || !repo) {
        ensure();   // inline: a tool, a test, an Athlete without the app's doors
        return Ask::Current;
    }
    if (m_jobsPaused)
        return Ask::Paused;
    if (summariesReading() && m_readAskedGen == m_readGen)
        return Ask::Running;
    QElapsedTimer clock;
    clock.start();
    auto plan = std::make_shared<std::vector<std::pair<SessionIndexEntry, std::function<JsonFileRead()>>>>();
    for (const SessionIndexEntry &e : entriesToRead())
        plan->push_back({ e, repo->deferredReadWorkOns(e.id()) });
    if (plan->empty()) {
        ensure();   // nothing to read: the derived results only
        return Ask::Current;
    }
    const quint64 gen = m_readGen;
    m_readAskedGen = gen;
    submit(QStringLiteral("summaries"), [plan, gen]() -> std::any {
        SummariesOut out;
        out.gen = gen;
        out.read.reserve(plan->size());
        for (const auto &[e, read] : *plan) out.read.push_back(summaryFrom(e, read ? read() : JsonFileRead{ }));
        return out;
    });
    m_counts.readAheadAskMs = clock.elapsed();
    return summariesReading() ? Ask::Running : Ask::Current;
}

void AthleteDiagnostics::ensure()
{
    if (!stale())
        return;
    if (m_indexStale) {
        // The index again: keep what was read of a session still there and not dirty; read the rest.
        QHash<SessionId, AthleteSessionSummary> kept;
        for (AthleteSessionSummary &s : m_summaries)
            if (!m_dirty.contains(s.id)) kept.insert(s.id, std::move(s));
        m_summaries.clear();
        for (const SessionIndexEntry &e : m_athlete->index()) {
            if (e.id().isNull())
                continue;
            if (auto it = kept.find(e.id()); it != kept.end()) {
                it->status = e.status;
                m_summaries.push_back(std::move(*it));
            } else {
                m_summaries.push_back(readSummary(e));
            }
        }
    } else {
        for (AthleteSessionSummary &s : m_summaries)
            if (m_dirty.contains(s.id)) {
                const auto e = repository() ? repository()->open(s.id) : std::nullopt;
                if (e) s = readSummary(*e);
            }
    }
    m_indexStale = false;
    m_dirty.clear();
    m_prefetched.clear();   // anything read ahead and not taken was not asked for any more
    std::stable_sort(m_summaries.begin(), m_summaries.end(), [](const AthleteSessionSummary &a, const AthleteSessionSummary &b) {
        return analysis::sessionPlaceBefore(a.place, b.place);
    });

    // The derived results, each a pure rule over what was read.
    std::vector<analysis::SessionWorkOns> sessions;
    QHash<QString, std::vector<analysis::ClubSessionPool>> pools;
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read)
            continue;
        sessions.push_back(s.results.workOns);
        if (!s.place.day.isValid())
            continue;   // nothing can say what comes before or after it (club_distances.h sessionsByDay)
        for (analysis::ClubSessionPool p : s.results.clubWindows) {
            p.folder = s.place.folder;
            p.day    = s.place.day;
            pools[p.club].push_back(std::move(p));
        }
    }
    m_tendenciesCurrent = false;   // computed again when next read

    // J-51: the sessions with shots that add nothing to the tendencies, the themes or the marks — never the one being
    // recorded here (its summary waits for its End).
    m_notSummedUp = {};
    QSet<SessionId> recording;
    for (Session *l : m_athlete->loadedSessions())
        if (l && l->runningHere() && l->inProgress()) recording.insert(l->id());
    for (const AthleteSessionSummary &s : m_summaries) {
        if (s.read && s.results.hasFacts) continue;
        if (recording.contains(s.id)) continue;
        if (!repository() || repository()->shots(s.id).isEmpty()) continue;   // nothing hit: nothing left out
        ++m_notSummedUp.count;
        m_notSummedUp.folders << s.place.folder;
        if (s.fromNewerBuild) m_notSummedUp.newerFolders << s.place.folder;   // 4-e: the rest are the catch-up's
    }
    m_conditions = analysis::aggregateConditions(sessions);
    m_workOns    = analysis::aggregateWorkOns(std::move(sessions));
    m_distances  = analysis::clubDistancesFromPools(pools);
    ++m_counts.recomputes;
}

const std::vector<AthleteSessionSummary> &AthleteDiagnostics::summaries()
{
    ensure();
    return m_summaries;
}

QHash<QString, analysis::SessionWorkOns> AthleteDiagnostics::records()
{
    ensure();
    QHash<QString, analysis::SessionWorkOns> out;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read) out.insert(s.results.workOns.sessionId, s.results.workOns);
    return out;
}

const std::vector<analysis::WorkOnSummary> &AthleteDiagnostics::workOns()
{
    ensure();
    return m_workOns;
}

const std::vector<analysis::WorkOnSummary> &AthleteDiagnostics::conditions()
{
    ensure();
    return m_conditions;
}

const std::vector<analysis::ClubDistanceItem> &AthleteDiagnostics::distances()
{
    ensure();
    return m_distances;
}

std::shared_ptr<const RuleDataSnapshot> AthleteDiagnostics::rules() const
{
    RuleData *rd = m_athlete && m_athlete->diagnosticsDoors().ruleData ? m_athlete->diagnosticsDoors().ruleData : RuleData::installed();
    return rd ? rd->snapshot() : nullptr;
}

// Each summary's compact tendency facts (DEC-192 as Mark amended it, "b"), placed. A summary without them (an older
// file) adds nothing.
std::vector<analysis::TendencySessionShots> AthleteDiagnostics::tendencySessions()
{
    ensure();
    std::vector<analysis::TendencySessionShots> out;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read && s.results.hasFacts)
            out.push_back(analysis::TendencySessionShots{ s.place, s.results.tendencyShots });
    return out;
}

const analysis::TendencySet &AthleteDiagnostics::tendencies()
{
    ensure();
    if (m_tendenciesCurrent)
        return m_tendencies;
    m_tendencies = {};
    const auto snap = rules();
    if (snap && snap->pack && snap->norms) {
        const analysis::ShotOutcomePack outcomes = analysis::shotOutcomePackFor(&snap->pack->pack());
        const analysis::ContextTree &tree = snap->norms->contexts();
        m_tendencies = analysis::reduceTendencyShots(tendencySessions(), outcomes,
                                                     [&tree](const QString &club) { return analysis::tendencyClubGroup(tree, club); });
        // Its distances: the summaries' club windows (D7), the same rows the ledger path pools (checked equal: run P).
        QJsonArray distances;
        for (const analysis::ClubDistanceItem &it : m_distances) distances.append(analysis::clubDistanceItemToJson(it));
        m_tendencies.clubDistances = distances;
    }
    m_tendenciesCurrent = true;
    return m_tendencies;
}

std::vector<analysis::ThemeSessionInput> AthleteDiagnostics::themeInputs()
{
    ensure();
    std::vector<analysis::ThemeSessionInput> out;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read && s.results.hasFacts) {
            analysis::ThemeSessionInput in;
            in.name  = s.place.folder;
            in.facts = s.results.themeShots;
            out.push_back(std::move(in));
        }
    std::stable_sort(out.begin(), out.end(), [](const analysis::ThemeSessionInput &a, const analysis::ThemeSessionInput &b) {
        return a.name < b.name;   // the reduction takes sessions in name order (swing_themes.h)
    });
    return out;
}

analysis::SwingThemes AthleteDiagnostics::themes(const analysis::ThemeParallelFor &parallel, const std::atomic<bool> *cancel)
{
    const auto snap = rules();
    if (!snap || !snap->pack)
        return {};
    const analysis::CharacteristicPack &pack = snap->pack->pack();
    const analysis::DrillSet drills = snap->drills ? snap->drills->merged : analysis::DrillSet{};
    return analysis::reduceSwingThemes(themeInputs(), analysis::themeMeasureInfo(pack), analysis::themeConditionInfo(pack, drills),
                                       analysis::ThemeOptions{}, parallel, cancel);
}

// A predecessor's summary fingerprint as of now: the session level's own rule (SessionDiagnostics::summaryFingerprintNow),
// handed the rule data's summary stamp (once per RuleData snapshot: it hashes the pack and norms) and the cohort facts.
QString AthleteDiagnostics::summaryFingerprintNow(const SessionIndexEntry &e)
{
    SessionRepository *repo = repository();
    if (!repo || !m_athlete)
        return {};
    const auto snap = rules();
    if (snap != m_summaryRulesOf) {
        m_summaryRulesOf = snap;
        m_summaryRules   = snap ? analysis::summaryRulesStamp(firingInputsOf(*snap)) : QString();
    }
    const AthleteRecord &rec = m_athlete->record();
    return SessionDiagnostics::summaryFingerprintNow(*repo, m_athlete->diagnosticsDoors().reader.get(), e.id(), m_summaryRules,
                                                     summaryCohortFacts(rec.dob, rec.sex));
}

ClubBaselineComputation AthleteDiagnostics::clubBaselineFor(const SessionId &session)
{
    ClubBaselineComputation out;
    SessionRepository *repo = repository();
    const auto me = repo ? repo->open(session) : std::nullopt;
    if (!me) {
        out.why = QStringLiteral("the session is not this athlete's");
        return out;
    }
    out.day = me->identity.localDay();
    if (!out.day.isValid()) {
        // A session with no day has no predecessors: a snapshot now would be empty for good (the old model's rule).
        out.why = QStringLiteral("the session has no day");
        return out;
    }
    // Strictly earlier days, newest first by day and place in the day (SessionIdentity's), never a folder's name.
    std::vector<SessionIndexEntry> earlier;
    for (const SessionIndexEntry &e : m_athlete->index())
        if (e.id() != session && e.identity.localDay().isValid() && e.identity.localDay() < out.day)
            earlier.push_back(e);
    std::sort(earlier.begin(), earlier.end(), [](const SessionIndexEntry &a, const SessionIndexEntry &b) {
        return analysis::sessionPlaceBefore(
            SessionPlace{ b.identity.folderName(), b.identity.localDay(), b.identity.nOfDay() },
            SessionPlace{ a.identity.folderName(), a.identity.localDay(), a.identity.nOfDay() });
    });

    std::vector<analysis::SummaryClubWindows> summaries;
    QStringList notCurrent;
    int taken = 0;
    for (const SessionIndexEntry &e : earlier) {
        if (taken >= analysis::kClubDistanceWindowSessions)
            break;
        const bool hasSwings = !repo->shots(e.id()).isEmpty();
        const AthleteSessionSummary s = readSummary(e);
        // FRESH, not merely readable (BRIEF-f 13; REVIEW-e R2-m4, REVIEW-f R1-B1): a rule-data, cohort, baseline or swing
        // change with no version bump leaves a summary strict-readable but stale; a baseline frozen from its windows
        // would be frozen from the old answer (DEC-46: frozen once).
        const bool fresh = s.read && s.results.hasGrowth && !s.results.derivedFrom.fingerprint.isEmpty()
                        && s.results.derivedFrom.fingerprint == summaryFingerprintNow(e);
        if (!fresh) {
            // A session played and not summed up as it stands now (no summary, an older rule's, one without its windows,
            // or one whose fingerprint has moved): the baseline is not computable until it is (the catch-up writes it
            // first, J-49).
            if (hasSwings) {
                ++taken;
                notCurrent << s.place.folder;
                out.waitingOn << e.id();
            }
            continue;
        }
        ++taken;
        out.read << s.place.folder;
        summaries.push_back(analysis::SummaryClubWindows{ s.place, s.results.clubWindows });
    }
    if (!notCurrent.isEmpty()) {
        out.why = QStringLiteral("earlier sessions not summed up yet: ") + notCurrent.join(QStringLiteral(", "));
        return out;
    }
    out.clubs      = analysis::clubBaselineFromSummaries(summaries, out.day);
    out.computable = true;
    return out;
}

// ── The report's as-of read, the sessions it cannot use, a re-read (refactor 3.7-f) ───────────────────────────────────

analysis::TendencySet AthleteDiagnostics::tendenciesAsOf(const SessionId &session)
{
    ensure();
    analysis::TendencySet out;
    SessionPlace asOf;
    bool found = false;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.id == session) { asOf = s.place; found = true; break; }
    const auto snap = rules();
    if (!found || !snap || !snap->pack || !snap->norms)
        return out;
    std::vector<analysis::TendencySessionShots> in;
    QHash<QString, std::vector<analysis::ClubSessionPool>> pools;
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read || !analysis::tendencySessionCounts(s.place, asOf))
            continue;
        if (s.results.hasFacts)
            in.push_back(analysis::TendencySessionShots{ s.place, s.results.tendencyShots });
        if (!s.place.day.isValid())
            continue;
        for (analysis::ClubSessionPool p : s.results.clubWindows) {
            p.folder = s.place.folder;
            p.day    = s.place.day;
            pools[p.club].push_back(std::move(p));
        }
    }
    const analysis::ShotOutcomePack outcomes = analysis::shotOutcomePackFor(&snap->pack->pack());
    const analysis::ContextTree &tree = snap->norms->contexts();
    out = analysis::reduceTendencyShots(std::move(in), outcomes,
                                        [&tree](const QString &club) { return analysis::tendencyClubGroup(tree, club); });
    QJsonArray distances;
    for (const analysis::ClubDistanceItem &it : analysis::clubDistancesFromPools(pools)) distances.append(analysis::clubDistanceItemToJson(it));
    out.clubDistances = distances;
    return out;
}

AthleteDiagnostics::NotSummedUp AthleteDiagnostics::notSummedUp()
{
    ensure();
    return m_notSummedUp;
}

const AthleteSessionSummary *AthleteDiagnostics::summaryOf(const QString &folderName)
{
    ensure();
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.place.folder == folderName) return s.read ? &s : nullptr;
    return nullptr;
}

void AthleteDiagnostics::rereadAll()
{
    const bool was = stale();
    for (const AthleteSessionSummary &s : m_summaries) m_dirty.insert(s.id);
    m_indexStale = true;
    ++m_readGen;
    ++m_counts.staleMarks;
    if (!was)
        emit changed();
}

// ── Home's jobs (J-53) ─────────────────────────────────────────────────────────────────────────────────────────────

QList<QMetaObject::Connection> AthleteDiagnostics::watch(QObject *context, Watcher watcher)
{
    QList<QMetaObject::Connection> out;
    auto w = std::make_shared<Watcher>(std::move(watcher));
    out << connect(this, &AthleteDiagnostics::changed, context, [w] { (*w)(Event::Changed, {}, {}); });
    out << connect(this, &AthleteDiagnostics::tendenciesLanded, context, [w] { (*w)(Event::TendenciesLanded, {}, {}); });
    out << connect(this, &AthleteDiagnostics::themesLayerOneLanded, context, [w] { (*w)(Event::ThemesLayerOneLanded, {}, {}); });
    out << connect(this, &AthleteDiagnostics::themesLanded, context, [w] { (*w)(Event::ThemesLanded, {}, {}); });
    out << connect(this, &AthleteDiagnostics::consistencyLanded, context, [w] { (*w)(Event::ConsistencyLanded, {}, {}); });
    out << connect(this, &AthleteDiagnostics::scopedWorking, context, [w] { (*w)(Event::ScopedWorking, {}, {}); });
    out << connect(this, &AthleteDiagnostics::scopedLayerOneLanded, context, [w] { (*w)(Event::ScopedLayerOneLanded, {}, {}); });
    out << connect(this, &AthleteDiagnostics::scopedLanded, context, [w] { (*w)(Event::ScopedLanded, {}, {}); });
    out << connect(this, &AthleteDiagnostics::writeFailed, context,
                   [w](const QString &what, const QString &why) { (*w)(Event::WriteFailed, what, why); });
    return out;
}

// The content the summaries are graded against, by the Rules ring's one rule (fire_findings.h contentStamp): once per
// RuleData snapshot (it hashes the whole pack and norms).
QString AthleteDiagnostics::contentStampNow()
{
    const auto snap = rules();
    if (snap != m_stampOf) {
        m_stampOf      = snap;
        m_contentStamp = snap ? analysis::contentStamp(snap->pack.get(), snap->norms.get()) : QString();
    }
    return m_contentStamp;
}

// The themes' inputs: the rule and the schema they are reduced and stored under, the content, which drills exist (the
// focus groups faults by drill), the input kind, and each summary that gives facts (its folder name and fingerprint), in
// name order — the reduction's own order. A summary rewritten with the same fingerprint moves nothing here (as before).
QString AthleteDiagnostics::themesFingerprint()
{
    ensure();
    const auto snap = rules();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(analysis::kThemeRuleVersion));
    h.addData(QByteArray::number(analysis::kThemeSchemaVersion));
    h.addData(contentStampNow().toUtf8());
    QStringList drillIds;
    if (snap && snap->drills)
        for (const analysis::Drill &d : snap->drills->merged.drills) drillIds.append(d.id);
    drillIds.sort();
    h.addData(drillIds.join(QLatin1Char('\x1f')).toUtf8());
    h.addData(QByteArrayLiteral("\x1d"));
    h.addData(QByteArray(kThemesInput));
    h.addData(QByteArrayLiteral("\x1d"));
    std::vector<const AthleteSessionSummary *> by;
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read && s.results.hasFacts) by.push_back(&s);
    std::stable_sort(by.begin(), by.end(), [](const AthleteSessionSummary *a, const AthleteSessionSummary *b) {
        return a->place.folder < b->place.folder;
    });
    for (const AthleteSessionSummary *s : by) {
        h.addData(s->place.folder.toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(s->results.derivedFrom.fingerprint.toUtf8());
        h.addData(QByteArrayLiteral("\x1e"));
    }
    return hex16(h);
}

// The tendencies' inputs: the rule, the schema, the content, and every summary read (its folder, fingerprint and when it
// was written — a rewrite with the same fingerprint still re-reduces, as a ledger rewrite did), oldest first.
QString AthleteDiagnostics::tendenciesFingerprint()
{
    ensure();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(analysis::kTendencyRuleVersion));
    h.addData(QByteArray::number(analysis::kTendencySchemaVersion));
    h.addData(contentStampNow().toUtf8());
    h.addData(QByteArrayLiteral("\x1d"));
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read) continue;
        h.addData(s.place.folder.toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(s.results.derivedFrom.fingerprint.toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(QByteArray::number(qint64(s.results.derivedFrom.writtenAtMs)));
        h.addData(QByteArrayLiteral("\x1e"));
    }
    return hex16(h);
}

void AthleteDiagnostics::setJobsPaused(bool on)
{
    if (m_jobsPaused == on) return;
    m_jobsPaused = on;
    if (on) cancelJobs();
}

void AthleteDiagnostics::cancelJobs()
{
    for (const auto &c : std::as_const(m_cancel)) if (c) c->store(true);
    m_cancel.clear();
    // Every ask still out is now behind its item's latest (a fresh number): dropped as superseded at the receiver.
    for (auto it = m_latest.begin(); it != m_latest.end(); ++it) it.value() = nextWorkNumber();
    if (m_pool) m_pool->cancelLane(m_instance);
    m_running.clear();
    // The row pass (DEC-195) let go: the session in hand dropped, and released when the pass loaded it.
    m_rowPassActive = false;
    m_rowQueue.clear();
    if (Session *held = m_rowSession.data()) {
        const SessionId id = m_rowSessionId;
        const bool here = m_rowLoadedHere;
        endRowWait();
        held->dropUse();
        if (here) letGo(id);
    } else {
        endRowWait();
    }
}

// A new ask's stop flag for `item`, the superseded job's told to stop first: its result is dropped by ask number anyway,
// but on the owner's serial lane it would hold the newest job back until it ran to the end (REVIEW-f R1-m1).
std::shared_ptr<std::atomic<bool>> AthleteDiagnostics::freshCancel(const QString &item)
{
    if (const auto old = m_cancel.value(item)) old->store(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_cancel.insert(item, cancel);
    return cancel;
}

// One item's job let go: told to stop, its result dropped (a fresh latest number), not running.
void AthleteDiagnostics::dropItem(const QString &item)
{
    if (const auto c = m_cancel.take(item)) c->store(true);
    if (m_latest.contains(item)) m_latest.insert(item, nextWorkNumber());
    m_running.insert(item, false);
}

void AthleteDiagnostics::submit(const QString &item, std::function<std::any()> work)
{
    const quint64 ask = nextWorkNumber();
    m_latest.insert(item, ask);
    m_running.insert(item, true);
    ++m_counts.jobsAsked;
    WorkAddress a;
    a.owner    = athleteWorkOwner(m_athlete->id());
    a.instance = m_instance;
    a.item     = item;
    a.ask      = ask;
    const DiagnosticsDoors &d = m_athlete->diagnosticsDoors();
    if (d.synchronous || !d.pool || !d.receiver) {
        // Inline (a tool, a test, an Athlete without the app's doors): the same job, then the same hand-off.
        WorkResult r;
        r.address = a;
        try {
            r.value = work();
        } catch (const std::exception &e) {
            r.error = QString::fromUtf8(e.what());
        } catch (...) {
            r.error = QStringLiteral("the job failed");
        }
        deliver(WorkVerdict::Kept, std::move(r));
        return;
    }
    m_pool = d.pool;
    if (!postToOwner(*d.pool, *d.receiver, m_instance, a, std::move(work), WorkPriority::Low)) {
        ppWarn() << "[AthleteDiagnostics] the pool refused the" << item << "job";
        m_running.insert(item, false);
    }
}

AthleteDiagnostics::Ask AthleteDiagnostics::askTendencies()
{
    if (m_jobsPaused) return Ask::Paused;
    const QString fp = tendenciesFingerprint();
    if ((m_tendenciesReady && fp == m_tendenciesFp && !tendenciesRunning()) || (tendenciesRunning() && fp == m_tendenciesAsked))
        return Ask::Current;
    const auto snap = rules();
    if (!snap || !snap->pack || !snap->norms) return Ask::Current;
    // Values only (R-CON-2): the facts, the outcome pack and the norms' snapshot (its context tree), the distances.
    const auto cancel = freshCancel(QStringLiteral("tendencies"));
    m_tendenciesAsked = fp;
    std::vector<analysis::TendencySessionShots> in = tendencySessions();
    QJsonArray distances;
    for (const analysis::ClubDistanceItem &it : m_distances) distances.append(analysis::clubDistanceItemToJson(it));
    const std::shared_ptr<const analysis::INormProvider> norms = snap->norms;
    const QString content = contentStampNow();
    auto outcomes = std::make_shared<const analysis::ShotOutcomePack>(analysis::shotOutcomePackFor(&snap->pack->pack()));
    submit(QStringLiteral("tendencies"), [in = std::move(in), distances, norms, outcomes, fp, content, cancel]() mutable -> std::any {
        QElapsedTimer clock;
        clock.start();
        const analysis::ContextTree &tree = norms->contexts();
        TendenciesOut out;
        out.set = analysis::reduceTendencyShots(std::move(in), *outcomes,
                                                [&tree](const QString &club) { return analysis::tendencyClubGroup(tree, club); },
                                                {}, cancel.get());
        out.set.clubDistances = distances;
        out.fingerprint = fp;
        out.content     = content;
        out.ms          = clock.elapsed();
        return out;
    });
    return tendenciesRunning() ? Ask::Running : Ask::Landed;
}

AthleteDiagnostics::Ask AthleteDiagnostics::askThemes(bool layerOneFirst)
{
    if (m_jobsPaused) return Ask::Paused;
    const QString fp = themesFingerprint();
    if ((m_themesReady && fp == m_themesFp && !themesRunning()) || (themesRunning() && fp == m_themesAsked))
        return Ask::Current;
    const auto snap = rules();
    if (!snap || !snap->pack) return Ask::Current;

    // THE STORED SUMMARY, when it was reduced from exactly this: the 500 replicates skipped.
    if (AthleteRepository *repo = m_athlete->m_repository) {
        const JsonFileRead f = repo->readCache(*m_athlete, AthleteRepository::Cache::Themes);
        if (f.exists && f.ok) {
            bool ok = false;
            analysis::SwingThemes st = analysis::swingThemesFromJson(f.root, &ok);
            const QString stored = f.root.value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("fingerprint")).toString();
            if (ok && !st.cancelled && stored == fp) {
                dropItem(QStringLiteral("themes1"));
                dropItem(QStringLiteral("themes"));
                m_landedThemes    = std::move(st);
                m_themesFp        = fp;
                m_themesWrittenFp = fp;
                m_themesReady     = true;
                ++m_counts.themeCacheReads;
                emit themesLanded();
                return Ask::Landed;
            }
        }
    }

    const analysis::CharacteristicPack &pack = snap->pack->pack();
    const analysis::DrillSet drills = snap->drills ? snap->drills->merged : analysis::DrillSet{};
    auto in         = std::make_shared<const std::vector<analysis::ThemeSessionInput>>(themeInputs());
    auto measures   = std::make_shared<const QHash<QString, analysis::ThemeMeasureInfo>>(analysis::themeMeasureInfo(pack));
    auto conditions = std::make_shared<const QHash<QString, analysis::ThemeConditionInfo>>(analysis::themeConditionInfo(pack, drills));
    const QString content = contentStampNow();
    const auto cancel = freshCancel(QStringLiteral("themes"));
    m_themesAsked = fp;
    if (layerOneFirst)
        submit(QStringLiteral("themes1"), [in, conditions, fp, content]() -> std::any {
            ThemesOut out;
            out.themes      = analysis::reduceSwingSummary(*in, *conditions, analysis::ThemeOptions{});
            out.fingerprint = fp;
            out.content     = content;
            out.layerOne    = true;
            return out;
        });
    const DiagnosticsDoors &d = m_athlete->diagnosticsDoors();
    const bool inline_ = d.synchronous || !d.pool || !d.receiver;
    const analysis::ThemeParallelFor parallel = inline_ ? inlineParallelFor() : poolParallelFor(d.pool);
    submit(QStringLiteral("themes"), [in, measures, conditions, fp, content, cancel, parallel]() -> std::any {
        QElapsedTimer clock;
        clock.start();
        ThemesOut out;
        out.themes      = analysis::reduceSwingThemes(*in, *measures, *conditions, analysis::ThemeOptions{}, parallel, cancel.get());
        out.fingerprint = fp;
        out.content     = content;
        out.ms          = clock.elapsed();
        return out;
    });
    return themesRunning() ? Ask::Running : Ask::Landed;
}

AthleteDiagnostics::Ask AthleteDiagnostics::askConsistency(const analysis::ClubScope &scope, const std::vector<ConsistencyRequest> &requests)
{
    if (m_jobsPaused) return Ask::Paused;
    ensure();
    const auto snap = rules();
    if (!snap || !snap->pack || !snap->norms) return Ask::Current;
    const analysis::CharacteristicPack &pack = snap->pack->pack();

    // What the pack says before a reading is read: no measure, a yes/no condition, a composite measure, an outcome (R4);
    // the measure's scale. In key order, so the fingerprint does not depend on the caller's.
    const QHash<QString, analysis::AreaConditionInfo> info = analysis::areaConditionInfoForPack(pack);
    QMap<QString, MarksReq> byKey;
    for (const ConsistencyRequest &r : requests) {
        MarksReq m;
        m.conditionId = r.conditionId;
        m.measureId   = r.measureId;
        const analysis::Measure *ms = pack.measure(r.measureId);
        m.ungraded  = r.measureId.isEmpty() || info.value(r.conditionId).binary
                   || (ms && ms->kind == analysis::MeasureKind::Composite) || variability::isOutcomeMeasure(ms);
        m.scaleType = variability::measureScaleType(ms);
        byKey.insert(consistencyKey(r.conditionId, r.measureId), m);
    }
    // The sessions newest first by when each was played, then by name (the aggregate's order).
    auto sessions = std::make_shared<std::vector<MarksSession>>();
    for (const AthleteSessionSummary &s : m_summaries)
        if (s.read && s.results.hasFacts)
            sessions->push_back(MarksSession{ s.place.folder, s.results.workOns.startMs, s.results.themeShots });
    std::sort(sessions->begin(), sessions->end(), [](const MarksSession &a, const MarksSession &b) {
        return a.startMs != b.startMs ? a.startMs > b.startMs : a.name > b.name;
    });

    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(kConsistencyMarksRule));
    h.addData(contentStampNow().toUtf8());
    h.addData(scope.key.toUtf8());
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it) {
        h.addData(it.key().toUtf8());
        h.addData(QByteArray::number(int(it->ungraded)));
        h.addData(QByteArray::number(int(it->scaleType)));
        h.addData(QByteArrayLiteral("\x1e"));
    }
    h.addData(QByteArrayLiteral("\x1d"));
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read || !s.results.hasFacts) continue;
        h.addData(s.place.folder.toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(s.results.derivedFrom.fingerprint.toUtf8());
        h.addData(QByteArray::number(qint64(s.results.derivedFrom.writtenAtMs)));
        h.addData(QByteArray::number(s.results.workOns.startMs));
        h.addData(QByteArrayLiteral("\x1e"));
    }
    const QString fp = hex16(h);
    if (fp == m_consistencyFp && !consistencyRunning()) return Ask::Current;
    if (consistencyRunning() && fp == m_consistencyAsked) return Ask::Running;   // the same ask in flight: not queued twice (R1-m1)

    auto reqs = std::make_shared<const std::vector<MarksReq>>(byKey.cbegin(), byKey.cend());
    const std::shared_ptr<const analysis::INormProvider> norms = snap->norms;
    const auto cancel = freshCancel(QStringLiteral("marks"));
    m_consistencyAsked = fp;
    submit(QStringLiteral("marks"), [sessions, reqs, norms, scope, fp, cancel]() -> std::any {
        QElapsedTimer clock;
        clock.start();
        MarksOut out;
        out.fingerprint = fp;
        out.scope       = scope.key;
        out.requests    = int(reqs->size());
        // ── Each request's window: its newest kNWindow readings across sessions (the shared rule's readings) ──
        struct Reading {
            qint64 ms = 0;          // the shot's clock, else its session's start
            int    session = 0;     // index into sessions: lower is newer
            int    shotId = 0;
            variability::LedgerConsistencyReading reading;
        };
        const auto newer = [](const Reading &a, const Reading &b) {
            if (a.ms != b.ms) return a.ms > b.ms;
            if (a.session != b.session) return a.session < b.session;
            return a.shotId > b.shotId;
        };
        QHash<QString, std::vector<int>> byCondition;
        for (int i = 0; i < int(reqs->size()); ++i)
            if (!(*reqs)[size_t(i)].ungraded) byCondition[(*reqs)[size_t(i)].conditionId].push_back(i);
        std::vector<std::vector<Reading>> windows(reqs->size());
        for (int i = 0; i < int(sessions->size()) && !byCondition.isEmpty(); ++i) {
            if (cancel->load()) return out;
            const MarksSession &s = (*sessions)[size_t(i)];
            ++out.sessions;
            for (const analysis::ThemeShot &shot : s.shots) {
                if (!scope.contains(canonicalClubId(shot.club))) continue;
                const qint64 ms = shot.timestampMs > 0 ? shot.timestampMs : s.startMs;
                for (const analysis::ThemeRow &row : shot.rows) {
                    const auto ids = byCondition.constFind(row.conditionId);
                    if (ids == byCondition.constEnd()) continue;
                    for (int r : *ids) {
                        // The rule's reading (consistency_grade.h ledgerConsistencyReading) of a fact: an assessed row whose
                        // driving measure is the one graded, with a finite value.
                        if ((*reqs)[size_t(r)].measureId.isEmpty() || row.measureId != (*reqs)[size_t(r)].measureId) continue;
                        if (!std::isfinite(row.value)) continue;
                        variability::LedgerConsistencyReading x;
                        x.club      = shot.club;
                        x.contextId = row.contextId;
                        x.value     = row.value;
                        x.noBand    = row.noBand;
                        windows[size_t(r)].push_back(Reading{ ms, i, shot.shotId, std::move(x) });
                    }
                }
            }
        }
        for (std::vector<Reading> &w : windows) {
            std::sort(w.begin(), w.end(), newer);
            if (int(w.size()) > variability::kNWindow) w.resize(size_t(variability::kNWindow));
        }
        // ── Grade each window: the shared rule ──
        for (int r = 0; r < int(reqs->size()); ++r) {
            if (cancel->load()) return out;
            const MarksReq &req = (*reqs)[size_t(r)];
            std::vector<variability::LedgerConsistencyReading> readings;
            for (auto it = windows[size_t(r)].rbegin(); it != windows[size_t(r)].rend(); ++it) readings.push_back(it->reading);   // strike order
            const analysis::INormProvider *np = norms.get();
            const QString measureId = req.measureId;
            const std::optional<variability::VariabilityResult> result = variability::ledgerConsistencyGrade(
                readings, req.ungraded, req.scaleType,
                [np, measureId](const QString &context) { return variability::normCorridorHalfWidth(np, measureId, context); });
            if (result) {
                out.marks.insert(consistencyKey(req.conditionId, req.measureId), *result);
                ++out.graded;
            }
        }
        out.ms = clock.elapsed();
        return out;
    });
    return consistencyRunning() ? Ask::Running : Ask::Landed;
}

void AthleteDiagnostics::deliver(WorkVerdict verdict, WorkResult &&result)
{
    const QString item = result.address.item;
    if (verdict != WorkVerdict::Kept || result.address.ask != m_latest.value(item))
        return;   // superseded, cancelled or paused meanwhile: dropped (the newer ask, if any, still runs)
    // "themes1" (layer one) leaves the full job running; every other item is done.
    if (item != QLatin1String("themes1") && item != QLatin1String("scoped1")) {
        m_running.insert(item, false);
        m_cancel.remove(item);
    }
    if (!result.error.isEmpty()) {
        ppWarn() << "[AthleteDiagnostics] the" << item << "job failed:" << result.error;
        if (item == QLatin1String("summaries")) { ensure(); emit changed(); return; }   // read here instead
        if (item == QLatin1String("tendencies")) emit tendenciesLanded();
        else if (item == QLatin1String("themes")) emit themesLanded();
        else if (item == QLatin1String("marks")) emit consistencyLanded();
        else if (item == QLatin1String("scoped")) emit scopedLanded();
        return;
    }
    if (item == QLatin1String("summaries")) {
        SummariesOut out = std::any_cast<SummariesOut>(std::move(result.value));
        if (!stale()) {
            // Read synchronously meanwhile (a reader that did not wait: "Your distances", the report): nothing to take, but
            // the readers waiting for this landing (Home) are told, or they would keep the figures from before (R2-m1).
            emit changed();
            return;
        }
        if (out.gen != m_readGen) {
            readAhead();   // stale again since it was asked: latest wins
            return;
        }
        for (AthleteSessionSummary &sum : out.read) m_prefetched.insert(sum.id, std::move(sum));
        m_counts.summaryReads      += quint64(out.read.size());
        m_counts.summariesReadAhead += quint64(out.read.size());
        QElapsedTimer clock;
        clock.start();
        ensure();
        m_counts.readAheadApplyMs = clock.elapsed();
        emit changed();   // read again: every figure is current now
        return;
    }
    if (item == QLatin1String("tendencies")) {
        TendenciesOut out = std::any_cast<TendenciesOut>(std::move(result.value));
        if (out.set.cancelled) return;
        ++m_counts.tendencyJobs;
        m_landedTendencies = std::move(out.set);
        m_tendenciesFp     = out.fingerprint;
        m_tendenciesContent = out.content;
        m_tendenciesReady  = true;
        ppInfo() << qPrintable(QStringLiteral("[Tendencies] %1 shots over %2 sessions, %3 keys, %4 ms")
                                   .arg(m_landedTendencies.countedShots).arg(m_landedTendencies.countedSessions)
                                   .arg(m_landedTendencies.items.size()).arg(out.ms));
        const WriteResult w = writeTendenciesCache();
        if (!w.ok()) emit writeFailed(QStringLiteral("The athlete's tendencies"), w.error);
        emit tendenciesLanded();   // after the write (DUTIES-f T7: a reader of the file reads it on this cue)
    } else if (item == QLatin1String("themes1")) {
        ThemesOut out = std::any_cast<ThemesOut>(std::move(result.value));
        m_layerOne = std::move(out.themes);
        emit themesLayerOneLanded();
    } else if (item == QLatin1String("themes")) {
        ThemesOut out = std::any_cast<ThemesOut>(std::move(result.value));
        if (out.themes.cancelled) return;
        ++m_counts.themeJobs;
        m_landedThemes  = std::move(out.themes);
        m_themesFp      = out.fingerprint;
        m_themesContent = out.content;
        m_themesReady   = true;
        int firm = 0, probably = 0, possibly = 0, none = 0;
        for (const analysis::SwingTheme &t : m_landedThemes.themes) {
            switch (t.tier) {
            case analysis::ThemeTier::Firm:     ++firm;     break;
            case analysis::ThemeTier::Probably: ++probably; break;
            case analysis::ThemeTier::Possibly: ++possibly; break;
            case analysis::ThemeTier::None:     ++none;     break;
            }
        }
        ppInfo() << qPrintable(QStringLiteral("[SwingThemes] %1 swings over %2 sessions, k=%3, themes %4 firm / "
                                              "%5 probably / %6 possibly / %7 not shown%8 — %9 ms")
                                   .arg(m_landedThemes.swings).arg(m_landedThemes.sessions).arg(m_landedThemes.k)
                                   .arg(firm).arg(probably).arg(possibly).arg(none)
                                   .arg(m_landedThemes.enough ? QString() : QStringLiteral(" (not enough yet)"))
                                   .arg(out.ms));
        const WriteResult w = writeThemesCache();
        if (!w.ok()) emit writeFailed(QStringLiteral("The swing summary"), w.error);
        emit themesLanded();
    } else if (item == QLatin1String("scoped1")) {
        ScopedResult out = std::any_cast<ScopedResult>(std::move(result.value));
        const QString pending = m_pendingFilter.isAll() ? m_pendingScope.key : m_pendingScope.key + QLatin1Char('|') + m_pendingFilter.key();
        if (out.key != pending) return;
        m_scopedLayerOne = std::move(out);
        emit scopedLayerOneLanded();
    } else if (item == QLatin1String("scoped")) {
        ScopedResult out = std::any_cast<ScopedResult>(std::move(result.value));
        if (out.themes.cancelled) return;
        ++m_counts.themeJobs;
        m_scoped       = std::move(out);
        m_scopedFp     = m_scopedAsked;
        m_scopedScope  = m_pendingScope;
        m_scopedFilter = m_pendingFilter;
        m_scopedReady  = true;
        const WriteResult w = writeScopedCache(m_scopedScope, m_scopedFilter);
        if (!w.ok()) emit writeFailed(QStringLiteral("The swing summary"), w.error);
        emit scopedLanded();
    } else if (item == QLatin1String("marks")) {
        MarksOut out = std::any_cast<MarksOut>(std::move(result.value));
        ++m_counts.consistencyJobs;
        m_counts.consistencySessions += quint64(out.sessions);
        m_consistency      = std::move(out.marks);
        m_consistencyFp    = out.fingerprint;
        m_consistencyScope = out.scope;
        ppInfo() << qPrintable(QStringLiteral("[Consistency] marks: %1 faults, %2 graded, %3 sessions' facts read, %4 ms")
                                   .arg(out.requests).arg(out.graded).arg(out.sessions).arg(out.ms));
        emit consistencyLanded();
    }
}

// ── The scoped views (refactor 3.7-f; DEC-195) ──────────────────────────────────────────────────────────────────────

namespace {

// One session's record under a club scope or a shot filter, from its chosen shots (moved from WorkOnsController with the
// scoped job). `stored` is the session's whole record: its clock stands in when none of these shots carried one.
analysis::SessionWorkOns scopedSessionRecord(const QString &name, const std::vector<analysis::ShotRecord> &shots,
                                             const analysis::LedgerOptions &opt, const analysis::CharacteristicPack &pack,
                                             const analysis::SessionWorkOns &stored)
{
    analysis::SessionWorkOns rec = analysis::sessionWorkOnsForPack(&pack, shots, analysis::conditionLedgers(shots, opt));
    rec.sessionId = name;
    for (analysis::WorkOnEntry &e : rec.entries) {
        const analysis::Condition *c = pack.condition(e.id);
        e.name = c && !c->label.isEmpty() ? c->label : e.id;
    }
    if (rec.startMs <= 0) {
        rec.startMs = stored.startMs;
        rec.endMs   = stored.endMs;
    }
    return rec;
}

// The stored block of a filter's file (swing_themes.<club>.<filter>.json): the key, the rule, the line's counts and each
// fault's [a, n1, b, n0] — the shape WorkOnsController wrote, unchanged.
QJsonObject outcomeToJson(const AthleteLevel::ScopedResult &r, const analysis::ShotFilter &filter)
{
    QJsonObject contrasts;
    for (auto it = r.contrasts.constBegin(); it != r.contrasts.constEnd(); ++it)
        contrasts.insert(it.key(), QJsonArray{ it->a, it->n1, it->b, it->n0 });
    return QJsonObject{
        { QStringLiteral("key"),       filter.key() },
        { QStringLiteral("rule"),      analysis::kOutcomeScopeRuleVersion },
        { QStringLiteral("chosen"),    r.chosen },
        { QStringLiteral("sessions"),  r.sessions },
        { QStringLiteral("others"),    r.others },
        { QStringLiteral("measured"),  r.measured },
        { QStringLiteral("contrasts"), contrasts },
    };
}

bool outcomeFromJson(const QJsonObject &o, const analysis::ShotFilter &filter, AthleteLevel::ScopedResult &r)
{
    if (o.value(QStringLiteral("key")).toString() != filter.key()
        || o.value(QStringLiteral("rule")).toInt() != analysis::kOutcomeScopeRuleVersion)
        return false;
    r.chosen   = o.value(QStringLiteral("chosen")).toInt();
    r.sessions = o.value(QStringLiteral("sessions")).toInt();
    r.others   = o.value(QStringLiteral("others")).toInt();
    r.measured = o.value(QStringLiteral("measured")).toInt();
    r.contrasts.clear();
    const QJsonObject contrasts = o.value(QStringLiteral("contrasts")).toObject();
    for (auto it = contrasts.constBegin(); it != contrasts.constEnd(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() != 4) return false;
        r.contrasts.insert(it.key(), analysis::OutcomeContrast{ a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(), a.at(3).toInt() });
    }
    return true;
}

QStringList scopedFileKeys(const analysis::ClubScope &scope, const analysis::ShotFilter &filter)
{
    QStringList keys{ analysis::clubScopeFileKey(scope.key) };
    if (!filter.isAll()) keys << analysis::clubScopeFileKey(filter.key());
    return keys;
}

QString combinedScopeKey(const analysis::ClubScope &scope, const analysis::ShotFilter &filter)
{
    return filter.isAll() ? scope.key : scope.key + QLatin1Char('|') + filter.key();
}

// One session as the scoped job reads it: its rows as its owner held them, and its summary's record and evidence.
struct ScopedSession {
    QString name;
    SessionPlace place;
    std::shared_ptr<const analysis::SessionReductionInput> rows;
    analysis::SessionWorkOns stored;
    analysis::TendencySessionEvidence evidence;
};

// The scoped job's per-session pass (the old runThemes' scoped half, over the owners' rows): which shots each session
// gives the choice, its record, the filter's counts. Pure; any thread.
struct ScopedPrep {
    std::vector<analysis::ThemeSessionInput> themeInputs;
    AthleteLevel::ScopedResult result;
};
ScopedPrep prepareScoped(const std::vector<ScopedSession> &sessions, const analysis::ClubScope &scope, const analysis::ShotFilter &filter,
                         const analysis::CharacteristicPack &pack, const analysis::ShotOutcomePack &outcomePack,
                         const analysis::ContextTree &tree, const analysis::FlightNorms &flight, const QSet<QString> &faultIds,
                         const std::atomic<bool> *cancel)
{
    ScopedPrep out;
    out.result.key = combinedScopeKey(scope, filter);
    for (const ScopedSession &s : sessions) {
        if (cancel && cancel->load()) return out;
        if (!s.rows) continue;
        analysis::ThemeSessionInput in;
        in.name  = s.name;
        in.shots = s.rows->shots;
        bool whole = analysis::wholeSessionInScope(in.shots, scope);
        std::vector<analysis::ShotRecord> sessionShots;
        if (!filter.isAll()) sessionShots = in.shots;
        if (!whole) in.shots = analysis::shotsInScope(in.shots, scope);
        if (!filter.isAll() && !in.shots.empty()) {
            analysis::TendencySessionInput tin;
            tin.session.folder      = s.name;
            tin.session.day         = s.place.day;
            tin.session.nOfDay      = s.place.nOfDay;
            tin.session.shots       = std::move(sessionShots);
            tin.session.corrections = s.rows->corrections;
            tin.intents             = s.rows->intents;
            tin.evidence            = s.evidence;
            if (s.rows->baseline) tin.baseline = *s.rows->baseline;
            QHash<int, analysis::ShotFactors> judged = analysis::judgedShotFactors(tin, outcomePack, flight);
            // Tendencies leaves out a club with no group (a putter, an undeclared club): judged by nothing here either.
            for (const analysis::ShotRecord &shot : tin.session.shots)
                if (analysis::tendencyClubGroup(tree, shot.club).isEmpty()) judged.insert(shot.shotId, analysis::ShotFactors{});
            const QHash<QString, analysis::OutcomeContrast> c = analysis::outcomeContrasts(in.shots, judged, filter, faultIds);
            for (auto it = c.constBegin(); it != c.constEnd(); ++it) out.result.contrasts[it.key()] += it.value();
            for (const analysis::ShotRecord &shot : in.shots) {
                const analysis::ShotFactors j = judged.value(shot.shotId);
                if (j.judged() && !filter.contains(j)) ++out.result.others;
            }
            std::vector<analysis::ShotRecord> chosen = analysis::shotsInShotFilter(in.shots, judged, filter);
            whole = whole && chosen.size() == in.shots.size();
            in.shots = std::move(chosen);
            if (!in.shots.empty()) {
                out.result.chosen += int(in.shots.size());
                ++out.result.sessions;
            }
            for (const analysis::ShotRecord &shot : in.shots)
                if (std::any_of(shot.rows.begin(), shot.rows.end(), [&faultIds](const analysis::ConditionRow &r) {
                        return r.state != analysis::ShotState::NotAssessable && faultIds.contains(r.conditionId);
                    }))
                    ++out.result.measured;
        }
        if (in.shots.empty()) continue;
        out.result.records.insert(s.name, whole ? s.stored : scopedSessionRecord(s.name, in.shots, s.rows->options, pack, s.stored));
        out.themeInputs.push_back(std::move(in));
    }
    std::stable_sort(out.themeInputs.begin(), out.themeInputs.end(),
                     [](const analysis::ThemeSessionInput &a, const analysis::ThemeSessionInput &b) { return a.name < b.name; });
    return out;
}

} // namespace

// The scoped file's fingerprint: every club's parts (themesFingerprint), then the club choice (its rule version, key and
// clubs), then a filter's (its rule version, key and the tendency fingerprint: a correction moves it).
QString AthleteDiagnostics::scopedFingerprint(const analysis::ClubScope &scope, const analysis::ShotFilter &filter)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(themesFingerprint().toUtf8());
    if (!scope.isAll()) {
        QStringList clubs(scope.clubIds.begin(), scope.clubIds.end());
        clubs.sort();
        h.addData(QByteArrayLiteral("\x1d"));
        h.addData(QByteArray::number(analysis::kClubScopeRuleVersion));
        h.addData(scope.key.toUtf8());
        h.addData(clubs.join(QLatin1Char('\x1f')).toUtf8());
    }
    if (!filter.isAll()) {
        h.addData(QByteArrayLiteral("\x1d"));
        h.addData(QByteArray::number(analysis::kOutcomeScopeRuleVersion));
        h.addData(filter.key().toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(tendenciesFingerprint().toUtf8());
    }
    return hex16(h);
}

AthleteDiagnostics::Ask AthleteDiagnostics::askScoped(const analysis::ClubScope &scope, const analysis::ShotFilter &filter, bool layerOneFirst)
{
    if (m_jobsPaused) return Ask::Paused;
    if (scope.isAll() && filter.isAll()) return Ask::Current;   // every club's is askThemes'
    ensure();
    const QString key = combinedScopeKey(scope, filter);
    const QString fp  = scopedFingerprint(scope, filter);
    if (m_scopedReady && m_scoped.key == key && fp == m_scopedFp && !scopedRunning()) return Ask::Current;
    if (scopedRunning() && fp == m_scopedAsked) return Ask::Running;
    dropItem(QStringLiteral("scoped1"));
    dropItem(QStringLiteral("scoped"));

    // THE STORED CHOICE, when it was reduced from exactly this: its records and counts restored, nothing reduced.
    if (AthleteRepository *repo = m_athlete->m_repository) {
        const JsonFileRead f = repo->readCache(*m_athlete, AthleteRepository::Cache::Themes, scopedFileKeys(scope, filter));
        if (f.exists && f.ok) {
            bool ok = false;
            ScopedResult r;
            r.key    = key;
            r.themes = analysis::swingThemesFromJson(f.root, &ok);
            for (const QJsonValue &v : f.root.value(QStringLiteral("records")).toArray()) {
                bool recordOk = false;
                analysis::SessionWorkOns rec = analysis::sessionWorkOnsFromJson(v.toObject(), &recordOk);
                ok = ok && recordOk;
                r.records.insert(rec.sessionId, std::move(rec));
            }
            if (ok && !filter.isAll()) ok = outcomeFromJson(f.root.value(QStringLiteral("outcome")).toObject(), filter, r);
            const QString stored = f.root.value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("fingerprint")).toString();
            if (ok && !r.themes.cancelled && stored == fp) {
                m_scoped = std::move(r);
                m_scopedFp = fp;
                m_scopedScope = scope;
                m_scopedFilter = filter;
                m_scopedReady = true;
                ++m_counts.themeCacheReads;
                emit scopedLanded();
                return Ask::Landed;
            }
        }
    }
    // No stored result: the screens showing another choice are in for a wait.
    m_pendingScope    = scope;
    m_pendingFilter   = filter;
    m_pendingLayerOne = layerOneFirst;
    m_scopedAsked     = fp;
    m_running.insert(QStringLiteral("scoped"), true);
    emit scopedWorking();
    startRowPass();
    return scopedRunning() ? Ask::Running : Ask::Landed;
}

// THE ROW PASS (DEC-195): every session whose summary this build reads, its judged rows taken from its own
// SessionDiagnostics — the one reader of its diagnostics.json — as its last applied diagnosis was reduced from. A session
// loaded already gives them at once once its owner is idle; one that is not is held for the read (a use added), its owner's
// first diagnosis awaited, and released after when this pass loaded it (the catch-up's discipline). Rows are kept until the
// session's summary moves. One with none to give is listed (rowsMissing: J-51).
void AthleteDiagnostics::startRowPass()
{
    ensure();
    retryReleases();
    m_rowQueue.clear();
    m_rowMissingIds.clear();
    QSet<SessionId> wanted;
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read) continue;
        wanted.insert(s.id);
        // The session already in hand (an earlier pass's, waiting for its owner) is not queued again: its rows land
        // when it settles, and a second load after its release would read it twice (REVIEW-f R1-m3 a).
        if (s.id == m_rowSessionId) continue;
        const QString key = s.results.derivedFrom.fingerprint + QLatin1Char('@') + QString::number(qint64(s.results.derivedFrom.writtenAtMs));
        const auto it = m_rows.constFind(s.id);
        if (it == m_rows.constEnd() || it->key != key || !it->input) m_rowQueue << s.id;
    }
    for (auto it = m_rows.begin(); it != m_rows.end();)   // a session gone (trashed, its summary no longer read)
        it = wanted.contains(it.key()) ? std::next(it) : m_rows.erase(it);
    m_rowPassActive = true;
    stepRows();
}

void AthleteDiagnostics::stepRows()
{
    if (m_rowStepping || !m_rowPassActive || m_rowSession) return;
    m_rowStepping = true;
    while (!m_rowQueue.isEmpty() && m_rowPassActive && !m_rowSession) {
        const SessionId id = m_rowQueue.takeFirst();
        const bool loadedHere = !m_athlete->loadedSession(id);
        Session *s = m_athlete->session(id);   // loads it (its diagnostics attach) when it is not loaded
        SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
        if (!s || !d || s->shots().isEmpty()) {
            m_rowMissingIds.insert(id);
            if (s && loadedHere) letGo(id);
            continue;
        }
        if (d->idle()) {
            takeRows(s, loadedHere);
            continue;
        }
        // Its owner is still loading or reducing: wait for it to settle, the Session held meanwhile.
        s->addUse();
        m_rowSession    = s;
        m_rowSessionId  = id;
        m_rowLoadedHere = loadedHere;
        const auto settled = [this] {
            Session *held = m_rowSession.data();
            SessionDiagnostics *hd = held ? held->diagnostics() : nullptr;
            if (!held || !hd || !hd->idle()) return;
            const bool here = m_rowLoadedHere;
            endRowWait();
            held->dropUse();
            takeRows(held, here);
            stepRows();
        };
        m_rowWaits << connect(d, &SessionDiagnostics::diagnosisApplied, this, settled, Qt::QueuedConnection);
        m_rowWaits << connect(d, &SessionDiagnostics::batchSettled, this, settled, Qt::QueuedConnection);
        m_rowWaits << connect(s, &QObject::destroyed, this, [this, id] {   // trashed while in hand: listed, the pass goes on
            if (m_rowSessionId != id) return;   // queued: a newer pass may hold another session by now (R1-m3 b)
            endRowWait();
            m_rowMissingIds.insert(id);
            stepRows();
        }, Qt::QueuedConnection);
        // NEVER WAITS FOR EVER: an owner that has not settled in a minute (its results lost, a read that hangs on the
        // share) gives no rows this time — listed (rowsMissing, J-51), let go, and the pass goes on.
        QTimer::singleShot(60'000, this, [this, id, held = QPointer<Session>(s)] {
            if (m_rowSessionId != id || m_rowSession != held) return;
            ppWarn() << "[AthleteDiagnostics] a session's diagnostics did not settle for the scoped views; left out, said:" << id.toString();
            const bool here = m_rowLoadedHere;
            endRowWait();
            if (held) {
                held->dropUse();
                if (here) letGo(id);
            }
            m_rowMissingIds.insert(id);
            stepRows();
        });
    }
    m_rowStepping = false;
    if (m_rowQueue.isEmpty() && !m_rowSession && m_rowPassActive) {
        m_rowPassActive = false;
        m_rowsMissing.clear();
        for (const AthleteSessionSummary &s : m_summaries)
            if (m_rowMissingIds.contains(s.id)) m_rowsMissing << s.place.folder;
        if (!m_rowsMissing.isEmpty())
            ppInfo() << "[AthleteDiagnostics] the judged rows of" << m_rowsMissing.size()
                     << "session(s) could not be read for the scoped views, said:" << m_rowsMissing.join(QStringLiteral(", "));
        submitScoped();
    }
}

void AthleteDiagnostics::endRowWait()
{
    for (const QMetaObject::Connection &c : std::as_const(m_rowWaits)) QObject::disconnect(c);
    m_rowWaits.clear();
    m_rowSession = nullptr;
    m_rowSessionId = SessionId();
}

void AthleteDiagnostics::takeRows(Session *session, bool loadedHere)
{
    const SessionId id = session->id();
    SessionDiagnostics *d = session->diagnostics();
    const std::shared_ptr<const analysis::SessionReductionInput> input = d ? d->diagnosisInput() : nullptr;
    if (!input) {
        m_rowMissingIds.insert(id);
    } else {
        QString key;
        for (const AthleteSessionSummary &s : m_summaries)
            if (s.id == id)
                key = s.results.derivedFrom.fingerprint + QLatin1Char('@') + QString::number(qint64(s.results.derivedFrom.writtenAtMs));
        m_rows.insert(id, HeldRows{ key, input });
    }
    if (loadedHere)
        letGo(id);
}

// A session the row pass loaded, released — or, when another holder has a use on it now (the catch-up, a report make,
// Review: none of them releases what it did not load), released once that use is dropped: tried again every 30 s and at
// the next pass (REVIEW-f R1-B2; the catch-up's own rule, AthleteCatchUp::releaseWhenFree). Never left loaded for the run.
void AthleteDiagnostics::letGo(const SessionId &id)
{
    QString why;
    if (m_athlete->releaseSession(id, &why))
        return;
    ppInfo() << "[AthleteDiagnostics] a session the row pass loaded is released when it is free (" << why << "):" << id.toString();
    if (!m_releaseOwed.contains(id))
        m_releaseOwed << id;
    if (!m_releaseTimer.isActive())
        m_releaseTimer.start();
}

void AthleteDiagnostics::retryReleases()
{
    for (int i = m_releaseOwed.size() - 1; i >= 0; --i) {
        const SessionId id = m_releaseOwed.at(i);
        if (!m_athlete->loadedSession(id)) {   // released by now (an athlete switch, a trash)
            m_releaseOwed.removeAt(i);
            continue;
        }
        if (id == m_rowSessionId)              // a newer pass holds it again: that pass lets it go
            continue;
        if (m_athlete->releaseSession(id))
            m_releaseOwed.removeAt(i);
    }
    if (m_releaseOwed.isEmpty())
        m_releaseTimer.stop();
}

void AthleteDiagnostics::submitScoped()
{
    const auto snap = rules();
    if (!snap || !snap->pack || !snap->norms) {
        m_running.insert(QStringLiteral("scoped"), false);
        emit scopedLanded();
        return;
    }
    auto sessions = std::make_shared<std::vector<ScopedSession>>();
    for (const AthleteSessionSummary &s : m_summaries) {
        if (!s.read) continue;
        const auto it = m_rows.constFind(s.id);
        if (it == m_rows.constEnd() || !it->input || m_rowMissingIds.contains(s.id)) continue;
        sessions->push_back(ScopedSession{ s.place.folder, s.place, it->input, s.results.workOns, s.results.outcomeEvidence });
    }
    const std::shared_ptr<const analysis::ICharacteristicPackProvider> packProv = snap->pack;
    const std::shared_ptr<const analysis::INormProvider> norms = snap->norms;
    const analysis::CharacteristicPack &pack = packProv->pack();
    const analysis::DrillSet drills = snap->drills ? snap->drills->merged : analysis::DrillSet{};
    auto measures   = std::make_shared<const QHash<QString, analysis::ThemeMeasureInfo>>(analysis::themeMeasureInfo(pack));
    auto conditions = std::make_shared<const QHash<QString, analysis::ThemeConditionInfo>>(analysis::themeConditionInfo(pack, drills));
    QSet<QString> faultIds;
    for (auto it = conditions->constBegin(); it != conditions->constEnd(); ++it)
        if (it->fault) faultIds.insert(it.key());
    auto outcomePack = std::make_shared<const analysis::ShotOutcomePack>(analysis::shotOutcomePackFor(&pack));
    auto flight = std::make_shared<const analysis::FlightNorms>(snap->flightNorms ? *snap->flightNorms : analysis::shippedFlightNorms());
    const analysis::ClubScope scope = m_pendingScope;
    const analysis::ShotFilter filter = m_pendingFilter;
    const QString fp = m_scopedAsked;
    m_scopedContent = contentStampNow();
    const auto cancel = freshCancel(QStringLiteral("scoped"));
    if (m_pendingLayerOne)
        submit(QStringLiteral("scoped1"), [sessions, scope, filter, packProv, outcomePack, norms, flight, faultIds, conditions, cancel]() -> std::any {
            ScopedPrep prep = prepareScoped(*sessions, scope, filter, packProv->pack(), *outcomePack, norms->contexts(), *flight, faultIds, cancel.get());
            prep.result.themes = analysis::reduceSwingSummary(prep.themeInputs, *conditions, analysis::ThemeOptions{});
            return prep.result;
        });
    const DiagnosticsDoors &d = m_athlete->diagnosticsDoors();
    const bool inline_ = d.synchronous || !d.pool || !d.receiver;
    const analysis::ThemeParallelFor parallel = inline_ ? inlineParallelFor() : poolParallelFor(d.pool);
    submit(QStringLiteral("scoped"), [sessions, scope, filter, packProv, outcomePack, norms, flight, faultIds, measures, conditions, cancel,
                                      parallel, fp]() -> std::any {
        QElapsedTimer clock;
        clock.start();
        ScopedPrep prep = prepareScoped(*sessions, scope, filter, packProv->pack(), *outcomePack, norms->contexts(), *flight, faultIds, cancel.get());
        prep.result.themes = analysis::reduceSwingThemes(prep.themeInputs, *measures, *conditions, analysis::ThemeOptions{}, parallel, cancel.get());
        ppInfo() << qPrintable(QStringLiteral("[ClubScope] %1: %2 swings over %3 sessions%4, %5 ms")
                                   .arg(prep.result.key).arg(prep.result.themes.swings).arg(prep.result.themes.sessions)
                                   .arg(filter.isAll() ? QString()
                                                       : QStringLiteral(" (%1 chosen shots over %2 sessions, %3 measured, %4 others)")
                                                             .arg(prep.result.chosen).arg(prep.result.sessions)
                                                             .arg(prep.result.measured).arg(prep.result.others))
                                   .arg(clock.elapsed()));
        Q_UNUSED(fp);
        return prep.result;
    });
}

// R-PERS-3 (refactor 4-e, REVIEW-e §5): THE SESSION'S SUMMARY ON DISK, AS IT STANDS, IS A NEWER BUILD'S — read now, not
// from what this level holds (the catch-up asks it before it takes a session in hand, so a session a newer build owns is
// never loaded, re-graded or written by this one).
bool AthleteDiagnostics::summaryFromNewerBuild(const SessionId &session)
{
    SessionRepository *repo = repository();
    if (!repo || session.isNull())
        return false;
    const JsonFileRead f = repo->readWorkOns(session);
    return f.exists && f.ok && analysis::sessionResultsFromNewerBuild(f.root);
}

// R-PERS-3 (refactor 4-e): A CACHE A NEWER BUILD SAVED IS READ-ONLY HERE (the library is shared between machines that may
// run different builds). Checked on the file as it stands, before each write; true: leave it, write nothing.
bool AthleteDiagnostics::cacheFromNewerBuild(bool tendencies, const QStringList &keys)
{
    AthleteRepository *repo = m_athlete ? m_athlete->m_repository : nullptr;
    if (!repo)
        return false;
    const JsonFileRead f = repo->readCache(*m_athlete, tendencies ? AthleteRepository::Cache::Tendencies
                                                                  : AthleteRepository::Cache::Themes, keys);
    if (!f.exists || !f.ok)
        return false;
    // Every version the file carries (REVIEW-e B1): the themes' own; and a scoped file's records (each a session's work-ons,
    // kWorkOn*) and its shot filter's outcome block (kOutcomeScopeRuleVersion), which its reader matches exactly.
    const auto newerThemes = [](const QJsonObject &root) {
        if (analysis::swingThemesFromNewerBuild(root))
            return true;
        for (const QJsonValue &v : root.value(QStringLiteral("records")).toArray()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("schemaVersion")).toInt(0) > analysis::kWorkOnSchemaVersion
                || r.value(QStringLiteral("ruleVersion")).toInt(0) > analysis::kWorkOnRuleVersion)
                return true;
        }
        return root.value(QStringLiteral("outcome")).toObject().value(QStringLiteral("rule")).toInt(0)
               > analysis::kOutcomeScopeRuleVersion;
    };
    const bool newer = tendencies ? analysis::tendencySetFromNewerBuild(f.root) : newerThemes(f.root);
    if (newer) {
        ++m_counts.cacheNewerKept;
        ppWarn() << "[AthleteDiagnostics] a newer build's" << (tendencies ? "tendencies" : "swing summary")
                 << "file, read-only here, not written:" << m_athlete->folder() << keys.join(QLatin1Char('.'));
    }
    return newer;
}

WriteResult AthleteDiagnostics::writeScopedCache(const analysis::ClubScope &scope, const analysis::ShotFilter &filter)
{
    WriteResult w;
    w.status = WriteResult::Status::Unchanged;
    AthleteRepository *repo = m_athlete ? m_athlete->m_repository : nullptr;
    if (!repo || !m_scopedReady || m_scoped.themes.cancelled) return w;
    QJsonObject root = analysis::toJson(m_scoped.themes);
    root[QStringLiteral("derivedFrom")] = QJsonObject{
        { QStringLiteral("fingerprint"),  m_scopedFp },
        { QStringLiteral("contentStamp"), m_scopedContent },
        { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
    };
    QJsonArray records;
    for (auto it = m_scoped.records.constBegin(); it != m_scoped.records.constEnd(); ++it) records.append(analysis::toJson(it.value()));
    root[QStringLiteral("records")] = records;
    if (!filter.isAll()) root[QStringLiteral("outcome")] = outcomeToJson(m_scoped, filter);
    if (cacheFromNewerBuild(false, scopedFileKeys(scope, filter))) return w;
    w = repo->writeCache(*m_athlete, AthleteRepository::Cache::Themes, root, scopedFileKeys(scope, filter));
    if (w.ok()) ++m_counts.cacheWrites;
    else ppWarn() << "[SwingThemes] could not write the swing summary of" << m_scoped.key << ":" << w.error;
    return w;
}

// ── The caches (FILE-ATH-02, 03): this owner is their one writer (refactor 3.7-f) ───────────────────────────────────

WriteResult AthleteDiagnostics::writeTendenciesCache()
{
    WriteResult w;
    w.status = WriteResult::Status::Unchanged;
    AthleteRepository *repo = m_athlete ? m_athlete->m_repository : nullptr;
    if (!repo || !m_tendenciesReady || m_tendenciesWrittenFp == m_tendenciesFp)
        return w;   // nothing new, or nowhere to keep it (a tool's in-memory run)
    if (m_summaries.empty())
        return w;   // no session: no file written into the athlete's folder
    QJsonObject root = analysis::tendencySetToJson(m_landedTendencies);
    root[QStringLiteral("derivedFrom")] = QJsonObject{
        { QStringLiteral("fingerprint"),  m_tendenciesFp },
        { QStringLiteral("contentStamp"), m_tendenciesContent },
        { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
    };
    if (cacheFromNewerBuild(true)) return w;
    w = repo->writeCache(*m_athlete, AthleteRepository::Cache::Tendencies, root);
    if (w.ok()) {
        m_tendenciesWrittenFp = m_tendenciesFp;
        ++m_counts.cacheWrites;
    } else {
        ppWarn() << "[Tendencies] could not write the athlete's tendencies:" << m_athlete->folder() << "-" << w.error;
    }
    return w;
}

WriteResult AthleteDiagnostics::writeThemesCache()
{
    WriteResult w;
    w.status = WriteResult::Status::Unchanged;
    AthleteRepository *repo = m_athlete ? m_athlete->m_repository : nullptr;
    if (!repo || !m_themesReady || m_themesWrittenFp == m_themesFp || m_landedThemes.cancelled)
        return w;
    QJsonObject root = analysis::toJson(m_landedThemes);
    root[QStringLiteral("derivedFrom")] = QJsonObject{
        { QStringLiteral("fingerprint"),  m_themesFp },
        { QStringLiteral("contentStamp"), m_themesContent },
        { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
    };
    if (cacheFromNewerBuild(false)) return w;
    w = repo->writeCache(*m_athlete, AthleteRepository::Cache::Themes, root);
    if (w.ok()) {
        m_themesWrittenFp = m_themesFp;
        ++m_counts.cacheWrites;
    } else {
        ppWarn() << "[SwingThemes] could not write the swing summary:" << m_athlete->folder() << "-" << w.error;
    }
    return w;
}

WriteResult AthleteDiagnostics::persist()
{
    // What has landed and is not written yet (a write that failed at its landing is tried again here). Never computes:
    // the reductions are the jobs' (the catch-up calls this on the GUI thread).
    const WriteResult t = writeTendenciesCache();
    const WriteResult h = writeThemesCache();
    if (!t.ok()) return t;
    if (!h.ok()) return h;
    WriteResult w;
    w.status = (t.changed() || h.changed()) ? WriteResult::Status::Written : WriteResult::Status::Unchanged;
    return w;
}

// ── The receiver's routing (J-53) ──────────────────────────────────────────────────────────────────────────────────

QString athleteWorkOwner(const AthleteId &athlete)
{
    return QStringLiteral("athlete:") + athlete.toString();
}

bool isAthleteWorkOwner(const QString &owner)
{
    return owner.startsWith(QLatin1String("athlete:"));
}

AthleteId athleteOfWorkOwner(const QString &owner)
{
    return isAthleteWorkOwner(owner) ? AthleteId::fromString(owner.mid(8)) : AthleteId();
}

WorkTarget athleteDiagnosticsWorkTarget(Athlete *athlete, const WorkAddress &address)
{
    WorkTarget t;
    AthleteDiagnostics *d = athlete ? athlete->findChild<AthleteDiagnostics *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (!d)
        return t;   // OwnerGone
    t.instance  = d->instance();
    t.itemFound = true;
    t.latestAsk = d->latestAsk(address.item);
    QPointer<AthleteDiagnostics> to(d);
    t.deliver = [to](WorkVerdict v, WorkResult &&r) {
        if (to) to->deliver(v, std::move(r));
    };
    return t;
}

} // namespace pinpoint
