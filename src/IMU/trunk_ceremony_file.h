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

// The trunk ceremony, as recorded and as replayed (trunk_imu_sequence_grf_design.md §6.7 "The raw
// ceremony data (samples and step boundaries) is recorded with the session, so swinglab and the
// unit tests replay the solve without the golfer"; §16.2 W2.1c).
//
// ONE model, ONE serialisation, ONE replay, used three ways:
//   * live — TrunkCeremonyRecorder (src/Gui/imu/trunk_ceremony_recorder.*) fills a Ceremony from
//     the sensors' taps and runs the turn checks through runTurn() below;
//   * on disk — toJson()/fromJson(), the session folder's trunk_ceremony.json;
//   * offline — swinglab --trunk-calib-from <trunk_ceremony.json> and trunk_ceremony_file_test
//     replay the stand-tall solve (replayStandTall) and the turn checks (replayTurn).
// Because the live turn check and the replay are the same function over the same samples, and the
// stand-tall replay is trunk_stand_tall::solveLatest — the function ImuInstance::calibrateTrunk
// runs — over the same samples ending at the instant the live solve recorded, the offline A and M
// equal the live ones. (One µs-wide exception: a sample stamped just before the live solve read the
// clock but pushed to its ring just after is in the file and not in the live window. It moves the
// mean by one sample in ~400 and has not been seen; the windowEndUs it would show is recorded.)
//
// pinpoint.trunk_ceremony/1 — one JSON object, written compact:
//   format   "pinpoint.trunk_ceremony/1"
//   header   { createdUtc, endedUtc (ISO 8601 UTC), appVersion, handedness ("right"|"left"),
//              clock: "host steady µs (EventBuffer::nowMicros)", beganUs, endedUs,
//              sensors: [ { role, serial, deviceId, instrument, configuredRateHz, measuredRateHz,
//                           samples, overCap } ] }
//   steps    [ { name, startUs, endUs, result {} } ]   in the order they began; a step repeated
//              after a failure appears again. endUs −1 = never ended. Names: "standTall",
//              "turnAway", "turnThrough", "armRoutine" (the wG3 routine's span, trunk + HackMotion),
//              and anything the routine marks.
//   lanes    [ { role, serial, instrument, t_us [n], accel [n][3], gyro [n][3], quat [n][4] } ]
//              one per sensor unit, every sample from begin() to end(): the host-arrival stamp and
//              the ImuSample exactly as the EventBuffer would store it (imu_sample.h: raw sensor
//              frame; accel g, gyro °/s, quat w,x,y,z). ⚠ A "hackmotion" lane's accel is the
//              device's GRAVITY-REMOVED linear acceleration and its quaternion the device's own
//              convention — as on its swing.json lane; never pool it with a Witmotion lane.
//              Floats are written with 9 significant digits, so they read back exactly — equal
//              as IEEE values; only a −0 reads back +0 (QJsonValue keeps integral numbers as
//              integers, whatever the text says).
//
// result{} by step:
//   standTall    { pelvis: <calibrateTrunk's map>, thorax: <…> } — keyed by the map's "role";
//                the map's windowEndUs is the instant the live solve's 2 s window ended
//   turnAway /   turnRecordToMap() plus, per sensor, the inputs runTurn() read: serial,
//   turnThrough  windowStartUs, windowEndUs, alignA, mountM ([w,x,y,z])
//
// Pure: Qt Core/Gui value types only — no QObject, no device, no clock. Tested by
// src/IMU/tests/trunk_ceremony_file_test.cpp.

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QQuaternion>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../Buffer/imu_sample.h"
#include "imu_calibration.h"
#include "trunk_ceremony.h"
#include "trunk_stand_tall.h"

