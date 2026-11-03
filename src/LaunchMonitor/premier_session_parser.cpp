/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "premier_session_parser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace pinpoint::lm {
namespace {

// Spelled out rather than M_PI, for the reason gcquad_csv_parser.cpp gives (MSVC test targets).
constexpr double kPi = 3.14159265358979323846;
constexpr double kMpsToMph = 2.2369362920544;

// One JSON field → one reading member. The bounds are the CSV parser's: sentinel filters, not
// validators, set several times beyond anything golf produces, in the CATALOGUE unit. Premier
// writes JSON null rather than an in-band marker, so these only ever catch a corrupt value.
struct FieldMap {
    const char *json;
    double scale;           // JSON value × scale = catalogue value
    double lo, hi;
    const char *unit;       // catalogue unit as a reader sees it, for a drop's evidence
    std::optional<double> LaunchMonitorReading::*member;
};

const std::vector<FieldMap> &ballFields()
{
    using R = LaunchMonitorReading;
    static const std::vector<FieldMap> f = {
        { "BallSpeedMs",  kMpsToMph,     0.0,   400.0, "mph", &R::ballSpeed       },
        { "ElevationDeg", 1.0,         -90.0,    90.0, "°",   &R::launchAngle     },
        { "AzimuthDeg",   1.0,         -90.0,    90.0, "°",   &R::launchDirection },
        { "BackSpinRpm",  1.0,      -30000.0, 30000.0, "rpm", &R::backSpin        },
        { "SideSpinRpm",  1.0,      -30000.0, 30000.0, "rpm", &R::sideSpin        },
        { "TotalSpinRpm", 1.0,           0.0, 30000.0, "rpm", &R::spinRate        },
    };
    return f;
}

const std::vector<FieldMap> &clubFields()
{
    using R = LaunchMonitorReading;
    static const std::vector<FieldMap> f = {
        { "ClubSpeedMs",            kMpsToMph,     0.0,   300.0, "mph",   &R::clubheadSpeed  },
        { "Efficiency",             1.0,           0.0,     5.0, "ratio", &R::smashFactor    },
        { "AngleOfAttackDeg",       1.0,         -90.0,    90.0, "°",     &R::attackAngle    },
        { "ClubPathDeg",            1.0,         -90.0,    90.0, "°",     &R::clubPath       },
        { "FaceToTargetDeg",        1.0,         -90.0,    90.0, "°",     &R::faceAngle      },
        { "LieDeg",                 1.0,         -90.0,    90.0, "°",     &R::lieAngle       },
        { "LoftDeg",                1.0,         -90.0,    90.0, "°",     &R::dynamicLoft    },
        // A ball quantity, though Premier files it under ClubData.
        { "FAxisDeg",               1.0,         -90.0,    90.0, "°",     &R::spinAxis       },
        // Heel-positive in Premier, toe-positive in PinPoint (§2.6.5): negated.
        { "FaceImpactHorizontalMm", -1.0,       -500.0,   500.0, "mm",    &R::strikeLocation },
        { "FaceImpactVerticalMm",   1.0,        -500.0,   500.0, "mm",    &R::strikeHeight   },
        { "ClosureRateDegs",        1.0,      -50000.0, 50000.0, "°/s",   &R::closureRate    },
        // ClubSpeedAtImpactMs is not mapped: within ±2 % of ClubSpeedMs, meaning unconfirmed.
    };
    return f;
}

QString fieldKeyFor(std::optional<double> LaunchMonitorReading::*member)
{
    for (const FieldDef &f : fieldDefs())
        if (f.member == member)
            return QString::fromLatin1(f.key);
    return {};
}

bool hasDrop(const LaunchMonitorReading &r, const QString &key)
{
    for (const DroppedField &d : r.dropped)
        if (d.key == key)
            return true;
    return false;
}

// Reads one mapped field. Absent or null → a noData drop when `dropMissing`; a finite value
// outside the bounds → an outOfRange drop; otherwise the member is set.
void readField(const QJsonObject &o, const FieldMap &m, LaunchMonitorReading &r, bool dropMissing)
{
    const QJsonValue v = o.value(QLatin1String(m.json));
    const QString key = fieldKeyFor(m.member);
    if (!v.isDouble()) {
        if (dropMissing && !key.isEmpty() && !hasDrop(r, key)) {
            DroppedField d;
            d.key  = key;
            d.code = QStringLiteral("noData");
            d.unit = QString::fromUtf8(m.unit);
            r.dropped.push_back(std::move(d));
        }
        return;
    }
    const double raw = v.toDouble();
    const double val = raw * m.scale;
    if (!std::isfinite(val) || val < m.lo || val > m.hi) {
        if (!key.isEmpty() && !hasDrop(r, key)) {
            DroppedField d;
            d.key   = key;
            d.code  = QStringLiteral("outOfRange");
            d.raw   = raw;
            if (std::isfinite(val))
                d.value = val;
            d.lo    = m.lo;
            d.hi    = m.hi;
            d.unit  = QString::fromUtf8(m.unit);
            r.dropped.push_back(std::move(d));
        }
        return;
    }
    r.*(m.member) = val;
}

// The values Premier states implicitly, derived exactly as the CSV parser derives them; every
// input is the device's, so the results carry the lm. prefix too.
void derive(LaunchMonitorReading &r)
{
    // Face to path: Premier does not write it. Face minus path, both right-positive.
    if (!r.faceToPath && r.faceAngle && r.clubPath)
        r.faceToPath = *r.faceAngle - *r.clubPath;
    // Spin axis: FAxisDeg when Premier gave it (it rides in ClubData), else from the spins.
    if (!r.spinAxis && r.sideSpin && r.backSpin && (*r.sideSpin != 0.0 || *r.backSpin != 0.0))
        r.spinAxis = std::atan2(*r.sideSpin, *r.backSpin) * 180.0 / kPi;
    if (!r.smashFactor && r.ballSpeed && r.clubheadSpeed && *r.clubheadSpeed > 0.0)
        r.smashFactor = *r.ballSpeed / *r.clubheadSpeed;
    if (!r.spinLoft && r.dynamicLoft && r.attackAngle)
        r.spinLoft = *r.dynamicLoft - *r.attackAngle;

    // A field the derivations filled was not dropped after all (Efficiency null, both speeds
    // present): one entry per field, and none for a field that has a value.
    std::vector<DroppedField> kept;
    for (DroppedField &d : r.dropped) {
        bool filled = false;
        for (const FieldDef &f : fieldDefs())
            if (d.key == QLatin1String(f.key) && (r.*(f.member)).has_value()) { filled = true; break; }
        if (!filled)
            kept.push_back(std::move(d));
    }
    r.dropped = std::move(kept);
}

qint64 parseUtcMs(const QJsonValue &v)
{
    if (!v.isString())
        return 0;
    // "2026-11-02T15:29:45.9223816Z": seven fractional digits, more than Qt::ISODateWithMs
    // takes. Trim the fraction to milliseconds; the rest is ordinary ISO 8601.
    QString s = v.toString();
    const int dot = s.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        int end = dot + 1;
        while (end < s.size() && s.at(end).isDigit())
            ++end;
        QString frac = s.mid(dot + 1, end - dot - 1).left(3);
        while (frac.size() < 3)
            frac += QLatin1Char('0');
        s = s.left(dot + 1) + frac + s.mid(end);
    }
    QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!t.isValid())
        return 0;
    return t.toMSecsSinceEpoch();
}

} // namespace

