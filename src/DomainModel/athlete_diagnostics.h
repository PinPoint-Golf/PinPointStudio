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

#pragma once

// AthleteDiagnostics — THE ATHLETE LEVEL'S DERIVED OWNER, one per Athlete, its child (refactor step 3.6;
// build/refactor/step-3/BRIEF.md §4.6, §6 row 3.6; governance/artefacts/information_model.md CLS-ATHLETE-DIAG,
// OBJ-ATHLETE-DIAG, R-DER-3, §7.1's athlete row; DEC-46, DEC-53, DEC-60, DEC-92, DEC-138, DEC-172, DEC-192).
//
// WHAT IT READS: THE SESSION SUMMARIES ONLY (R-DER-3, DEC-92): each session's work_ons.json (FILE-SESS-05, written
// by its SessionDiagnostics: DEC-55), through the session repository the Athlete was made with, by the SessionIndex's
// ids (DEC-53). Never a ledger (diagnostics.json), a shot, a review or a swing document; it never re-runs a session's
// reduction. A summary of an older rule is not this build's answer (read strictly); one without DEC-192's growth
// gives its work-ons but no club windows.
//
// WHAT IT COMPUTES (each a pure Coaching rule over the summaries):
//   • the athlete work-ons (GL-70; DEC-138): aggregatePriorities over the sessions' ranked lists, and every condition
//     any session assessed (aggregateConditions) — work_ons.h's arithmetic, the same Home's list is built from;
//   • the distances ("Your distances", FILE-ATH-02's clubDistances; D7: DEC-172): the club windows each summary
//     carries, pooled newest first up to the caps (club_distances.h clubDistancesFromPools);
//   • a session's CLUB BASELINE before the Session freezes it (DEC-46): clubBaselineFor() — the predecessors'
//     summaries (strictly earlier days, the newest ten that hold a session), clubBaselineFromSummaries. Not cached
//     (D7): read fresh at each freeze, so a summary written by anyone is the one used. NOT COMPUTABLE (nothing is
//     frozen, so nothing wrong is frozen for good) while an earlier session in that window holds swings but no
//     current summary with its windows — the old model's live rule ("all current"), applied to every freeze.
//   • THE TENDENCIES (GL-64, FILE-ATH-02's rule): reduceTendencyShots over the summaries' compact tendency facts — each
//     shot as its session judged it by the Tendencies rule (DEC-192 as Mark amended it, 16 Nov "b": compact per-shot
//     facts, never the judged rows) — with the distances from the summaries' club windows;
//   • THE THEMES ("Your swing", "what you do well", the focus; FILE-ATH-03's rule, every club): reduceSwingThemes over
//     the summaries' compact theme facts (each assessed row: fired, in band; the shot's table cells), by session name,
//     the replicates through the caller's parallel-for.
//   • THE CONSISTENCY MARKS (Home's Consistency column and badges; refactor 3.7-f, J-52): a fault's measure graded over its
//     last kNWindow (20) readings across the golfer's sessions, newest first by shot time, from the summaries' compact
//     theme facts (each assessed row's driving measure, value, context, no-band; the shot's effective club) — the Rules
//     ring's one rule (consistency_grade.h, variability_measures.h). The grades are held here (CLS-ATHLETE-DIAG: "the
//     grades are held here and read by the themes reduction"); the praise rule reads them (swing_themes.h applyPraiseRule,
//     through the adapter's publishSummary). Nothing is written.
//   • THE SCOPED VIEWS (a club scope, a shot filter; Mark's J-50 answer (a), DEC-195): they need each shot's full judged
//     rows, which DEC-192 "b" keeps out of the summary, so they are read through each session's own SessionDiagnostics
//     (diagnosisInput(), the Session held for the read: the row pass), never diagnostics.json — R-DER-3's one exception.
// NOT HERE: intent consistency (no code computes it: DEC-58's intent summaries were never built).
//
// HOME'S JOBS (refactor 3.7-f; J-53): the tendencies, the themes (every club) and the marks are worked out OFF the GUI
// thread, on THIS OWNER'S OWN LANE of the one pool (R-CON-5: one owner, one lane), each over values taken here (the
// summaries' facts, the RuleData snapshot): askTendencies(), askThemes(), askConsistency(). Each answers Current (what is
// landed is already of these inputs), Landed (worked out inline — a synchronous door, a tool, a test — or the themes'
// stored file was current), Running (its signal follows) or Paused. A newer ask, a cancel or a pause drops the older
// result (by ask number, at the receiver). The themes' 500 replicates fan out over the pool's other workers at low
// priority, the job's own worker draining them too, and never wait for a helper that has not started. A live session
// pauses them (the adapter's setPaused: none runs beside capture).
//
// LAZY (DEC-60): an event marks it stale and it recomputes when next read. Heard: the Athlete's sessions started,
// resumed or trashed (EVT-17, 23, 20) and its record edited (EVT-14's interim) — the index again; and, for every
// Session the Athlete holds (Athlete::sessionHeld), its end (EVT-09), a shot trashed (EVT-03), its club baseline
// rewritten (EVT-16) and its diagnostics writing the summary (SessionDiagnostics::written) — that session's summary
// again. Only what moved is read again; changed() says a reader should read again. A summary written outside this
// process (another machine, over the share) is not heard: the adapter's scan says so (markStale(id), rereadAll()).
//
// PERSISTS (FILE-ATH-02 tendencies.json, FILE-ATH-03 swing_themes.json — every club's; refactor 3.7-f): THIS OWNER IS
// THEIR ONE WRITER, through the Athlete's AthleteRepository (readCache / writeCache): each written when its job lands
// with inputs that moved, with derivedFrom { fingerprint, contentStamp, writtenAtMs }; the themes' stored file is read
// back when its fingerprint is the inputs' (the 500 replicates skipped). A failed write is said (writeFailed; DEC-126),
// never only logged. persist() writes what has landed and is not written yet (the catch-up asks it at an athlete's end).
// An Athlete with no repository (a tool's in-memory run) keeps nothing.
//
// SESSIONS IT CANNOT USE (J-51; HARD: show with a warning, never hide): a session with shots whose summary is not read by
// this build (none yet, an older rule's, a newer schema's, unreadable) or is read without current per-shot facts adds
// nothing to the tendencies, the themes or the marks. notSummedUp() counts them (not the one being recorded here), so
// every screen that shows these figures can say so — and says which a newer build saved (newerFolders: read-only here)
// and which are stale or missing (the catch-up's), so a screen says what is really happening (refactor 4-e). They are never read from their ledgers (that would be C6: Mark's).
//
// Ownership (information_model.md §10): owner AthleteDiagnostics; level athlete; one writer of FILE-ATH-02 and FILE-ATH-03
// (every club's), through AthleteRepository; a view reaches it through its Athlete (AthleteDiagnostics::of), until step 4
// through WorkOnsController (Home) and the report (its Tendencies page, "Earlier today").
// Domain model ring (src/DomainModel): names no store type; GUI thread, like its Athlete.

