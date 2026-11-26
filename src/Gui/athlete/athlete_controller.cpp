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

#include "athlete_controller.h"

#include "pp_settings.h"
#include "../../Core/pp_debug.h"
#include "../../Vocabulary/Club/club_vocabulary.h"
#include "../../Vocabulary/Club/club_standards.h"
#include "../../Vocabulary/Common/height_text.h"
#include "../../Diagnostics/content/norm_pack.h" // Cohort, cohortFor(), cohortToMap()
#include "../../DomainModel/session.h"   // the move: the Unassigned session by its folder (step 5.1); the Wrist ★'s edge
#include "../../DomainModel/shot.h"      // the Wrist ★'s edge: a Shot's ref and folder, through its Session (step 5.7b)
#include "../session/session_list_presenter.h"   // the one session list: each athlete's activity (step 5.7b)
#include "../../DomainModel/athlete_diagnostics.h"   // AthleteLevel: Your distances, the athlete level's (3.7-f)
#include "../../Diagnostics/derived/tendencies.h"           // clubDistanceItemToJson
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QVector>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <cmath>

using pinpoint::Athlete;
using pinpoint::AthleteActivity;
using pinpoint::AthleteId;
using pinpoint::AthleteRecord;
using pinpoint::WriteResult;

// SET-02 stays in settings (DEC-05). Every other key is the record's (pinpoint::AthleteRecord).
static constexpr auto kCurrentUuid   = "currentAthleteUuid";

// Defined below clubsFor(): factory per-club defaults + the standard seeded bag.
static QVariantMap defaultClubRecordFor(const QString &clubId, pinpoint::ClubSet set = pinpoint::ClubSet::Men,
                                        double heightM = 0.0);
// The standard set an athlete's clubs default to (club_standards.h): a junior (under 18 today) by
// height, else women's for "female", else men's.
static pinpoint::ClubSet clubSetOf(const QString &sex, const QString &dobIso)
{
    const QDate dob = QDate::fromString(dobIso, Qt::ISODate);
    bool junior = false;
    if (dob.isValid()) {
        const QDate today = QDate::currentDate();
        int age = today.year() - dob.year();
        if (today < dob.addYears(age)) --age;
        junior = age < 18;
    }
    return pinpoint::clubSetFor(sex, junior);
}
static const QStringList &defaultBag();

// Map a stored primaryClub token to the canonical vocabulary (club_vocabulary.h).
// Handles the legacy non-canonical picker values ("Driver", "3-wood", "5-iron",
// "7-iron", "Wedge") so old data self-heals on read; canonical values pass
// through. Empty in / empty out. Unknown tokens are returned uppercased (they
// simply won't match a bag key, so effectivePrimaryClub falls back).
static QString normalizeClubId(const QString &raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty())
        return QString();

    const QStringList &vocab = pinpoint::clubVocabulary();
    if (vocab.contains(t))
        return t;                                  // already canonical

    QString up = t.toUpper();
    up.replace(u'-', u' ');
    up = up.simplified();                          // "3-wood" -> "3 WOOD"
    if (vocab.contains(up))
        return up;

    if (up == QLatin1String("WEDGE"))              // legacy catch-all wedge
        return QStringLiteral("SAND WEDGE");
    return up;                                      // best effort
}


AthleteController::AthleteController(QObject *parent)
    : QObject(parent)
{
}

AthleteController::~AthleteController() = default;

void AthleteController::setCallables(const pinpoint::AthleteCallables &athletes, pinpoint::SessionRepository *sessions)
{
    m_athletesDoor = athletes;
    m_sessions     = sessions;
    refreshActivity();
}

// ── The typed values (step 5.7b) ────────────────────────────────────────────────────────────────────────────────────

ClubRecord ClubRecord::fromMap(const QString &clubId, const QVariantMap &m)
{
    ClubRecord r;
    r.club = clubId;
    const auto has = [&m](const char *k) { return m.contains(QLatin1String(k)); };
    if (has("shaftType"))       r.shaftType       = m.value(QStringLiteral("shaftType")).toString();
    if (has("loftDeg"))         r.loftDeg         = m.value(QStringLiteral("loftDeg")).toDouble();
    if (has("lengthMm"))        r.lengthMm        = m.value(QStringLiteral("lengthMm")).toInt();
    if (has("bandWidthMm"))     r.bandWidthMm     = m.value(QStringLiteral("bandWidthMm")).toInt();
    for (const QVariant &c : m.value(QStringLiteral("bandCentersMm")).toList()) r.bandCentersMm.append(c.toInt());
    if (has("hoselFromButtMm")) r.hoselFromButtMm = m.value(QStringLiteral("hoselFromButtMm")).toInt();
    if (has("headPatch"))       r.headPatch       = m.value(QStringLiteral("headPatch")).toBool();
    if (has("shaftLengthMm"))   r.shaftLengthMm   = m.value(QStringLiteral("shaftLengthMm")).toInt();
    if (has("handsEndMm"))      r.handsEndMm      = m.value(QStringLiteral("handsEndMm")).toInt();
    if (has("tapedOn"))         r.tapedOn         = m.value(QStringLiteral("tapedOn")).toString();
    if (has("notes"))           r.notes           = m.value(QStringLiteral("notes")).toString();
    if (has("lieDeg"))          r.lieDeg          = m.value(QStringLiteral("lieDeg")).toDouble();
    return r;
}

