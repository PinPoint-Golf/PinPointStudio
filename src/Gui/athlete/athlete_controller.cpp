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
#include "club_vocabulary.h"
#include "club_standards.h"
#include "height_text.h"
#include "../../Diagnostics/norms/norm_pack.h" // Cohort, cohortFor(), cohortToMap()
#include "../../DomainModel/athlete_repository.h"
#include "../../DomainModel/session_repository.h"
#include "../../Export/swing_paths.h"           // a swing folder's number, at the Wrist ★'s edge
#include "../../Export/athlete_files.h"         // tendencies.json's name (step 2.5)
#include "../../Export/session_files.h"         // diagnostics.json's name (step 2.5)
#include "../../Coaching/tendencies.h"           // kTendencySchemaVersion: the schema check
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
#include <algorithm>

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

AthleteController::~AthleteController()
{
    m_pool.waitForDone();
}

void AthleteController::setRepositories(pinpoint::AthleteRepository *athletes, pinpoint::SessionRepository *sessions)
{
    m_repo     = athletes;
    m_sessions = sessions;
    refreshActivity();
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
    return (m_repo && !id.isNull()) ? m_repo->load(id) : nullptr;
}

QString AthleteController::folderOf(const QString &uuid) const
{
    const Athlete *a = athleteFor(uuid);
    return a ? a->folder() : QString();
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

// One row of `athletes`, the keys QML has always read.
QVariantMap AthleteController::mapOf(const Athlete &a) const
{
    const AthleteRecord &r = a.record();
    QVariantMap m;
    m[QStringLiteral("uuid")]                   = a.id().toString();
    m[QStringLiteral("name")]                   = r.name;
    m[QStringLiteral("handedness")]             = r.handedness;
    m[QStringLiteral("heightValue")]            = r.heightValue;
    m[QStringLiteral("heightUnit")]             = r.heightUnit;
    m[QStringLiteral("weightValue")]            = r.weightValue;
    m[QStringLiteral("weightUnit")]             = r.weightUnit;
    m[QStringLiteral("handicap")]               = r.handicap;
    m[QStringLiteral("primaryClub")]            = normalizeClubId(r.primaryClub);
    m[QStringLiteral("speedTarget")]            = r.speedTarget;
    m[QStringLiteral("notes")]                  = r.notes;
    m[QStringLiteral("dob")]                    = r.dob;
    m[QStringLiteral("sex")]                    = r.sex;
    m[QStringLiteral("reportEmail")]            = r.reportEmail;
    m[QStringLiteral("reportGreeting")]         = r.reportGreeting;
    m[QStringLiteral("reportPhotosApproved")]   = r.reportPhotosApproved;
    m[QStringLiteral("reportPhotosApprovedOn")] = r.reportPhotosApprovedOn;
    m[QStringLiteral("createdAt")]              = r.createdAt;
    // Derived from the index (DEC-89), never stored: 0 until the first read lands.
    const AthleteActivity act = m_activity.value(a.id().toString());
    m[QStringLiteral("lastSessionAt")]          = act.lastAtSec;
    m[QStringLiteral("sessionCount")]           = act.sessions;
    m[QStringLiteral("clubs")]                  = bagOf(r);
    m[QStringLiteral("initials")]               = computeInitials(r.name);
    return m;
}

void AthleteController::reload()
{
    m_ids = m_repo ? m_repo->list() : QList<AthleteId>{};
    rebuild();
}

void AthleteController::rebuild()
{
    QVariantList list;
    list.reserve(m_ids.size());
    for (const AthleteId &id : std::as_const(m_ids)) {
        Athlete *a = m_repo ? m_repo->load(id) : nullptr;
        if (!a) continue;
        // An edit is announced by the Athlete itself (the interim for EVT-14).
        connect(a, &Athlete::recordChanged, this, &AthleteController::rebuild, Qt::UniqueConnection);
        list.append(mapOf(*a));
    }

    // The current athlete: SET-02's, if it is listed. While a session runs the current athlete is kept
    // even when the library no longer lists them (a library re-pointed mid-session, D7): the session's
    // athlete must not change until it ends.
    const AthleteId wanted = AthleteId::fromString(ppSettings().value(kCurrentUuid, QString()).toString());
    Athlete *cur = m_ids.contains(wanted) ? m_repo->load(wanted) : nullptr;
    if (!cur && m_current && sessionRunning() && m_current->id() == wanted) {
        cur = m_current;
        list.append(mapOf(*cur));
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
void AthleteController::sortAthletes(QVariantList &list)
{
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const auto ma = a.toMap();
        const auto mb = b.toMap();
        const qint64 aLast = ma[QStringLiteral("lastSessionAt")].toLongLong();
        const qint64 bLast = mb[QStringLiteral("lastSessionAt")].toLongLong();
        if (aLast != bLast) return aLast > bLast;
        return ma[QStringLiteral("createdAt")].toLongLong() > mb[QStringLiteral("createdAt")].toLongLong();
    });
}

void AthleteController::refreshActivity()
{
    reload();
    if (!m_repo) return;
    QVector<QPair<QString, pinpoint::SessionOwner>> owners;   // taken on this thread
    owners.reserve(m_ids.size());
    for (const AthleteId &id : std::as_const(m_ids))
        if (const Athlete *a = m_repo->load(id))
            owners.append({ id.toString(), a->sessionOwner() });
    const int generation = ++m_activityGeneration;
    pinpoint::AthleteRepository *repo = m_repo;
    const auto readAll = [owners, repo]() {
        QHash<QString, AthleteActivity> out;
        for (const auto &o : owners) out.insert(o.first, repo->readActivity(o.second));   // thread-safe
        return out;
    };
    if (m_syncActivity) { applyActivity(generation, readAll()); return; }
    m_pool.start([this, generation, readAll]() {
        const QHash<QString, AthleteActivity> out = readAll();
        QMetaObject::invokeMethod(this, [this, generation, out]() { applyActivity(generation, out); },
                                  Qt::QueuedConnection);
    });
}

// Patches the list from the athletes held — never a switch, so nothing that follows the current
// athlete re-points.
void AthleteController::applyActivity(int generation, const QHash<QString, AthleteActivity> &byUuid)
{
    if (generation != m_activityGeneration) return;          // a newer read is on its way
    m_activity = byUuid;
    rebuild();
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
    if (name.trimmed().isEmpty() || !m_repo)
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
    const pinpoint::AthleteCreateResult made = m_repo->create(r);
    if (!made.ok()) {
        ppWarn() << "[Athlete] could not create" << r.name << "-" << made.result.error;
        return {};
    }
    const QString newUuid = made.athlete->id().toString();

    // A new athlete becomes current — except while a session runs, when the session's athlete
    // must stay current (its swings are being saved under them; see setSessionRunningCheck).
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

QVariantMap AthleteController::cohortFor(const QString &uuid, const QString &onIsoDate) const
{
    using namespace pinpoint::analysis;

    for (const QVariant &v : m_athletes) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("uuid")).toString() != uuid) continue;

        // Both dates parsed rather than defaulted. An unparseable or absent swing date yields an
        // invalid QDate, and ageBandFor answers "unknown" for it — which is right: substituting
        // today would grade an old swing against the band this golfer is in NOW, and that is the
        // one mistake deriving-at-the-swing-date exists to prevent.
        const QDate dob = QDate::fromString(a.value(QStringLiteral("dob")).toString(), Qt::ISODate);
        const QDate on  = QDate::fromString(onIsoDate, Qt::ISODate);

        // Qualified: this member has the same name, so unqualified lookup would find it in class
        // scope, stop there, and suppress ADL.
        return cohortToMap(
            pinpoint::analysis::cohortFor(dob, a.value(QStringLiteral("sex")).toString(), on));
    }
    return {};                     // no such athlete: unqualified, which grades against everyone
}

