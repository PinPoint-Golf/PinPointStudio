/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// The trunk calibration's raw recording, written by its Session (FILE-SESS-11; platform refactor C-14, #380, which
// fixes #265): SessionStore::writeTrunkCeremony behind Session::saveTrunkCeremony, and the hand-over that holds a
// ceremony for its session (src/App/trunk_ceremony_hand_over.h), over the REAL session store on a temporary library and
// a Rig over an in-memory repository. Never the share, the real library, settings or devices.
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT (session_repository.h's writeTrunkCeremony, the hand-over's header,
// rig.h's attachEvidence) OR FROM THE FILESYSTEM, NEVER FROM RUNNING THE CODE.
//
//   C1  the bytes written are tcf::toJson's for a synthetic ceremony, byte for byte (today's writer wrote exactly
//       those); the file is trunk_ceremony.json, nothing copied aside
//   C2  a second and a third ceremony in one session: the earlier ones kept aside, trunk_ceremony_2.json then _3,
//       each byte-identical to what it was
//   C3  #265: the earlier file cannot be read (write-only), so it cannot be copied aside: nothing written, the earlier
//       file untouched, no aside file (POSIX permissions; skipped where an unreadable file cannot be made)
//   C4  the write itself fails (the earlier file read-only): refused, the earlier file untouched, no stray copy
//   C5  a session the store does not hold: refused
//   H1  the hand-over: written at once into the running session, byte-identical; nothing owed; saved
//   H2  #265: a failed write keeps it (owed, reported once); the next try, once the folder is back, writes it
//       byte-identical, the earlier one aside
//   H3  #265: no session to take it yet (its folder not made): kept, nothing reported; written when the folder appears
//   H4  #265: its session ends with it still unwritten: a last try, then dropped and reported; nothing written
//   H5  never into a later session: owed to one session, then another is running: dropped, the other has no file
//   H6  #265: quitting with it owed to the running session: written then; quitting with no session: dropped, logged
//   H7  a second session in the same run gets no file once the first was written
//   R1  the Rig's pointer after a good write: the solved sensor's record names the session and trunk_ceremony.json,
//       the rig state's id is unchanged and no RigChanged came; a solve no record matches gets none; a failed
//       write sets none; a second ceremony moves the first's pointer to the aside name
//
//   cmake --build build/tests --target ceremony_store_test -j 6
//   ctest --test-dir build/tests -R '^ceremony_store_test$' --output-on-failure

#include "../session_store.h"
#include "../session_files.h"   // the file names, to look at the folder

#include "../../App/trunk_ceremony_hand_over.h"
#include "../../Core/tests/isolated_stores.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/rig.h"
#include "../../DomainModel/session.h"
#include "../../IMU/trunk_ceremony_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>

#include <cstdio>
#include <memory>

using namespace pinpoint;
namespace tcf = trunk_ceremony_file;

namespace {

int g_fail = 0, g_run = 0, g_skip = 0;

void check(bool ok, const QString &label)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}
void skip(const QString &label, const QString &why)
{
    ++g_skip;
    std::printf("  [SKIP] %s — %s\n", qPrintable(label), qPrintable(why));
}

const QString   kName    = QStringLiteral("Test Golfer");
const QString   kToken   = QStringLiteral("Test-Golfer");
const AthleteId kAthlete = AthleteId::fromString(u"7c3e2a1b-4d5f-4a6b-8c9d-0e1f2a3b4c5d");
const QString   kPat     = QStringLiteral("date-name-type");

QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool plant(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QStringList ceremonyFiles(const QString &folder)
{
    return QDir(folder).entryList({ QStringLiteral("trunk_ceremony*.json") }, QDir::Files, QDir::Name);
}

// A synthetic ceremony, as the recorder holds one (trunk_ceremony_file_test builds its own the same way): two trunk
// lanes, a stand tall that solved both, and the turns. `tag` makes each one's bytes its own.
tcf::Ceremony fixture(int tag, const QDateTime &pelvisSolvedAt = {}, const QDateTime &thoraxSolvedAt = {})
{
    tcf::Ceremony c;
    c.createdUtc = QStringLiteral("2026-11-27T09:00:0%1.000Z").arg(tag % 10);
    c.endedUtc   = QStringLiteral("2026-11-27T09:01:0%1.000Z").arg(tag % 10);
    c.appVersion = QStringLiteral("test");
    c.handedness = QStringLiteral("right");
    c.beganUs    = 1'000'000;
    c.endedUs    = 61'000'000;
    for (const auto &[role, id] : { std::pair{ "pelvis", "W1" }, std::pair{ "thorax", "W2" } }) {
        tcf::Lane L;
        L.role             = QString::fromLatin1(role);
        L.serial           = QStringLiteral("WT-") + QString::fromLatin1(id);
        L.deviceId         = QString::fromLatin1(id);
        L.instrument       = tcf::instrumentWitmotion();
        L.configuredRateHz = 200;
        L.measuredRateHz   = 199.5;
        for (int i = 0; i < 400; ++i) {
            L.tUs.push_back(1'000'000 + 5'000 * i);
            L.rows.push_back(makeImuSample(0.01f * i, -0.98f, 0.1f * tag, 0.5f, -0.25f, 0.125f, 1.f, 0.f, 0.f, 0.f));
        }
        c.lanes.push_back(std::move(L));
    }
    tcf::Step st{ tcf::stepStandTall(), 1'500'000, 4'500'000, {} };
    st.result.insert(QStringLiteral("pelvis"), QVariantMap{ { QStringLiteral("valid"), true },
        { QStringLiteral("calibratedAt"), pelvisSolvedAt.toString(Qt::ISODateWithMs) } });
    st.result.insert(QStringLiteral("thorax"), QVariantMap{ { QStringLiteral("valid"), true },
        { QStringLiteral("calibratedAt"), thoraxSolvedAt.toString(Qt::ISODateWithMs) } });
    c.steps.push_back(st);
    c.steps.push_back({ tcf::stepTurnAway(), 5'000'000, 12'000'000, { { QStringLiteral("valid"), true } } });
    c.steps.push_back({ tcf::stepTurnThrough(), 13'000'000, 20'000'000, { { QStringLiteral("valid"), true } } });
    return c;
}

// One scratch library: the real session store, an athlete over it, a Rig over memory, and the hand-over between them.
class MemoryRig final : public RigRepository
{
public:
    RigLoad load() override { return {}; }
    bool    save(const RigId &, const rig::RigParts &) override { ++saves; return true; }
    int saves = 0;
};

struct Bay {
    QTemporaryDir                          dir;
    SessionStore                           store;
    std::unique_ptr<Athlete>               athlete;
    MemoryRig                              rigRepo;
    Rig                                    rig{ rigRepo };
    Session                               *open = nullptr;   // what the composition root's "running session" answers
    std::unique_ptr<TrunkCeremonyHandOver> handOver;
    QStringList                            notSaved;
    int                                    saved = 0, rigChanged = 0;

    Bay()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, dir.path() + QLatin1Char('/') + kToken, SessionOwner{ kAthlete, kName, dir.path() },
                           Athlete::Source::Library, false);
        rig.load();
        QObject::connect(&rig, &Rig::rigChanged, &rig, [this] { ++rigChanged; });
        handOver = std::make_unique<TrunkCeremonyHandOver>([this] { return open; }, &rig);
        QObject::connect(handOver.get(), &TrunkCeremonyHandOver::notSaved, handOver.get(),
                         [this](const QString &why) { notSaved << why; });
        QObject::connect(handOver.get(), &TrunkCeremonyHandOver::saved, handOver.get(), [this] { ++saved; });
    }
    Session *start(const QString &type = QStringLiteral("swing")) { return athlete->startSession(type, kPat); }
    static QString folderOf(Session *s) { const auto e = s ? s->entry() : std::nullopt; return e ? e->folder : QString(); }
};

QList<TrunkCeremonySolve> solvesOf(const QDateTime &pelvisAt, const QDateTime &thoraxAt)
{
    return { { QStringLiteral("W1"), pelvisAt }, { QStringLiteral("W2"), thoraxAt } };
}

const rig::CalibrationRecord *recordOf(const Rig &rig, const QString &id)
{
    for (const rig::SensorSetup &x : rig.parts().state.sensors)
        if (x.key == rig::PlacementKey(rig::DeviceKey::fromPersistedId(id)) && x.calibration) return &*x.calibration;
    return nullptr;
}

rig::CalibrationRecord alignment(const QString &id, const QDateTime &at)
{
    rig::SensorAlignmentParameters p;
    p.role      = analysis::SegmentRole::Pelvis;
    p.alignment = rig::SensorAlignment{ QQuaternion(), QQuaternion(), 0.0, 0.0 };
    return rig::CalibrationRecord::forSensor(rig::PlacementKey(rig::DeviceKey::fromPersistedId(id)), rig::CalibrationMethod::Solved,
                                             p, rig::Uncertainty::notKnown(), at);
}

// ── The store ────────────────────────────────────────────────────────────────────────────────────────────────────────

void testStore()
{
    std::printf("\nC1–C5 the store: SessionStore::writeTrunkCeremony through Session::saveTrunkCeremony\n");
    Bay b;
    Session *s = b.start();
    const QString folder = Bay::folderOf(s);
    check(s && !folder.isEmpty(), "a session is running, its folder made");
    const QString path = QDir(folder).filePath(SessionFiles::trunkCeremonyFileName());

    const QByteArray one = tcf::toJson(fixture(1)), two = tcf::toJson(fixture(2)), three = tcf::toJson(fixture(3));
    check(one != two && two != three, "three different ceremonies");
    TrunkCeremonyWrite w = s->saveTrunkCeremony(one);
    check(w.ok() && w.fileName == QLatin1String("trunk_ceremony.json") && w.asideName.isEmpty() && bytesOf(path) == one
              && ceremonyFiles(folder) == QStringList{ QStringLiteral("trunk_ceremony.json") },
          "C1 the file holds tcf::toJson's bytes exactly, named trunk_ceremony.json, nothing aside");

    w = s->saveTrunkCeremony(two);
    const QString aside2 = QDir(folder).filePath(QStringLiteral("trunk_ceremony_2.json"));
    check(w.ok() && w.asideName == QLatin1String("trunk_ceremony_2.json") && bytesOf(path) == two && bytesOf(aside2) == one,
          "C2a a second ceremony: the first kept aside as trunk_ceremony_2.json, byte-identical");
    w = s->saveTrunkCeremony(three);
    check(w.ok() && w.asideName == QLatin1String("trunk_ceremony_3.json") && bytesOf(path) == three && bytesOf(aside2) == one
              && bytesOf(QDir(folder).filePath(QStringLiteral("trunk_ceremony_3.json"))) == two,
          "C2b a third: the second kept as trunk_ceremony_3.json, the first untouched");

    // C3: the earlier file cannot be read, so it cannot be kept.
    {
        Session *t = b.start(QStringLiteral("wrist"));
        const QString f = Bay::folderOf(t);
        const QString p = QDir(f).filePath(SessionFiles::trunkCeremonyFileName());
        check(plant(p, one), "C3 an earlier ceremony planted");
        const QFile::Permissions was = QFile::permissions(p);
        // Write-only: it cannot be read (so not copied aside) but could still be replaced, which is #265's loss.
        if (!QFile::setPermissions(p, QFile::WriteOwner) || QFile(p).open(QIODevice::ReadOnly)) {
            QFile::setPermissions(p, was);
            skip(QStringLiteral("C3 an unreadable earlier file"), QStringLiteral("this filesystem cannot make one"));
        } else {
            const TrunkCeremonyWrite x = t->saveTrunkCeremony(two);
            QFile::setPermissions(p, was);
            check(!x.ok() && !x.result.error.isEmpty(), QStringLiteral("C3a #265: refused, and says why (%1)").arg(x.result.error));
            check(bytesOf(p) == one, "C3b #265: the earlier ceremony is untouched (it was not kept aside, so not replaced)");
            check(ceremonyFiles(f) == QStringList{ QStringLiteral("trunk_ceremony.json") }, "C3c no aside file is left");
        }
    }
    // C4: the write itself fails: the earlier file is read-only (atomicWrite refuses it, every platform).
    {
        Session *t = b.start(QStringLiteral("grf"));
        const QString f = Bay::folderOf(t);
        const QString p = QDir(f).filePath(SessionFiles::trunkCeremonyFileName());
        check(plant(p, one), "C4 an earlier ceremony planted");
        const QFile::Permissions was = QFile::permissions(p);
        QFile::setPermissions(p, QFile::ReadOwner);
        const TrunkCeremonyWrite x = t->saveTrunkCeremony(two);
        QFile::setPermissions(p, was);
        check(!x.ok() && x.asideName.isEmpty() && bytesOf(p) == one,
              QStringLiteral("C4a refused, the earlier ceremony untouched (%1)").arg(x.result.error));
        check(ceremonyFiles(f) == QStringList{ QStringLiteral("trunk_ceremony.json") },
              "C4b no stray copy of the earlier one is left aside");
    }
    {
        const TrunkCeremonyWrite x = b.store.writeTrunkCeremony(SessionId::create(), one);
        check(!x.ok() && x.fileName.isEmpty(), "C5 a session the store does not hold: refused");
    }
}

// ── The hand-over ────────────────────────────────────────────────────────────────────────────────────────────────────

void testHandOver()
{
    std::printf("\nH1–H7 the hand-over: kept until written, within its own session only\n");
    const QByteArray one = tcf::toJson(fixture(1)), two = tcf::toJson(fixture(2));
    {
        Bay b;
        b.open = b.start();
        const QString f = Bay::folderOf(b.open);
        b.handOver->take(one, {});
        check(bytesOf(QDir(f).filePath(QStringLiteral("trunk_ceremony.json"))) == one && b.handOver->owed() == 0
                  && b.notSaved.isEmpty() && b.saved == 1,
              "H1 written at once into the running session, byte-identical; nothing owed; saved");

        // H7: the next session in the same run gets no file.
        Session *next = b.start(QStringLiteral("wrist"));
        b.open = next;
        b.handOver->retry();
        b.handOver->sessionEnding();
        check(ceremonyFiles(Bay::folderOf(next)).isEmpty(), "H7 a second session in the same run gets no file");
    }
    {
        Bay b;
        b.open = b.start();
        const QString f = Bay::folderOf(b.open);
        const QString p = QDir(f).filePath(QStringLiteral("trunk_ceremony.json"));
        // The first write fails: the session's folder has gone from under it.
        check(QDir(f).removeRecursively(), "H2 the session's folder taken away");
        b.handOver->take(one, {});
        check(b.handOver->owed() == 1 && b.notSaved.size() == 1 && b.saved == 0,
              QStringLiteral("H2a #265: a failed write keeps it (owed 1) and says so once (%1)").arg(b.notSaved.value(0)));
        QDir().mkpath(f);
        b.handOver->retry();
        check(b.handOver->owed() == 0 && bytesOf(p) == one && b.saved == 1 && b.notSaved.size() == 1,
              "H2b #265: the next try writes it, byte-identical to tcf::toJson's");
        // A read-only earlier file: the write fails after the copy aside; the next try copies and writes.
        const QFile::Permissions was = QFile::permissions(p);
        QFile::setPermissions(p, QFile::ReadOwner);
        b.handOver->take(two, {});
        check(b.handOver->owed() == 1 && b.notSaved.size() == 2 && bytesOf(p) == one,
              "H2c a second ceremony whose write fails: kept, reported, the first untouched");
        QFile::setPermissions(p, was);
        b.handOver->retry();
        check(b.handOver->owed() == 0 && bytesOf(p) == two
                  && bytesOf(QDir(f).filePath(QStringLiteral("trunk_ceremony_2.json"))) == one
                  && ceremonyFiles(f).size() == 2,
              "H2d then written, the first kept aside once (no stray copy from the failed try)");
    }
    {
        Bay b;
        b.handOver->take(one, {});   // the session started, but its folder could not be made: none is open yet
        check(b.handOver->owed() == 1 && b.notSaved.isEmpty(), "H3a #265: no session folder yet: kept, nothing reported");
        b.open = b.start();          // the first swing makes the folder (ShotProcessor::captureSession)
        b.handOver->retry();
        check(b.handOver->owed() == 0 && bytesOf(QDir(Bay::folderOf(b.open)).filePath(QStringLiteral("trunk_ceremony.json"))) == one,
              "H3b #265: written when the session's folder appears");
    }
    {
        Bay b;
        b.open = b.start();
        const QString f = Bay::folderOf(b.open);
        QDir(f).removeRecursively();
        b.handOver->take(one, {});
        b.handOver->sessionEnding();
        check(b.handOver->owed() == 0 && b.notSaved.size() == 2 && !QFileInfo::exists(f),
              "H4 #265: its session ends with it unwritten: a last try, then dropped and reported (never silent)");
    }
    {
        Bay b;
        Session *first = b.start();
        b.open = first;
        const QString f1 = Bay::folderOf(first);
        QDir(f1).removeRecursively();
        b.handOver->take(one, {});
        QDir().mkpath(f1);
        Session *later = b.start(QStringLiteral("wrist"));   // another session, the first never ended through us
        b.open = later;
        b.handOver->retry();
        check(b.handOver->owed() == 0 && ceremonyFiles(Bay::folderOf(later)).isEmpty() && ceremonyFiles(f1).isEmpty(),
              "H5 never into a later session: owed to one, another running: dropped, neither has a file");
    }
    {
        Bay b;
        b.open = b.start();
        const QString f = Bay::folderOf(b.open);
        QDir(f).removeRecursively();
        b.handOver->take(one, {});
        QDir().mkpath(f);
        b.handOver->quitting();
        check(b.handOver->owed() == 0 && bytesOf(QDir(f).filePath(QStringLiteral("trunk_ceremony.json"))) == one,
              "H6a #265: quitting with it owed to the running session: written then");
        Bay c;
        c.handOver->take(one, {});
        c.handOver->quitting();
        check(c.handOver->owed() == 0, "H6b quitting with no session to take it: dropped (the loss is logged)");
    }
}

// ── The Rig's pointer ────────────────────────────────────────────────────────────────────────────────────────────────

void testRigPointer()
{
    std::printf("\nR1 the Rig's pointer, set by the hand-over after a good write\n");
    const QDateTime t1 = QDateTime::fromMSecsSinceEpoch(1764230400123, QTimeZone::UTC);
    const QDateTime t2 = QDateTime::fromMSecsSinceEpoch(1764230500456, QTimeZone::UTC);
    const QDateTime t3 = QDateTime::fromMSecsSinceEpoch(1764230600789, QTimeZone::UTC);
    Bay b;
    b.rig.commit(alignment(QStringLiteral("W1"), t1));
    b.rig.commit(alignment(QStringLiteral("W2"), t2));
    const RigStateId held = b.rig.stateId();
    const int changed = b.rigChanged, saves = b.rigRepo.saves;
    b.open = b.start();
    const SessionId sid = b.open->id();
    const QString f = Bay::folderOf(b.open);

    // A failed write first: no pointer.
    QDir(f).removeRecursively();
    b.handOver->take(tcf::toJson(fixture(1, t1, t3)), solvesOf(t1, t3));
    check(!recordOf(b.rig, QStringLiteral("W1"))->evidence, "R1a a failed write sets no pointer");
    QDir().mkpath(f);
    b.handOver->retry();
    const rig::CalibrationRecord *w1 = recordOf(b.rig, QStringLiteral("W1"));
    const rig::CalibrationRecord *w2 = recordOf(b.rig, QStringLiteral("W2"));
    check(w1 && w1->evidence && w1->evidence->sessionId == sid && w1->evidence->fileName == QLatin1String("trunk_ceremony.json"),
          "R1b written: the solved sensor's record names the session and trunk_ceremony.json");
    check(w2 && !w2->evidence, "R1c a sensor whose record is not this ceremony's solve gets no pointer");
    check(b.rig.stateId() == held && b.rigChanged == changed && b.rigRepo.saves == saves,
          "R1d the rig state's id is unchanged, no RigChanged, nothing saved");

    // A second ceremony that solved W2 only: the first file goes aside, and W1's pointer with it.
    b.handOver->take(tcf::toJson(fixture(2, t3, t2)), solvesOf(t3, t2));
    w1 = recordOf(b.rig, QStringLiteral("W1"));
    w2 = recordOf(b.rig, QStringLiteral("W2"));
    check(w1 && w1->evidence && w1->evidence->fileName == QLatin1String("trunk_ceremony_2.json")
              && w2 && w2->evidence && w2->evidence->fileName == QLatin1String("trunk_ceremony.json")
              && b.rig.stateId() == held && b.rigChanged == changed,
          "R1e a second ceremony: the first's pointer follows it to trunk_ceremony_2.json, the second's names "
          "trunk_ceremony.json; still the same state, no event");
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // every per-user store is scratch, first
    QCoreApplication app(argc, argv);
    std::printf("ceremony_store_test — the trunk calibration's raw recording, written by its Session (C-14, #265)\n");
    testStore();
    testHandOver();
    testRigPointer();
    std::printf("\n%d checks, %d failed, %d skipped\n", g_run, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