#include "athlete.h"
#include "session_repository.h"
#include "rule_data.h"
#include "../Diagnostics/derived/club_distances.h"
#include "../Diagnostics/derived/club_scope.h"
#include "../Diagnostics/derived/outcome_scope.h"
#include "../Core/work_pool.h"
#include "../Vocabulary/Metrics/variability/variability_grader.h"   // VariabilityResult: the marks
#include "../Diagnostics/derived/session_results.h"
#include "../Diagnostics/derived/swing_themes.h"
#include "../Diagnostics/derived/tendencies.h"
#include "../Diagnostics/derived/work_ons.h"
#include "../Vocabulary/Identity/identity.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pinpoint::analysis { struct SessionReductionInput; }

namespace pinpoint {

class Session;

// One session's summary as the athlete level holds it.
struct AthleteSessionSummary {
    SessionId               id;
    analysis::SessionPlace  place;       // its folder's name, its day and place in the day (SessionIdentity's)
    SessionStatus           status = SessionStatus::Ended;
    bool                    read = false;   // a summary of this build's rule was read (Strict); else `results` is empty
    analysis::SessionResults results;
    // The file on disk was saved by a NEWER build (session_results.h sessionResultsFromNewerBuild; refactor 4-e): this
    // build can't read it in full. False for none, an older rule's or schema's, an unreadable file: the catch-up's to
    // write again.
    bool                    fromNewerBuild = false;
};

// The club baseline as of one session (DEC-46, DEC-172), as the athlete level computes it for the Session to freeze.
struct ClubBaselineComputation {
    bool        computable = false;
    QString     why;                       // when not computable
    QDate       day;                       // the session's day
    QHash<QString, analysis::ClubBaselineEntry> clubs;   // keys canonical; a club with no qualifying shot: no entry
    QStringList read;                      // the earlier sessions it read (folder names), newest first
    QList<SessionId> waitingOn;            // not computable: the earlier sessions not summed up yet, newest first (J-49)
};

// THE ATHLETE LEVEL AS ITS ADAPTERS READ IT (refactor 3.7-f): Home's adapter (WorkOnsController), "Your distances"
// (AthleteController) and the report reach the athlete level through this interface alone, handed to them by the
// composition root (main.cpp's doors) — so the QML suites, which compile those adapters without the domain owners
// (CMakeLists.txt: "the QML suites … need none of it"), link no AthleteDiagnostics. AthleteDiagnostics implements it; each
// member is the owner's own (below).
class AthleteLevel
{
public:
    enum class Ask { Current, Landed, Running, Paused };
    // What watch() tells: the owner went stale (a reader should read again), a job landed, a cache could not be saved.
    enum class Event { Changed, TendenciesLanded, ThemesLayerOneLanded, ThemesLanded, ConsistencyLanded, WriteFailed,
                       ScopedWorking, ScopedLayerOneLanded, ScopedLanded };
    // THE THEMES AND WORK-ONS UNDER A CLUB SCOPE OR A SHOT FILTER (refactor 3.7-f; DEC-195): what a choice's shots say —
    // the summary's layers, each session's record over its chosen shots, and the filter's counts (the line under it and
    // each fault's contrast with the golfer's other shots). Ids and numbers only.
    struct ScopedResult {
        QString key;                                                // the combined key (club, or club|filter)
        analysis::SwingThemes themes;
        QHash<QString, analysis::SessionPriorities> records;           // by session folder name
        int chosen = 0, sessions = 0, others = 0, measured = 0;     // the filter's (all 0 under a club scope alone)
        QHash<QString, analysis::OutcomeContrast> contrasts;        // fault condition id -> its counts
    };
    struct ConsistencyRequest {
        QString conditionId;
        QString measureId;
    };
    // J-51: the sessions with shots whose summary adds nothing here, not the one being recorded here. Of them (refactor
    // 4-e), the ones whose summary a NEWER build saved (this build can't read it in full); the rest are stale or missing,
    // the catch-up's to write again (stale()).
    struct NotSummedUp {
        int         count = 0;
        QStringList folders;        // their folder names, oldest first
        QStringList newerFolders;   // of `folders`, the ones a newer build summed up (AthleteSessionSummary::fromNewerBuild)
        int newer() const { return int(newerFolders.size()); }
        int stale() const { return count - newer(); }
    };
    struct Counts {
        quint64 recomputes = 0;            // the derived results computed again
        quint64 summaryReads = 0;          // work_ons.json files read (and, for a baseline's freshness, a predecessor's
                                           // club_baseline.json: clubBaselineFor, R1-B1 — both through SessionRepository)
        quint64 staleMarks = 0;
        quint64 tendencyJobs = 0;          // tendency reductions run (an unchanged ask counts none)
        quint64 themeJobs = 0;             // themes reductions run (a current stored file counts none)
        quint64 themeCacheReads = 0;       // the themes' stored file reused
        quint64 consistencyJobs = 0;       // marks graded
        quint64 consistencySessions = 0;   // sessions whose facts the graded marks read
        quint64 cacheWrites = 0;           // FILE-ATH-02/03 written
        quint64 cacheNewerKept = 0;        // … a write skipped: the file on disk is a newer build's (R-PERS-3; 4-e)
        quint64 jobsAsked = 0;             // jobs submitted, any item (a duplicate ask queues none: R1-m1)
        quint64 summariesReadAhead = 0;    // summaries read on the lane and applied (readAhead, J-57)
        qint64  readAheadAskMs = -1;       // the last readAhead()'s GUI-thread part: the plan (ms)
        qint64  readAheadApplyMs = -1;     // … and its apply on the GUI thread: the derived results (ms)
    };
    using Watcher = std::function<void(Event, const QString &what, const QString &why)>;

