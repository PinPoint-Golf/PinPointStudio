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

#include "athlete.h"
#include "athlete_repository.h"
#include "session.h"
#include "shot.h"   // the tuning log hears each Shot's EVT-06 (step 3.4b-i)

#include "../Core/pp_debug.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>

#include <algorithm>

namespace pinpoint {

// ── The record ───────────────────────────────────────────────────────────────────────────────────

namespace {

const QString kName              = QStringLiteral("name");
const QString kHandedness        = QStringLiteral("handedness");
const QString kHeightValue       = QStringLiteral("heightValue");
const QString kHeightUnit        = QStringLiteral("heightUnit");
const QString kWeightValue       = QStringLiteral("weightValue");
const QString kWeightUnit        = QStringLiteral("weightUnit");
const QString kHandicap          = QStringLiteral("handicap");
const QString kPrimaryClub       = QStringLiteral("primaryClub");
const QString kSpeedTarget       = QStringLiteral("speedTarget");
const QString kNotes             = QStringLiteral("notes");
const QString kDob               = QStringLiteral("dob");
const QString kSex               = QStringLiteral("sex");
const QString kReportEmail       = QStringLiteral("reportEmail");
const QString kReportGreeting    = QStringLiteral("reportGreeting");
const QString kReportPhotosOk    = QStringLiteral("reportPhotosApproved");
const QString kReportPhotosOkOn  = QStringLiteral("reportPhotosApprovedOn");
const QString kCreatedAt         = QStringLiteral("createdAt");
const QString kClubs             = QStringLiteral("clubs");
const QString kWristReference    = QStringLiteral("wristReference");
// Derived (DEC-89): never part of the record, dropped on the way in.
const QString kLastSessionAt     = QStringLiteral("lastSessionAt");
const QString kSessionCount      = QStringLiteral("sessionCount");

// A settings value comes back as a string from an INI file; a JSON one as its own type. Both read the
// same through QVariant's conversions.
bool asBool(const QVariant &v)
{
    if (v.typeId() == QMetaType::QString)
        return v.toString() == QLatin1String("true") || v.toString() == QLatin1String("1");
    return v.toBool();
}

} // namespace

const QStringList &AthleteRecord::knownKeys()
{
    static const QStringList keys{ kName, kHandedness, kHeightValue, kHeightUnit, kWeightValue, kWeightUnit,
                                   kHandicap, kPrimaryClub, kSpeedTarget, kNotes, kDob, kSex, kReportEmail,
                                   kReportGreeting, kReportPhotosOk, kReportPhotosOkOn, kCreatedAt, kClubs,
                                   kWristReference };
    return keys;
}

QVariant AthleteRecord::value(const QString &key) const
{
    if (key == kName)              return name;
    if (key == kHandedness)        return handedness;
    if (key == kHeightValue)       return heightValue;
    if (key == kHeightUnit)        return heightUnit;
    if (key == kWeightValue)       return weightValue;
    if (key == kWeightUnit)        return weightUnit;
    if (key == kHandicap)          return handicap;
    if (key == kPrimaryClub)       return primaryClub;
    if (key == kSpeedTarget)       return speedTarget;
    if (key == kNotes)             return notes;
    if (key == kDob)               return dob;
    if (key == kSex)               return sex;
    if (key == kReportEmail)       return reportEmail;
    if (key == kReportGreeting)    return reportGreeting;
    if (key == kReportPhotosOk)    return reportPhotosApproved;
    if (key == kReportPhotosOkOn)  return reportPhotosApprovedOn;
    if (key == kCreatedAt)         return createdAt;
    if (key == kClubs)             return hasBag ? QVariant(bag) : QVariant();
    if (key == kWristReference)    return wristReference.isNull() ? QVariant() : QVariant(wristReference.toString());
    return extra.value(key);
}

void AthleteRecord::setValue(const QString &key, const QVariant &v)
{
    if      (key == kName)             name                   = v.toString();
    else if (key == kHandedness)       handedness             = v.toString();
    else if (key == kHeightValue)      heightValue            = v.toDouble();
    else if (key == kHeightUnit)       heightUnit             = v.toString();
    else if (key == kWeightValue)      weightValue            = v.toDouble();
    else if (key == kWeightUnit)       weightUnit             = v.toString();
    else if (key == kHandicap)         handicap               = v.toDouble();
    else if (key == kPrimaryClub)      primaryClub            = v.toString();
    else if (key == kSpeedTarget)      speedTarget            = v.toDouble();
    else if (key == kNotes)            notes                  = v.toString();
    else if (key == kDob)              dob                    = v.toString();
    else if (key == kSex)              sex                    = v.toString();
    else if (key == kReportEmail)      reportEmail            = v.toString();
    else if (key == kReportGreeting)   reportGreeting         = v.toString();
    else if (key == kReportPhotosOk)   reportPhotosApproved   = asBool(v);
    else if (key == kReportPhotosOkOn) reportPhotosApprovedOn = v.toString();
    else if (key == kCreatedAt)        createdAt              = v.toLongLong();
    else if (key == kClubs)          { bag = v.toMap(); hasBag = v.isValid(); }
    else if (key == kWristReference)   wristReference         = ShotRef::fromString(v.toString());
    else if (key == kLastSessionAt || key == kSessionCount) return;   // derived (DEC-89)
    else if (v.isValid())              extra.insert(key, v);
    else                               extra.remove(key);
}

QVariantMap AthleteRecord::toMap() const
{
    QVariantMap m = extra;
    for (const QString &k : knownKeys()) {
        const QVariant v = value(k);
        if (v.isValid())
            m.insert(k, v);
    }
    return m;
}

AthleteRecord AthleteRecord::fromMap(const QVariantMap &m)
{
    AthleteRecord r;
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        r.setValue(it.key(), it.value());
    return r;
}

// ── The Athlete ──────────────────────────────────────────────────────────────────────────────────

Athlete::Athlete(AthleteRepository *repository, SessionRepository *sessions, const AthleteId &id, QObject *parent)
    : QObject(parent), m_repository(repository), m_sessions(sessions), m_id(id)
{
}

void Athlete::setStored(const AthleteRecord &record, const QString &folder, const SessionOwner &owner,
                        Source source, bool readOnly)
{
    const bool changed = record != m_record || folder != m_folder;
    m_record   = record;
    m_folder   = folder;
    m_owner    = owner;
    m_source   = source;
    m_readOnly = readOnly;
    if (changed)
        emit recordChanged();
}

QList<SessionIndexEntry> Athlete::index() const
{
    return m_sessions ? m_sessions->enumerate(m_owner) : QList<SessionIndexEntry>{};
}

bool Athlete::hasSessionInProgress() const
{
    const QList<SessionIndexEntry> entries = index();
    return std::any_of(entries.cbegin(), entries.cend(),
                       [](const SessionIndexEntry &e) { return e.status == SessionStatus::InProgress; });
}

AthleteActivity Athlete::activityOf(const QList<SessionIndexEntry> &index, const SessionRepository &sessions)
{
    AthleteActivity a;
    for (const SessionIndexEntry &e : index) {
        if (sessions.shots(e.id()).isEmpty())
            continue;   // nothing was hit in it: not a session (the Sessions list's rule)
        ++a.sessions;
        qint64 at = 0;
        if (e.startedAtUtc.isValid())
            at = e.startedAtUtc.toSecsSinceEpoch();
        else if (e.identity.localDay().isValid())
            at = QDateTime(e.identity.localDay(), QTime(0, 0)).toSecsSinceEpoch();
        a.lastAtSec = std::max(a.lastAtSec, at);
    }
    return a;
}

ClubSet Athlete::clubSetOn(const QDate &on) const
{
    return pinpoint::clubSetOn(m_record.sex, QDate::fromString(m_record.dob, Qt::ISODate), on);
}

WriteResult Athlete::editRecord(const AthleteRecord &record)
{
    if (!m_repository)
        return { WriteResult::Status::Failed, QStringLiteral("No athlete store.") };
    return m_repository->save(*this, record);
}

WriteResult Athlete::editBag(const QVariantMap &bag)
{
    AthleteRecord r = m_record;
    r.bag    = bag;
    r.hasBag = true;
    return editRecord(r);
}

WriteResult Athlete::removeClub(const QString &clubId)
{
    AthleteRecord r = m_record;
    if (r.bag.remove(clubId) == 0)
        return { WriteResult::Status::Unchanged, {} };
    r.hasBag = true;
    return editRecord(r);
}

AthleteRemoval Athlete::remove()
{
    AthleteRemoval out;
    if (!m_repository) {
        out.result = { WriteResult::Status::Failed, QStringLiteral("No athlete store.") };
        return out;
    }
    if (hasSessionInProgress()) {
        out.result = { WriteResult::Status::Failed,
                       QStringLiteral("A session of %1 is in progress: end it before deleting the athlete.")
                           .arg(m_record.name) };
        out.note = out.result.error;
        return out;
    }
    return m_repository->trash(m_id);
}

// ── Its sessions (step 3.4a) ─────────────────────────────────────────────────────────────────────

namespace {
WriteResult refused(const QString &why) { return { WriteResult::Status::Failed, why }; }
}

bool Athlete::ensureHeld(const SessionId &id)
{
    if (!m_sessions || id.isNull())
        return false;
    if (!m_sessions->open(id))
        m_sessions->enumerate(m_owner);   // the index read holds every session of the athlete
    return m_sessions->open(id).has_value();
}

Session *Athlete::hold(const SessionId &id, bool startedHere, bool onDemand)
{
    if (Session *s = m_loaded.value(id))
        return s;
    const auto e = m_sessions ? m_sessions->open(id) : std::nullopt;
    if (!e || e->athlete != m_id)
        return nullptr;   // not held, or another athlete's session
    Session *s = new Session(this, m_sessions, m_shotDoors, id, startedHere, onDemand);
    m_loaded.insert(id, s);
    // Every loaded session's diagnostics run (DEC-02), when the composition root gave the doors (step 3.5-i).
    if (m_diagnosticsDoors.attach)
        m_diagnosticsDoors.attach(s, m_diagnosticsDoors);
    emit sessionHeld(s);   // its AthleteDiagnostics hears it (step 3.6)
    return s;
}

bool Athlete::releaseSession(const SessionId &id, QString *whyNot)
{
    const auto refuse = [whyNot](const QString &why) {
        if (whyNot)
            *whyNot = why;
        return false;
    };
    Session *s = m_loaded.value(id);
    if (!s)
        return refuse(QStringLiteral("That session is not loaded."));
    if (s->runningHere() && s->inProgress())
        return refuse(QStringLiteral("That session is being recorded here."));
    if (s->uses() > 0)
        return refuse(QStringLiteral("That session is in use."));
    // What its diagnostics kept is written FIRST and their waiting jobs cancelled (§4.4 steps 8–9), on this
    // thread, before the session leaves the identity map; a job still running is dropped at the receiver.
    emit s->releasing();
    m_loaded.remove(id);
    ppInfo() << "[Athlete] session released:" << id.toString();
    s->deleteLater();   // a view still bound to it reads a live object until the event loop runs
    return true;
}

int Athlete::releaseUnused()
{
    int n = 0;
    const QList<SessionId> ids = m_loaded.keys();
    for (const SessionId &id : ids)
        if (releaseSession(id))
            ++n;
    return n;
}

void Athlete::endRunningSessions(const SessionId &except)
{
    const QList<Session *> loaded = m_loaded.values();
    for (Session *s : loaded)
        if (s->id() != except && s->runningHere() && s->inProgress())
            s->endSession(/*discardIfEmpty=*/false);   // EVT-09; never the empty discard (today's rule)
}

Session *Athlete::startSession(const QString &type, const QString &namingPattern, WriteResult *result,
                               SessionStart how)
{
    if (!m_sessions) {
        if (result)
            *result = refused(QStringLiteral("No session store."));
        return nullptr;
    }
    endRunningSessions({});   // one InProgress per athlete (DEC-78); a crash's leftovers: the store's start
    const SessionStartResult r = m_sessions->start(m_owner, type, namingPattern);
    if (result)
        *result = r.result;
    if (!r.ok())
        return nullptr;
    Session *s = hold(r.id, /*startedHere=*/true, how == SessionStart::OnDemand);
    if (s) {
        ppInfo() << "[Athlete] EVT-17 session started:" << r.id.toString()
                 << (how == SessionStart::OnDemand ? "(on demand)" : "");
        emit sessionStarted(r.id);   // EVT-17
    }
    return s;
}

SessionId Athlete::todaysSession(const QString &type) const
{
    return m_sessions ? m_sessions->todaysSession(m_owner, type) : SessionId();
}

Session *Athlete::resumeSession(const SessionId &id, const QString &type, WriteResult *result)
{
    if (!ensureHeld(id)) {
        if (result)
            *result = refused(QStringLiteral("That session is not known."));
        return nullptr;
    }
    if (m_sessions->open(id)->athlete != m_id) {
        if (result)
            *result = refused(QStringLiteral("That session is another athlete's."));
        return nullptr;
    }
    // The session already running here: nothing to resume (no write, no event), but it is resumed work from
    // now on, so never the empty discard's (as today's extend made it, which ended it and resumed it). ONLY
    // WHERE DEC-134 WOULD ALLOW THE RESUME: today's session of that type, as the repository judges it
    // (todaysSession: its clock and time zone). Any other day or type falls through to the rules below, and
    // the repository refuses it, as before.
    if (Session *running = m_loaded.value(id);
        running && running->runningHere() && running->inProgress() && todaysSession(type) == id) {
        if (result)
            *result = { WriteResult::Status::Unchanged, {} };
        running->m_startedHere = false;
        running->m_onDemand    = false;
        return running;
    }
    // The session running here is ended FIRST, with its end observed (DEC-134: "any other InProgress session of
    // the athlete is ended first"), as today's extend does; then the repository judges the resume: only a
    // session started today, of the same type, with its own SessionId (DEC-77, DEC-134). A refused resume
    // leaves that earlier session ended, as today (the caller then starts a new one).
    endRunningSessions(id);
    const WriteResult r = m_sessions->resume(id, type);
    if (result)
        *result = r;
    if (!r.ok())
        return nullptr;
    Session *s = hold(id, /*startedHere=*/false);
    if (!s)
        return nullptr;
    s->m_startedHere = false;   // somebody's earlier work: never the empty discard's
    s->m_onDemand    = false;
    s->m_runningHere = true;
    ppInfo() << "[Athlete] EVT-23 session resumed:" << id.toString();
    emit sessionResumed(id);   // EVT-23
    return s;
}

WriteResult Athlete::trashSession(const SessionId &id, QString *where)
{
    if (!ensureHeld(id))
        return refused(QStringLiteral("That session is not known."));
    if (m_sessions->open(id)->athlete != m_id)
        return refused(QStringLiteral("That session is another athlete's."));
    const WriteResult r = m_sessions->trash(id, where);   // refused while in progress (J-46)
    if (!r.ok())
        return r;
    Session *s = m_loaded.take(id);
    ppInfo() << "[Athlete] EVT-20 session trashed:" << id.toString();
    emit sessionTrashed(id);   // EVT-20
    if (s)
        s->deleteLater();      // a view still bound to it reads a live object until the event loop runs
    return r;
}

Session *Athlete::sessionAt(const QString &folder)
{
    if (!m_sessions || folder.isEmpty())
        return nullptr;
    // The folder translated ONCE, here, at the edge (DEC-90): the session file's id, or D3's for a session
    // older than step 2, named for this athlete as its folder's owner.
    // Only a session folder in this athlete's folder is one of its sessions (path text only, nothing read).
    if (QDir::cleanPath(folder + QStringLiteral("/..")) != QDir::cleanPath(m_folder))
        return nullptr;
    // A SESSION ALREADY HELD IS ANSWERED FROM MEMORY (refactor step 3.4b-ii, review 1: CN-2): the repository's
    // identity map knows its folder, so no session.json is read again. Every row, edit and refresh asks this; only a
    // folder not held yet is read (once).
    const QString clean = QDir::cleanPath(folder);
    for (Session *s : std::as_const(m_loaded))
        if (const auto e = m_sessions->open(s->id()); e && QDir::cleanPath(e->folder) == clean)
            return s;
    const SessionId id = m_sessions->openFolder(folder, m_id);
    return id.isNull() ? nullptr : hold(id, /*startedHere=*/false);
}

Session *Athlete::session(const SessionId &id)
{
    if (Session *s = m_loaded.value(id))
        return s;
    return ensureHeld(id) ? hold(id, /*startedHere=*/false) : nullptr;
}

QList<Session *> Athlete::sessions()
{
    QList<Session *> out;
    for (const SessionIndexEntry &e : index())
        if (!e.id().isNull())
            if (Session *s = hold(e.id(), /*startedHere=*/false))
                out.append(s);
    return out;
}

// ── Its tuning log (refactor step 3.4b-i; DEC-43) ──────────────────────────────────────────────────────────

void Athlete::hearLabelCorrections(Shot *shot)
{
    if (!shot)
        return;
    connect(shot, &Shot::labelCorrected, this, [this, shot](const ShotRef &) { appendLabelCorrection(*shot); },
            Qt::DirectConnection);
}

void Athlete::appendLabelCorrection(const Shot &shot)
{
    // THE LINE AS THE TUNING TOOLS READ IT (shot_outcomes_design.md §4.4; one JSON object per line), with today's
    // keys and values: the session folder's name and the swing folder as the caller filed the shot, the swing
    // number as `shotId`, and the moment of the append.
    const LabelCorrectionNote &n = shot.lastLabelCorrection();
    const QJsonObject line{
        { QStringLiteral("kind"),      QStringLiteral("correction") },
        { QStringLiteral("session"),   n.sessionName },
        { QStringLiteral("shotId"),    shot.swingNumber() },
        { QStringLiteral("swingDir"),  n.swingDir },
        { QStringLiteral("family"),    n.family },
        { QStringLiteral("detected"),  n.detected },
        { QStringLiteral("corrected"), n.corrected },
        { QStringLiteral("measures"),  n.measures },
        { QStringLiteral("atMs"),      double(QDateTime::currentMSecsSinceEpoch()) },
    };
    appendTuningLine(line, shot.ref());
}

WriteResult Athlete::flagForTuning(const ShotRef &shot, const TuningFlagNote &n)
{
    // Today's "flag" line, keys and values unchanged (shot_outcomes_design.md §4.4).
    const QJsonObject line{
        { QStringLiteral("kind"),     QStringLiteral("flag") },
        { QStringLiteral("session"),  n.sessionName },
        { QStringLiteral("shotId"),   shot.swingNumber() },
        { QStringLiteral("swingDir"), n.swingDir },
        { QStringLiteral("phrase"),   n.phrase },
        { QStringLiteral("labels"),   n.labels },
        { QStringLiteral("measures"), n.measures },
        { QStringLiteral("atMs"),     double(QDateTime::currentMSecsSinceEpoch()) },
    };
    return appendTuningLine(line, shot);
}

WriteResult Athlete::appendTuningLine(const QJsonObject &line, const ShotRef &shot)
{
    if (!m_repository) {
        ppWarn() << "[Athlete] the tuning log line for" << shot.toString() << "was not appended - no athlete repository";
        return { WriteResult::Status::Failed, QStringLiteral("no athlete repository") };
    }
    const WriteResult w = m_repository->appendTuningLine(*this, QJsonDocument(line).toJson(QJsonDocument::Compact));
    if (!w.ok()) {
        ppWarn() << "[Athlete] could not append to the tuning log:" << w.error;
        emit writeFailed(QStringLiteral("A shot flagged or corrected for the model's tuning"), w.error);   // DEC-126 (3.7-a)
    }
    return w;
}

} // namespace pinpoint
