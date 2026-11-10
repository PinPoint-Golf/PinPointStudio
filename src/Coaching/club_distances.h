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

#pragma once

// CLUB DISTANCES — pure, header-only, no Qt-GUI. What a golfer's clubs carry, read from the ledger's
// per-shot launch monitor fields (schema 11) and nothing else (docs/design/shot_outcomes_design.md §7.5):
//
//   poolSessionByClub()    one session → the qualifying shots of each club it used
//   clubBaselineAsOf()     every session → the frozen usual numbers a session is judged against,
//                          from strictly earlier days only (Contract A)
//   reduceClubDistances()  every session → one row per club for "Your distances"
//
// Yards only: distances are stored and shown in yd, with no conversion anywhere. Every number comes
// through lmSessionStats(), so a club's mean and SD are the same arithmetic as the launch monitor panel.
//
// WHICH SHOTS COUNT. A shot qualifies when it was not a part swing and has a carry. A strike miss (chunk, thin, top, sky, shank, corrections applied) is left out and counted.
// The miss is read off the ledger's CHECKED rows (checkStrikesAgainstFlight, strike_from_flight.h): a measured
// thin the flight contradicts (a sticker placed off) is overruled there, is not a miss, and its carry counts.
// Sky is the flight rule's (flightSaysSky), whatever the rows say.
// ShotRecord::dataWarning is deliberately NOT a filter: it also marks a refused club track or a broken
// recording, and the monitor's readings on those shots stand. The monitor's own flag is the import's
// drop list (LaunchMonitorReading::dropped), which leaves the field absent — so a refused carry is
// already a missing carry and the shot does not qualify.
//
// Unit-tested standalone in src/Analysis/tests/club_distances_test.cpp.

#include "diagnostic_ledger.h"
#include "strike_from_flight.h"
#include "../Core/club_vocabulary.h"
#include "../Diagnostics/engine/excludes_rule.h"
#include "../Diagnostics/measures/club_baseline_provider.h"
#include "../LaunchMonitor/lm_session_reductions.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int    kClubDistanceWindowSessions = 10;   // the club's last 10 sessions …
inline constexpr int    kClubDistanceWindowShots    = 60;   // … or last 60 qualifying shots, whichever is fewer
inline constexpr double kClubShortBelowSds          = 1.5;  // "short" is under mean − 1.5 × SD (D8)
inline constexpr double kPartialShaftAngleFraction  = 0.80; // D6: P4 shaft angle under this share of the usual
inline constexpr double kPartialSpeedFraction       = 0.85; // D6: club speed under this share of the usual
inline constexpr int    kClubTrendRecentSessions    = 3;
inline constexpr double kClubTrendMinYards          = 3.0;
inline constexpr double kClubTrendSpreadChange      = 0.20;
inline constexpr int    kClubBaselineSchemaVersion  = 4;    // club_baseline.json; 2: strikes read from the flight leave the pool; 3: every shot counts; 4: misses off the flight-checked rows (a sticker-offset thin counts), Sky the flight's

// A golfer's correction to one shot's labels (diagnostics.json `corrections`). `from` is what was
// detected, `to` what it should have been ("" = none in that family).
struct ShotCorrection {
    QString family, from, to;
};

// One session as the reductions read it.
struct ClubDistanceSession {
    QString folder;     // session folder name
    QDate   day;        // sessionDayOfFolder(); invalid = a folder with no date, ordered nowhere
    std::vector<ShotRecord> shots;      // in strike order
    QHash<int, std::vector<ShotCorrection>> corrections;      // keyed by shotId
};

// The leading yyyy-MM-dd of a session folder ("<date>_<athlete>_<type>_<nn>"). A session's day is the
// day in its name, so re-analysis, copying and clock changes cannot reorder a golfer's history. Takes a
// bare name or a path.
inline QDate sessionDayOfFolder(const QString &folder)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})"));
    const QRegularExpressionMatch m = re.match(QFileInfo(QDir::cleanPath(folder)).fileName());
    if (!m.hasMatch()) return QDate();
    return QDate::fromString(m.captured(1), Qt::ISODate);
}

