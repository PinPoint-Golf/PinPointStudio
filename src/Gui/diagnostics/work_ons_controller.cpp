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

#include "../../Diagnostics/characteristic_pack.h"
#include "../../Diagnostics/drill_pack.h"
#include "../../Diagnostics/pack_io.h"
#include "../../Diagnostics/pack_provider.h"
#include "../../Export/swing_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace pinpoint::analysis;

namespace {

const QString kRecordFile = QStringLiteral("work_ons.json");

QString fmtNumber(double v)
{
    QString s = QString::number(std::fabs(v), 'f', 1);
    return v < 0.0 ? QString(QChar(0x2212)) + s : s;
}

// "62.0°" but "12.0 mm": a symbol closes up, a word does not.
QString withUnit(const QString &s, const QString &unit)
{
    if (unit.isEmpty()) return s;
    return unit.at(0).isLetter() ? s + QLatin1Char(' ') + unit : s + unit;
}

QString dayLabel(qint64 ms)
{
    if (ms <= 0) return QString();
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms).date();
    return QLocale::c().toString(d, d.year() == QDate::currentDate().year()
                                        ? QStringLiteral("d MMM") : QStringLiteral("d MMM yyyy"));
}

// A COUNT of swings, never a percentage — recurrenceText()'s rule, one level up.
QString swingsText(int fired, int total)
{
    return total == 1 ? QStringLiteral("%1 of 1 swing").arg(fired)
                      : QStringLiteral("%1 of %2 swings").arg(fired).arg(total);
}

} // namespace

WorkOnsController::WorkOnsController(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

WorkOnsController::~WorkOnsController()
{
    // The pool's lambda delivers back to `this`; drain it before the members go.
    m_pool.waitForDone();
}

QString WorkOnsController::recordPath(const QString &sessionDir)
{
    return QDir(sessionDir).filePath(kRecordFile);
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
    // The previous golfer's list must not sit under this one's name while the scan runs.
    m_records.clear();
    m_queue.clear();
    m_sessionsFound = 0;
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
// answered), and the model reconciles the ledger against these same documents anyway.
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

        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(QByteArray::number(kWorkOnRuleVersion));
        h.addData(contentStamp.toUtf8());
        for (const QString &name : swings) {
            const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(d.filePath(name));
            h.addData(name.toUtf8());
            h.addData(QByteArray::number(doc.size));
            h.addData(QByteArray::number(doc.mtimeMs));
        }

        Scanned s;
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
    QDeadlineTimer deadline(msTimeout);
    while (updating() && !(m_paused && !m_scanning && !m_deriving) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_scanning && !m_deriving;
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
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        sessions.push_back(it.value());
        dirOf.insert(it.value().sessionId, it.key());
        if (it.value().substantive()) substantive.insert(it.value().sessionId);
    }
    int totalSwings = 0;
    for (const SessionWorkOns &s : sessions) totalSwings += s.shotCount;
    const std::vector<WorkOnSummary> agg = aggregateWorkOns(std::move(sessions));

    const auto nameOf = [&pack](const QString &id, const QString &fallback = QString()) {
        const Condition *c = pack.condition(id);
        if (c && !c->label.isEmpty()) return c->label;
        return fallback.isEmpty() ? id : fallback;
    };

    QVariantList items;
    int cleared = 0;
    for (const WorkOnSummary &w : agg) {
        if (w.status == WorkOnStatus::Cleared) { ++cleared; continue; }

        const Condition *cond = pack.condition(w.id);
        QVariantMap m;
        m[QStringLiteral("id")]        = w.id;
        m[QStringLiteral("name")]      = nameOf(w.id, w.name);
        m[QStringLiteral("status")]    = workOnStatusToString(w.status);
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
        // Ranked on evidence the newest sessions could not renew. The row says so where it is
        // read, because a high place on old evidence must not pass for a fresh finding.
        const bool unconfirmed = w.status == WorkOnStatus::Active && unmeasuredSince && lastConclusive;
        m[QStringLiteral("unconfirmed")]  = unconfirmed;
        m[QStringLiteral("lastSeenText")] = unconfirmed
            ? QStringLiteral("last seen %1").arg(dayLabel(lastConclusive->startMs)) : QString();

        // The latest session that listed it: the count, and the reading where one can be stated.
        QString latestText;
        if (w.hasLatest) {
            const WorkOnEntry &e = w.latest;
            qint64 when = 0;
            for (const WorkOnPoint &p : w.history) if (p.sessionId == w.latestSessionId) when = p.startMs;
            latestText = QStringLiteral("%1 · %2").arg(dayLabel(when), recurrenceText(e.fired, e.assessable));
            if (e.hasTypical) {
                const Measure *mm = pack.measure(e.measureId);
                const QString unit = mm ? mm->unit : QString();
                // THE SAME SENTENCE THE PANEL PRINTS (session_spread.h spreadCorridorWords): the pass
                // band and the fault line. This used to quote the stored Ideal band as "at most
                // 4.3", which is neither edge the session panel draws nor where anything fires.
                ConditionRow band;
                band.corridorLo    = e.corridorLo;
                band.corridorHi    = e.corridorHi;
                band.corridorShape = e.corridorShape;
                const SpreadCorridor sc = spreadCorridorFromRow(band, gradePolicyByName(m_gradePolicy));
                const QString corridor = sc.known ? spreadCorridorWords(sc, unit) : QString();
                if (!corridor.isEmpty())
                    latestText += QStringLiteral(" · typically %1 against %2")
                                      .arg(withUnit(fmtNumber(e.typicalValue), unit), corridor);
            }
            QStringList causes, maybe;
            for (const QString &c : e.causedBy)  causes << nameOf(c);
            for (const QString &c : e.mayFollow) maybe  << nameOf(c);
            QStringList lines;
            if (!causes.isEmpty()) lines << QStringLiteral("Follows from ") + causes.join(QStringLiteral(", "));
            if (!maybe.isEmpty())
                lines << QStringLiteral("May follow from %1 — these swings cannot show it")
                             .arg(maybe.join(QStringLiteral(", ")));
            m[QStringLiteral("causedByText")] = lines.join(QStringLiteral(" · "));
        }
        m[QStringLiteral("latestText")] = latestText;

        // "What do I do about it" — the pack's first drill for this condition, when it authors one.
        const Drill *drill = nullptr;
        if (cond)
            for (const QString &id : cond->drills)
                if ((drill = drills.drill(id))) break;
        m[QStringLiteral("drillLabel")]       = drill ? drill->label : QString();
        m[QStringLiteral("drillInstruction")] = drill ? drill->instruction : QString();

        // Where to go and look: the newest session in which it was a pattern.
        m[QStringLiteral("sessionDir")]   = lastPattern ? dirOf.value(lastPattern->sessionId) : QString();
        m[QStringLiteral("sessionLabel")] = lastPattern ? dayLabel(lastPattern->startMs) : QString();

        items.append(m);
    }

    m_items = items;
    m_clearedCount = cleared;
    emit itemsChanged();
}
