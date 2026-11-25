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

#include "work_ons_controller.h"
#include "../../DomainModel/rule_data.h"   // the packs and norms: RuleData's snapshot (refactor step 3.2)
#include "../../Coaching/tendency_flow_map.h"

#include "../../Coaching/session_spread.h"


#include "../../Coaching/board_annotations.h"   // the launch monitor board's annotations (step 3.6)
#include "../../Coaching/club_scope.h"
#include "../../Coaching/tendencies_pack.h"   // the tendencies over read sessions: one rule (3.6)
#include "../../Coaching/shot_outcomes_pack.h"
#include "../../Coaching/condition_in_hand.h"   // a condition worded for the golfer's hand (refactor 6.3b fix round, B-2)
#include "../../Coaching/swing_areas_pack.h"
#include "../../Coaching/work_ons_pack.h"

#include "../../Diagnostics/packs/characteristic_pack.h"
#include "../../Diagnostics/packs/drill_pack.h"
#include "../../Diagnostics/engine/pack_io.h"
#include "../../Diagnostics/engine/fire_findings.h"   // contentStamp: the one rule (refactor 3.7-e)
#include "../../Coaching/session_results.h"        // the summary's strict read (3.7-e)
#include "../../DomainModel/summary_rules.h"       // the rule data as the firing reads it; summaryFingerprintOf (3.7-e round 2)
#include "../../DomainModel/athlete_diagnostics.h"   // AthleteLevel: the athlete level, which this adapts (3.7-f)
#include <QMutex>
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Diagnostics/packs/swing_themes_pack.h"
#include "../../Diagnostics/norms/context_tree.h"
#include "../../Diagnostics/norms/norm_provider.h"
#include "../../Metrics/variability/variability_grader.h"
#include "../../Core/pp_debug.h"
#include "../../Export/swing_store.h"
#include "../../Export/session_diagnostics_files.h"   // the session's files, and the golfer's input (step 2.5)
#include "../../Export/session_files.h"
#include "../../Export/swing_paths.h"
#include "../../Export/athlete_files.h"

#include <limits>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace pinpoint::analysis;
namespace pv = pinpoint::variability;

namespace {

using pinpoint::SessionDiagnosticsFiles;
using pinpoint::SessionFiles;

// The consistency marks' rule and its version moved with the marks to AthleteDiagnostics (refactor 3.7-f; J-52).

QString dayLabel(qint64 ms)
{
    if (ms <= 0) return QString();
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms).date();
    return QLocale::c().toString(d, d.year() == QDate::currentDate().year()
                                        ? QStringLiteral("d MMM") : QStringLiteral("d MMM yyyy"));
}

// "Mon 26 Oct" — the session's day with its weekday, the year only when it is not this one.
QString weekdayLabel(qint64 ms)
{
    if (ms <= 0) return QString();
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms).date();
    return QLocale::c().toString(d, d.year() == QDate::currentDate().year()
                                        ? QStringLiteral("ddd d MMM") : QStringLiteral("ddd d MMM yyyy"));
}

// THE GRADE A VALUE EARNS against a corridor, off the edges the session panel draws its bands from
// (session_spread.h spreadBands) — so the word beside an area item's typical value is the band its
// dot would sit in there. Boundaries as grade() has them: ON an edge is the inner band (|z| ≤ max).
// An OPEN side does not grade, and grade() calls a ceiling's low tail Ideal; so does this. "" when
// there is no corridor to read the value against. The panel's tokens, which Theme.corridorTone and
// corridorWord take.
QString gradeOnCorridor(const SpreadCorridor &c, double v)
{
    if (!c.known || !std::isfinite(v)) return QString();
    const auto past = [&c, v](double lo, double hi) {
        return (!c.lowOpen && v < lo) || (!c.highOpen && v > hi);
    };
    if (past(c.faultLo,  c.faultHi))  return gradeName(Grade::Action);
    if (past(c.signalLo, c.signalHi)) return gradeName(Grade::Watch);
    if (past(c.idealLo,  c.idealHi))  return gradeName(Grade::Good);
    return gradeName(Grade::Ideal);
}

// ONE TAIL OF A TWO-SIDED CORRIDOR: a condition watches one end of its measure (AreaItem::tail), and
// the other end belongs to its mirror. Without this, a wrist cupped past the low edge graded "watch"
// on bowed_at_top as well as on cupped_at_top (#112). The unwatched side opens, its edges collapsing
// onto mu as spreadCorridorFromRow's open sides do. "" leaves the corridor as it is.
// The session panel opens the same side the same way (session_spread.h spreadCorridorOnSide, #16).
SpreadCorridor watchingTail(SpreadCorridor c, const QString &tail)
{
    return spreadCorridorOnSide(c, firingSideFromString(tail));
}

// A COUNT of swings, never a percentage — recurrenceText()'s rule, one level up.
QString swingsText(int fired, int total)
{
    return total == 1 ? QStringLiteral("%1 of 1 swing").arg(fired)
                      : QStringLiteral("%1 of %2 swings").arg(fired).arg(total);
}

// A condition's name as the golfer reads it: the pack label of the condition as glossed for their hand (the one
// handedness gloss, refactor 6.3b, G-02: a left-hander's slice row is their hook), else the row's, else the fallback,
// else the id. `id` is a ROW id.
QString packLabel(const CharacteristicPack &pack, const QString &id, const QString &handedness,
                  const QString &fallback = QString())
{
    const ConditionInHand w = conditionInGolfersHand(pack, id, handedness);   // the one function (B-2)
    if (!w.label.isEmpty()) return w.label;
    return fallback.isEmpty() ? id : fallback;
}

// tendencyClubGroup and tendencySetFromSessions: Coaching/tendencies_pack.h (one rule with AthleteDiagnostics; 3.6).

// "1 shot", "38 shots".
QString outcomeCountText(int n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

// One factor's values in a club's tuple; empty when it wasn't read.
const QStringList &factorTupleValues(const TendencyFactorTuple &&, const QString &) = delete;   // a temporary would leave the result dangling (#247, #248)
const QStringList &factorTupleValues(const TendencyFactorTuple &t, const QString &factor)
{
    return factor == shotFactorStrike() ? t.strike : factor == shotFactorDirection() ? t.direction : t.flight;
}

// Whether a tuple's shots are in `filter`, `skip`'s choice left out (counted as Any); a factor not read fails
// any choice for it but Any, as ShotFilter::contains has it.
bool factorTupleHolds(const TendencyFactorTuple &t, const ShotFilter &filter, const QString &skip = QString())
{
    for (const QString &name : shotFactorNames()) {
        if (name == skip) continue;
        const QString &want = filter.choice(name);
        if (want != shotFilterAny() && !factorTupleValues(t, name).contains(want)) return false;
    }
    return true;
}

// "Active" before "Easing": how pressing a work-on's status is, for the Tendencies row's words.
int workOnStatusRank(const QString &status)
{
    if (status == QLatin1String("active")) return 0;
    if (status == QLatin1String("easing")) return 1;
    return 2;
}

} // namespace

WorkOnsController::WorkOnsController(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_filterOptions = shotFilterOptionsFor(m_filter);      // Any alone, until there are tendencies to count
    // EVT-13 PackChanged (refactor 5.7e): the process's RuleData, the one main.cpp installs before this is made.
    if (pinpoint::RuleData *rd = pinpoint::RuleData::installed())
        connect(rd, &pinpoint::RuleData::packChanged, this, [this](const QString &) { onPackChanged(); });
}

// The view of RuleData's current pack, made again when another has been published since it was made; the outcome pack
// marshalled from the old one goes with it. GUI thread (RuleData publishes there too).
void WorkOnsController::followPackSave() const
{
    const std::shared_ptr<const pinpoint::RuleDataSnapshot> now = pinpoint::RuleData::current();
    if (m_packProv && now->pack == m_packOf) return;
    m_packOf   = now->pack;
    m_packProv = makeCharacteristicPackProvider();
    m_outcomePack.reset();
}

// A saved pack edit (EVT-13): the words shown are made again from the new pack — the list, its areas and screens, Your
// swing and the tendencies' columns. The records are NOT re-derived here: the rule stamp moved with the snapshot, so the
// scan finds every summary stale at its own triggers (Home shown, an athlete switch) and the catch-up brings them up to
// date then, never once per editor save. While the scan, the catch-up or a live session runs, it waits for their publish.
void WorkOnsController::onPackChanged()
{
    followPackSave();
    if (m_scanning || m_deriving || m_paused || !m_queue.isEmpty()) { m_ownerDirty = true; return; }
    publish();
    publishSummary();
}

WorkOnsController::~WorkOnsController()
{
    // The pools' lambdas deliver back to `this`; drain them before the members go. A themes job
    // in flight is told to stop first, so quitting does not wait out a reduction.
    m_pool.waitForDone();
}

QString WorkOnsController::recordPath(const QString &sessionDir)
{
    return SessionFiles::workOnsFile(QDir(sessionDir).path());
}

// The athlete's files by AthleteFiles' names (step 2.5: lint A4's owner); still written here until step 3.
QString WorkOnsController::themesPath(const QString &athleteDir)
{
    return pinpoint::AthleteFiles::themesFile(QDir(athleteDir).path());
}

QString WorkOnsController::themesPath(const QString &athleteDir, const ClubScope &scope)
{
    if (scope.isAll()) return themesPath(athleteDir);
    return pinpoint::AthleteFiles::themesFile(QDir(athleteDir).path(), { clubScopeFileKey(scope.key) });
}

QString WorkOnsController::themesPath(const QString &athleteDir, const ClubScope &scope, const ShotFilter &filter)
{
    if (filter.isAll()) return themesPath(athleteDir, scope);
    return pinpoint::AthleteFiles::themesFile(QDir(athleteDir).path(),
                                              { clubScopeFileKey(scope.key), clubScopeFileKey(filter.key()) });
}

QString WorkOnsController::tendenciesPath(const QString &athleteDir)
{
    return pinpoint::AthleteFiles::tendenciesFile(QDir(athleteDir).path());
}

// THE CONTENT STAMP THE RECORDS ARE GRADED AGAINST (refactor 3.7-e): the process's RuleData snapshot, hashed by the Rules
// ring's one rule (fire_findings.h contentStamp) — exactly what each session's owner puts in its summary's fingerprint, so
// the scan's freshness check and the writer agree. (Until 3.7-e the private model's own stamp.)
// THE CONTENT STAMP and THE RULE DATA'S SUMMARY STAMP, ONCE PER RuleData SNAPSHOT (review R1-m3 of 3.7-e: each hashes the
// whole pack and norms, ~10 ms; the refresh and every scope or filter change asked again). Both by the owner's rules
// (fire_findings.h contentStamp; summary_rules.h + summaryRulesStamp). GUI thread (a mutex anyway: the stand-ins call it).
QString WorkOnsController::rulesStampNow()
{
    static QMutex lock;
    static std::shared_ptr<const pinpoint::RuleDataSnapshot> of;
    static QString stamp;
    const std::shared_ptr<const pinpoint::RuleDataSnapshot> rules = pinpoint::RuleData::current();
    const QMutexLocker guard(&lock);
    if (rules != of) {
        of    = rules;
        stamp = rules ? pinpoint::analysis::summaryRulesStamp(pinpoint::firingInputsOf(*rules)) : QString();
    }
    return stamp;
}

void WorkOnsController::setCatchUp(CatchUpDoor door)
{
    m_catchUp = std::move(door);
}