// A canonical club id for display ("7 IRON" -> "7 iron"). Keys stay uppercase.
inline QString clubDistanceLabel(const QString &id)
{
    return id.isEmpty() ? QString() : id.left(1).toUpper() + id.mid(1).toLower();
}

// ── Formatting ──────────────────────────────────────────────────────────────

// "161 ± 7 yd". An SD below the floor is an em dash and the mean still stands ("161 ± — yd"); no
// mean is the em dash alone.
inline QString yardsMeanSdText(double mean, double sd)
{
    if (!std::isfinite(mean)) return QStringLiteral("—");
    return lmFormat(mean, 0) + QStringLiteral(" ± ")
         + (std::isfinite(sd) ? lmFormat(sd, 0) : QStringLiteral("—")) + QStringLiteral(" yd");
}

// "161 yd", or the em dash.
inline QString yardsText(double yards)
{
    return std::isfinite(yards) ? lmFormat(yards, 0) + QStringLiteral(" yd") : QStringLiteral("—");
}

// "24 yd short".
inline QString yardsShortText(double yards)
{
    return yardsText(yards) + QStringLiteral(" short");
}

// ── Swing length ────────────────────────────────────────────────────────────

// PARTIAL when the backswing is well short of, or the speed well under, the club's usual (Contract E,
// D6). `speed` is the shot's launch monitor club speed. A missing number leaves that half out; no
// history, or history with neither number, is "full". `ballSpeed` is the last rung, for a ball-only
// monitor with no club track: a mishit also loses ball speed, so it's only asked when neither of the
// other two can answer.
inline QString classifySwingLength(double shaftAngleP4, double speed, const ClubBaselineEntry *usual,
                                   double ballSpeed = std::numeric_limits<double>::quiet_NaN())
{
    if (!usual) return QStringLiteral("full");
    const bool angleAnswers = std::isfinite(shaftAngleP4) && std::isfinite(usual->shaftAngleP4Mean) && usual->shaftAngleP4Mean != 0.0;
    const bool speedAnswers = std::isfinite(speed) && std::isfinite(usual->speedMean) && usual->speedMean > 0.0;
    // The angle's sign is the track's, and "shorter" means a smaller turn either way round.
    if (angleAnswers && std::fabs(shaftAngleP4) < kPartialShaftAngleFraction * std::fabs(usual->shaftAngleP4Mean))
        return QStringLiteral("partial");
    if (speedAnswers && speed < kPartialSpeedFraction * usual->speedMean)
        return QStringLiteral("partial");
    if (!angleAnswers && !speedAnswers && std::isfinite(ballSpeed) && std::isfinite(usual->ballSpeedMean)
        && usual->ballSpeedMean > 0.0 && ballSpeed < kPartialSpeedFraction * usual->ballSpeedMean)
        return QStringLiteral("partial");
    return QStringLiteral("full");
}

// ── The pool ────────────────────────────────────────────────────────────────

// The five strike misses left out of the average. Shots with any other label are real distances.
inline bool isClubDistanceMishit(const QString &conditionId)
{
    return conditionId == QLatin1String("chunk") || conditionId == QLatin1String("thin")
        || conditionId == QLatin1String("top")   || conditionId == QLatin1String("sky")
        || conditionId == QLatin1String("shank");
}

