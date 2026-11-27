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

// device_key_test — DeviceKey and PlacementKey (src/Vocabulary/Rig/device_key.h; rig_design.md §4.6;
// information_model.md §5.2 ID-DEVICE-KEY, ID-PLACEMENT-KEY) spell every persisted device key exactly
// as today's code does, byte for byte, so no setting is migrated or lost.
//
// ⚠ THE FIXTURE IS NEVER BUILT WITH THE NEW TYPES. Every key it stores is spelled by today's code:
// literals, the open-coded expressions copied from their sites (the camera key; camera_roi.h's key and
// tuningKey as they were before C-1b; imu_roles::aliasKey and capture_context.cpp's prior key as they
// were before C-1c, which spell through the types now), and hm_unit_id::unitIdFor. It is
// written through the REAL AppSettings into a scratch store, read back by a second AppSettings, and only
// then looked up with the typed keys; the six camera keys the Rig took over (platform refactor C-5b) and the
// club-length prior (C-7), which AppSettings no longer holds, are written and read raw, whole maps, as the last
// build before it wrote them.
// A test that built the fixture with the types would prove nothing.
//
// Devices: two cameras (one with a serial, one without, whose device id is a Windows path full of '#'),
// a Witmotion and a HackMotion wG3.

#include "app/app_settings.h"
#include "cameras/camera_roi.h"
#include "../../IMU/imu_role_map.h"
#include "../../IMU/hm_unit_id.h"
#include "../../Vocabulary/Rig/device_key.h"
#include "../../Vocabulary/Rig/club_length_prior_key.h"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QSettings>

#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using pinpoint::rig::DeviceKey;
using pinpoint::rig::PlacementKey;
using pinpoint::rig::ClubLengthPriorKey;
using pinpoint::AthleteId;
using CropEntry = DeviceKey::CropEntry;
using TuningEntry = DeviceKey::TuningEntry;
using Lane      = PlacementKey::Lane;
namespace hmu   = pinpoint::hm_unit_id;
// The impact camera's code, as the settings store holds it: the one perspective type (perspective.h).
constexpr int kImpact = int(pinpoint::recording::Perspective::Impact);

static int g_fail = 0;
static int g_run  = 0;

// Quiet on success: a FAIL line names the case, the summary counts them.
static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", qPrintable(label));
        ++g_fail;
    }
}
static void same(const QString &label, const QString &got, const QString &want)
{
    // QString equality is code-unit equality; the UTF-8 comparison states the "byte for byte" claim.
    check(label + QStringLiteral(" (got \"") + got + QStringLiteral("\", want \"") + want + QStringLiteral("\")"),
          got == want && got.toUtf8() == want.toUtf8() && got.isNull() == want.isNull());
}

// ── The fixture's devices, as the enumerator reports them ─────────────────────────────────────────

struct Cam { QString description, serial, id; };
struct Imu { QString description, id; };

static const Cam kCamA{ QStringLiteral("Chameleon3 CM3-U3-13Y3C"), QStringLiteral("18277032"),
                        QStringLiteral("Spinnaker:1") };
static const Cam kCamB{ QStringLiteral("HD Pro Webcam C920"), QString(),
                        QStringLiteral(R"(\\?\usb#vid_046d&pid_082d&mi_00#7&1b2a3c4d&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\global)") };
static const Imu kWit{ QStringLiteral("WT901BLE68"), QStringLiteral("{767a6a67-1c2d-4e5f-8a9b-0c1d2e3f4a5b}") };
static const Imu kHm { QStringLiteral("HackMotion wG3"), QStringLiteral("{0b1c2d3e-4f5a-4b6c-9d7e-8f9a0b1c2d3e}") };

static const QString kAthlete = QStringLiteral("5b0c7f1e-2a3d-4c5b-8e6f-7a8b9c0d1e2f");
static const QString kClub    = QStringLiteral("7i");

// Today's camera key, as it was open-coded identically on 68d7461e at camera_manager.h:216
// (CameraManager::cameraKey), camera_instance.cpp:122 and :925 (CameraInstance::cameraKey),
// camera_manager.cpp:969, and resource_monitor_controller.cpp:196 (C-1b spells them through the type).
// Copied here so the test holds today's rule, not the type's.
static QString todaysCameraKey(const Cam &c)
{
    return c.description + QLatin1Char('|') + (c.serial.isEmpty() ? c.id : c.serial);
}

// imu/alias's key, as imu_roles::aliasKey open-coded it before C-1c (imu_role_map.h:82 on 8273d1cf;
// shot_processor.cpp:1005 spelled the same), and the seed imu_manager.cpp:183 gave a newly seen sensor:
// that key with every '|' a space.
static QString todaysAliasKey(const QString &description, const QString &deviceId)
{
    return description + QLatin1Char('|') + deviceId;
}
static QString todaysSeededAlias(const QString &description, const QString &deviceId)
{
    return QString(todaysAliasKey(description, deviceId)).replace(QLatin1Char('|'), QLatin1Char(' '));
}

// camera_roi.h's key() and tuningKey() as they were on 68d7461e, before C-1b spelled them through the
// type. Copied so the fixture keeps today's rule. NOTE the two suffixes go opposite ways: the crop's
// impact strip is the one suffixed; for exposure and gain it is every role BUT Impact.
static QString todaysRoiKey(const QString &cameraKey, int perspective)
{
    return perspective == kImpact ? cameraKey + QStringLiteral("#impact") : cameraKey;
}
static QString todaysTuningKey(const QString &cameraKey, int perspective)
{
    return perspective == kImpact ? cameraKey : cameraKey + QStringLiteral("#wide");
}

