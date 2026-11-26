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

// rig_state_test — the rig state as a value (src/Vocabulary/Rig/rig_state.h, calibration_record.h) and its three ids
// (src/Vocabulary/Identity/identity.h: RigId, RigStateId, RigRef); platform refactor C-3, docs/design/rig_design.md
// §4.2, §4.5, §4.8 and information_model.md §5.2 ID-RIG.
//
// The contract: the digest is a function of the rig state's content only. It ignores list order; it changes with each
// versioned field (one case per field); it ignores sinceUtc, a record's evidence pointer, a sensor's description
// (sensor keys are equal by device id), and the Rig's other facts held beside the state in RigParts (aliases, enabled
// flags, the learned prior); a third camera is one more entry and changes the id. The canonical bytes write every
// number one fixed way, so a fixture's digest is pinned as a literal here and must be the same literal on the cabin
// PC (MSVC) as on the Mac (clang). Both literals (the hand-written case and the fixture) were made from the header's
// format description in Python (bytes spelled by hand, hashlib's SHA-256), not by running this code.

#include "../rig_state.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimeZone>

#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <type_traits>

using namespace pinpoint;
using namespace pinpoint::rig;
using pinpoint::analysis::SegmentRole;
using Perspective = pinpoint::recording::Perspective;

static_assert(!std::is_default_constructible_v<Uncertainty>, "a record's uncertainty is always stated");
static_assert(!std::is_constructible_v<RigId, QString>, "a RigId is never made from text by accident");
static_assert(!std::is_convertible_v<QUuid, RigId>, "a QUuid becomes a RigId only on purpose");

static int g_fail = 0;
static int g_run  = 0;

static void check(const char *label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_fail;
}

static QDateTime utc(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC); }

static const DeviceKey kCamA = DeviceKey::camera(QStringLiteral("Blackfly S BFS-U3-16S2C"), QStringLiteral("23456789"),
                                                 QStringLiteral("Spinnaker:0"));
static const DeviceKey kCamB = DeviceKey::camera(QStringLiteral("Chameleon3 CM3-U3-13Y3C"), QStringLiteral("18277032"),
                                                 QStringLiteral("Spinnaker:1"));
static const DeviceKey kCamC = DeviceKey::camera(QStringLiteral("FaceTime HD Camera"), QString(),
                                                 QStringLiteral("0x1420000005ac8600"));
static const DeviceKey kWit  = DeviceKey::sensor(QStringLiteral("WT901BLE68"), QStringLiteral("E2:1F:5A:33:10:9C"));
static const DeviceKey kHm   = DeviceKey::sensor(QStringLiteral("HackMotion wG3"), QStringLiteral("HM-00412"));

static CalibrationRecord assumedSquare(const DeviceKey &cam)
{
    return CalibrationRecord::forCamera(cam, CalibrationKind::Extrinsics, CalibrationMethod::Assumed,
                                        NamedValues{ { QStringLiteral("yawDeg"), 0.0 } },
                                        Uncertainty::sigmas(NamedValues{ { QStringLiteral("yawDeg"), 3.0 } }),
                                        utc(1764000000000));
}
static CalibrationRecord witAlignment()
{
    SensorAlignmentParameters p;
    p.role      = SegmentRole::Pelvis;
    p.alignment = SensorAlignment{ QQuaternion(0.9f, 0.1f, -0.2f, 0.3f), QQuaternion(1.f, 0.f, 0.f, 0.f), 4.5, 1.25 };
    p.trunkSolve.present               = true;
    p.trunkSolve.deltaTier             = QStringLiteral("\u03b41");
    p.trunkSolve.sigmaDeltaDeg         = 10.0;
    p.trunkSolve.sampleFraction        = 0.98;
    p.trunkSolve.maxAbsGyroDps         = 0.7;
    p.trunkSolve.gravityErrPct         = 0.4;
    p.trunkSolve.longAxisFromUpDeg     = 2.0;
    p.trunkSolve.faceFromHorizontalDeg = 81.0;
    return CalibrationRecord::forSensor(PlacementKey(kWit), CalibrationMethod::Solved, p,
                                        Uncertainty::sigmas(NamedValues{ { QStringLiteral("mountYawDeg"), 10.0 } }),
                                        utc(1764000100000));
}
static CalibrationRecord hmHeld(PlacementKey::Lane lane, SegmentRole role)
{
    SensorAlignmentParameters p;
    p.role = role;   // the device holds its calibration; A = identity, M = the unit's mount (addendum decision 4)
    p.alignment = SensorAlignment{ QQuaternion(1.f, 0.f, 0.f, 0.f), QQuaternion(0.5f, 0.5f, -0.5f, 0.5f), 0.0, 0.0 };
    return CalibrationRecord::forSensor(PlacementKey(kHm, lane), CalibrationMethod::DeviceHeld, p,
                                        Uncertainty::notKnown(), utc(1764000200000));
}

