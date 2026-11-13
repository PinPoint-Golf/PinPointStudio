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

// A bad strike read from the flight, for a monitor that doesn't report where the ball met the face
// (shot_outcomes_design.md §3.7). A shank has a signature of its own: a ball that leaves low and sharp
// right. Fat, thin, top and sky show in the launch, the attack angle and the smash.
//
// Everything is judged against this club's median in this session rather than the book's window for the
// club. Club labels are often wrong (a wedge filed as a 7 iron), and the session's own shots are right
// about what that club does however it was labelled. Only a bad strike shows up this way: a contact
// 10 mm low on the face usually flies normally, so "nothing inferred" does not mean "struck well".
//
// Measured always wins over inferred: a label is inferred only where the row that would have measured it
// is not assessable. The other way round, a measured thin, top or fat must agree with the flight before it
// is a miss (measuredStrikeFlightAgrees), and Sky is the flight's to call (flightSaysSky). Both are applied
// ONCE, to the ledger rows, when the session's ledger is built (checkStrikesAgainstFlight, at the end of this
// file; Mark, 10 Nov 2026): a row the flight overrules is NotAssessable, keeps its value and is flagged
// `overruled`, so every surface that counts rows, labels, mishits or work-ons reads the same checked verdict.
//
// How high a ball flew (flight_norms.json, classifyFlight) lives here too, beside the strike rules, so the
// usual carry's mishits (club_distances.h), the composer (shot_outcomes.h), the three factors
// (shot_factors.h) and the ledger check all read one Sky.

#include "diagnostic_ledger.h"
#include "../Core/club_vocabulary.h"
#include "../LaunchMonitor/lm_flight_path.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int    kFlightUsualMinShots    = 4;     // of this club in this session, per reading
inline constexpr double kFlightLowLaunchShare   = 0.50;  // launch under half of usual
inline constexpr double kFlightHighLaunchShare  = 1.50;  // launch at least 1.5 × usual: no strike is read (Sky is the flight's)
inline constexpr double kFlightShankStartDeg    = 8.0;   // starts this far to the golfer's hosel side
inline constexpr double kFlightTopSmashShare    = 0.85;  // smash at least 15 % below usual
inline constexpr double kFlightFatSmashShare    = 0.91;  // smash at least 9 % below usual
inline constexpr double kFlightFatSteeperDeg    = 1.5;   // attack this much steeper than usual
inline constexpr double kFlightShortCarryShare  = 0.70;  // carry under this share of usual is a mishit

// A MEASURED strike height has to agree with the flight before it is a miss (shot_outcomes_design.md
// §3.7, Mark, 10 Nov 2026). The height comes from the club's marker stickers, and a sticker placed a few
// millimetres off shifts every reading of the session: on 4 Aug 2026 every 7-iron strike read low (median
// about −12 mm) on a normal flight, so 10 of 18 read Thin. A real thin launches far lower (2 Nov, shot 5:
// 1.7° against a usual 11°); a real fat loses its smash or its carry. Fat reuses kFlightFatSmashShare.
inline constexpr double kFlightThinLaunchShare  = 0.80;  // a measured thin or top launches at most 0.8 × usual
inline constexpr double kFlightFatCarryShare    = 0.85;  // or a measured fat carries at most 0.85 × usual

// What this club does in this session. A reading with too few shots stays NaN.
struct ClubFlightUsual {
    double launch = std::numeric_limits<double>::quiet_NaN();
    double smash  = std::numeric_limits<double>::quiet_NaN();
    double attack = std::numeric_limits<double>::quiet_NaN();
    double carry  = std::numeric_limits<double>::quiet_NaN();
};

inline double flightMedian(std::vector<double> values)
{
    if (int(values.size()) < kFlightUsualMinShots) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
}