// A shot's strike miss, or "" for none, corrections applied. A correction to a miss is the golfer's word
// and is taken as it stands. Otherwise the fired misses resolve by the excludes keep-rule, so a topped
// shot (which fires thin as well) counts once, as a top; the rows are the ledger's checked ones, so a fired
// sky is already the flight's Sky and an overruled thin is no miss. With no strike measured, `flight` (the
// club's usual flight this session) lets the flight name the strike; with `norms` too, a shot the flight rule
// calls Sky goes out as a sky (flightSaysSky: driver, woods and hybrids only); and a carry far under the usual
// goes out as "short": a fat shot a ball-only monitor can't name would otherwise drag the usual down.
inline QString shotMishitKind(const ShotRecord &shot, const std::vector<ShotCorrection> &corrections,
                              const ClubFlightUsual *flight = nullptr, const FlightNorms *norms = nullptr)
{
    QString corrected;
    QStringList removed;
    for (const ShotCorrection &c : corrections) {
        if (!c.from.isEmpty()) removed << c.from;
        if (isClubDistanceMishit(c.to)) corrected = c.to;
    }
    if (!corrected.isEmpty()) return corrected;

    const ConditionRow *best = nullptr;
    for (const ConditionRow &r : shot.rows) {
        if (r.state != ShotState::Fired || !isClubDistanceMishit(r.conditionId) || removed.contains(r.conditionId))
            continue;
        if (!best || !keepFirstOfExcludingPair(best->confidence, best->firedSignalCount, best->conditionId,
                                               r.confidence, r.firedSignalCount, r.conditionId))
            best = &r;
    }
    if (best) return best->conditionId;
    if (!flight) return QString();

    const QString read = strikeFromFlight(shot, *flight);
    if (!read.isEmpty()) return removed.contains(read) ? QString() : read;
    if (norms && flightSaysSky(shot, *flight, *norms)) return removed.contains(QStringLiteral("sky")) ? QString() : QStringLiteral("sky");
    // The golfer's "that strike was fine" stands against this as well.
    const bool strikeCorrected = std::any_of(corrections.begin(), corrections.end(),
                                             [](const ShotCorrection &c) { return c.family == QLatin1String("strike"); });
    if (!strikeCorrected && strikeHeightUnread(shot) && std::isfinite(shot.carry) && std::isfinite(flight->carry)
        && shot.carry < kFlightShortCarryShare * flight->carry)
        return QStringLiteral("short");
    return QString();
}

// How many shots went out, by kind.
struct ClubMishits {
    int chunk = 0, thin = 0, top = 0, sky = 0, shank = 0, veryShort = 0;
    int total() const { return chunk + thin + top + sky + shank + veryShort; }
    void add(const QString &kind)
    {
        if (kind == QLatin1String("chunk"))      ++chunk;
        else if (kind == QLatin1String("thin"))  ++thin;
        else if (kind == QLatin1String("top"))   ++top;
        else if (kind == QLatin1String("sky"))   ++sky;
        else if (kind == QLatin1String("shank")) ++shank;
        else if (kind == QLatin1String("short")) ++veryShort;
    }
};

// One qualifying shot's readings, keyed as lmSessionStats reads them.
struct ClubPoolShot {
    int          position = 0;           // the shot's index in its session
    LmShotValues lm;
    double       shaftAngleP4 = std::numeric_limits<double>::quiet_NaN();
};

struct ClubPoolMishit {
    int     position = 0;
    QString kind;
};

// One club's shots in one session.
struct ClubSessionPool {
    QString club;       // canonical
    QString folder;
    QDate   day;
    std::vector<ClubPoolShot>   shots;
    std::vector<ClubPoolMishit> mishits;
};

// The session's pools, one per club, in the order the clubs were first hit. `norms` is the flight table a
// shot's Sky is read against (the shipped one unless a test hands another).
inline std::vector<ClubSessionPool> poolSessionByClub(const ClubDistanceSession &session,
                                                      const FlightNorms &norms = shippedFlightNorms())
{
    std::vector<ClubSessionPool> pools;
    QHash<QString, int> indexOf;
    const QHash<QString, ClubFlightUsual> flightUsuals = sessionFlightUsuals(session.shots);
    for (int i = 0; i < int(session.shots.size()); ++i) {
        const ShotRecord &s = session.shots[size_t(i)];
        if (s.swingLength == QLatin1String("partial")) continue;
        if (!std::isfinite(s.carry)) continue;
        const QString club = canonicalClubId(s.club);
        if (club.isEmpty()) continue;

        auto it = indexOf.constFind(club);
        if (it == indexOf.constEnd()) {
            it = indexOf.insert(club, int(pools.size()));
            ClubSessionPool p;
            p.club   = club;
            p.folder = session.folder;
            p.day    = session.day;
            pools.push_back(std::move(p));
        }
        ClubSessionPool &pool = pools[size_t(*it)];

        const auto flightIt = flightUsuals.constFind(club);
        const QString kind = shotMishitKind(s, session.corrections.value(s.shotId),
                                            flightIt == flightUsuals.constEnd() ? nullptr : &*flightIt, &norms);
        if (!kind.isEmpty()) {
            pool.mishits.push_back(ClubPoolMishit{ i, kind });
            continue;
        }

        ClubPoolShot shot;
        shot.position     = i;
        shot.shaftAngleP4 = s.shaftAngleP4;
        shot.lm.insert(QStringLiteral("lm.carryDistance"), s.carry);
        if (std::isfinite(s.total))       shot.lm.insert(QStringLiteral("lm.totalDistance"), s.total);
        if (std::isfinite(s.offline))     shot.lm.insert(QStringLiteral("lm.offline"), s.offline);
        if (std::isfinite(s.ballSpeed))   shot.lm.insert(QStringLiteral("lm.ballSpeed"), s.ballSpeed);
        if (std::isfinite(s.lmClubSpeed)) shot.lm.insert(QStringLiteral("lm.clubheadSpeed"), s.lmClubSpeed);
        if (std::isfinite(s.smash))       shot.lm.insert(QStringLiteral("lm.smashFactor"), s.smash);
        pool.shots.push_back(std::move(shot));
    }
    return pools;
}