QVariantMap ClubRecord::toMap() const
{
    QVariantList centres;
    for (int c : bandCentersMm) centres.append(c);
    return { { QStringLiteral("shaftType"),       shaftType },
             { QStringLiteral("loftDeg"),         loftDeg },
             { QStringLiteral("lengthMm"),        lengthMm },
             { QStringLiteral("bandWidthMm"),     bandWidthMm },
             { QStringLiteral("bandCentersMm"),   centres },
             { QStringLiteral("hoselFromButtMm"), hoselFromButtMm },
             { QStringLiteral("headPatch"),       headPatch },
             { QStringLiteral("shaftLengthMm"),   shaftLengthMm },
             { QStringLiteral("handsEndMm"),      handsEndMm },
             { QStringLiteral("tapedOn"),         tapedOn },
             { QStringLiteral("notes"),           notes },
             { QStringLiteral("lieDeg"),          lieDeg } };
}

ClubDistanceRow ClubDistanceRow::fromJson(const QJsonObject &o)
{
    const auto num = [&o](const char *k) {
        const QJsonValue v = o.value(QLatin1String(k));
        return v.isDouble() ? v.toDouble() : kNA;   // left out when not measured
    };
    const auto text = [&o](const char *k) { return o.value(QLatin1String(k)).toString(); };
    ClubDistanceRow r;
    r.club = text("club");                   r.label = text("label");
    r.carryMean = num("carryMean");          r.carrySd = num("carrySd");          r.carryText = text("carryText");
    r.totalMean = num("totalMean");          r.totalSd = num("totalSd");          r.offlineSd = num("offlineSd");
    r.speedMean = num("speedMean");          r.speedSd = num("speedSd");
    r.ballSpeedMean = num("ballSpeedMean");  r.ballSpeedSd = num("ballSpeedSd");
    r.smashMean = num("smashMean");          r.smashSd = num("smashSd");
    r.shots = o.value(QLatin1String("shots")).toInt();
    r.sessions = o.value(QLatin1String("sessions")).toInt();
    r.mishitsText = text("mishitsText");
    r.fewShots = o.value(QLatin1String("fewShots")).toBool(true);
    r.fewShotsText = text("fewShotsText");
    r.trend = text("trend");                 r.trendWords = text("trendWords");   r.gapWith = text("gapWith");
    r.gapPrev = num("gapPrev");              r.gapNext = num("gapNext");          r.gapsText = text("gapsText");
    r.shortBelow = num("shortBelow");        r.shortBelowText = text("shortBelowText");
    for (const QJsonValue &v : o.value(QLatin1String("perSession")).toArray()) {
        const QJsonObject p = v.toObject();
        ClubDistancePoint pt;
        pt.date = p.value(QLatin1String("date")).toString();
        if (p.value(QLatin1String("mean")).isDouble()) pt.mean = p.value(QLatin1String("mean")).toDouble();
        if (p.value(QLatin1String("sd")).isDouble())   pt.sd   = p.value(QLatin1String("sd")).toDouble();
        pt.n = p.value(QLatin1String("n")).toInt();
        r.perSession.append(pt);
    }
    return r;
}

QVariantMap ReportDetails::toMap() const
{
    if (isEmpty()) return {};
    return { { QStringLiteral("uuid"),             uuid },
             { QStringLiteral("name"),             name },
             { QStringLiteral("email"),            email },
             { QStringLiteral("greeting"),         greeting },
             { QStringLiteral("dob"),              dob },
             { QStringLiteral("photosApproved"),   photosApproved },
             { QStringLiteral("photosApprovedOn"), photosApprovedOn } };
}

QString AthleteController::computeInitials(const QString &name)
{
    const QStringList words = name.trimmed().split(u' ', Qt::SkipEmptyParts);
    if (words.size() >= 2)
        return QString(words.first()[0]).toUpper() + QString(words.last()[0]).toUpper();
    if (!words.isEmpty() && words.first().length() >= 2)
        return words.first().left(2).toUpper();
    if (!words.isEmpty())
        return words.first().left(1).toUpper();
    return QStringLiteral("?");
}

Athlete *AthleteController::athleteFor(const QString &uuid) const
{
    const AthleteId id = AthleteId::fromString(uuid);
    return (m_athletesDoor.loadAthlete && !id.isNull()) ? m_athletesDoor.loadAthlete(id) : nullptr;
}

QString AthleteController::folderOf(const QString &uuid) const
{
    const Athlete *a = athleteFor(uuid);
    return a ? a->folder() : QString();
}

ReportDetails AthleteController::reportDetailsForSession(const QString &sessionDir) const
{
    // An athlete folder as a key, however each side spelled it: absolute and cleaned.
    const auto key = [](const QString &folder) {
        return folder.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    };
    if (sessionDir.trimmed().isEmpty())
        return {};
    const QString folder = key(QFileInfo(QDir::cleanPath(sessionDir)).absolutePath());
    for (const AthleteRow &row : m_athletes)
        if (!row.uuid.isEmpty() && key(folderOf(row.uuid)) == folder)
            return reportDetails(row.uuid);
    return {};
}

// The standard set a bag that was never stored shows (driver, 5-9 iron, GW, SW, putter, with factory
// defaults for the athlete's set). Shown, NOT written: a read never writes (until the first club edit
// stores the bag). An intentionally emptied bag is stored, and stays empty.
static QVariantMap bagOf(const AthleteRecord &r)
{
    if (r.hasBag)
        return r.bag;
    QVariantMap bag;
    const pinpoint::ClubSet set = clubSetOf(r.sex, r.dob);
    for (const QString &id : defaultBag())
        bag.insert(id, defaultClubRecordFor(id, set, r.heightM()));
    return bag;
}