// The fixture: two cameras (one with a box and an assumed record), a Witmotion with its alignment, a HackMotion's two
// lanes held by the device.
static RigState fixture()
{
    RigState s;
    CameraSetup a(kCamA);
    a.setPerspective(Perspective::FaceOn);
    a.mirrored     = true;
    a.fixedInPlace = true;
    a.hittingArea  = QRectF(0.25, 0.4, 0.5, 0.234375);
    a.calibrations << assumedSquare(kCamA);
    CameraSetup b(kCamB);
    b.setPerspective(Perspective::DownTheLine);
    b.calibrations << assumedSquare(kCamB);
    s.cameras << a << b;
    s.sensors << SensorSetup{ PlacementKey(kWit), SegmentRole::Pelvis, witAlignment() }
              << SensorSetup{ PlacementKey(kHm, PlacementKey::Lane::LowerArm), SegmentRole::LeadForearm,
                              hmHeld(PlacementKey::Lane::LowerArm, SegmentRole::LeadForearm) }
              << SensorSetup{ PlacementKey(kHm, PlacementKey::Lane::Palm), SegmentRole::LeadHand,
                              hmHeld(PlacementKey::Lane::Palm, SegmentRole::LeadHand) };
    s.sinceUtc = utc(1764000300000);
    return s;
}

static QString idOf(const RigState &s) { return rigStateIdOf(s).toString(); }
static QString idOf(const RigParts &p) { return rigStateIdOf(p).toString(); }

