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

// imu_instance_rig_test: the sensors' live objects keep the Rig's alignment records true (platform refactor C-6b.2;
// docs/design/rig_design.md §4.2, §4.8 step 1; rig_design_addendum.md decision 4: "the record follows the live sensor's
// alignment, whatever changed it"; the authority for the paths is
// docs/implementation/platform_refactor/duties/DUTIES-sensor-properties.md §2 and §3). A real ImuInstance and a real
// HmInstance, made with no I/O thread, no EventBuffer and no link, are handed a real Rig (over an in-memory repository
// that counts saves) as their calibration sink, as ImuManager hands it. Every set path and every clear path of the
// inventory is run on the live object, and after each the Rig's record is compared with what the object now holds.
//
//   W1  the table, a Witmotion: each set path (SE1 setCalibration, SE2 setFunctionalCalibration, SE3
//       setNominalCalibration, SE4a and SE4b refineMountAboutLongAxis, SE5 calibrateTrunk, its valid exit on frames fed
//       through the device's own driver, and its two exits that apply nothing) and each clear path (CL1
//       clearCalibration, CL2 clearFunctionalCalibration, CL4 the two routines' resets, CL7 the object's teardown). After
//       each: the Rig's record equals the live anatomical half (A, M, both mount checks, the trunk provenance, the method
//       `solved`, the kind, the role, a time observed equal to the object's own), or there is none when the object is
//       not fully calibrated (#417: arm-down alone, a stand tall alone, a failed refine and a one-shot functional solve
//       apply an alignment and hold no record); exactly one RigChanged when the record changed and none when it did
//       not; nothing saved. SE1 and CL1 touch only the older flag, so the record does not move (decision 4.2)
//   C1  the ruling (#417): a sensor is calibrated, and the Rig holds its record, only after a whole run passed — the arm
//       after arm-down and an accepted refine with both mount checks in limits, judged per sensor; the trunk after a
//       valid stand tall and the routine's confirmation once both turns pass. Half a run leaves the alignment applied
//   W2  re-entrancy: at each RigChanged the object already holds what the record says (the commit is its last act),
//       and the roles, aliases and enabled flags are as they were; the object's own A and M equal what the same calls
//       give with no sink at all (the sensor's behaviour is unchanged)
//   H1  the table, a HackMotion: "calibrated" (SE6, the presence measured) commits a `deviceHeld` record on each of its
//       two lanes (A identity, M the unit's fixed mount, hm_frame::mountM(), the mount checks 0, each lane's own role); a
//       write that leaves "calibrated" as it was reports nothing; it is cleared by a lost link and by a restarted stream
//       (CL5, "calibration invalidated", before that signal is emitted), by a state refresh that reads it gone, by a
//       presence check not measured and by a phase event that reads it gone (CL6), and by the object's teardown (CL7)
//   R1  a role released or moved leaves the record (CL8, decision 4.6); a record for a sensor with no role is kept
//
// Real stores and devices: none. IsolatedStores is made first (#187); the Rig's repository is in memory; no BLE link is
// opened (no start()), and no frame reaches a device. Hand-rolled main.

#include "../../Core/tests/isolated_stores.h"

#include "../imu/hm_instance.h"
#include "../imu/imu_instance.h"
#include "../imu/imu_rig_link.h"
#include "event_buffer.h"
#include "../../DomainModel/rig.h"
#include "../../IMU/ble_imu_transport.h"
#include "../../IMU/hm_frame.h"
#include "../../IMU/hm_session_worker.h"
#include "../../IMU/imu_calibration.h"
#include "../../IMU/imu_role_map.h"

#include <wrist/wrist.h>

#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace pinpoint;
using namespace pinpoint::rig;
using analysis::SegmentRole;

// The seams the two classes name as friends: the driver a Witmotion's frames enter by, and the HackMotion's worker.
struct ImuInstanceTestAccess {
    static WT9011DCL_BLE *driver(ImuInstance &i) { return i.m_imu; }
};
struct HmInstanceTestAccess {
    static HmSessionWorker *worker(HmInstance &h) { return h.m_worker; }
};

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