// One row of `athletes`, the keys QML has always read (the bag is clubRecords' since step 5.7b).
AthleteRow AthleteController::rowOf(const Athlete &a) const
{
    const AthleteRecord &r = a.record();
    AthleteRow m;
    m.uuid                   = a.id().toString();
    m.name                   = r.name;
    m.handedness             = r.handedness;
    m.heightValue            = r.heightValue;
    m.heightUnit             = r.heightUnit;
    m.weightValue            = r.weightValue;
    m.weightUnit             = r.weightUnit;
    m.handicap               = r.handicap;
    m.primaryClub            = normalizeClubId(r.primaryClub);
    m.speedTarget            = r.speedTarget;
    m.notes                  = r.notes;
    m.dob                    = r.dob;
    m.sex                    = r.sex;
    m.reportEmail            = r.reportEmail;
    m.reportGreeting         = r.reportGreeting;
    m.reportPhotosApproved   = r.reportPhotosApproved;
    m.reportPhotosApprovedOn = r.reportPhotosApprovedOn;
    m.createdAt              = r.createdAt;
    // Derived from the index (DEC-89), never stored: 0 until the athlete's group of the one session list is read.
    const AthleteActivity act = m_activity.value(m.uuid);
    m.lastSessionAt          = act.lastAtSec;
    m.sessionCount           = act.sessions;
    m.initials               = computeInitials(r.name);
    return m;
}

void AthleteController::reload()
{
    m_ids = m_athletesDoor.listAthletes ? m_athletesDoor.listAthletes() : QList<AthleteId>{};
    followAll();
    rebuild();
}

void AthleteController::rebuild()
{
    QList<AthleteRow> list;
    list.reserve(m_ids.size());
    const AthleteId wantedNow = AthleteId::fromString(ppSettings().value(kCurrentUuid, QString()).toString());
    for (const AthleteId &id : std::as_const(m_ids)) {
        Athlete *a = m_athletesDoor.loadAthlete ? m_athletesDoor.loadAthlete(id) : nullptr;
        if (!a) continue;
        // R-UNASSIGNED-3 (DEC-198, step 5's question 2 (a)): the Unassigned owner is listed only while its SessionIndex
        // holds a session — read with the activity (off the GUI thread: the index is a folder walk), so it shows once
        // that read lands — and while it is the athlete on screen (a last session trashed under it), so the picker still
        // finds the current athlete; a last MOVE switches to the golfer instead (J-68: moveSessionToAthlete).
        if (a->isUnassigned() && m_activity.value(id.toString()).indexed == 0 && id != wantedNow)
            continue;
        // An edit is announced by the Athlete itself (the interim for EVT-14).
        connect(a, &Athlete::recordChanged, this, &AthleteController::rebuild, Qt::UniqueConnection);
        list.append(rowOf(*a));
    }

    // The current athlete: SET-02's, if it is listed. While a session runs the current athlete is kept
    // even when the library no longer lists them (a library re-pointed mid-session, D7): the session's
    // athlete must not change until it ends.
    const AthleteId wanted = AthleteId::fromString(ppSettings().value(kCurrentUuid, QString()).toString());
    Athlete *cur = m_ids.contains(wanted) ? m_athletesDoor.loadAthlete(wanted) : nullptr;
    if (!cur && m_current && sessionRunning() && m_current->id() == wanted) {
        cur = m_current;
        list.append(rowOf(*cur));
    }
    sortAthletes(list);

    const QString before = m_currentUuid;
    m_current           = cur;
    m_currentUuid       = cur ? cur->id().toString() : QString();
    m_currentName       = cur ? cur->record().name : QString();
    m_currentInitials   = cur ? computeInitials(cur->record().name) : QString();
    m_currentHandedness = cur ? cur->record().handedness : QString();
    m_athletes          = list;

    emit athletesChanged();
    if (m_currentUuid != before)
        emit currentAthleteChanged();   // a SWITCH only (E-03)
}

pinpoint::SessionOwner AthleteController::currentSessionOwner() const
{
    return m_current ? m_current->sessionOwner() : pinpoint::SessionOwner{};
}

// lastSessionAt desc, createdAt desc as tie-breaker.
void AthleteController::sortAthletes(QList<AthleteRow> &list)
{
    std::stable_sort(list.begin(), list.end(), [](const AthleteRow &a, const AthleteRow &b) {
        if (a.lastSessionAt != b.lastSessionAt) return a.lastSessionAt > b.lastSessionAt;
        return a.createdAt > b.createdAt;
    });
}

void AthleteController::refreshActivity()
{
    reload();   // re-listed: a new athlete is followed and read (followAll)
    // The picker shown, or the library moved: every group already read is read again, as 5.7a's screens ask when shown — a
    // session nobody holds, or one another machine wrote into the library, has no event. Coalesced in the list while a
    // read runs; one in flight is left to land.
    if (m_list)
        for (const AthleteId &id : std::as_const(m_ids))
            if (m_list->loaded(id) && !m_list->loading(id))
                m_list->reload(id);
    applyActivity();
}

// ── The activity, from the one session list (step 5.7b) ───────────────────────────────────────────────────────────────

void AthleteController::setSessionList(pinpoint::SessionListPresenter *list)
{
    if (m_list == list) return;
    if (m_list) disconnect(m_list, nullptr, this, nullptr);
    m_list = list;
    if (m_list) {
        // A group's rows added, removed or read again (EVT-17/18/19/20/23 and the move, through the list's follow), or one
        // row's summary read again (EVT-01/03: a first swing in a session). The list follows the owners; this follows it.
        connect(m_list, &pinpoint::SessionListPresenter::rowsChanged, this, &AthleteController::activityMoved);
        connect(m_list, &pinpoint::SessionListPresenter::rowChanged,  this, &AthleteController::activityMoved);
    }
    followAll();
    applyActivity();
}

// Every listed athlete is followed in the one session list (its events, through its Athlete and the Sessions it holds),
// and read once when nobody has read it yet. Again for one already followed: nothing.
void AthleteController::followAll()
{
    if (!m_list || !m_athletesDoor.loadAthlete) return;
    for (const AthleteId &id : std::as_const(m_ids)) {
        Athlete *a = m_athletesDoor.loadAthlete(id);
        if (!a) continue;
        m_list->follow(a);
        if (!m_list->loaded(id) && !m_list->loading(id))
            m_list->reload(id);
    }
}

