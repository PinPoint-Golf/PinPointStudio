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

// rig_commands_test: the Rig's camera commands (src/DomainModel/rig.*, rig_commands.h; docs/design/rig_design.md §4.1,
// §4.2; rig_design_addendum.md decisions 1, 8 and 9). Most cases run over an in-memory repository that counts saves
// and can fail; K8 and K9 run over the real store in a temporary folder. IsolatedStores is made first (#187).
//
//   K1  each versioned command makes a new state: one save, then one RigChanged with the new RigRef
//   K2  Impact is exclusive across three cameras, through the commands; the camera that loses it becomes None
//   K3  putting a camera back as it was gives the earlier RigStateId back, from a camera first seen by a command
//   K4  a command that leaves the digest unchanged saves nothing and emits nothing
//   K5  the box and the fixed flag are held side by side: a box is kept while its camera is not fixed
//   K6  an alias or an enabled flag: no RigChanged, exactly one RigDeviceLabelChanged (the key, and which fact),
//       after the save; none for a no-op
//   K7  the alias is looked up by its own spelling: two descriptions of one sensor id are two entries (decision 9)
//   K8  a perspective code this build does not know survives a save and a load
//   K9  a failed save still changes the state in memory (and its event is emitted); the next command saves again
//   K10 a key of the wrong kind changes nothing
//   K11 the Rig remembers a change whose save failed and saves it again at quit (P4), the launch's own save too;
//       nothing is saved at quit when nothing is unsaved, nor over a file this build must not write

#include "../../Core/tests/isolated_stores.h"

#include "../rig.h"
#include "../../Core/PpMessageLog.h"
#include "../../Stores/rig_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <functional>

using namespace pinpoint;
using namespace pinpoint::rig;

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

// An in-memory repository: nothing stored, every save counted (and kept), optionally failing.
class MemoryRepository final : public RigRepository
{
public:
    RigLoad load() override { return loaded; }
    bool    save(const RigId &id, const RigParts &parts) override
    {
        ++saves;
        if (fail) return false;
        savedId    = id;
        savedParts = parts;
        return true;
    }
    int      saves = 0;
    bool     fail  = false;
    RigLoad  loaded;   // what load() hands the Rig: nothing stored, unless a case says otherwise
    RigId    savedId;
    RigParts savedParts;
};

const int kNone = recording::perspectiveCode(recording::Perspective::None);
const int kFaceOn = recording::perspectiveCode(recording::Perspective::FaceOn);
const int kDtl = recording::perspectiveCode(recording::Perspective::DownTheLine);
const int kImpact = recording::perspectiveCode(recording::Perspective::Impact);

const DeviceKey camA = DeviceKey::camera("Blackfly S BFS-U3-16S2M", "24157001", "usb-1");
const DeviceKey camB = DeviceKey::camera("Blackfly S BFS-U3-16S2M", "24157002", "usb-2");
const DeviceKey camC = DeviceKey::camera("FaceTime HD Camera", "", "0x1420000005ac8600");

// Every event the Rig emits, in order, with the save count when it came.
struct Events {
    explicit Events(Rig &rig, const MemoryRepository *repo = nullptr)
    {
        QObject::connect(&rig, &Rig::rigChanged, &rig, [this, repo](const RigRef &r) {
            changed << r;
            savesAtChanged << (repo ? repo->saves : -1);
        });
        QObject::connect(&rig, &Rig::rigDeviceLabelChanged, &rig, [this, repo](const DeviceKey &k, RigDeviceLabel w) {
            labels << qMakePair(k, w);
            savesAtLabel << (repo ? repo->saves : -1);
        });
    }
    void clear() { changed.clear(); labels.clear(); savesAtChanged.clear(); savesAtLabel.clear(); }
    QList<RigRef>                           changed;
    QList<QPair<DeviceKey, RigDeviceLabel>> labels;
    QList<int>                              savesAtChanged, savesAtLabel;
};