// The sessions in day order, oldest first, then by folder name within a day. Sessions with no date
// are left out: nothing can say what comes before or after them.
inline std::vector<const ClubDistanceSession *> sessionsByDay(const std::vector<ClubDistanceSession> &sessions)
{
    std::vector<const ClubDistanceSession *> out;
    out.reserve(sessions.size());
    for (const ClubDistanceSession &s : sessions)
        if (s.day.isValid()) out.push_back(&s);
    std::stable_sort(out.begin(), out.end(), [](const ClubDistanceSession *a, const ClubDistanceSession *b) {
        return a->day != b->day ? a->day < b->day : a->folder < b->folder;
    });
    return out;
}

// A club's window: the newest sessions and shots up to the caps, oldest first.
struct ClubWindowSession {
    QString folder;
    QDate   day;
    std::vector<ClubPoolShot> shots;
};

struct ClubDistanceWindow {
    QString club;
    std::vector<ClubWindowSession> sessions;
    ClubMishits mishits;
    int shotCount = 0;
};

// Newest first, the shots from the end of each session, stopping at whichever cap comes first. A
// session whose only shots with the club were mishits adds its count and uses none of the cap. A
// session cut part-way counts only the mishits at or after the earliest shot it kept.
inline ClubDistanceWindow clubWindow(const QString &club, const std::vector<ClubSessionPool> &oldestFirst)
{
    ClubDistanceWindow window;
    window.club = club;
    std::vector<ClubWindowSession> newestFirst;
    for (auto it = oldestFirst.rbegin(); it != oldestFirst.rend(); ++it) {
        if (int(newestFirst.size()) >= kClubDistanceWindowSessions || window.shotCount >= kClubDistanceWindowShots)
            break;
        const ClubSessionPool &pool = *it;
        if (pool.shots.empty()) {
            for (const ClubPoolMishit &m : pool.mishits) window.mishits.add(m.kind);
            continue;
        }
        const int take  = std::min(int(pool.shots.size()), kClubDistanceWindowShots - window.shotCount);
        const int first = int(pool.shots.size()) - take;
        ClubWindowSession part;
        part.folder = pool.folder;
        part.day    = pool.day;
        part.shots.assign(pool.shots.begin() + first, pool.shots.end());
        const int earliestKept = first > 0 ? part.shots.front().position : -1;
        for (const ClubPoolMishit &m : pool.mishits)
            if (m.position >= earliestKept) window.mishits.add(m.kind);
        window.shotCount += take;
        newestFirst.push_back(std::move(part));
    }
    window.sessions.assign(newestFirst.rbegin(), newestFirst.rend());
    return window;
}

// Each club's pools across the given sessions, oldest first.
inline QHash<QString, std::vector<ClubSessionPool>>
poolsByClub(const std::vector<const ClubDistanceSession *> &oldestFirst, const FlightNorms &norms = shippedFlightNorms())
{
    QHash<QString, std::vector<ClubSessionPool>> out;
    for (const ClubDistanceSession *s : oldestFirst)
        for (ClubSessionPool &p : poolSessionByClub(*s, norms))
            out[p.club].push_back(std::move(p));
    return out;
}

// ── Statistics ──────────────────────────────────────────────────────────────