// SET-05's key, as capture_context.cpp:348 built it on 8273d1cf, before C-1c:
//   athlete.uuid + QLatin1Char('|') + athlete.sessionClub + QLatin1Char('|') + faceOnKey
static QString todaysPriorKey(const QString &athleteUuid, const QString &club, const QString &faceOnKey)
{
    return athleteUuid + QLatin1Char('|') + club + QLatin1Char('|') + faceOnKey;
}
static QString todaysPriorKey(const QString &faceOnKey) { return todaysPriorKey(kAthlete, kClub, faceOnKey); }

static DeviceKey camKey(const Cam &c) { return DeviceKey::camera(c.description, c.serial, c.id); }
static DeviceKey imuKey(const Imu &i) { return DeviceKey::sensor(i.description, i.id); }

static QVariantMap box(double x, double y, double w, double h)
{
    return { { "x", x }, { "y", y }, { "w", w }, { "h", h } };
}

// ── 1. The ten spellings of §4.6, against literals and today's functions ──────────────────────────

static void testSpellings()
{
    const DeviceKey a = camKey(kCamA), b = camKey(kCamB), w = imuKey(kWit), h = imuKey(kHm);

    // 1. A camera with a serial: description|serial (camera/perspective, isMirrored, fixedInPlace,
    //    ballRoi, alias, excluded).
    same("1 camera with serial", a.persistedCameraKey(), QStringLiteral("Chameleon3 CM3-U3-13Y3C|18277032"));
    same("1 camera with serial = today", a.persistedCameraKey(), todaysCameraKey(kCamA));
    // 2. A camera without one: description|device id.
    same("2 camera without serial", b.persistedCameraKey(), QStringLiteral("HD Pro Webcam C920|") + kCamB.id);
    same("2 camera without serial = today", b.persistedCameraKey(), todaysCameraKey(kCamB));
    // 3. camera/roi, the shared crop: the camera key itself.
    same("3 roi shared", a.persistedRoiKey(CropEntry::Shared), todaysRoiKey(todaysCameraKey(kCamA), 2));
    same("3 roi shared (no serial)", b.persistedRoiKey(CropEntry::Shared), todaysRoiKey(todaysCameraKey(kCamB), 1));
    // 4. camera/roi, the impact strip: the camera key + "#impact".
    same("4 roi impact", a.persistedRoiKey(CropEntry::ImpactStrip),
         QStringLiteral("Chameleon3 CM3-U3-13Y3C|18277032#impact"));
    same("4 roi impact = today", a.persistedRoiKey(CropEntry::ImpactStrip),
         todaysRoiKey(todaysCameraKey(kCamA), kImpact));
    same("4 roi impact = today (no serial)", b.persistedRoiKey(CropEntry::ImpactStrip),
         todaysRoiKey(todaysCameraKey(kCamB), kImpact));
    // 4b. camera/exposureUs, camera/tuning: the camera key for Impact, "#wide" for every other role
    //     (camera_roi.h tuningKey; §4.6's list omitted it, PR-52).
    same("4b tuning other roles", a.persistedTuningKey(TuningEntry::OtherRoles),
         QStringLiteral("Chameleon3 CM3-U3-13Y3C|18277032#wide"));
    same("4b tuning impact role", a.persistedTuningKey(TuningEntry::ImpactRole), todaysCameraKey(kCamA));
    for (int p : { 0, 1, 2, 3, kImpact }) {
        const TuningEntry e = p == kImpact ? TuningEntry::ImpactRole : TuningEntry::OtherRoles;
        same(QStringLiteral("4b tuning = today p%1").arg(p), a.persistedTuningKey(e), todaysTuningKey(todaysCameraKey(kCamA), p));
        same(QStringLiteral("4b tuning = today (no serial) p%1").arg(p), b.persistedTuningKey(e),
             todaysTuningKey(todaysCameraKey(kCamB), p));
    }
    // 5. imu/alias: description|deviceId.
    same("5 imu alias", w.persistedWithDescription(), QStringLiteral("WT901BLE68|{767a6a67-1c2d-4e5f-8a9b-0c1d2e3f4a5b}"));
    same("5 imu alias = today", w.persistedWithDescription(), todaysAliasKey(kWit.description, kWit.id));
    same("5 imu alias = today (wG3)", h.persistedWithDescription(), todaysAliasKey(kHm.description, kHm.id));
    // 6. imu/excluded: the bare device id.
    same("6 imu excluded", w.persistedId(), kWit.id);
    same("6 imu excluded (wG3)", h.persistedId(), kHm.id);
    // 7. imu/roles, a Witmotion: the bare device id.
    same("7 roles Witmotion", PlacementKey(w).persisted(), kWit.id);
    // 8. imu/roles, the wG3 lower-arm lane.
    same("8 roles lowerArm", PlacementKey(h, Lane::LowerArm).persisted(), kHm.id + QStringLiteral("#lowerArm"));
    same("8 roles lowerArm = hm_unit_id", PlacementKey(h, Lane::LowerArm).persisted(), hmu::unitIdFor(kHm.id, hmu::kLowerArm));
    // 9. imu/roles, the wG3 palm lane.
    same("9 roles palm", PlacementKey(h, Lane::Palm).persisted(), kHm.id + QStringLiteral("#palm"));
    same("9 roles palm = hm_unit_id", PlacementKey(h, Lane::Palm).persisted(), hmu::unitIdFor(kHm.id, hmu::kPalm));
    // 10. SET-05: athlete|club|<the face-on camera's key>.
    same("10 club-length prior", todaysPriorKey(a.persistedCameraKey()),
         kAthlete + QStringLiteral("|7i|Chameleon3 CM3-U3-13Y3C|18277032"));

    // The two lane names are hm_unit_id.h's (the Vocabulary header may not include it).
    same("lane lowerArm = hm_unit_id", PlacementKey::laneSuffix(Lane::LowerArm), hmu::suffix(hmu::kLowerArm));
    same("lane palm = hm_unit_id", PlacementKey::laneSuffix(Lane::Palm), hmu::suffix(hmu::kPalm));
    same("lane none", PlacementKey::laneSuffix(Lane::None), QString());

    // Today's expression keeps its shape whatever the parts hold: no description still spells "|<id>".
    const Cam bare{ QString(), QString(), QStringLiteral("0x8020000005ac8514") };
    same("camera, empty description = today", camKey(bare).persistedCameraKey(), todaysCameraKey(bare));
    same("sensor, empty description = today", DeviceKey::sensor(QString(), kWit.id).persistedWithDescription(),
         todaysAliasKey(QString(), kWit.id));

    // A spelling of another kind, or of the null key, is the null string.
    same("camera has no sensor spelling", a.persistedWithDescription(), QString());
    same("camera has no bare id", a.persistedId(), QString());
    same("sensor has no camera spelling", w.persistedCameraKey(), QString());
    same("sensor has no roi spelling", w.persistedRoiKey(CropEntry::ImpactStrip), QString());
    same("sensor has no tuning spelling", w.persistedTuningKey(TuningEntry::OtherRoles), QString());
    same("null camera spelling", DeviceKey().persistedCameraKey(), QString());
    same("null placement", PlacementKey().persisted(), QString());
    check("a camera holds no role", PlacementKey(a, Lane::Palm).isNull());
}