void WorkOnsController::setAthleteDoor(std::function<pinpoint::AthleteLevel *(const QString &)> door)
{
    m_athleteDoor = std::move(door);
    pointAtOwner();
}

pinpoint::AthleteLevel *WorkOnsController::owner() const
{
    return m_ownerObject ? m_owner : nullptr;
}

int WorkOnsController::tendencyJobs() const
{
    return owner() ? int(owner()->counts().tendencyJobs) : 0;
}

int WorkOnsController::consistencyLedgersRead() const
{
    return owner() ? int(owner()->counts().consistencySessions) : 0;
}

// ── The owner (refactor 3.7-f) ──────────────────────────────────────────────────────────────────────────────────────

// THE CURRENT ATHLETE'S AthleteDiagnostics, through main.cpp's door (the AthleteLevel interface: this file links no owner):
// the one this list reads. A switch drops the old one's signals and lets its jobs go; the new one reads every summary again
// (a file may have been written outside this process since it last read them). Its "changed" reaches this object queued
// and coalesced (onOwnerChanged), so a run of summaries written during the catch-up publishes once it settles.
void WorkOnsController::pointAtOwner()
{
    pinpoint::AthleteLevel *next = (m_athleteDoor && !m_athleteDir.isEmpty()) ? m_athleteDoor(m_athleteDir) : nullptr;
    if (next == owner() && (next || !m_owner)) return;
    for (const QMetaObject::Connection &c : std::as_const(m_ownerConnections)) QObject::disconnect(c);
    m_ownerConnections.clear();
    if (owner()) owner()->cancelJobs();
    m_owner       = next;
    m_ownerObject = next ? next->object() : nullptr;
    m_ownerThemesRunning = false;
    if (!owner()) return;
    m_owner->setJobsPaused(m_paused);
    m_owner->rereadAll();
    if (!m_synchronous) m_owner->readAhead();   // its summaries read on its lane from now, beside the scan (R1-m9 a)
    using E = pinpoint::AthleteLevel::Event;
    m_ownerConnections = m_owner->watch(this, [this](E e, const QString &what, const QString &why) {
        switch (e) {
        case E::Changed:
            if (m_ownerChangePosted) return;
            m_ownerChangePosted = true;
            QMetaObject::invokeMethod(this, [this] { m_ownerChangePosted = false; onOwnerChanged(); }, Qt::QueuedConnection);
            return;
        case E::TendenciesLanded:     applyOwnerTendencies(); return;
        case E::ThemesLayerOneLanded: applyOwnerThemes(true); return;
        case E::ThemesLanded:         applyOwnerThemes(false); return;
        case E::ConsistencyLanded:    applyOwnerMarks(); return;
        case E::ScopedWorking:        if (m_themesRunning) setScopeWorking(true); return;
        case E::ScopedLayerOneLanded: applyOwnerScoped(true); return;
        case E::ScopedLanded:         applyOwnerScoped(false); return;
        case E::WriteFailed:          emit storageProblem(what, m_athleteDir, why); return;   // DEC-126: main.cpp's one route
        }
    });
}

// The owner went stale (a summary written, a session ended or trashed). While the scan or the catch-up runs, or a session is
// live, it waits: they end in a publish and the reductions (catchUpDone, the pause lifting).
void WorkOnsController::onOwnerChanged()
{
    if (!owner()) return;
    if (m_scanning || m_deriving || m_paused || !m_queue.isEmpty()) { m_ownerDirty = true; return; }
    m_ownerDirty = false;
    publish();
    maybeThemes();
    maybeMarks();
    maybeTendencies();
}

// One session's summary read again by the owner (its file moved, or the catch-up let it go); a folder it does not list yet:
// the index again.
void WorkOnsController::markSummaryStale(const QString &sessionDir)
{
    if (!owner()) return;
    // By the owner's index, not its summaries (reading them would read every file here, on the GUI thread: R1-m9 a).
    owner()->markStale(owner()->sessionIdOf(QFileInfo(sessionDir).fileName()));
}

QString WorkOnsController::dirOfFolder(const QString &folderName) const
{
    for (auto it = m_dirMtime.constBegin(); it != m_dirMtime.constEnd(); ++it)
        if (QFileInfo(it.key()).fileName() == folderName) return it.key();
    return QDir(m_athleteDir).absoluteFilePath(folderName);
}

// J-51: the sessions with shots the owner cannot use, said.
// THE OWNER'S SUMMARIES ARE READ ON ITS LANE (REVIEW-f R1-m9 a; J-57): while they are, every figure here stays as it was
// and waits — the owner's changed() after the apply brings this back (onOwnerChanged) — so no summary file is read on the
// GUI thread. Paused (a live session): what is stale is read here, as before (the live session's own summary, at most).
bool WorkOnsController::ownerReading()
{
    if (!owner() || owner()->readAhead() != pinpoint::AthleteLevel::Ask::Running) return false;
    m_ownerDirty = true;
    return true;
}

// WHAT A PUBLISH MAY SHOW (REVIEW-f R2-B2; DUTIES-f A8): the owner's figures when its summaries are read; while they are
// being read, THIS owner's last figures stay (nothing returns) — but a list showing another owner's figures (a switch)
// is emptied at once, so the previous golfer's figures never sit under this one's name. Null: publish the empty state.
pinpoint::AthleteLevel *WorkOnsController::publishSource(bool *hold)
{
    *hold = false;
    if (!ownerReading()) {
        m_shownOwner = m_ownerObject;
        return owner();
    }
    if (m_shownOwner && m_shownOwner == m_ownerObject) { *hold = true; return nullptr; }
    m_shownOwner = nullptr;
    return nullptr;
}

QString WorkOnsController::notSummedUpWords(int bringing, int leftOut)
{
    QStringList words;
    if (bringing > 0)
        words << (bringing == 1 ? QStringLiteral("Bringing 1 session up to date\u2026")
                                : QStringLiteral("Bringing %1 sessions up to date\u2026").arg(bringing));
    if (leftOut > 0)
        words << (leftOut == 1 ? QStringLiteral("1 session is left out: this version of PinPoint Studio can't read it in full yet.")
                               : QStringLiteral("%1 sessions are left out: this version of PinPoint Studio can't read them in full yet.").arg(leftOut));
    return words.join(QLatin1Char(' '));
}

void WorkOnsController::refreshNotSummedUp()
{
    int bringing = 0, leftOut = 0;
    bool hold = false;
    pinpoint::AthleteLevel *src = publishSource(&hold);
    if (hold) return;   // this owner's note as it was, until its summaries land (another owner's: cleared, R2-B2)
    if (src) {
        // THE OWNER SAYS WHY (refactor 4-e): a newer build's summary is left out; a stale or missing one is the catch-up's.
        // This object says only whether the catch-up is still at it (its scan, its asks in flight, or held by a pause): a
        // stale one it has finished with and could not write is left out, never "being brought up to date" for ever.
        const pinpoint::AthleteLevel::NotSummedUp ns = src->notSummedUp();
        const bool catchingUp = m_scanning || m_deriving || !m_queue.isEmpty();
        (catchingUp ? bringing : leftOut) += ns.stale();
        leftOut += ns.newer();
        // Under a club scope or a shot filter, a session whose judged rows could not be read (DEC-195) is left out too.
        if (m_summaryScope != clubScopeAllKey())
            for (const QString &f : src->rowsMissing())
                if (!ns.folders.contains(f)) ++leftOut;
    }
    const QString before = m_notSummedUpNote;
    m_notSummedUpNote = notSummedUpWords(bringing, leftOut);
    if (m_notSummedUpNote != before) emit notSummedUpChanged();
}

void WorkOnsController::setAthleteDir(const QString &dir)
{
    if (m_athleteDir == dir) return;
    m_athleteDir = dir;
    emit athleteDirChanged();
    pointAtOwner();
    // The previous golfer's list must not sit under this one's name while the scan runs —
    // nor their swing summary, and a reduction of their sessions has nothing left to do.
    m_records.clear();
    m_queue.clear();
    m_sessionsFound = 0;
    // D-15: the previous golfer's catch-up is let go (the session in hand released unfinished, the queue dropped).
    if (m_deriving) {
        m_deriving = false;
        if (m_catchUp.cancel) m_catchUp.cancel();
    }
    cancelThemes();
    cancelTendencies();
    // The marks are this golfer's alone.
    cancelMarks();
    m_marks.clear();
    m_marksFingerprint.clear();
    m_markRequests.clear();
    // Which clubs this golfer has hit is theirs too; the scope is worked out again from their records.
    m_scope = ClubScope();
    m_scopeOptions.clear();
    m_clubScopes.clear();
    m_scopedRecords.clear();
    m_summaryScope = m_togetherScope = clubScopeAllKey();
    m_scopeWorking = false;
    // And what their shots did; the request stays, as the club's does, and resolves once their tendencies land.
    // The options are their tendencies' counts, so they go first: Any alone until those land.
    clearTendencies();
    m_filter = ShotFilter();
    m_filterOptions = shotFilterOptionsFor(m_filter);
    m_shownOutcome = ShownOutcome();
    emit clubScopeChanged();
    emit clubScopeStateChanged();
    clearSummary();
    clearTendencies();
    publish();
    refresh();
}

void WorkOnsController::setGradePolicy(const QString &name)
{
    if (m_gradePolicy == name || name.isEmpty()) return;
    m_gradePolicy = name;
    emit gradePolicyChanged();
}

void WorkOnsController::setPaused(bool on)
{
    if (m_paused == on) return;
    m_paused = on;
    emit pausedChanged();
    // A reduction is all cores for seconds; it never runs beside capture. The summary on screen
    // stays, and the pause lifting computes it again.
    if (on) cancelThemes();
    if (on) setScopeWorking(false);
    if (on) cancelTendencies();
    // Nor does reading every ledger for the consistency marks; the pause lifting grades them again.
    if (on) cancelMarks();
    // The athlete level's jobs (refactor 3.7-f): let go, nothing asked while live.
    if (owner()) owner()->setJobsPaused(on);
    if (on && m_ownerThemesRunning) { m_ownerThemesRunning = false; emit summaryChanged(); }
    // NOR THE CATCH-UP IN FLIGHT (audit D-15; refactor 3.7-e): the catch-up service lets go of the session in hand at once
    // (released: what it kept is written, its waiting jobs cancelled) and keeps it at the front of its queue for the pause
    // lifting — which rescans anyway (below), so the queue is asked again then.
    if (m_catchUp.pause) m_catchUp.pause(on);
    // Unpausing is a session ending: the one moment a session is certain to have gone stale.
    if (!on) refresh();
}

// ── Scan ────────────────────────────────────────────────────────────────────────────────

void WorkOnsController::refresh()
{
    if (m_scanning) { m_rescan = true; return; }
    if (m_athleteDir.isEmpty()) return;

    const bool was = updating();
    m_scanning = true;
    if (!was) emit updatingChanged();

    const int generation = ++m_generation;
    const QString dir = m_athleteDir;
    // What a summary is produced under, as the owner computes it (R-STAMP-1; refactor 3.7-e round 2): the rule data, taken
    // on this thread once per RuleData snapshot, and the athlete record's cohort facts (the catch-up's door).
    const QString rules  = rulesStampNow();
    const QString cohort = m_catchUp.cohortFacts ? m_catchUp.cohortFacts() : pinpoint::summaryCohortFacts(QString(), QString());

    if (m_synchronous) {
        applyScan(generation, scan(dir, rules, cohort));
        return;
    }
    m_pool.start([this, generation, dir, rules, cohort]() {
        std::vector<Scanned> rows = scan(dir, rules, cohort);
        QMetaObject::invokeMethod(this, [this, generation, rows = std::move(rows)]() mutable {
            applyScan(generation, std::move(rows));
        }, Qt::QueuedConnection);
    });
}