// Each club's usual flight in this session, keyed by canonical club. Part swings fly differently and are
// left out.
inline QHash<QString, ClubFlightUsual> sessionFlightUsuals(const std::vector<ShotRecord> &shots)
{
    struct Pool { std::vector<double> launch, smash, attack, carry; };
    QHash<QString, Pool> pools;
    for (const ShotRecord &recorded : shots) {
        ShotRecord scratch;
        const ShotRecord &s = gradedReadings(recorded, scratch);   // a held reading never sets a usual (#176)
        if (s.swingLength == QLatin1String("partial")) continue;
        const QString club = canonicalClubId(s.club);
        if (club.isEmpty()) continue;
        Pool &p = pools[club];
        if (std::isfinite(s.launchAngle)) p.launch.push_back(s.launchAngle);
        if (std::isfinite(s.smash))       p.smash.push_back(s.smash);
        if (std::isfinite(s.attackAngle)) p.attack.push_back(s.attackAngle);
        if (std::isfinite(s.carry))       p.carry.push_back(s.carry);
    }
    QHash<QString, ClubFlightUsual> out;
    for (auto it = pools.constBegin(); it != pools.constEnd(); ++it)
        out.insert(it.key(), ClubFlightUsual{ flightMedian(it->launch), flightMedian(it->smash),
                                              flightMedian(it->attack), flightMedian(it->carry) });
    return out;
}

// True when the row exists and couldn't be assessed: the monitor didn't report what it reads. A row the flight
// OVERRULED (checkStrikesAgainstFlight) was read, so it is not unread: a sticker-offset thin the flight
// contradicts never turns into a thin read from the flight, nor into a "far short" for want of a height.
inline bool strikeRowUnread(const ShotRecord &s, const char *conditionId)
{
    const ConditionRow *r = rowFor(s, QLatin1String(conditionId));
    return r && r->state == ShotState::NotAssessable && !r->overruled;
}

// Neither fat nor thin could be measured: the monitor gave no strike height.
inline bool strikeHeightUnread(const ShotRecord &s)
{
    return strikeRowUnread(s, "chunk") && strikeRowUnread(s, "thin");
}

// The strike the flight points to ("shank", "thin", "top", "chunk"), or "" for none. A reading that's missing
// rules out only the labels that need it. A high launch is no strike: Sky is a flight result, the flight
// rule's alone (flightSaysSky), so this never names it.
inline QString strikeFromFlight(const ShotRecord &recorded, const ClubFlightUsual &usual)
{
    ShotRecord scratch;
    const ShotRecord &s = gradedReadings(recorded, scratch);       // only live readings judge (#176)
    if (s.swingLength == QLatin1String("partial")) return QString();
    if (!std::isfinite(s.launchAngle) || !std::isfinite(usual.launch) || !(usual.launch > 0.0)) return QString();

    const bool heightUnread = strikeHeightUnread(s);
    const bool haveSmash    = std::isfinite(s.smash) && std::isfinite(usual.smash) && usual.smash > 0.0;
    const bool haveAttack   = std::isfinite(s.attackAngle) && std::isfinite(usual.attack);

    if (s.launchAngle < kFlightLowLaunchShare * usual.launch) {
        // Measures are never mirrored, so the hosel side is the other way for a left-hander.
        const double towardHosel = s.handedness == QLatin1String("Left") ? -s.startDirection : s.startDirection;
        if (strikeRowUnread(s, "shank") && std::isfinite(towardHosel) && towardHosel >= kFlightShankStartDeg)
            return QStringLiteral("shank");
        if (!heightUnread) return QString();
        return haveSmash && s.smash <= kFlightTopSmashShare * usual.smash ? QStringLiteral("top") : QStringLiteral("thin");
    }

    if (!heightUnread || !haveSmash || !haveAttack) return QString();

    if (s.launchAngle >= kFlightHighLaunchShare * usual.launch) return QString();

    if (s.smash <= kFlightFatSmashShare * usual.smash && s.attackAngle <= usual.attack - kFlightFatSteeperDeg)
        return QStringLiteral("chunk");
    return QString();
}

// The strike the flight points to for this shot in its session, "" when the club has no usual yet.
inline QString strikeFromFlight(const ShotRecord &s, const QHash<QString, ClubFlightUsual> &usuals)
{
    const auto it = usuals.constFind(canonicalClubId(s.club));
    return it == usuals.constEnd() ? QString() : strikeFromFlight(s, *it);
}