// ── 2. Parse then spell is the identity; spell then parse gives the key back ──────────────────────

static void testRoundTrips()
{
    const QStringList cameraTexts{ todaysCameraKey(kCamA), todaysCameraKey(kCamB), QStringLiteral("|x"),
                                   QStringLiteral("a|b|c"), QStringLiteral("model|"), QStringLiteral("m|id#impact#x") };
    for (const QString &s : cameraTexts)
        same("camera parse→spell " + s, DeviceKey::fromPersistedCameraKey(s).persistedCameraKey(), s);
    check("camera parse: no '|' is null", DeviceKey::fromPersistedCameraKey(u"no separator").isNull());
    check("camera parse: description split at the last '|'",
          DeviceKey::fromPersistedCameraKey(u"a|b|c").description() == QStringLiteral("a|b"));

    for (const QString &base : { todaysCameraKey(kCamA), todaysCameraKey(kCamB) }) {
        for (int p : { 0, 1, 2, 3, kImpact }) {
            const QString s = todaysRoiKey(base, p);
            CropEntry e = CropEntry::Shared;
            const DeviceKey k = DeviceKey::fromPersistedRoiKey(s, &e);
            same(QStringLiteral("roi parse→spell p%1 ").arg(p) + s, k.persistedRoiKey(e), s);
            check(QStringLiteral("roi entry p%1").arg(p),
                  (e == CropEntry::ImpactStrip) == (p == kImpact));
            check(QStringLiteral("roi camera p%1").arg(p), k == DeviceKey::fromPersistedCameraKey(base));

            const QString t = todaysTuningKey(base, p);
            TuningEntry te = TuningEntry::ImpactRole;
            const DeviceKey tk = DeviceKey::fromPersistedTuningKey(t, &te);
            same(QStringLiteral("tuning parse→spell p%1 ").arg(p) + t, tk.persistedTuningKey(te), t);
            check(QStringLiteral("tuning entry p%1").arg(p),
                  (te == TuningEntry::ImpactRole) == (p == kImpact));
            check(QStringLiteral("tuning camera p%1").arg(p), tk == DeviceKey::fromPersistedCameraKey(base));
        }
    }

    for (const Imu &i : { kWit, kHm }) {
        const QString wd = todaysAliasKey(i.description, i.id);
        same("sensor parse→spell " + wd, DeviceKey::fromPersistedWithDescription(wd).persistedWithDescription(), wd);
        same("sensor bare parse→spell", DeviceKey::fromPersistedId(i.id).persistedId(), i.id);
        check("sensor spell→parse (with description)",
              DeviceKey::fromPersistedWithDescription(imuKey(i).persistedWithDescription()) == imuKey(i));
        check("sensor spell→parse keeps the description",
              DeviceKey::fromPersistedWithDescription(imuKey(i).persistedWithDescription()).description() == i.description);
        check("sensor spell→parse (bare id)", DeviceKey::fromPersistedId(imuKey(i).persistedId()) == imuKey(i));
    }
    check("sensor parse: no '|' is null", DeviceKey::fromPersistedWithDescription(u"x").isNull());
    check("bare parse: empty is null", DeviceKey::fromPersistedId(u"").isNull());

    for (const Cam &c : { kCamA, kCamB })
        check("camera spell→parse", DeviceKey::fromPersistedCameraKey(camKey(c).persistedCameraKey()) == camKey(c));

    // PlacementKey reads a key exactly as hm_unit_id::parse does: the same lane or none, the same id.
    const QStringList placements{ kWit.id, hmu::unitIdFor(kHm.id, hmu::kLowerArm), hmu::unitIdFor(kHm.id, hmu::kPalm),
                                  QStringLiteral("#palm"), QStringLiteral("#lowerArm"), QStringLiteral("x#foo"),
                                  QStringLiteral("x#palm#lowerArm"), QStringLiteral("x#lowerArm#palm"),
                                  QStringLiteral("x#Palm"), QStringLiteral("x#palm "), QStringLiteral("x#"),
                                  QStringLiteral("a#b#palm"), QStringLiteral("plain") };
    for (const QString &s : placements) {
        const PlacementKey k = PlacementKey::fromPersisted(s);
        same("placement parse→spell " + s, k.persisted(), s);
        QString id; int unit = -1;
        const bool laned = hmu::parse(s, &id, &unit);
        check("placement lane agrees with hm_unit_id " + s, k.hasLane() == laned);
        if (laned) {
            check("placement unit " + s, k.lane() == (unit == hmu::kPalm ? Lane::Palm : Lane::LowerArm));
            same("placement id " + s, k.device().persistedId(), id);
        } else {
            same("placement bare id " + s, k.device().persistedId(), s);
        }
    }
    check("placement parse: empty is null", PlacementKey::fromPersisted(u"").isNull());
    for (Lane l : { Lane::None, Lane::LowerArm, Lane::Palm }) {
        const PlacementKey k(imuKey(kHm), l);
        check("placement spell→parse", PlacementKey::fromPersisted(k.persisted()) == k);
    }
}

