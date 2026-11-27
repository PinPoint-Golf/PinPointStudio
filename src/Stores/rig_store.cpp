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

#include "rig_store.h"

#include "../Core/atomic_write.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_settings.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QtGlobal>

#include <initializer_list>

using namespace pinpoint;
using namespace pinpoint::rig;

namespace {

const QString kFileName = QStringLiteral("rig.json");   // FILE-APP-07: spelled here only (lint A4)

QString schemaLine(int version) { return QStringLiteral("pinpoint.rig/%1").arg(version); }

// ── Times and numbers ─────────────────────────────────────────────────────────────────────────

QJsonValue timeToJson(const QDateTime &t)
{
    return t.isValid() ? QJsonValue(t.toUTC().toString(Qt::ISODateWithMs)) : QJsonValue();
}
QDateTime timeFromJson(const QJsonValue &v)
{
    if (!v.isString()) return {};
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toUTC() : QDateTime();
}

QJsonObject namedToJson(const NamedValues &v)
{
    QJsonObject o;
    for (const auto &e : v.values()) o.insert(e.first, e.second);
    return o;
}
NamedValues namedFromJson(const QJsonObject &o)
{
    NamedValues v;
    for (auto it = o.begin(); it != o.end(); ++it) v.set(it.key(), it.value().toDouble());
    return v;
}

// ── Keys: each device by its own spelling (rig_design.md §4.6) ─────────────────────────────────

// A device named in the other facts: {"camera": <camera key>} or {"sensor": <description|deviceId>}.
void putDevice(QJsonObject &o, const DeviceKey &k)
{
    if (k.isCamera()) o.insert(QStringLiteral("camera"), k.persistedCameraKey());
    else if (k.isSensor()) o.insert(QStringLiteral("sensor"), k.persistedWithDescription());
}
DeviceKey takeDevice(const QJsonObject &o)
{
    if (o.contains(QStringLiteral("camera")))
        return DeviceKey::fromPersistedCameraKey(o.value(QStringLiteral("camera")).toString());
    if (o.contains(QStringLiteral("sensor")))
        return DeviceKey::fromPersistedWithDescription(o.value(QStringLiteral("sensor")).toString());
    return {};
}

// ── Calibration records ──────────────────────────────────────────────────────────────────────

std::optional<CalibrationKind> kindFromName(const QString &s)
{
    for (CalibrationKind k : { CalibrationKind::Intrinsics, CalibrationKind::Extrinsics, CalibrationKind::Focus,
                               CalibrationKind::SensorAlignment })
        if (calibrationKindName(k) == s) return k;
    return std::nullopt;
}
std::optional<CalibrationMethod> methodFromName(const QString &s)
{
    for (CalibrationMethod m : { CalibrationMethod::Factory, CalibrationMethod::PerFrame, CalibrationMethod::Solved,
                                 CalibrationMethod::UserMeasured, CalibrationMethod::EstimatedOnline,
                                 CalibrationMethod::Assumed, CalibrationMethod::DeviceHeld })
        if (calibrationMethodName(m) == s) return m;
    return std::nullopt;
}

// A camera's record. A sensor alignment is never written (it is held for the run only); a camera record holds its
// parameters by name.
std::optional<QJsonObject> recordToJson(const CalibrationRecord &r)
{
    const auto *named = std::get_if<NamedValues>(&r.parameters);
    const auto *camera = std::get_if<DeviceKey>(&r.subject);
    if (!named || !camera) return std::nullopt;
    QJsonObject o;
    QJsonObject subject;
    putDevice(subject, *camera);
    o.insert(QStringLiteral("subject"), subject);
    o.insert(QStringLiteral("kind"), calibrationKindName(r.kind));
    o.insert(QStringLiteral("method"), calibrationMethodName(r.method));
    o.insert(QStringLiteral("parameters"), namedToJson(*named));
    o.insert(QStringLiteral("uncertainty"),
             r.uncertainty.isKnown() ? QJsonValue(namedToJson(r.uncertainty.values())) : QJsonValue());   // null: not known
    o.insert(QStringLiteral("observedAtUtc"), timeToJson(r.observedAtUtc));
    if (r.evidence)
        o.insert(QStringLiteral("evidence"), QJsonObject{ { QStringLiteral("sessionId"), r.evidence->sessionId.toString() },
                                                          { QStringLiteral("fileName"), r.evidence->fileName } });
    return o;
}
std::optional<CalibrationRecord> recordFromJson(const QJsonObject &o)
{
    const DeviceKey camera = takeDevice(o.value(QStringLiteral("subject")).toObject());
    const auto      kind   = kindFromName(o.value(QStringLiteral("kind")).toString());
    const auto      method = methodFromName(o.value(QStringLiteral("method")).toString());
    if (!camera.isCamera() || !kind || !method || *kind == CalibrationKind::SensorAlignment) return std::nullopt;
    const QJsonValue u = o.value(QStringLiteral("uncertainty"));
    CalibrationRecord r = CalibrationRecord::forCamera(
        camera, *kind, *method, namedFromJson(o.value(QStringLiteral("parameters")).toObject()),
        u.isObject() ? Uncertainty::sigmas(namedFromJson(u.toObject())) : Uncertainty::notKnown(),
        timeFromJson(o.value(QStringLiteral("observedAtUtc"))));
    if (o.value(QStringLiteral("evidence")).isObject()) {
        const QJsonObject e = o.value(QStringLiteral("evidence")).toObject();
        r.evidence = CalibrationEvidence{ SessionId::fromString(e.value(QStringLiteral("sessionId")).toString()),
                                          e.value(QStringLiteral("fileName")).toString() };
    }
    return r;
}

// ── The two parts ────────────────────────────────────────────────────────────────────────────

QJsonArray camerasToJson(const QList<CameraSetup> &cameras)
{
    QJsonArray out;
    for (const CameraSetup &c : cameras) {
        QJsonObject o;
        o.insert(QStringLiteral("key"), c.key().persistedCameraKey());
        o.insert(QStringLiteral("perspective"), c.perspectiveCode());   // the integer: an unknown code is kept
        o.insert(QStringLiteral("mirrored"), c.mirrored);
        o.insert(QStringLiteral("fixedInPlace"), c.fixedInPlace);
        if (c.hittingArea)
            o.insert(QStringLiteral("hittingArea"),
                     QJsonObject{ { QStringLiteral("x"), c.hittingArea->x() }, { QStringLiteral("y"), c.hittingArea->y() },
                                  { QStringLiteral("w"), c.hittingArea->width() },
                                  { QStringLiteral("h"), c.hittingArea->height() } });
        QJsonArray records;
        for (const CalibrationRecord &r : c.calibrations)
            if (const auto j = recordToJson(r)) records.append(*j);
            else ppWarn() << "[RigStore] a camera record that is not a camera's was not written:" << c.key().persistedCameraKey();
        o.insert(QStringLiteral("calibrations"), records);
        out.append(o);
    }
    return out;
}
// What a file holds that this build cannot read (P2): kept exactly as read, outside the rig state and its digest, and
// written back at every save.
struct Unread {
    QJsonArray                cameras;   // camera entries with no camera key
    QMap<QString, QJsonArray> records;   // by camera key: calibration records this build cannot read
    QJsonArray                sensors;   // sensor entries whose role this build does not know, or with no placement key
    QMap<QString, QJsonArray> other;     // by list ("aliases", "enabledOnThisMachine", "clubLengthPriors"): entries
                                         // whose device (or prior key) this build cannot read
};

// ── Writing each entry over the entry as read (`[^rig-unknown]`; the fix round's B2) ─────────────────────────────
//
// A typed entry is written fresh from its value; a field this build does not know inside it would then be lost. So the
// fresh entry is written over the entry as read with the same identity: the fields this build knows come from the
// value (and a known field the value no longer holds, such as a cleared box, goes), every other field stays as read.

QJsonObject overRead(const QJsonObject &fresh, const QJsonObject &asRead, const QList<const char *> &known)
{
    QJsonObject o = asRead;
    for (const char *k : known) o.remove(QLatin1String(k));
    for (auto it = fresh.begin(); it != fresh.end(); ++it) o.insert(it.key(), it.value());
    return o;
}

template <typename Identity>
QJsonArray mergedOverRead(const QJsonArray &fresh, const QJsonArray &asRead, Identity identity,
                          const QList<const char *> &known)
{
    QHash<QString, QJsonObject> read;
    for (const QJsonValue &v : asRead) {
        const QString id = identity(v.toObject());
        if (!id.isEmpty() && !read.contains(id)) read.insert(id, v.toObject());
    }
    QJsonArray out;
    for (const QJsonValue &v : fresh) {
        const QJsonObject o = v.toObject();
        const auto it = read.constFind(identity(o));
        out.append(it == read.cend() ? o : overRead(o, *it, known));
    }
    return out;
}

QString cameraIdentity(const QJsonObject &o) { return o.value(QStringLiteral("key")).toString(); }
QString recordIdentity(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o.value(QStringLiteral("subject")).toObject()).toJson(QJsonDocument::Compact))
           + QLatin1Char('|') + o.value(QStringLiteral("kind")).toString() + QLatin1Char('|')
           + o.value(QStringLiteral("method")).toString();
}
QString sensorIdentity(const QJsonObject &o) { return o.value(QStringLiteral("placement")).toString(); }
QString deviceIdentity(const QJsonObject &o)
{
    if (o.value(QStringLiteral("camera")).isString()) return QStringLiteral("camera:") + o.value(QStringLiteral("camera")).toString();
    if (o.value(QStringLiteral("sensor")).isString()) return QStringLiteral("sensor:") + o.value(QStringLiteral("sensor")).toString();
    return {};
}
QString priorIdentity(const QJsonObject &o)
{
    return o.value(QStringLiteral("athlete")).toString() + QLatin1Char('|') + o.value(QStringLiteral("club")).toString()
           + QLatin1Char('|') + o.value(QStringLiteral("faceOnCamera")).toString();
}