QString AthleteController::cohortLabelFor(const QString &dobIsoDate, const QString &sex,
                                          const QString &onIsoDate) const
{
    using namespace pinpoint::analysis;
    // Qualified for the reason cohortFor() above is: the member of the same name is found by
    // ordinary lookup in class scope, which stops the search and suppresses ADL.
    return cohortLabel(pinpoint::analysis::cohortFor(
        QDate::fromString(dobIsoDate, Qt::ISODate), sex,
        QDate::fromString(onIsoDate, Qt::ISODate)));
}

QVariantList AthleteController::sexOptions() const
{
    // "Prefer not to say" is stored as its own token rather than as an empty string. The two mean
    // the same thing to a norm — only rows unqualified on that axis can match — but they do not
    // mean the same thing to the person: an answer they gave has to survive re-opening the form,
    // and a blank field would read as never having been asked.
    auto opt = [](const char *value, const char *label) {
        QVariantMap m;
        m.insert(QStringLiteral("value"), QString::fromLatin1(value));
        m.insert(QStringLiteral("label"), label ? QObject::tr(label) : QString());
        return QVariant(m);
    };
    return { opt("female",   QT_TR_NOOP("Female")),
             opt("male",     QT_TR_NOOP("Male")),
             opt("declined", QT_TR_NOOP("Prefer not to say")) };
}

bool AthleteController::updateAthlete(const QString &uuid, const QString &fieldName, const QVariant &value)
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