namespace trunk_ceremony_file {

inline constexpr const char *kFormat   = "pinpoint.trunk_ceremony/1";
inline constexpr const char *kFileName = "trunk_ceremony.json";

inline QString stepStandTall()   { return QStringLiteral("standTall"); }
inline QString stepTurnAway()    { return QStringLiteral("turnAway"); }
inline QString stepTurnThrough() { return QStringLiteral("turnThrough"); }
inline QString stepArmRoutine()  { return QStringLiteral("armRoutine"); }

inline QString instrumentWitmotion()  { return QStringLiteral("witmotion"); }
inline QString instrumentHackMotion() { return QStringLiteral("hackmotion"); }

struct Lane {
    QString  role;                    // placement role: pelvis | thorax | leadForearm | leadHand
    QString  serial;                  // the EventBuffer identifier = swing.json source.serial
    QString  deviceId;
    QString  instrument;              // instrumentWitmotion() | instrumentHackMotion()
    int      configuredRateHz = 0;    // the Witmotion's output rate; 0 = not configurable (wG3)
    double   measuredRateHz   = 0.0;  // the instance's data rate when the ceremony began
    uint64_t overCap          = 0;    // samples dropped at the recorder's cap
    std::vector<int64_t>             tUs;
    std::vector<pinpoint::ImuSample> rows;
};

struct Step {
    QString     name;
    int64_t     startUs = 0;
    int64_t     endUs   = -1;         // −1 while open (and in a file, a step never ended)
    QVariantMap result;
    bool open() const { return endUs < 0; }
};

struct Ceremony {
    QString createdUtc;
    QString endedUtc;
    QString appVersion;
    QString handedness = QStringLiteral("right");
    int64_t beganUs = 0;
    int64_t endedUs = -1;
    std::vector<Lane> lanes;
    std::vector<Step> steps;
};

// ── Lookup ─────────────────────────────────────────────────────────────────────────────────────

inline const Lane *laneBySerial(const Ceremony &c, const QString &serial)
{
    for (const Lane &L : c.lanes)
        if (!serial.isEmpty() && L.serial == serial) return &L;
    return nullptr;
}

inline const Lane *laneByRole(const Ceremony &c, const QString &role)
{
    for (const Lane &L : c.lanes)
        if (L.role == role) return &L;
    return nullptr;
}

// The last step of that name (the one a repeat left in force), or null.
inline const Step *lastStep(const Ceremony &c, const QString &name)
{
    for (auto it = c.steps.rbegin(); it != c.steps.rend(); ++it)
        if (it->name == name) return &*it;
    return nullptr;
}

// [lo, hi) of the samples stamped in [t0, t1]. tUs ascending (arrival order).
inline void spanOf(const Lane &L, int64_t t0, int64_t t1, size_t *lo, size_t *hi)
{
    *lo = size_t(std::lower_bound(L.tUs.begin(), L.tUs.end(), t0) - L.tUs.begin());
    *hi = size_t(std::upper_bound(L.tUs.begin(), L.tUs.end(), t1) - L.tUs.begin());
    if (*hi < *lo) *hi = *lo;
}

inline QVariantList quatList(const QQuaternion &q) { return trunk_stand_tall::quatList(q); }

inline QQuaternion quatFromList(const QVariant &v)
{
    const QVariantList l = v.toList();
    if (l.size() != 4) return QQuaternion();
    return QQuaternion(float(l.at(0).toDouble()), float(l.at(1).toDouble()),
                       float(l.at(2).toDouble()), float(l.at(3).toDouble()));
}

// ── The stand-tall replay ──────────────────────────────────────────────────────────────────────

struct StandTallReplay {
    bool        found = false;        // a standTall step exists
    int         stepIndex = -1;       // into Ceremony::steps
    int64_t     windowEndUs = 0;
    trunk_stand_tall::LiveSolve solve;
};

// The solve that left A/M on this sensor, replayed. Live, calibrateTrunk applies only a valid
// solve and a later invalid one changes nothing, so the A/M in force are the LAST VALID standTall
// result for this role; with none valid, the last standTall step (its record says why it failed).
// The window ends where the live one did (result[role].windowEndUs), else at the step's end, and
// is summarised by solveLatest at the lane's configured rate — the live call, sample for sample.
inline StandTallReplay replayStandTall(const Ceremony &c, const Lane &lane, imu_calibration::TrunkSensor sensor)
{
    StandTallReplay out;
    const QString role = sensor == imu_calibration::TrunkSensor::Pelvis ? QStringLiteral("pelvis")
                                                                        : QStringLiteral("thorax");
    int pick = -1, lastAny = -1;
    for (int i = int(c.steps.size()) - 1; i >= 0; --i) {
        const Step &s = c.steps[size_t(i)];
        if (s.name != stepStandTall()) continue;
        if (lastAny < 0) lastAny = i;
        // A role's record is matched by serial when it carries one, so a sensor that changed role
        // between attempts is not replayed on another sensor's window.
        const QVariantMap r = s.result.value(role).toMap();
        if (r.value(QStringLiteral("valid")).toBool()
            && (r.value(QStringLiteral("serial")).toString().isEmpty()
                || r.value(QStringLiteral("serial")).toString() == lane.serial)) { pick = i; break; }
    }
    if (pick < 0) pick = lastAny;
    if (pick < 0) return out;
    const Step &s = c.steps[size_t(pick)];
    const QVariantMap r = s.result.value(role).toMap();
    out.found     = true;
    out.stepIndex = pick;
    if (r.contains(QStringLiteral("windowEndUs")))
        out.windowEndUs = r.value(QStringLiteral("windowEndUs")).toLongLong();
    else if (!s.open())
        out.windowEndUs = s.endUs;
    else
        out.windowEndUs = lane.tUs.empty() ? s.startUs : lane.tUs.back();
    out.solve = trunk_stand_tall::solveLatest(lane.tUs, lane.rows, out.windowEndUs, lane.configuredRateHz, sensor);
    return out;
}

// ── The turn checks, live and replayed ─────────────────────────────────────────────────────────

// What one turn check reads: per sensor, its lane, its window and the A/M it is read through.
struct TurnSensorInput {
    QString     serial;
    int64_t     windowStartUs = 0;
    int64_t     windowEndUs   = 0;
    QQuaternion A, M;
};
struct TurnInputs {
    imu_calibration::TrunkTurnDirection direction = imu_calibration::TrunkTurnDirection::Away;
    bool leftHanded = false;
    TurnSensorInput pelvis, thorax;
};

inline imu_calibration::TrunkTurnSeries turnSeries(const Ceremony &c, const TurnSensorInput &in)
{
    imu_calibration::TrunkTurnSeries s;
    s.A = in.A;
    s.M = in.M;
    const Lane *L = laneBySerial(c, in.serial);
    if (!L) return s;   // no data: checkTrunkTurn says the sensor sent none
    size_t lo = 0, hi = 0;
    spanOf(*L, in.windowStartUs, in.windowEndUs, &lo, &hi);
    s.qRaw.reserve(hi - lo);
    for (size_t k = lo; k < hi; ++k) {
        const pinpoint::ImuSample &r = L->rows[k];
        s.qRaw.emplace_back(r.quat_w, r.quat_x, r.quat_y, r.quat_z);
    }
    return s;
}

// What a turn window held, for the log: a zero reading is then told apart from a still sensor
// (spread 0 — the quaternion never moved), a broken one (nonFinite > 0, or a non-finite A/M) and
// a window in the wrong place (firstUs/lastUs against the step).
struct TurnWindowStats {
    size_t  samples   = 0;      // in [windowStartUs, windowEndUs]
    size_t  nonFinite = 0;      // of those, quaternions with a NaN/inf component or zero length
    int64_t firstUs   = -1;     // first and last sample stamp in the window (−1: none)
    int64_t lastUs    = -1;
    double  spreadDeg = 0.0;    // the largest rotation of any finite raw quaternion from the first finite one
    bool    alignFinite = true; // A and M are finite
};

inline TurnWindowStats turnWindowStats(const Ceremony &c, const TurnSensorInput &in)
{
    TurnWindowStats st;
    const auto finite = [](const QQuaternion &q) {
        return std::isfinite(q.scalar()) && std::isfinite(q.x()) && std::isfinite(q.y()) && std::isfinite(q.z());
    };
    st.alignFinite = finite(in.A) && finite(in.M);
    const Lane *L = laneBySerial(c, in.serial);
    if (!L) return st;
    size_t lo = 0, hi = 0;
    spanOf(*L, in.windowStartUs, in.windowEndUs, &lo, &hi);
    st.samples = hi - lo;
    if (hi > lo) { st.firstUs = L->tUs[lo]; st.lastUs = L->tUs[hi - 1]; }
    bool haveRef = false;
    QQuaternion ref;
    double minDot = 1.0;
    for (size_t k = lo; k < hi; ++k) {
        const pinpoint::ImuSample &r = L->rows[k];
        const QQuaternion q = QQuaternion(r.quat_w, r.quat_x, r.quat_y, r.quat_z);
        if (!finite(q) || q.lengthSquared() < 1e-12f) { ++st.nonFinite; continue; }
        const QQuaternion qn = q.normalized();
        if (!haveRef) { ref = qn; haveRef = true; continue; }
        minDot = std::min(minDot, std::abs(double(QQuaternion::dotProduct(ref, qn))));
    }
    st.spreadDeg = 2.0 * std::acos(std::clamp(minDot, 0.0, 1.0)) * 57.29577951308232;
    return st;
}

// The one turn check, live (TrunkCeremonyRecorder::checkTurn) and replayed (replayTurn).
inline imu_calibration::TrunkTurnRecord runTurn(const Ceremony &c, const TurnInputs &in)
{
    return imu_calibration::checkTrunkTurn(turnSeries(c, in.pelvis), turnSeries(c, in.thorax),
                                           in.direction, in.leftHanded);
}

inline QString directionName(imu_calibration::TrunkTurnDirection d)
{
    return d == imu_calibration::TrunkTurnDirection::Away ? QStringLiteral("away") : QStringLiteral("through");
}

inline QVariantMap turnSensorToMap(const imu_calibration::TrunkTurnSensorRecord &r)
{
    QVariantMap m;
    m.insert(QStringLiteral("samples"),             r.samples);
    m.insert(QStringLiteral("rangeDeg"),            double(r.rangeDeg));
    m.insert(QStringLiteral("turnDeg"),             double(r.turnDeg));
    m.insert(QStringLiteral("siTurnDeg"),           double(r.siTurnDeg));
    m.insert(QStringLiteral("axisFromVerticalDeg"), double(r.axisFromVerticalDeg));
    m.insert(QStringLiteral("rangeOk"),             r.rangeOk);
    m.insert(QStringLiteral("senseOk"),             r.senseOk);
    m.insert(QStringLiteral("axisOk"),              r.axisOk);
    m.insert(QStringLiteral("failure"),             r.failure);
    return m;
}

inline void addSensorInputs(QVariantMap &m, const TurnSensorInput &in)
{
    m.insert(QStringLiteral("serial"),        in.serial);
    m.insert(QStringLiteral("windowStartUs"), qint64(in.windowStartUs));
    m.insert(QStringLiteral("windowEndUs"),   qint64(in.windowEndUs));
    m.insert(QStringLiteral("alignA"),        quatList(in.A));
    m.insert(QStringLiteral("mountM"),        quatList(in.M));
}

// The record as checkTurn returns it to QML and as a turn step's result — the keys are the
// contract W2.2 reads (trunk_ceremony_recorder.h lists them).
inline QVariantMap turnRecordToMap(const imu_calibration::TrunkTurnRecord &r, const TurnInputs &in)
{
    QVariantMap m;
    m.insert(QStringLiteral("valid"),         r.valid);
    m.insert(QStringLiteral("failure"),       r.failure);
    m.insert(QStringLiteral("direction"),     directionName(r.direction));
    m.insert(QStringLiteral("leftHanded"),    r.leftHanded);
    m.insert(QStringLiteral("expectedSign"),  r.expectedSign);
    m.insert(QStringLiteral("chestOverHips"), double(r.chestOverHips));
    m.insert(QStringLiteral("orderOk"),       r.orderOk);
    m.insert(QStringLiteral("wrongWay"),      r.wrongWay);
    m.insert(QStringLiteral("cause"),         r.cause);
    QVariantMap p = turnSensorToMap(r.pelvis);
    QVariantMap t = turnSensorToMap(r.thorax);
    addSensorInputs(p, in.pelvis);
    addSensorInputs(t, in.thorax);
    m.insert(QStringLiteral("pelvis"), p);
    m.insert(QStringLiteral("thorax"), t);
    return m;
}

// The inputs a turn step's result recorded — what replayTurn reads back.
inline bool turnInputsFromResult(const Step &s, TurnInputs *out)
{
    if (s.name != stepTurnAway() && s.name != stepTurnThrough()) return false;
    const QVariantMap &r = s.result;
    if (!r.contains(QStringLiteral("pelvis")) || !r.contains(QStringLiteral("thorax"))) return false;
    out->direction = s.name == stepTurnAway() ? imu_calibration::TrunkTurnDirection::Away
                                              : imu_calibration::TrunkTurnDirection::Through;
    out->leftHanded = r.value(QStringLiteral("leftHanded")).toBool();
    const auto read = [](const QVariantMap &m, TurnSensorInput *in) {
        in->serial        = m.value(QStringLiteral("serial")).toString();
        in->windowStartUs = m.value(QStringLiteral("windowStartUs")).toLongLong();
        in->windowEndUs   = m.value(QStringLiteral("windowEndUs")).toLongLong();
        in->A             = quatFromList(m.value(QStringLiteral("alignA")));
        in->M             = quatFromList(m.value(QStringLiteral("mountM")));
    };
    read(r.value(QStringLiteral("pelvis")).toMap(), &out->pelvis);
    read(r.value(QStringLiteral("thorax")).toMap(), &out->thorax);
    return true;
}

// A turn step replayed through the A/M its live check used (as recorded) — or, given aP/mP/aT/mT,
// through another solve's (e.g. the stand-tall replay's), which is how a changed solve is judged.
inline bool replayTurn(const Ceremony &c, const Step &s, imu_calibration::TrunkTurnRecord *out,
                       const QQuaternion *aP = nullptr, const QQuaternion *mP = nullptr,
                       const QQuaternion *aT = nullptr, const QQuaternion *mT = nullptr)
{
    TurnInputs in;
    if (!turnInputsFromResult(s, &in)) return false;
    if (aP && mP) { in.pelvis.A = *aP; in.pelvis.M = *mP; }
    if (aT && mT) { in.thorax.A = *aT; in.thorax.M = *mT; }
    *out = runTurn(c, in);
    return true;
}

// ── Serialisation ──────────────────────────────────────────────────────────────────────────────

namespace detail {
inline void appendFloat(QByteArray &out, float v)
{
    if (!std::isfinite(v)) { out.append("null"); return; }
    char buf[32];
    const int n = std::snprintf(buf, sizeof buf, "%.9g", double(v));   // float round-trips exactly
    out.append(buf, std::min(n, int(sizeof buf) - 1));
}

inline void appendLane(QByteArray &out, const Lane &L)
{
    const QJsonObject head{ { QStringLiteral("role"), L.role }, { QStringLiteral("serial"), L.serial },
                            { QStringLiteral("instrument"), L.instrument } };
    QByteArray h = QJsonDocument(head).toJson(QJsonDocument::Compact);
    h.chop(1);                                 // drop the closing brace; the arrays follow
    out.append(h);
    const size_t n = std::min(L.tUs.size(), L.rows.size());
    out.append(",\"t_us\":[");
    for (size_t i = 0; i < n; ++i) {
        if (i) out.append(',');
        out.append(QByteArray::number(qlonglong(L.tUs[i])));
    }
    const auto triples = [&](const char *key, bool accel) {
        out.append("],\"").append(key).append("\":[");
        for (size_t i = 0; i < n; ++i) {
            if (i) out.append(',');
            const pinpoint::ImuSample &s = L.rows[i];
            out.append('[');
            appendFloat(out, accel ? s.accel_x : s.gyro_x); out.append(',');
            appendFloat(out, accel ? s.accel_y : s.gyro_y); out.append(',');
            appendFloat(out, accel ? s.accel_z : s.gyro_z); out.append(']');
        }
    };
    triples("accel", true);
    triples("gyro",  false);
    out.append("],\"quat\":[");
    for (size_t i = 0; i < n; ++i) {
        if (i) out.append(',');
        const pinpoint::ImuSample &s = L.rows[i];
        out.append('['); appendFloat(out, s.quat_w); out.append(',');
        appendFloat(out, s.quat_x); out.append(','); appendFloat(out, s.quat_y); out.append(',');
        appendFloat(out, s.quat_z); out.append(']');
    }
    out.append("]}");
}
} // namespace detail

// The header and steps go through QJsonDocument; the lanes — tens of thousands of samples — are
// written directly, so each float is 9 digits rather than QJson's 17 and the file stays a few MB.
inline QByteArray toJson(const Ceremony &c)
{
    QJsonArray sensors;
    for (const Lane &L : c.lanes)
        sensors.append(QJsonObject{
            { QStringLiteral("role"), L.role }, { QStringLiteral("serial"), L.serial },
            { QStringLiteral("deviceId"), L.deviceId }, { QStringLiteral("instrument"), L.instrument },
            { QStringLiteral("configuredRateHz"), L.configuredRateHz },
            { QStringLiteral("measuredRateHz"), L.measuredRateHz },
            { QStringLiteral("samples"), qint64(std::min(L.tUs.size(), L.rows.size())) },
            { QStringLiteral("overCap"), qint64(L.overCap) } });
    QJsonArray steps;
    for (const Step &s : c.steps)
        steps.append(QJsonObject{
            { QStringLiteral("name"), s.name }, { QStringLiteral("startUs"), qint64(s.startUs) },
            { QStringLiteral("endUs"), qint64(s.endUs) },
            { QStringLiteral("result"), QJsonObject::fromVariantMap(s.result) } });
    const QJsonObject root{
        { QStringLiteral("format"), QString::fromLatin1(kFormat) },
        { QStringLiteral("header"), QJsonObject{
              { QStringLiteral("createdUtc"), c.createdUtc }, { QStringLiteral("endedUtc"), c.endedUtc },
              { QStringLiteral("appVersion"), c.appVersion }, { QStringLiteral("handedness"), c.handedness },
              { QStringLiteral("clock"), QStringLiteral("host steady µs (EventBuffer::nowMicros)") },
              { QStringLiteral("beganUs"), qint64(c.beganUs) }, { QStringLiteral("endedUs"), qint64(c.endedUs) },
              { QStringLiteral("sensors"), sensors } } },
        { QStringLiteral("steps"), steps } };

    QByteArray out = QJsonDocument(root).toJson(QJsonDocument::Compact);
    out.chop(1);                               // the closing brace — "lanes" goes inside it
    size_t bytes = 0;
    for (const Lane &L : c.lanes) bytes += 160 * L.tUs.size() + 256;
    out.reserve(out.size() + qsizetype(bytes));
    out.append(",\"lanes\":[");
    for (size_t i = 0; i < c.lanes.size(); ++i) {
        if (i) out.append(',');
        detail::appendLane(out, c.lanes[i]);
    }
    out.append("]}\n");
    return out;
}

inline bool fromJson(const QByteArray &bytes, Ceremony *out, QString *error = nullptr)
{
    const auto fail = [&](const QString &why) { if (error) *error = why; return false; };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("not a JSON object (%1 at %2)").arg(pe.errorString()).arg(pe.offset));
    const QJsonObject root = doc.object();
    if (root[QStringLiteral("format")].toString() != QLatin1String(kFormat))
        return fail(QStringLiteral("format is \"%1\", not %2")
                        .arg(root[QStringLiteral("format")].toString(), QLatin1String(kFormat)));