    virtual ~AthleteLevel() = default;
    virtual QObject *object() = 0;   // its lifetime (a QPointer's target)
    virtual QList<QMetaObject::Connection> watch(QObject *context, Watcher watcher) = 0;

    virtual void markStale(const SessionId &session = {}) = 0;
    virtual void rereadAll() = 0;
    // THE SUMMARIES READ OFF THE GUI THREAD (REVIEW-f R1-m9 a, J-57): the files read and parsed on this owner's lane, the
    // result applied on the GUI thread (latest wins: a staleness since asks again). Current: nothing to read, or read
    // inline (synchronous doors); Running: changed() follows the apply; Paused: the jobs are paused (read at the next ask).
    // Every read below still reads synchronously whatever is stale: an adapter asks this first, and reads on changed().
    virtual Ask  readAhead() = 0;
    virtual bool summariesReading() const = 0;
    // A session of this athlete by its folder name, from the index (no summary read); null when it lists none.
    virtual SessionId sessionIdOf(const QString &folderName) = 0;
    virtual const std::vector<AthleteSessionSummary> &summaries() = 0;
    virtual const std::vector<analysis::PrioritySummary> &workOns() = 0;
    virtual const std::vector<analysis::PrioritySummary> &conditions() = 0;
    virtual const std::vector<analysis::ClubDistanceItem> &distances() = 0;
    virtual NotSummedUp notSummedUp() = 0;
    virtual const AthleteSessionSummary *summaryOf(const QString &folderName) = 0;
    virtual analysis::TendencySet tendenciesAsOf(const SessionId &session) = 0;