// Worker thread. Touches no member.
//
// THE FINGERPRINT is everything a record depends on (FIELD-LIST §1.8, R-STAMP-1; ONE RULE since refactor 3.7-e,
// session_reduction.h summaryFingerprintOf, which each session's owner writes into its summary): every rule version the
// summary is produced under, the rule data (pack, norms, grade policy, disabled sets, flight norms), the session's frozen
// club baseline, the athlete's cohort facts, and every swing document's identity (summaryFingerprintFor). Not diagnostics.json's own stamp — the
// owner rewrites that file for reasons that change no verdict (a focus declared, a screen answered). (The athlete level's
// own fingerprints are AthleteDiagnostics', over the summaries: refactor 3.7-f.)
// FRESH = this build's summary (read strictly: today's schema and rule), with DEC-192's per-shot facts made under the
// current facts' rules (`factsRules`: CARRY 3.6 review 2 — so a version bump alone stales it), whose fingerprint is the
// one the folder gives now.
QString WorkOnsController::summaryFingerprintFor(const QString &sessionDir, const QString &rulesStamp, const QString &cohort)
{
    // The session's swings as its Session holds them: NUMBERED swing folders only (SwingPaths::swingNumberOf; review R1-m2
    // (a): a stray "swing_tmp" is no shot, so it must not stale the summary for ever).
    std::vector<SummarySwing> stamps;
    for (const QString &name : pinpoint::SwingPaths::swingFolders(sessionDir)) {
        if (pinpoint::SwingPaths::swingNumberOf(name) <= 0) continue;
        const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(sessionDir + QLatin1Char('/') + name);
        stamps.push_back(SummarySwing{ name, doc.exists(), doc.size, doc.mtimeMs });
    }
    // The frozen club baseline as the owner holds it: a current file's clubs; none for no file, an older rule's or an
    // unreadable one.
    // (summary_rules.h frozenClubBaselineOf: the athlete level's freshness check reads it alike, REVIEW-f R1-B1.)
    SummaryRules rules;
    rules.rules    = rulesStamp;
    rules.baseline = clubBaselineStamp(pinpoint::frozenClubBaselineOf(SessionDiagnosticsFiles::readClubBaseline(sessionDir)));
    rules.cohort   = cohort;
    return summaryFingerprintOf(rules, std::move(stamps));
}

std::vector<WorkOnsController::Scanned> WorkOnsController::scan(const QString &athleteDir, const QString &rulesStamp,
                                                                const QString &cohort)
{
    std::vector<Scanned> out;
    // Each session's day and place in it, SessionIdentity's (start times; a legacy folder by its name, read
    // there): the catch-up's order (step 2.7a, audit F-06).
    const QList<pinpoint::SessionIndexEntry> index = SessionDiagnosticsFiles::athleteSessions(athleteDir);
    const QFileInfoList sessions = QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : sessions) {
        const QString sessionDir = fi.absoluteFilePath();
        // SwingPaths' one listing, by name (so by swing number): the fingerprint hashes them in this order.
        QStringList swings = pinpoint::SwingPaths::swingFolders(sessionDir);
        std::sort(swings.begin(), swings.end());
        if (swings.isEmpty()) continue;     // nothing was hit; there is nothing to say

        Scanned s;
        s.sessionDir  = sessionDir;
        s.fingerprint = summaryFingerprintFor(sessionDir, rulesStamp, cohort);
        s.mtimeMs     = fi.lastModified().toMSecsSinceEpoch();
        // The baseline's absence queues the session (applyScan) for its freeze; its value is in the fingerprint (R-STAMP-1).
        s.hasBaseline = SessionDiagnosticsFiles::clubBaselineExists(sessionDir)
                     && !clubBaselineIsOutdated(SessionDiagnosticsFiles::readClubBaseline(sessionDir).root);
        const SessionPlace place = SessionDiagnosticsFiles::placeOf(index, sessionDir);
        s.day         = place.day;
        s.nOfDay      = place.nOfDay;

        const QFileInfo summaryFile(SessionFiles::workOnsFile(sessionDir));
        if (summaryFile.exists()) {
            s.summarySize    = summaryFile.size();
            s.summaryMtimeMs = summaryFile.lastModified().toMSecsSinceEpoch();
        }
        const pinpoint::JsonFileRead f = SessionDiagnosticsFiles::readWorkOns(sessionDir);
        if (f.exists) {
            bool ok = false;
            const SessionResults r = sessionResultsFromJson(f.root, SessionResultsRead::Strict, &ok);
            s.fresh  = ok && r.hasFacts && r.derivedFrom.fingerprint == s.fingerprint;
            s.newer  = sessionResultsFromNewerBuild(f.root);   // R-PERS-3 (4-e): a newer build's session, not asked for
        }
        out.push_back(std::move(s));
    }
    return out;
}

void WorkOnsController::applyScan(int generation, std::vector<Scanned> rows)
{
    m_scanning = false;
    if (generation != m_generation || m_rescan) {      // the athlete moved on, or was asked again
        m_rescan = false;
        emit updatingChanged();
        refresh();
        return;
    }

    m_scanned.clear();
    m_dirMtime.clear();
    m_sessionsFound = int(rows.size());
    // A SUMMARY WRITTEN OUTSIDE THIS PROCESS (another machine over the share, a library copied in; DUTIES-f A1) is heard by
    // no signal: the owner is told to read again each one whose file moved since the last scan of this athlete.
    QHash<QString, QString> stamps;
    for (const Scanned &r : rows)
        stamps.insert(r.sessionDir, QStringLiteral("%1:%2").arg(r.summarySize).arg(r.summaryMtimeMs));
    if (owner() && m_stampsOf == m_athleteDir) {
        for (auto it = stamps.constBegin(); it != stamps.constEnd(); ++it)
            if (m_summaryStamps.value(it.key()) != it.value()) markSummaryStale(it.key());
        if (stamps.size() != m_summaryStamps.size()) owner()->markStale();   // a session came or went: the index again
    }
    m_summaryStamps = std::move(stamps);
    m_stampsOf      = m_athleteDir;

    // Newest first: the latest session is the one the golfer is about to look for. BUT while any session
    // still lacks its frozen baseline, oldest first by THE SESSION'S DAY and its place in it (Contract A;
    // SessionIdentity's, step 2.7a): a session's baseline is read off its predecessors' rows, so they have to
    // be current before it is frozen. A folder's mtime says nothing about when it was played.
    const bool catchingUp = std::any_of(rows.begin(), rows.end(), [](const Scanned &s) { return !s.hasBaseline && !s.newer; });
    if (catchingUp)
        std::sort(rows.begin(), rows.end(), [](const Scanned &a, const Scanned &b) {
            if (a.day.isValid() != b.day.isValid()) return a.day.isValid();
            if (a.day != b.day) return a.day < b.day;
            if (a.nOfDay != b.nOfDay) return a.nOfDay < b.nOfDay;
            return a.sessionDir < b.sessionDir;
        });
    else
        std::sort(rows.begin(), rows.end(),
                  [](const Scanned &a, const Scanned &b) { return a.mtimeMs > b.mtimeMs; });
    QStringList queue;
    QList<CatchUpAsk> asks;
    for (Scanned &s : rows) {
        m_scanned.insert(s.sessionDir);
        m_dirMtime.insert(s.sessionDir, s.mtimeMs);
        // A NEWER BUILD'S SESSION is never asked for (R-PERS-3; REVIEW-e §5): nothing here may load, re-grade or write it.
        // The owner still counts it, and the note names it ("can't read it in full yet").
        const bool needsDerive = !s.newer && (!s.fresh || !s.hasBaseline);
        if (needsDerive) {
            queue << s.sessionDir;
            asks << CatchUpAsk{ s.sessionDir, /*baselineOnly=*/s.fresh };
        }
    }
    // A catch-up in flight is asked again with the new set (the service drops an ask it already holds).
    m_queue = queue;

    publish();
    emit updatingChanged();
    if (m_queue.isEmpty() || m_paused) {
        if (m_queue.isEmpty()) catchUpDone();
        return;
    }
    if (!m_catchUp.run) {
        // No catch-up service (a test or tool that gave none): nothing derives a stale session here, so its record stays
        // out of the list, as one that never had one.
        ppInfo() << "[WorkOns] no catch-up service: " << m_queue.size() << "stale session(s) left as they are";
        m_queue.clear();
        catchUpDone();
        return;
    }
    m_deriving = true;
    emit updatingChanged();
    m_catchUp.run(m_athleteDir, asks);   // each session's sessionCaughtUp, then catchUpDone (inline when synchronous)
}

// ── The catch-up's answers (refactor 3.7-e) ───────────────────────────────────────────────────────────────────

// A SESSION THE CATCH-UP HAS LET GO: its summary was written by its owner (or it could not be taken: another golfer's id in
// its file, J-a3; no Session for its folder). Its work_ons.json is read again, by today's strict reader, and whatever it
// holds is this list's record of it — the owner's new one, or the record the file already held (shown, never hidden: a
// session no Session holds here is summed up by the machine that recorded it).
void WorkOnsController::sessionCaughtUp(const QString &answered)
{
    // BY FOLDER NAME, NEVER A PATH SPELLING (refactor 3.7-f round 0c and REVIEW-f R1-m10; the 2aa5bc72 double count, NOTES-f
    // "Round 0c"): the doors answer with the session's folder name (main.cpp: the caught-up session's id looked up in the
    // index; a folder no Session holds, the scan's own name for it). A session is this list's by that name, under the scan's
    // own spelling (dirOfFolder). A path is still taken (a tool's door): its last part is the name, whatever its spelling.
    const QString name = answered.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    const QString sessionDir = dirOfFolder(name);
    const bool ours = !m_athleteDir.isEmpty() && m_dirMtime.contains(sessionDir);
    if (!m_queue.removeAll(sessionDir) && !m_deriving)
        return;   // not one this scan asked for (a cancelled athlete's)
    if (ours && owner()) {
        // Its owner wrote the summary (heard: AthleteDiagnostics marks it stale), or nothing here holds it (J-a3: its file
        // is what it is): either way the owner reads it again, and the list is the owner's.
        markSummaryStale(sessionDir);
        publish();
    }
    emit updatingChanged();
}

// THE CATCH-UP IS DONE (or there was nothing to do): every record is current, every ledger written by its owner, so the
// swing summary, the consistency marks and the tendencies can be read off them.
void WorkOnsController::catchUpDone()
{
    const bool was = updating();
    m_deriving = false;
    if (!m_paused) m_queue.clear();
    if (was != updating()) emit updatingChanged();
    if (!m_queue.isEmpty()) return;
    if (m_ownerDirty) { m_ownerDirty = false; publish(); }
    else refreshNotSummedUp();   // 4-e: the catch-up is done, so the note says what is still left out (or clears)
    maybeThemes();
    maybeMarks();
    maybeTendencies();
}