inline const LmFieldStats *clubStat(const std::vector<LmFieldStats> &stats, const char *key)
{
    for (const LmFieldStats &st : stats)
        if (st.key == QLatin1String(key)) return &st;
    return nullptr;
}

inline double clubStatMean(const std::vector<LmFieldStats> &stats, const char *key)
{
    const LmFieldStats *st = clubStat(stats, key);
    return st ? st->mean : std::numeric_limits<double>::quiet_NaN();
}

// The SD, or NaN below the floor: the caller prints an em dash, never a number nobody should read.
inline double clubStatSd(const std::vector<LmFieldStats> &stats, const char *key)
{
    const LmFieldStats *st = clubStat(stats, key);
    return (st && st->hasSpread) ? st->sd : std::numeric_limits<double>::quiet_NaN();
}

inline std::vector<LmShotValues> windowLmValues(const std::vector<ClubWindowSession> &sessions)
{
    std::vector<LmShotValues> out;
    for (const ClubWindowSession &s : sessions)
        for (const ClubPoolShot &shot : s.shots) out.push_back(shot.lm);
    return out;
}

// ── The frozen baseline (Contract A) ────────────────────────────────────────

// What a session is judged against, read from <session>/club_baseline.json.
struct ClubBaselineSnapshot {
    int     schema = kClubBaselineSchemaVersion;
    QString frozenAt;
    QHash<QString, ClubBaselineEntry> clubs;
};

// The baseline for one club from its window.
inline ClubBaselineEntry clubBaselineEntryFromWindow(const ClubDistanceWindow &window)
{
    ClubBaselineEntry e;
    e.club = window.club;
    const std::vector<LmFieldStats> stats = lmSessionStats(windowLmValues(window.sessions), -1);
    e.carryMean     = clubStatMean(stats, "lm.carryDistance");
    e.carrySd       = clubStatSd(stats, "lm.carryDistance");
    e.ballSpeedMean = clubStatMean(stats, "lm.ballSpeed");
    e.ballSpeedSd   = clubStatSd(stats, "lm.ballSpeed");
    e.speedMean     = clubStatMean(stats, "lm.clubheadSpeed");
    e.smashMean     = clubStatMean(stats, "lm.smashFactor");
    if (const LmFieldStats *carry = clubStat(stats, "lm.carryDistance")) e.n = carry->n;

    double sum = 0.0;
    int count = 0;
    for (const ClubWindowSession &s : window.sessions)
        for (const ClubPoolShot &shot : s.shots)
            if (std::isfinite(shot.shaftAngleP4)) {
                sum += shot.shaftAngleP4;
                ++count;
            }
    if (count > 0) e.shaftAngleP4Mean = sum / double(count);
    return e;
}

// The usual numbers as of a session on `day`: strictly earlier days only, so a session and its
// same-day neighbours never judge each other. A club with no qualifying shots has no entry.
inline QHash<QString, ClubBaselineEntry> clubBaselineAsOf(const std::vector<ClubDistanceSession> &sessions,
                                                          const QDate &day,
                                                          const FlightNorms &norms = shippedFlightNorms())
{
    QHash<QString, ClubBaselineEntry> out;
    if (!day.isValid()) return out;

    std::vector<const ClubDistanceSession *> earlier;
    for (const ClubDistanceSession *s : sessionsByDay(sessions))
        if (s->day < day) earlier.push_back(s);

    const QHash<QString, std::vector<ClubSessionPool>> pools = poolsByClub(earlier, norms);
    for (auto it = pools.constBegin(); it != pools.constEnd(); ++it) {
        const ClubDistanceWindow window = clubWindow(it.key(), it.value());
        if (window.shotCount > 0) out.insert(it.key(), clubBaselineEntryFromWindow(window));
    }
    return out;
}