// A burst of row changes (a reload's landing, a re-analysis's rows) is recomputed once, on the event loop.
void AthleteController::activityMoved()
{
    if (m_syncActivity) { applyActivity(); return; }
    if (m_activityArmed) return;
    m_activityArmed = true;
    QTimer::singleShot(0, this, [this] { m_activityArmed = false; applyActivity(); });
}

// The activity of each listed athlete, from its group's rows: the sessions with a swing (the Sessions list's rule: a folder
// nothing was hit in is not a session) and the latest one's start — its local day's midnight for a session read from its
// name (DEC-150) — as Athlete::activityOf counts them from the index; and every session the index holds (R-UNASSIGNED-3).
// A row whose summary is not read yet holds no swing it can count. The list is rebuilt only when an activity moved, so a
// shot captured in a session that already has one announces nothing.
void AthleteController::applyActivity()
{
    QHash<QString, AthleteActivity> next;
    if (m_list)
        for (const AthleteId &id : std::as_const(m_ids)) {
            if (!m_list->loaded(id)) continue;   // 0 until its first read lands
            AthleteActivity act;
            const QVector<pinpoint::SessionListRow> rows = m_list->rowsOf(id);
            act.indexed = int(rows.size());
            for (const pinpoint::SessionListRow &r : rows) {
                if (!r.entry.summary.read || r.entry.summary.swings.swings == 0) continue;
                ++act.sessions;
                qint64 at = 0;
                if (r.entry.startedAtUtc.isValid())
                    at = r.entry.startedAtUtc.toSecsSinceEpoch();
                else if (r.entry.identity.localDay().isValid())
                    at = QDateTime(r.entry.identity.localDay(), QTime(0, 0)).toSecsSinceEpoch();
                act.lastAtSec = std::max(act.lastAtSec, at);
            }
            next.insert(id.toString(), act);
        }
    const auto same = [](const QHash<QString, AthleteActivity> &a, const QHash<QString, AthleteActivity> &b) {
        if (a.size() != b.size()) return false;
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
            const auto o = b.constFind(it.key());
            if (o == b.cend() || o->sessions != it->sessions || o->lastAtSec != it->lastAtSec || o->indexed != it->indexed)
                return false;
        }
        return true;
    };
    if (same(next, m_activity)) return;
    m_activity = next;
    rebuild();   // patches the list from the athletes held — never a switch, so nothing that follows the current athlete re-points
}

QString AthleteController::saveAthlete(
    const QString &uuid,
    const QString &name,
    const QString &handedness,
    double         heightValue,
    const QString &heightUnit,
    double         weightValue,
    const QString &weightUnit,
    double         handicap,
    const QString &primaryClub,
    double         speedTarget,
    const QString &notes)
{
    if (name.trimmed().isEmpty() || !m_athletesDoor.createAthlete)
        return {};

    const bool creating = uuid.isEmpty();
    Athlete   *existing = creating ? nullptr : athleteFor(uuid);
    if (!creating && !existing)
        return {};                           // unknown uuid -> no-op

    // Convert to base unit before storing (always ft / lb).
    double storedHeight = heightValue;
    if (heightUnit == QLatin1String("cm") && heightValue > 0.0)
        storedHeight = heightValue / 30.48;

    double storedWeight = weightValue;
    if (weightUnit == QLatin1String("kg") && weightValue > 0.0)
        storedWeight = weightValue * 2.20462;

    AthleteRecord r = existing ? existing->record() : AthleteRecord{};
    r.name        = name.trimmed();
    r.handedness  = handedness;
    r.heightValue = storedHeight;
    r.heightUnit  = heightUnit;
    r.weightValue = storedWeight;
    r.weightUnit  = weightUnit;
    r.handicap    = handicap;
    r.primaryClub = normalizeClubId(primaryClub);
    r.speedTarget = speedTarget;
    r.notes       = notes;

    if (!creating) {
        // An EDIT: the record is written through the repository; the Athlete announces it
        // (recordChanged), and the current athlete does not change.
        const WriteResult w = existing->editRecord(r);
        if (!w.ok()) {
            ppWarn() << "[Athlete] could not save" << r.name << "-" << w.error;
            return {};
        }
        return uuid;
    }

    // A NEW athlete: its athlete.json is written now, in a folder chosen once (DEC-04, D4), with the
    // standard bag (today seeded on first sight; stored at once here).
    r.createdAt = QDateTime::currentSecsSinceEpoch();
    r.bag       = bagOf(r);
    r.hasBag    = true;
    WriteResult made;
    Athlete    *created = m_athletesDoor.createAthlete(r, &made);
    if (!created || !made.ok()) {
        ppWarn() << "[Athlete] could not create" << r.name << "-" << made.error;
        return {};
    }
    const QString newUuid = created->id().toString();

    // A new athlete becomes current — except while a session runs, when the session's athlete
    // must stay current (its swings are being saved under them; see setAppState).
    if (sessionRunning()) {
        ppInfo() << "[Athlete] created" << r.name << newUuid
                 << "but kept" << m_currentName << "current: a session is running for them";
    } else {
        QSettings s = ppSettings();
        s.setValue(kCurrentUuid, newUuid);   // auto-select new athlete
        s.sync();
    }
    reload();
    return newUuid;
}

QString AthleteController::createAthlete(
    const QString &name,
    const QString &handedness,
    double         heightValue,
    const QString &heightUnit,
    double         weightValue,
    const QString &weightUnit,
    double         handicap,
    const QString &primaryClub,
    double         speedTarget,
    const QString &notes)
{
    return saveAthlete(QString(), name, handedness,
                       heightValue, heightUnit,
                       weightValue, weightUnit,
                       handicap, primaryClub, speedTarget, notes);
}