const CameraSetup *cameraOf(const Rig &rig, const DeviceKey &k)
{
    for (const CameraSetup &c : rig.parts().state.cameras)
        if (c.key() == k) return &c;
    return nullptr;
}
int codeOf(const Rig &rig, const DeviceKey &k)
{
    const CameraSetup *c = cameraOf(rig, k);
    return c ? c->perspectiveCode() : kNone;
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("rig_commands_test — the temporary directory: %s\n", qPrintable(stores.path()));

    std::printf("\nK1 each versioned command makes a new state, saved, then one RigChanged\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        const int savesAtLoad = repo.saves;
        struct Step { const char *name; std::function<void()> run; std::function<bool()> holds; };
        const Step steps[] = {
            { "assignPerspective", [&] { rig.assignPerspective(camA, kFaceOn); }, [&] { return codeOf(rig, camA) == kFaceOn; } },
            { "setMirrored",       [&] { rig.setMirrored(camA, true); },          [&] { return cameraOf(rig, camA) && cameraOf(rig, camA)->mirrored; } },
            { "setFixedInPlace",   [&] { rig.setFixedInPlace(camA, true); },      [&] { return cameraOf(rig, camA) && cameraOf(rig, camA)->fixedInPlace; } },
            { "setHittingArea",    [&] { rig.setHittingArea(camA, QRectF(0.25, 0.5, 0.125, 0.25)); },
              [&] { return cameraOf(rig, camA) && cameraOf(rig, camA)->hittingArea == QRectF(0.25, 0.5, 0.125, 0.25); } },
            { "clearHittingArea",  [&] { rig.clearHittingArea(camA); },           [&] { return cameraOf(rig, camA) && !cameraOf(rig, camA)->hittingArea; } },
        };
        int n = 0;
        for (const Step &st : steps) {
            const RigStateId before = rig.stateId();
            const QDateTime  since  = rig.parts().state.sinceUtc;
            ev.clear();
            st.run();
            ++n;
            check(st.holds() && rig.stateId() != before && rig.stateId() == rigStateIdOf(rig.parts().state)
                      && rig.parts().state.sinceUtc.isValid() && rig.parts().state.sinceUtc >= since,
                  QStringLiteral("K1 %1: the new state is in force, with its own id and sinceUtc").arg(st.name));
            check(repo.saves == savesAtLoad + n && repo.savedParts.state.cameras.size() == rig.parts().state.cameras.size()
                      && rigStateIdOf(repo.savedParts.state) == rig.stateId(),
                  QStringLiteral("K1 %1: saved once, the whole new state").arg(st.name));
            check(ev.changed.size() == 1 && ev.changed.first() == rig.ref() && ev.changed.first().rig() == rig.id()
                      && ev.savesAtChanged.first() == repo.saves && ev.labels.isEmpty(),
                  QStringLiteral("K1 %1: one RigChanged, after the save, carrying the new RigRef").arg(st.name));
        }
    }

    std::printf("\nK2 Impact is exclusive, across three cameras\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kImpact);
        rig.assignPerspective(camB, kDtl);
        rig.assignPerspective(camC, kFaceOn);
        rig.setMirrored(camA, true);   // so A keeps an entry when it loses Impact
        Events ev(rig, &repo);
        rig.assignPerspective(camB, kImpact);
        check(codeOf(rig, camB) == kImpact && codeOf(rig, camA) == kNone && codeOf(rig, camC) == kFaceOn,
              "K2a B takes Impact: A, which held it, becomes None; C keeps face-on");
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->mirrored, "K2b A keeps its other facts (mirrored)");
        check(ev.changed.size() == 1, "K2c one RigChanged for the one command");
        rig.assignPerspective(camC, kImpact);
        int holders = 0;
        for (const CameraSetup &c : rig.parts().state.cameras) holders += c.perspectiveCode() == kImpact ? 1 : 0;
        check(codeOf(rig, camC) == kImpact && codeOf(rig, camB) == kNone && holders == 1,
              "K2d the third camera takes Impact: exactly one holder");
        check(!cameraOf(rig, camB), "K2e B, now all defaults, has no entry (the same as no entry)");
        rig.assignPerspective(camA, kDtl);
        check(codeOf(rig, camC) == kImpact && codeOf(rig, camA) == kDtl, "K2f a non-Impact perspective takes nothing from anyone");
    }

    std::printf("\nK3 putting a camera back gives the earlier id back\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const RigStateId empty = rig.stateId();
        rig.assignPerspective(camA, kFaceOn);   // a camera first seen by a command
        const RigStateId faceOn = rig.stateId();
        rig.setMirrored(camA, true);
        check(rig.stateId() != faceOn, "K3a mirrored makes another state");
        rig.setMirrored(camA, false);
        check(rig.stateId() == faceOn, "K3b mirrored back: the face-on state's id again");
        rig.assignPerspective(camA, kNone);
        check(rig.stateId() == empty && rig.parts().state.cameras.isEmpty(),
              "K3c perspective back to None: the empty rig's id, and no camera entry");
    }

    std::printf("\nK4 a command that changes nothing\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kFaceOn);
        rig.setHittingArea(camA, QRectF(0.1, 0.2, 0.3, 0.4));
        Events ev(rig, &repo);
        const int saves = repo.saves;
        rig.assignPerspective(camA, kFaceOn);
        rig.setMirrored(camA, false);
        rig.setFixedInPlace(camA, false);
        rig.setHittingArea(camA, QRectF(0.1, 0.2, 0.3, 0.4));
        rig.clearHittingArea(camB);
        rig.setMirrored(camB, false);   // an unknown camera set to its default: still no entry
        check(ev.changed.isEmpty() && ev.labels.isEmpty(), "K4a no event");
        check(repo.saves == saves, "K4b nothing saved");
        check(!cameraOf(rig, camB), "K4c no entry made for a camera left at its defaults");
    }

    std::printf("\nK5 the box and the fixed flag side by side\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.setHittingArea(camA, QRectF(0.4, 0.6, 0.2, 0.1));
        check(cameraOf(rig, camA) && !cameraOf(rig, camA)->fixedInPlace && cameraOf(rig, camA)->hittingArea,
              "K5a a box is held for a camera that is not fixed");
        rig.setFixedInPlace(camA, true);
        const RigStateId fixedWithBox = rig.stateId();
        rig.setFixedInPlace(camA, false);
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->hittingArea == QRectF(0.4, 0.6, 0.2, 0.1), "K5b un-fixing keeps the box");
        check(rig.stateId() != fixedWithBox, "K5c the flag is in the digest");
        rig.setFixedInPlace(camA, true);
        check(rig.stateId() == fixedWithBox, "K5d re-fixing brings the same state back, box and all");
    }

    std::printf("\nK6 an alias or an enabled flag\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kFaceOn);
        const RigStateId state = rig.stateId();
        Events ev(rig, &repo);
        int saves = repo.saves;
        rig.setAlias(camA, "Face on");
        check(ev.changed.isEmpty() && rig.stateId() == state, "K6a setAlias: no new state, no RigChanged");
        check(ev.labels.size() == 1 && ev.labels.first().first == camA && ev.labels.first().second == RigDeviceLabel::Alias
                  && repo.saves == saves + 1 && ev.savesAtLabel.first() == repo.saves
                  && repo.savedParts.other.aliases.size() == 1 && repo.savedParts.other.aliases.first().second == "Face on",
              "K6b exactly one RigDeviceLabelChanged (the camera, Alias), after the save");
        ev.clear(); saves = repo.saves;
        rig.setAlias(camA, "Face on");
        check(ev.labels.isEmpty() && repo.saves == saves, "K6c the same alias again: nothing saved, nothing emitted");
        rig.setAlias(camA, "");
        check(ev.labels.size() == 1 && rig.parts().other.aliases.isEmpty() && repo.saves == saves + 1,
              "K6d an empty alias removes it: one event");
        ev.clear(); saves = repo.saves;
        rig.setAlias(camB, "");
        check(ev.labels.isEmpty() && repo.saves == saves, "K6e removing an alias that is not there: nothing");
        rig.setEnabledOnThisMachine(camB, false);
        check(ev.changed.isEmpty() && rig.stateId() == state && ev.labels.size() == 1 && ev.labels.first().first == camB
                  && ev.labels.first().second == RigDeviceLabel::EnabledOnThisMachine && repo.saves == saves + 1
                  && ev.savesAtLabel.first() == repo.saves,
              "K6f disabling: no RigChanged, one RigDeviceLabelChanged (the camera, EnabledOnThisMachine), after the save");
        ev.clear(); saves = repo.saves;
        rig.setEnabledOnThisMachine(camB, false);
        rig.setEnabledOnThisMachine(camC, true);
        check(ev.labels.isEmpty() && repo.saves == saves, "K6g no change (disabled again; enabled, the default): nothing");
        rig.setEnabledOnThisMachine(camB, true);
        check(ev.labels.size() == 1 && rig.parts().other.enabledOnThisMachine.isEmpty(),
              "K6h re-enabling: one event, and the default is not stored");
    }

    std::printf("\nK7 the alias by its own spelling (decision 9)\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const DeviceKey one = DeviceKey::sensor("WT901BLE68", "{767a6a67-0000-4000-8000-000000000002}");
        const DeviceKey two = DeviceKey::sensor("WT901BLE67", "{767a6a67-0000-4000-8000-000000000002}");
        rig.setAlias(one, "Pelvis");
        rig.setAlias(two, "Chest");
        check(one == two && rig.parts().other.aliases.size() == 2,
              "K7 two descriptions of one sensor id (equal keys) keep two aliases, as imu/alias does");
    }

    std::printf("\nK8 an unknown perspective code survives a save and a load\n");
    {
        const QString f = stores.path() + "/k8/rig";
        RigId id;
        {
            RigStore store(f);
            Rig      rig(store);
            rig.load();
            rig.assignPerspective(camA, 7);   // a newer build's sixth perspective
            id = rig.id();
        }
        RigStore store(f);
        Rig      rig(store);
        rig.load();
        check(rig.id() == id && codeOf(rig, camA) == 7
                  && cameraOf(rig, camA) && cameraOf(rig, camA)->perspective() == recording::Perspective::None,
              "K8 the code 7 is read back as 7 (and reads as None)");
    }

    std::printf("\nK9 a failed save\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        repo.fail = true;
        rig.assignPerspective(camA, kDtl);
        rig.setAlias(camA, "Down the line");
        check(codeOf(rig, camA) == kDtl && rig.parts().other.aliases.size() == 1 && repo.saves == 3,
              "K9a both changes stand in memory, each having tried its save");
        check(ev.changed.size() == 1 && ev.changed.first() == rig.ref() && ev.labels.size() == 1,
              "K9b each still emits its event (the state in force is the new one)");
        repo.fail = false;
        rig.setMirrored(camA, true);
        check(repo.savedParts.state.cameras.size() == 1 && repo.savedParts.state.cameras.first().perspectiveCode() == kDtl
                  && repo.savedParts.other.aliases.size() == 1 && repo.savedParts.state.cameras.first().mirrored,
              "K9c the next command saves the whole rig, the earlier changes with it");

        // The same through the real store: a folder that cannot be made, then made.
        const QString blocker = stores.path() + "/k9-blocked";
        {
            QFile b(blocker);
            if (b.open(QIODevice::WriteOnly)) b.write("in the way");
        }
        int seq = 0;
        PpMessageLog::instance()->fetchSince(seq);
        RigStore store(blocker + "/rig");
        Rig      real(store);
        real.load();
        real.assignPerspective(camA, kFaceOn);
        QStringList warned;
        for (const auto &e : PpMessageLog::instance()->fetchSince(seq)) warned << e.message;
        check(codeOf(real, camA) == kFaceOn && warned.join('\n').contains("[RigStore] not saved"),
              "K9d over the store: the change stands and the failed save is logged");
        QFile::remove(blocker);
        real.setMirrored(camA, true);
        RigStore again(blocker + "/rig");
        const RigLoad back = again.load();
        check(back.id == real.id() && back.parts.state.cameras.size() == 1
                  && back.parts.state.cameras.first().perspectiveCode() == kFaceOn && back.parts.state.cameras.first().mirrored,
              "K9e once the folder can be made, the next command writes the whole rig, with its RigId");
    }

    std::printf("\nK10 a key of the wrong kind\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        const int saves = repo.saves;
        const DeviceKey sensor = DeviceKey::sensor("WT901BLE68", "{767a6a67-0000-4000-8000-000000000003}");
        rig.assignPerspective(sensor, kFaceOn);
        rig.setMirrored(DeviceKey(), true);
        rig.setHittingArea(sensor, QRectF(0, 0, 1, 1));
        rig.setAlias(DeviceKey(), "x");
        rig.setEnabledOnThisMachine(DeviceKey(), false);
        check(ev.changed.isEmpty() && ev.labels.isEmpty() && repo.saves == saves && rig.parts().state.cameras.isEmpty(),
              "K10 a sensor's key given a camera command, or a null key: nothing");
    }

    std::printf("\nK11 a change whose save failed, saved again at quit (P4)\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        check(!rig.hasUnsavedChange(), "K11a after a launch that saved: nothing unsaved");
        int saves = repo.saves;
        rig.saveIfUnsaved();
        check(repo.saves == saves, "K11b nothing unsaved: the quit saves nothing");
        repo.fail = true;
        rig.assignPerspective(camA, kDtl);
        check(rig.hasUnsavedChange(), "K11c a failed save is remembered");
        repo.fail = false;
        saves = repo.saves;
        rig.saveIfUnsaved();
        check(repo.saves == saves + 1 && repo.savedParts.state.cameras.size() == 1
                  && repo.savedParts.state.cameras.first().perspectiveCode() == kDtl && !rig.hasUnsavedChange(),
              "K11d at quit the change is saved again, whole");
        repo.fail = true;
        rig.setAlias(camA, "Down the line");
        repo.fail = false;
        rig.setMirrored(camA, true);
        check(!rig.hasUnsavedChange() && repo.savedParts.other.aliases.size() == 1,
              "K11e the next command's save also clears it, as before");
    }
    {
        MemoryRepository repo;
        repo.fail = true;
        Rig rig(repo);
        rig.load();   // a new RigId, whose save fails
        check(rig.hasUnsavedChange(), "K11f the launch's own save failed: remembered");
        repo.fail = false;
        rig.saveIfUnsaved();
        check(repo.savedId == rig.id() && !rig.hasUnsavedChange(), "K11g and saved at quit, with the RigId");
    }
    {
        MemoryRepository repo;
        repo.loaded.source = RigLoad::Source::ReadOnly;
        repo.loaded.id     = RigId::create();
        repo.fail          = true;   // as the store refuses every save over a file this build must not write
        Rig rig(repo);
        rig.load();
        rig.setMirrored(camA, true);
        const int saves = repo.saves;
        rig.saveIfUnsaved();
        check(!rig.hasUnsavedChange() && repo.saves == saves,
              "K11h a read-only file: a refused save is not an unsaved change, and the quit tries nothing");
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
