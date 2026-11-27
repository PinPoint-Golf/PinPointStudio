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

// The rig state as a value: what the Rig holds, its canonical bytes and its digest (information_model.md §2.3
// OBJ-RIG, §5.2 ID-RIG; docs/design/rig_design.md §4.1, §4.2, §4.5). Vocabulary ring, context Acquisition.
// Header-only; it includes only Platform, Identity, Vocabulary/Common, the perspective header and Qt value types, and
// never recording.h (rig_design.md §6 rules 1 and 2). Nothing persists these types or reads them yet (C-4 and C-8).
//
// THE TWO PARTS (RP-4; information_model.md §2.3; rig_design_addendum.md decision 8). The rig state (`RigState`) is
// the versioned part: per camera, by its DeviceKey, the perspective, mirrored, fixed in place, the hitting-area box and
// its calibration records; per sensor placement, by its PlacementKey, the role and the alignment record. Its canonical
// bytes are digested into the RigStateId, so a change to it makes a new rig state. It also carries `sinceUtc` (when it
// came into force) and each record may carry `evidence`: both OUTSIDE the digest (addendum decision 2). The Rig's other
// facts (`RigOtherFacts`: display aliases, whether each device is enabled on this machine, the learned club-length
// prior) are not in the rig state at all: they sit beside it in `RigParts`, the two parts as the Rig and rig.json hold
// them, and a change to them makes no new state. A Recording's reference holds the rig state only.
//
// THE CANONICAL BYTES fix how every value is written, so the Mac and the cabin PC compute the same digest:
//   - a camera by its DeviceKey's camera spelling; a sensor by its PlacementKey's spelling (the device id and the
//     lane) only, never its description: two sensor keys are equal when their device ids are (addendum decision 9);
//   - a string: its UTF-8 byte count in decimal, ':', the bytes;
//   - an integer: decimal; a flag: 0 or 1;
//   - a real number: its IEEE-754 double bit pattern, 16 lower-case hex digits, with -0 written as 0 and every NaN as
//     one quiet NaN; no decimal formatting, so no library's rounding can differ (a float is widened to double first);
//   - a time: milliseconds since the epoch, UTC, in decimal; "-" for none;
//   - lists: each entry's bytes are made, then the entries are SORTED by those bytes, so the digest ignores list
//     order; a camera's records are sorted inside its entry.
// The first line names the format, "pinpoint.rigState/1". Changing ANY of this changes every digest: it is a new
// format line, never an edit.

#include "calibration_record.h"
#include "device_key.h"
#include "club_length_prior_key.h"
#include "../Common/segment_role.h"
#include "../Identity/identity.h"
#include "../Recording/perspective.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QCryptographicHash>
#include <QDateTime>
#include <QList>
#include <QRectF>
#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace pinpoint::rig {

// One camera in the rig state. The perspective is held as the enum AND its integer code, so a code this build does not
// know (a newer build's sixth perspective) is kept and written back unchanged: perspective() reads it as None, as
// recording::perspectiveFromCode does, and the digest uses the code.
class CameraSetup
{
public:
    CameraSetup() = default;
    explicit CameraSetup(DeviceKey key) : m_key(std::move(key)) {}

    const DeviceKey &key() const { return m_key; }

    recording::Perspective perspective() const { return recording::perspectiveFromCode(m_perspectiveCode); }
    int                    perspectiveCode() const { return m_perspectiveCode; }
    void setPerspective(recording::Perspective p) { m_perspectiveCode = recording::perspectiveCode(p); }
    void setPerspectiveCode(int code) { m_perspectiveCode = code; }

    bool mirrored = false;
    bool fixedInPlace = false;
    // The hitting-area box, normalised to the frame (0..1). Held whether or not the camera is fixed in place, as today
    // (the box is applied only while it is; rig_design.md §4.1); both are in the digest.
    std::optional<QRectF> hittingArea;
    QList<CalibrationRecord> calibrations;

private:
    DeviceKey m_key;
    int       m_perspectiveCode = 0;
};

