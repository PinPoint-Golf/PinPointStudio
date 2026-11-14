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
#include "../../Diagnostics/norms/norm_pack.h" // Cohort, cohortFor(), cohortToMap()
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QVector>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>

static constexpr auto kGroup         = "athletes";
static constexpr auto kCurrentUuid   = "currentAthleteUuid";
static constexpr auto kName          = "name";
static constexpr auto kHandedness    = "handedness";
static constexpr auto kHeightValue   = "heightValue";
static constexpr auto kHeightUnit    = "heightUnit";
static constexpr auto kWeightValue   = "weightValue";
static constexpr auto kWeightUnit    = "weightUnit";
static constexpr auto kHandicap      = "handicap";
static constexpr auto kPrimaryClub   = "primaryClub";
static constexpr auto kSpeedTarget   = "speedTarget";
static constexpr auto kNotes         = "notes";
// Demographics, both optional, both absent on every record written before norm cohorts
// existed. Stored as strings: an ISO date and a stable token, so a value this build does
// not recognise degrades to "unknown" rather than to a wrong answer.
static constexpr auto kDob           = "dob";
static constexpr auto kSex           = "sex";
// The session report's details (design §7.3). Absent on every record written before the report.
static constexpr auto kReportEmail        = "reportEmail";
static constexpr auto kReportGreeting     = "reportGreeting";
static constexpr auto kReportPhotosOk     = "reportPhotosApproved";
static constexpr auto kReportPhotosOkOn   = "reportPhotosApprovedOn";
static constexpr auto kCreatedAt     = "createdAt";
static constexpr auto kLastSessionAt = "lastSessionAt";
static constexpr auto kSessionCount  = "sessionCount";
static constexpr auto kClubs         = "clubs";

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
    reload();
}