std::optional<PremierFile> parsePremierSessionFile(const QByteArray &bytesIn, QString *error,
                                                   bool *formatChanged)
{
    if (formatChanged)
        *formatChanged = false;
    auto fail = [&](const QString &why, bool changed) -> std::optional<PremierFile> {
        if (error)
            *error = why;
        if (formatChanged)
            *formatChanged = changed;
        return std::nullopt;
    };

    // Premier writes UTF-8 with a BOM, which QJsonDocument does not skip.
    QByteArray bytes = bytesIn;
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    if (bytes.trimmed().isEmpty())
        return fail(QStringLiteral("empty file"), false);

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError)
        return fail(QStringLiteral("not valid JSON: %1").arg(pe.errorString()), false);
    if (!doc.isObject())
        return fail(QStringLiteral("top level is not an object"), true);

    const QJsonObject root = doc.object();
    QJsonArray sessions;
    if (root.contains(QStringLiteral("Sessions"))) {
        if (!root.value(QStringLiteral("Sessions")).isArray())
            return fail(QStringLiteral("Sessions is not an array"), true);
        sessions = root.value(QStringLiteral("Sessions")).toArray();
    } else if (root.contains(QStringLiteral("ShotGroups"))) {
        sessions.append(root);
    } else {
        return fail(QStringLiteral("neither a session nor a session queue"), true);
    }

    PremierFile out;
    for (const QJsonValue &sv : sessions) {
        const QJsonObject s = sv.toObject();
        const QJsonValue groups = s.value(QStringLiteral("ShotGroups"));
        if (!groups.isArray())
            return fail(QStringLiteral("a session has no ShotGroups array"), true);
        ++out.sessionCount;
        const QString sessionId = s.value(QStringLiteral("LocalSessionId")).toString();

        for (const QJsonValue &gv : groups.toArray()) {
            const QJsonValue shots = gv.toObject().value(QStringLiteral("Shots"));
            if (!shots.isArray())
                return fail(QStringLiteral("a shot group has no Shots array"), true);

            for (const QJsonValue &shv : shots.toArray()) {
                const QJsonObject o = shv.toObject();
                PremierShot shot;
                shot.localShotId  = o.value(QStringLiteral("LocalShotId")).toString();
                shot.sessionId    = sessionId;
                shot.createdUtcMs = parseUtcMs(o.value(QStringLiteral("CreatedAtUtc")));
                shot.hidden       = o.value(QStringLiteral("Hidden")).toBool(false);
                if (shot.localShotId.isEmpty())
                    return fail(QStringLiteral("a shot has no LocalShotId"), true);
                if (shot.createdUtcMs == 0)
                    return fail(QStringLiteral("shot %1 has no readable CreatedAtUtc").arg(shot.localShotId), true);
                if (!o.value(QStringLiteral("BallSpeedMs")).isDouble())
                    return fail(QStringLiteral("shot %1 has no BallSpeedMs").arg(shot.localShotId), true);

                LaunchMonitorReading &r = shot.reading;
                r.deviceShotId = shot.localShotId;
                // "4h", "Gw" — recorded only; ClubType ("HYBRID") is coarser and adds nothing.
                r.deviceClub   = o.value(QStringLiteral("ClubName")).toString();
                r.contactAtMs  = shot.createdUtcMs - kPremierCreatedAfterContactMs;

                for (const FieldMap &m : ballFields())
                    readField(o, m, r, /*dropMissing=*/false);

                const QJsonValue cd = o.value(QStringLiteral("ClubData"));
                if (cd.isObject()) {
                    shot.hasClubData = true;
                    const QJsonObject c = cd.toObject();
                    for (const FieldMap &m : clubFields()) {
                        // FAxisDeg is a ball quantity with a fallback (atan2 of the spins), so
                        // its absence is not a drop.
                        const bool dropMissing = m.member != &LaunchMonitorReading::spinAxis;
                        readField(c, m, r, dropMissing);
                    }
                }
                derive(r);
                out.shots.push_back(std::move(shot));
            }
        }
    }
    return out;
}

