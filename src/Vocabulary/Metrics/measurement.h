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

// A SHOT'S MEASUREMENTS, ONE SOURCE-TAGGED SET (refactor 6.1a; information_model.md OBJ-MEASUREMENT, R-INS-1, R-INS-2).
//
//   Measurement      {metric, source (instrument + route), kind, value, σ}: one measurement of the shot. The gates are
//                    the set's records (gateRecords), which trust::gateFor reads per metric key, as before.
//   MeasurementSet   every measurement of one shot, held by its Shot (Shot::measurements). Readers filter it by source
//                    and kind; which source WINS for a metric is never decided here but by the one precedence rule
//                    (measurement_precedence.h, the rules ring).
//   the stored key   how FILE-SHOT-01 spells a measurement in `analysis.metrics[]`: `lm.<metric>` a launch monitor
//                    reading, `hm.<metric>` a HackMotion reading, the LM-derived keys (what PinPoint derives from a
//                    reading: lmDerivedKeys) and every other key PinPoint's own measurement rules over the Recording's
//                    streams, its route in `routeId`. parseStoredKey / storedKey are the one translation; the file's
//                    keys are unchanged (no format change: the set is a projection of what the document stores).
//
// THE INSTRUMENTS' KEY LISTS ARE STATED HERE ONCE: the metric providers' claim lists (metric_providers.cpp) and the
// document writer's LM-derived strip list (swing_doc_writer.cpp) are these.
//
// KINDS (DEC-54): a reading is what an instrument delivered (the launch monitor, HackMotion) — source; a derived
// measurement is what the measurement rules made of the Recording's streams (and of a reading: the LM-derived keys).
//
// Vocabulary ring (src/Vocabulary/Metrics): pure, no I/O; parses the analysis block a reader hands it.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>
#include <vector>

namespace pinpoint::analysis {

// Phase::Impact's number in a stored phase sample (swing_analysis.h). Stated here so the domain ring's headers that hold
// a set need not include the analysis vocabulary; measurement_precedence.h asserts it equals Phase::Impact.
inline constexpr int kStoredImpactPhase = 5;

enum class MeasurementKind { Reading, Derived };
// Own: PinPoint's measurement rules over the Recording's streams (camera, IMU); the route says which.
enum class Instrument { Own, LaunchMonitor, HackMotion };

struct MeasurementSource {
    Instrument instrument = Instrument::Own;
    QString    route;   // the producer route (MetricSeries::routeId); "" when not stated
};

// ── The stored keys (FILE-SHOT-01) ───────────────────────────────────────────────────────────────────────────
// The launch monitor's readings, as stored. Every key `lm.`-prefixed; a quantity we also estimate keeps its bare key for
// our own (the validation pairs).
inline const std::vector<QString> &launchMonitorReadingKeys()
{
    static const std::vector<QString> keys{
        // Measured where we also estimate — the validation pairs.
        QStringLiteral("lm.clubheadSpeed"),  QStringLiteral("lm.ballSpeed"),
        QStringLiteral("lm.attackAngle"),    QStringLiteral("lm.clubPath"),
        QStringLiteral("lm.launchAngle"),    QStringLiteral("lm.launchDirection"),
        QStringLiteral("lm.lowPointAhead"),
        // Club delivery no camera of ours can resolve.
        QStringLiteral("lm.faceAngle"),      QStringLiteral("lm.faceToPath"),
        QStringLiteral("lm.dynamicLoft"),    QStringLiteral("lm.spinLoft"),
        QStringLiteral("lm.lieAngle"),       QStringLiteral("lm.closureRate"),
        // Strike.
        QStringLiteral("lm.smashFactor"),    QStringLiteral("lm.strikeLocation"),
        QStringLiteral("lm.strikeHeight"),
        // Spin.
        QStringLiteral("lm.spinRate"),       QStringLiteral("lm.backSpin"),
        QStringLiteral("lm.sideSpin"),       QStringLiteral("lm.spinAxis"),
        // Flight-model outputs.
        QStringLiteral("lm.carryDistance"),  QStringLiteral("lm.totalDistance"),
        QStringLiteral("lm.offline"),        QStringLiteral("lm.peakHeight"),
        QStringLiteral("lm.descentAngle"),   QStringLiteral("lm.distanceToPin"),
    };
    return keys;
}
// HackMotion's readings, as stored (`hm.`-prefixed beside the bare key our own sensors' wrist maths writes).
inline const std::vector<QString> &hackMotionReadingKeys()
{
    static const std::vector<QString> keys{ QStringLiteral("hm.leadWristFlexExt"), QStringLiteral("hm.leadWristRadUln"),
                                            QStringLiteral("hm.forearmRotation") };
    return keys;
}
// What PinPoint derives from a launch monitor reading (swing_doc_writer.cpp lmDerivedEntries): bare keys, written beside
// the readings when a reading is paired, derived again from the raw block on a re-analysis.
inline const std::vector<QString> &lmDerivedKeys()
{
    static const std::vector<QString> keys{ QStringLiteral("compoundMiss"), QStringLiteral("lowPointFromAttack") };
    return keys;
}
// The metric an LM-derived key measures (the quantity it is a rung of): lowPointFromAttack is a low point.
inline QString lmDerivedMetric(const QString &key)
{
    return key == QLatin1String("lowPointFromAttack") ? QStringLiteral("lowPointAhead") : key;
}

inline bool isLaunchMonitorReading(const QString &key) { return key.startsWith(QLatin1String("lm.")); }
inline bool isHackMotionReading(const QString &key)    { return key.startsWith(QLatin1String("hm.")); }
inline bool isLmDerivedKey(const QString &key)
{
    for (const QString &k : lmDerivedKeys()) if (k == key) return true;
    return false;
}

struct StoredKey {
    QString         metric;   // the quantity ("clubheadSpeed", "lowPointAhead")
    Instrument      instrument = Instrument::Own;
    MeasurementKind kind = MeasurementKind::Derived;
};

// A stored key's metric, instrument and kind (the one translation).
inline StoredKey parseStoredKey(const QString &key)
{
    if (isLaunchMonitorReading(key)) return { key.mid(3), Instrument::LaunchMonitor, MeasurementKind::Reading };
    if (isHackMotionReading(key))    return { key.mid(3), Instrument::HackMotion, MeasurementKind::Reading };
    if (isLmDerivedKey(key))         return { lmDerivedMetric(key), Instrument::LaunchMonitor, MeasurementKind::Derived };
    return { key, Instrument::Own, MeasurementKind::Derived };
}

// The stored key of `metric` as `instrument` delivers it (a reading), or PinPoint's own (Own). The LM-derived keys are
// named, not composed: the vocabulary's list, by metric.
inline QString storedKey(const QString &metric, Instrument instrument)
{
    switch (instrument) {
    case Instrument::LaunchMonitor: return QStringLiteral("lm.") + metric;
    case Instrument::HackMotion:    return QStringLiteral("hm.") + metric;
    case Instrument::Own:           return metric;
    }
    return metric;
}

// Is `key` one the instruments' lists declare (a reading or an LM-derived key)? A bare key of our own is not listed here.
inline bool isDeclaredInstrumentKey(const QString &key)
{
    for (const QString &k : launchMonitorReadingKeys()) if (k == key) return true;
    for (const QString &k : hackMotionReadingKeys())    if (k == key) return true;
    return isLmDerivedKey(key);
}

// ── The set ──────────────────────────────────────────────────────────────────────────────────────────────────
struct Measurement {
    QString               metric;     // the quantity
    QString               key;        // as stored (FILE-SHOT-01)
    MeasurementSource     source;
    MeasurementKind       kind = MeasurementKind::Derived;
    std::optional<double> value;      // at Impact: a reading's one number, a derived curve's Impact sample
    std::optional<double> sigma;      // the series' 1σ, when its producer propagated one
    QString               unit;
};

class MeasurementSet
{
public:
    std::vector<Measurement> items;          // in the document's order
    QVariantList             gateRecords;    // the shot's gate records, as stored (analysis.gates)
    std::optional<int>       gatesVersion;   // analysis.gatesVersion, when the document has one