    virtual Ask  askTendencies() = 0;
    virtual Ask  askThemes(bool layerOneFirst = false) = 0;
    virtual Ask  askConsistency(const analysis::ClubScope &scope, const std::vector<ConsistencyRequest> &requests) = 0;
    virtual void setJobsPaused(bool on) = 0;
    virtual void cancelJobs() = 0;
    virtual bool tendenciesRunning() const = 0;
    virtual bool themesRunning() const = 0;
    virtual bool consistencyRunning() const = 0;
    virtual bool tendenciesReady() const = 0;
    virtual const analysis::TendencySet &landedTendencies() const = 0;
    virtual QString landedTendenciesFingerprint() const = 0;
    virtual bool themesReady() const = 0;
    virtual const analysis::SwingThemes &landedThemes() const = 0;
    virtual const analysis::SwingThemes &layerOne() const = 0;
    virtual QString landedThemesFingerprint() const = 0;
    virtual const QHash<QString, variability::VariabilityResult> &consistency() const = 0;
    virtual QString consistencyScopeKey() const = 0;
    virtual const Counts &counts() const = 0;

    // The scoped views (DEC-195). `scope` not All, or `filter` not All. Landed / Current: scopedResult() is the choice's.
    virtual Ask  askScoped(const analysis::ClubScope &scope, const analysis::ShotFilter &filter, bool layerOneFirst) = 0;
    virtual bool scopedRunning() const = 0;
    virtual const ScopedResult &scopedLayerOne() const = 0;
    virtual const ScopedResult &scopedResult() const = 0;
    // The sessions whose judged rows could not be read through their own SessionDiagnostics for the last scoped views
    // (another athlete id: no Session; no diagnostics; a load that failed), by folder name: never left out without a word.
    virtual QStringList rowsMissing() const = 0;

    static QString consistencyKey(const QString &conditionId, const QString &measureId)
    {
        return conditionId + QLatin1Char('\x1f') + measureId;
    }
};

class AthleteDiagnostics : public QObject, public AthleteLevel
{
    Q_OBJECT
public:
    // The Athlete's AthleteDiagnostics: its child, made on the first ask (one per Athlete). Null for a null athlete.
    static AthleteDiagnostics *of(Athlete *athlete);

    Athlete *athlete() const { return m_athlete; }
    bool     stale() const { return m_indexStale || !m_dirty.isEmpty(); }
    // An event's chain ended here (DEC-60): `session` null marks the whole index stale.
    void     markStale(const SessionId &session = {}) override;