bool WorkOnsController::waitForIdle(int msTimeout)
{
    // IDLE MEANS STILL IDLE AFTER A TURN OF THE EVENT LOOP. Deriving hands the next session to a
    // zero-timer, and the summary starts from the end of that call — so for one turn after the
    // last derivation nothing reads as busy although the reduction is about to begin.
    const auto busy = [this] {
        return (updating() && !(m_paused && !m_scanning && !m_deriving)) || m_themesRunning || m_marksRunning
            || m_tendenciesRunning || m_ownerThemesRunning || m_ownerChangePosted
            || (owner() && owner()->summariesReading());
    };
    QDeadlineTimer deadline(msTimeout);
    while (!deadline.hasExpired()) {
        if (!busy()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (!busy()) break;
            continue;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    return !m_scanning && !m_deriving && !m_themesRunning && !m_marksRunning && !m_tendenciesRunning && !m_ownerThemesRunning;
}

// ── Publish ─────────────────────────────────────────────────────────────────────────────

void WorkOnsController::publish()
{
    // R1-m9 a: this owner's list as it was until its summaries land; R2-B2: another owner's (a switch) emptied now.
    bool hold = false;
    pinpoint::AthleteLevel *src = publishSource(&hold);
    if (hold) return;
    // THE RECORDS ARE THE OWNER'S (refactor 3.7-f): every summary it read, by this list's session folder.
    m_records.clear();
    if (src)
        for (const pinpoint::AthleteSessionSummary &sum : src->summaries())
            if (sum.read) m_records.insert(dirOfFolder(sum.place.folder), sum.results.workOns);
    refreshNotSummedUp();
    const SessionPriorities *newest = nullptr;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        const SessionPriorities &r = it.value();
        // The newest by when it was played — the aggregate's own order (start, then id).
        if (!newest || r.startMs > newest->startMs || (r.startMs == newest->startMs && r.sessionId > newest->sessionId))
            newest = &r;
    }
    m_lastSession.clear();
    if (newest)
        m_lastSession = QVariantMap{
            { QStringLiteral("label"), weekdayLabel(newest->startMs) },
            { QStringLiteral("shots"), newest->shotCount },
        };

    static const std::vector<PrioritySummary> kNone;
    const DescribedWorkOns all = describeWorkOns(m_records, src ? src->conditions() : kNone, src ? src->workOns() : kNone);
    m_items = all.items;
    m_clearedCount = all.cleared;

    publishScreens(m_summaryScope == clubScopeAllKey() ? &all : nullptr);
    emit itemsChanged();
    // The Tendencies tab's work-on column reads the list just rebuilt.
    publishTendencies();
    updateClubScope();
}

const QHash<QString, SessionPriorities> &WorkOnsController::shownRecords() const
{
    return m_summaryScope == clubScopeAllKey() ? m_records : m_scopedRecords;
}

void WorkOnsController::publishScreens(const DescribedWorkOns *known)
{
    followPackSave();
    const CharacteristicPack &pack = m_packProv->pack();

    const QHash<QString, SessionPriorities> &records = shownRecords();
    DescribedWorkOns scoped;
    if (!known) {
        if (&records == &m_records && owner()) {
            scoped = describeWorkOns(records, owner()->conditions(), owner()->workOns(), &m_shownOutcome);
        } else {
            // A scope's or a filter's records (the owner's scoped result, DEC-195): their aggregates are worked out
            // here from those records, as before.
            std::vector<SessionPriorities> sessions;
            for (auto it = records.constBegin(); it != records.constEnd(); ++it) sessions.push_back(it.value());
            const std::vector<PrioritySummary> every = aggregateConditions(sessions);
            const std::vector<PrioritySummary> agg   = aggregatePriorities(std::move(sessions));
            scoped = describeWorkOns(records, every, agg, &m_shownOutcome);
        }
        known = &scoped;
    }
    m_details = known->details;
    ++m_detailsRevision;
    // What the consistency marks are asked for: each detail's, then each area item's below.
    m_markRequests = known->markRequests;

    // One call — the every-condition aggregate, and the same again without the newest substantive session for
    // the trend — per publish, on this thread; the aggregate's per-session evidence index keeps it linear.
    std::vector<SessionPriorities> sessions;
    sessions.reserve(size_t(records.size()));
    for (auto it = records.constBegin(); it != records.constEnd(); ++it) sessions.push_back(it.value());
    publishAreas(pack, sessions);

    emit detailsChanged();
    emit areasChanged();
}

WorkOnsController::DescribedWorkOns WorkOnsController::describeWorkOns(const QHash<QString, SessionPriorities> &records,
                                                                      const std::vector<PrioritySummary> &every,
                                                                      const std::vector<PrioritySummary> &agg,
                                                                      const ShownOutcome *outcome)
{
    followPackSave();
    const CharacteristicPack &pack = m_packProv->pack();
    const DrillSet &drills = sharedDrillSet();

    std::vector<SessionPriorities> sessions;
    sessions.reserve(size_t(records.size()));
    QHash<QString, QString> dirOf;      // session id -> dir
    QSet<QString> substantive;          // sessions that could say something about something
    for (auto it = records.constBegin(); it != records.constEnd(); ++it) {
        const SessionPriorities &r = it.value();
        sessions.push_back(r);
        dirOf.insert(r.sessionId, it.key());
        if (r.substantive()) substantive.insert(r.sessionId);
    }
    int totalSwings = 0;
    for (const SessionPriorities &s : sessions) totalSwings += s.shotCount;
    QHash<QString, MarksRequest> markRequests;

    // Every condition any session assessed, for the details Home opens in place; then the list (the caller's aggregates:
    // the owner's, or the scoped job's).

    const QString hand = golferHandedness();
    const auto nameOf = [&pack, &hand](const QString &id, const QString &fallback = QString()) {
        return packLabel(pack, id, hand, fallback);
    };

    // One condition's summary in words: a work-on row, or the detail of any condition.
    const auto describe = [&](const PrioritySummary &w) {
        const Condition *cond = pack.condition(w.id);
        QVariantMap m;
        m[QStringLiteral("id")]        = w.id;
        m[QStringLiteral("name")]      = nameOf(w.id, w.name);
        m[QStringLiteral("status")]    = priorityStatusToString(w.status);
        // The swing area it belongs to: the pack's ConditionGroup token, the same token as an
        // areaItems entry's `group`, so a work-on can be placed in its area ("" when the
        // pack no longer holds the condition).
        m[QStringLiteral("group")]     = cond ? conditionGroupName(cond->group) : QString();
        // SWINGS, AND ONE DENOMINATOR FOR EVERY ROW: every swing in the golfer's sessions. A
        // per-fault denominator ("26 of 26" beside "41 of 55") reads as two different histories;
        // the swings this fault could not be judged on are said in the detail instead.
        m[QStringLiteral("countText")] = swingsText(w.swingsFired, totalSwings);
        const int unjudged = std::max(0, totalSwings - w.swingsAssessable);
        m[QStringLiteral("coverageText")] =
            unjudged == 0 ? QString() : QStringLiteral("not measurable on %1 of them").arg(unjudged);
        // The glossed condition's sentence under its glossed name, never the row's under another's (B-2).
        m[QStringLiteral("consequence")] = conditionInGolfersHand(pack, w.id, hand).consequence;

        // One tick per session, oldest first, in PpTickRun's own vocabulary: a pattern is a
        // firing, a quiet session is clean, and a session that could not tell is the short
        // outlined tick — present, never a gap.
        QVariantList ticks;
        for (int i = 0; i < int(w.history.size()); ++i) {
            const SessionEvidence st = w.history[size_t(i)].state;
            ticks.append(QVariantMap{
                { QStringLiteral("state"), st == SessionEvidence::Pattern ? QStringLiteral("fired")
                                         : st == SessionEvidence::Quiet   ? QStringLiteral("clean")
                                         :                                  QStringLiteral("notAssessable") },
                { QStringLiteral("shotId"), i },
                { QStringLiteral("selected"), false },
            });
        }
        m[QStringLiteral("ticks")] = ticks;

        // The status in words. An easing fault says what eased it; an active one that no recent
        // session could measure says so rather than passing for freshly confirmed.
        QString statusText;
        const PriorityPoint *lastPattern = w.latestPatternIndex >= 0 ? &w.history[size_t(w.latestPatternIndex)] : nullptr;
        const PriorityPoint *lastConclusive = w.latestConclusiveIndex >= 0 ? &w.history[size_t(w.latestConclusiveIndex)] : nullptr;
        // A later session that measured OTHER things and not this one. Three balls
        // that measured nothing are not a session this fault went missing from.
        bool unmeasuredSince = false;
        for (int i = w.latestConclusiveIndex + 1; i < int(w.history.size()); ++i)
            if (substantive.contains(w.history[size_t(i)].sessionId)) unmeasuredSince = true;
        if (w.status == PriorityStatus::Easing && lastConclusive)
            statusText = QStringLiteral("Easing — %1 of %2 measurable shots on %3")
                             .arg(lastConclusive->fired).arg(lastConclusive->assessable)
                             .arg(dayLabel(lastConclusive->startMs));
        else if (unmeasuredSince && lastConclusive)
            statusText = QStringLiteral("Not measured since %1").arg(dayLabel(lastConclusive->startMs));
        m[QStringLiteral("statusText")] = statusText;

        // The latest session that listed it — or, for one never listed, the newest that assessed
        // it: the count, and the reading where one can be stated.
        const auto latestLine = [&](const QString &sessionId, int fired, int assessable, bool hasTypical,
                                    const QString &measureId, double typical, double lo, double hi,
                                    CorridorShape shape) {
            qint64 when = 0;
            for (const PriorityPoint &p : w.history) if (p.sessionId == sessionId) when = p.startMs;
            QString text = QStringLiteral("%1 · %2").arg(dayLabel(when), recurrenceText(fired, assessable));
            if (hasTypical) {
                const Measure *mm = pack.measure(measureId);
                const QString unit = mm ? mm->unit : QString();
                // THE SAME SENTENCE THE PANEL PRINTS (session_spread.h spreadCorridorWords): the pass
                // band and the fault line. This used to quote the stored Ideal band as "at most
                // 4.3", which is neither edge the session panel draws nor where anything fires.
                ConditionRow band;
                band.corridorLo    = lo;
                band.corridorHi    = hi;
                band.corridorShape = shape;
                // On this condition's own end of the band (#16), as the panel states it.
                const SpreadCorridor sc = spreadCorridorOnSide(spreadCorridorFromRow(band, gradePolicyByName(m_gradePolicy)),
                                                               conditionFiringSide(pack, w.id, measureId));
                const QString corridor = sc.known ? spreadCorridorWords(sc, unit) : QString();
                if (!corridor.isEmpty())
                    text += QStringLiteral(" · typically %1 against %2")
                                .arg(pinpoint::figures::withUnit(pinpoint::figures::fig(typical, 1), unit), corridor);
            }
            return text;
        };
        QString latestText;
        if (w.hasLatest) {
            const PriorityEntry &e = w.latest;
            latestText = latestLine(w.latestSessionId, e.fired, e.assessable, e.hasTypical, e.measureId,
                                    e.typicalValue, e.corridorLo, e.corridorHi, e.corridorShape);
            QStringList causes, maybe;
            for (const QString &c : e.causedBy)  causes << nameOf(c);
            for (const QString &c : e.mayFollow) maybe  << nameOf(c);
            QStringList lines;
            if (!causes.isEmpty()) lines << QStringLiteral("Follows from ") + causes.join(QStringLiteral(", "));
            if (!maybe.isEmpty())
                lines << QStringLiteral("May follow from %1 — these swings cannot show it")
                             .arg(maybe.join(QStringLiteral(", ")));
            m[QStringLiteral("causedByText")] = lines.join(QStringLiteral(" · "));
        } else if (w.hasLatestEvidence) {
            const PriorityEvidence &e = w.latestEvidence;
            latestText = latestLine(w.latestEvidenceSessionId, e.fired, e.assessable, e.hasTypical, e.measureId,
                                    e.typicalValue, e.corridorLo, e.corridorHi, e.corridorShape);
        }
        m[QStringLiteral("latestText")] = latestText;

        // "What do I do about it" — the pack's first drill for this condition, when it authors one.
        const Drill *drill = nullptr;
        if (cond)
            for (const QString &id : cond->drills)
                if ((drill = drills.drill(id))) break;
        m[QStringLiteral("drillLabel")]       = drill ? drill->label : QString();
        m[QStringLiteral("drillInstruction")] = drill ? drill->instruction : QString();

        // What it does to the golfer's shots (§7.4), once the shot log has linked it to a result.
        m[QStringLiteral("onBall")] = onBallFor(w.id);

        // Its consistency mark, on the measure an area item names (AreaItem::measureId's rule), so the
        // detail and its row agree. It sits beside the work-on's status (R10).
        const QString measureId = (w.hasLatestEvidence && !w.latestEvidence.measureId.isEmpty())
                                      ? w.latestEvidence.measureId
                                      : (w.hasLatest ? w.latest.measureId : QString());
        m[QStringLiteral("consistency")] = markFor(w.id, measureId);
        const QString key = markKey(w.id, measureId);
        if (!markRequests.contains(key)) {
            const Measure *mm = pack.measure(measureId);
            markRequests.insert(key, MarksRequest{ w.id, measureId, mm ? mm->unit : QString() });
        }

        // Where to go and look: the newest session in which it was a pattern.
        m[QStringLiteral("sessionDir")]   = lastPattern ? dirOf.value(lastPattern->sessionId) : QString();
        m[QStringLiteral("sessionLabel")] = lastPattern ? dayLabel(lastPattern->startMs) : QString();
        return m;
    };

    // How often it fired on the outcome choice's shots against the golfer's other ones: the detail's alone, so
    // the list stays as it is under every choice.
    const auto withContrast = [outcome](QVariantMap m, const QString &id) {
        m[QStringLiteral("outcomeContrast")] = outcome ? outcomeContrastFor(*outcome, id) : QString();
        return m;
    };

    QVariantList items;
    int cleared = 0;
    QHash<QString, QVariantMap> details;
    for (const PrioritySummary &w : every) {
        // A status read off evidence alone: a never-listed pattern would say Active. Only the list
        // speaks of work-on status, so a condition that is not on it says nothing in that line.
        QVariantMap m = describe(w);
        if (w.sessionsListed == 0) m[QStringLiteral("statusText")] = QString();
        details.insert(w.id, withContrast(m, w.id));
    }
    for (const PrioritySummary &w : agg) {
        if (w.status == PriorityStatus::Cleared) { ++cleared; continue; }
        const QVariantMap m = describe(w);
        details.insert(w.id, withContrast(m, w.id));     // a listed one's detail is its row, plus the contrast
        items.append(m);
    }

    DescribedWorkOns out;
    out.items   = std::move(items);
    out.details = std::move(details);
    out.markRequests = std::move(markRequests);
    out.cleared = cleared;
    return out;
}

// ── Club scope ──────────────────────────────────────────────────────────────────────────

void WorkOnsController::setClubScope(const QString &key)
{
    if (key == m_scopeRequested) return;
    m_scopeRequested = key;
    updateClubScope();
}

void WorkOnsController::setScopeWorking(bool on)
{
    if (m_scopeWorking == on) return;
    m_scopeWorking = on;
    emit clubScopeStateChanged();
}

// The options, from the clubs the records name; then what the asked-for key resolves to among them. Last in
// publish(), because a scope that moves can land inline (the test seam) and publish again.
void WorkOnsController::updateClubScope()
{
    QStringList clubs;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) clubs += it->clubs;
    if (!clubs.isEmpty() || !m_scopeOptions.empty()) {
        const std::shared_ptr<const INormProvider> norms = sharedNormProvider();   // held while `tree` is read
        const ContextTree &tree = norms->contexts();
        m_scopeOptions = clubScopeOptions(clubs, [&tree](const QString &club) { return tendencyClubGroup(tree, club); });
    }

    QVariantList list;
    for (const ClubScopeGroupOption &g : m_scopeOptions) {
        QVariantList groupClubs;
        for (const ClubScopeOption &c : g.clubs)
            groupClubs.append(QVariantMap{ { QStringLiteral("value"), c.value }, { QStringLiteral("label"), c.label } });
        list.append(QVariantMap{ { QStringLiteral("value"), g.value }, { QStringLiteral("group"), g.group },
                                 { QStringLiteral("label"), g.label },
                                 { QStringLiteral("clubs"), groupClubs } });
    }

    ClubScope next = resolveClubScope(m_scopeRequested, m_scopeOptions);
    const bool clubMoved = next.key != m_scope.key || next.clubIds != m_scope.clubIds;
    const bool clubListMoved = list != m_clubScopes;
    m_clubScopes = list;
    m_scope = std::move(next);

    // The filter's options are the club choice's, so they follow it: first what the asked-for filter resolves to
    // there, part by part, then each factor's options counted under the other two choices.
    ShotFilter nextFilter = resolveShotFilter(shotFilterFromKey(m_filterRequested));
    const QVariantMap options = shotFilterOptionsFor(nextFilter);
    const bool filterMoved = nextFilter.key() != m_filter.key();
    const bool optionsMoved = options != m_filterOptions;
    if (!clubMoved && !clubListMoved && !filterMoved && !optionsMoved) return;
    m_filterOptions = options;
    m_filter = std::move(nextFilter);
    emit clubScopeChanged();
    if (clubMoved || filterMoved) onClubScopeMoved(clubMoved);
}