QList<CameraSetup> camerasFromJson(const QJsonArray &a, Unread &unread)
{
    QList<CameraSetup> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o   = v.toObject();
        const DeviceKey   key = DeviceKey::fromPersistedCameraKey(o.value(QStringLiteral("key")).toString());
        if (!key.isCamera()) {
            ppWarn() << "[RigStore] a camera entry with no camera key is kept as read, outside the rig state:"
                     << o.value(QStringLiteral("key")).toString();
            unread.cameras.append(v);
            continue;
        }
        CameraSetup c(key);
        c.setPerspectiveCode(o.value(QStringLiteral("perspective")).toInt());
        c.mirrored     = o.value(QStringLiteral("mirrored")).toBool();
        c.fixedInPlace = o.value(QStringLiteral("fixedInPlace")).toBool();
        if (o.value(QStringLiteral("hittingArea")).isObject()) {
            const QJsonObject b = o.value(QStringLiteral("hittingArea")).toObject();
            c.hittingArea = QRectF(b.value(QStringLiteral("x")).toDouble(), b.value(QStringLiteral("y")).toDouble(),
                                   b.value(QStringLiteral("w")).toDouble(), b.value(QStringLiteral("h")).toDouble());
        }
        bool unreadRecords = false;
        for (const QJsonValue &r : o.value(QStringLiteral("calibrations")).toArray()) {
            if (const auto rec = recordFromJson(r.toObject())) {
                c.calibrations.append(*rec);
            } else {
                ppWarn() << "[RigStore] a calibration record of" << key.persistedCameraKey()
                         << "that this build cannot read is kept as read, outside the rig state";
                unread.records[key.persistedCameraKey()].append(r);
                unreadRecords = true;
            }
        }
        // A camera that holds nothing this build reads but such records is the same as no entry (rig_commands.h): it
        // stays out of the rig state; its records are written back under its key.
        const bool allDefault = c.perspectiveCode() == 0 && !c.mirrored && !c.fixedInPlace && !c.hittingArea
                                && c.calibrations.isEmpty();
        if (unreadRecords && allDefault) continue;
        out.append(c);
    }
    return out;
}