class MemoryRepository final : public RigRepository
{
public:
    RigLoad load() override { return {}; }
    bool    save(const RigId &, const RigParts &) override { ++saves; return true; }
    int     saves = 0;
};

void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

const SensorSetup *sensorOf(const Rig &rig, const PlacementKey &k)
{
    for (const SensorSetup &x : rig.parts().state.sensors)
        if (x.key == k) return &x;
    return nullptr;
}
const CalibrationRecord *recordOf(const Rig &rig, const PlacementKey &k)
{
    const SensorSetup *x = sensorOf(rig, k);
    return x && x->calibration ? &*x->calibration : nullptr;
}

bool sameProvenance(const TrunkCalibProvenance &a, const TrunkCalibProvenance &b)
{
    return a.present == b.present && a.deltaTier == b.deltaTier && a.sigmaDeltaDeg == b.sigmaDeltaDeg
           && a.sampleFraction == b.sampleFraction && a.maxAbsGyroDps == b.maxAbsGyroDps
           && a.gravityErrPct == b.gravityErrPct && a.longAxisFromUpDeg == b.longAxisFromUpDeg
           && a.faceFromHorizontalDeg == b.faceFromHorizontalDeg;
}

// The Rig's record of a Witmotion against the live object: what the object holds, copied, or nothing.
QString witmotionMismatch(const Rig &rig, const ImuInstance &imu, SegmentRole role)
{
    const PlacementKey key(DeviceKey::fromPersistedId(imu.deviceId()));
    const CalibrationRecord *r = recordOf(rig, key);
    // #417: a record only for a sensor a whole run calibrated; half a run (the alignment applied) holds none.
    if (!imu.fullyCalibrated()) return r ? QStringLiteral("a record, and the sensor is not calibrated") : QString();
    if (!r) return QStringLiteral("no record, and the sensor is calibrated");
    const auto *p = std::get_if<SensorAlignmentParameters>(&r->parameters);
    const auto *subject = std::get_if<PlacementKey>(&r->subject);
    if (!subject || *subject != key) return QStringLiteral("subject");
    if (r->kind != CalibrationKind::SensorAlignment) return QStringLiteral("kind");
    if (r->method != CalibrationMethod::Solved) return QStringLiteral("method");
    if (!p || !p->alignment) return QStringLiteral("no alignment");
    if (p->role != role) return QStringLiteral("role %1").arg(analysis::segmentRoleName(p->role));
    if (p->alignment->alignA != imu.alignA()) return QStringLiteral("A");
    if (p->alignment->mountM != imu.mountM()) return QStringLiteral("M");
    if (p->alignment->mountDeviationDeg != imu.mountDeviationDeg()) return QStringLiteral("mount deviation");
    if (p->alignment->mountGravityErrorDeg != imu.mountGravityErrorDeg()) return QStringLiteral("gravity error");
    if (!sameProvenance(p->trunkSolve, imu.trunkCalibProvenance())) return QStringLiteral("trunk provenance");
    if (r->uncertainty.isKnown()) return QStringLiteral("uncertainty");
    if (!r->observedAtUtc.isValid() || r->observedAtUtc != imu.calibratedAtUtc()) return QStringLiteral("time observed");
    return {};
}

// One 20-byte combined frame (0x55 0x61): accel ×3 (±16 g), gyro ×3 (±2000 °/s), the angles left 0, int16 LE — what a
// WT901BLE67 sends (trunk_turn_path_test's builder).
QByteArray combinedFrame(const QVector3D &accG, const QVector3D &gyroDps)
{
    QByteArray f(20, char(0));
    f[0] = char(0x55);
    f[1] = char(0x61);
    const auto put = [&](int off, double v, double fullScale) {
        const long r = std::lround(v / fullScale * 32768.0);
        const qint16 s = qint16(std::clamp(r, -32768L, 32767L));
        f[off]     = char(s & 0xFF);
        f[off + 1] = char((s >> 8) & 0xFF);
    };
    put(2, accG.x(), 16.0);  put(4, accG.y(), 16.0);  put(6, accG.z(), 16.0);
    put(8, gyroDps.x(), 2000.0); put(10, gyroDps.y(), 2000.0); put(12, gyroDps.z(), 2000.0);
    return f;
}