// A grade belongs to the shots it read, so the old scope's stop showing at once ("-" until the new ones land).
// The marks read the club choice's shots only, so the shot filter moving alone keeps them.
void WorkOnsController::onClubScopeMoved(bool clubMoved)
{
    if (clubMoved) {
        cancelMarks();
        m_marks.clear();
        m_marksFingerprint.clear();
    }
    emit clubScopeStateChanged();
    maybeThemes();
}

void WorkOnsController::setShotFilter(const QString &key)
{
    if (key == m_filterRequested) return;
    m_filterRequested = key;
    updateClubScope();
}

QString WorkOnsController::scopeKeyOf(const ClubScope &club, const ShotFilter &filter)
{
    return filter.isAll() ? club.key : club.key + QLatin1Char('|') + filter.key();
}

// Each part the club choice's judged shots have shown is kept; any other is Any. The tuples are the tendency
// reduction's (TendencySet::clubOutcomes), so with none in hand nothing is chosen.
ShotFilter WorkOnsController::resolveShotFilter(const ShotFilter &asked) const
{
    ShotFilter out;
    if (!m_tendenciesReady || asked.isAll()) return out;
    for (const QString &name : shotFactorNames()) {
        const QString &want = asked.choice(name);
        if (want == shotFilterAny()) continue;
        bool seen = false;
        for (const TendencyClubOutcomes &c : m_tendencySet.clubOutcomes) {
            if (!m_scope.contains(c.clubId)) continue;
            seen = std::any_of(c.tuples.begin(), c.tuples.end(), [&](const TendencyFactorTuple &t) {
                return factorTupleValues(t, name).contains(want);
            });
            if (seen) break;
        }
        if (!seen) continue;
        if (name == shotFactorStrike())         out.strike = want;
        else if (name == shotFactorDirection()) out.direction = want;
        else                                    out.flight = want;
    }
    return out;
}

// Per factor: Any, then every value the club choice's judged shots show, in the table's order (shotFactorValueRank).
// Each count is the shots that choice leaves with the other two factors' choices in `chosen` kept: Any's, the
// shots the other two hold; a value's, those of them that show it. A shot showing two values (a toe and a weak
// strike) is under both. A value that leaves none is still listed, with 0.
QVariantMap WorkOnsController::shotFilterOptionsFor(const ShotFilter &chosen) const
{
    const ShotOutcomePack *pack = m_tendenciesReady ? &outcomePack() : nullptr;
    QVariantMap out;
    for (const QString &name : shotFactorNames()) {
        int any = 0;
        QStringList ids;
        QHash<QString, int> count;
        if (m_tendenciesReady)
            for (const TendencyClubOutcomes &c : m_tendencySet.clubOutcomes) {
                if (!m_scope.contains(c.clubId)) continue;
                for (const TendencyFactorTuple &t : c.tuples) {
                    const bool held = factorTupleHolds(t, chosen, name);
                    if (held) any += t.shots;
                    for (const QString &id : factorTupleValues(t, name)) {
                        if (!ids.contains(id)) ids << id;
                        if (held) count[id] += t.shots;
                    }
                }
            }
        std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
            const int ra = shotFactorValueRank(a), rb = shotFactorValueRank(b);
            return ra != rb ? ra < rb : a < b;
        });
        QVariantList list{ QVariantMap{ { QStringLiteral("value"), shotFilterAny() }, { QStringLiteral("label"), QStringLiteral("Any") },
                                        { QStringLiteral("count"), any } } };
        for (const QString &id : std::as_const(ids))
            list.append(QVariantMap{ { QStringLiteral("value"), id }, { QStringLiteral("label"), shotFactorValueLabel(*pack, id) },
                                     { QStringLiteral("count"), count.value(id) } });
        out.insert(name, list);
    }
    return out;
}

// The club choice as the word before "shots": a group's shot word, an iron group's hyphenated ("mid-iron"),
// a club's own name ("7 iron", "driver").
QString WorkOnsController::clubScopeWords() const
{
    if (m_scope.isAll()) return QString();
    for (const ClubScopeGroupOption &g : m_scopeOptions) {
        if (g.value == m_scope.key) {
            QString word = tendencyClubShotWord(g.group);
            if (word.endsWith(QLatin1String(" iron"))) word.replace(word.size() - 5, 1, QLatin1Char('-'));
            return word;
        }
        for (const ClubScopeOption &c : g.clubs)
            if (c.value == m_scope.key) return c.label.toLower();
    }
    return QString();
}

// "Your pulled, thin mid-iron shots: 11 over 5 sessions, against 31 others. Your swing was measured on 3 of them."
QString WorkOnsController::shotFilterLine() const
{
    const ShownOutcome &o = m_shownOutcome;
    if (o.scope.isAll()) return QString();
    QString what = filterWords(o.scope);
    if (!o.clubWords.isEmpty()) what += QLatin1Char(' ') + o.clubWords;
    const QString against = o.others <= 0 ? QStringLiteral("and no others")
                          : QStringLiteral("against ") + outcomeCountText(o.others, QStringLiteral("other"), QStringLiteral("others"));
    // A choice with no shots under it (each factor's values are listed whatever the others leave) says so, and
    // nothing about measuring them.
    if (o.chosen <= 0) return QStringLiteral("Your %1 shots: none yet, %2.").arg(what, against);
    // The second sentence says how many of them had the swing measured: a launch-monitor-only session judges the
    // ball but assesses no fault, and an empty page must not read as "no faults".
    const bool one = o.chosen == 1;
    const QString measured = o.measured <= 0          ? (one ? QStringLiteral("Your swing was not measured on it.")
                                                             : QStringLiteral("Your swing was not measured on any of them."))
                           : o.measured >= o.chosen   ? (one ? QStringLiteral("Your swing was measured on it.")
                                                             : QStringLiteral("Your swing was measured on all of them."))
                                                      : QStringLiteral("Your swing was measured on %1 of them.").arg(o.measured);
    return QStringLiteral("Your %1 shots: %2 over %3, %4. %5")
        .arg(what, QString::number(o.chosen),
             outcomeCountText(o.sessions, QStringLiteral("session"), QStringLiteral("sessions")), against, measured);
}