// False only when the flight says a MEASURED thin, top or fat ("thin", "top", "chunk") didn't happen: a
// thin or top that launched above kFlightThinLaunchShare of usual, a fat whose smash and carry both held
// (every one of the two that can be compared). With nothing to compare against (a part swing, the
// reading missing, or the club with too few full shots this session for a usual) there is no evidence
// against it, so it stands. Any other condition is not this rule's to judge. Strike height and launch
// are never mirrored, so a left-hander is judged the same way.
inline bool measuredStrikeFlightAgrees(const ShotRecord &recorded, const QString &conditionId, const ClubFlightUsual &usual)
{
    ShotRecord scratch;
    const ShotRecord &s = gradedReadings(recorded, scratch);       // only live readings overrule (#176)
    if (s.swingLength == QLatin1String("partial")) return true;
    if (conditionId == QLatin1String("thin") || conditionId == QLatin1String("top")) {
        if (!std::isfinite(s.launchAngle) || !std::isfinite(usual.launch) || !(usual.launch > 0.0)) return true;
        return s.launchAngle <= kFlightThinLaunchShare * usual.launch;
    }
    if (conditionId == QLatin1String("chunk")) {
        const bool haveSmash = std::isfinite(s.smash) && std::isfinite(usual.smash) && usual.smash > 0.0;
        const bool haveCarry = std::isfinite(s.carry) && std::isfinite(usual.carry) && usual.carry > 0.0;
        if (!haveSmash && !haveCarry) return true;
        return (haveSmash && s.smash <= kFlightFatSmashShare * usual.smash)
            || (haveCarry && s.carry <= kFlightFatCarryShare * usual.carry);
    }
    return true;
}

// The same, against the club's usual in this session; a club with no usual yet has no evidence against it.
inline bool measuredStrikeFlightAgrees(const ShotRecord &s, const QString &conditionId,
                                       const QHash<QString, ClubFlightUsual> &usuals)
{
    const auto it = usuals.constFind(canonicalClubId(s.club));
    return it == usuals.constEnd() || measuredStrikeFlightAgrees(s, conditionId, *it);
}


// ── How high it flew (flight_norms.json, docs/design/ball-flight-classification.md) ─────────────────
//
// Height has no official definition and is relative to ball speed and club, so it is judged on
// speed-normalised readings, the first one the shot has (spec §5):
//   1. LANDING angle (m_lmDescentAngle, degrees) against the club category's band;
//   2. APEX / CARRY, apex and carry in the same units (the monitor's apex is in FEET, the carry in
//      yards), against the category's band; full swings only;
//   3. APEX against a speed-matched reference apex for the shot's ball speed, as a ratio, against one
//      band ([0.85, 1.15]); only when the carry isn't known, and only for a category with a reference
//      (the driver);
//   else not read. Low below the band's L, High above its H, Mid between (inclusive). Which method decided
//   and the reading it used are kept with the value (spec §5: a shot can be reclassified when the
//   thresholds move).
//
// MEASURED ALWAYS WINS OVER MODELLED. Where the monitor reported no landing angle or apex (GSPro often
// doesn't; an FSX export usually does), the flight model behind the launch monitor panel's drawing
// (lmIntegrateFlight, lm_flight_path.h) gives them from the ball speed, launch and spin. The order is the
// measured landing angle, the measured apex over the carry, then the modelled landing angle ("landing
// (modelled)"), then the measured apex against the driver's reference. With no spin or no launch there is
// no model, and nothing modelled is ever claimed.
//
// { "schema": 2, "categories": { "driver": { "label": "Driver", "clubs": ["DRIVER"] }, ... },
//   "methods": [ { "id": "landing",    "bands": { "driver": [33, 41], ... } },
//                { "id": "apex_carry", "bands": { "driver": [0.10, 0.14], ... } },
//                { "id": "apex", "band": [0.85, 1.15],
//                  "reference": { "driver": { "points": [ { "ballSpeedMph": 140, "apexYd": 25 }, ... ] } } } ],
//   "sources": [...], "notes": [...] }

inline const QString &flightMethodLanding()   { static const QString k = QStringLiteral("landing");    return k; }
inline const QString &flightMethodApexCarry() { static const QString k = QStringLiteral("apex_carry"); return k; }
inline const QString &flightMethodApex()      { static const QString k = QStringLiteral("apex");       return k; }
inline const QString &flightMethodLandingModelled() { static const QString k = QStringLiteral("landing (modelled)"); return k; }