QVariantMap AthleteController::reportDetails(const QString &uuidOrName) const
{
    const QString key = uuidOrName.trimmed();
    if (key.isEmpty()) return {};
    QVariantMap found;
    for (const QVariant &v : m_athletes) {                 // uuid first: a name can repeat
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("uuid")).toString() == key) { found = a; break; }
    }
    if (found.isEmpty()) {
        for (const QVariant &v : m_athletes) {
            const QVariantMap a = v.toMap();
            if (a.value(QStringLiteral("name")).toString().trimmed().compare(key, Qt::CaseInsensitive) == 0) { found = a; break; }
        }
    }
    if (found.isEmpty()) return {};

    const QString name     = found.value(QStringLiteral("name")).toString();
    const QString greeting = found.value(QStringLiteral("reportGreeting")).toString().trimmed();
    QVariantMap out;
    out.insert(QStringLiteral("uuid"),             found.value(QStringLiteral("uuid")));
    out.insert(QStringLiteral("name"),             name);
    out.insert(QStringLiteral("email"),            found.value(QStringLiteral("reportEmail")).toString().trimmed());
    out.insert(QStringLiteral("greeting"),         greeting.isEmpty() ? firstWordOf(name) : greeting);
    out.insert(QStringLiteral("dob"),              found.value(QStringLiteral("dob")).toString());
    out.insert(QStringLiteral("photosApproved"),   found.value(QStringLiteral("reportPhotosApproved")).toBool());
    out.insert(QStringLiteral("photosApprovedOn"), found.value(QStringLiteral("reportPhotosApprovedOn")).toString());
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

QVariantMap AthleteController::defaultClubRecord(const QString &clubId) const
{
    return defaultClubRecordFor(clubId, clubSetOf(currentSex(), currentDob()),
                                currentHeightM());
}

QVariantMap AthleteController::clubsFor(const QString &uuid) const
{
    const Athlete *a = athleteFor(uuid);
    return a ? bagOf(a->record()) : QVariantMap{};
}

QVariantMap AthleteController::clubDistances(const QString &uuid) const
{
    const QString kReading = QStringLiteral("Distances are worked out when this golfer's sessions are read");
    QVariantMap out{ { QStringLiteral("items"), QVariantList() },
                     { QStringLiteral("stale"), true },
                     { QStringLiteral("note"),  kReading } };

    const Athlete *a = athleteFor(uuid);
    if (!a || a->folder().isEmpty()) return out;

    const QDir athleteDir(a->folder());
    const QFileInfo file(pinpoint::AthleteFiles::tendenciesFile(athleteDir.path()));
    if (!file.exists()) return out;

    QFile f(file.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    // ITS SCHEMA CHECKED (H-08; step 2.5): a newer build's file is not read as ours — it reads as "being
    // worked out". An older one is shown as before (the catch-up re-reduces it; the stale mark below says so).
    if (root.value(QStringLiteral("schema")).toInt(-1) > pinpoint::analysis::kTendencySchemaVersion) return out;
    out[QStringLiteral("items")] = root.value(QStringLiteral("clubDistances")).toArray().toVariantList();

    // Older than the newest ledger: these numbers were reduced before that session was graded.
    qint64 newestLedger = 0;
    for (const QFileInfo &session : athleteDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo ledger(pinpoint::SessionFiles::diagnosticsFile(session.absoluteFilePath()));
        if (ledger.exists()) newestLedger = std::max(newestLedger, ledger.lastModified().toMSecsSinceEpoch());
    }
    const bool stale = file.lastModified().toMSecsSinceEpoch() < newestLedger;
    out[QStringLiteral("stale")] = stale;
    out[QStringLiteral("note")]  = stale ? kReading : QString();
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
                                      const QVariantMap &record)
{
    if (uuid.isEmpty() || clubId.trimmed().isEmpty())
        return false;
    Athlete *a = athleteFor(uuid);
    if (!a)
        return false;
    QVariantMap clubs = bagOf(a->record());
    clubs.insert(clubId, record);
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

QString AthleteController::wristReferenceSwingDir() const
{
    if (!m_current || !m_sessions) return {};
    const pinpoint::ShotRef ref = m_current->record().wristReference;
    if (ref.isNull()) return {};
    m_current->index();                              // holds the athlete's sessions, so the ref resolves
    const QString folder = m_sessions->swingFolder(ref);
    return QFileInfo(folder).isDir() ? folder : QString();
}

bool AthleteController::setWristReferenceSwingDir(const QString &swingDir)
{
    if (!m_current || !m_sessions) return false;
    pinpoint::ShotRef ref;
    if (!swingDir.isEmpty()) {
        // The path the views carry, translated ONCE, here (DEC-90).
        const QFileInfo swing(swingDir);
        const QString   sessionDir = QDir::cleanPath(swing.absolutePath());
        if (QDir::cleanPath(QFileInfo(sessionDir).absolutePath()) != QDir::cleanPath(QFileInfo(m_current->folder()).absoluteFilePath())) {
            ppInfo() << "[Athlete] the Wrist reference" << swingDir << "is not a swing of" << m_currentName
                     << "- not recorded on their record";
            return false;
        }
        ref = pinpoint::ShotRef(m_sessions->openFolder(sessionDir, m_current->id()),
                                pinpoint::SwingPaths::swingNumberOf(swing.fileName()));
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