// JSON for the sidecar: { schema, frozenAt, clubs: { "<club>": {...} } }. A non-finite number is left
// out and reads back as NaN, so absent stays absent.
inline QJsonObject clubBaselineToJson(const ClubBaselineSnapshot &snap)
{
    QJsonObject clubs;
    for (auto it = snap.clubs.constBegin(); it != snap.clubs.constEnd(); ++it) {
        const ClubBaselineEntry &e = it.value();
        QJsonObject o;
        o[QStringLiteral("club")] = e.club;
        const auto put = [&o](const char *key, double v) {
            if (std::isfinite(v)) o[QLatin1String(key)] = v;
        };
        put("carryMean", e.carryMean);
        put("carrySd", e.carrySd);
        put("ballSpeedMean", e.ballSpeedMean);
        put("ballSpeedSd", e.ballSpeedSd);
        put("speedMean", e.speedMean);
        put("smashMean", e.smashMean);
        put("shaftAngleP4Mean", e.shaftAngleP4Mean);
        o[QStringLiteral("n")] = e.n;
        clubs[it.key()] = o;
    }
    QJsonObject root;
    root[QStringLiteral("schema")]   = snap.schema;
    root[QStringLiteral("frozenAt")] = snap.frozenAt;
    root[QStringLiteral("clubs")]    = clubs;
    return root;
}

// A snapshot written under an older rule for what goes in the pool: the one case a frozen baseline is
// written again. An unreadable file, or one from a newer build, stays frozen and empty as before.
inline bool clubBaselineIsOutdated(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const int schema = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("schema")).toInt(0);
    return schema > 0 && schema < kClubBaselineSchemaVersion;
}

// `*ok` is false for a file of another schema: its numbers are not to be read as ours.
inline ClubBaselineSnapshot clubBaselineFromJson(const QJsonObject &root, bool *ok = nullptr)
{
    ClubBaselineSnapshot snap;
    snap.schema = root.value(QStringLiteral("schema")).toInt(0);
    const bool good = snap.schema == kClubBaselineSchemaVersion;
    if (ok) *ok = good;
    if (!good) return snap;

    snap.frozenAt = root.value(QStringLiteral("frozenAt")).toString();
    const QJsonObject clubs = root.value(QStringLiteral("clubs")).toObject();
    for (auto it = clubs.constBegin(); it != clubs.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        ClubBaselineEntry e;
        e.club = o.value(QStringLiteral("club")).toString(it.key());
        const auto get = [&o](const char *key) {
            const QJsonValue v = o.value(QLatin1String(key));
            return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
        };
        e.carryMean        = get("carryMean");
        e.carrySd          = get("carrySd");
        e.ballSpeedMean    = get("ballSpeedMean");
        e.ballSpeedSd      = get("ballSpeedSd");
        e.speedMean        = get("speedMean");
        e.smashMean        = get("smashMean");
        e.shaftAngleP4Mean = get("shaftAngleP4Mean");
        e.n                = o.value(QStringLiteral("n")).toInt();
        snap.clubs.insert(it.key(), e);
    }
    return snap;
}

// ── Your distances ──────────────────────────────────────────────────────────

struct ClubDistanceBand {
    double lo   = std::numeric_limits<double>::quiet_NaN();
    double hi   = std::numeric_limits<double>::quiet_NaN();
    double mean = std::numeric_limits<double>::quiet_NaN();
};

struct ClubDistanceSessionPoint {
    QString date;       // yyyy-MM-dd
    double  mean = std::numeric_limits<double>::quiet_NaN();
    double  sd   = std::numeric_limits<double>::quiet_NaN();
    int     n    = 0;
};