inline constexpr double kFeetPerYard = 3.0;

// A band [lo, hi]: Low under lo, High over hi, Mid otherwise.
struct FlightBand {
    double lo = std::numeric_limits<double>::quiet_NaN();
    double hi = std::numeric_limits<double>::quiet_NaN();
    bool valid() const { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi; }
};

struct FlightApexPoint {
    double ballSpeedMph = std::numeric_limits<double>::quiet_NaN();
    double apexYd       = std::numeric_limits<double>::quiet_NaN();
};

struct FlightNorms {
    int schema = 0;
    QHash<QString, QString>    categoryOf;      // canonical club -> category ("7 IRON" -> "mid_irons")
    QHash<QString, QString>    categoryLabel;   // category -> "Mid irons"
    QHash<QString, FlightBand> landing;         // by category, degrees
    QHash<QString, FlightBand> apexCarry;       // by category, a ratio
    FlightBand                 apexBand;        // apex over the reference apex
    QHash<QString, std::vector<FlightApexPoint>> apexReference;  // by category, ball speed ascending
    QJsonArray  sources;
    QStringList notes;
    bool isEmpty() const { return categoryOf.isEmpty(); }
};

inline FlightBand flightBandFromJson(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    FlightBand b;
    if (a.size() == 2 && a.at(0).isDouble() && a.at(1).isDouble()) {
        b.lo = a.at(0).toDouble();
        b.hi = a.at(1).toDouble();
    }
    return b;
}

inline FlightNorms flightNormsFromJson(const QJsonObject &root)
{
    FlightNorms n;
    n.schema  = root.value(QStringLiteral("schema")).toInt();
    n.sources = root.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &v : root.value(QStringLiteral("notes")).toArray())
        if (v.isString()) n.notes << v.toString();

    const QJsonObject categories = root.value(QStringLiteral("categories")).toObject();
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
        const QJsonObject c = it.value().toObject();
        n.categoryLabel.insert(it.key(), c.value(QStringLiteral("label")).toString());
        for (const QJsonValue &club : c.value(QStringLiteral("clubs")).toArray()) {
            const QString id = canonicalClubId(club.toString());
            if (!id.isEmpty()) n.categoryOf.insert(id, it.key());
        }
    }

    for (const QJsonValue &mv : root.value(QStringLiteral("methods")).toArray()) {
        const QJsonObject m = mv.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id == flightMethodLanding() || id == flightMethodApexCarry()) {
            QHash<QString, FlightBand> &into = id == flightMethodLanding() ? n.landing : n.apexCarry;
            const QJsonObject bands = m.value(QStringLiteral("bands")).toObject();
            for (auto it = bands.constBegin(); it != bands.constEnd(); ++it) {
                const FlightBand b = flightBandFromJson(it.value());
                if (b.valid()) into.insert(it.key(), b);
            }
        } else if (id == flightMethodApex()) {
            n.apexBand = flightBandFromJson(m.value(QStringLiteral("band")));
            const QJsonObject refs = m.value(QStringLiteral("reference")).toObject();
            for (auto it = refs.constBegin(); it != refs.constEnd(); ++it) {
                std::vector<FlightApexPoint> points;
                for (const QJsonValue &pv : it.value().toObject().value(QStringLiteral("points")).toArray()) {
                    const QJsonObject p = pv.toObject();
                    const FlightApexPoint pt{ p.value(QStringLiteral("ballSpeedMph")).toDouble(std::numeric_limits<double>::quiet_NaN()),
                                              p.value(QStringLiteral("apexYd")).toDouble(std::numeric_limits<double>::quiet_NaN()) };
                    if (std::isfinite(pt.ballSpeedMph) && std::isfinite(pt.apexYd)) points.push_back(pt);
                }
                std::sort(points.begin(), points.end(), [](const FlightApexPoint &a, const FlightApexPoint &b) {
                    return a.ballSpeedMph < b.ballSpeedMph;
                });
                if (!points.empty()) n.apexReference.insert(it.key(), std::move(points));
            }
        }
    }
    return n;
}

// The club's category, "" for a club in none (a putter, a club the table doesn't list).
inline QString flightClubCategory(const FlightNorms &norms, const QString &club)
{
    return norms.categoryOf.value(canonicalClubId(club));
}

