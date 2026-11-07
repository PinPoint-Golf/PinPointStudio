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

#include "../../Coaching/swing_areas_pack.h"

#include "../../Diagnostics/packs/characteristic_pack.h"
#include "../../Diagnostics/packs/drill_pack.h"
#include "../../Diagnostics/engine/pack_io.h"
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Diagnostics/packs/swing_themes_pack.h"
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

// THE VERSION OF THE CONSISTENCY MARKS' RULE (the window, the filters, the scale): in the marks job's
// fingerprint, so a new rule grades again. Nothing is stored, so a bump only costs the next settle a read.
constexpr int kConsistencyMarksRule = 2;     // 2: warm-ups and data-warning shots in (R13), outcomes ungraded (R4)

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

} // namespace

WorkOnsController::WorkOnsController(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_themesPool.setMaxThreadCount(1);
    m_marksPool.setMaxThreadCount(1);
    m_marksPool.setThreadPriority(QThread::LowPriority);
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

SessionDiagnosticsModel *WorkOnsController::model()
{
    if (!m_model) {
        m_model = new SessionDiagnosticsModel(this);
        m_model->setSynchronous(m_synchronous);
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
    // The marks are this golfer's ledgers' alone.
    cancelMarks();
    m_marks.clear();
    m_marksFingerprint.clear();
    m_markRequests.clear();
    clearSummary();
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

    // Newest first: the latest session is the one the golfer is about to look for.
    std::sort(rows.begin(), rows.end(),
              [](const Scanned &a, const Scanned &b) { return a.mtimeMs > b.mtimeMs; });
    QStringList queue;
    for (Scanned &s : rows) {
        m_fingerprints.insert(s.sessionDir, s.fingerprint);
        m_dirMtime.insert(s.sessionDir, s.mtimeMs);
        if (s.fresh) m_records.insert(s.sessionDir, std::move(s.record));
        else         queue << s.sessionDir;
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
        return (updating() && !(m_paused && !m_scanning && !m_deriving)) || m_themesRunning || m_marksRunning;
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
        if (m_themesRunning) m_themesPool.waitForDone(20);
        if (m_marksRunning) m_marksPool.waitForDone(20);
    }
    return !m_scanning && !m_deriving && !m_themesRunning && !m_marksRunning;
}

// ── Publish ─────────────────────────────────────────────────────────────────────────────

void WorkOnsController::publish()
{
    if (!m_packProv) m_packProv = makeCharacteristicPackProvider();
    const CharacteristicPack &pack = m_packProv->pack();
    const DrillSet &drills = sharedDrillSet();

    std::vector<SessionWorkOns> sessions;
    QHash<QString, QString> dirOf;      // session id -> dir
    QSet<QString> substantive;          // sessions that could say something about something
    const SessionWorkOns *newest = nullptr;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        const SessionWorkOns &r = it.value();
        sessions.push_back(r);
        dirOf.insert(r.sessionId, it.key());
        if (r.substantive()) substantive.insert(r.sessionId);
        // The newest by when it was played — the aggregate's own order (start, then id).
        if (!newest || r.startMs > newest->startMs || (r.startMs == newest->startMs && r.sessionId > newest->sessionId))
            newest = &r;
    }
    int totalSwings = 0;
    for (const SessionWorkOns &s : sessions) totalSwings += s.shotCount;

    m_lastSession.clear();
    if (newest)
        m_lastSession = QVariantMap{
            { QStringLiteral("label"), weekdayLabel(newest->startMs) },
            { QStringLiteral("shots"), newest->shotCount },
        };

    // What the consistency marks are asked for, gathered afresh below (each area item, each detail).
    m_markRequests.clear();

    // The areas first, while `sessions` is still whole: summariseAreas copies what it sorts. One call —
    // the every-condition aggregate, and the same again without the newest substantive session for the
    // trend — per publish, on this thread; the aggregate's per-session evidence index keeps it linear.
    publishAreas(pack, sessions);

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
        // A later session that measured OTHER things and not this one. Three warm-up balls
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

        // Its consistency mark, on the measure an area item names (AreaItem::measureId's rule), so the
        // detail and its row agree. It sits beside the work-on's status (R10).
        const QString measureId = (w.hasLatestEvidence && !w.latestEvidence.measureId.isEmpty())
                                      ? w.latestEvidence.measureId
                                      : (w.hasLatest ? w.latest.measureId : QString());
        m[QStringLiteral("consistency")] = markFor(w.id, measureId);
        const QString key = markKey(w.id, measureId);
        if (!m_markRequests.contains(key)) {
            const Measure *mm = pack.measure(measureId);
            m_markRequests.insert(key, MarksRequest{ w.id, measureId, mm ? mm->unit : QString() });
        }

        // Where to go and look: the newest session in which it was a pattern.
        m[QStringLiteral("sessionDir")]   = lastPattern ? dirOf.value(lastPattern->sessionId) : QString();
        m[QStringLiteral("sessionLabel")] = lastPattern ? dayLabel(lastPattern->startMs) : QString();
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
        details.insert(w.id, m);
    }
    for (const WorkOnSummary &w : agg) {
        if (w.status == WorkOnStatus::Cleared) { ++cleared; continue; }
        const QVariantMap m = describe(w);
        details.insert(w.id, m);        // a listed one's detail is its row, exactly
        items.append(m);
    }

    m_items = items;
    m_details = details;
    ++m_detailsRevision;
    m_clearedCount = cleared;
    emit itemsChanged();
    emit areasChanged();
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
        // Every shot, warm-ups and data-warning shots included (R13; variability_ledger.h): a broken recording's rows
        // are already NotAssessable in the ledger, and the shared rule leaves those out.
        for (const ShotRecord &shot : shots) {
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
            publish();      // the marks ride on areaItems and faultDetail(): a changed one rebuilds them
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

    // ── 1. The stored summary, when it was reduced from exactly this ──
    {
        QFile f(themesPath(job.athleteDir));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QString stored = root.value(QStringLiteral("derivedFrom")).toObject()
                                       .value(QStringLiteral("fingerprint")).toString();
            bool ok = false;
            SwingThemes st = swingThemesFromJson(root, &ok);     // another schema or rule: not ok
            if (ok && !st.cancelled && stored == job.fingerprint) {
                out.fromDisk = true;
                out.themes   = std::move(st);
                out.wallMs   = clock.elapsed();
                return out;
            }
        }
    }

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
        in.shots = fromJson(doc.object().value(QStringLiteral("ledger")).toObject());
        sessions.push_back(std::move(in));
    }

    // ── 3. Reduce, the replicates fanned out ──
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
    SwingThemes st = reduceSwingThemes(sessions, job.measures, job.conditions, ThemeOptions{}, parallelFor, cancel);
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
        emit summaryChanged();
        return;
    }

    if (out.computed) {
        QJsonObject root = toJson(out.themes);
        root[QStringLiteral("derivedFrom")] = QJsonObject{
            { QStringLiteral("fingerprint"),  out.fingerprint },
            { QStringLiteral("contentStamp"), out.contentStamp },
            { QStringLiteral("writtenAtMs"),  double(QDateTime::currentMSecsSinceEpoch()) },
        };
        QString why;
        if (!atomicWrite(themesPath(out.athleteDir), QJsonDocument(root).toJson(QJsonDocument::Compact), &why))
            ppWarn() << "[SwingThemes] could not write" << qPrintable(themesPath(out.athleteDir)) << "—" << qPrintable(why);

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

    m_themes            = std::move(out.themes);
    m_themesFingerprint = out.fingerprint;
    m_summaryReady      = true;
    publishSummary();
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
    m_togetherItems.clear();
    m_togetherNote.clear();
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
        const auto fault = [&pips](const NeedsWorkItem &n) {
            return QVariantMap{
                { QStringLiteral("text"),      n.text },
                { QStringLiteral("share"),     n.share },
                { QStringLiteral("frequency"), n.frequency },
                { QStringLiteral("trend"),     n.trend },
                { QStringLiteral("sessions"),  pips(n.pips) },
                // So Home's row can open the fault's detail in place (its work-on's `id`).
                { QStringLiteral("conditionId"), n.conditionId },
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
        for (const TogetherItem &t : view.together)
            m_togetherItems.append(QVariantMap{
                { QStringLiteral("tier"),       t.tier },
                { QStringLiteral("first"),      t.first },
                { QStringLiteral("second"),     t.second },
                { QStringLiteral("startStop"),  t.startStop },
                { QStringLiteral("startWords"), t.startWords },
                { QStringLiteral("trend"),      t.trend },
            });
        m_togetherNote = view.note;
    }
    emit summaryChanged();
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