// The contrast's own floor for "too few other shots", applied to the chosen shots the swing was measured on.
QString WorkOnsController::outcomeTooFewNote() const
{
    const ShownOutcome &o = m_shownOutcome;
    if (o.scope.isAll() || o.measured >= TendencyOptions{}.minSessionShots) return QString();
    return QStringLiteral("Too few of these shots had your swing measured to say what comes with them yet.");
}

QString WorkOnsController::outcomeContrastFor(const ShownOutcome &shown, const QString &conditionId)
{
    if (shown.scope.isAll()) return QString();
    const auto it = shown.contrasts.constFind(conditionId);
    if (it == shown.contrasts.constEnd()) return QString();
    // The filter's words, which end where "shots" follows: "On your pulled, thin shots 8 of 11 · other shots 12 of 31".
    return outcomeContrastLine(*it, filterWords(shown.scope));
}

// Whether a line tied to a tendency belongs to the club choice on screen. The tendency's key is its row,
// "<club>|<length>" ("7 IRON|full"); the fault it feeds is the row's factor (TendencyOnBall::factor, ::value),
// not in the key, so the club is the key's first part.
bool WorkOnsController::onBallInScope(const QString &tendencyKey) const
{
    return tendencyKey.isEmpty() || m_scope.contains(tendencyKey.section(QLatin1Char('|'), 0, 0));
}

// The areas of "Your swing" (swing_areas.h), every string final but the grade's colour. Built from
// the records, not the themes, so it is current whenever the list is. No session with a record: no
// rows at all — eight areas reading "not measured" would blame a rig for a library with nothing in it.
void WorkOnsController::publishAreas(const CharacteristicPack &pack, const std::vector<SessionPriorities> &sessions)
{
    m_areaItems.clear();
    if (sessions.empty()) return;

    const GradePolicy policy = gradePolicyByName(m_gradePolicy);
    const QString hand = golferHandedness();   // the one gloss's hand, for every name and phrase below (B-2)
    // Every condition in an area, not the first kAreaMaxItems: Home's open area is the only list
    // of them now, each opening in place (Mark, 28 Oct).
    AreaOptions opt;
    opt.maxItems = std::numeric_limits<int>::max();
    for (const AreaSummary &a : summariseAreasForPack(&pack, sessions, opt)) {
        ConditionGroup g;
        const QString label = conditionGroupFromName(a.group, g) ? conditionGroupLabel(g) : a.group;

        QVariantList items;
        items.reserve(qsizetype(a.items.size()));
        for (const AreaItem &it : a.items) {
            // THE SAME EDGES the work-on row's corridor words come from (spreadCorridorFromRow): the
            // stored band is the policy's Ideal band, and the rest is recovered from it.
            QString grade;
            if (it.hasTypical) {
                ConditionRow band;
                band.corridorLo    = it.corridorLo;
                band.corridorHi    = it.corridorHi;
                band.corridorShape = corridorShapeFromString(it.corridorShape);
                grade = gradeOnCorridor(watchingTail(spreadCorridorFromRow(band, policy), it.tail), it.typical);
            } else if (areaBinaryFires(it)) {
                // A yes/no condition (an order test, a peak after impact) on its typical swing: no band
                // to grade a distance on, so a firing is Action and a clean one is left ungraded.
                grade = gradeName(Grade::Action);
            }
            // Worded for the golfer's hand by the one function, as the area's topFault is (B-2): a left-hander's hook
            // row reads "Slice" here and in its detail. `conditionId` stays the row's: it opens that row's detail.
            const ConditionInHand words = conditionInGolfersHand(pack, it.conditionId, hand);
            items.append(QVariantMap{
                { QStringLiteral("conditionId"),    it.conditionId },
                { QStringLiteral("label"),          !words.label.isEmpty() ? words.label : it.label },
                { QStringLiteral("golfer"),         words.condition ? words.golfer : it.golfer },
                { QStringLiteral("band"),           areaBandToString(it.band) },
                { QStringLiteral("share"),          it.share },
                { QStringLiteral("fired"),          it.fired },
                { QStringLiteral("assessable"),     it.assessable },
                { QStringLiteral("measureId"),      it.measureId },
                { QStringLiteral("hasTypical"),     it.hasTypical },
                { QStringLiteral("typical"),        it.typical },
                { QStringLiteral("corridorLo"),     it.corridorLo },
                { QStringLiteral("corridorHi"),     it.corridorHi },
                { QStringLiteral("corridorShape"),  it.corridorShape },
                { QStringLiteral("tail"),           it.tail },
                { QStringLiteral("unit"),           it.unit },
                { QStringLiteral("grade"),          grade },
                { QStringLiteral("workOn"),         it.workOn },
                { QStringLiteral("sessionsListed"), it.sessionsListed },
                // Its own consistency mark (R5), over the same window rule as the column's.
                { QStringLiteral("consistency"),    markFor(it.conditionId, it.measureId) },
            });
            m_markRequests.insert(markKey(it.conditionId, it.measureId), MarksRequest{ it.conditionId, it.measureId, it.unit });
        }
        // The Consistency column: the mark of the measure behind the biggest fault (R5). How that fault
        // is chosen is summariseAreas' (topFaultId), unchanged; ungraded with none.
        QVariant consistency = QVariant::fromValue(pinpoint::variability::VariabilityResultView());
        for (const AreaItem &it : a.items)
            if (!a.topFaultId.isEmpty() && it.conditionId == a.topFaultId) consistency = markFor(it.conditionId, it.measureId);

        m_areaItems.append(QVariantMap{
            { QStringLiteral("group"),         a.group },
            { QStringLiteral("label"),         label },
            { QStringLiteral("measurable"),    a.measurable },
            { QStringLiteral("measured"),      a.measured },
            { QStringLiteral("habits"),        a.habits },
            { QStringLiteral("occasional"),    a.occasional },
            { QStringLiteral("clean"),         a.clean },
            { QStringLiteral("unclear"),       a.unclear },
            { QStringLiteral("habitTrend"),    a.habitTrend },
            { QStringLiteral("hasTrend"),      a.hasTrend },
            { QStringLiteral("topFault"),      a.topFaultId.isEmpty() ? QString() : packLabel(pack, a.topFaultId, hand) },
            { QStringLiteral("topFaultId"),    a.topFaultId },
            { QStringLiteral("workOnsActive"), a.workOnsActive },
            { QStringLiteral("workOnsNew"),    a.workOnsNew },
            { QStringLiteral("items"),         items },
            { QStringLiteral("consistency"),   consistency },
        });
    }
}

// ── Consistency marks ───────────────────────────────────────────────────────────────────

QString WorkOnsController::markKey(const QString &conditionId, const QString &measureId)
{
    return conditionId + QLatin1Char('\x1f') + measureId;
}

// The consistency grade the praise rule reads for a condition (step 3.6): the mark of the measure its area item (or
// its detail) names — the one Your swing shows beside it; none asked or none landed yet: ungraded (not praised).
PraiseGrade WorkOnsController::praiseGradeOf(const QString &conditionId) const
{
    for (auto it = m_markRequests.constBegin(); it != m_markRequests.constEnd(); ++it) {
        if (it->conditionId != conditionId) continue;
        const auto mark = m_marks.constFind(it.key());
        if (mark == m_marks.constEnd()) continue;
        if (mark->graded()) return PraiseGrade{ true, mark->grade(), mark->measurementLimited() };
    }
    return PraiseGrade{};
}

// The mark publish() shows for one condition on one measure: the last job's, else ungraded (no badge).
QVariant WorkOnsController::markFor(const QString &conditionId, const QString &measureId) const
{
    return QVariant::fromValue(m_marks.value(markKey(conditionId, measureId), pv::VariabilityResultView()));
}

// Called once the catch-up is done, beside maybeThemes. THE OWNER GRADES (refactor 3.7-f; J-52): this asks it for the
// marks of what the screens show (each detail's and area item's condition and measure) under the club choice; cheap when
// nothing moved (it answers Current).
void WorkOnsController::maybeMarks()
{
    if (m_athleteDir.isEmpty() || m_paused || m_scanning || m_deriving || !m_queue.isEmpty()) return;
    if (ownerReading()) return;   // R1-m9 a: asked again when the owner's summaries land
    if (clubScopeRefreshing()) return;      // the landing asks again, for the rows it brings
    if (!owner()) return;
    std::vector<pinpoint::AthleteLevel::ConsistencyRequest> requests;
    for (auto it = m_markRequests.constBegin(); it != m_markRequests.constEnd(); ++it)
        requests.push_back({ it->conditionId, it->measureId });
    ++m_marksJobs;
    m_marksRunning = true;
    switch (owner()->askConsistency(m_scope, requests)) {
    case pinpoint::AthleteLevel::Ask::Current: m_marksRunning = false; applyOwnerMarks(); break;
    case pinpoint::AthleteLevel::Ask::Paused:  m_marksRunning = false; break;
    case pinpoint::AthleteLevel::Ask::Landed:  break;   // applied as it landed
    case pinpoint::AthleteLevel::Ask::Running: break;
    }
}

// The marks on screen stay; one that lands for a scope no longer shown is not applied.
void WorkOnsController::cancelMarks()
{
    m_marksRunning = false;
}

// The owner's marks, as the screens show them: one result view per request (ungraded, no badge, for none). A landing that
// changes a mark publishes again — and "what you do well", whose praise reads them (step 3.6).
void WorkOnsController::applyOwnerMarks()
{
    if (!owner()) return;
    m_marksRunning = owner()->consistencyRunning();
    if (owner()->consistencyScopeKey() != m_scope.key) return;   // a scope since left
    QHash<QString, pv::VariabilityResultView> marks;
    const QHash<QString, pv::VariabilityResult> &graded = owner()->consistency();
    for (auto it = m_markRequests.constBegin(); it != m_markRequests.constEnd(); ++it) {
        const auto g = graded.constFind(pinpoint::AthleteLevel::consistencyKey(it->conditionId, it->measureId));
        marks.insert(it.key(), g == graded.constEnd() ? pv::VariabilityResultView() : pv::VariabilityResultView::fromResult(*g, it->unit, 1));
    }
    m_marksFingerprint = m_scope.key;
    if (marks == m_marks) return;
    m_marks = std::move(marks);
    if (m_summaryScope == clubScopeAllKey()) publish();
    else publishScreens();
    publishSummary();
}

// ── Your swing ──────────────────────────────────────────────────────────────────────────