    bool isEmpty() const { return items.empty(); }

    const Measurement *byKey(const QString &) const && = delete;   // a temporary would leave the result dangling (#247, #248)
    const Measurement *byKey(const QString &key) const&
    {
        for (const Measurement &m : items) if (m.key == key) return &m;
        return nullptr;
    }
    const Measurement *find(const QString &, Instrument, std::optional<MeasurementKind> = std::nullopt) const && = delete;
    // `metric` from `instrument`, of `kind` when given.
    const Measurement *find(const QString &metric, Instrument instrument,
                            std::optional<MeasurementKind> kind = std::nullopt) const &
    {
        for (const Measurement &m : items)
            if (m.metric == metric && m.source.instrument == instrument && (!kind || m.kind == *kind)) return &m;
        return nullptr;
    }
    // Every measurement from `instrument` of `kind`, with a value at Impact, by stored key (a launch monitor's readings:
    // values(LaunchMonitor, Reading), keyed "lm.carryDistance").
    QHash<QString, double> values(Instrument instrument, MeasurementKind kind) const
    {
        QHash<QString, double> out;
        for (const Measurement &m : items)
            if (m.source.instrument == instrument && m.kind == kind && m.value) out.insert(m.key, *m.value);
        return out;
    }
};

// The set, from a shot's analysis block as stored (`metrics[]`, `gates`, `gatesVersion`). A series' value is its
// Impact phase sample — a reading is an empty curve carrying one (lmMetricEntries), and a derived curve's Impact
// instant is the one number a reader quotes. Pure.
inline MeasurementSet measurementSetFromAnalysis(const QJsonObject &analysis)
{
    MeasurementSet set;
    for (const QJsonValue &v : analysis.value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject mo = v.toObject();
        const QString key = mo.value(QStringLiteral("key")).toString();
        if (key.isEmpty())
            continue;
        const StoredKey sk = parseStoredKey(key);
        Measurement m;
        m.metric            = sk.metric;
        m.key               = key;
        m.kind              = sk.kind;
        m.source.instrument = sk.instrument;
        m.source.route      = mo.value(QStringLiteral("routeId")).toString();
        m.unit              = mo.value(QStringLiteral("unit")).toString();
        if (mo.contains(QStringLiteral("sigma")))
            m.sigma = mo.value(QStringLiteral("sigma")).toDouble();
        for (const QJsonValue &pv : mo.value(QStringLiteral("phaseSamples")).toArray()) {
            const QJsonObject ps = pv.toObject();
            if (ps.value(QStringLiteral("phase")).toInt() != kStoredImpactPhase)
                continue;
            const QJsonValue val = ps.value(QStringLiteral("value"));
            if (val.isDouble())
                m.value = val.toDouble();
            break;
        }
        set.items.push_back(std::move(m));
    }
    set.gateRecords = analysis.value(QStringLiteral("gates")).toArray().toVariantList();
    if (analysis.contains(QStringLiteral("gatesVersion")))
        set.gatesVersion = analysis.value(QStringLiteral("gatesVersion")).toInt();
    return set;
}

} // namespace pinpoint::analysis
