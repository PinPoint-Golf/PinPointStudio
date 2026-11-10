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

#include "session_diagnostics_model.h"
#include "session_spread.h"

#include "../variability/variability_ledger.h"     // ledgerConsistencyReading/Grade: the rule Home and the report share
#include "../variability/variability_measures.h"   // measureScaleType, isOutcomeMeasure, normCorridorHalfWidth

#include "../../Coaching/club_scope.h"
#include "../../Coaching/shot_outcomes_pack.h"
#include "../../Coaching/swing_areas_pack.h"
#include "../../Coaching/work_ons_pack.h"

#include "../../Diagnostics/packs/characteristic_pack.h"
#include "../../Diagnostics/packs/drill_pack.h"
#include "../../Diagnostics/engine/pack_io.h"
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Diagnostics/packs/swing_themes_pack.h"
#include "../../Diagnostics/norms/context_tree.h"
#include "../../Diagnostics/norms/norm_provider.h"
#include "../../Metrics/variability/variability_grader.h"
#include "../../Core/pp_debug.h"
#include "../../Export/swing_store.h"

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

const QString kRecordFile = QStringLiteral("work_ons.json");
const QString kThemesFile = QStringLiteral("swing_themes.json");
const QString kLedgerFile = QStringLiteral("diagnostics.json");
const QString kBaselineFile   = QStringLiteral("club_baseline.json");
const QString kTendenciesFile = QStringLiteral("tendencies.json");

// THE VERSION OF THE CONSISTENCY MARKS' RULE (the window, the filters, the scale): in the marks job's
// fingerprint, so a new rule grades again. Nothing is stored, so a bump only costs the next settle a read.
constexpr int kConsistencyMarksRule = 2;     // 2: every shot in, data-warning shots too (R13), outcomes ungraded (R4)

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

// A condition's name as the golfer reads it: the pack label, else the fallback, else the id.
QString packLabel(const CharacteristicPack &pack, const QString &id, const QString &fallback = QString())
{
    const Condition *c = pack.condition(id);
    if (c && !c->label.isEmpty()) return c->label;
    return fallback.isEmpty() ? id : fallback;
}

// §7.1's club groups, by where the club resolves in the context tree. "" = a putter or an undeclared club.
QString tendencyClubGroup(const ContextTree &tree, const QString &club)
{
    const QString id = contextIdForClub(club);
    const auto under = [&tree, &id](const char *root) {
        const QString r = QString::fromLatin1(root);
        return id == r || tree.isDescendantOf(id, r);
    };
    if (under("driver"))       return QStringLiteral("driver");
    if (under("fairway_wood")) return QStringLiteral("woods");
    if (id == QLatin1String("wedge_pitching") || under("iron")) return tendencyIronGroup(club);
    if (under("wedge"))        return QStringLiteral("wedges");
    return QString();
}

// One session as the tendency reduction reads it, from its ledger file already parsed (`root`) and the shots
// read off it. The outcome scope reads a session through here too, so the two can never disagree about a shot.
void tendencySessionFromLedger(const QString &dir, const QJsonObject &root, std::vector<ShotRecord> shots,
                               TendencySessionInput &out)
{
    out.session.folder = QFileInfo(dir).fileName();
    out.session.day    = sessionDayOfFolder(out.session.folder);
    out.session.shots  = std::move(shots);
    out.session.corrections = shotCorrectionsFromJson(root.value(QStringLiteral("corrections")).toObject());
    const QJsonObject run = root.value(QStringLiteral("intentFrom")).toObject();
    out.intents = resolvedShotIntents(shotIntentsFromJson(root.value(QStringLiteral("shotIntent")).toObject()),
                                      out.session.shots, run.value(QStringLiteral("fromShot")).toInt(-1),
                                      run.value(QStringLiteral("shape")).toString());

    // The session's evidence as finishDerive() stored it. Absent on a record not re-derived yet: the words
    // then say less, nothing more.
    QFile rec(WorkOnsController::recordPath(dir));
    if (rec.open(QIODevice::ReadOnly))
        out.evidence = tendencyEvidenceFromJson(
            QJsonDocument::fromJson(rec.readAll()).object().value(QStringLiteral("outcomeEvidence")).toObject());

    QFile base(QDir(dir).filePath(kBaselineFile));
    if (base.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const ClubBaselineSnapshot snap = clubBaselineFromJson(QJsonDocument::fromJson(base.readAll()).object(), &ok);
        if (ok) out.baseline = snap.clubs;
    }
}