// One sensor placement: the thing that holds a role (a Witmotion, or one lane of a HackMotion), and its alignment
// record, held for the run of the app and never saved to rig.json (rig_design.md §4.2).
struct SensorSetup {
    PlacementKey                     key;
    analysis::SegmentRole            role = analysis::SegmentRole::Unknown;
    std::optional<CalibrationRecord> calibration;   // a sensorAlignment record, inside the digest
};

// The rig state: the versioned part.
struct RigState {
    QList<CameraSetup> cameras;   // no fixed length: a third camera is one more entry
    QList<SensorSetup> sensors;
    QDateTime          sinceUtc;  // when this state came into force; outside the digest
};

// The Rig's other facts: not versioned, never in the rig state, outside the digest (information_model.md §2.3).
struct RigOtherFacts {
    QList<QPair<DeviceKey, QString>>               aliases;
    QList<QPair<DeviceKey, bool>>                  enabledOnThisMachine;
    QList<QPair<ClubLengthPriorKey, QVariantMap>>  clubLengthPriors;   // the value as SET-05 stores it
};

// The learned club-length prior's entry for `key`, found by the key's spelling (ClubLengthPriorKey::persisted(), as
// analysis/clubLenPrior was looked up), never by key equality; empty when there is none, or for a null key. The Rig
// (learnClubLength) and the capture side's read at the freeze both find an entry this way.
inline QVariantMap clubLengthPriorOf(const RigOtherFacts &facts, const ClubLengthPriorKey &key)
{
    if (key.isNull()) return {};
    const QString spelling = key.persisted();
    for (const auto &p : facts.clubLengthPriors)
        if (p.first.persisted() == spelling) return p.second;
    return {};
}

// The Rig's two parts side by side, as the Rig holds them and rig.json stores them (rig_design.md §4.4). Its id is the
// rig state's: rigStateIdOf(parts) reads `state` only.
struct RigParts {
    RigState      state;
    RigOtherFacts other;
};

// The rig a Recording was captured with (rig_design.md §4.5): exactly three states. Unknown: written before the Rig
// existed, or by a writer that does not know the rig. None: made with no rig (an external upload). Known: captured on
// this rig state, with its reference. It holds the rig state only, never the Rig's other facts.
class RigReference
{
public:
    enum class State { Unknown, None, Known };

    RigReference() = default;   // Unknown
    static RigReference unknown() { return {}; }
    static RigReference none() { RigReference r; r.m_state = State::None; return r; }
    static RigReference known(const RigRef &ref, RigState rig)
    {
        RigReference r;
        r.m_state = State::Known;
        r.m_ref   = ref;
        r.m_rig   = std::move(rig);
        return r;
    }

    State           state() const { return m_state; }
    const RigRef   &ref() const { return m_ref; }        // null unless Known
    const RigState &rigState() const { return m_rig; }   // empty unless Known

private:
    State    m_state = State::Unknown;
    RigRef   m_ref;
    RigState m_rig;
};

// ── The canonical bytes and the digest ─────────────────────────────────────────────────────────────────────────