    // ── Reads: each recomputes first if stale (DEC-60) ─────────────────────────────────────────────────
    const std::vector<AthleteSessionSummary> &summaries() override;     // by place, oldest first (sessionPlaceBefore)
    QHash<QString, analysis::SessionPriorities>  records();       // every summary read, by its sessionId (folder name)
    const std::vector<analysis::PrioritySummary>     &workOns() override;      // the athlete work-ons (DEC-138)
    const std::vector<analysis::PrioritySummary>     &conditions() override;   // every condition any session assessed
    const std::vector<analysis::ClubDistanceItem>  &distances() override;    // Your distances (D7)
    // From the summaries' compact per-shot facts (DEC-192 as Mark amended it, 16 Nov "b"). A summary without them (an
    // older file) adds nothing.
    std::vector<analysis::TendencySessionShots> tendencySessions();
    const analysis::TendencySet                &tendencies();     // its clubDistances: the summaries' club windows (D7)
    std::vector<analysis::ThemeSessionInput>    themeInputs();    // by session (folder) name
    // Every club; `parallel` runs the replicates (empty: serially); not cached (a caller with a cancel recomputes).
    analysis::SwingThemes themes(const analysis::ThemeParallelFor &parallel = {}, const std::atomic<bool> *cancel = nullptr);
    // Not cached (D7): read fresh from the predecessors' summaries each time.
    ClubBaselineComputation clubBaselineFor(const SessionId &session);

    // The tendencies AS THEY STOOD AT `session` (the golfer report's page, so a report remade later says the same): the
    // facts of that session and of the ones strictly before it by day and place (tendencySessionCounts), and the distances
    // pooled from the same sessions' club windows. GUI thread; the caller hands the value to its worker.
    analysis::TendencySet tendenciesAsOf(const SessionId &session) override;

    NotSummedUp notSummedUp() override;
    // R-PERS-3 (refactor 4-e): the session's work_ons.json as it stands now is a newer build's (read-only here). The
    // catch-up asks it before it takes a session in hand: such a session is not loaded, re-graded or written.
    bool summaryFromNewerBuild(const SessionId &session);
    // The summary of the session in `folderName` as read here (strict), or null (none read: notSummedUp counts it).
    const AthleteSessionSummary *summaryOf(const QString &folderName) override;
    // Every summary read again at the next read (the files may have been written outside this process).
    void rereadAll() override;
    Ask  readAhead() override;
    bool summariesReading() const override { return m_running.value(QStringLiteral("summaries")); }
    SessionId sessionIdOf(const QString &folderName) override;

    // ── Home's jobs (header; J-53) ───────────────────────────────────────────────────────────────────────────
    Ask askTendencies() override;
    Ask askThemes(bool layerOneFirst = false) override;   // layerOneFirst: the summary's layer 1 lands first (themesLayerOneLanded)
    Ask askConsistency(const analysis::ClubScope &scope, const std::vector<ConsistencyRequest> &requests) override;
    void setJobsPaused(bool on) override;    // on: the running jobs let go, nothing asked until off
    void cancelJobs() override;              // the running jobs let go (their results dropped)
    bool tendenciesRunning() const override  { return m_running.value(QStringLiteral("tendencies")); }
    bool themesRunning() const override      { return m_running.value(QStringLiteral("themes")); }
    bool consistencyRunning() const override { return m_running.value(QStringLiteral("marks")); }
    bool jobsBusy() const           { return tendenciesRunning() || themesRunning() || consistencyRunning(); }

    bool                         tendenciesReady() const override { return m_tendenciesReady; }
    const analysis::TendencySet &landedTendencies() const override { return m_landedTendencies; }
    QString                      landedTendenciesFingerprint() const override { return m_tendenciesFp; }
    bool                         themesReady() const override { return m_themesReady; }
    const analysis::SwingThemes &landedThemes() const override { return m_landedThemes; }
    const analysis::SwingThemes &layerOne() const override { return m_layerOne; }
    QString                      landedThemesFingerprint() const override { return m_themesFp; }
    // The marks of the last ask, by consistencyKey(condition, measure); a request graded nothing has no entry.
    const QHash<QString, variability::VariabilityResult> &consistency() const override { return m_consistency; }
    QString consistencyScopeKey() const override { return m_consistencyScope; }