// One session as the tendency reduction reads it. False with no readable ledger (it adds nothing).
bool readTendencySession(const QString &dir, TendencySessionInput &out)
{
    const QString path = QDir(dir).filePath(kLedgerFile);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        ppWarn() << "[Tendencies] ledger unreadable, left out:" << qPrintable(path) << "—" << qPrintable(err.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    tendencySessionFromLedger(dir, root, fromJson(root.value(QStringLiteral("ledger")).toObject()), out);
    return true;
}

// "1 shot", "38 shots".
QString outcomeCountText(int n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

// One session's record under a club scope, from its in-scope shots. `stored` is the session's whole record: its
// clock stands in when none of these shots carried one.
SessionWorkOns scopedSessionRecord(const QString &dir, const std::vector<ShotRecord> &shots, const LedgerOptions &opt,
                                   const CharacteristicPack &pack, const SessionWorkOns &stored)
{
    SessionWorkOns rec = sessionWorkOnsForPack(&pack, shots, conditionLedgers(shots, opt));
    rec.sessionId = QFileInfo(dir).fileName();
    for (WorkOnEntry &e : rec.entries) e.name = packLabel(pack, e.id);
    if (rec.startMs <= 0) {
        rec.startMs = stored.startMs;
        rec.endMs   = stored.endMs;
    }
    return rec;
}

// The reduction over sessions already read, with the per-club distances from those same sessions.
TendencySet tendencySetFromSessions(std::vector<TendencySessionInput> &sessions, const ShotOutcomePack &pack, const ContextTree &tree,
                                    const std::atomic<bool> *cancel = nullptr)
{
    // The flight table every shot's height is judged against; its bytes are in the tendency fingerprint.
    TendencySet set = reduceTendencies(sessions, pack, [&tree](const QString &club) { return tendencyClubGroup(tree, club); },
                                       shippedFlightNorms(), TendencyOptions{}, cancel);
    if (set.cancelled || (cancel && cancel->load())) return set;

    std::vector<ClubDistanceSession> clubSessions;
    clubSessions.reserve(sessions.size());
    for (TendencySessionInput &s : sessions) clubSessions.push_back(std::move(s.session));
    QJsonArray distances;
    for (const ClubDistanceItem &it : reduceClubDistances(clubSessions)) distances.append(clubDistanceItemToJson(it));
    set.clubDistances = distances;
    return set;
}

// The flight table's bytes, hashed once (as shippedFlightNorms reads it once): in the tendency fingerprint, so a
// threshold tuned in flight_norms.json re-reduces. Empty when the file can't be read (no flight is read either).
const QByteArray &flightNormsStamp()
{
    static const QByteArray k = [] {
        QFile f(flightNormsPath());
        if (!f.open(QIODevice::ReadOnly)) return QByteArray();
        return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex();
    }();
    return k;
}

// One factor's values in a club's tuple; empty when it wasn't read.
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

TendencySet WorkOnsController::tendenciesAsOf(const QString &sessionDir, const ShotOutcomePack &pack, const INormProvider &norms)
{
    const QFileInfo session{ QDir::cleanPath(sessionDir) };
    std::vector<TendencySessionInput> sessions;
    const QFileInfoList folders = QDir(session.absolutePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : folders) {
        if (!tendencySessionCounts(fi.fileName(), session.fileName())) continue;
        TendencySessionInput in;
        if (readTendencySession(fi.absoluteFilePath(), in)) sessions.push_back(std::move(in));
    }
    return tendencySetFromSessions(sessions, pack, norms.contexts());
}

WorkOnsController::WorkOnsController(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_themesPool.setMaxThreadCount(1);
    m_marksPool.setMaxThreadCount(1);
    m_marksPool.setThreadPriority(QThread::LowPriority);
    m_filterOptions = shotFilterOptionsFor(m_filter);      // Any alone, until there are tendencies to count
    // The job's own thread works through the replicates too, so this plus it is every core.
    m_replicatePool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
    // Seconds of every core, beside a home screen being used: the screen comes first.
    m_themesPool.setThreadPriority(QThread::LowPriority);
    m_replicatePool.setThreadPriority(QThread::LowPriority);
}

WorkOnsController::~WorkOnsController()
{
    // The pools' lambdas deliver back to `this`; drain them before the members go. A themes job
    // in flight is told to stop first, so quitting does not wait out a reduction.
    if (m_themesCancel) m_themesCancel->store(true);
    if (m_tendenciesCancel) m_tendenciesCancel->store(true);
    if (m_marksCancel) m_marksCancel->store(true);
    m_marksPool.waitForDone();
    m_themesPool.waitForDone();
    m_replicatePool.waitForDone();
    m_pool.waitForDone();
}

QString WorkOnsController::recordPath(const QString &sessionDir)
{
    return QDir(sessionDir).filePath(kRecordFile);
}

QString WorkOnsController::themesPath(const QString &athleteDir)
{
    return QDir(athleteDir).filePath(kThemesFile);
}

QString WorkOnsController::themesPath(const QString &athleteDir, const ClubScope &scope)
{
    if (scope.isAll()) return themesPath(athleteDir);
    return QDir(athleteDir).filePath(QStringLiteral("swing_themes.%1.json").arg(clubScopeFileKey(scope.key)));
}

QString WorkOnsController::themesPath(const QString &athleteDir, const ClubScope &scope, const ShotFilter &filter)
{
    if (filter.isAll()) return themesPath(athleteDir, scope);
    return QDir(athleteDir).filePath(QStringLiteral("swing_themes.%1.%2.json")
                                         .arg(clubScopeFileKey(scope.key), clubScopeFileKey(filter.key())));
}

QString WorkOnsController::tendenciesPath(const QString &athleteDir)
{
    return QDir(athleteDir).filePath(kTendenciesFile);
}

SessionDiagnosticsModel *WorkOnsController::model()
{
    if (!m_model) {
        m_model = new SessionDiagnosticsModel(this);
        m_model->setSynchronous(m_synchronous);
        // Reaching a session is the moment its baseline is frozen (Contract A): a predecessor that
        // cannot be read adds no shots, and the session still gets its one snapshot.
        m_model->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
        // REVIEWING, because that is what this model does: it reads recorded sessions, never the
        // live one (deriving pauses while a session runs). It is also the one switch that keeps
        // the panel-only work off the catch-up: a reviewed session's driver footer is final, so
        // rebuild() never runs the live debounce — a walk back through the session's prefixes,
        // a full ledger reduction per step, and the walk behind the startup freeze before it was
        // capped — for a footer nobody draws.
        // Reviewing touches only the displayed surfaces: the rows, the ledgers, the persisted
        // stage and sessionWorkOns() read the same with it on or off.
        m_model->setReviewing(true);
        // QUEUED: the model emits busyChanged before it appends the row that made it idle, and
        // a record read in between would be one shot short.
        connect(m_model, &SessionDiagnosticsModel::busyChanged,
                this, &WorkOnsController::onModelSettled, Qt::QueuedConnection);
    }
    return m_model;
}

void WorkOnsController::setAthleteDir(const QString &dir)
{
    if (m_athleteDir == dir) return;
    m_athleteDir = dir;
    emit athleteDirChanged();
    // The previous golfer's list must not sit under this one's name while the scan runs —
    // nor their swing summary, and a reduction of their sessions has nothing left to do.
    m_records.clear();
    m_queue.clear();
    m_sessionsFound = 0;
    cancelThemes();
    cancelTendencies();
    // The marks are this golfer's ledgers' alone.
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
    const QString stamp = model()->contentStamp();

    if (m_synchronous) {
        applyScan(generation, scan(dir, stamp));
        return;
    }
    m_pool.start([this, generation, dir, stamp]() {
        std::vector<Scanned> rows = scan(dir, stamp);
        QMetaObject::invokeMethod(this, [this, generation, rows = std::move(rows)]() mutable {
            applyScan(generation, std::move(rows));
        }, Qt::QueuedConnection);
    });
}

// Worker thread. Touches no member.
//
// THE FINGERPRINT is everything a record depends on: the selection rule, the content the rows
// are graded against, and every swing document's identity. Not diagnostics.json's own stamp —
// the panel rewrites that file for reasons that change no verdict (a focus declared, a screen
// answered), and the model reconciles the ledger against these same documents anyway. (The
// consistency marks, which read the ledger itself, fingerprint it in their own job: runMarks.)
std::vector<WorkOnsController::Scanned> WorkOnsController::scan(const QString &athleteDir,
                                                                const QString &contentStamp)
{
    std::vector<Scanned> out;
    const QFileInfoList sessions = QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : sessions) {
        const QString sessionDir = fi.absoluteFilePath();
        const QDir d(sessionDir);
        const QStringList swings = d.entryList(QStringList{ QStringLiteral("swing_*") },
                                               QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (swings.isEmpty()) continue;     // nothing was hit; there is nothing to say

        Scanned s;
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(QByteArray::number(kWorkOnRuleVersion));
        h.addData(contentStamp.toUtf8());
        for (const QString &name : swings) {
            const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(d.filePath(name));
            h.addData(name.toUtf8());
            h.addData(QByteArray::number(doc.size));
            h.addData(QByteArray::number(doc.mtimeMs));
        }

        s.sessionDir  = sessionDir;
        s.fingerprint = QString::fromLatin1(h.result().toHex().left(16));
        s.mtimeMs     = fi.lastModified().toMSecsSinceEpoch();
        // NOT in the fingerprint: a baseline that appears mid-catch-up must not stale every record
        // again. Its absence simply queues the session (applyScan), and that is the only thing it does.
        s.hasBaseline = QFileInfo::exists(d.filePath(kBaselineFile)) && !clubBaselineIsOutdated(d.filePath(kBaselineFile));
        s.day         = sessionDayOfFolder(fi.fileName());

        QFile f(recordPath(sessionDir));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            bool ok = false;
            s.record = sessionWorkOnsFromJson(root, &ok);
            s.fresh = ok && root.value(QStringLiteral("derivedFrom")).toObject()
                                .value(QStringLiteral("fingerprint")).toString() == s.fingerprint;
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

    m_records.clear();
    m_fingerprints.clear();
    m_dirMtime.clear();
    m_sessionsFound = int(rows.size());

    // Newest first: the latest session is the one the golfer is about to look for. BUT while any session
    // still lacks its frozen baseline, oldest first by FOLDER DAY (Contract A): a session's baseline is read
    // off its predecessors' rows, so they have to be current before it is frozen. A folder's mtime
    // says nothing about when it was played.
    const bool catchingUp = std::any_of(rows.begin(), rows.end(), [](const Scanned &s) { return !s.hasBaseline; });
    if (catchingUp)
        std::sort(rows.begin(), rows.end(), [](const Scanned &a, const Scanned &b) {
            if (a.day.isValid() != b.day.isValid()) return a.day.isValid();
            if (a.day != b.day) return a.day < b.day;
            return a.sessionDir < b.sessionDir;
        });
    else
        std::sort(rows.begin(), rows.end(),
                  [](const Scanned &a, const Scanned &b) { return a.mtimeMs > b.mtimeMs; });
    QStringList queue;
    for (Scanned &s : rows) {
        m_fingerprints.insert(s.sessionDir, s.fingerprint);
        m_dirMtime.insert(s.sessionDir, s.mtimeMs);
        const bool needsDerive = !s.fresh || !s.hasBaseline;
        if (s.fresh) m_records.insert(s.sessionDir, std::move(s.record));
        if (needsDerive) queue << s.sessionDir;
    }
    // A derivation in flight keeps its place; it will land in the new record set.
    if (m_deriving) queue.removeAll(m_derivingDir);
    m_queue = queue;

    publish();
    emit updatingChanged();
    deriveNext();
}

// ── Derive ──────────────────────────────────────────────────────────────────────────────

void WorkOnsController::deriveNext()
{
    while (!m_deriving && !m_paused && !m_queue.isEmpty()) {
        m_derivingDir = m_queue.takeFirst();
        m_deriving = true;

        SessionDiagnosticsModel *m = model();
        m->setGradePolicy(m_gradePolicy);
        m->activateSession(m_derivingDir);       // back-fills and regrades; may go busy
        if (m->busy()) return;                   // onModelSettled() picks it up
        finishDerive();
        if (!m_synchronous) {
            // One session per turn of the event loop: a ledger that needed no detection still
            // costs a parse and a reduction on this thread, and a library of them in one go
            // would be a visible stall on the home screen.
            QTimer::singleShot(0, this, &WorkOnsController::deriveNext);
            return;
        }
    }
    if (!m_deriving && m_queue.isEmpty() && m_model && !m_model->sessionDir().isEmpty())
        m_model->activateSession(QString());     // let go of the last session's rows
    // Every record current: every ledger is reconciled, so the summary can be read off them, and the
    // consistency marks too.
    maybeThemes();
    maybeMarks();
    maybeTendencies();
}

void WorkOnsController::onModelSettled()
{
    if (!m_deriving || !m_model || m_model->busy()) return;
    finishDerive();
    deriveNext();
}

void WorkOnsController::finishDerive()
{
    const QString dir = m_derivingDir;
    m_deriving = false;
    m_derivingDir.clear();

    // The athlete changed underneath it: the record is still true of the session, so it is
    // written, but it is not this list's.
    const bool ours = !m_athleteDir.isEmpty() && QDir(QFileInfo(dir).absolutePath()) == QDir(m_athleteDir);

    SessionWorkOns rec = m_model->sessionWorkOns();
    if (rec.startMs <= 0) {
        // No shot carried a clock. The folder's own date, then its mtime — a session with no
        // date at all would sort before every other and age everything behind it.
        const QDate d = QDate::fromString(rec.sessionId.left(10), Qt::ISODate);
        rec.startMs = d.isValid() ? d.startOfDay().toMSecsSinceEpoch()
                                  : m_dirMtime.value(dir, QFileInfo(dir).lastModified().toMSecsSinceEpoch());
        rec.endMs = rec.startMs;
    }

    QJsonObject root = toJson(rec);
    root[QStringLiteral("derivedFrom")] = QJsonObject{
        { QStringLiteral("fingerprint"),  m_fingerprints.value(dir) },
        { QStringLiteral("contentStamp"), m_model->contentStamp() },
        { QStringLiteral("gradePolicy"),  m_gradePolicy },
        { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
    };
    // Per condition, never per shot: what the tendency reduction needs to re-compose every shot itself.
    root[QStringLiteral("outcomeEvidence")] = tendencyEvidenceToJson(m_model->outcomeEvidence());
    atomicWrite(recordPath(dir), QJsonDocument(root).toJson(QJsonDocument::Compact));

    if (ours) {
        m_records.insert(dir, std::move(rec));
        publish();
    }
    emit updatingChanged();
}

bool WorkOnsController::waitForIdle(int msTimeout)
{
    // IDLE MEANS STILL IDLE AFTER A TURN OF THE EVENT LOOP. Deriving hands the next session to a
    // zero-timer, and the summary starts from the end of that call — so for one turn after the
    // last derivation nothing reads as busy although the reduction is about to begin.
    const auto busy = [this] {
        return (updating() && !(m_paused && !m_scanning && !m_deriving)) || m_themesRunning || m_marksRunning
            || m_tendenciesRunning;
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
        if (m_themesRunning || m_tendenciesRunning) m_themesPool.waitForDone(20);
        if (m_marksRunning) m_marksPool.waitForDone(20);
    }
    return !m_scanning && !m_deriving && !m_themesRunning && !m_marksRunning && !m_tendenciesRunning;
}

// ── Publish ─────────────────────────────────────────────────────────────────────────────

void WorkOnsController::publish()
{
    const SessionWorkOns *newest = nullptr;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        const SessionWorkOns &r = it.value();
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

    const DescribedWorkOns all = describeWorkOns(m_records);
    m_items = all.items;
    m_clearedCount = all.cleared;

    publishScreens(m_summaryScope == clubScopeAllKey() ? &all : nullptr);
    emit itemsChanged();
    // The Tendencies tab's work-on column reads the list just rebuilt.
    publishTendencies();
    updateClubScope();
}

const QHash<QString, SessionWorkOns> &WorkOnsController::shownRecords() const
{
    return m_summaryScope == clubScopeAllKey() ? m_records : m_scopedRecords;
}

void WorkOnsController::publishScreens(const DescribedWorkOns *known)
{
    if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
    const CharacteristicPack &pack = m_packProv->pack();

    const QHash<QString, SessionWorkOns> &records = shownRecords();
    DescribedWorkOns scoped;
    if (!known) { scoped = describeWorkOns(records, &m_shownOutcome); known = &scoped; }
    m_details = known->details;
    ++m_detailsRevision;
    // What the consistency marks are asked for: each detail's, then each area item's below.
    m_markRequests = known->markRequests;

    // One call — the every-condition aggregate, and the same again without the newest substantive session for
    // the trend — per publish, on this thread; the aggregate's per-session evidence index keeps it linear.
    std::vector<SessionWorkOns> sessions;
    sessions.reserve(size_t(records.size()));
    for (auto it = records.constBegin(); it != records.constEnd(); ++it) sessions.push_back(it.value());
    publishAreas(pack, sessions);

    emit detailsChanged();
    emit areasChanged();
}

WorkOnsController::DescribedWorkOns WorkOnsController::describeWorkOns(const QHash<QString, SessionWorkOns> &records,
                                                                      const ShownOutcome *outcome)
{
    if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
    const CharacteristicPack &pack = m_packProv->pack();
    const DrillSet &drills = sharedDrillSet();

    std::vector<SessionWorkOns> sessions;
    sessions.reserve(size_t(records.size()));
    QHash<QString, QString> dirOf;      // session id -> dir
    QSet<QString> substantive;          // sessions that could say something about something
    for (auto it = records.constBegin(); it != records.constEnd(); ++it) {
        const SessionWorkOns &r = it.value();
        sessions.push_back(r);
        dirOf.insert(r.sessionId, it.key());
        if (r.substantive()) substantive.insert(r.sessionId);
    }
    int totalSwings = 0;
    for (const SessionWorkOns &s : sessions) totalSwings += s.shotCount;
    QHash<QString, MarksRequest> markRequests;

    // Every condition any session assessed, for the details Home opens in place; then the list.
    const std::vector<WorkOnSummary> every = aggregateConditions(sessions);
    const std::vector<WorkOnSummary> agg = aggregateWorkOns(std::move(sessions));

    const auto nameOf = [&pack](const QString &id, const QString &fallback = QString()) {
        return packLabel(pack, id, fallback);
    };

    // One condition's summary in words: a work-on row, or the detail of any condition.
    const auto describe = [&](const WorkOnSummary &w) {
        const Condition *cond = pack.condition(w.id);
        QVariantMap m;
        m[QStringLiteral("id")]        = w.id;
        m[QStringLiteral("name")]      = nameOf(w.id, w.name);
        m[QStringLiteral("status")]    = workOnStatusToString(w.status);
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
        m[QStringLiteral("consequence")] = cond ? cond->consequence.text() : QString();

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
        const WorkOnPoint *lastPattern = w.latestPatternIndex >= 0 ? &w.history[size_t(w.latestPatternIndex)] : nullptr;
        const WorkOnPoint *lastConclusive = w.latestConclusiveIndex >= 0 ? &w.history[size_t(w.latestConclusiveIndex)] : nullptr;
        // A later session that measured OTHER things and not this one. Three balls
        // that measured nothing are not a session this fault went missing from.
        bool unmeasuredSince = false;
        for (int i = w.latestConclusiveIndex + 1; i < int(w.history.size()); ++i)
            if (substantive.contains(w.history[size_t(i)].sessionId)) unmeasuredSince = true;
        if (w.status == WorkOnStatus::Easing && lastConclusive)
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
            for (const WorkOnPoint &p : w.history) if (p.sessionId == sessionId) when = p.startMs;
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
            const WorkOnEntry &e = w.latest;
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
            const WorkOnEvidence &e = w.latestEvidence;
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
    for (const WorkOnSummary &w : every) {
        // A status read off evidence alone: a never-listed pattern would say Active. Only the list
        // speaks of work-on status, so a condition that is not on it says nothing in that line.
        QVariantMap m = describe(w);
        if (w.sessionsListed == 0) m[QStringLiteral("statusText")] = QString();
        details.insert(w.id, withContrast(m, w.id));
    }
    for (const WorkOnSummary &w : agg) {
        if (w.status == WorkOnStatus::Cleared) { ++cleared; continue; }
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
        const ContextTree &tree = sharedNormProvider()->contexts();
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

// The stored block: the key it was counted for, the line's counts, and each fault's [a, n1, b, n0].
QJsonObject WorkOnsController::outcomeToJson(const ShownOutcome &shown)
{
    QJsonObject contrasts;
    for (auto it = shown.contrasts.constBegin(); it != shown.contrasts.constEnd(); ++it)
        contrasts.insert(it.key(), QJsonArray{ it->a, it->n1, it->b, it->n0 });
    return QJsonObject{
        { QStringLiteral("key"),       shown.scope.key() },
        { QStringLiteral("rule"),      kOutcomeScopeRuleVersion },
        { QStringLiteral("chosen"),    shown.chosen },
        { QStringLiteral("sessions"),  shown.sessions },
        { QStringLiteral("others"),    shown.others },
        { QStringLiteral("measured"),  shown.measured },
        { QStringLiteral("contrasts"), contrasts },
    };
}

bool WorkOnsController::outcomeFromJson(const QJsonObject &o, ShownOutcome &shown)
{
    if (o.value(QStringLiteral("key")).toString() != shown.scope.key()
        || o.value(QStringLiteral("rule")).toInt() != kOutcomeScopeRuleVersion)
        return false;
    shown.chosen   = o.value(QStringLiteral("chosen")).toInt();
    shown.sessions = o.value(QStringLiteral("sessions")).toInt();
    shown.others   = o.value(QStringLiteral("others")).toInt();
    shown.measured = o.value(QStringLiteral("measured")).toInt();
    shown.contrasts.clear();
    const QJsonObject contrasts = o.value(QStringLiteral("contrasts")).toObject();
    for (auto it = contrasts.constBegin(); it != contrasts.constEnd(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() != 4) return false;
        shown.contrasts.insert(it.key(), OutcomeContrast{ a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(), a.at(3).toInt() });
    }
    return true;
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
void WorkOnsController::publishAreas(const CharacteristicPack &pack, const std::vector<SessionWorkOns> &sessions)
{
    m_areaItems.clear();
    if (sessions.empty()) return;

    const GradePolicy policy = gradePolicyByName(m_gradePolicy);
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
            items.append(QVariantMap{
                { QStringLiteral("conditionId"),    it.conditionId },
                { QStringLiteral("label"),          it.label },
                { QStringLiteral("golfer"),         it.golfer },
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
            { QStringLiteral("topFault"),      a.topFaultId.isEmpty() ? QString() : packLabel(pack, a.topFaultId) },
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

// The mark publish() shows for one condition on one measure: the last job's, else ungraded (no badge).
QVariant WorkOnsController::markFor(const QString &conditionId, const QString &measureId) const
{
    return QVariant::fromValue(m_marks.value(markKey(conditionId, measureId), pv::VariabilityResultView()));
}

// Called once the catch-up is done, beside maybeThemes. Cheap when the ledgers have not moved: the
// job stats them and stops.
void WorkOnsController::maybeMarks()
{
    if (m_athleteDir.isEmpty() || m_paused || m_scanning || m_deriving || !m_queue.isEmpty()) return;
    if (clubScopeRefreshing()) return;      // the landing asks again, for the rows it brings
    if (m_marksRunning) { m_marksAgain = true; return; }
    m_marksAgain = false;

    if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
    const CharacteristicPack &pack = m_packProv->pack();

    // What the pack says before a ledger is read: no measure, a yes/no condition, a composite measure, an outcome (R4).
    const QHash<QString, AreaConditionInfo> info = areaConditionInfoForPack(pack);
    MarksJob job;
    QStringList keys = m_markRequests.keys();
    keys.sort();
    QCryptographicHash seed(QCryptographicHash::Sha1);
    seed.addData(QByteArray::number(kConsistencyMarksRule));
    seed.addData((m_model ? m_model->contentStamp() : QString()).toUtf8());
    seed.addData(m_scope.key.toUtf8());
    for (const QString &key : keys) {
        MarksRequest r = m_markRequests.value(key);
        const Measure *m = pack.measure(r.measureId);
        r.ungraded = r.measureId.isEmpty() || info.value(r.conditionId).binary
                  || (m && m->kind == MeasureKind::Composite) || pv::isOutcomeMeasure(m);
        r.scaleType = pv::measureScaleType(m);
        seed.addData(key.toUtf8());
        seed.addData(QByteArray::number(int(r.ungraded)));
        seed.addData(QByteArray::number(int(r.scaleType)));
        seed.addData(r.unit.toUtf8());
        seed.addData(QByteArrayLiteral("\x1e"));
        job.requests.push_back(std::move(r));
    }
    // Newest first by when each was played, then by name, as the aggregate orders them.
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it)
        job.sessions.push_back({ it.key(), it->startMs, std::max(it->startMs, it->endMs) });
    std::sort(job.sessions.begin(), job.sessions.end(), [](const MarksSession &a, const MarksSession &b) {
        return a.startMs != b.startMs ? a.startMs > b.startMs : QFileInfo(a.dir).fileName() > QFileInfo(b.dir).fileName();
    });
    job.generation = ++m_marksGeneration;
    job.seed       = QString::fromLatin1(seed.result().toHex());
    job.previous   = m_marksFingerprint;
    job.scope      = m_scope;
    // Here, on the GUI thread: sharedNormProvider()'s first build is not safe on a worker (norm_provider.h).
    job.norms      = sharedNormProvider();

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_marksCancel = cancel;
    m_marksRunning = true;
    ++m_marksJobs;

    if (m_synchronous) {
        applyMarks(job.generation, runMarks(job, cancel.get()));
        return;
    }
    m_marksPool.start([this, job = std::move(job), cancel]() {
        MarksOutcome out = runMarks(job, cancel.get());
        QMetaObject::invokeMethod(this, [this, generation = job.generation, out = std::move(out)]() mutable {
            applyMarks(generation, std::move(out));
        }, Qt::QueuedConnection);
    });
}

// Stops the running job, if any. Its result, should it still arrive, is a generation behind and is
// dropped; the marks on screen stay.
void WorkOnsController::cancelMarks()
{
    if (m_marksCancel) m_marksCancel->store(true);
    m_marksCancel.reset();
    m_marksRunning = false;
    m_marksAgain = false;
    ++m_marksGeneration;
}

// Worker thread (or inline when synchronous). Touches no member.
WorkOnsController::MarksOutcome WorkOnsController::runMarks(const MarksJob &job, const std::atomic<bool> *cancel)
{
    QElapsedTimer clock;
    clock.start();
    MarksOutcome out;

    // ── 1. The ledgers' identity: an unchanged library stops here ──
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(job.seed.toUtf8());
    for (const MarksSession &s : job.sessions) {
        const QFileInfo fi(QDir(s.dir).filePath(kLedgerFile));
        h.addData(QFileInfo(s.dir).fileName().toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(QByteArray::number(fi.exists() ? fi.size() : -1));
        h.addData(QByteArray::number(fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0));
        h.addData(QByteArray::number(s.startMs));
        h.addData(QByteArrayLiteral("\x1e"));
    }
    out.fingerprint = QString::fromLatin1(h.result().toHex().left(16));
    if (out.fingerprint == job.previous) {
        out.unchanged = true;
        return out;
    }

    // ── 2. Each request's window: its newest kNWindow readings across sessions ──
    struct Reading {
        qint64  ms = 0;         // the shot's clock, else its session's start
        int     session = 0;    // index into job.sessions: lower is newer
        int     shotId = 0;
        pv::LedgerConsistencyReading reading;   // the shared rule's (variability_ledger.h)
    };
    const auto newer = [](const Reading &a, const Reading &b) {
        if (a.ms != b.ms) return a.ms > b.ms;
        if (a.session != b.session) return a.session < b.session;
        return a.shotId > b.shotId;
    };
    QHash<QString, std::vector<int>> byCondition;           // conditionId -> the requests reading it
    for (int i = 0; i < int(job.requests.size()); ++i)
        if (!job.requests[size_t(i)].ungraded) byCondition[job.requests[size_t(i)].conditionId].push_back(i);
    std::vector<std::vector<Reading>> windows(job.requests.size());

    // The latest a reading in session i or any older one can be: past it, a full window is final.
    std::vector<qint64> latestFrom(job.sessions.size() + 1, std::numeric_limits<qint64>::min());
    for (int i = int(job.sessions.size()) - 1; i >= 0; --i)
        latestFrom[size_t(i)] = std::max(latestFrom[size_t(i) + 1], job.sessions[size_t(i)].endMs);

    for (int i = 0; i < int(job.sessions.size()) && !byCondition.isEmpty(); ++i) {
        if (cancel && cancel->load()) return out;
        bool settled = true;
        for (const auto &ids : byCondition)
            for (int r : ids) {
                const std::vector<Reading> &w = windows[size_t(r)];
                if (int(w.size()) < pv::kNWindow || w[size_t(pv::kNWindow - 1)].ms < latestFrom[size_t(i)]) settled = false;
            }
        if (settled) break;

        const MarksSession &s = job.sessions[size_t(i)];
        const QString path = QDir(s.dir).filePath(kLedgerFile);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;     // a session with no ledger has no reading to give
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            ppWarn() << "[Consistency] ledger unreadable, left out:" << qPrintable(path) << "—" << qPrintable(err.errorString());
            continue;
        }
        ++out.ledgersRead;
        const std::vector<ShotRecord> shots = fromJson(doc.object().value(QStringLiteral("ledger")).toObject());
        // Every shot, data-warning shots included (R13; variability_ledger.h): a broken recording's rows
        // are already NotAssessable in the ledger, and the shared rule leaves those out.
        for (const ShotRecord &shot : shots) {
            if (!job.scope.contains(shot)) continue;
            const qint64 ms = shot.timestampMs > 0 ? shot.timestampMs : s.startMs;
            for (const ConditionRow &row : shot.rows) {
                const auto ids = byCondition.constFind(row.conditionId);
                if (ids == byCondition.constEnd()) continue;
                for (int r : *ids) {
                    std::optional<pv::LedgerConsistencyReading> x =
                        pv::ledgerConsistencyReading(shot, row, job.requests[size_t(r)].measureId);
                    if (!x) continue;
                    windows[size_t(r)].push_back(Reading{ ms, i, shot.shotId, std::move(*x) });
                }
            }
        }
        for (std::vector<Reading> &w : windows) {
            std::sort(w.begin(), w.end(), newer);
            if (int(w.size()) > pv::kNWindow) w.resize(size_t(pv::kNWindow));
        }
    }

    // ── 3. Grade each window: the shared rule (variability_ledger.h), the report's too ──
    int graded = 0;
    for (int r = 0; r < int(job.requests.size()); ++r) {
        if (cancel && cancel->load()) return out;
        const MarksRequest &req = job.requests[size_t(r)];
        const std::vector<Reading> &w = windows[size_t(r)];
        std::vector<pv::LedgerConsistencyReading> readings;
        readings.reserve(w.size());
        for (auto it = w.rbegin(); it != w.rend(); ++it) readings.push_back(it->reading);    // strike order
        const auto *norms = job.norms.get();
        const QString measureId = req.measureId;
        const std::optional<pv::VariabilityResult> result = pv::ledgerConsistencyGrade(
            readings, req.ungraded, req.scaleType,
            [norms, measureId](const QString &context) { return pv::normCorridorHalfWidth(norms, measureId, context); });
        pv::VariabilityResultView view;     // no result: no badge
        if (result) {
            view = pv::VariabilityResultView::fromResult(*result, req.unit, 1);
            ++graded;
        }
        out.marks.insert(markKey(req.conditionId, req.measureId), view);
    }
    out.computed = true;
    ppInfo() << qPrintable(QStringLiteral("[Consistency] marks: %1 faults, %2 graded, %3 of %4 ledgers read, %5 ms")
                               .arg(job.requests.size()).arg(graded).arg(out.ledgersRead)
                               .arg(job.sessions.size()).arg(clock.elapsed()));
    return out;
}

void WorkOnsController::applyMarks(int generation, MarksOutcome out)
{
    if (generation != m_marksGeneration) return;       // cancelled or superseded
    m_marksRunning = false;
    m_marksCancel.reset();
    m_marksLedgersRead += out.ledgersRead;

    if (out.computed) {
        m_marksFingerprint = out.fingerprint;
        if (out.marks != m_marks) {
            m_marks = std::move(out.marks);
            // The marks ride on areaItems and faultDetail(): a changed one rebuilds them.
            if (m_summaryScope == clubScopeAllKey()) publish();
            else publishScreens();
        }
    }
    if (m_marksAgain) maybeMarks();
}

// ── Your swing ──────────────────────────────────────────────────────────────────────────

// Everything the summary depends on: the rule and the schema it was reduced and stored under, the
// content the ledgers were graded against, which drills exist (the focus groups the faults by the
// first of each one's drills the drill set holds, and that order is stored), and every session's
// work-ons fingerprint — which moves whenever a swing arrives, leaves or is re-analysed, i.e.
// whenever that session's ledger is reconciled into something new. Sessions in NAME order, the
// order the reduction takes them in.
QString WorkOnsController::themesFingerprint() const
{
    QStringList dirs = m_fingerprints.keys();
    std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() < QFileInfo(b).fileName();
    });
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(kThemeRuleVersion));
    h.addData(QByteArray::number(kThemeSchemaVersion));
    h.addData((m_model ? m_model->contentStamp() : QString()).toUtf8());
    QStringList drillIds;
    for (const Drill &d : sharedDrillSet().drills) drillIds.append(d.id);
    drillIds.sort();
    h.addData(drillIds.join(QLatin1Char('\x1f')).toUtf8());
    h.addData(QByteArrayLiteral("\x1d"));
    for (const QString &d : dirs) {
        h.addData(QFileInfo(d).fileName().toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(m_fingerprints.value(d).toUtf8());
        h.addData(QByteArrayLiteral("\x1e"));
    }
    if (!m_scope.isAll()) {      // All's fingerprint stays what every stored summary was written under
        QStringList clubs(m_scope.clubIds.begin(), m_scope.clubIds.end());
        clubs.sort();
        h.addData(QByteArrayLiteral("\x1d"));
        h.addData(QByteArray::number(kClubScopeRuleVersion));
        h.addData(m_scope.key.toUtf8());
        h.addData(clubs.join(QLatin1Char('\x1f')).toUtf8());
    }
    // A shot filter reads each shot through the Tendencies rule, off corrections and intents that only touch the
    // ledger (and the flight table): the tendency fingerprint, over every ledger's size and mtime and the table's
    // bytes, moves with them. All's, again, is left as every stored summary was written under.
    if (!m_filter.isAll()) {
        h.addData(QByteArrayLiteral("\x1d"));
        h.addData(QByteArray::number(kOutcomeScopeRuleVersion));
        h.addData(m_filter.key().toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(m_tendencyFingerprint.toUtf8());
    }
    return QString::fromLatin1(h.result().toHex().left(16));
}

// Called once the catch-up is done. Cheap when the summary on screen is already current.
void WorkOnsController::maybeThemes()
{
    if (m_athleteDir.isEmpty() || m_paused || m_scanning || m_deriving || !m_queue.isEmpty()) return;

    // No session with a swing in it: nothing to sum up, and no file to write into the folder.
    if (m_fingerprints.isEmpty()) {
        cancelThemes();
        if (m_summaryReady) clearSummary();
        return;
    }

    const QString fp = themesFingerprint();
    if (m_summaryReady && fp == m_themesFingerprint) {
        cancelThemes();         // a job for a state that has since come back to this one
        return;
    }
    if (m_themesRunning && fp == m_themesJobFingerprint) return;
    cancelThemes();             // superseded

    // The pack is marshalled here, on the GUI thread; the job sees plain values only.
    if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
    const CharacteristicPack &pack = m_packProv->pack();

    ThemesJob job;
    job.generation   = ++m_themesGeneration;
    job.athleteDir   = m_athleteDir;
    job.fingerprint  = fp;
    job.contentStamp = m_model ? m_model->contentStamp() : QString();
    job.sessionDirs  = m_fingerprints.keys();
    std::sort(job.sessionDirs.begin(), job.sessionDirs.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() < QFileInfo(b).fileName();
    });
    job.measures   = themeMeasureInfo(pack);
    job.conditions = themeConditionInfo(pack, sharedDrillSet());
    job.scope      = m_scope;
    job.outcome    = m_filter;
    job.announce   = clubScopeRefreshing() || matchupsRefreshing();
    if (!m_scope.isAll() || !m_filter.isAll()) {
        job.records = m_records;
        job.pack    = std::make_shared<const CharacteristicPack>(pack);
    }
    if (!m_filter.isAll()) {
        // Marshalled here, on the GUI thread, as the tendency job's are; sharedNormProvider()'s first build is not
        // safe on a worker, and the flight table is read here first so the worker only reads it.
        job.outcomePack = std::make_shared<const ShotOutcomePack>(outcomePack());
        job.norms       = sharedNormProvider();
        (void)shippedFlightNorms();
        for (auto it = job.conditions.constBegin(); it != job.conditions.constEnd(); ++it)
            if (it->fault) job.faultIds.insert(it.key());
        job.clubWords = clubScopeWords();
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_themesCancel = cancel;
    m_themesJobFingerprint = fp;
    m_themesRunning = true;
    emit summaryChanged();

    if (m_synchronous) {
        applyThemes(job.generation, runThemes(job, cancel.get()));
        return;
    }
    m_themesPool.start([this, job = std::move(job), cancel]() {
        ThemesOutcome out = runThemes(job, cancel.get());
        QMetaObject::invokeMethod(this, [this, generation = job.generation, out = std::move(out)]() mutable {
            applyThemes(generation, std::move(out));
        }, Qt::QueuedConnection);
    });
}

// Stops the running job, if any. Its result, should it still arrive, is a generation behind and
// is dropped; the summary on screen is left as it is.
void WorkOnsController::cancelThemes()
{
    if (m_themesCancel) m_themesCancel->store(true);
    m_themesCancel.reset();
    ++m_themesGeneration;
    if (!m_themesRunning) return;
    m_themesRunning = false;
    m_themesJobFingerprint.clear();
    emit summaryChanged();
}

// Worker thread (or inline when synchronous). Touches no member but the replicate pool.
WorkOnsController::ThemesOutcome WorkOnsController::runThemes(const ThemesJob &job, const std::atomic<bool> *cancel)
{
    QElapsedTimer clock;
    clock.start();
    ThemesOutcome out;
    out.fingerprint  = job.fingerprint;
    out.contentStamp = job.contentStamp;
    out.athleteDir   = job.athleteDir;
    out.scope        = job.scope;
    out.outcome.scope     = job.outcome;
    out.outcome.clubWords = job.clubWords;
    const bool scoped = !job.scope.isAll() || !job.outcome.isAll();
    const QString combinedKey = scopeKeyOf(job.scope, job.outcome);

    // ── 1. The stored summary, when it was reduced from exactly this ──
    {
        QFile f(themesPath(job.athleteDir, job.scope, job.outcome));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QString stored = root.value(QStringLiteral("derivedFrom")).toObject()
                                       .value(QStringLiteral("fingerprint")).toString();
            bool ok = false;
            SwingThemes st = swingThemesFromJson(root, &ok);     // another schema or rule: not ok
            if (ok && scoped)
                for (const QJsonValue &v : root.value(QStringLiteral("records")).toArray()) {
                    bool recordOk = false;
                    SessionWorkOns rec = sessionWorkOnsFromJson(v.toObject(), &recordOk);
                    ok = ok && recordOk;
                    out.records.insert(QDir(job.athleteDir).filePath(rec.sessionId), std::move(rec));
                }
            // The contrast and the line's counts, restored as the records are.
            if (ok && !job.outcome.isAll()) ok = outcomeFromJson(root.value(QStringLiteral("outcome")).toObject(), out.outcome);
            if (ok && !st.cancelled && stored == job.fingerprint) {
                out.fromDisk = true;
                out.themes   = std::move(st);
                out.wallMs   = clock.elapsed();
                return out;
            }
            out.records.clear();
            out.outcome.contrasts.clear();
            out.outcome.chosen = out.outcome.sessions = out.outcome.others = out.outcome.measured = 0;
        }
    }

    // No stored result: the screens showing another scope are in for a wait.
    if (job.announce)
        QMetaObject::invokeMethod(this, [this, generation = job.generation]() { applyScopeWorking(generation); }, Qt::QueuedConnection);

    // ── 2. Every session's ledger ──
    std::vector<ThemeSessionInput> sessions;
    sessions.reserve(size_t(job.sessionDirs.size()));
    for (const QString &dir : job.sessionDirs) {
        if (cancel && cancel->load()) return out;
        const QString path = QDir(dir).filePath(kLedgerFile);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            ppWarn() << "[SwingThemes] no ledger to read:" << qPrintable(path) << "—" << qPrintable(f.errorString());
            continue;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            ppWarn() << "[SwingThemes] ledger unreadable, left out:" << qPrintable(path) << "—" << qPrintable(err.errorString());
            continue;
        }
        ThemeSessionInput in;
        in.name  = QFileInfo(dir).fileName();
        LedgerOptions ledgerOpt;
        const QJsonObject root = doc.object();
        in.shots = fromJson(root.value(QStringLiteral("ledger")).toObject(), &ledgerOpt);
        if (scoped) {
            bool whole = wholeSessionInScope(in.shots, job.scope);
            // The shot filter judges every shot as the tendency reduction does, factor by factor, off the whole
            // session (its flight usuals are the session's), so it takes the ledger before the club filter.
            std::vector<ShotRecord> sessionShots;
            if (!job.outcome.isAll()) sessionShots = in.shots;
            if (!whole) in.shots = shotsInScope(in.shots, job.scope);
            if (!job.outcome.isAll() && !in.shots.empty()) {
                TendencySessionInput tin;
                tendencySessionFromLedger(dir, root, std::move(sessionShots), tin);
                QHash<int, ShotFactors> judged = judgedShotFactors(tin, *job.outcomePack, shippedFlightNorms());
                // Tendencies leaves out a club with no group (a putter, an undeclared club), so its shots are
                // judged by nothing here either: in no choice but All, and no one's other shot.
                const ContextTree &tree = job.norms->contexts();
                for (const ShotRecord &shot : tin.session.shots)
                    if (tendencyClubGroup(tree, shot.club).isEmpty()) judged.insert(shot.shotId, ShotFactors{});

                const QHash<QString, OutcomeContrast> contrasts = outcomeContrasts(in.shots, judged, job.outcome, job.faultIds);
                for (auto it = contrasts.constBegin(); it != contrasts.constEnd(); ++it) out.outcome.contrasts[it.key()] += it.value();
                for (const ShotRecord &shot : in.shots) {
                    const ShotFactors j = judged.value(shot.shotId);
                    if (j.judged() && !job.outcome.contains(j)) ++out.outcome.others;
                }
                std::vector<ShotRecord> chosen = shotsInShotFilter(in.shots, judged, job.outcome);
                whole = whole && chosen.size() == in.shots.size();
                in.shots = std::move(chosen);
                if (!in.shots.empty()) {
                    out.outcome.chosen += int(in.shots.size());
                    ++out.outcome.sessions;
                }
                // Measured: a chosen shot with at least one fault row assessed. A launch-monitor-only session
                // judges the ball and assesses no fault, so its shots are chosen and not measured.
                for (const ShotRecord &shot : in.shots)
                    if (std::any_of(shot.rows.begin(), shot.rows.end(), [&job](const ConditionRow &r) {
                            return r.state != ShotState::NotAssessable && job.faultIds.contains(r.conditionId);
                        }))
                        ++out.outcome.measured;
            }
            if (in.shots.empty()) continue;
            const SessionWorkOns stored = job.records.value(dir);
            out.records.insert(dir, whole ? stored : scopedSessionRecord(dir, in.shots, ledgerOpt, *job.pack, stored));
        }
        sessions.push_back(std::move(in));
    }
    if (cancel && cancel->load()) return out;
    if (!job.outcome.isAll())
        ppInfo() << qPrintable(QStringLiteral("[OutcomeScope] %1: %2 chosen shots over %3 sessions (%4 with the swing measured), "
                                              "%5 other judged shots, %6 faults set against them")
                                   .arg(combinedKey).arg(out.outcome.chosen).arg(out.outcome.sessions).arg(out.outcome.measured)
                                   .arg(out.outcome.others).arg(job.faultIds.size()));

    const ThemeOptions themeOpt;

    // ── 3. What Coaching and Your swing read lands first, while a screen is showing another scope ──
    if (job.announce) {
        SwingThemes layerOne = reduceSwingSummary(sessions, job.conditions, themeOpt);
        ppInfo() << qPrintable(QStringLiteral("[ClubScope] %1: summary after %2 ms").arg(combinedKey).arg(clock.elapsed()));
        if (m_synchronous) {
            applyScopeSummary(job.generation, combinedKey, out.records, std::move(layerOne), out.outcome);
        } else {
            QMetaObject::invokeMethod(this, [this, generation = job.generation, key = combinedKey, records = out.records,
                                             layerOne = std::move(layerOne), outcome = out.outcome]() mutable {
                applyScopeSummary(generation, key, std::move(records), std::move(layerOne), std::move(outcome));
            }, Qt::QueuedConnection);
        }
    }

    // ── 4. Reduce, the replicates fanned out ──
    // The calling thread works through the indices beside the pool's threads, and waits for them.
    // Only this one job uses the pool, so its threads are free when it asks.
    QThreadPool *pool = &m_replicatePool;
    const ThemeParallelFor parallelFor = [pool](int n, const std::function<void(int)> &body) {
        std::atomic<int> next{ 0 };
        const auto drain = [&]() {
            for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1)) body(i);
        };
        const int helpers = std::max(0, std::min(n - 1, pool->maxThreadCount()));
        QSemaphore done;
        for (int h = 0; h < helpers; ++h)
            pool->start([&]() { drain(); done.release(); });
        drain();
        done.acquire(helpers);
    };
    SwingThemes st = reduceSwingThemes(sessions, job.measures, job.conditions, themeOpt, parallelFor, cancel);
    if (st.cancelled || (cancel && cancel->load())) return out;

    out.computed = true;
    out.themes   = std::move(st);
    out.wallMs   = clock.elapsed();
    return out;
}

void WorkOnsController::applyThemes(int generation, ThemesOutcome out)
{
    if (generation != m_themesGeneration) return;      // cancelled or superseded
    m_themesRunning = false;
    m_themesCancel.reset();
    m_themesJobFingerprint.clear();

    if (!out.fromDisk && !out.computed) {              // stopped part way: neither written nor shown
        setScopeWorking(false);
        emit summaryChanged();
        return;
    }
    // A club choice, an outcome choice or both: the records and the stored file are the choice's own.
    const bool scoped = !out.scope.isAll() || !out.outcome.scope.isAll();
    const QString combinedKey = scopeKeyOf(out.scope, out.outcome.scope);

    if (out.computed) {
        QJsonObject root = toJson(out.themes);
        root[QStringLiteral("derivedFrom")] = QJsonObject{
            { QStringLiteral("fingerprint"),  out.fingerprint },
            { QStringLiteral("contentStamp"), out.contentStamp },
            { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
        };
        if (scoped) {
            QJsonArray records;
            for (auto it = out.records.constBegin(); it != out.records.constEnd(); ++it) records.append(toJson(it.value()));
            root[QStringLiteral("records")] = records;
        }
        if (!out.outcome.scope.isAll()) root[QStringLiteral("outcome")] = outcomeToJson(out.outcome);
        const QString path = themesPath(out.athleteDir, out.scope, out.outcome.scope);
        QString why;
        if (!atomicWrite(path, QJsonDocument(root).toJson(QJsonDocument::Compact), &why))
            ppWarn() << "[SwingThemes] could not write" << qPrintable(path) << "—" << qPrintable(why);

        int firm = 0, probably = 0, possibly = 0, none = 0;
        for (const SwingTheme &t : out.themes.themes) {
            switch (t.tier) {
            case ThemeTier::Firm:     ++firm;     break;
            case ThemeTier::Probably: ++probably; break;
            case ThemeTier::Possibly: ++possibly; break;
            case ThemeTier::None:     ++none;     break;
            }
        }
        ppInfo() << qPrintable(QStringLiteral("[SwingThemes] %1 swings over %2 sessions, k=%3, themes %4 firm / "
                                             "%5 probably / %6 possibly / %7 not shown%8 — %9 ms")
                                  .arg(out.themes.swings).arg(out.themes.sessions).arg(out.themes.k)
                                  .arg(firm).arg(probably).arg(possibly).arg(none)
                                  .arg(out.themes.enough ? QString() : QStringLiteral(" (not enough yet)"))
                                  .arg(out.wallMs));
    }

    const bool wasOtherScope = m_summaryScope != combinedKey;
    m_themes            = std::move(out.themes);
    m_themesFingerprint = out.fingerprint;
    m_summaryReady      = true;
    if (!scoped) m_scopedRecords.clear();
    else m_scopedRecords = std::move(out.records);
    m_shownOutcome = std::move(out.outcome);
    m_summaryScope = m_togetherScope = combinedKey;
    m_scopeWorking = false;
    emit clubScopeStateChanged();
    publishSummary();
    // Through publishScreens, so detailsRevision moves and an open fault re-reads its contrast.
    if (scoped || wasOtherScope) {
        publishScreens();
        maybeMarks();
    }
}

// Coaching and Your swing, ahead of Matchups: Layer 1 and the records. The job carries on with the replicates.
void WorkOnsController::applyScopeSummary(int generation, const QString &scopeKey, QHash<QString, SessionWorkOns> records,
                                          SwingThemes layerOne, ShownOutcome outcome)
{
    if (generation != m_themesGeneration) return;      // cancelled or superseded
    m_scopedRecords = std::move(records);
    m_shownOutcome  = std::move(outcome);
    m_summaryScope  = scopeKey;
    m_themes        = std::move(layerOne);
    m_summaryReady  = true;
    emit clubScopeStateChanged();
    publishSummary();
    publishScreens();
    maybeMarks();
}

void WorkOnsController::applyScopeWorking(int generation)
{
    if (generation != m_themesGeneration || !m_themesRunning) return;
    setScopeWorking(true);
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
    m_nextItems.clear();
    // While Layer 2 is still working for a new scope, Matchups keeps the answer it has.
    const bool together = !matchupsRefreshing();
    if (together) {
        m_togetherItems.clear();
        m_togetherNote.clear();
    }
    if (m_summaryReady) {
        if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
        // The drill set gives the focus its drill's name and instruction.
        const SwingSummaryView view = swingSummaryView(m_themes, themePhrases(m_packProv->pack(), sharedDrillSet()));
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
    if (!m_outcomePack) {
        if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
        m_outcomePack = std::make_unique<ShotOutcomePack>(shotOutcomePackFor(&m_packProv->pack()));
    }
    return *m_outcomePack;
}

// Called once the catch-up is done, beside maybeThemes and maybeMarks. Cheap when nothing moved: the
// job stats the ledgers and stops.
void WorkOnsController::maybeTendencies()
{
    if (m_athleteDir.isEmpty() || m_paused || m_scanning || m_deriving || !m_queue.isEmpty()) return;

    // No session with a swing in it: nothing to reduce, and nothing to write into the folder.
    if (m_fingerprints.isEmpty()) {
        cancelTendencies();
        if (m_tendenciesReady) {
            clearTendencies();
            updateClubScope();      // the shot filter's options were these tendencies' counts
        }
        return;
    }

    cancelTendencies();      // superseded, if one was running

    TendenciesJob job;
    job.generation   = ++m_tendenciesGeneration;
    job.athleteDir   = m_athleteDir;
    job.previous     = m_tendencyFingerprint;
    job.contentStamp = m_model ? m_model->contentStamp() : QString();
    job.sessionDirs  = m_fingerprints.keys();
    std::sort(job.sessionDirs.begin(), job.sessionDirs.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() < QFileInfo(b).fileName();
    });
    // Marshalled here, on the GUI thread; sharedNormProvider()'s first build is not safe on a worker.
    job.pack  = outcomePack();
    job.norms = sharedNormProvider();

    QCryptographicHash seed(QCryptographicHash::Sha1);
    seed.addData(QByteArray::number(kTendencyRuleVersion));
    seed.addData(QByteArray::number(kTendencySchemaVersion));
    seed.addData(job.contentStamp.toUtf8());
    // The flight table every shot's height is judged against (read here first, so the worker only reads it).
    (void)shippedFlightNorms();
    seed.addData(QByteArrayLiteral("\x1d"));
    seed.addData(flightNormsStamp());
    seed.addData(QByteArrayLiteral("\x1d"));
    for (const QString &d : std::as_const(job.sessionDirs)) {
        seed.addData(QFileInfo(d).fileName().toUtf8());
        seed.addData(QByteArrayLiteral("\x1f"));
        seed.addData(m_fingerprints.value(d).toUtf8());
        seed.addData(QByteArrayLiteral("\x1e"));
    }
    job.seed = QString::fromLatin1(seed.result().toHex());

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_tendenciesCancel = cancel;
    const bool wasRunning = m_tendenciesRunning;
    m_tendenciesRunning = true;
    if (!wasRunning) emit tendenciesChanged();

    if (m_synchronous) {
        applyTendencies(job.generation, runTendencies(job, cancel.get()));
        return;
    }
    m_themesPool.start([this, job = std::move(job), cancel]() {
        TendenciesOutcome out = runTendencies(job, cancel.get());
        QMetaObject::invokeMethod(this, [this, generation = job.generation, out = std::move(out)]() mutable {
            applyTendencies(generation, std::move(out));
        }, Qt::QueuedConnection);
    });
}

// Stops the running job, if any. Its result, should it still arrive, is a generation behind and is
// dropped; the tendencies on screen stay.
void WorkOnsController::cancelTendencies()
{
    if (m_tendenciesCancel) m_tendenciesCancel->store(true);
    m_tendenciesCancel.reset();
    ++m_tendenciesGeneration;
    if (!m_tendenciesRunning) return;
    m_tendenciesRunning = false;
    emit tendenciesChanged();
}

// Worker thread (or inline when synchronous). Touches no member.
WorkOnsController::TendenciesOutcome WorkOnsController::runTendencies(const TendenciesJob &job, const std::atomic<bool> *cancel)
{
    QElapsedTimer clock;
    clock.start();
    TendenciesOutcome out;
    out.contentStamp = job.contentStamp;
    out.athleteDir   = job.athleteDir;

    // ── 1. The ledgers' identity: an unchanged library stops here ──
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(job.seed.toUtf8());
    for (const QString &dir : job.sessionDirs) {
        const QFileInfo fi(QDir(dir).filePath(kLedgerFile));
        h.addData(QFileInfo(dir).fileName().toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
        h.addData(QByteArray::number(fi.exists() ? fi.size() : -1));
        h.addData(QByteArray::number(fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0));
        h.addData(QByteArrayLiteral("\x1e"));
    }
    out.fingerprint = QString::fromLatin1(h.result().toHex().left(16));
    if (out.fingerprint == job.previous) {
        out.unchanged = true;
        return out;
    }

    // ── 2. The stored file, when it was reduced from exactly this ──
    {
        QFile f(tendenciesPath(job.athleteDir));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QString stored = root.value(QStringLiteral("derivedFrom")).toObject()
                                       .value(QStringLiteral("fingerprint")).toString();
            bool ok = false;
            TendencySet s = tendencySetFromJson(root, &ok);
            if (ok && stored == out.fingerprint) {
                out.fromDisk = true;
                out.set = std::move(s);
                return out;
            }
        }
    }

    // ── 3. Every session, re-composed shot by shot ──
    std::vector<TendencySessionInput> sessions;
    sessions.reserve(size_t(job.sessionDirs.size()));
    for (const QString &dir : job.sessionDirs) {
        if (cancel && cancel->load()) return out;
        TendencySessionInput in;
        if (readTendencySession(dir, in)) sessions.push_back(std::move(in));
    }

    TendencySet set = tendencySetFromSessions(sessions, job.pack, job.norms->contexts(), cancel);
    if (set.cancelled || (cancel && cancel->load())) return out;

    out.computed = true;
    out.set = std::move(set);
    ppInfo() << qPrintable(QStringLiteral("[Tendencies] %1 shots over %2 sessions, %3 keys, %4 ms")
                               .arg(out.set.countedShots).arg(out.set.countedSessions)
                               .arg(out.set.items.size()).arg(clock.elapsed()));
    return out;
}

void WorkOnsController::applyTendencies(int generation, TendenciesOutcome out)
{
    if (generation != m_tendenciesGeneration) return;      // cancelled or superseded
    m_tendenciesRunning = false;
    m_tendenciesCancel.reset();

    if (out.unchanged || (!out.fromDisk && !out.computed)) {  // the tendencies in hand stand
        emit tendenciesChanged();
        return;
    }

    if (out.computed) {
        ++m_tendencyJobs;
        QJsonObject root = tendencySetToJson(out.set);
        root[QStringLiteral("derivedFrom")] = QJsonObject{
            { QStringLiteral("fingerprint"),  out.fingerprint },
            { QStringLiteral("contentStamp"), out.contentStamp },
            { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
        };
        QString why;
        if (!atomicWrite(tendenciesPath(out.athleteDir), QJsonDocument(root).toJson(QJsonDocument::Compact), &why))
            ppWarn() << "[Tendencies] could not write" << qPrintable(tendenciesPath(out.athleteDir)) << "—" << qPrintable(why);
    }

    m_tendencySet         = std::move(out.set);
    m_tendencyFingerprint = out.fingerprint;
    m_tendenciesReady     = true;
    // As the marks do: the tendencies ride on faultDetail() and the focus, so a landing rebuilds both. publish()
    // ends in updateClubScope, which rebuilds the shot filter's options from these counts and resolves the request.
    publish();
    publishSummary();
    publishTendencies();
    emit tendenciesChanged();
    emit tendenciesLanded();
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
    std::vector<TendencyFlowCombo> combos;
    for (const TendencyItem &it : m_tendencySet.items)
        if (group.isEmpty() || it.clubGroup == group)
            combos.insert(combos.end(), it.flow.begin(), it.flow.end());

    FlowOptions opt;
    opt.plotHeight = std::max(1.0, plotHeight);
    opt.gap        = std::max(0.0, gap);
    const FlowLayout l = layoutTendencyFlow(combos, opt);
    const ShotOutcomePack &pack = outcomePack();
    const auto pct = [&l](int n) { return l.total > 0 ? int(std::lround(100.0 * double(n) / double(l.total))) : 0; };
    const auto shots = [](int n) { return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? QStringLiteral("shot") : QStringLiteral("shots")); };
    const auto labelOf = [&pack](const FlowNode &n) { return n.other ? QStringLiteral("Other") : shotFactorValueLabel(pack, n.value); };

    // The good path in words, from the good values themselves (§3.6): "solid", "on line" (more than Straight is good),
    // "mid or high".
    const auto goodWords = [&pack, &opt](int col) {
        QStringList ids = QStringList(opt.good[size_t(col)].begin(), opt.good[size_t(col)].end());
        if (col == 1 && ids.size() > 1) return QStringLiteral("on line");
        std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return shotFactorValueRank(a) < shotFactorValueRank(b); });
        QStringList words;
        for (const QString &id : ids) words << shotFactorValueLabel(pack, id).toLower();
        return words.join(QStringLiteral(" or "));
    };
    const QString strikeGood = goodWords(0), lineGood = goodWords(1), flightGood = goodWords(2);
    const std::array<QString, kFlowGroups> groupNames{
        outcomeCapitalised(QStringLiteral("%1, %2, %3").arg(strikeGood, lineGood, flightGood)),
        QStringLiteral("On line, flight off"), QStringLiteral("Struck well, off line"), QStringLiteral("Mis-struck") };

    const QString clubWords = group.isEmpty() ? QStringLiteral("All clubs") : tendencyClubLabel(group);
    const QString noun = group.isEmpty() ? QStringLiteral("shots")
                       : group == QLatin1String("driver") ? QStringLiteral("drives")
                                                          : tendencyClubShotWord(group) + QStringLiteral(" shots");

    QVariantList groups;
    for (int g = 0; g < kFlowGroups; ++g)
        groups << QVariantMap{ { QStringLiteral("group"), g }, { QStringLiteral("name"), groupNames[size_t(g)] },
                               { QStringLiteral("count"), l.groups[size_t(g)] }, { QStringLiteral("percent"), pct(l.groups[size_t(g)]) },
                               { QStringLiteral("line"), QStringLiteral("%1 · %2%").arg(l.groups[size_t(g)]).arg(pct(l.groups[size_t(g)])) } };
    // What a node's label says while a node or ribbon is examined: how many of the examined shots pass through it, and
    // what share of them ("2 · 67%"); "" for none, and the node dims.
    const auto examinedSubs = [](const std::vector<int> &at, int of) {
        QVariantList out;
        for (int n : at)
            out << (n > 0 && of > 0 ? QStringLiteral("%1 · %2%").arg(n).arg(int(std::lround(100.0 * double(n) / double(of)))) : QString());
        return out;
    };
    QVariantList nodes;
    for (const FlowNode &n : l.nodes) {
        QVariantList byGroup;
        for (int c : n.byGroup) byGroup << c;
        QString label = labelOf(n);
        nodes << QVariantMap{ { QStringLiteral("column"), n.column }, { QStringLiteral("value"), n.value }, { QStringLiteral("label"), label },
                              { QStringLiteral("good"), n.good }, { QStringLiteral("other"), n.other }, { QStringLiteral("count"), n.count },
                              // its tone in the rows' scheme: good, a value to watch (Low), a fault, or a fold of several
                              { QStringLiteral("kind"), n.good ? QStringLiteral("good") : n.other ? QStringLiteral("other")
                                                      : shotFactorValueWatch(n.value) ? QStringLiteral("watch") : QStringLiteral("fault") },
                              { QStringLiteral("percent"), pct(n.count) },
                              { QStringLiteral("sub"), QStringLiteral("%1 · %2%").arg(n.count).arg(pct(n.count)) },
                              { QStringLiteral("y"), n.y }, { QStringLiteral("h"), n.h }, { QStringLiteral("byGroup"), byGroup },
                              { QStringLiteral("withSub"), examinedSubs(n.with, n.count) } };
    }
    QVariantList links;
    for (const FlowLink &k : l.links) {
        QVariantList through;
        for (int t : k.through) through << t;
        links << QVariantMap{ { QStringLiteral("from"), k.fromNode }, { QStringLiteral("to"), k.toNode }, { QStringLiteral("group"), int(k.group) },
                              { QStringLiteral("count"), k.count }, { QStringLiteral("percent"), pct(k.count) },
                              { QStringLiteral("y0"), k.y0 }, { QStringLiteral("y1"), k.y1 }, { QStringLiteral("h"), k.h },
                              { QStringLiteral("through"), through },
                              { QStringLiteral("bySub"), examinedSubs(k.byNode, k.count) },
                              { QStringLiteral("line"), QStringLiteral("%1 → %2: %3 (%4%)")
                                    .arg(labelOf(l.nodes[size_t(k.fromNode)]), labelOf(l.nodes[size_t(k.toNode)]), shots(k.count))
                                    .arg(pct(k.count)) } };
    }

    return QVariantMap{
        { QStringLiteral("enough"),   l.enough() },
        { QStringLiteral("total"),    l.total },
        { QStringLiteral("excluded"), l.excluded },
        { QStringLiteral("idealPct"), pct(l.groups[0]) },
        { QStringLiteral("micro"),    QStringLiteral("Tendencies · %1 · %2").arg(clubWords, shots(l.total)) },
        { QStringLiteral("headline"), QStringLiteral("%1% of your %2 were %3, %4 and flew %5")
                                          .arg(pct(l.groups[0])).arg(noun, strikeGood, lineGood, flightGood) },
        { QStringLiteral("subtitle"), QStringLiteral("Read left to right: strike, then direction, then flight. Green is the path you "
                                                     "want. Every other colour shows where a shot first left it.") },
        { QStringLiteral("emptyLine"), QStringLiteral("Hit at least %1 shots with strike, direction and flight all read, with %2, "
                                                      "to see how your shots flow.")
                                           .arg(kTendencyFlowMinShots).arg(group.isEmpty() ? QStringLiteral("any club") : QStringLiteral("these clubs")) },
        { QStringLiteral("excludedLine"), l.excluded == 0 ? QString()
                                          : QStringLiteral("%1 not shown: strike, direction or flight wasn't read.").arg(shots(l.excluded)) },
        { QStringLiteral("columns"), QVariantList{ QStringLiteral("Strike"), QStringLiteral("Direction"), QStringLiteral("Flight") } },
        { QStringLiteral("groups"), groups },
        { QStringLiteral("nodes"),  nodes },
        { QStringLiteral("links"),  links },
    };
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
            { QStringLiteral("earned"),    n >= kClubBaselineMinShots },
            { QStringLiteral("line"),      o.value(QStringLiteral("carryText")).toString() },
        };
    }
    return QVariantMap();
}

QVariantMap WorkOnsController::onBallFor(const QString &conditionId) const
{
    for (const TendencyOnBall &b : m_tendencySet.onBall) {
        if (b.conditionId != conditionId || !onBallInScope(b.tendencyKey)) continue;
        QVariantList produced;
        for (const TendencyProduced &p : b.produced)
            produced.append(QVariantMap{ { QStringLiteral("phrase"), p.phrase }, { QStringLiteral("count"), p.count } });
        return QVariantMap{
            { QStringLiteral("conditionId"),  b.conditionId },
            { QStringLiteral("text"),         b.text },
            { QStringLiteral("produced"),     produced },
            { QStringLiteral("costWords"),    b.costWords },
            { QStringLiteral("tendencyKey"),  b.tendencyKey },
            { QStringLiteral("factor"),       b.factor },
            { QStringLiteral("value"),        b.value },
            { QStringLiteral("tendencyText"), b.tendencyText },
            { QStringLiteral("onTheBall"),    b.onTheBall },
        };
    }
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
    if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
    return packLabel(m_packProv->pack(), id);
}

void WorkOnsController::clearSummary()
{
    m_themes = SwingThemes();
    m_themesFingerprint.clear();
    m_summaryReady = false;
    publishSummary();
}