// A trunk sensor worn standing tall, still: USB up (sensor +Y = world up), label out, leaning 13° forward (the pelvis
// angle the 26 Oct stand tall logged; trunk_turn_path_test's mount). `n` frames, fed through the device's own transport
// signal, so the driver parses and fuses them and the instance's data path fills its stand-tall window.
void standTall(ImuInstance &imu, int n)
{
    const QQuaternion upright = QQuaternion::fromAxes(QVector3D(-1, 0, 0), QVector3D(0, 0, 1), QVector3D(0, 1, 0));
    const QQuaternion R = (QQuaternion::fromAxisAndAngle(1, 0, 0, 13.0f) * upright).normalized();
    const QVector3D accBody = R.conjugated().rotatedVector(QVector3D(0, 0, 1));
    auto *transport = ImuInstanceTestAccess::driver(imu)->findChild<BleImuTransport *>();
    for (int i = 0; transport && i < n; ++i) emit transport->dataReceived(combinedFrame(accBody, QVector3D()));
}

Device witmotion(const QString &id)
{
    Device d;
    d.type         = DeviceType::Imu;
    d.imuTransport = ImuBase::Transport::Ble;
    d.imuVendor    = ImuVendor::WitMotion;
    d.id           = id;
    d.description  = QStringLiteral("WT901BLE68");
    return d;
}
Device hackMotion(const QString &id)
{
    Device d = witmotion(id);
    d.imuVendor   = ImuVendor::HackMotion;
    d.description = QStringLiteral("HackMotion wG3");
    return d;
}

// The role each placement holds in the rig state, as ImuManager answers it: its own function (imu_rig_link.h) over its
// roles map, read from the state.
ImuDeviceBase::RoleOfPlacement rolesOf(const Rig &rig)
{
    return [&rig](const PlacementKey &k) { return pp_imurig::roleOfPlacement(imu_roles::rolesMapOf(rig.parts().state), k); };
}