std::optional<std::vector<PremierHistoryRecord>>
parsePremierHistoryFile(const QByteArray &bytesIn, QString *error, bool *formatChanged)
{
    if (formatChanged)
        *formatChanged = false;
    auto fail = [&](const QString &why, bool changed) -> std::optional<std::vector<PremierHistoryRecord>> {
        if (error)
            *error = why;
        if (formatChanged)
            *formatChanged = changed;
        return std::nullopt;
    };

    // history.json was written without a BOM on 2026-11-02; skipped anyway, as the session files.
    QByteArray bytes = bytesIn;
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    if (bytes.trimmed().isEmpty())
        return fail(QStringLiteral("empty file"), false);

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError)
        return fail(QStringLiteral("not valid JSON: %1").arg(pe.errorString()), false);
    if (!doc.isObject() || !doc.object().value(QStringLiteral("records")).isArray())
        return fail(QStringLiteral("no records array"), true);

    // Sanity bounds on carry, in yards: a sentinel filter, not a validator.
    constexpr double kMetresToYards = 1.0 / 0.9144;
    constexpr double kMaxCarryYd    = 500.0;

    std::vector<PremierHistoryRecord> out;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("records")).toArray()) {
        const QJsonObject o = v.toObject();
        PremierHistoryRecord rec;
        rec.createdUtcMs = parseUtcMs(o.value(QStringLiteral("created")));
        const QJsonValue speed = o.value(QStringLiteral("ballSpeedMPH"));
        if (rec.createdUtcMs == 0 || !speed.isDouble())
            return fail(QStringLiteral("a record has no readable created time or ball speed"), true);
        rec.ballSpeedMph = speed.toDouble();
        const QJsonValue carry = o.value(QStringLiteral("carryDistanceMeters"));
        if (carry.isDouble()) {
            const double yd = carry.toDouble() * kMetresToYards;
            // Premier writes 0.0 for a knock it never flew: "not measured", never a carry of 0.
            if (std::isfinite(yd) && yd > 0.0 && yd <= kMaxCarryYd)
                rec.carryYd = yd;
        }
        out.push_back(rec);
    }
    return out;
}

std::optional<double> premierCarryFor(const std::vector<PremierHistoryRecord> &records,
                                      qint64 createdUtcMs, double ballSpeedMph)
{
    const PremierHistoryRecord *best = nullptr;
    for (const PremierHistoryRecord &r : records) {
        const qint64 after = r.createdUtcMs - createdUtcMs;
        if (after < 0 || after > kPremierHistoryWindowMs)
            continue;
        if (std::fabs(r.ballSpeedMph - ballSpeedMph) > kPremierHistorySpeedTolMph)
            continue;
        if (!best || after < best->createdUtcMs - createdUtcMs)
            best = &r;
    }
    if (!best)
        return std::nullopt;
    return best->carryYd;
}

void addClubNotMeasuredDrops(LaunchMonitorReading &r)
{
    const QJsonObject none;
    for (const FieldMap &m : clubFields()) {
        if (m.member == &LaunchMonitorReading::spinAxis)
            continue;
        if ((r.*(m.member)).has_value())
            continue;
        readField(none, m, r, /*dropMissing=*/true);
    }
}

} // namespace pinpoint::lm