// "low", "mid" or "high" for a reading against a band.
inline QString flightBandValue(double value, const FlightBand &band)
{
    if (value < band.lo) return QStringLiteral("low");
    if (value > band.hi) return QStringLiteral("high");
    return QStringLiteral("mid");
}

// The reference apex (yd) for a ball speed: linear through the category's points, the line through the
// nearest two carried on outside them (the spec's "extrapolate cautiously": nothing better is published),
// the one point's apex when there is only one. NaN for a category with no reference, or a reference that
// would come out at no height.
inline double flightReferenceApexYd(const FlightNorms &norms, const QString &category, double ballSpeedMph)
{
    const auto it = norms.apexReference.constFind(category);
    if (it == norms.apexReference.constEnd() || it->empty() || !std::isfinite(ballSpeedMph))
        return std::numeric_limits<double>::quiet_NaN();
    const std::vector<FlightApexPoint> &p = *it;
    if (p.size() == 1) return p.front().apexYd;
    size_t i = 1;
    while (i + 1 < p.size() && ballSpeedMph > p[i].ballSpeedMph) ++i;
    const FlightApexPoint &a = p[i - 1], &b = p[i];
    const double span = b.ballSpeedMph - a.ballSpeedMph;
    const double apex = span > 0.0 ? a.apexYd + (ballSpeedMph - a.ballSpeedMph) * (b.apexYd - a.apexYd) / span : a.apexYd;
    return apex > 0.0 ? apex : std::numeric_limits<double>::quiet_NaN();
}

// How high one shot flew, and how that was decided.
struct FlightReading {
    QString value;              // "low", "mid", "high"
    QString method;             // "landing", "apex_carry", "landing (modelled)", "apex"
    bool    modelled = false;   // read off the flight model, not the monitor
    double  rawValue = std::numeric_limits<double>::quiet_NaN();   // degrees, apex/carry, or apex/reference
    QString category;           // "driver", "mid_irons", ...
};

// The landing angle the flight model gives for the shot's launch, NaN when the shot lacks what it needs
// (the ball speed, the launch angle, the spin): no launch, no flight, and no invented one.
inline double modelledDescentAngle(const ShotRecord &shot)
{
    if (!std::isfinite(shot.ballSpeed) || !std::isfinite(shot.launchAngle) || !std::isfinite(shot.spinRate))
        return std::numeric_limits<double>::quiet_NaN();
    LmLaunch in;
    in.ballSpeedMph   = shot.ballSpeed;
    in.launchAngleDeg = shot.launchAngle;
    in.startDirDeg    = std::isfinite(shot.startDirection) ? shot.startDirection : 0.0;
    in.spinRpm        = shot.spinRate;
    in.spinAxisDeg    = std::isfinite(shot.spinAxis) ? shot.spinAxis : 0.0;
    const LmFlightIntegration f = lmIntegrateFlight(in);
    return f.has && std::isfinite(f.descentDeg) && f.descentDeg > 0.0 ? f.descentDeg : std::numeric_limits<double>::quiet_NaN();
}