// Called once the catch-up is done. Cheap when the summary on screen is already current.
void WorkOnsController::maybeThemes()
{
    if (m_athleteDir.isEmpty() || m_paused || m_scanning || m_deriving || !m_queue.isEmpty()) return;
    if (ownerReading()) return;   // R1-m9 a: asked again when the owner's summaries land

    // No session with a swing in it: nothing to sum up, and no file to write into the folder.
    if (m_scanned.isEmpty()) {
        cancelThemes();
        if (m_summaryReady) clearSummary();
        return;
    }

    // EVERY CLUB, EVERY SHOT: THE OWNER'S (refactor 3.7-f; J-53). A scope or a filter: the owner's scoped views (DEC-195),
    // below.
    if (m_scope.isAll() && m_filter.isAll()) {
        cancelThemes();         // a scoped job for a choice since left
        if (!owner()) return;
        const bool announce = clubScopeRefreshing() || matchupsRefreshing();
        m_ownerThemesRunning = true;
        switch (owner()->askThemes(announce)) {
        case pinpoint::AthleteLevel::Ask::Current:
            m_ownerThemesRunning = false;
            if (owner()->themesReady() && (!m_summaryReady || m_themesFingerprint != owner()->landedThemesFingerprint()
                                           || m_summaryScope != clubScopeAllKey() || m_togetherScope != clubScopeAllKey()))
                applyOwnerThemes(false);
            else
                emit summaryChanged();
            break;
        case pinpoint::AthleteLevel::Ask::Paused:
            m_ownerThemesRunning = false;
            emit summaryChanged();
            break;
        case pinpoint::AthleteLevel::Ask::Landed:
            break;   // applied as it landed
        case pinpoint::AthleteLevel::Ask::Running:
            if (announce) setScopeWorking(true);   // no stored result: the screens showing another scope are in for a wait
            emit summaryChanged();
            break;
        }
        return;
    }

    // A CLUB SCOPE OR A SHOT FILTER (refactor 3.7-f; Mark's J-50 answer, DEC-195): the owner's too, over each session's
    // judged rows as that session's own SessionDiagnostics holds them; its stored file read back when current.
    if (!owner()) return;
    const bool announce = clubScopeRefreshing() || matchupsRefreshing();
    m_themesRunning = true;
    switch (owner()->askScoped(m_scope, m_filter, announce)) {
    case pinpoint::AthleteLevel::Ask::Current:
        m_themesRunning = false;
        if (owner()->scopedResult().key == scopeKey()
            && (!m_summaryReady || m_summaryScope != scopeKey() || m_togetherScope != scopeKey()))
            applyOwnerScoped(false);
        else
            emit summaryChanged();
        break;
    case pinpoint::AthleteLevel::Ask::Paused:
        m_themesRunning = false;
        emit summaryChanged();
        break;
    case pinpoint::AthleteLevel::Ask::Landed:
        break;   // applied as it landed
    case pinpoint::AthleteLevel::Ask::Running:
        emit summaryChanged();
        break;
    }
}

// The summary on screen is left as it is; a scoped result that lands for a choice since left is not applied.
void WorkOnsController::cancelThemes()
{
    if (!m_themesRunning) return;
    m_themesRunning = false;
    emit summaryChanged();
}

// THE OWNER'S SCOPED VIEW LANDED (refactor 3.7-f; DEC-195): Layer 1 first (Coaching and Your swing) while Matchups keeps its
// answer, then the whole; the records (by this list's session folders) and the filter's counts, said in its words here.
void WorkOnsController::applyOwnerScoped(bool layerOne)
{
    if (!owner()) return;
    const pinpoint::AthleteLevel::ScopedResult &r = layerOne ? owner()->scopedLayerOne() : owner()->scopedResult();
    if (r.key != scopeKey()) return;   // a choice since left
    QHash<QString, SessionPriorities> records;
    for (auto it = r.records.constBegin(); it != r.records.constEnd(); ++it) records.insert(dirOfFolder(it.key()), it.value());
    ShownOutcome shown;
    shown.scope     = m_filter;
    shown.clubWords = clubScopeWords();
    shown.chosen    = r.chosen;
    shown.sessions  = r.sessions;
    shown.others    = r.others;
    shown.measured  = r.measured;
    shown.contrasts = r.contrasts;
    const QString key = scopeKey();
    if (layerOne) {
        m_scopedRecords = std::move(records);
        m_shownOutcome  = std::move(shown);
        m_summaryScope  = key;
        m_themes        = r.themes;
        m_summaryReady  = true;
        emit clubScopeStateChanged();
        publishSummary();
        publishScreens();
        maybeMarks();
        return;
    }
    m_themesRunning = owner()->scopedRunning();
    m_themes        = r.themes;
    m_themesFingerprint.clear();   // every club's is compared, not a choice's
    m_summaryReady  = true;
    m_scopedRecords = std::move(records);
    m_shownOutcome  = std::move(shown);
    m_summaryScope  = m_togetherScope = key;
    m_scopeWorking  = false;
    refreshNotSummedUp();   // a session whose rows could not be read is said (DEC-195; J-51)
    emit clubScopeStateChanged();
    publishSummary();
    publishScreens();
    maybeMarks();
}

// THE OWNER'S SWING SUMMARY LANDED (every club; refactor 3.7-f). Layer 1 (`layerOne`: the focus, "what you do well", next)
// first while Matchups keeps its answer, as the scoped job's early landing does; then the whole. Applied only while every
// club is what the screens ask for.
void WorkOnsController::applyOwnerThemes(bool layerOne)
{
    if (!owner() || !m_scope.isAll() || !m_filter.isAll()) return;
    if (layerOne) {
        m_scopedRecords.clear();
        m_shownOutcome = ShownOutcome();
        m_summaryScope = clubScopeAllKey();
        m_themes       = owner()->layerOne();
        m_summaryReady = true;
        emit clubScopeStateChanged();
        publishSummary();
        publishScreens();
        maybeMarks();
        return;
    }
    m_ownerThemesRunning = owner()->themesRunning();
    if (!owner()->themesReady()) { emit summaryChanged(); return; }
    const bool wasOtherScope = m_summaryScope != clubScopeAllKey();
    m_themes            = owner()->landedThemes();
    m_themesFingerprint = owner()->landedThemesFingerprint();
    m_summaryReady      = true;
    m_scopedRecords.clear();
    m_shownOutcome  = ShownOutcome();
    m_summaryScope  = m_togetherScope = clubScopeAllKey();
    m_scopeWorking  = false;
    refreshNotSummedUp();
    emit clubScopeStateChanged();
    publishSummary();
    if (wasOtherScope) {
        publishScreens();
        maybeMarks();
    }
}

// The words, made now from the stored structure: a phrase edited in the pack (or a drill in the
// drill set) shows at the next publish without a recompute. Every item a plain QVariantMap, so
// QML reads it by key. The view's needsWork (every needs-work fault, capped) is not published: the
// home screen draws the focus and what is next instead, and the faults by name are `items`.
void WorkOnsController::publishSummary()
{
    m_summarySubtitle.clear();
    m_focusItem.clear();
    m_doWellItems.clear();
    m_doWellBefore.clear();
    m_doWellPraised.clear();
    m_nextItems.clear();
    // While Layer 2 is still working for a new scope, Matchups keeps the answer it has.
    const bool together = !matchupsRefreshing();
    if (together) {
        m_togetherItems.clear();
        m_togetherNote.clear();
    }
    if (m_summaryReady) {
        followPackSave();
        // THE PRAISE RULE (refactor step 3.6; 04 step 3 item 5; D23): "what you do well" keeps a condition only when its
        // consistency grade — the mark Your swing shows beside it — is Repeatable or Grooved; ungraded is not praised
        // (still shown in range in its area). Applied to the stored ranked list before the view caps it.
        SwingThemes shown = m_themes;
        applyPraiseRule(shown.doWell, [this](const QString &id) { return praiseGradeOf(id); });
        for (const DoWellRow &r : m_themes.doWell) m_doWellBefore << r.conditionId;
        for (const DoWellRow &r : shown.doWell) m_doWellPraised << r.conditionId;
        // The drill set gives the focus its drill's name and instruction.
        const SwingSummaryView view = swingSummaryView(shown, themePhrases(m_packProv->pack(), sharedDrillSet()));
        const auto pips = [](const std::vector<bool> &v) {
            QVariantList out;
            out.reserve(qsizetype(v.size()));
            for (bool b : v) out.append(b);
            return out;
        };
        // One fault item, the same shape in the focus's "right now" and in "next on your list".
        const auto fault = [this, &pips](const NeedsWorkItem &n) {
            return QVariantMap{
                { QStringLiteral("text"),      n.text },
                { QStringLiteral("share"),     n.share },
                { QStringLiteral("frequency"), n.frequency },
                { QStringLiteral("trend"),     n.trend },
                { QStringLiteral("sessions"),  pips(n.pips) },
                // So Home's row can open the fault's detail in place (its work-on's `id`).
                { QStringLiteral("conditionId"), n.conditionId },
                // What it does to the ball, when an earned, confirmed tendency says (§7.3).
                { QStringLiteral("onTheBall"), onTheBallFor(n.conditionId) },
                // Under one outcome: how often on those shots against the golfer's others ("" under All).
                { QStringLiteral("outcomeContrast"), outcomeContrastFor(m_shownOutcome, n.conditionId) },
            };
        };

        m_summarySubtitle = view.subtitle;

        const FocusItem &f = view.focus;
        QVariantList aimFor, rightNow;
        for (const QString &a : f.aimFor) aimFor.append(a);
        for (const NeedsWorkItem &n : f.rightNow) rightNow.append(fault(n));
        m_focusItem = QVariantMap{
            { QStringLiteral("present"),       f.present },
            { QStringLiteral("title"),         f.title },
            { QStringLiteral("aimFor"),        aimFor },
            { QStringLiteral("rightNow"),      rightNow },
            { QStringLiteral("why"),           f.why },
            { QStringLiteral("practiseLabel"), f.practiseLabel },
            { QStringLiteral("practise"),      f.practise },
            { QStringLiteral("reason"),        f.reason },
            { QStringLiteral("reasonShort"),   f.reasonShort },
            // The condition the focus starts from: the first "right now" fault's id ("" when none).
            { QStringLiteral("focusConditionId"), f.rightNow.empty() ? QString() : f.rightNow.front().conditionId },
            { QStringLiteral("onTheBall"), f.rightNow.empty() ? QString() : onTheBallFor(f.rightNow.front().conditionId) },
            { QStringLiteral("outcomeContrast"),
              f.rightNow.empty() ? QString() : outcomeContrastFor(m_shownOutcome, f.rightNow.front().conditionId) },
        };

        for (const DoWellItem &d : view.doWell)
            m_doWellItems.append(QVariantMap{
                { QStringLiteral("text"),     d.text },
                { QStringLiteral("caption"),  d.caption },
                { QStringLiteral("share"),    d.share },
                { QStringLiteral("sessions"), pips(d.pips) },
            });
        for (const NeedsWorkItem &n : view.next)
            m_nextItems.append(fault(n));
        const std::vector<TogetherItem> noneYet;
        for (const TogetherItem &t : together ? view.together : noneYet) {
            QVariantList togetherFaults;
            for (const NeedsWorkItem &n : t.faults) togetherFaults.append(fault(n));
            m_togetherItems.append(QVariantMap{
                { QStringLiteral("tier"),       t.tier },
                { QStringLiteral("first"),      t.first },
                { QStringLiteral("second"),     t.second },
                { QStringLiteral("startStop"),  t.startStop },
                { QStringLiteral("startWords"), t.startWords },
                { QStringLiteral("trend"),      t.trend },
                { QStringLiteral("also"),       t.also },
                { QStringLiteral("faults"),     togetherFaults },
            });
        }
        if (together) m_togetherNote = view.note;
    }
    emit summaryChanged();
}

// ── Tendencies ──────────────────────────────────────────────────────────────────────────

const ShotOutcomePack &WorkOnsController::outcomePack() const
{
    followPackSave();   // drops the outcome pack when the pack it was marshalled from has been replaced
    if (!m_outcomePack)
        m_outcomePack = std::make_unique<ShotOutcomePack>(shotOutcomePackFor(&m_packProv->pack()));
    return *m_outcomePack;
}