    Ceremony c;
    const QJsonObject h = root[QStringLiteral("header")].toObject();
    c.createdUtc = h[QStringLiteral("createdUtc")].toString();
    c.endedUtc   = h[QStringLiteral("endedUtc")].toString();
    c.appVersion = h[QStringLiteral("appVersion")].toString();
    c.handedness = h[QStringLiteral("handedness")].toString();
    c.beganUs    = h[QStringLiteral("beganUs")].toInteger();
    c.endedUs    = h[QStringLiteral("endedUs")].toInteger(-1);
    QHash<QString, QJsonObject> sensorBySerial;
    for (const QJsonValue &v : h[QStringLiteral("sensors")].toArray())
        sensorBySerial.insert(v.toObject()[QStringLiteral("serial")].toString(), v.toObject());

    for (const QJsonValue &v : root[QStringLiteral("steps")].toArray()) {
        const QJsonObject o = v.toObject();
        Step s;
        s.name    = o[QStringLiteral("name")].toString();
        s.startUs = o[QStringLiteral("startUs")].toInteger();
        s.endUs   = o[QStringLiteral("endUs")].toInteger(-1);
        s.result  = o[QStringLiteral("result")].toObject().toVariantMap();
        c.steps.push_back(std::move(s));
    }

    for (const QJsonValue &v : root[QStringLiteral("lanes")].toArray()) {
        const QJsonObject o = v.toObject();
        Lane L;
        L.role       = o[QStringLiteral("role")].toString();
        L.serial     = o[QStringLiteral("serial")].toString();
        L.instrument = o[QStringLiteral("instrument")].toString();
        const QJsonObject sh = sensorBySerial.value(L.serial);
        L.deviceId         = sh[QStringLiteral("deviceId")].toString();
        L.configuredRateHz = sh[QStringLiteral("configuredRateHz")].toInt();
        L.measuredRateHz   = sh[QStringLiteral("measuredRateHz")].toDouble();
        L.overCap          = uint64_t(sh[QStringLiteral("overCap")].toInteger());
        const QJsonArray t = o[QStringLiteral("t_us")].toArray();
        const QJsonArray a = o[QStringLiteral("accel")].toArray();
        const QJsonArray g = o[QStringLiteral("gyro")].toArray();
        const QJsonArray q = o[QStringLiteral("quat")].toArray();
        if (a.size() != t.size() || g.size() != t.size() || q.size() != t.size())
            return fail(QStringLiteral("lane %1: t_us/accel/gyro/quat lengths differ (%2/%3/%4/%5)")
                            .arg(L.serial).arg(t.size()).arg(a.size()).arg(g.size()).arg(q.size()));
        L.tUs.reserve(size_t(t.size()));
        L.rows.reserve(size_t(t.size()));
        for (qsizetype i = 0; i < t.size(); ++i) {
            const QJsonArray ai = a.at(i).toArray(), gi = g.at(i).toArray(), qi = q.at(i).toArray();
            if (ai.size() != 3 || gi.size() != 3 || qi.size() != 4)
                return fail(QStringLiteral("lane %1: sample %2 is malformed").arg(L.serial).arg(i));
            const auto f = [](const QJsonArray &arr, int k) { return float(arr.at(k).toDouble()); };
            L.tUs.push_back(t.at(i).toInteger());
            L.rows.push_back(pinpoint::makeImuSample(f(ai, 0), f(ai, 1), f(ai, 2), f(gi, 0), f(gi, 1), f(gi, 2),
                                                     f(qi, 0), f(qi, 1), f(qi, 2), f(qi, 3)));
        }
        c.lanes.push_back(std::move(L));
    }
    *out = std::move(c);
    return true;
}

} // namespace trunk_ceremony_file