// ── 3. Hashable, ordered, with a null ──────────────────────────────────────────────────────────────

static void testIdentity()
{
    check("default DeviceKey is null", DeviceKey().isNull() && DeviceKey() == DeviceKey());
    check("default PlacementKey is null", PlacementKey().isNull());
    check("a built key is not null", !camKey(kCamA).isNull() && !imuKey(kWit).isNull());

    // A sensor is identified by its device id: the key read from imu/roles (no description) is the
    // enumerator's key for the same sensor.
    check("sensor: bare-id key equals the described key", DeviceKey::fromPersistedId(kWit.id) == imuKey(kWit));
    check("sensor: equal keys hash equal", qHash(DeviceKey::fromPersistedId(kWit.id)) == qHash(imuKey(kWit)));
    check("sensor: different ids differ", imuKey(kWit) != imuKey(kHm));
    // A camera is identified by its description and its identifier.
    check("camera: same serial, other model differs",
          DeviceKey::camera(QStringLiteral("Blackfly S"), kCamA.serial, kCamA.id) != camKey(kCamA));
    check("camera: serial wins over device id",
          DeviceKey::camera(kCamA.description, kCamA.serial, QStringLiteral("other")) == camKey(kCamA));
    check("camera and sensor with the same text differ",
          DeviceKey::camera(kWit.description, QString(), kWit.id) != imuKey(kWit));
    check("placement lanes differ", PlacementKey(imuKey(kHm), Lane::LowerArm) != PlacementKey(imuKey(kHm), Lane::Palm));

    const QList<DeviceKey> keys{ camKey(kCamA), camKey(kCamB), imuKey(kWit), imuKey(kHm), DeviceKey() };
    for (const DeviceKey &x : keys)
        for (const DeviceKey &y : keys) {
            const int lt = (x < y) + (y < x);
            check("order is strict and total", (x == y) ? lt == 0 : lt == 1);
        }
    QSet<DeviceKey> set(keys.cbegin(), keys.cend());
    check("QSet holds each key once", set.size() == keys.size());
    QMap<PlacementKey, int> placed{ { PlacementKey(imuKey(kWit)), 1 }, { PlacementKey(imuKey(kHm), Lane::LowerArm), 2 },
                                    { PlacementKey(imuKey(kHm), Lane::Palm), 3 } };
    check("QMap keyed by PlacementKey: three entries", placed.size() == 3);
    check("QMap finds a parsed key", placed.value(PlacementKey::fromPersisted(hmu::unitIdFor(kHm.id, hmu::kPalm))) == 3);
    QHash<PlacementKey, int> hashed;
    for (auto it = placed.cbegin(); it != placed.cend(); ++it) hashed.insert(it.key(), it.value());
    check("QHash finds a parsed key", hashed.value(PlacementKey::fromPersisted(kWit.id)) == 1);
}

// ── 4. A settings store written by today's code resolves unchanged ─────────────────────────────────