// The first method the shot can use (the file comment above), or none: no reading, a club in no
// category, or nothing the shot has that a method reads.
inline std::optional<FlightReading> classifyFlight(const ShotRecord &recorded, const FlightNorms &norms)
{
    ShotRecord scratch;
    const ShotRecord &shot = gradedReadings(recorded, scratch);    // a flight band is a verdict (#176)
    FlightReading r;
    r.category = flightClubCategory(norms, shot.club);
    if (r.category.isEmpty()) return std::nullopt;
    const auto landing = norms.landing.constFind(r.category);
    const bool haveLanding = landing != norms.landing.constEnd();

    if (std::isfinite(shot.descentAngle) && haveLanding) {
        r.method = flightMethodLanding();
        r.rawValue = shot.descentAngle;
        r.value = flightBandValue(r.rawValue, *landing);
        return r;
    }
    const double apexYd = std::isfinite(shot.peakHeight) ? shot.peakHeight / kFeetPerYard
                                                         : std::numeric_limits<double>::quiet_NaN();
    // A part swing's apex over its carry says how much of a swing it was, not how it flew.
    if (std::isfinite(apexYd) && std::isfinite(shot.carry) && shot.carry > 0.0 && shot.swingLength != QLatin1String("partial")) {
        if (const auto it = norms.apexCarry.constFind(r.category); it != norms.apexCarry.constEnd()) {
            r.method = flightMethodApexCarry();
            r.rawValue = apexYd / shot.carry;
            r.value = flightBandValue(r.rawValue, *it);
            return r;
        }
    }
    if (haveLanding) {
        const double modelled = modelledDescentAngle(shot);
        if (std::isfinite(modelled)) {
            r.method = flightMethodLandingModelled();
            r.modelled = true;
            r.rawValue = modelled;
            r.value = flightBandValue(r.rawValue, *landing);
            return r;
        }
    }
    // Absolute apex is the last resort: only when the carry isn't known at all.
    if (std::isfinite(apexYd) && !std::isfinite(shot.carry) && norms.apexBand.valid()) {
        const double reference = flightReferenceApexYd(norms, r.category, shot.ballSpeed);
        if (std::isfinite(reference)) {
            r.method = flightMethodApex();
            r.rawValue = apexYd / reference;
            r.value = flightBandValue(r.rawValue, norms.apexBand);
            return r;
        }
    }
    return std::nullopt;
}

// Where the shipped table is read from: the file PINPOINT_CORE_FLIGHT_NORMS names (tests, tooling), else
// the resource beside norms.json.
inline QString flightNormsPath()
{
    const QByteArray override = qgetenv("PINPOINT_CORE_FLIGHT_NORMS");
    return override.isEmpty() ? QStringLiteral(":/diagnostics/flight_norms.json") : QString::fromLocal8Bit(override);
}

// The table at `path`; empty (nothing is read from the flight) when it can't be read, with `*error` saying why.
inline FlightNorms loadFlightNorms(const QString &path, QString *error = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = err.errorString();
        return {};
    }
    if (error) error->clear();
    return flightNormsFromJson(doc.object());
}

// The shipped table, read once.
inline const FlightNorms &shippedFlightNorms()
{
    static const FlightNorms k = loadFlightNorms(flightNormsPath());
    return k;
}

inline constexpr double kSkySteeperAttackDeg = 2.0;    // attack at least this much steeper than the usual
inline constexpr double kSkyCarryShare       = 0.85;   // carry at most this share of the usual

// The clubs a ball can be skied with: off a tee or a high-lofted wood. Never an iron or a wedge.
inline bool isSkyClub(const QString &clubId)
{
    const QString c = canonicalClubId(clubId);
    return c.contains(QLatin1String("DRIVER")) || c.contains(QLatin1String("WOOD")) || c.contains(QLatin1String("HYBRID"));
}

// A High flight skied (by whichever method): a driver, wood or hybrid, an attack at least kSkySteeperAttackDeg steeper (more
// negative) than the golfer's usual for the club this session, and a carry at most kSkyCarryShare of the
// usual. Every reading has to be there; with one missing it stays High. `usual` is the shot's own club's.
inline bool flightSkied(const ShotRecord &recorded, const ClubFlightUsual &usual)
{
    ShotRecord scratch;
    const ShotRecord &shot = gradedReadings(recorded, scratch);    // only live readings call a sky (#176)
    if (!isSkyClub(shot.club)) return false;
    if (!std::isfinite(shot.attackAngle) || !std::isfinite(usual.attack)) return false;
    if (!std::isfinite(shot.carry) || !std::isfinite(usual.carry) || !(usual.carry > 0.0)) return false;
    return shot.attackAngle <= usual.attack - kSkySteeperAttackDeg && shot.carry <= kSkyCarryShare * usual.carry;
}

// The same, against the session's usuals; a club with no usual yet never skies.
inline bool flightSkied(const ShotRecord &shot, const QHash<QString, ClubFlightUsual> &usuals)
{
    const auto it = usuals.constFind(canonicalClubId(shot.club));
    return it != usuals.constEnd() && flightSkied(shot, *it);
}