// The cameras as written: the Rig's, each with the records this build could not read added back, then an entry for
// each camera that holds only such records, then the entries with no key, all exactly as read.
QJsonArray withUnreadCameras(QJsonArray cameras, const Unread &unread)
{
    QSet<QString> written;
    for (int i = 0; i < cameras.size(); ++i) {
        QJsonObject   o   = cameras.at(i).toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        written.insert(key);
        const auto it = unread.records.constFind(key);
        if (it == unread.records.cend()) continue;
        QJsonArray records = o.value(QStringLiteral("calibrations")).toArray();
        for (const QJsonValue &r : *it) records.append(r);
        o.insert(QStringLiteral("calibrations"), records);
        cameras.replace(i, o);
    }
    for (auto it = unread.records.cbegin(); it != unread.records.cend(); ++it) {
        if (written.contains(it.key())) continue;
        cameras.append(QJsonObject{ { QStringLiteral("key"), it.key() }, { QStringLiteral("perspective"), 0 },
                                    { QStringLiteral("mirrored"), false }, { QStringLiteral("fixedInPlace"), false },
                                    { QStringLiteral("calibrations"), *it } });
    }
    for (const QJsonValue &v : unread.cameras) cameras.append(v);
    return cameras;
}

QJsonArray sensorsToJson(const QList<SensorSetup> &sensors)
{
    QJsonArray out;
    for (const SensorSetup &s : sensors)   // the role only: an alignment is never saved (rig_design.md §4.2)
        out.append(QJsonObject{ { QStringLiteral("placement"), s.key.persisted() },
                                { QStringLiteral("role"), analysis::placementRoleName(s.role) } });
    return out;
}
QList<SensorSetup> sensorsFromJson(const QJsonArray &a, Unread &unread)
{
    QList<SensorSetup> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        SensorSetup       s;
        s.key  = PlacementKey::fromPersisted(o.value(QStringLiteral("placement")).toString());
        const QString role = o.value(QStringLiteral("role")).toString();
        s.role = analysis::segmentRoleForPlacementRole(role);
        // A role this build does not know (any name but the five and this build's own "" for none), or no placement
        // key: kept as read, outside the rig state (P2).
        if (s.key.isNull() || (s.role == analysis::SegmentRole::Unknown && !role.isEmpty())) {
            ppWarn() << "[RigStore] a sensor entry this build cannot read is kept as read, outside the rig state:"
                     << o.value(QStringLiteral("placement")).toString() << role;
            unread.sensors.append(v);
            continue;
        }
        out.append(s);
    }
    return out;
}