// One club's row (§7.5). Distances in yd; speeds as the monitor stores them. A missing mean or SD is
// NaN and is printed as an em dash. band.lo/hi equal the mean when the SD is below its floor.
struct ClubDistanceItem {
    QString club;           // canonical bag id
    QString label;          // "7 iron"
    double  carryMean = std::numeric_limits<double>::quiet_NaN();
    double  carrySd   = std::numeric_limits<double>::quiet_NaN();
    QString carryText;      // "161 ± 7 yd"
    ClubDistanceBand band;
    double  totalMean = std::numeric_limits<double>::quiet_NaN();
    double  totalSd   = std::numeric_limits<double>::quiet_NaN();
    double  offlineSd = std::numeric_limits<double>::quiet_NaN();
    double  speedMean = std::numeric_limits<double>::quiet_NaN();
    double  speedSd   = std::numeric_limits<double>::quiet_NaN();
    double  ballSpeedMean = std::numeric_limits<double>::quiet_NaN();
    double  ballSpeedSd   = std::numeric_limits<double>::quiet_NaN();
    double  smashMean = std::numeric_limits<double>::quiet_NaN();
    double  smashSd   = std::numeric_limits<double>::quiet_NaN();
    int     shots    = 0;
    int     sessions = 0;
    ClubMishits mishits;
    bool    fewShots = true;     // under kClubBaselineMinShots: "few shots yet", and a dashed band
    QString trend;               // "up", "down", "tighter", "wider", "steady"; "" = nothing to compare
    QString trendWords;          // "↑ 4 yd", "↓ spread tighter", "→ steady"
    QString gapWith;             // "overlaps 6 iron"; "" = no overlap
    double  gapPrev = std::numeric_limits<double>::quiet_NaN();   // yd to the neighbour above in the bag
    double  gapNext = std::numeric_limits<double>::quiet_NaN();   // yd to the neighbour below
    QString gapPrevClub, gapNextClub;                             // those neighbours' labels
    double  shortBelow = std::numeric_limits<double>::quiet_NaN();    // mean − 1.5 × SD
    std::vector<ClubDistanceSessionPoint> perSession;             // oldest first
};

inline std::vector<LmFieldStats> clubWindowCarryStats(const std::vector<ClubWindowSession> &sessions)
{
    return lmSessionStats(windowLmValues(sessions), -1);
}

// §7.5: the last three sessions against the rest of the window. Both halves need a measured spread, or
// the comparison is not made: a trend needs a standard error, and a thin window has none.
inline void applyClubTrend(ClubDistanceItem &item, const std::vector<ClubWindowSession> &sessions)
{
    if (int(sessions.size()) <= kClubTrendRecentSessions) return;
    const auto split = sessions.end() - kClubTrendRecentSessions;
    const std::vector<LmFieldStats> restStats =
        clubWindowCarryStats(std::vector<ClubWindowSession>(sessions.begin(), split));
    const std::vector<LmFieldStats> recentStats =
        clubWindowCarryStats(std::vector<ClubWindowSession>(split, sessions.end()));
    const LmFieldStats *rest   = clubStat(restStats, "lm.carryDistance");
    const LmFieldStats *recent = clubStat(recentStats, "lm.carryDistance");
    if (!rest || !recent || !rest->hasSpread || !recent->hasSpread) return;

    const double diff = recent->mean - rest->mean;
    const double se   = std::sqrt(recent->sd * recent->sd / double(recent->n) + rest->sd * rest->sd / double(rest->n));
    if (std::fabs(diff) > se && std::fabs(diff) >= kClubTrendMinYards) {
        item.trend      = diff > 0.0 ? QStringLiteral("up") : QStringLiteral("down");
        item.trendWords = (diff > 0.0 ? QStringLiteral("↑ ") : QStringLiteral("↓ ")) + yardsText(std::fabs(diff));
        return;
    }
    const double change = (recent->sd - rest->sd) / rest->sd;
    if (change <= -kClubTrendSpreadChange) {
        item.trend      = QStringLiteral("tighter");
        item.trendWords = QStringLiteral("↓ spread tighter");
    } else if (change >= kClubTrendSpreadChange) {
        item.trend      = QStringLiteral("wider");
        item.trendWords = QStringLiteral("↑ spread wider");
    } else {
        item.trend      = QStringLiteral("steady");
        item.trendWords = QStringLiteral("→ steady");
    }
}