namespace canonical_detail {
inline QByteArray str(const QString &s)
{
    const QByteArray u = s.toUtf8();
    return QByteArray::number(u.size()) + ':' + u;
}
inline QByteArray num(qint64 v) { return QByteArray::number(v); }
inline QByteArray flag(bool b) { return b ? QByteArrayLiteral("1") : QByteArrayLiteral("0"); }
inline QByteArray real(double v)
{
    if (std::isnan(v)) v = std::numeric_limits<double>::quiet_NaN();
    if (v == 0.0) v = 0.0;   // -0 -> +0
    std::uint64_t bits = std::bit_cast<std::uint64_t>(v);
    if (std::isnan(v)) bits = 0x7ff8000000000000ULL;
    return QByteArray::number(static_cast<qulonglong>(bits), 16).rightJustified(16, '0');
}
inline QByteArray time(const QDateTime &t)
{
    return t.isValid() ? QByteArray::number(t.toMSecsSinceEpoch()) : QByteArrayLiteral("-");
}
inline QByteArray quat(const QQuaternion &q)
{
    return real(q.scalar()) + ',' + real(q.x()) + ',' + real(q.y()) + ',' + real(q.z());
}
inline QByteArray named(const NamedValues &v)
{
    QByteArray out = num(v.values().size());
    for (const auto &e : v.values()) out += ' ' + str(e.first) + '=' + real(e.second);
    return out;
}
inline QByteArray camera(const DeviceKey &k) { return str(k.persistedCameraKey()); }
// A sensor by its device id and lane only: the description is not part of the key (addendum decision 9).
inline QByteArray placement(const PlacementKey &k) { return str(k.persisted()); }
inline QByteArray sorted(QByteArrayList parts)
{
    std::sort(parts.begin(), parts.end());
    return parts.join('\n');
}

// One record, without its evidence.
inline QByteArray record(const CalibrationRecord &r)
{
    QByteArray out = "record ";
    out += std::holds_alternative<DeviceKey>(r.subject) ? "camera " + camera(std::get<DeviceKey>(r.subject))
                                                        : "sensor " + placement(std::get<PlacementKey>(r.subject));
    out += ' ' + str(calibrationKindName(r.kind)) + ' ' + str(calibrationMethodName(r.method));
    if (const auto *p = std::get_if<SensorAlignmentParameters>(&r.parameters)) {
        out += " alignment role " + str(analysis::segmentRoleName(p->role));
        if (p->alignment)
            out += " A " + quat(p->alignment->alignA) + " M " + quat(p->alignment->mountM) + " mountDeviation "
                   + real(p->alignment->mountDeviationDeg) + " mountGravityError "
                   + real(p->alignment->mountGravityErrorDeg);
        else
            out += " A - M -";
        const TrunkCalibProvenance &t = p->trunkSolve;
        if (t.present)
            out += " trunk " + str(t.deltaTier) + ' ' + real(t.sigmaDeltaDeg) + ' ' + real(t.sampleFraction) + ' '
                   + real(t.maxAbsGyroDps) + ' ' + real(t.gravityErrPct) + ' ' + real(t.longAxisFromUpDeg) + ' '
                   + real(t.faceFromHorizontalDeg);
        else
            out += " trunk -";
    } else {
        out += " parameters " + named(std::get<NamedValues>(r.parameters));
    }
    out += " uncertainty " + (r.uncertainty.isKnown() ? named(r.uncertainty.values()) : QByteArrayLiteral("-"));
    out += " observed " + time(r.observedAtUtc);
    return out;
}
} // namespace canonical_detail

// The canonical bytes of the rig state, the versioned part. sinceUtc and evidence are not in them.
inline QByteArray canonicalBytes(const RigState &s)
{
    using namespace canonical_detail;
    QByteArrayList cameras;
    for (const CameraSetup &c : s.cameras) {
        QByteArray e = "camera " + camera(c.key()) + " perspective " + num(c.perspectiveCode()) + " mirrored "
                       + flag(c.mirrored) + " fixed " + flag(c.fixedInPlace) + " area ";
        e += c.hittingArea ? real(c.hittingArea->x()) + ',' + real(c.hittingArea->y()) + ','
                                 + real(c.hittingArea->width()) + ',' + real(c.hittingArea->height())
                           : QByteArrayLiteral("-");
        QByteArrayList records;
        for (const CalibrationRecord &r : c.calibrations) records << "  " + record(r);
        e += " records " + num(records.size());
        if (!records.isEmpty()) e += '\n' + sorted(records);
        cameras << e;
    }
    QByteArrayList sensors;
    for (const SensorSetup &x : s.sensors)
        sensors << "sensor " + placement(x.key) + " role " + str(analysis::segmentRoleName(x.role)) + " calibration "
                       + (x.calibration ? record(*x.calibration) : QByteArrayLiteral("-"));
    return QByteArrayLiteral("pinpoint.rigState/1\n") + "cameras " + num(cameras.size()) + '\n' + sorted(cameras)
           + "\nsensors " + num(sensors.size()) + '\n' + sorted(sensors) + '\n';
}

// The rig state's id: SHA-256 over its canonical bytes.
inline RigStateId rigStateIdOf(const RigState &s)
{
    return RigStateId::fromDigest(QCryptographicHash::hash(canonicalBytes(s), QCryptographicHash::Sha256));
}
// The Rig's two parts have the id of their rig state: the other facts never reach it.
inline RigStateId rigStateIdOf(const RigParts &p) { return rigStateIdOf(p.state); }

} // namespace pinpoint::rig