QJsonObject otherToJson(const RigOtherFacts &f, QJsonObject o)
{
    QJsonArray aliases, enabled, priors;
    for (const auto &a : f.aliases) {
        QJsonObject e;
        putDevice(e, a.first);
        e.insert(QStringLiteral("alias"), a.second);
        aliases.append(e);
    }
    for (const auto &a : f.enabledOnThisMachine) {
        QJsonObject e;
        putDevice(e, a.first);
        e.insert(QStringLiteral("enabled"), a.second);
        enabled.append(e);
    }
    for (const auto &p : f.clubLengthPriors)
        priors.append(QJsonObject{ { QStringLiteral("athlete"), p.first.athlete().toString() },
                                   { QStringLiteral("club"), p.first.club() },
                                   { QStringLiteral("faceOnCamera"), p.first.faceOnCamera().persistedCameraKey() },
                                   { QStringLiteral("value"), QJsonObject::fromVariantMap(p.second) } });
    o.insert(QStringLiteral("aliases"), aliases);
    o.insert(QStringLiteral("enabledOnThisMachine"), enabled);
    o.insert(QStringLiteral("clubLengthPriors"), priors);
    return o;
}
// The other facts. An entry whose device (or, for a prior, whose key) this build cannot read is kept as read in
// `unread.other`, outside the Rig's facts, and written back at every save (the Solution Architect, 27 Nov 2026,
// widening P2; the fix round's B1).
RigOtherFacts otherFromJson(const QJsonObject &o, Unread &unread)
{
    RigOtherFacts f;
    auto keep = [&](const char *list, const QJsonValue &v) {
        ppWarn() << "[RigStore] an entry of" << list << "that this build cannot read is kept as read, outside the Rig's facts";
        unread.other[QLatin1String(list)].append(v);
    };
    for (const QJsonValue &v : o.value(QStringLiteral("aliases")).toArray()) {
        const QJsonObject e = v.toObject();
        const DeviceKey   k = takeDevice(e);
        if (!k.isNull()) f.aliases.append({ k, e.value(QStringLiteral("alias")).toString() });
        else keep("aliases", v);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("enabledOnThisMachine")).toArray()) {
        const QJsonObject e = v.toObject();
        const DeviceKey   k = takeDevice(e);
        if (!k.isNull()) f.enabledOnThisMachine.append({ k, e.value(QStringLiteral("enabled")).toBool(true) });
        else keep("enabledOnThisMachine", v);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("clubLengthPriors")).toArray()) {
        const QJsonObject        e = v.toObject();
        const ClubLengthPriorKey k(AthleteId::fromString(e.value(QStringLiteral("athlete")).toString()),
                                   e.value(QStringLiteral("club")).toString(),
                                   DeviceKey::fromPersistedCameraKey(e.value(QStringLiteral("faceOnCamera")).toString()));
        if (!k.isNull()) f.clubLengthPriors.append({ k, e.value(QStringLiteral("value")).toObject().toVariantMap() });
        else keep("clubLengthPriors", v);
    }
    return f;
}

// ── The copy from settings (rig_design.md §4.4; decisions 9 and 10) ──────────────────────────────

// A settings value as JSON, losing nothing: a map, a list, a string, a number or a flag as itself; anything JSON has no
// form for (a QByteArray, a QRectF, ...) as its QDataStream bytes, base64, under "@qvariant".
QJsonValue settingsValueToJson(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::QVariantMap: {
        QJsonObject o;
        const QVariantMap m = v.toMap();
        for (auto it = m.cbegin(); it != m.cend(); ++it) o.insert(it.key(), settingsValueToJson(it.value()));
        return o;
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        QJsonArray a;
        for (const QVariant &e : v.toList()) a.append(settingsValueToJson(e));
        return a;
    }
    case QMetaType::QString: return v.toString();
    case QMetaType::Bool: return v.toBool();
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::UInt: case QMetaType::ULongLong:
        return QJsonValue(v.toLongLong());
    case QMetaType::Double: case QMetaType::Float: return v.toDouble();
    case QMetaType::UnknownType: return QJsonValue(QJsonValue::Null);
    default: break;
    }
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << v;
    }
    return QJsonObject{ { QStringLiteral("@qvariant"), QString::fromLatin1(bytes.toBase64()) } };
}

// A flag only when it is one: a bool, "true"/"false", or 0/1. Anything else is kept verbatim.
std::optional<bool> settingsFlag(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool) return v.toBool();
    if (v.typeId() == QMetaType::QString) {
        const QString t = v.toString();
        if (t == QLatin1String("true") || t == QLatin1String("1")) return true;
        if (t == QLatin1String("false") || t == QLatin1String("0")) return false;
        return std::nullopt;
    }
    bool ok = false;
    const int i = v.toInt(&ok);
    if (ok && (i == 0 || i == 1) && v.typeId() != QMetaType::Double) return i == 1;
    return std::nullopt;
}

std::optional<int> settingsInt(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool || v.typeId() == QMetaType::Double) return std::nullopt;
    bool ok = false;
    const int i = v.toInt(&ok);
    return ok ? std::optional<int>(i) : std::nullopt;
}