// Called once the catch-up is done, beside maybeThemes and maybeMarks. THE OWNER REDUCES (refactor 3.7-f): from the
// summaries' per-shot facts, on its lane, and writes FILE-ATH-02; cheap when nothing moved (Current).
void WorkOnsController::maybeTendencies()
{
    if (m_athleteDir.isEmpty() || m_paused || m_scanning || m_deriving || !m_queue.isEmpty()) return;
    if (ownerReading()) return;   // R1-m9 a: asked again when the owner's summaries land

    // No session with a swing in it: nothing to reduce, and nothing to write into the folder.
    if (m_scanned.isEmpty() || !owner()) {
        cancelTendencies();
        if (m_tendenciesReady) {
            clearTendencies();
            updateClubScope();      // the shot filter's options were these tendencies' counts
        }
        return;
    }
    const bool wasRunning = m_tendenciesRunning;
    m_tendenciesRunning = true;
    if (!wasRunning) emit tendenciesChanged();
    switch (owner()->askTendencies()) {
    case pinpoint::AthleteLevel::Ask::Current:
        m_tendenciesRunning = false;
        if (owner()->tendenciesReady() && (!m_tendenciesReady || m_tendencyFingerprint != owner()->landedTendenciesFingerprint()))
            applyOwnerTendencies();
        else
            emit tendenciesChanged();   // the tendencies in hand stand
        break;
    case pinpoint::AthleteLevel::Ask::Paused:
        m_tendenciesRunning = false;
        emit tendenciesChanged();
        break;
    case pinpoint::AthleteLevel::Ask::Landed:
    case pinpoint::AthleteLevel::Ask::Running:
        break;
    }
}

// The tendencies on screen stay.
void WorkOnsController::cancelTendencies()
{
    if (!m_tendenciesRunning) return;
    m_tendenciesRunning = false;
    emit tendenciesChanged();
}

// THE OWNER'S TENDENCIES LANDED (refactor 3.7-f): written first by it (FILE-ATH-02), then said here.
void WorkOnsController::applyOwnerTendencies()
{
    if (!owner()) return;
    m_tendenciesRunning = owner()->tendenciesRunning();
    if (!owner()->tendenciesReady()) { emit tendenciesChanged(); return; }
    m_tendencySet         = owner()->landedTendencies();
    m_tendencyFingerprint = owner()->landedTendenciesFingerprint();
    m_tendenciesReady     = true;
    // As the marks do: the tendencies ride on faultDetail() and the focus, so a landing rebuilds both. publish()
    // ends in updateClubScope, which rebuilds the shot filter's options from these counts and resolves the request.
    publish();
    publishSummary();
    publishTendencies();
    emit tendenciesChanged();
    emit tendenciesLanded();   // after the owner's write: "Your distances" reads again on this cue (main.cpp)
    // A shot filter that stayed put is still read off the tendency fingerprint (a correction, an intent):
    // cheap when that summary is current.
    if (!m_filter.isAll()) maybeThemes();
}

void WorkOnsController::publishTendencies()
{
    QVariantList items;
    if (m_tendenciesReady) {
        QHash<QString, QString> workOnStatus;
        for (const QVariant &v : std::as_const(m_items)) {
            const QVariantMap m = v.toMap();
            workOnStatus.insert(m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("status")).toString());
        }
        // One row per club and swing length, and in it each factor: its usual, how often, and for a fault what it
        // is seen with, which way it is going and its work-on's status ("active", "easing"; "" for none).
        for (const TendencyItem &it : m_tendencySet.items) {
            QStringList rowStatuses;
            const auto factorMap = [&workOnStatus, &rowStatuses](const TendencyFactor &f) {
                const QString status = f.workOn.isEmpty() ? QString() : workOnStatus.value(f.workOn);
                if (!status.isEmpty() && !rowStatuses.contains(status)) rowStatuses << status;
                return QVariantMap{
                    { QStringLiteral("factor"),     f.factor },
                    { QStringLiteral("value"),      f.value },
                    { QStringLiteral("label"),      f.label },
                    { QStringLiteral("fault"),      f.fault },
                    { QStringLiteral("watch"),      !f.fault && shotFactorValueWatch(f.value) },
                    { QStringLiteral("building"),   f.building },
                    { QStringLiteral("share"),      f.share },
                    { QStringLiteral("frequency"),  f.frequency },
                    { QStringLiteral("sessions"),   f.sessions },
                    { QStringLiteral("shots"),      f.shots },
                    { QStringLiteral("seenWith"),   f.seenWith },
                    { QStringLiteral("seenWithId"), f.seenWithId },
                    { QStringLiteral("strength"),   f.strength },
                    { QStringLiteral("trend"),      f.trend },
                    { QStringLiteral("trendWords"), f.trendWords },
                    { QStringLiteral("workOn"),     status },
                    { QStringLiteral("workOnText"), outcomeCapitalised(status) },
                    { QStringLiteral("mix"),        shotMixBarToJson(f.mix).toVariantMap() },
                };
            };
            const QVariantMap strike = factorMap(it.strike), direction = factorMap(it.direction), flight = factorMap(it.flight);
            std::stable_sort(rowStatuses.begin(), rowStatuses.end(), [](const QString &a, const QString &b) {
                return workOnStatusRank(a) < workOnStatusRank(b);
            });
            QStringList rowWords;
            for (const QString &s : std::as_const(rowStatuses)) rowWords << outcomeCapitalised(s);
            items.append(QVariantMap{
                { QStringLiteral("key"),         it.key },
                { QStringLiteral("club"),        it.club },
                { QStringLiteral("clubLine"),    it.length == QLatin1String("partial") ? it.club + QStringLiteral(" · part swing") : it.club },
                { QStringLiteral("clubGroup"),   it.clubGroup },
                { QStringLiteral("groupLabel"),  tendencyClubLabel(it.clubGroup) },
                { QStringLiteral("distance"),    tendencyUsualDistance(it.clubId, it.length, m_tendencySet.clubDistances) },
                { QStringLiteral("length"),      it.length },
                { QStringLiteral("handedness"),  it.handedness },
                { QStringLiteral("sessions"),    it.sessions },
                { QStringLiteral("shots"),       it.shots },
                { QStringLiteral("workOnText"),  rowWords.join(QStringLiteral(" · ")) },
                { QStringLiteral("strike"),      strike },
                { QStringLiteral("direction"),   direction },
                { QStringLiteral("flight"),      flight },
            });
        }
    }

    const QString note = !m_tendenciesReady ? QString()
        : m_tendencySet.countedShots > 0
            ? QStringLiteral("From %1 shots over %2 sessions").arg(m_tendencySet.countedShots).arg(m_tendencySet.countedSessions)
            : QStringLiteral("Hit a few sessions with the launch monitor on");

    if (items == m_tendencyItems && note == m_tendencyNote) return;
    m_tendencyItems = items;
    m_tendencyNote  = note;
    emit tendenciesChanged();
}

void WorkOnsController::clearTendencies()
{
    m_tendencySet = TendencySet();
    m_tendencyFingerprint.clear();
    m_tendenciesReady = false;
    m_tendencyItems.clear();
    m_tendencyNote.clear();
    emit tendenciesChanged();
}

// One block per factor: its fault's chain, also-seen, early note, play-to and the condition to practise. A factor
// with none of them (a value, not a fault; a factor never read) is {}.
QVariantMap WorkOnsController::tendencyDetail(const QString &key) const
{
    for (const TendencyItem &it : m_tendencySet.items) {
        if (it.key != key) continue;
        QVariantMap out;
        for (const QString &name : shotFactorNames()) {
            const TendencyFactor &f = it.factor(name);
            if (f.chain.empty() && f.alsoSeen.isEmpty() && f.earlyNote.isEmpty() && f.playTo.isEmpty() && f.workOn.isEmpty()) {
                out.insert(name, QVariantMap());
                continue;
            }
            QVariantList chain;
            for (const TendencyChainStep &s : f.chain)
                chain.append(QVariantMap{ { QStringLiteral("text"), s.text }, { QStringLiteral("words"), s.words } });
            out.insert(name, QVariantMap{
                { QStringLiteral("chain"),    chain },
                { QStringLiteral("alsoSeen"), f.alsoSeen },
                { QStringLiteral("earlyNote"), f.earlyNote },
                { QStringLiteral("playTo"),   f.playTo },
                { QStringLiteral("practiseConditionId"), f.workOn },
            });
        }
        // How the factors came together (rule 15): the open row's last block.
        out.insert(QStringLiteral("together"), QVariantMap{
            { QStringLiteral("pairs"),      QVariant(it.together.pairs) },
            { QStringLiteral("combos"),     QVariant(it.together.combos) },
            { QStringLiteral("moreCombos"), it.together.moreCombos },
        });
        return out;
    }
    return QVariantMap();
}

QVariantMap WorkOnsController::tendencyFlow(const QString &group, double plotHeight, double gap) const
{
    return tendencyFlowMap(m_tendencySet, group, outcomePack(), plotHeight, gap);
}

QVariantMap WorkOnsController::clubDistance(const QString &club) const
{
    const QString canonical = pinpoint::canonicalClubId(club);
    for (const QJsonValue &v : m_tendencySet.clubDistances) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("club")).toString() != canonical) continue;
        const int n = o.value(QStringLiteral("shots")).toInt();
        return QVariantMap{
            { QStringLiteral("mean"),      o.value(QStringLiteral("carryMean")).toDouble(std::numeric_limits<double>::quiet_NaN()) },
            { QStringLiteral("sd"),        o.value(QStringLiteral("carrySd")).toDouble(std::numeric_limits<double>::quiet_NaN()) },
            { QStringLiteral("n"),         n },
            { QStringLiteral("total"),     o.value(QStringLiteral("totalMean")).toDouble(std::numeric_limits<double>::quiet_NaN()) },
            { QStringLiteral("offlineSd"), o.value(QStringLiteral("offlineSd")).toDouble(std::numeric_limits<double>::quiet_NaN()) },
            { QStringLiteral("earned"),    clubUsualEarned(n) },
            { QStringLiteral("line"),      o.value(QStringLiteral("carryText")).toString() },
        };
    }
    return QVariantMap();
}

QVariantMap WorkOnsController::onBallFor(const QString &conditionId) const
{
    // The one serialiser (audit D-17): the file's keys are the map's.
    for (const TendencyOnBall &b : m_tendencySet.onBall)
        if (b.conditionId == conditionId && onBallInScope(b.tendencyKey)) return tendencyOnBallToJson(b).toVariantMap();
    return QVariantMap();
}

QString WorkOnsController::onTheBallFor(const QString &conditionId) const
{
    for (const TendencyOnBall &b : m_tendencySet.onBall)
        if (b.conditionId == conditionId && onBallInScope(b.tendencyKey)) return b.onTheBall;
    return QString();
}

QString WorkOnsController::conditionLabel(const QString &id) const
{
    if (id.isEmpty()) return QString();
    followPackSave();
    return packLabel(m_packProv->pack(), id, golferHandedness());
}

// The golfer's handedness, as their shots record it: the tendencies' (each item carries its shots' hand), once they
// have landed; "" (right-handed words) before. A landing publishes again, so the names follow it.
QString WorkOnsController::golferHandedness() const
{
    if (!m_tendenciesReady) return QString();
    for (const TendencyItem &it : m_tendencySet.items)
        if (!it.handedness.isEmpty()) return it.handedness;
    return QString();
}

void WorkOnsController::clearSummary()
{
    m_themes = SwingThemes();
    m_themesFingerprint.clear();
    m_summaryReady = false;
    publishSummary();
}