AthleteController::~AthleteController()
{
    m_pool.waitForDone();
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

void AthleteController::reload()
{
    QSettings s = ppSettings();

    // Collect all athlete UUIDs
    s.beginGroup(kGroup);
    const QStringList uuids = s.childGroups();
    s.endGroup();

    QVariantList list;
    list.reserve(uuids.size());

    for (const QString &uuid : uuids) {
        s.beginGroup(kGroup);
        s.beginGroup(uuid);

        QVariantMap m;
        m[QStringLiteral("uuid")]          = uuid;
        m[QStringLiteral("name")]          = s.value(kName,         QString()).toString();
        m[QStringLiteral("handedness")]    = s.value(kHandedness,   QStringLiteral("Right")).toString();
        m[QStringLiteral("heightValue")]   = s.value(kHeightValue,  0.0).toDouble();
        m[QStringLiteral("heightUnit")]    = s.value(kHeightUnit,   QStringLiteral("ft")).toString();
        m[QStringLiteral("weightValue")]   = s.value(kWeightValue,  0.0).toDouble();
        m[QStringLiteral("weightUnit")]    = s.value(kWeightUnit,   QStringLiteral("lb")).toString();
        m[QStringLiteral("handicap")]      = s.value(kHandicap,     -999.0).toDouble();
        m[QStringLiteral("primaryClub")]   = normalizeClubId(s.value(kPrimaryClub, QStringLiteral("DRIVER")).toString());
        m[QStringLiteral("speedTarget")]   = s.value(kSpeedTarget,  0.0).toDouble();
        m[QStringLiteral("notes")]         = s.value(kNotes,        QString()).toString();
        m[QStringLiteral("dob")]           = s.value(kDob,          QString()).toString();
        m[QStringLiteral("sex")]           = s.value(kSex,          QString()).toString();
        // Without these four here they would be written and never read back: reload() builds each
        // record from this fixed list, not from whatever keys the group holds.
        m[QStringLiteral("reportEmail")]            = s.value(kReportEmail,      QString()).toString();
        m[QStringLiteral("reportGreeting")]         = s.value(kReportGreeting,   QString()).toString();
        m[QStringLiteral("reportPhotosApproved")]   = s.value(kReportPhotosOk,   false).toBool();
        m[QStringLiteral("reportPhotosApprovedOn")] = s.value(kReportPhotosOkOn, QString()).toString();
        m[QStringLiteral("createdAt")]     = s.value(kCreatedAt,    0LL).toLongLong();
        m[QStringLiteral("lastSessionAt")] = s.value(kLastSessionAt,0LL).toLongLong();
        m[QStringLiteral("sessionCount")]  = s.value(kSessionCount, 0).toInt();
        // The library's figures win over the registry's (which nothing updates) once a scan has run.
        if (const auto it = m_activity.constFind(uuid); it != m_activity.cend()) {
            m[QStringLiteral("lastSessionAt")] = it->lastAtSec;
            m[QStringLiteral("sessionCount")]  = it->sessions;
        }
        if (!s.contains(kClubs)) {
            // First sight of this athlete's bag: seed the standard set
            // (driver, 5-9 iron, GW, SW, putter) with factory defaults.
            // Written once — an intentionally emptied bag stays empty.
            QVariantMap bag;
            const pinpoint::ClubSet set = clubSetOf(m.value(QStringLiteral("sex")).toString(),
                                                    m.value(QStringLiteral("dob")).toString());
            const double heightM = m.value(QStringLiteral("heightValue")).toDouble() * 0.3048;
            for (const QString &id : defaultBag())
                bag.insert(id, defaultClubRecordFor(id, set, heightM));
            s.setValue(kClubs, bag);
        }
        m[QStringLiteral("clubs")]         = s.value(kClubs,        QVariantMap{}).toMap();
        m[QStringLiteral("initials")]      = computeInitials(m[QStringLiteral("name")].toString());

        s.endGroup(); // uuid
        s.endGroup(); // athletes

        list.append(m);
    }

    sortAthletes(list);

    m_athletes = list;
    emit athletesChanged();
    refreshActivity();          // a rename moves the folder; a new golfer needs one

    // Resolve current athlete
    const QString wantedUuid = s.value(kCurrentUuid, QString()).toString();
    bool found = false;
    for (const QVariant &v : std::as_const(m_athletes)) {
        const auto m = v.toMap();
        if (m[QStringLiteral("uuid")].toString() == wantedUuid) {
            m_currentUuid       = wantedUuid;
            m_currentName       = m[QStringLiteral("name")].toString();
            m_currentInitials   = m[QStringLiteral("initials")].toString();
            m_currentHandedness = m[QStringLiteral("handedness")].toString();
            found = true;
            break;
        }
    }
    if (!found) {
        m_currentUuid       = {};
        m_currentName       = {};
        m_currentInitials   = {};
        m_currentHandedness = {};
    }

    emit currentAthleteChanged();
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

// Worker thread. Touches no member.
AthleteController::Activity AthleteController::scanActivity(const QString &athleteDir)
{
    Activity a;
    if (athleteDir.isEmpty()) return a;
    const QFileInfoList sessions = QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : sessions) {
        // The Sessions list's rule: a folder nothing was hit in is not a session.
        if (QDir(fi.absoluteFilePath()).entryList(QStringList{ QStringLiteral("swing_*") },
                                                  QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
            continue;
        ++a.sessions;
        const QDate named = QDate::fromString(fi.fileName().left(10), QStringLiteral("yyyy-MM-dd"));
        const qint64 at = named.isValid() ? QDateTime(named, QTime(0, 0)).toSecsSinceEpoch()
                                          : fi.lastModified().toSecsSinceEpoch();
        a.lastAtSec = std::max(a.lastAtSec, at);
    }
    return a;
}

void AthleteController::setAthleteDirResolver(std::function<QString(const QString &)> resolver)
{
    m_dirFor = std::move(resolver);
    refreshActivity();
}

void AthleteController::refreshActivity()
{
    if (!m_dirFor) return;
    QVector<QPair<QString, QString>> dirs;                    // uuid, folder — taken on this thread
    dirs.reserve(m_athletes.size());
    for (const QVariant &v : std::as_const(m_athletes)) {
        const QVariantMap m = v.toMap();
        dirs.append({ m.value(QStringLiteral("uuid")).toString(),
                      m_dirFor(m.value(QStringLiteral("name")).toString()) });
    }
    const int generation = ++m_activityGeneration;
    const auto scanAll = [dirs]() {
        QHash<QString, Activity> out;
        for (const auto &d : dirs) out.insert(d.first, scanActivity(d.second));
        return out;
    };
    if (m_syncActivity) { applyActivity(generation, scanAll()); return; }
    m_pool.start([this, generation, scanAll]() {
        const QHash<QString, Activity> out = scanAll();
        QMetaObject::invokeMethod(this, [this, generation, out]() { applyActivity(generation, out); },
                                  Qt::QueuedConnection);
    });
}

// Patches the list in place — no reload(), which would re-announce the current golfer and re-point
// everything that follows it.
void AthleteController::applyActivity(int generation, const QHash<QString, Activity> &byUuid)
{
    if (generation != m_activityGeneration) return;          // a newer scan is on its way
    m_activity = byUuid;
    QVariantList list = m_athletes;
    bool changed = false;
    for (QVariant &v : list) {
        QVariantMap m = v.toMap();
        const auto it = byUuid.constFind(m.value(QStringLiteral("uuid")).toString());
        if (it == byUuid.cend()) continue;
        if (m.value(QStringLiteral("sessionCount")).toInt() == it->sessions
            && m.value(QStringLiteral("lastSessionAt")).toLongLong() == it->lastAtSec)
            continue;
        m[QStringLiteral("sessionCount")]  = it->sessions;
        m[QStringLiteral("lastSessionAt")] = it->lastAtSec;
        v = m;
        changed = true;
    }
    if (!changed) return;
    sortAthletes(list);
    m_athletes = list;
    emit athletesChanged();
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
    if (name.trimmed().isEmpty())
        return {};

    const bool creating = uuid.isEmpty();
    QString    targetUuid = uuid;

    QSettings s = ppSettings();

    if (creating) {
        targetUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } else {
        s.beginGroup(kGroup);
        const bool exists = s.childGroups().contains(uuid);
        s.endGroup();
        if (!exists)
            return {};                       // unknown uuid -> no-op
    }

    // Convert to base unit before storing (always ft / lb).
    double storedHeight = heightValue;
    if (heightUnit == QLatin1String("cm") && heightValue > 0.0)
        storedHeight = heightValue / 30.48;

    double storedWeight = weightValue;
    if (weightUnit == QLatin1String("kg") && weightValue > 0.0)
        storedWeight = weightValue * 2.20462;

    s.beginGroup(kGroup);
    s.beginGroup(targetUuid);

    s.setValue(kName,        name.trimmed());
    s.setValue(kHandedness,  handedness);
    s.setValue(kHeightValue, storedHeight);
    s.setValue(kHeightUnit,  heightUnit);
    s.setValue(kWeightValue, storedWeight);
    s.setValue(kWeightUnit,  weightUnit);
    s.setValue(kHandicap,    handicap);
    s.setValue(kPrimaryClub, normalizeClubId(primaryClub));
    s.setValue(kSpeedTarget, speedTarget);
    s.setValue(kNotes,       notes);

    if (creating) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        s.setValue(kCreatedAt,     now);
        s.setValue(kLastSessionAt, 0LL);
        s.setValue(kSessionCount,  0);
    }
    // On update: createdAt / lastSessionAt / sessionCount are intentionally
    // left untouched.

    s.endGroup(); // uuid
    s.endGroup(); // athletes

    // A new athlete becomes current — except while a session runs, when the session's athlete
    // must stay current (its swings are being saved under them; see setSessionRunningCheck).
    // On update: do NOT change the current selection.
    if (creating) {
        if (sessionRunning())
            ppInfo() << "[Athlete] created" << name.trimmed() << targetUuid
                     << "but kept" << m_currentName << "current: a session is running for them";
        else
            s.setValue(kCurrentUuid, targetUuid);   // auto-select new athlete
    }

    s.sync();
    reload();
    return targetUuid;
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
    QSettings s = ppSettings();

    s.beginGroup(kGroup);
    const bool exists = s.childGroups().contains(uuid);
    s.endGroup();

    if (!exists)
        return false;

    s.beginGroup(kGroup);
    s.beginGroup(uuid);
    s.setValue(fieldName, value);
    s.endGroup();
    s.endGroup();
    s.sync();

    reload();
    return true;
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

void AthleteController::setPhotosApproved(const QString &uuid, bool approved)
{
    QSettings s = ppSettings();
    s.beginGroup(kGroup);
    const bool exists = s.childGroups().contains(uuid);
    s.endGroup();
    if (!exists) return;

    s.beginGroup(kGroup);
    s.beginGroup(uuid);
    const bool was = s.value(kReportPhotosOk, false).toBool();
    if (approved) {
        s.setValue(kReportPhotosOk, true);
        if (!was || s.value(kReportPhotosOkOn).toString().isEmpty())
            s.setValue(kReportPhotosOkOn, QDate::currentDate().toString(Qt::ISODate));
    } else {
        s.setValue(kReportPhotosOk, false);
        s.remove(kReportPhotosOkOn);
    }
    s.endGroup();
    s.endGroup();
    s.sync();
    reload();
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
    QSettings s = ppSettings();
    s.beginGroup(kGroup);
    s.beginGroup(uuid);
    const QVariantMap clubs = s.value(kClubs, QVariantMap{}).toMap();
    s.endGroup();
    s.endGroup();
    return clubs;
}

QVariantMap AthleteController::clubDistances(const QString &uuid) const
{
    const QString kReading = QStringLiteral("Distances are worked out when this golfer's sessions are read");
    QVariantMap out{ { QStringLiteral("items"), QVariantList() },
                     { QStringLiteral("stale"), true },
                     { QStringLiteral("note"),  kReading } };

    QString name;
    for (const QVariant &v : m_athletes) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("uuid")).toString() == uuid) { name = a.value(QStringLiteral("name")).toString(); break; }
    }
    if (name.isEmpty() || !m_dirFor) return out;

    const QDir athleteDir(m_dirFor(name));
    const QFileInfo file(athleteDir.filePath(QStringLiteral("tendencies.json")));
    if (!file.exists()) return out;

    QFile f(file.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    out[QStringLiteral("items")] = root.value(QStringLiteral("clubDistances")).toArray().toVariantList();

    // Older than the newest ledger: these numbers were reduced before that session was graded.
    qint64 newestLedger = 0;
    for (const QFileInfo &session : athleteDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo ledger(QDir(session.absoluteFilePath()).filePath(QStringLiteral("diagnostics.json")));
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
    const QVariantMap bag = clubsFor(uuid);

    QSettings s = ppSettings();
    s.beginGroup(kGroup);
    s.beginGroup(uuid);
    const QString stored = normalizeClubId(s.value(kPrimaryClub, QStringLiteral("DRIVER")).toString());
    s.endGroup();
    s.endGroup();

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
    QVariantMap clubs = clubsFor(uuid);
    clubs.insert(clubId, record);
    return updateAthlete(uuid, QString::fromLatin1(kClubs), clubs);
}

bool AthleteController::removeClubRecord(const QString &uuid, const QString &clubId)
{
    QVariantMap clubs = clubsFor(uuid);
    if (clubs.remove(clubId) == 0)
        return false;
    return updateAthlete(uuid, QString::fromLatin1(kClubs), clubs);
}

bool AthleteController::deleteAthlete(const QString &uuid)
{
    if (sessionRunning() && !uuid.isEmpty() && uuid == m_currentUuid) {
        ppInfo() << "[Athlete] refused to delete" << m_currentName
                 << "- a session is running for them; end it first";
        return false;
    }

    QSettings s = ppSettings();

    s.beginGroup(kGroup);
    s.remove(uuid);
    s.endGroup();

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