// One versioned field changed: the id must change.
static void changes(const char *label, const std::function<void(RigState &)> &edit)
{
    RigState s = fixture();
    edit(s);
    check(label, idOf(s) != idOf(fixture()));
}
// A field outside the digest changed: the id must not. Run on the Rig's two parts, so the other facts are reachable.
static void ignores(const char *label, const std::function<void(RigParts &)> &edit)
{
    RigParts p{ fixture(), {} };
    edit(p);
    check(label, idOf(p) == idOf(fixture()));
}
static SensorAlignmentParameters &witParams(RigState &s)
{
    return std::get<SensorAlignmentParameters>(s.sensors[0].calibration->parameters);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::printf("ids (ID-RIG)\n");
    {
        const RigId r = RigId::create();
        check("a new RigId is not null, and reads back from its own text", !r.isNull() && RigId::fromString(r.toString()) == r);
        check("a RigId is a uuid's text and nothing else",
              RigId::fromString(r.toString() + QStringLiteral("/x")).isNull() && RigId::fromString(QStringLiteral("rig")).isNull());
        const QString hex64 = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        const RigStateId st = RigStateId::fromString(hex64);
        check("a RigStateId is 64 lower-case hex digits", !st.isNull() && st.toString() == hex64);
        check("anything else is the null RigStateId",
              RigStateId::fromString(hex64.toUpper()).isNull() && RigStateId::fromString(hex64.left(63)).isNull()
                  && RigStateId::fromString(hex64 + QLatin1Char('0')).isNull() && RigStateId::fromDigest(QByteArray(31, 'x')).isNull());
        const RigRef ref(r, st);
        check("RigRef is the pair, and reads back from its own text", ref.rig() == r && ref.state() == st
                                                                          && RigRef::fromString(ref.toString()) == ref);
        check("a null half gives the null RigRef", RigRef(RigId(), st).isNull() && RigRef(r, RigStateId()).isNull()
                                                       && RigRef(r, RigStateId()) == RigRef());
        QSet<RigRef> refs{ ref, ref, RigRef(RigId::create(), st) };
        check("RigRef hashes, and two machines' refs to one state differ", refs.size() == 2);
        check("ordered by rig, then state", (RigRef(r, st) < RigRef(r, RigStateId::fromString(QString(64, QLatin1Char('f'))))));
    }

    std::printf("the canonical bytes, spelled by hand from the header's format\n");
    {
        RigState s;
        CameraSetup c(DeviceKey::camera(QStringLiteral("Cam"), QStringLiteral("42"), QStringLiteral("d0")));
        c.setPerspective(Perspective::FaceOn);
        c.fixedInPlace = true;
        c.hittingArea  = QRectF(0.5, -0.0, 1.0, 0.25);
        s.cameras << c;
        const QByteArray expected = "pinpoint.rigState/1\ncameras 1\n"
                                    "camera 6:Cam|42 perspective 2 mirrored 0 fixed 1 area "
                                    "3fe0000000000000,0000000000000000,3ff0000000000000,3fd0000000000000 records 0\n"
                                    "sensors 0\n\n";
        check("the bytes are exactly the format's, -0 written as 0", canonicalBytes(s) == expected);
        if (canonicalBytes(s) != expected) std::printf("    got: %s\n", canonicalBytes(s).constData());
        // python3 -c "import hashlib;print(hashlib.sha256(b'<expected>').hexdigest())"
        check("its digest is SHA-256 of those bytes",
              idOf(s) == QStringLiteral("6abda75c3d86e493457538d7b92281f4b0e5b5b6f502e17f118d13686d8dde1b"));
    }

    std::printf("the digest ignores list order\n");
    {
        RigState s = fixture();
        std::swap(s.cameras[0], s.cameras[1]);
        std::swap(s.sensors[0], s.sensors[2]);
        check("cameras and sensors in another order: the same id", idOf(s) == idOf(fixture()));
        RigState t = fixture();
        t.cameras[0].calibrations.prepend(CalibrationRecord::forCamera(
            kCamA, CalibrationKind::Focus, CalibrationMethod::UserMeasured, NamedValues{ { QStringLiteral("mm"), 25.0 } },
            Uncertainty::notKnown(), utc(1)));
        RigState u = t;
        std::swap(u.cameras[0].calibrations[0], u.cameras[0].calibrations[1]);
        check("a camera's records in another order: the same id", idOf(t) == idOf(u));
        NamedValues one{ { QStringLiteral("b"), 2.0 }, { QStringLiteral("a"), 1.0 } };
        NamedValues two{ { QStringLiteral("a"), 1.0 }, { QStringLiteral("b"), 2.0 } };
        check("named values in another order: the same bytes", canonical_detail::named(one) == canonical_detail::named(two));
    }

    std::printf("the digest changes with each versioned field\n");
    changes("a camera's key (its serial)", [](RigState &s) {
        CameraSetup c(DeviceKey::camera(QStringLiteral("Blackfly S BFS-U3-16S2C"), QStringLiteral("23456780"), QStringLiteral("Spinnaker:0")));
        c.setPerspectiveCode(s.cameras[0].perspectiveCode()); c.mirrored = s.cameras[0].mirrored;
        c.fixedInPlace = s.cameras[0].fixedInPlace; c.hittingArea = s.cameras[0].hittingArea;
        c.calibrations = s.cameras[0].calibrations; s.cameras[0] = c; });
    changes("a camera's perspective", [](RigState &s) { s.cameras[0].setPerspective(Perspective::Impact); });
    changes("an unknown perspective code, kept", [](RigState &s) { s.cameras[1].setPerspectiveCode(7); });
    changes("mirrored", [](RigState &s) { s.cameras[0].mirrored = false; });
    changes("fixed in place", [](RigState &s) { s.cameras[0].fixedInPlace = false; });
    changes("the hitting area's x", [](RigState &s) { s.cameras[0].hittingArea->moveLeft(0.26); });
    changes("the hitting area's y", [](RigState &s) { s.cameras[0].hittingArea->moveTop(0.41); });
    changes("the hitting area's width", [](RigState &s) { s.cameras[0].hittingArea->setWidth(0.51); });
    changes("the hitting area's height", [](RigState &s) { s.cameras[0].hittingArea->setHeight(0.25); });
    changes("a camera's description", [](RigState &s) {
        CameraSetup c(DeviceKey::camera(QStringLiteral("Blackfly S BFS-U3-16S2M"), QStringLiteral("23456789"), QStringLiteral("Spinnaker:0")));
        c.setPerspectiveCode(s.cameras[0].perspectiveCode()); c.mirrored = s.cameras[0].mirrored;
        c.fixedInPlace = s.cameras[0].fixedInPlace; c.hittingArea = s.cameras[0].hittingArea;
        c.calibrations = s.cameras[0].calibrations; s.cameras[0] = c; });
    changes("the hitting area cleared", [](RigState &s) { s.cameras[0].hittingArea.reset(); });
    changes("a camera record added", [](RigState &s) { s.cameras[1].calibrations << CalibrationRecord::forCamera(
        kCamB, CalibrationKind::Intrinsics, CalibrationMethod::Factory, NamedValues{ { QStringLiteral("fx"), 1200.0 } },
        Uncertainty::notKnown(), utc(5)); });
    changes("a camera record removed", [](RigState &s) { s.cameras[0].calibrations.clear(); });
    changes("a record's kind", [](RigState &s) { s.cameras[0].calibrations[0].kind = CalibrationKind::Intrinsics; });
    changes("a record's subject", [](RigState &s) { s.cameras[0].calibrations[0].subject = kCamB; });
    changes("a record's method", [](RigState &s) { s.cameras[0].calibrations[0].method = CalibrationMethod::Solved; });
    changes("a record's parameter", [](RigState &s) {
        std::get<NamedValues>(s.cameras[0].calibrations[0].parameters).set(QStringLiteral("yawDeg"), 0.5); });
    changes("a record's uncertainty value", [](RigState &s) {
        s.cameras[0].calibrations[0].uncertainty = Uncertainty::sigmas(NamedValues{ { QStringLiteral("yawDeg"), 2.0 } }); });
    changes("a record's uncertainty becoming not known", [](RigState &s) {
        s.cameras[0].calibrations[0].uncertainty = Uncertainty::notKnown(); });
    changes("a record's time observed", [](RigState &s) { s.cameras[0].calibrations[0].observedAtUtc = utc(1764000000001); });
    changes("a sensor's placement (its lane)", [](RigState &s) { s.sensors[1].key = PlacementKey(kHm, PlacementKey::Lane::None); });
    changes("a sensor's device id", [](RigState &s) {
        s.sensors[0].key = PlacementKey(DeviceKey::sensor(QStringLiteral("WT901BLE68"), QStringLiteral("E2:1F:5A:33:10:9D"))); });
    changes("a sensor's role", [](RigState &s) { s.sensors[0].role = SegmentRole::Thorax; });
    changes("a sensor's calibration cleared", [](RigState &s) { s.sensors[0].calibration.reset(); });
    changes("the alignment's role", [](RigState &s) { witParams(s).role = SegmentRole::Thorax; });
    changes("A (one component)", [](RigState &s) { witParams(s).alignment->alignA.setY(-0.21f); });
    changes("M (one component)", [](RigState &s) { witParams(s).alignment->mountM.setZ(0.01f); });
    changes("the mount deviation check", [](RigState &s) { witParams(s).alignment->mountDeviationDeg = 4.6; });
    changes("the mount gravity check", [](RigState &s) { witParams(s).alignment->mountGravityErrorDeg = 1.3; });
    changes("the alignment dropped (device-held)", [](RigState &s) { witParams(s).alignment.reset(); });
    changes("the trunk solve's tier", [](RigState &s) { witParams(s).trunkSolve.deltaTier = QStringLiteral("\u03b42"); });
    changes("the trunk solve's sigma", [](RigState &s) { witParams(s).trunkSolve.sigmaDeltaDeg = 12.0; });
    changes("the trunk solve's sample fraction", [](RigState &s) { witParams(s).trunkSolve.sampleFraction = 0.97; });
    changes("the trunk solve's stillness", [](RigState &s) { witParams(s).trunkSolve.maxAbsGyroDps = 0.8; });
    changes("the trunk solve's gravity error", [](RigState &s) { witParams(s).trunkSolve.gravityErrPct = 0.5; });
    changes("the trunk solve's long axis", [](RigState &s) { witParams(s).trunkSolve.longAxisFromUpDeg = 2.5; });
    changes("the trunk solve's face angle", [](RigState &s) { witParams(s).trunkSolve.faceFromHorizontalDeg = 80.0; });
    changes("a device-held unit's mount M", [](RigState &s) {
        std::get<SensorAlignmentParameters>(s.sensors[1].calibration->parameters).alignment->mountM = QQuaternion(1.f, 0.f, 0.f, 0.f); });
    changes("the trunk solve absent", [](RigState &s) { witParams(s).trunkSolve.present = false; });
    changes("a sensor added", [](RigState &s) {
        s.sensors << SensorSetup{ PlacementKey(DeviceKey::sensor(QStringLiteral("WT901BLE68"), QStringLiteral("AA:BB"))),
                                  SegmentRole::Thorax, std::nullopt }; });

    std::printf("the digest ignores what is outside the versioned part\n");
    ignores("sinceUtc", [](RigParts &p) { p.state.sinceUtc = utc(1); });
    ignores("a record's evidence pointer, set after commit", [](RigParts &p) {
        p.state.sensors[0].calibration->evidence = CalibrationEvidence{ SessionId::create(), QStringLiteral("trunk_ceremony.json") }; });
    ignores("a sensor's description (keys are equal by device id)", [](RigParts &p) {
        // as imu/roles reads it back: the bare device id, no description
        const PlacementKey bare(DeviceKey::sensor(QString(), QStringLiteral("E2:1F:5A:33:10:9C")));
        p.state.sensors[0].key = bare;
        p.state.sensors[0].calibration->subject = bare; });
    ignores("an alias", [](RigParts &p) { p.other.aliases << qMakePair(kCamA, QStringLiteral("Face-on")); });
    ignores("an enabled flag", [](RigParts &p) { p.other.enabledOnThisMachine << qMakePair(kWit, false); });
    ignores("the learned club-length prior", [](RigParts &p) {
        p.other.clubLengthPriors << qMakePair(ClubLengthPriorKey(AthleteId(QUuid::createUuid()), QStringLiteral("7i"), kCamA),
                                              QVariantMap{ { QStringLiteral("lengthM"), 0.95 } }); });

    std::printf("a third camera\n");
    {
        RigState s = fixture();
        CameraSetup c(kCamC);   // no serial: keyed by its device id
        c.setPerspective(Perspective::Impact);
        s.cameras << c;
        check("is one more entry", s.cameras.size() == 3 && canonicalBytes(s).contains("cameras 3\n"));
        check("and changes the id", idOf(s) != idOf(fixture()));
    }

    std::printf("numbers are written one way\n");
    {
        check("-0 and +0 are the same bytes", canonical_detail::real(-0.0) == canonical_detail::real(0.0));
        check("every NaN is one NaN", canonical_detail::real(std::nan("1")) == canonical_detail::real(-std::numeric_limits<double>::quiet_NaN()));
        check("a float is widened, never rounded through text", canonical_detail::real(double(0.1f)) == QByteArray("3fb99999a0000000"));
        check("the next double up is a different value", canonical_detail::real(0.1) != canonical_detail::real(std::nextafter(0.1, 1.0)));
    }

    std::printf("an unknown perspective code is kept\n");
    {
        CameraSetup c(kCamA);
        c.setPerspectiveCode(7);
        check("read as None, kept as 7", c.perspective() == Perspective::None && c.perspectiveCode() == 7);
    }

    std::printf("the rig reference has exactly three states\n");
    {
        const RigRef ref(RigId::create(), rigStateIdOf(fixture()));
        check("a default reference is Unknown", RigReference().state() == RigReference::State::Unknown
                                                    && RigReference().ref().isNull());
        check("None", RigReference::none().state() == RigReference::State::None);
        const RigReference k = RigReference::known(ref, fixture());
        check("Known carries its ref and its state", k.state() == RigReference::State::Known && k.ref() == ref
                                                        && k.rigState().cameras.size() == 2);
    }

    std::printf("the fixture's digest, pinned: the same literal on the Mac and the cabin PC\n");
    {
        const QString id = idOf(fixture());
        check("pinned", id == QStringLiteral("8a30b447fabb1560addca066054785c29e2385c36112d7cdece5b87e0b61970d"));
        std::printf("    fixture digest: %s\n", qPrintable(id));
    }

    std::printf("\n%d of %d checks passed\n", g_run - g_fail, g_run);
    return g_fail == 0 ? 0 : 1;
}