static void testSettingsFixture()
{
    const QString camA = todaysCameraKey(kCamA), camB = todaysCameraKey(kCamB);
    const QString witAlias = todaysAliasKey(kWit.description, kWit.id);
    const QString hmAlias  = todaysAliasKey(kHm.description, kHm.id);
    const QString hmLower  = hmu::unitIdFor(kHm.id, hmu::kLowerArm), hmPalm = hmu::unitIdFor(kHm.id, hmu::kPalm);
    const QString prior    = todaysPriorKey(camA);   // camera A is the fixed face-on camera
    {
        // The six camera keys the Rig took over (C-5b) as the last build before it wrote them: whole maps, one
        // setValue each, through the settings store. Nothing writes them now; they are kept for the copy (SET-09).
        {
            QSettings raw = ppSettings();
            raw.setValue(QStringLiteral("camera/perspective"), QVariantMap{ { camA, 2 }, { camB, 1 } });
            raw.setValue(QStringLiteral("camera/isMirrored"), QVariantMap{ { camA, false }, { camB, true } });
            raw.setValue(QStringLiteral("camera/fixedInPlace"), QVariantMap{ { camA, true }, { camB, false } });
            raw.setValue(QStringLiteral("camera/ballRoi"), QVariantMap{ { camA, box(0.4, 0.6, 0.1, 0.1) } });
            raw.setValue(QStringLiteral("camera/alias"), QVariantMap{ { camA, QStringLiteral("Face-on") }, { camB, QStringLiteral("Webcam") } });
            raw.setValue(QStringLiteral("camera/excluded"), QStringList{ camB });
            // SET-05, which the Rig took over in C-7, as the last build before it wrote it: one whole map.
            raw.setValue(QStringLiteral("analysis/clubLenPrior"),
                         QVariantMap{ { prior, QVariantMap{ { "lenM", 0.94 }, { "frameW", 1280 }, { "frameH", 1024 } } } });
        }
        AppSettings s;   // today's writer of the rest
        s.setCameraRoi({ { todaysRoiKey(camA, 2), box(0.0, 0.0, 1.0, 1.0) },
                         { todaysRoiKey(camA, kImpact), box(0.25, 0.4, 0.5, 0.234375) },
                         { todaysRoiKey(camB, 1), box(0.2, 0.0, 0.6, 1.0) } });
        s.setCameraExposureUs({ { todaysTuningKey(camA, kImpact), 100 },
                                { todaysTuningKey(camA, 1), 4000 },
                                { todaysTuningKey(camB, 2), 8000 } });
        s.setCameraTuning({ { todaysTuningKey(camA, kImpact), QVariantMap{ { "gain", 12.0 } } },
                            { todaysTuningKey(camB, 2), QVariantMap{ { "gain", 3.0 } } } });
        s.setImuAlias({ { witAlias, QStringLiteral("Pelvis sensor") }, { hmAlias, QStringLiteral("Lead wrist") } });
        s.setImuExcluded({ kWit.id });
        s.setImuRoles({ { kWit.id, pinpoint::imu_roles::pelvis() },
                        { hmLower, pinpoint::imu_roles::leadForearm() },
                        { hmPalm, pinpoint::imu_roles::leadHand() } });
    }

    AppSettings r;   // today's reader, a fresh load from the scratch store
    const QSettings raw = ppSettings();   // the Rig's six camera keys, read as the copy reads them
    const auto rawMap = [&raw](const char *k) { return raw.value(QLatin1String(k)).toMap(); };
    const DeviceKey a = camKey(kCamA), b = camKey(kCamB), w = imuKey(kWit), h = imuKey(kHm);

    // Each map holds exactly the keys the types spell: the same strings, not merely the same lookups.
    auto keysAre = [](const QString &label, QStringList got, QStringList want) {
        got.sort(); want.sort();
        check(label + QStringLiteral(": stored keys = typed spellings"), got == want);
    };
    keysAre("camera/perspective", rawMap("camera/perspective").keys(), { a.persistedCameraKey(), b.persistedCameraKey() });
    keysAre("camera/isMirrored", rawMap("camera/isMirrored").keys(), { a.persistedCameraKey(), b.persistedCameraKey() });
    keysAre("camera/fixedInPlace", rawMap("camera/fixedInPlace").keys(), { a.persistedCameraKey(), b.persistedCameraKey() });
    keysAre("camera/ballRoi", rawMap("camera/ballRoi").keys(), { a.persistedCameraKey() });
    keysAre("camera/alias", rawMap("camera/alias").keys(), { a.persistedCameraKey(), b.persistedCameraKey() });
    keysAre("camera/excluded", raw.value(QStringLiteral("camera/excluded")).toStringList(), { b.persistedCameraKey() });
    keysAre("camera/roi", r.cameraRoi().keys(), { a.persistedRoiKey(CropEntry::Shared), a.persistedRoiKey(CropEntry::ImpactStrip),
                                                  b.persistedRoiKey(CropEntry::Shared) });
    keysAre("camera/exposureUs", r.cameraExposureUs().keys(),
            { a.persistedTuningKey(TuningEntry::ImpactRole), a.persistedTuningKey(TuningEntry::OtherRoles),
              b.persistedTuningKey(TuningEntry::OtherRoles) });
    keysAre("camera/tuning", r.cameraTuning().keys(),
            { a.persistedTuningKey(TuningEntry::ImpactRole), b.persistedTuningKey(TuningEntry::OtherRoles) });
    keysAre("imu/alias", r.imuAlias().keys(), { w.persistedWithDescription(), h.persistedWithDescription() });
    keysAre("imu/excluded", r.imuExcluded(), { w.persistedId() });
    keysAre("imu/roles", r.imuRoles().keys(), { PlacementKey(w).persisted(), PlacementKey(h, Lane::LowerArm).persisted(),
                                                PlacementKey(h, Lane::Palm).persisted() });
    const QString typedPrior = ClubLengthPriorKey(AthleteId::fromString(kAthlete), kClub, a).persisted();
    keysAre("analysis/clubLenPrior", rawMap("analysis/clubLenPrior").keys(), { typedPrior });

    // And each value resolves through the typed key.
    check("perspective A", rawMap("camera/perspective").value(a.persistedCameraKey()).toInt() == 2);
    check("perspective B", rawMap("camera/perspective").value(b.persistedCameraKey()).toInt() == 1);
    check("mirrored B", rawMap("camera/isMirrored").value(b.persistedCameraKey()).toBool());
    check("fixed A", rawMap("camera/fixedInPlace").value(a.persistedCameraKey()).toBool());
    check("ball box A", rawMap("camera/ballRoi").value(a.persistedCameraKey()).toMap().value("x").toDouble() == 0.4);
    check("alias A", rawMap("camera/alias").value(a.persistedCameraKey()).toString() == QStringLiteral("Face-on"));
    check("alias B", rawMap("camera/alias").value(b.persistedCameraKey()).toString() == QStringLiteral("Webcam"));
    const QStringList excluded = raw.value(QStringLiteral("camera/excluded")).toStringList();
    check("excluded B", excluded.contains(b.persistedCameraKey()) && !excluded.contains(a.persistedCameraKey()));
    check("impact strip A", pp_camroi::cropFor(r.cameraRoi(), a, kImpact)
                                == QRectF(0.25, 0.4, 0.5, 0.234375));
    check("impact strip A, typed", r.cameraRoi().value(a.persistedRoiKey(CropEntry::ImpactStrip)).toMap().value("h").toDouble() == 0.234375);
    check("shared crop B, typed", r.cameraRoi().value(b.persistedRoiKey(CropEntry::Shared)).toMap().value("w").toDouble() == 0.6);
    check("exposure A impact", r.cameraExposureUs().value(a.persistedTuningKey(TuningEntry::ImpactRole)).toInt() == 100);
    check("exposure A other", r.cameraExposureUs().value(a.persistedTuningKey(TuningEntry::OtherRoles)).toInt() == 4000);
    check("exposure B other", r.cameraExposureUs().value(b.persistedTuningKey(TuningEntry::OtherRoles)).toInt() == 8000);
    check("gain A impact", r.cameraTuning().value(a.persistedTuningKey(TuningEntry::ImpactRole)).toMap().value("gain").toDouble() == 12.0);
    check("gain B other", r.cameraTuning().value(b.persistedTuningKey(TuningEntry::OtherRoles)).toMap().value("gain").toDouble() == 3.0);
    check("imu alias Witmotion", r.imuAlias().value(w.persistedWithDescription()).toString() == QStringLiteral("Pelvis sensor"));
    check("imu alias wG3", r.imuAlias().value(h.persistedWithDescription()).toString() == QStringLiteral("Lead wrist"));
    check("imu excluded", r.imuExcluded().contains(w.persistedId()) && !r.imuExcluded().contains(h.persistedId()));
    check("role Witmotion", r.imuRoles().value(PlacementKey(w).persisted()).toString() == pinpoint::imu_roles::pelvis());
    check("role lowerArm", r.imuRoles().value(PlacementKey(h, Lane::LowerArm).persisted()).toString() == pinpoint::imu_roles::leadForearm());
    check("role palm", r.imuRoles().value(PlacementKey(h, Lane::Palm).persisted()).toString() == pinpoint::imu_roles::leadHand());
    check("prior", rawMap("analysis/clubLenPrior").value(typedPrior).toMap().value("lenM").toDouble() == 0.94);

    // Read back the other way: every stored key parses, and spells itself again.
    for (const QString &k : rawMap("camera/perspective").keys())
        same("stored camera key parses", DeviceKey::fromPersistedCameraKey(k).persistedCameraKey(), k);
    for (const QString &k : r.cameraRoi().keys()) {
        CropEntry e = CropEntry::Shared;
        same("stored roi key parses", DeviceKey::fromPersistedRoiKey(k, &e).persistedRoiKey(e), k);
    }
    for (const QString &k : r.cameraExposureUs().keys() + r.cameraTuning().keys()) {
        TuningEntry e = TuningEntry::ImpactRole;
        same("stored tuning key parses", DeviceKey::fromPersistedTuningKey(k, &e).persistedTuningKey(e), k);
    }
    for (const QString &k : r.imuAlias().keys())
        same("stored alias key parses", DeviceKey::fromPersistedWithDescription(k).persistedWithDescription(), k);
    for (const QString &k : r.imuExcluded())
        same("stored excluded id parses", DeviceKey::fromPersistedId(k).persistedId(), k);
    for (const QString &k : r.imuRoles().keys())
        same("stored role key parses", PlacementKey::fromPersisted(k).persisted(), k);

    // The role map read back as typed keys names each sensor the enumerator names.
    QSet<DeviceKey> roleDevices;
    for (const QString &k : r.imuRoles().keys()) roleDevices.insert(PlacementKey::fromPersisted(k).device());
    check("roles name the two sensors", roleDevices == QSet<DeviceKey>({ w, h }));
}