// ── Demographics, for norm cohorts ──────────────────────────────────────────

QString AthleteController::cohortLabelFor(const QString &dobIsoDate, const QString &sex,
                                          const QString &onIsoDate) const
{
    using namespace pinpoint::analysis;
    // Qualified, as a member of the same name once was: unqualified lookup would find it by
    // ordinary lookup in class scope, which stops the search and suppresses ADL.
    return cohortLabel(pinpoint::analysis::cohortFor(
        QDate::fromString(dobIsoDate, Qt::ISODate), sex,
        QDate::fromString(onIsoDate, Qt::ISODate)));
}

QList<SexOption> AthleteController::sexOptions() const
{
    // "Prefer not to say" is stored as its own token rather than as an empty string. The two mean
    // the same thing to a norm — only rows unqualified on that axis can match — but they do not
    // mean the same thing to the person: an answer they gave has to survive re-opening the form,
    // and a blank field would read as never having been asked.
    return { { QStringLiteral("female"),   QObject::tr("Female") },
             { QStringLiteral("male"),     QObject::tr("Male") },
             { QStringLiteral("declined"), QObject::tr("Prefer not to say") } };
}

bool AthleteController::updateAthlete(const QString &uuid, const QString &fieldName, const QString &value)
{
    Athlete *a = athleteFor(uuid);
    if (!a)
        return false;
    AthleteRecord r = a->record();
    r.setValue(fieldName, value);
    const WriteResult w = a->editRecord(r);
    if (!w.ok())
        ppWarn() << "[Athlete] could not save" << fieldName << "of" << a->record().name << "-" << w.error;
    return w.ok();
}

// ── The session report's details ────────────────────────────────────────────

// The first word of the name, as the greeting falls back to it: "Nick" for "Nick Henn".
static QString firstWordOf(const QString &name)
{
    const QStringList words = name.trimmed().split(u' ', Qt::SkipEmptyParts);
    return words.isEmpty() ? QString() : words.first();
}

static bool juniorOn(const QDate &dob, const QDate &on)
{
    if (!dob.isValid() || !on.isValid()) return false;
    int age = on.year() - dob.year();
    if (on < dob.addYears(age)) --age;
    return age < 18;
}

QString AthleteController::currentReportGreeting() const
{
    const QString g = currentField(QStringLiteral("reportGreeting")).trimmed();
    return g.isEmpty() ? firstWordOf(m_currentName) : g;
}

bool AthleteController::currentIsJunior() const
{
    return juniorOn(QDate::fromString(currentDob(), Qt::ISODate), QDate::currentDate());
}

ReportDetails AthleteController::reportDetails(const QString &uuidOrName) const
{
    const QString key = uuidOrName.trimmed();
    if (key.isEmpty()) return {};
    const AthleteRow *found = nullptr;
    for (const AthleteRow &a : m_athletes)                 // uuid first: a name can repeat
        if (a.uuid == key) { found = &a; break; }
    if (!found)
        for (const AthleteRow &a : m_athletes)
            if (a.name.trimmed().compare(key, Qt::CaseInsensitive) == 0) { found = &a; break; }
    if (!found || found->uuid.isEmpty()) return {};

    const QString greeting = found->reportGreeting.trimmed();
    ReportDetails out;
    out.uuid             = found->uuid;
    out.name             = found->name;
    out.email            = found->reportEmail.trimmed();
    out.greeting         = greeting.isEmpty() ? firstWordOf(found->name) : greeting;
    out.dob              = found->dob;
    out.photosApproved   = found->reportPhotosApproved;
    out.photosApprovedOn = found->reportPhotosApprovedOn;
    return out;
}

bool AthleteController::isJuniorOn(const QString &dobIsoDate, qint64 sessionMs) const
{
    const QDate on = sessionMs > 0 ? QDateTime::fromMSecsSinceEpoch(sessionMs).date() : QDate::currentDate();
    return juniorOn(QDate::fromString(dobIsoDate.trimmed(), Qt::ISODate), on);
}

bool AthleteController::validReportEmail(const QString &text) const
{
    // something@something.tld: no spaces, one @, a dot in the domain with something either side, and
    // a top-level part of two letters or more. Deliberately loose — a shape check catches the typo
    // ("nick@gmail", "nick gmail.com"), and the mail program is the real judge.
    static const QRegularExpression shape(
        QStringLiteral("^[^\\s@,;]+@[^\\s@,;.]+(\\.[^\\s@,;.]+)*\\.[A-Za-z]{2,}$"));
    const QStringList parts = text.split(u',');
    int addresses = 0;
    for (const QString &p : parts) {
        const QString a = p.trimmed();
        if (a.isEmpty()) {
            // "a@b.com, " while typing the next one is not an error yet; ", ," or a leading comma is.
            if (&p == &parts.last() && addresses > 0) continue;
            return false;
        }
        if (!shape.match(a).hasMatch()) return false;
        ++addresses;
    }
    return addresses > 0;
}

QString AthleteController::normaliseReportEmails(const QString &text) const
{
    QStringList out;
    for (const QString &p : text.split(u',')) {
        const QString a = p.trimmed();
        if (!a.isEmpty()) out << a;
    }
    return out.join(QStringLiteral(", "));
}

QString AthleteController::greetingDefault(const QString &name) const
{
    return firstWordOf(name);
}