inline ClubDistanceItem clubDistanceItemFromWindow(const ClubDistanceWindow &window)
{
    ClubDistanceItem item;
    item.club  = window.club;
    item.label = clubDistanceLabel(window.club);

    const std::vector<LmFieldStats> stats = lmSessionStats(windowLmValues(window.sessions), -1);
    item.carryMean = clubStatMean(stats, "lm.carryDistance");
    item.carrySd   = clubStatSd(stats, "lm.carryDistance");
    item.carryText = yardsMeanSdText(item.carryMean, item.carrySd);
    item.band.mean = item.carryMean;
    item.band.lo   = std::isfinite(item.carrySd) ? item.carryMean - item.carrySd : item.carryMean;
    item.band.hi   = std::isfinite(item.carrySd) ? item.carryMean + item.carrySd : item.carryMean;
    item.totalMean     = clubStatMean(stats, "lm.totalDistance");
    item.totalSd       = clubStatSd(stats, "lm.totalDistance");
    item.offlineSd     = clubStatSd(stats, "lm.offline");
    item.speedMean     = clubStatMean(stats, "lm.clubheadSpeed");
    item.speedSd       = clubStatSd(stats, "lm.clubheadSpeed");
    item.ballSpeedMean = clubStatMean(stats, "lm.ballSpeed");
    item.ballSpeedSd   = clubStatSd(stats, "lm.ballSpeed");
    item.smashMean     = clubStatMean(stats, "lm.smashFactor");
    item.smashSd       = clubStatSd(stats, "lm.smashFactor");
    if (const LmFieldStats *carry = clubStat(stats, "lm.carryDistance")) item.shots = carry->n;
    item.sessions = int(window.sessions.size());
    item.mishits  = window.mishits;
    item.fewShots = item.shots < kClubBaselineMinShots;
    if (std::isfinite(item.carrySd)) item.shortBelow = item.carryMean - kClubShortBelowSds * item.carrySd;

    for (const ClubWindowSession &s : window.sessions) {
        const std::vector<LmFieldStats> own = lmSessionStats(windowLmValues({ s }), -1);
        ClubDistanceSessionPoint p;
        p.date = s.day.toString(Qt::ISODate);
        p.mean = clubStatMean(own, "lm.carryDistance");
        p.sd   = clubStatSd(own, "lm.carryDistance");
        if (const LmFieldStats *carry = clubStat(own, "lm.carryDistance")) p.n = carry->n;
        item.perSession.push_back(std::move(p));
    }
    applyClubTrend(item, window.sessions);
    return item;
}

// Neighbours are adjacent rows in bag order. Bands are compared only when both have a measured SD.
inline void applyClubGaps(std::vector<ClubDistanceItem> &items)
{
    for (size_t i = 0; i + 1 < items.size(); ++i) {
        ClubDistanceItem &a = items[i];
        ClubDistanceItem &b = items[i + 1];
        const double gap = std::fabs(a.carryMean - b.carryMean);
        a.gapNext = gap;
        a.gapNextClub = b.label;
        b.gapPrev = gap;
        b.gapPrevClub = a.label;
        if (!std::isfinite(a.carrySd) || !std::isfinite(b.carrySd)) continue;
        if (a.band.lo <= b.band.hi && b.band.lo <= a.band.hi) {
            const auto addOverlap = [](ClubDistanceItem &row, const QString &other) {
                row.gapWith = row.gapWith.isEmpty() ? QStringLiteral("overlaps ") + other
                                                    : row.gapWith + QStringLiteral(" and ") + other;
            };
            addOverlap(a, b.label);
            addOverlap(b, a.label);
        }
    }
}

// One row per club with at least one qualifying shot, in `bagOrder` (the canonical order when empty,
// and any club not in it follows, alphabetically). A club with no carry reported has no row.
inline std::vector<ClubDistanceItem> reduceClubDistances(const std::vector<ClubDistanceSession> &sessions,
                                                         const QStringList &bagOrder = QStringList(),
                                                         const FlightNorms &norms = shippedFlightNorms())
{
    const QHash<QString, std::vector<ClubSessionPool>> pools = poolsByClub(sessionsByDay(sessions), norms);

    std::vector<ClubDistanceItem> items;
    for (auto it = pools.constBegin(); it != pools.constEnd(); ++it) {
        const ClubDistanceWindow window = clubWindow(it.key(), it.value());
        if (window.shotCount > 0) items.push_back(clubDistanceItemFromWindow(window));
    }

    const QStringList &order = bagOrder.isEmpty() ? clubVocabulary() : bagOrder;
    const auto rankOf = [&order](const QString &club) {
        const int i = int(order.indexOf(club));
        return i < 0 ? int(order.size()) : i;
    };
    std::sort(items.begin(), items.end(), [&rankOf](const ClubDistanceItem &a, const ClubDistanceItem &b) {
        const int ra = rankOf(a.club), rb = rankOf(b.club);
        return ra != rb ? ra < rb : a.club < b.club;
    });
    applyClubGaps(items);
    return items;
}

} // namespace pinpoint::analysis