// ── 5. C-1b's sites: each now calls the type; each spells what its old expression spelled ─────────
//
// The four sites that hold a Device (camera_manager.cpp's cameraKey and createController,
// camera_instance.cpp's constructor, resource_monitor_controller.cpp's camera row) all call
// pp_camroi::cameraDeviceKey, called here on a struct with Device's field names: a serial distinct from
// the device id proves which field each part comes from. CameraInstance::cameraKey() (members, no
// Device) and the monitor's sensor alias are one-line calls, checked by reading (NOTES-C-1b).

struct DeviceFields {   // Device's camera fields, by the names cameraDeviceKey reads
    QString id, description;
    CameraCapabilities capabilities;
};
static DeviceFields deviceFields(const Cam &c)
{
    DeviceFields d;
    d.id = c.id;
    d.description = c.description;
    d.capabilities.serialNumber = c.serial;
    return d;
}

// The old site expression, verbatim but for the variable name (camera_manager.cpp:969 before C-1b).
static QString todaysSiteKey(const DeviceFields &device)
{
    return device.description + QLatin1Char('|')
           + (device.capabilities.serialNumber.isEmpty() ? device.id : device.capabilities.serialNumber);
}

static void testSites()
{
    const QList<Cam> cams{ kCamA, kCamB,
                           { QString(), QString(), QStringLiteral("0x8020000005ac8514") },          // no description
                           { QStringLiteral("Rig|Cam A|left"), QStringLiteral("SN|9"), QStringLiteral("d") },  // '|' in the parts
                           { QStringLiteral("Caméra 前 "), QStringLiteral("SN 1 "), QStringLiteral("x") } };
    for (const Cam &c : cams) {
        const DeviceFields d = deviceFields(c);
        const QString today = todaysSiteKey(d);
        const DeviceKey k = pp_camroi::cameraDeviceKey(d);
        same("site camera key " + today, k.persistedCameraKey(), today);
        same("site camera key = the fixture's rule " + today, k.persistedCameraKey(), todaysCameraKey(c));
        // camera_manager.cpp:85, the alias seed: every '|' a space (C6).
        same("site alias seed " + today, pp_camroi::seededAlias(k), QString(today).replace(QLatin1Char('|'), QLatin1Char(' ')));
        // camera_roi.h: the role's crop and tuning entries, for every role.
        for (int p : { 0, 1, 2, 3, kImpact }) {
            same(QStringLiteral("site roi key p%1 ").arg(p) + today, pp_camroi::key(k, p), todaysRoiKey(today, p));
            same(QStringLiteral("site tuning key p%1 ").arg(p) + today, pp_camroi::tuningKey(k, p), todaysTuningKey(today, p));
        }
    }
    check("site: a serial wins over the device id",
          pp_camroi::cameraDeviceKey(deviceFields(kCamA)).id() == kCamA.serial);
    check("site: the device id when there is no serial",
          pp_camroi::cameraDeviceKey(deviceFields(kCamB)).id() == kCamB.id);

    // camera_role_probe.cpp: its target's key is the camera list's string, parsed once; the crop
    // entries it reads and writes are the ones today's code spelled from the string.
    for (const QString &t : { todaysCameraKey(kCamA), todaysCameraKey(kCamB), QStringLiteral("|x"), QStringLiteral("a|b|c"),
                              QStringLiteral("model|"), QStringLiteral("m|id#impact#x") }) {
        const DeviceKey parsed = DeviceKey::fromPersistedCameraKey(t);
        for (int p : { 0, 1, 2, 3, kImpact })
            same(QStringLiteral("probe roi key p%1 ").arg(p) + t, pp_camroi::key(parsed, p), todaysRoiKey(t, p));
    }
    check("probe: a key with no '|' does not parse (the target is skipped)",
          DeviceKey::fromPersistedCameraKey(u"no separator").isNull());

    // resource_monitor_controller.cpp:342, the sensor alias: description|deviceId, as it was open-coded.
    for (const Imu &i : { kWit, kHm })
        same("site sensor alias " + i.id, DeviceKey::sensor(i.description, i.id).persistedWithDescription(),
             i.description + QLatin1Char('|') + i.id);

    // C-1c's sensor sites. imu_roles::aliasKey (imu_manager.cpp's three alias reads and writes,
    // sensorDisplayName) and shot_processor.cpp's export alias lookup spell through the type; the seed
    // imu_manager.cpp gives a newly seen sensor asks the key for its parts, every '|' a space.
    const QList<Imu> sensors{ kWit, kHm, { QString(), QStringLiteral("0x8020000005ac8514") },   // no description
                              { QStringLiteral("Rig|Left|wrist"), QStringLiteral("{1}") },     // '|' in the description
                              { QStringLiteral("Capteur é "), QStringLiteral("x") } };
    for (const Imu &i : sensors) {
        const QString today = todaysAliasKey(i.description, i.id);
        same("site imu alias key (imu_roles::aliasKey) " + today, pinpoint::imu_roles::aliasKey(i.description, i.id), today);
        same("site imu alias key (shot export) " + today, DeviceKey::sensor(i.description, i.id).persistedWithDescription(), today);
        same("site imu alias seed " + today, pinpoint::imu_roles::seededAlias(DeviceKey::sensor(i.description, i.id)),
             todaysSeededAlias(i.description, i.id));
    }
}