bool AthleteController::validDob(const QString &text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return true;
    // ASCII digits only, the exact shape — Qt::ISODate alone would also take "2026/10/31" and a
    // trailing time. QDate::isValid then refuses a day the month does not have.
    static const QRegularExpression shape(QStringLiteral("^([0-9]{4})-([0-9]{2})-([0-9]{2})$"));
    const QRegularExpressionMatch m = shape.match(t);
    if (!m.hasMatch()) return false;
    return QDate::isValid(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
}

double AthleteController::parseHeight(const QString &text, const QString &unit) const
{
    return pinpoint::parseHeightText(text, unit);
}

void AthleteController::setPhotosApproved(const QString &uuid, bool approved)
{
    Athlete *a = athleteFor(uuid);
    if (!a) return;
    AthleteRecord r = a->record();
    const bool was = r.reportPhotosApproved;
    if (approved) {
        r.reportPhotosApproved = true;
        if (!was || r.reportPhotosApprovedOn.isEmpty())
            r.reportPhotosApprovedOn = QDate::currentDate().toString(Qt::ISODate);
    } else {
        r.reportPhotosApproved = false;
        r.reportPhotosApprovedOn.clear();
    }
    if (const WriteResult w = a->editRecord(r); !w.ok())
        ppWarn() << "[Athlete] could not save the photo consent of" << r.name << "-" << w.error;
}

// Factory defaults per vocabulary club — lofts/lengths guided by Titleist
// T100 irons, Vokey wedges (52/56/60 stock) and stock Titleist metals; club
// length converted to mm (1" = 25.4 mm). Bands default to untaped.
static QVariantMap defaultClubRecordFor(const QString &clubId, pinpoint::ClubSet set, double heightM)
{
    struct Spec { const char *id; double loft; int lengthMm; const char *shaft; };
    static const Spec kSpecs[] = {
        { "DRIVER",          10.0, 1156, "graphite" },   // 45.5"
        { "3 WOOD",          15.0, 1092, "graphite" },   // 43.0"
        { "5 WOOD",          18.0, 1080, "graphite" },   // 42.5"
        { "3 HYBRID",        21.0, 1035, "graphite" },   // 40.75"
        { "4 HYBRID",        24.0, 1016, "graphite" },   // 40.0"
        { "3 IRON",          21.0,  991, "steel"    },   // 39.0"
        { "4 IRON",          24.0,  978, "steel"    },   // 38.5"
        { "5 IRON",          27.0,  965, "steel"    },   // 38.0"
        { "6 IRON",          30.0,  953, "steel"    },   // 37.5"
        { "7 IRON",          34.0,  940, "steel"    },   // 37.0"
        { "8 IRON",          38.0,  927, "steel"    },   // 36.5"
        { "9 IRON",          42.0,  914, "steel"    },   // 36.0"
        { "PITCHING WEDGE",  46.0,  908, "steel"    },   // 35.75"
        { "GAP WEDGE",       52.0,  902, "steel"    },   // Vokey 52, 35.5"
        { "SAND WEDGE",      56.0,  895, "steel"    },   // Vokey 56, 35.25"
        { "LOB WEDGE",       60.0,  889, "steel"    },   // Vokey 60, 35.0"
        { "PUTTER",           3.0,  864, "steel"    },   // 34.0"
    };
    QVariantMap rec;
    rec[QStringLiteral("shaftType")]       = QStringLiteral("steel");
    rec[QStringLiteral("loftDeg")]         = 0.0;
    rec[QStringLiteral("lengthMm")]        = 0;
    rec[QStringLiteral("bandWidthMm")]     = 25;
    rec[QStringLiteral("bandCentersMm")]   = QVariantList{};
    rec[QStringLiteral("hoselFromButtMm")] = 0;
    // Markerless tracking (docs/design/markerless_club_tracker_design.md §4.1):
    // exposed shaft, bottom of grip → top of hosel (mm), and where the golfer's
    // hands end on the grip (mm from the butt). 0 = unknown ⇒ the tracker assumes
    // a 265 mm grip and a 180 mm hands' end.
    rec[QStringLiteral("shaftLengthMm")]   = 0;
    rec[QStringLiteral("handsEndMm")]      = 0;
    rec[QStringLiteral("headPatch")]       = false;
    rec[QStringLiteral("tapedOn")]         = QString();
    rec[QStringLiteral("notes")]           = QString();
    rec[QStringLiteral("lieDeg")]          = 0.0;
    for (const Spec &sp : kSpecs) {
        if (clubId == QLatin1String(sp.id)) {
            rec[QStringLiteral("loftDeg")]   = sp.loft;
            rec[QStringLiteral("lengthMm")]  = sp.lengthMm;
            rec[QStringLiteral("shaftType")] = QLatin1String(sp.shaft);
            break;
        }
    }
    // Length and lie: the standard for this athlete's set (club_standards.h) — the men's lengths
    // are the ones above; a women's or a junior's differ.
    const pinpoint::ClubStandard st = pinpoint::standardClub(clubId, set, heightM);
    if (st.valid) {
        rec[QStringLiteral("lengthMm")] = st.lengthMm;
        rec[QStringLiteral("lieDeg")]   = st.lieDeg;
    }
    return rec;
}

// The bag every golfer starts with; fairway woods / hybrids / specialty
// wedges are added by the user (with defaults from defaultClubRecordFor).
static const QStringList &defaultBag()
{
    static const QStringList kBag = {
        QStringLiteral("DRIVER"),
        QStringLiteral("5 IRON"), QStringLiteral("6 IRON"),
        QStringLiteral("7 IRON"), QStringLiteral("8 IRON"),
        QStringLiteral("9 IRON"),
        QStringLiteral("GAP WEDGE"), QStringLiteral("SAND WEDGE"),
        QStringLiteral("PUTTER"),
    };
    return kBag;
}

ClubRecord AthleteController::defaultClubRecord(const QString &clubId) const
{
    return ClubRecord::fromMap(clubId, defaultClubRecordFor(clubId, clubSetOf(currentSex(), currentDob()),
                                                            currentHeightM()));
}

QVariantMap AthleteController::clubsFor(const QString &uuid) const
{
    const Athlete *a = athleteFor(uuid);
    return a ? bagOf(a->record()) : QVariantMap{};
}

QList<ClubRecord> AthleteController::clubRecords(const QString &uuid) const
{
    const QVariantMap bag = clubsFor(uuid);
    QList<ClubRecord> out;
    for (const QString &id : bagClubIds(uuid))
        out.append(ClubRecord::fromMap(id, bag.value(id).toMap()));
    return out;
}

ClubRecord AthleteController::clubRecord(const QString &uuid, const QString &clubId) const
{
    const QVariantMap bag = clubsFor(uuid);
    if (clubId.isEmpty() || !bag.contains(clubId)) return {};
    return ClubRecord::fromMap(clubId, bag.value(clubId).toMap());
}

ClubDistances AthleteController::clubDistances(const QString &uuid) const
{
    const QString kReading = QStringLiteral("Distances are worked out when this golfer's sessions are read");
    ClubDistances out;
    out.stale = true;
    out.note  = kReading;

    // THE ATHLETE LEVEL'S OWN DISTANCES (refactor 3.7-f; J-51): AthleteDiagnostics pools them from the session summaries'
    // club windows (D7: DEC-172), the rows tendencies.json carries, keys as clubDistanceItemToJson writes them — no longer
    // that file, read and judged stale against the newest ledger (its writer rewrote it on every ledger write; the owner
    // writes it only when its inputs move). Stale while a session with shots is not summed up yet (notSummedUp): those
    // numbers leave it out, and the note says so.
    Athlete *a = athleteFor(uuid);
    pinpoint::AthleteLevel *ad = (a && m_athleteLevelDoor) ? m_athleteLevelDoor(a) : nullptr;
    if (!ad) return out;
    QList<ClubDistanceRow> items;
    for (const pinpoint::analysis::ClubDistanceItem &it : ad->distances())
        items.append(ClubDistanceRow::fromJson(pinpoint::analysis::clubDistanceItemToJson(it)));   // its one rule for the words
    if (ad->summaries().empty()) return out;
    out.items = items;
    const bool stale = ad->notSummedUp().count > 0;
    out.stale = stale;
    out.note  = stale ? kReading : QString();
    return out;
}

QStringList AthleteController::clubOptions() const
{
    return pinpoint::clubVocabulary();
}

QStringList AthleteController::bagClubIds(const QString &uuid) const
{
    if (uuid.isEmpty()) return {};
    // The record keys (QVariantMap: alphabetical), stably sorted by vocabulary position — the order
    // every club picker shows. An id the vocabulary does not know has position -1 and goes first.
    QStringList ids = clubsFor(uuid).keys();
    const QStringList vocab = pinpoint::clubVocabulary();
    std::stable_sort(ids.begin(), ids.end(), [&vocab](const QString &a, const QString &b) {
        return vocab.indexOf(a) < vocab.indexOf(b);
    });
    return ids;
}

QString AthleteController::effectivePrimaryClub(const QString &uuid) const
{
    const Athlete    *a      = athleteFor(uuid);
    const QVariantMap bag    = a ? bagOf(a->record()) : QVariantMap{};
    const QString     stored = normalizeClubId(a ? a->record().primaryClub : QStringLiteral("DRIVER"));

    if (!stored.isEmpty() && bag.contains(stored))
        return stored;
    if (bag.contains(QStringLiteral("DRIVER")))
        return QStringLiteral("DRIVER");
    // First configured club in canonical (bag) order.
    for (const QString &c : pinpoint::clubVocabulary())
        if (bag.contains(c))
            return c;
    return QString();
}

bool AthleteController::setClubRecord(const QString &uuid, const QString &clubId,
                                      const ClubRecord &record)
{
    if (uuid.isEmpty() || clubId.trimmed().isEmpty())
        return false;
    Athlete *a = athleteFor(uuid);
    if (!a)
        return false;
    QVariantMap clubs = bagOf(a->record());
    // Laid over the stored record, so a key this build does not know is kept (as the form's copy of every key kept it).
    QVariantMap stored = clubs.value(clubId).toMap();
    const QVariantMap typed = record.toMap();
    for (auto it = typed.cbegin(); it != typed.cend(); ++it)
        stored.insert(it.key(), it.value());
    clubs.insert(clubId, stored);
    const WriteResult w = a->editBag(clubs);
    if (!w.ok())
        ppWarn() << "[Athlete] could not save the" << clubId << "of" << a->record().name << "-" << w.error;
    return w.ok();
}

bool AthleteController::removeClubRecord(const QString &uuid, const QString &clubId)
{
    Athlete *a = athleteFor(uuid);
    if (!a)
        return false;
    QVariantMap clubs = bagOf(a->record());
    if (clubs.remove(clubId) == 0)
        return false;
    const WriteResult w = a->editBag(clubs);
    return w.ok();
}

bool AthleteController::isUnassignedSession(const QString &sessionDir) const
{
    if (sessionDir.trimmed().isEmpty() || !m_athletesDoor.loadAthlete)
        return false;
    Athlete *u = m_athletesDoor.loadAthlete(pinpoint::unassignedAthleteId());
    return u && u->sessionAt(QDir::cleanPath(sessionDir)) != nullptr;
}

QString AthleteController::moveSessionToAthlete(const QString &sessionDir, const QString &toUuid)
{
    Athlete *to = athleteFor(toUuid);
    if (!to || to->isUnassigned())
        return tr("Choose a golfer to move the session to.");
    // The record is written at the first move if it is not there yet (J-67: held in memory until a write).
    pinpoint::WriteResult made;
    Athlete *from = m_athletesDoor.unassigned ? m_athletesDoor.unassigned(/*create=*/true, &made) : nullptr;
    if (!from)
        return made.error.isEmpty() ? tr("The session could not be moved.") : made.error;
    pinpoint::Session *session = from->sessionAt(QDir::cleanPath(sessionDir));
    if (!session)
        return tr("Only a session with no golfer can be moved to a golfer.");
    QString moved;
    const pinpoint::WriteResult r = from->moveSessionTo(session->id(), *to, &moved);
    if (!r.ok()) {
        ppWarn() << "[Athlete] could not move" << sessionDir << "to" << to->record().name << "-" << r.error;
        return r.error.isEmpty() ? tr("The session could not be moved.") : r.error;
    }
    ppInfo() << "[Athlete] moved" << sessionDir << "to" << to->record().name << "at" << moved;
    // J-68 (step 5.1 review B3): the last session moved out of Unassigned while it is the athlete on screen — the app goes
    // to the golfer it went to (where the golfer's attention goes next), so Unassigned, empty, leaves the picker
    // (R-UNASSIGNED-3). A switch refused (a session running) keeps it until the next switch.
    if (m_current == from && from->index().isEmpty() && !selectAthlete(toUuid))
        ppInfo() << "[Athlete] Unassigned is empty but stays on screen: the switch to" << to->record().name << "was refused";
    // Both golfers' counts follow the move's events through the one session list (step 5.7b); re-listed here, so Unassigned
    // leaves the picker once it holds none (R-UNASSIGNED-3).
    reload();
    emit sessionMoved(QDir::cleanPath(sessionDir), moved);
    return {};
}

bool AthleteController::deleteAthlete(const QString &uuid)
{
    if (sessionRunning() && !uuid.isEmpty() && uuid == m_currentUuid) {
        ppInfo() << "[Athlete] refused to delete" << m_currentName
                 << "- a session is running for them; end it first";
        return false;
    }
    Athlete *a = athleteFor(uuid);
    if (!a)
        return false;
    // The whole folder to the recoverable trash (DEC-79); refused while one of its sessions is
    // InProgress (DEC-99) — Athlete::remove's rule.
    const QString name = a->record().name;
    const pinpoint::AthleteRemoval removed = a->remove();
    if (!removed.ok()) {
        ppWarn() << "[Athlete] could not delete" << name << "-" << removed.result.error;
        if (!removed.note.isEmpty())
            emit deleteNotice(tr("%1 was not deleted").arg(name), removed.note);
        return false;
    }
    // The folder stayed (shared with another athlete, J-30): the prompt said it would move, so say so.
    if (!removed.folderMoved && !removed.note.isEmpty())
        emit deleteNotice(tr("%1 was deleted; the folder was kept").arg(name), removed.note);

    QSettings s = ppSettings();
    if (s.value(kCurrentUuid).toString() == uuid)
        s.remove(kCurrentUuid);
    s.sync();
    reload();
    return true;
}

bool AthleteController::selectAthlete(const QString &uuid)
{
    if (sessionRunning() && uuid != m_currentUuid) {
        ppInfo() << "[Athlete] refused to switch from" << m_currentName << "to" << uuid
                 << "- a session is running; end it first";
        return false;
    }
    QSettings s = ppSettings();
    s.setValue(kCurrentUuid, uuid);
    s.sync();
    reload();
    return true;
}

void AthleteController::clearCurrentAthlete()
{
    if (sessionRunning() && !m_currentUuid.isEmpty()) {
        ppInfo() << "[Athlete] refused to clear the current athlete" << m_currentName
                 << "- a session is running; end it first";
        return;
    }
    QSettings s = ppSettings();
    s.remove(kCurrentUuid);
    s.sync();
    reload();
}

// ── The Wrist reference ★ (D5: DEC-152) ─────────────────────────────────────────────────────────

// Through the owners (step 5.7b; P11): the record's ShotRef → its Session (held by the Athlete) → its Shot → the Shot's
// folder; and back, the folder → the Athlete's Session that holds it (sessionAt, the edge: DEC-90) → its Shot's ref. A swing
// gone from its session (no Shot listed) reads as none, as a folder gone did.
QString AthleteController::wristReferenceSwingDir() const
{
    if (!m_current) return {};
    const pinpoint::ShotRef ref = m_current->record().wristReference;
    if (ref.isNull()) return {};
    pinpoint::Session *session = m_current->session(ref.session());
    pinpoint::Shot    *shot    = session ? session->shot(ref) : nullptr;
    return shot ? shot->folder() : QString();
}

bool AthleteController::setWristReferenceSwingDir(const QString &swingDir)
{
    if (!m_current) return false;
    pinpoint::ShotRef ref;
    if (!swingDir.isEmpty()) {
        // The path the views carry, translated ONCE, here (DEC-90), by the Session of the current athlete's that holds it.
        const QString sessionDir = QDir::cleanPath(QFileInfo(QDir::cleanPath(swingDir)).absolutePath());
        if (QDir::cleanPath(QFileInfo(sessionDir).absolutePath()) != QDir::cleanPath(QFileInfo(m_current->folder()).absoluteFilePath())) {
            ppInfo() << "[Athlete] the Wrist reference" << swingDir << "is not a swing of" << m_currentName
                     << "- not recorded on their record";
            return false;
        }
        pinpoint::Session *session = m_current->sessionAt(sessionDir);
        pinpoint::Shot    *shot    = session ? session->shotAtFolder(swingDir) : nullptr;
        ref = shot ? shot->ref() : pinpoint::ShotRef();
        if (ref.isNull()) return false;
    }
    AthleteRecord r = m_current->record();
    if (r.wristReference == ref) return true;
    r.wristReference = ref;
    const WriteResult w = m_current->editRecord(r);
    if (!w.ok())
        ppWarn() << "[Athlete] could not record the Wrist reference of" << m_currentName << "-" << w.error;
    return w.ok();
}