std::optional<QRectF> settingsBox(const QVariant &v)
{
    if (v.typeId() != QMetaType::QVariantMap) return std::nullopt;
    const QVariantMap m = v.toMap();
    double xs[4];
    const char *names[4] = { "x", "y", "w", "h" };
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        const QVariant e = m.value(QLatin1String(names[i]));
        xs[i] = e.toDouble(&ok);
        if (!ok || !e.isValid()) return std::nullopt;
    }
    if (m.size() != 4) return std::nullopt;   // a field this build does not know: keep the entry verbatim
    return QRectF(xs[0], xs[1], xs[2], xs[3]);
}

// The camera group, decoded from the settings store: typed values for every entry that is a camera's fact, and the
// entries that are not, verbatim.
struct CameraGroup {
    QList<CameraSetup>               cameras;   // all-default entries dropped, as the Rig holds them
    QList<QPair<DeviceKey, QString>> aliases;
    QList<DeviceKey>                 excluded;
    QJsonObject                      kept;      // settings key -> {"entries": {key: value}} | {"list": [...]} | {"whole": v}
};

const char *const kCameraMaps[] = { "camera/perspective", "camera/isMirrored", "camera/fixedInPlace", "camera/ballRoi",
                                    "camera/alias" };
const char *const kCameraList = "camera/excluded";

CameraGroup readCameraGroup()
{
    const QSettings settings = ppSettings();   // read only: the copy never writes a key
    CameraGroup     g;
    QMap<DeviceKey, CameraSetup> cams;
    auto camera = [&](const DeviceKey &k) -> CameraSetup & {
        auto it = cams.find(k);
        if (it == cams.end()) it = cams.insert(k, CameraSetup(k));
        return it.value();
    };
    for (const char *name : kCameraMaps) {
        const QString key = QLatin1String(name);
        if (!settings.contains(key)) continue;
        const QVariant whole = settings.value(key);
        if (!whole.isValid()) continue;   // INI's "@Invalid()": no value at all, the same as absent
        if (whole.typeId() != QMetaType::QVariantMap) {   // not the shape AppSettings writes: kept whole
            g.kept.insert(key, QJsonObject{ { QStringLiteral("whole"), settingsValueToJson(whole) } });
            continue;
        }
        const QVariantMap map = whole.toMap();
        QJsonObject keptEntries;
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const DeviceKey k = DeviceKey::fromPersistedCameraKey(it.key());
            const QVariant &v = it.value();
            bool typed = false;
            if (k.isCamera()) {
                if (key == QLatin1String("camera/perspective")) {
                    if (const auto code = settingsInt(v)) { camera(k).setPerspectiveCode(*code); typed = true; }
                } else if (key == QLatin1String("camera/isMirrored")) {
                    if (const auto f = settingsFlag(v)) { camera(k).mirrored = *f; typed = true; }
                } else if (key == QLatin1String("camera/fixedInPlace")) {
                    if (const auto f = settingsFlag(v)) { camera(k).fixedInPlace = *f; typed = true; }
                } else if (key == QLatin1String("camera/ballRoi")) {
                    if (const auto b = settingsBox(v)) { camera(k).hittingArea = *b; typed = true; }
                } else if (key == QLatin1String("camera/alias")) {
                    if (v.typeId() == QMetaType::QString) {
                        if (!v.toString().isEmpty()) g.aliases.append({ k, v.toString() });
                        typed = true;
                    }
                }
            }
            if (!typed) keptEntries.insert(it.key(), settingsValueToJson(v));   // never dropped (decision 10)
        }
        if (!keptEntries.isEmpty()) g.kept.insert(key, QJsonObject{ { QStringLiteral("entries"), keptEntries } });
    }
    if (settings.contains(QLatin1String(kCameraList))) {
        const QVariant whole = settings.value(QLatin1String(kCameraList));
        // An emptied list reads back from an INI file as "@Invalid()": no value at all, the same as absent.
        if (!whole.isValid()) {
        } else if (whole.typeId() != QMetaType::QStringList && whole.typeId() != QMetaType::QString
            && whole.typeId() != QMetaType::QVariantList) {
            g.kept.insert(QLatin1String(kCameraList), QJsonObject{ { QStringLiteral("whole"), settingsValueToJson(whole) } });
        } else {
            // A one-entry list reads back from an INI file as a plain string; an empty one as an empty string.
            QVariantList entries;
            if (whole.typeId() == QMetaType::QString) {
                if (!whole.toString().isEmpty()) entries << whole;
            } else {
                entries = whole.toList();
            }
            QJsonArray keptList;
            for (const QVariant &e : entries) {
                const DeviceKey k = e.typeId() == QMetaType::QString ? DeviceKey::fromPersistedCameraKey(e.toString()) : DeviceKey();
                if (k.isCamera()) {
                    if (!g.excluded.contains(k)) g.excluded.append(k);
                } else {
                    keptList.append(settingsValueToJson(e));
                }
            }
            if (!keptList.isEmpty()) g.kept.insert(QLatin1String(kCameraList), QJsonObject{ { QStringLiteral("list"), keptList } });
        }
    }
    for (const CameraSetup &c : cams)
        if (c.perspectiveCode() != 0 || c.mirrored || c.fixedInPlace || c.hittingArea) g.cameras.append(c);
    return g;
}