// ── 7. SET-05: a club-length prior stored by today's code is found under the typed key ──────────────
//
// capture_context.cpp reads the prior and the Rig keeps it under ClubLengthPriorKey::persisted() (since C-7,
// in rig.json); today's build stored it under the open-coded expression (todaysPriorKey). The settings store
// is written raw with today's spelling, as the last build before the Rig took the key over wrote it, read
// back raw from the file, and looked up with the typed key. The writer's update under the typed key is the
// Rig's now: rig_commands_test K12 (saved under today's spelling, byte for byte; the entry's other members
// kept) and club_length_prior_test R4.

static void testClubLengthPrior()
{
    const QString unassigned = pinpoint::unassignedAthleteId().toString();
    // The athlete's part. The capture path's uuid is AthleteController::currentUuid(), an AthleteId's own
    // toString(), so the typed key reads it back byte for byte.
    same("athlete uuid reads back", AthleteId::fromString(kAthlete).toString(), kAthlete);
    same("Unassigned uuid reads back", AthleteId::fromString(unassigned).toString(), unassigned);

    struct Case { const char *label; QString uuid, club; Cam cam; double lenM; };
    const QList<Case> cases{
        { "athlete, 7i, camera with a serial", kAthlete, kClub, kCamA, 0.94 },
        { "athlete, driver, camera with no serial", kAthlete, QStringLiteral("Driver"), kCamB, 1.12 },
        { "no athlete chosen", QString(), kClub, kCamA, 0.91 },
        { "the Unassigned owner", unassigned, QStringLiteral("PW"), kCamA, 0.88 },
        { "'|' in the camera's description", kAthlete, QStringLiteral("5i"),
          { QStringLiteral("Rig|Cam A"), QStringLiteral("SN 9"), QStringLiteral("d") }, 0.97 },
        { "a camera with no description", kAthlete, kClub,
          { QString(), QString(), QStringLiteral("0x8020000005ac8514") }, 0.95 },
    };
    auto typed = [](const Case &c) {
        return ClubLengthPriorKey(AthleteId::fromString(c.uuid), c.club, camKey(c.cam));
    };
    auto today = [](const Case &c) { return todaysPriorKey(c.uuid, c.club, todaysCameraKey(c.cam)); };

    for (const Case &c : cases)
        same(QStringLiteral("prior key = today: ") + QLatin1String(c.label), typed(c).persisted(), today(c));

    {
        QSettings raw = ppSettings();   // the last writer before C-7, today's keys: one whole map
        QVariantMap all;
        for (const Case &c : cases) all[today(c)] = QVariantMap{ { "emaPx", 512.0 }, { "lenM", c.lenM } };
        raw.setValue(QStringLiteral("analysis/clubLenPrior"), all);
        raw.sync();
    }
    {
        QSettings f(ppSettings().fileName(), QSettings::IniFormat);   // parsed from the file's text, as a launch reads it
        const QVariantMap stored = f.value(QStringLiteral("analysis/clubLenPrior")).toMap();
        QStringList want;
        for (const Case &c : cases) {
            want << typed(c).persisted();
            check(QStringLiteral("prior found under the typed key: ") + QLatin1String(c.label),
                  stored.value(typed(c).persisted()).toMap().value("lenM").toDouble() == c.lenM);
        }
        QStringList got = stored.keys();
        got.sort(); want.sort();
        check("analysis/clubLenPrior: stored keys = typed spellings", got == want);
    }

    // Null means "nothing to learn for this shot": only a key with no face-on camera.
    const AthleteId athlete = AthleteId::fromString(kAthlete);
    check("null key", ClubLengthPriorKey().isNull() && ClubLengthPriorKey().persisted().isNull());
    check("a sensor is no face-on camera", ClubLengthPriorKey(athlete, kClub, imuKey(kWit)).isNull());
    check("no camera, no key", ClubLengthPriorKey(athlete, kClub, DeviceKey()).isNull());
    check("no athlete still keys a prior", !ClubLengthPriorKey(AthleteId(), kClub, camKey(kCamA)).isNull());
    check("equal parts, equal keys",
          ClubLengthPriorKey(athlete, kClub, camKey(kCamA)) == ClubLengthPriorKey(athlete, kClub, camKey(kCamA)));
    check("another camera is another prior",
          ClubLengthPriorKey(athlete, kClub, camKey(kCamA)) != ClubLengthPriorKey(athlete, kClub, camKey(kCamB)));
    check("another club is another prior",
          ClubLengthPriorKey(athlete, kClub, camKey(kCamA)) != ClubLengthPriorKey(athlete, QStringLiteral("PW"), camKey(kCamA)));
}