// THE Sky (shot_outcomes_design.md §3.8): the flight read High (classifyFlight) and skied (flightSkied). The
// three factors call a shot's flight Sky by exactly this (shotFactors), the ledger keeps a measured sky row
// only where it holds (checkStrikesAgainstFlight), and the usual carry leaves such a shot out (shotMishitKind).
inline bool flightSaysSky(const ShotRecord &shot, const ClubFlightUsual &usual, const FlightNorms &norms)
{
    if (!flightSkied(shot, usual)) return false;        // the cheap half first: the model below integrates a flight
    const std::optional<FlightReading> reading = classifyFlight(shot, norms);
    return reading && reading->value == QLatin1String("high");
}

inline bool flightSaysSky(const ShotRecord &shot, const QHash<QString, ClubFlightUsual> &usuals, const FlightNorms &norms)
{
    const auto it = usuals.constFind(canonicalClubId(shot.club));
    return it != usuals.constEnd() && flightSaysSky(shot, *it, norms);
}



// ── The ledger, checked against the flight (Mark, 10 Nov 2026) ───────────────────────────────────────
//
// Every shot stays in the ledger, bad ones too: they are the diagnostic gold. But a bad READING is not a bad
// SHOT. This is the ONE place the flight overrules a measured strike, applied by SessionDiagnosticsModel each
// time the session's rows change (a shot arrives, is regraded or leaves, the file is read), so diagnostics.json
// holds the checked rows and every reader — the pattern cards, This shot, the composer's labels, the mishits
// that leave the usual carry, work_ons.json, the report, Charts, the tuning log — counts the same verdict:
//
//   · a FIRED measured thin, top or chunk the flight contradicts (measuredStrikeFlightAgrees false) becomes
//     NotAssessable, "the strike height disagrees with the ball flight";
//   · a FIRED sky row stands only where the flight rule calls the shot Sky (flightSaysSky: a driver, wood or
//     hybrid flying High, the attack steeper and the carry short); otherwise NotAssessable, "Sky is judged
//     from the ball flight". No sky row is ever made where none was detected.
//
// The row keeps its value, corridor and evidence, and is flagged `overruled`, so it is never read as "the
// monitor sent no strike height" (strikeRowUnread) and the composer can still say "Low on the face (−12 mm),
// flight normal". With nothing to compare a strike against (fewer than kFlightUsualMinShots full shots of the
// club in the session, a part swing, a reading missing) it stands as detected.
//
// Idempotent and convergent: what an earlier pass overruled is put back as detected first, then every row is
// judged against the session's usuals over ALL its shots as they are now. A live session therefore settles as
// shots arrive: the first three 7 irons' thins stand, and the fourth gives the club a usual that can overrule
// all four. Returns how many rows are overruled now.

inline const QString &strikeOverruledReason()
{
    static const QString k = QStringLiteral("the strike height disagrees with the ball flight");
    return k;
}

inline const QString &skyOverruledReason()
{
    static const QString k = QStringLiteral("Sky is judged from the ball flight");
    return k;
}

inline int checkStrikesAgainstFlight(std::vector<ShotRecord> &shots, const FlightNorms &norms)
{
    for (ShotRecord &s : shots)
        for (ConditionRow &r : s.rows)
            if (r.overruled) {
                r.state = ShotState::Fired;     // only a fired row is ever overruled, and a fired row has no reason
                r.notAssessableReason.clear();
                r.overruled = false;
            }

    const QHash<QString, ClubFlightUsual> usuals = sessionFlightUsuals(shots);
    int overruled = 0;
    for (ShotRecord &s : shots)
        for (ConditionRow &r : s.rows) {
            if (r.state != ShotState::Fired) continue;
            const QString *why = nullptr;
            if (r.conditionId == QLatin1String("thin") || r.conditionId == QLatin1String("top")
                || r.conditionId == QLatin1String("chunk")) {
                if (!measuredStrikeFlightAgrees(s, r.conditionId, usuals)) why = &strikeOverruledReason();
            } else if (r.conditionId == QLatin1String("sky")) {
                if (!flightSaysSky(s, usuals, norms)) why = &skyOverruledReason();
            }
            if (!why) continue;
            r.state = ShotState::NotAssessable;
            r.notAssessableReason = *why;
            r.overruled = true;
            ++overruled;
        }
    return overruled;
}

} // namespace pinpoint::analysis