    // The athlete level's own caches (FILE-ATH-02, 03): what has landed and is not written yet, written now.
    WriteResult persist();

    // The receiver's hand-off (athleteDiagnosticsWorkTarget, below). GUI thread.
    void    deliver(WorkVerdict verdict, WorkResult &&result);
    quint64 instance() const { return m_instance; }
    quint64 latestAsk(const QString &item) const { return m_latest.value(item); }

    const Counts &counts() const override { return m_counts; }
    // The sessions the row pass loaded and could not release yet (R1-B2), and the retry's period (a test's seam).
    int  releasesOwed() const { return int(m_releaseOwed.size()); }
    void setReleaseRetryMsForTest(int ms) { m_releaseTimer.setInterval(ms); }
    QObject *object() override { return this; }
    Ask  askScoped(const analysis::ClubScope &scope, const analysis::ShotFilter &filter, bool layerOneFirst) override;
    bool scopedRunning() const override { return m_running.value(QStringLiteral("scoped")) || m_rowPassActive; }
    const ScopedResult &scopedLayerOne() const override { return m_scopedLayerOne; }
    const ScopedResult &scopedResult() const override { return m_scoped; }
    QStringList rowsMissing() const override { return m_rowsMissing; }
    QList<QMetaObject::Connection> watch(QObject *context, Watcher watcher) override;

signals:
    void changed();   // it went stale: a reader should read again
    // Home's jobs landing (GUI thread): after a Running ask, or before an inline Landed one returns.
    void tendenciesLanded();
    void themesLayerOneLanded();
    void themesLanded();
    void consistencyLanded();
    void scopedWorking();
    void scopedLayerOneLanded();
    void scopedLanded();
    // A cache this owner writes could not be saved (DEC-126): `what` in words, `why` the store's reason.
    void writeFailed(const QString &what, const QString &why);

private:
    explicit AthleteDiagnostics(Athlete *athlete);
    ~AthleteDiagnostics() override;
    void hearSession(Session *session);
    void submit(const QString &item, std::function<std::any()> work);
    void dropItem(const QString &item);
    std::shared_ptr<std::atomic<bool>> freshCancel(const QString &item);
    QString contentStampNow();
    QString themesFingerprint();
    QString tendenciesFingerprint();
    WriteResult writeTendenciesCache();
    QString scopedFingerprint(const analysis::ClubScope &scope, const analysis::ShotFilter &filter);
    void startRowPass();
    void stepRows();
    void takeRows(Session *session, bool loadedHere);
    void endRowWait();
    void letGo(const SessionId &id);   // a session the row pass loaded: released now, or once it is free (R1-B2)
    void retryReleases();
    void submitScoped();
    WriteResult writeScopedCache(const analysis::ClubScope &scope, const analysis::ShotFilter &filter);
    // R-PERS-3 (4-e): the cache on disk (tendencies.json, else swing_themes[.<keys>].json) is a newer build's: not written.
    bool        cacheFromNewerBuild(bool tendencies, const QStringList &keys = {});
    WriteResult writeThemesCache();
    void ensure();
    AthleteSessionSummary readSummary(const SessionIndexEntry &e);
    std::vector<SessionIndexEntry> entriesToRead() const;   // what ensure() would read now
    // The fingerprint a session's summary would carry if it were written now: the one rule the writer
    // (SessionDiagnostics::summaryFingerprint) and the catch-up's scan share (session_reduction.h summaryFingerprintOf).
    QString summaryFingerprintNow(const SessionIndexEntry &e);
    SessionRepository *repository() const;
    std::shared_ptr<const RuleDataSnapshot> rules() const;   // the Athlete's doors' RuleData, else the process's

    Athlete *m_athlete = nullptr;
    bool     m_indexStale = true;
    QSet<SessionId> m_dirty;
    quint64         m_readGen = 0;                  // every staleness moves it (readAhead's latest wins)
    quint64         m_readAskedGen = 0;
    QHash<SessionId, AthleteSessionSummary> m_prefetched;   // read on the lane, taken by the next ensure()
    QSet<SessionId> m_heard;
    std::vector<AthleteSessionSummary>      m_summaries;
    std::vector<analysis::PrioritySummary>    m_workOns, m_conditions;
    std::vector<analysis::ClubDistanceItem> m_distances;
    analysis::TendencySet                   m_tendencies;
    bool                                    m_tendenciesCurrent = false;
    NotSummedUp                             m_notSummedUp;
    Counts   m_counts;