QString fingerprintOf(const CameraGroup &g)
{
    using namespace canonical_detail;
    RigState state;
    state.cameras = g.cameras;
    QByteArray bytes = "pinpoint.rigGroup/cameras/1\n" + canonicalBytes(state);
    QByteArrayList aliases, excluded;
    for (const auto &a : g.aliases) aliases << str(a.first.persistedCameraKey()) + ' ' + str(a.second);
    for (const DeviceKey &k : g.excluded) excluded << str(k.persistedCameraKey());
    bytes += "aliases " + num(aliases.size()) + '\n' + sorted(aliases) + "\nexcluded " + num(excluded.size()) + '\n'
             + sorted(excluded) + "\nkept " + QJsonDocument(g.kept).toJson(QJsonDocument::Compact) + '\n';
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// The group into the Rig's two parts: the cameras' entries replaced (each one's calibration records carried over:
// they are not in settings), and the cameras' aliases and enabled flags replaced. Sensors are untouched.
void applyCameraGroup(const CameraGroup &g, RigParts &parts)
{
    QList<CameraSetup> next = g.cameras;
    for (const CameraSetup &old : parts.state.cameras) {
        if (old.calibrations.isEmpty()) continue;
        auto it = std::find_if(next.begin(), next.end(), [&](const CameraSetup &c) { return c.key() == old.key(); });
        if (it == next.end()) { next.append(CameraSetup(old.key())); it = next.end() - 1; }
        it->calibrations = old.calibrations;
    }
    parts.state.cameras = next;
    auto &aliases = parts.other.aliases;
    aliases.removeIf([](const auto &a) { return a.first.isCamera(); });
    aliases.append(g.aliases);
    auto &flags = parts.other.enabledOnThisMachine;
    flags.removeIf([](const auto &f) { return f.first.isCamera(); });
    for (const DeviceKey &k : g.excluded) flags.append({ k, false });
}

} // namespace

RigStore::RigStore(QString folder, QList<Group> groupsTheRigWrites)
    : m_folder(QDir::cleanPath(std::move(folder))), m_groups(std::move(groupsTheRigWrites)) {}

QString RigStore::groupName(Group g)
{
    switch (g) {
    case Group::Cameras: return QStringLiteral("cameras");
    }
    return {};
}

QString RigStore::settingsFingerprint(Group g)
{
    switch (g) {
    case Group::Cameras: return fingerprintOf(readCameraGroup());
    }
    return {};
}

QString RigStore::defaultFolder()
{
    const QString env = qEnvironmentVariable("PINPOINT_RIG_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/rig"));
}

QString RigStore::scratchCopyForProbe(const QString &folder)
{
    const QString scratch = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/pinpoint-role-probe-rig-%1")
                                                .arg(QCoreApplication::applicationPid()));
    if (!QDir().mkpath(scratch)) {
        ppWarn() << "[RigStore] the role probe's scratch rig folder cannot be made:" << scratch;
        return {};
    }
    const QString from = QDir::cleanPath(folder) + QLatin1Char('/') + kFileName;
    const QString to   = scratch + QLatin1Char('/') + kFileName;
    QFile::remove(to);
    if (QFileInfo::exists(from) && !QFile::copy(from, to))
        ppWarn() << "[RigStore] the machine's rig.json could not be copied into the probe's scratch folder:" << to;
    ppInfo() << "[RigStore] the camera role probe runs on a scratch copy of the rig:" << scratch;
    return scratch;
}

QString RigStore::filePath() const { return m_folder + QLatin1Char('/') + kFileName; }

QString RigStore::asidePath() const
{
    const QString stem = filePath() + QStringLiteral(".unreadable-")
                         + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    QString candidate = stem;
    for (int n = 2; QFileInfo::exists(candidate); ++n) candidate = stem + QStringLiteral("-%1").arg(n);
    return candidate;
}

bool RigStore::settingsHoldAnyKeyOf(Group g)
{
    const QSettings settings = ppSettings();   // read only
    switch (g) {
    case Group::Cameras:
        for (const char *k : kCameraMaps)
            if (settings.contains(QLatin1String(k))) return true;
        return settings.contains(QLatin1String(kCameraList));
    }
    return false;
}