const QQuaternion kRef1 = QQuaternion::fromEulerAngles(10.f, 20.f, 30.f);
const QQuaternion kRef2 = QQuaternion::fromEulerAngles(-15.f, 40.f, 5.f);
const QQuaternion kRef3 = QQuaternion::fromEulerAngles(25.f, -10.f, 60.f);
const QQuaternion kRef4 = QQuaternion::fromEulerAngles(5.f, 5.f, -45.f);

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("imu_instance_rig_test — the temporary directory: %s\n", qPrintable(stores.path()));

    const SensorFacts on{ true, true, false }, hmOn{ true, true, true };

    std::printf("\nW1 a Witmotion: each set path and each clear path, and the Rig's record after it\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        SensorFactsTable table;
        table.set(DeviceKey::fromPersistedId(u"W1"), on);
        rig.claimRole(DeviceKey::sensor("WT901BLE68", "W1"), SegmentRole::LeadForearm, table);
        const RigStateId roleOnly = rig.stateId();
        // Re-entrancy (W2): what a RigChanged listener sees, at the moment it is told.
        auto *imu = new ImuInstance(witmotion("W1"), nullptr, nullptr, nullptr);
        imu->setCalibrationSink(&rig, rolesOf(rig));
        imu->setOutputRateHz(200);
        settle();
        rig.setAlias(DeviceKey::sensor("WT901BLE68", "W1"), "Forearm");
        rig.setEnabledOnThisMachine(DeviceKey::fromPersistedId(u"W2"), false);
        const QVariantMap rolesBefore = imu_roles::rolesMapOf(rig.parts().state);
        const auto aliasesBefore = rig.parts().other.aliases;
        const auto flagsBefore   = rig.parts().other.enabledOnThisMachine;
        int events = 0, reentrancyFaults = 0;
        QObject::connect(&rig, &Rig::rigChanged, &rig, [&] {
            ++events;
            if (!witmotionMismatch(rig, *imu, imu->trunkCalibProvenance().present ? SegmentRole::Pelvis : SegmentRole::LeadForearm)
                     .isEmpty()
                || imu_roles::rolesMapOf(rig.parts().state) != rolesBefore || rig.parts().other.aliases != aliasesBefore
                || rig.parts().other.enabledOnThisMachine != flagsBefore)
                ++reentrancyFaults;
        });
        const int savesBefore = repo.saves;

        struct Row {
            const char           *id;
            const char           *what;
            std::function<void()> run;
            int                   events;      // RigChanged expected
            SegmentRole           role;        // the role the record names
        };
        const QVector3D grav(0, -1, 0), longAxis(0, 1, 0), flex(1, 0, 0);
        QVariantMap trunkOut;
        double se4cGravity = 0.0, se4cDeviation = 0.0;
        const Row rows[] = {
            { "SE1", "setCalibration: the older half only, no record", [&] { imu->setCalibration(kRef1, kRef2); }, 0, SegmentRole::LeadForearm },
            { "SE2", "setFunctionalCalibration: no run, so no record", [&] { imu->setFunctionalCalibration(kRef1, grav, longAxis, flex, false); }, 0, SegmentRole::LeadForearm },
            { "CL1", "clearCalibration: the older half only, the record stays", [&] { imu->clearCalibration(); }, 0, SegmentRole::LeadForearm },
            { "SE3", "setNominalCalibration: arm-down alone, no record", [&] { imu->setNominalCalibration(kRef2, false); }, 0, SegmentRole::LeadForearm },
            { "SE4a", "refineMountAboutLongAxis, |phi| <= 25", [&] { imu->refineMountAboutLongAxis(kRef3, 10.0, false); }, 1, SegmentRole::LeadForearm },
            { "SE4b", "refineMountAboutLongAxis, |phi| > 25 (kept nominal, fails the gate): the record cleared", [&] { imu->refineMountAboutLongAxis(kRef4, 40.0, false); }, 1, SegmentRole::LeadForearm },
            { "SE5x", "calibrateTrunk, an unknown role: nothing applied", [&] { imu->calibrateTrunk("knee"); }, 0, SegmentRole::LeadForearm },
            { "SE5i", "calibrateTrunk, an invalid solve (no samples): nothing applied", [&] { imu->calibrateTrunk("pelvis"); }, 0, SegmentRole::LeadForearm },
            { "SE5", "calibrateTrunk, a valid solve on the device's own frames: stand tall alone, no record", [&] { standTall(*imu, 600); trunkOut = imu->calibrateTrunk("pelvis"); }, 0, SegmentRole::Pelvis },
            { "SE5c", "confirmTrunkCalibration after both turns", [&] { imu->confirmTrunkCalibration(); }, 1, SegmentRole::Pelvis },
            { "CL4w", "the arm routine's reset: clearCalibration then clearFunctionalCalibration", [&] { imu->clearCalibration(); imu->clearFunctionalCalibration(); }, 1, SegmentRole::LeadForearm },
            { "SE3b", "setNominalCalibration again", [&] { imu->setNominalCalibration(kRef1, false); }, 0, SegmentRole::LeadForearm },
            // The sensor's last frames are SE5's, standing upright, so this arm-down's gravity check reads the mount
            // flipped (> 25°): an accepted refine still fails the gate, so no record.
            { "SE4c", "refineMountAboutLongAxis after it, the gravity check failed at arm-down: no record", [&] { imu->refineMountAboutLongAxis(kRef2, 8.0, false); se4cGravity = imu->mountGravityErrorDeg(); se4cDeviation = imu->mountDeviationDeg(); }, 0, SegmentRole::LeadForearm },
            { "CL4t", "the trunk routine's reset: clearFunctionalCalibration then clearCalibration, of no record", [&] { imu->clearFunctionalCalibration(); imu->clearCalibration(); }, 0, SegmentRole::LeadForearm },
            { "CL2", "clearFunctionalCalibration of nothing", [&] { imu->clearFunctionalCalibration(); }, 0, SegmentRole::LeadForearm },
            { "SE3c", "setNominalCalibration before the teardown", [&] { imu->setNominalCalibration(kRef3, false); }, 0, SegmentRole::LeadForearm },
            { "SE5d", "a stand tall and its confirmation before the teardown", [&] { standTall(*imu, 600); imu->calibrateTrunk("pelvis"); imu->confirmTrunkCalibration(); }, 1, SegmentRole::Pelvis },
        };
        for (const Row &row : rows) {
            const int before = events;
            row.run();
            const QString why = witmotionMismatch(rig, *imu, row.role);
            check(why.isEmpty() && events - before == row.events && repo.saves == savesBefore,
                  QStringLiteral("W1 %1 %2: the record %3; %4 RigChanged; nothing saved%5")
                      .arg(QLatin1String(row.id), QLatin1String(row.what),
                           imu->fullyCalibrated() ? QStringLiteral("equals the live alignment") : QStringLiteral("is none"))
                      .arg(row.events)
                      .arg(why.isEmpty() ? QString() : QStringLiteral(" (differs: %1, events %2)").arg(why).arg(events - before)));
        }
        check(se4cGravity > ImuInstance::kMountGravityErrorMaxDeg && se4cDeviation <= ImuInstance::kMountDeviationMaxDeg,
              QStringLiteral("W1 SE4c failed on the gravity check alone (gravity %1°, strap %2°)").arg(se4cGravity, 0, 'f', 1).arg(se4cDeviation, 0, 'f', 1));
        check(trunkOut.value("valid").toBool() && imu->trunkCalibProvenance().present,
              "W1 SE5's solve was valid (and SE5d's is the one in force now)");
        check(reentrancyFaults == 0 && events == 5,
              QStringLiteral("W2 at each of the 5 RigChanged the object already held the record's alignment, and the roles, "
                             "aliases and enabled flags were as before (faults %1)").arg(reentrancyFaults));

        const int before = events;
        delete imu;   // CL7: the object's teardown
        settle();
        check(!recordOf(rig, PlacementKey(DeviceKey::fromPersistedId(u"W1"))) && events - before == 1 && rig.stateId() == roleOnly
                  && repo.saves == savesBefore,
              "W1 CL7 the object's teardown clears the record: one RigChanged, the role-only RigStateId back, nothing saved");
    }

    std::printf("\nC1 a sensor is calibrated only by a whole calibration that passed (#417): half a run applies, and is not "
                "calibrated\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const auto made = [&](const char *id) {
            auto *i = new ImuInstance(witmotion(QString::fromLatin1(id)), nullptr, nullptr, nullptr);
            i->setCalibrationSink(&rig, rolesOf(rig));
            i->setOutputRateHz(200);
            return i;
        };
        const auto held = [&](const ImuInstance *i) { return recordOf(rig, PlacementKey(DeviceKey::fromPersistedId(i->deviceId()))) != nullptr; };

        // The arm: arm-down alone (a run stopped, or the lead lost, before the T-pose).
        ImuInstance *a = made("C1A");
        a->setNominalCalibration(kRef1, false);
        check(a->anatCalibrated() && !a->fullyCalibrated() && !held(a),
              "C1a arm-down alone: the alignment is applied (anatCalibrated), the sensor is NOT calibrated, the Rig holds no record");
        // The arm: the T-pose's refine without an arm-down before it.
        ImuInstance *b = made("C1B");
        b->refineMountAboutLongAxis(kRef2, 5.0, false);
        check(!b->fullyCalibrated() && !held(b), "C1b a refine with no arm-down before it: not calibrated, no record");
        // The trunk: stand tall alone (the two turns not yet checked).
        ImuInstance *t = made("C1T");
        standTall(*t, 600);
        const QVariantMap out = t->calibrateTrunk("pelvis");
        check(out.value("valid").toBool() && t->anatCalibrated() && !t->fullyCalibrated() && !held(t),
              "C1c a valid stand tall alone: the alignment is applied, the sensor is NOT calibrated until both turns pass, no record");

        // The passing paths, and what ends them.
        a->refineMountAboutLongAxis(kRef3, 10.0, false);
        check(a->fullyCalibrated() && held(a) && witmotionMismatch(rig, *a, SegmentRole::Unknown).isEmpty(),
              "C1d arm-down then an accepted refine within the gates: calibrated, and the Rig's record equals the live alignment");
        ImuInstance *f = made("C1F");
        f->setNominalCalibration(kRef2, false);
        f->refineMountAboutLongAxis(kRef3, 20.0, false);   // accepted (<= 25), over the 15° strap gate
        check(f->anatCalibrated() && !f->fullyCalibrated() && !held(f) && a->fullyCalibrated() && held(a),
              "C1e per sensor: a refine over the strap gate leaves that sensor applied and NOT calibrated, and another "
              "sensor of the same run that passed stays calibrated");
        ImuInstance *g = made("C1G");
        g->setNominalCalibration(kRef2, false);
        g->refineMountAboutLongAxis(kRef4, 40.0, false);   // rejected: kept nominal, |φ| as the deviation
        check(!g->fullyCalibrated() && !held(g), "C1f a refine rejected for |phi| > 25: not calibrated, no record");
        check(!a->confirmTrunkCalibration() && a->fullyCalibrated() && held(a),
              "C1g confirmTrunkCalibration on an arm calibration is refused and changes nothing");
        check(t->confirmTrunkCalibration() && t->fullyCalibrated() && held(t)
                  && witmotionMismatch(rig, *t, SegmentRole::Pelvis).isEmpty(),
              "C1h stand tall then the routine's confirmation (both turns passed): calibrated, the record equals the live solve");
        a->setNominalCalibration(kRef1, false);
        t->clearFunctionalCalibration();
        check(!a->fullyCalibrated() && !held(a) && !t->fullyCalibrated() && !held(t) && !t->confirmTrunkCalibration(),
              "C1i a new arm-down, and a clear, end the calibration and the record at once; a confirmation after a clear is refused");
        for (ImuInstance *i : { a, b, t, f, g }) delete i;
        settle();
    }

    std::printf("\nW2 the sensor's own behaviour is unchanged: the same calls with no sink give the same A and M\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        auto *withSink = new ImuInstance(witmotion("W3"), nullptr, nullptr, nullptr);
        auto *without  = new ImuInstance(witmotion("W4"), nullptr, nullptr, nullptr);
        withSink->setCalibrationSink(&rig, rolesOf(rig));
        bool same = true;
        for (ImuInstance *i : { withSink, without }) {
            i->setNominalCalibration(kRef2, false);
            i->refineMountAboutLongAxis(kRef3, 12.0, false);
        }
        same = withSink->alignA() == without->alignA() && withSink->mountM() == without->mountM()
               && withSink->mountDeviationDeg() == without->mountDeviationDeg() && withSink->anatCalibrated() == without->anatCalibrated();
        for (ImuInstance *i : { withSink, without }) i->clearFunctionalCalibration();
        same = same && withSink->alignA() == without->alignA() && withSink->mountM() == without->mountM()
               && !withSink->anatCalibrated() && !without->anatCalibrated();
        check(same, "W2 a nominal set, a refine and a clear give the same A, M, mount check and flag with the sink as without");
        delete withSink;
        delete without;
        settle();
    }

    std::printf("\nH1 a HackMotion: the device's \"calibrated\" commits two deviceHeld records, and every way it ends clears them\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const QString id = QStringLiteral("{0d7a1c2e-0000-4000-8000-0000000000a1}");
        SensorFactsTable table;
        table.set(DeviceKey::fromPersistedId(id), hmOn);
        rig.claimRole(DeviceKey::sensor("HackMotion wG3", id), SegmentRole::LeadForearm, table);
        const RigStateId roleOnly = rig.stateId();
        auto *hm = new HmInstance(hackMotion(id), nullptr, nullptr, nullptr);
        hm->setCalibrationSink(&rig, rolesOf(rig));
        HmSessionWorker *worker = HmInstanceTestAccess::worker(*hm);
        const PlacementKey lower(DeviceKey::fromPersistedId(id), PlacementKey::Lane::LowerArm);
        const PlacementKey palm(DeviceKey::fromPersistedId(id), PlacementKey::Lane::Palm);
        int events = 0;
        QObject::connect(&rig, &Rig::rigChanged, &rig, [&] { ++events; });
        bool invalidatedWithRecords = false;
        QObject::connect(hm, &HmInstance::calibrationInvalidated, hm, [&] {
            invalidatedWithRecords = invalidatedWithRecords || recordOf(rig, lower) || recordOf(rig, palm);
        });
        const int savesBefore = repo.saves;

        const auto held = [&](const PlacementKey &k, SegmentRole role) {
            const CalibrationRecord *r = recordOf(rig, k);
            const auto *p = r ? std::get_if<SensorAlignmentParameters>(&r->parameters) : nullptr;
            return r && p && p->alignment && r->kind == CalibrationKind::SensorAlignment
                   && r->method == CalibrationMethod::DeviceHeld && p->role == role && p->alignment->alignA == QQuaternion()
                   && p->alignment->mountM == hm_frame::mountM() && p->alignment->mountDeviationDeg == 0.0
                   && p->alignment->mountGravityErrorDeg == 0.0 && !p->trunkSolve.present && !r->uncertainty.isKnown()
                   && r->observedAtUtc.isValid();
        };
        const auto bothHeld = [&] {
            const CalibrationRecord *a = recordOf(rig, lower), *b = recordOf(rig, palm);
            return held(lower, SegmentRole::LeadForearm) && held(palm, SegmentRole::LeadHand) && a->observedAtUtc == b->observedAtUtc
                   && hm->fullyCalibrated();
        };
        const auto noneHeld = [&] { return !recordOf(rig, lower) && !recordOf(rig, palm) && !hm->fullyCalibrated() && rig.stateId() == roleOnly; };
        const auto calibrate = [&] { emit worker->calibrationPresenceMeasured(WR_CAL_CALIBRATED); };

        int before = events;
        calibrate();
        check(bothHeld() && events - before == 2 && repo.saves == savesBefore,
              "H1 SE6 the presence measured, \"calibrated\": a deviceHeld record on each lane (A identity, M the unit's mount, "
              "the lane's role), two RigChanged, nothing saved");
        before = events;
        const QDateTime seen = recordOf(rig, lower) ? recordOf(rig, lower)->observedAtUtc : QDateTime();
        QThread::msleep(5);   // a later millisecond, so a second report could not equal the first
        emit worker->calibrationPhaseEvent(WR_CALP_COMPLETE, WR_CALP_VERIFYING, WR_CAL_ABORT_NONE, WR_CAL_CALIBRATED, 0);
        emit worker->calibrationPresenceMeasured(WR_CAL_CALIBRATED);
        check(bothHeld() && events == before && recordOf(rig, lower)->observedAtUtc == seen,
              "H1 a write that leaves \"calibrated\" as it was reports nothing: no RigChanged, the record's time unchanged");

        struct End {
            const char           *id;
            const char           *what;
            std::function<void()> run;
        };
        const End ends[] = {
            { "CL5a", "the link lost (calibration invalidated, UNCALIBRATED)",
              [&] { emit worker->calibrationLost(WR_CAL_UNCALIBRATED, QStringLiteral("link down")); } },
            { "CL5b", "the stream restarted (calibration invalidated, UNKNOWN)",
              [&] { emit worker->calibrationLost(WR_CAL_UNKNOWN, QStringLiteral("stream restarted")); } },
            { "CL6r", "a state refresh reads it gone (no invalidated signal)",
              [&] { emit worker->calibrationStateRefreshed(WR_CAL_UNCALIBRATED); } },
            { "CL6u", "the presence check not measured (UNKNOWN)",
              [&] { emit worker->calibrationPresenceUnmeasured(5, WR_CAL_UNKNOWN); } },
            { "CL6p", "a new routine's phase event reads it gone",
              [&] { emit worker->calibrationPhaseEvent(WR_CALP_AWAIT_HORIZONTAL, WR_CALP_IDLE, WR_CAL_ABORT_NONE, WR_CAL_UNKNOWN, 0); } },
        };
        for (const End &e : ends) {
            calibrate();
            const bool wasHeld = bothHeld();
            before = events;
            e.run();
            check(wasHeld && noneHeld() && events - before == 2 && repo.saves == savesBefore,
                  QStringLiteral("H1 %1 %2: both records cleared, two RigChanged, the role-only state back, nothing saved")
                      .arg(QLatin1String(e.id), QLatin1String(e.what)));
        }
        check(!invalidatedWithRecords, "H1 CL5 the records are cleared before \"calibration invalidated\" is emitted");

        calibrate();
        before = events;
        delete hm;   // CL7
        settle();
        check(!recordOf(rig, lower) && !recordOf(rig, palm) && events - before == 2 && rig.stateId() == roleOnly,
              "H1 CL7 the object's teardown clears both records");
    }

    std::printf("\nR1 a role released or moved leaves the record (CL8, decision 4.6); a sensor with no role keeps its record\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        SensorFactsTable table;
        table.set(DeviceKey::fromPersistedId(u"W5"), on);
        auto *imu = new ImuInstance(witmotion("W5"), nullptr, nullptr, nullptr);
        imu->setCalibrationSink(&rig, rolesOf(rig));
        const PlacementKey key(DeviceKey::fromPersistedId(u"W5"));
        imu->setNominalCalibration(kRef1, false);   // calibrated with no role: a record-only entry
        imu->refineMountAboutLongAxis(kRef2, 5.0, false);
        const SensorSetup *x = sensorOf(rig, key);
        check(x && x->role == SegmentRole::Unknown && x->calibration && witmotionMismatch(rig, *imu, SegmentRole::Unknown).isEmpty(),
              "R1a a sensor calibrated while it holds no role: the Rig keeps a record-only entry, equal to the live alignment");
        rig.claimRole(DeviceKey::sensor("WT901BLE68", "W5"), SegmentRole::LeadUpperArm, table);
        x = sensorOf(rig, key);
        check(x && x->role == SegmentRole::LeadUpperArm && witmotionMismatch(rig, *imu, SegmentRole::Unknown).isEmpty(),
              "R1b a role claimed afterwards: the record stays, naming the role it was solved for");
        rig.claimRole(DeviceKey::sensor("WT901BLE68", "W5"), SegmentRole::Thorax, table);
        rig.releaseRole(DeviceKey::sensor("WT901BLE68", "W5"));
        x = sensorOf(rig, key);
        check(x && x->role == SegmentRole::Unknown && witmotionMismatch(rig, *imu, SegmentRole::Unknown).isEmpty(),
              "R1c a role moved, then released (CL8): the record stays with the live alignment, as decision 4.6 rules");
        delete imu;
        settle();
        check(!sensorOf(rig, key), "R1d the teardown clears it, and the entry, now empty, goes");
    }

    // L1 (#394 step 81): the HackMotion as the shot processor's deferred source. The gather stitches the block's lower
    // arm into lane 0 and its palm into lane 1 (shot_processor.cpp, finishGatherAndLaunch): the lanes must be the two
    // units' buffer sources in that order, or palm data would be stitched into the lower arm's lane with no error.
    std::printf("\nL1 a HackMotion's deferred lanes: lane 0 the lower arm's source, lane 1 the palm's\n");
    {
        pinpoint::EventBuffer buffer;   // not started: Idle, where registerSource is allowed
        auto *hm = new HmInstance(hackMotion(QStringLiteral("{0d7a1c2e-0000-4000-8000-0000000000b1}")), &buffer, nullptr,
                                  nullptr);
        pinpoint::DeferredSource *source = hm;
        const pinpoint::SourceId lower = hm->unitLowerArm()->sourceId(), palm = hm->unitPalm()->sourceId();
        check(lower != pinpoint::kInvalidSourceId && palm != pinpoint::kInvalidSourceId && lower != palm,
              "L1a both units are registered with the buffer, as two sources");
        check(source->deferredLane(0) == lower && source->deferredLane(1) == palm,
              "L1b lane 0 is the lower arm's source and lane 1 the palm's");
        check(source->deferredLane(2) == pinpoint::kInvalidSourceId && source->deferredLane(-1) == pinpoint::kInvalidSourceId,
              "L1c no other lane");
        check(source->deviceId() == hm->deviceId() && !source->historyPending(),
              "L1d its provenance key is the device id, and nothing is pending before a reservation");
        delete hm;
        settle();
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