    // Home's jobs (J-53)
    quint64   m_instance = 0;
    WorkPool *m_pool = nullptr;                 // the pool its lane is on (outlives every Athlete in the app)
    bool      m_jobsPaused = false;
    QHash<QString, quint64> m_latest;           // item -> its latest ask
    QHash<QString, bool>    m_running;
    QHash<QString, std::shared_ptr<std::atomic<bool>>> m_cancel;
    analysis::TendencySet   m_landedTendencies;
    QString  m_tendenciesFp, m_tendenciesWrittenFp, m_tendenciesContent, m_tendenciesAsked;
    bool     m_tendenciesReady = false;
    analysis::SwingThemes   m_landedThemes, m_layerOne;
    QString  m_themesFp, m_themesWrittenFp, m_themesContent, m_themesAsked;
    bool     m_themesReady = false;
    QHash<QString, variability::VariabilityResult> m_consistency;
    QString  m_consistencyFp, m_consistencyScope, m_consistencyAsked;
    // The scoped views (DEC-195): each session's judged rows as its own SessionDiagnostics held them, kept until its
    // summary moves (the summary's fingerprint and write time), and the row pass that fetches them.
    struct HeldRows {
        QString key;                                                    // its summary's fingerprint and write time
        std::shared_ptr<const analysis::SessionReductionInput> input;   // the owner's last applied diagnosis' input
    };
    QHash<SessionId, HeldRows> m_rows;
    QStringList        m_rowsMissing;
    QList<SessionId>   m_rowQueue;
    QSet<SessionId>    m_rowMissingIds;
    bool               m_rowPassActive = false;
    bool               m_rowStepping = false;
    QPointer<Session>  m_rowSession;                // in hand, waiting for its owner to settle
    SessionId          m_rowSessionId;
    bool               m_rowLoadedHere = false;
    QList<QMetaObject::Connection> m_rowWaits;
    QList<SessionId>   m_releaseOwed;           // loaded by the row pass, refused at let-go: released when free
    QTimer             m_releaseTimer;
    analysis::ClubScope  m_pendingScope;
    analysis::ShotFilter m_pendingFilter;
    bool                 m_pendingLayerOne = false;
    ScopedResult m_scopedLayerOne, m_scoped;
    QString      m_scopedFp, m_scopedAsked, m_scopedContent;
    analysis::ClubScope  m_scopedScope;
    analysis::ShotFilter m_scopedFilter;
    bool         m_scopedReady = false;
    std::shared_ptr<const RuleDataSnapshot> m_stampOf, m_summaryRulesOf;
    QString  m_contentStamp, m_summaryRules;
};

// Where a Home job's result goes (the composition root's WorkResolver): `athlete`'s AthleteDiagnostics, or OwnerGone. The
// address's owner is athleteWorkOwner(id): "athlete:" and the AthleteId, so it can never be a SessionId's text.
QString    athleteWorkOwner(const AthleteId &athlete);
bool       isAthleteWorkOwner(const QString &owner);
AthleteId  athleteOfWorkOwner(const QString &owner);
WorkTarget athleteDiagnosticsWorkTarget(Athlete *athlete, const WorkAddress &address);
// THE SESSION'S OWN ATHLETE LEVEL (REVIEW-f R2-B1): the AthleteDiagnostics of the Athlete among `athletes` whose folder holds
// `sessionDir` (D4: by folder, never a name), any athlete — not only the current one; null when none does. The report's
// door (main.cpp), so its stamps read each golfer's own owner.
AthleteLevel *athleteLevelOfSession(const QList<Athlete *> &athletes, const QString &sessionDir);
// The themes' replicates fanned out over the one pool's workers (J-53), the calling job's worker draining them too: what the
// owner's themes jobs run with, and what athlete_dump --bench-themes times.
analysis::ThemeParallelFor athleteLaneParallelFor(WorkPool *pool);

} // namespace pinpoint