RigLoad RigStore::load()
{
    RigLoad out;
    m_readOnly = false;
    m_loaded = m_groupsTaken = m_kept = QJsonObject();
    m_unreadCameras = m_unreadSensors = QJsonArray();
    m_unreadRecords.clear();
    m_unreadOther.clear();
    const QString path = filePath();
    if (!QFileInfo::exists(path)) {
        ppInfo() << "[RigStore] no rig.json in" << m_folder << ": an empty rig";
        copyGroups(out);   // Nothing: every group the Rig writes is copied (again, if the file was deleted)
        return out;
    }
    // P3 (the Solution Architect, 27 Nov 2026): a file this build cannot read at all is renamed
    // rig.json.unreadable-<UTC time> and kept, and the run starts as on a machine with nothing stored: a new RigId,
    // the groups copied from settings, saving as normal. If the rename fails, the file stays read-only, as before.
    auto unreadable = [&](const QString &why) {
        const QString aside = asidePath();
        if (QFile::rename(path, aside)) {
            ppWarn() << "[RigStore]" << path << why << ": this build cannot read it, so it is kept as"
                     << QFileInfo(aside).fileName() << "and the rig starts as on a machine with nothing stored"
                     << "(a new RigId)";
            out = RigLoad{};
            copyGroups(out);
            return out;
        }
        m_readOnly = true;
        ppWarn() << "[RigStore]" << path << why << ": this build cannot read it, and could not move it aside as"
                 << QFileInfo(aside).fileName() << ", so it runs an empty rig and never saves over the file";
        out.source = RigLoad::Source::ReadOnly;
        return out;
    };
    QByteArray bytes;
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return unreadable(QStringLiteral("cannot be opened (%1)").arg(f.errorString()));
        bytes = f.readAll();
    }   // closed before any rename
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return unreadable(QStringLiteral("is not a JSON object (%1)").arg(err.errorString()));
    const QJsonObject root = doc.object();
    static const QRegularExpression schemaRe(QStringLiteral(R"(^pinpoint\.rig/([1-9][0-9]*)$)"));
    const auto m = schemaRe.match(root.value(QStringLiteral("schema")).toString());
    if (!m.hasMatch()) return unreadable(QStringLiteral("has no pinpoint.rig schema line"));
    const int   version = m.captured(1).toInt();
    const RigId id      = RigId::fromString(root.value(QStringLiteral("rigId")).toString());
    // A newer schema's file is read-only whatever it holds, a rigId this build cannot read included: moving it aside
    // would lose the later build's facts (P3).
    if (id.isNull() && version <= kSchemaVersion) return unreadable(QStringLiteral("has no valid rigId"));

    m_loaded      = root;
    m_groupsTaken = root.value(QStringLiteral("groupsTaken")).toObject();
    m_kept        = root.value(QStringLiteral("keptFromSettings")).toObject();
    out.id        = id;
    const QJsonObject state = root.value(QStringLiteral("state")).toObject();
    Unread unread;
    out.parts.state.sinceUtc = timeFromJson(state.value(QStringLiteral("sinceUtc")));
    out.parts.state.cameras  = camerasFromJson(state.value(QStringLiteral("cameras")).toArray(), unread);
    out.parts.state.sensors  = sensorsFromJson(state.value(QStringLiteral("sensors")).toArray(), unread);
    m_unreadCameras = unread.cameras;
    m_unreadRecords = unread.records;
    out.parts.other          = otherFromJson(root.value(QStringLiteral("other")).toObject(), unread);
    m_unreadSensors = unread.sensors;
    m_unreadOther   = unread.other;
    if (version > kSchemaVersion) {
        m_readOnly = true;
        out.source = RigLoad::Source::ReadOnly;
        ppWarn() << "[RigStore]" << path << "is" << schemaLine(version) << ", newer than this build's"
                 << schemaLine(kSchemaVersion) << ": the rig runs from the fields this build knows, and the file is"
                 << "read-only to this build (never saved over)";
        return out;
    }
    out.source = RigLoad::Source::File;
    copyGroups(out);
    return out;
}

void RigStore::copyGroups(RigLoad &out)
{
    for (const Group g : m_groups) {
        const QString name = groupName(g);
        // P1 (the Solution Architect, 27 Nov 2026): when none of the group's keys exists in settings (the settings
        // file reset or deleted, or a later stage removing them), the copy leaves the Rig as it is: no copy, no wipe.
        // A group with at least one key still copies again when its fingerprint differs.
        if (!settingsHoldAnyKeyOf(g)) {
            if (!m_groupsTaken.value(name).toObject().isEmpty())
                ppInfo() << "[RigStore] no" << name << "key in settings: the rig's" << name << "are left as they are";
            continue;
        }
        CameraGroup   decoded;
        switch (g) {
        case Group::Cameras: decoded = readCameraGroup(); break;
        }
        const QString fingerprint = fingerprintOf(decoded);
        const QJsonObject taken   = m_groupsTaken.value(name).toObject();
        if (out.source == RigLoad::Source::File && taken.value(QStringLiteral("fingerprint")).toString() == fingerprint)
            continue;   // taken, and nothing changed since: nothing happens
        if (out.source == RigLoad::Source::Nothing)
            ppInfo() << "[RigStore] no rig.json: the" << name << "keys are copied from settings (a new RigId)";
        else if (taken.isEmpty())
            ppInfo() << "[RigStore] the" << name << "keys are taken from settings into rig.json";
        else
            ppWarn() << "[RigStore] the" << name << "keys in settings no longer match rig.json's copy: an older build"
                     << "changed them, so the whole group is copied again (the last writer wins for the group)";
        applyCameraGroup(decoded, out.parts);
        // The group's kept entries replace its earlier ones; another group's stay.
        for (const char *k : kCameraMaps) m_kept.remove(QLatin1String(k));
        m_kept.remove(QLatin1String(kCameraList));
        for (auto it = decoded.kept.begin(); it != decoded.kept.end(); ++it) m_kept.insert(it.key(), it.value());
        if (!decoded.kept.isEmpty())
            ppInfo() << "[RigStore]" << decoded.kept.size() << "camera settings kept verbatim (keys or values that fit no"
                     << "device; matched to no camera)";
        m_groupsTaken.insert(name, QJsonObject{ { QStringLiteral("fingerprint"), fingerprint },
                                                { QStringLiteral("takenUtc"), timeToJson(QDateTime::currentDateTimeUtc()) } });
        out.saveNeeded = true;
    }
}