// ── 6. A store written by today's code gives each role the same crop through cropFor ──────────────

static void testFixtureCrops()
{
    const QString camA = todaysCameraKey(kCamA), camB = todaysCameraKey(kCamB);
    {
        AppSettings s;   // today's writer, today's keys
        s.setCameraRoi({ { todaysRoiKey(camA, 2), box(0.0, 0.1, 1.0, 0.8) },
                         { todaysRoiKey(camA, kImpact), box(0.25, 0.4, 0.5, 0.234375) },
                         { todaysRoiKey(camB, 1), box(0.2, 0.0, 0.6, 1.0) } });
        QSettings raw = ppSettings();   // the alias key, as the last build before the Rig's cut-over wrote it
        raw.setValue(QStringLiteral("camera/alias"), QVariantMap{ { camA, QStringLiteral("Face-on") } });
    }
    AppSettings r;
    const DeviceKey a = pp_camroi::cameraDeviceKey(deviceFields(kCamA)), b = pp_camroi::cameraDeviceKey(deviceFields(kCamB));
    check("crop A, impact role: the strip", pp_camroi::cropFor(r.cameraRoi(), a, kImpact) == QRectF(0.25, 0.4, 0.5, 0.234375));
    for (int p : { 0, 1, 2, 3 })
        check(QStringLiteral("crop A, role %1: the shared crop").arg(p), pp_camroi::cropFor(r.cameraRoi(), a, p) == QRectF(0.0, 0.1, 1.0, 0.8));
    check("crop B, a role: its shared crop", pp_camroi::cropFor(r.cameraRoi(), b, 1) == QRectF(0.2, 0.0, 0.6, 1.0));
    check("crop B, impact role, no strip stored: none", pp_camroi::cropFor(r.cameraRoi(), b, kImpact).isEmpty());
    check("alias A through the site's key",
          ppSettings().value(QStringLiteral("camera/alias")).toMap().value(a.persistedCameraKey()).toString() == QStringLiteral("Face-on"));
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    std::printf("device_key_test — DeviceKey and PlacementKey spell every persisted key as today (rig_design.md §4.6)\n");
    testSpellings();
    testRoundTrips();
    testIdentity();
    testSettingsFixture();
    testSites();
    testFixtureCrops();
    testClubLengthPrior();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