bool RigStore::save(const RigId &id, const RigParts &parts)
{
    const QString path = filePath();
    if (m_readOnly) {
        ppWarn() << "[RigStore] not saved:" << path << "is read-only to this build (a newer schema, or unreadable)";
        return false;
    }
    if (id.isNull()) {
        ppWarn() << "[RigStore] not saved: no RigId";
        return false;
    }
    QJsonObject root  = m_loaded;   // what this build does not know is written back as it was read
    QJsonObject state = m_loaded.value(QStringLiteral("state")).toObject();
    state.insert(QStringLiteral("sinceUtc"), timeToJson(parts.state.sinceUtc));
    Unread unread;   // what the file held that this build cannot read: written back as read (P2, widened)
    unread.cameras = m_unreadCameras;
    unread.records = m_unreadRecords;
    // Each entry this build writes goes over the entry as read, so a field it does not know stays (`[^rig-unknown]`).
    const QJsonArray readCameras = state.value(QStringLiteral("cameras")).toArray();
    QHash<QString, QJsonArray> readRecords;   // by camera key
    for (const QJsonValue &v : readCameras)
        readRecords.insert(cameraIdentity(v.toObject()), v.toObject().value(QStringLiteral("calibrations")).toArray());
    QJsonArray cameras = mergedOverRead(camerasToJson(parts.state.cameras), readCameras, cameraIdentity,
                                        { "key", "perspective", "mirrored", "fixedInPlace", "hittingArea", "calibrations" });
    for (int i = 0; i < cameras.size(); ++i) {
        QJsonObject c = cameras.at(i).toObject();
        c.insert(QStringLiteral("calibrations"),
                 mergedOverRead(c.value(QStringLiteral("calibrations")).toArray(), readRecords.value(cameraIdentity(c)),
                                recordIdentity,
                                { "subject", "kind", "method", "parameters", "uncertainty", "observedAtUtc", "evidence" }));
        cameras.replace(i, c);
    }
    QJsonArray sensors = mergedOverRead(sensorsToJson(parts.state.sensors), state.value(QStringLiteral("sensors")).toArray(),
                                        sensorIdentity, { "placement", "role" });
    for (const QJsonValue &v : m_unreadSensors) sensors.append(v);
    state.insert(QStringLiteral("cameras"), withUnreadCameras(cameras, unread));
    state.insert(QStringLiteral("sensors"), sensors);
    root.insert(QStringLiteral("schema"), schemaLine(kSchemaVersion));
    root.insert(QStringLiteral("rigId"), id.toString());
    root.insert(QStringLiteral("state"), state);
    {
        const QJsonObject readOther = m_loaded.value(QStringLiteral("other")).toObject();
        QJsonObject other = otherToJson(parts.other, readOther);
        const struct { const char *list; QString (*identity)(const QJsonObject &); QList<const char *> known; } lists[] = {
            { "aliases", deviceIdentity, { "camera", "sensor", "alias" } },
            { "enabledOnThisMachine", deviceIdentity, { "camera", "sensor", "enabled" } },
            { "clubLengthPriors", priorIdentity, { "athlete", "club", "faceOnCamera", "value" } },
        };
        for (const auto &l : lists) {
            const QString name = QLatin1String(l.list);
            QJsonArray a = mergedOverRead(other.value(name).toArray(), readOther.value(name).toArray(), l.identity, l.known);
            for (const QJsonValue &v : m_unreadOther.value(name)) a.append(v);   // kept as read (B1)
            other.insert(name, a);
        }
        root.insert(QStringLiteral("other"), other);
    }
    root.insert(QStringLiteral("groupsTaken"), m_groupsTaken);
    root.insert(QStringLiteral("keptFromSettings"), m_kept);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (const AtomicWriteResult w = atomicWrite(path, bytes); !w.ok) {
        ppWarn() << "[RigStore] not saved:" << path << w.error << w.detail
                 << "; the rig in memory stands and the next change saves it again";
        return false;
    }
    m_loaded = root;
    return true;
}
