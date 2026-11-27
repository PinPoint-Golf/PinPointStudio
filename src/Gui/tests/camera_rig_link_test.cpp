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

// camera_rig_link_test: the camera manager's link to the Rig (src/Gui/cameras/camera_rig_link.*; rig_design.md §4.1;
// rig_design_addendum.md decision 8), the rules CameraManager follows once the Rig writes the cameras' set-up, tested
// without a camera. Written before the rules (BT-40): run first against a link that did nothing, where every case that
// expects an answer failed (L1–L6 but L2d, L3i and L4c, which expect nothing; L7 tests the presenter, real code).
// IsolatedStores is made first (#187); the Rig runs over an in-memory repository.
//
//   L1  one camera's facts, read from the two parts by its key: an absent entry is every default; the alias by its own
//       spelling; the enabled flag
//   L2  what a newly connected camera is given (createController's restore): the perspective only above None, pose off
//       for Impact, the mirror flag, the box only while the camera is fixed in place, never an empty box
//   L3  what a change of the rig state does to the live cameras (decision 8's table): a perspective is set on the live
//       instance and the camera list changes; a selected live camera whose Impact-ness changed is reconnected, the one
//       that lost Impact too; mirrored and the box are set live; fixed in place applies nothing; no change, nothing
//   L4  the manager's writers as requests: one Rig command each; the box normalised and clipped to the frame, a box
//       with no area a clear; the alias trimmed, an empty one a removal; Enable is "enabled on this machine"
//   L5  the alias seeded at launch for each listed camera with none: its key with every '|' a space, once
//   L6  through the real Rig: Impact moved from one camera to another strips the first, and both selected live cameras
//       are reconnected, after the save
//   L8  a camera's enabled flag changed (decision 8's row, moved out of the manager: fix round B3): a listed camera's
//       flag, the session's list and the selection follow; a flag that already says so does nothing; a camera not
//       listed changes the session's list only
//   L7  app.rig (RigPresenter) over the real Rig: each camera's facts read by its camera key; each command forwarded;
//       camerasChanged once per change, for a versioned change and for a camera's alias or enabled flag; a key that is
//       not a camera key changes nothing

#include "../../Core/tests/isolated_stores.h"

#include "../app/rig_presenter.h"
#include "../cameras/camera_rig_link.h"
#include "../../DomainModel/rig.h"

#include <QCoreApplication>

#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::rig;
using namespace pp_camrig;

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

const int kNone   = recording::perspectiveCode(recording::Perspective::None);
const int kDtl    = recording::perspectiveCode(recording::Perspective::DownTheLine);
const int kFaceOn = recording::perspectiveCode(recording::Perspective::FaceOn);
const int kImpact = recording::perspectiveCode(recording::Perspective::Impact);

const DeviceKey camA = DeviceKey::camera("Blackfly S BFS-U3-16S2C", "12345678", "usb-1");
const DeviceKey camB = DeviceKey::camera("Chameleon3 CM3-U3-13Y3M", "87654321", "usb-2");
const DeviceKey camC = DeviceKey::camera("FaceTime HD Camera", "", "0x1420000005ac8600");

// Every command asked for, in order.
class RecordingCommands final : public RigCameraCommands
{
public:
    void assignPerspective(const DeviceKey &c, int p) override { calls << QStringLiteral("perspective %1 %2").arg(c.persistedCameraKey()).arg(p); }
    void setMirrored(const DeviceKey &c, bool m) override { calls << QStringLiteral("mirrored %1 %2").arg(c.persistedCameraKey()).arg(m); }
    void setFixedInPlace(const DeviceKey &c, bool f) override { calls << QStringLiteral("fixed %1 %2").arg(c.persistedCameraKey()).arg(f); }
    void setHittingArea(const DeviceKey &c, const QRectF &b) override
    {
        calls << QStringLiteral("box %1 %2,%3,%4,%5").arg(c.persistedCameraKey()).arg(b.x(), 0, 'g', 6).arg(b.y(), 0, 'g', 6).arg(b.width(), 0, 'g', 6).arg(b.height(), 0, 'g', 6);
    }
    void clearHittingArea(const DeviceKey &c) override { calls << QStringLiteral("clear %1").arg(c.persistedCameraKey()); }
    void setAlias(const DeviceKey &d, const QString &a) override { calls << QStringLiteral("alias %1 [%2]").arg(d.persistedCameraKey(), a); }
    void setEnabledOnThisMachine(const DeviceKey &d, bool e) override { calls << QStringLiteral("enabled %1 %2").arg(d.persistedCameraKey()).arg(e); }
    QStringList calls;
};

class MemoryRepository final : public RigRepository
{
public:
    RigLoad load() override { return {}; }
    bool    save(const RigId &, const RigParts &) override { ++saves; return true; }
    int     saves = 0;
};

CameraSetup cam(const DeviceKey &k, int code, bool mirrored = false, bool fixed = false,
                std::optional<QRectF> box = std::nullopt)
{
    CameraSetup c(k);
    c.setPerspectiveCode(code);
    c.mirrored     = mirrored;
    c.fixedInPlace = fixed;
    c.hittingArea  = box;
    return c;
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("camera_rig_link_test — the temporary directory: %s\n", qPrintable(stores.path()));

    const QRectF box(0.31, 0.62, 0.12, 0.18);

    std::printf("\nL1 one camera's facts\n");
    {
        RigParts p;
        p.state.cameras = { cam(camA, kFaceOn, true, true, box) };
        p.other.aliases = { { camA, "Face on" } };
        p.other.enabledOnThisMachine = { { camB, false } };
        const CameraFacts a = factsOf(p, camA), b = factsOf(p, camB);
        check(a.perspectiveCode == kFaceOn && a.mirrored && a.fixedInPlace && a.hittingArea == box && a.alias == "Face on"
                  && a.enabledOnThisMachine,
              "L1a a camera's entry, alias and (default) enabled flag");
        check(b.perspectiveCode == kNone && !b.mirrored && !b.fixedInPlace && !b.hittingArea && b.alias.isEmpty()
                  && !b.enabledOnThisMachine,
              "L1b a camera with no entry has every default, and its enabled flag");
        RigParts q;
        q.other.aliases = { { DeviceKey::fromPersistedCameraKey(camA.persistedCameraKey()), "By its spelling" } };
        check(factsOf(q, camA).alias == "By its spelling", "L1c the alias is found by the camera key's spelling");
    }

    std::printf("\nL2 what a newly connected camera is given\n");
    {
        RigParts p;
        p.state.cameras = { cam(camA, kFaceOn, true, true, box), cam(camB, kImpact, false, false, box),
                            cam(camC, kNone, false, true, QRectF()) };
        const ConnectRestore a = restoreAtConnect(p, camA), b = restoreAtConnect(p, camB), c = restoreAtConnect(p, camC);
        check(a.perspectiveCode == kFaceOn && !a.poseOff && a.mirrored && a.fixedInPlace && a.box == box,
              "L2a a fixed face-on camera: its perspective, its mirror flag and its box");
        check(b.perspectiveCode == kImpact && b.poseOff && !b.box,
              "L2b the impact camera: pose off for good; a box kept for a camera not fixed is not applied");
        check(!c.perspectiveCode && !c.poseOff && c.fixedInPlace && !c.box,
              "L2c None is not set; an empty box is never applied, fixed or not");
        const ConnectRestore none = restoreAtConnect(p, DeviceKey::camera("Unknown", "", "usb-9"));
        check(!none.perspectiveCode && !none.mirrored && !none.box, "L2d a camera the rig has never held: nothing");
    }

    std::printf("\nL3 what a change of the rig state does to the live cameras\n");
    {
        const QList<ManagedCamera> cams = { { camA, true, true }, { camB, true, true }, { camC, false, true } };
        RigState before;
        before.cameras = { cam(camA, kImpact), cam(camB, kFaceOn), cam(camC, kDtl) };
        RigState after = before;
        after.cameras = { cam(camA, kNone), cam(camB, kImpact), cam(camC, kDtl) };
        const StateReaction r = reactionTo(before, after, cams);
        check(r.listChanged && r.perspectives.size() == 2 && r.perspectives[0].camera == camA
                  && r.perspectives[0].code == kNone && r.perspectives[1].camera == camB && r.perspectives[1].code == kImpact,
              "L3a Impact moved: the camera that lost it is set to None, the new one to Impact, on their live instances");
        check(r.reconnect == QList<DeviceKey>{ camA, camB },
              "L3b both selected live cameras whose Impact-ness changed are reconnected, in the manager's order");

        RigState onlyRole = before;
        onlyRole.cameras = { cam(camA, kImpact), cam(camB, kDtl), cam(camC, kImpact) };
        const StateReaction r2 = reactionTo(before, onlyRole, cams);
        check(r2.listChanged && r2.reconnect.isEmpty() && r2.perspectives.size() == 2,
              "L3c face-on to down-the-line reconnects nothing; a live camera that is not selected is set, never reconnected");

        const QList<ManagedCamera> notLive = { { camA, true, false }, { camB, true, false } };
        const StateReaction r3 = reactionTo(before, after, notLive);
        check(r3.listChanged && r3.perspectives.isEmpty() && r3.reconnect.isEmpty(),
              "L3d a camera with no live instance: nothing on the camera (it takes the role at its next connect)");

        RigState flags = before;
        flags.cameras = { cam(camA, kImpact, true, false), cam(camB, kFaceOn, false, true, box), cam(camC, kDtl) };
        const StateReaction r4 = reactionTo(before, flags, cams);
        check(!r4.listChanged && r4.perspectives.isEmpty() && r4.reconnect.isEmpty() && r4.mirrored.size() == 1
                  && r4.mirrored[0].camera == camA && r4.mirrored[0].on,
              "L3e mirrored is set on the live instance; the list does not change");
        check(r4.fixedInPlace.size() == 1 && r4.fixedInPlace[0].camera == camB && r4.fixedInPlace[0].on
                  && r4.boxes.size() == 1 && r4.boxes[0].camera == camB && r4.boxes[0].box == box,
              "L3f the box is set on the live instance; fixed in place is only held (L3g)");
        RigState fixedOnly = before;
        fixedOnly.cameras = { cam(camA, kImpact), cam(camB, kFaceOn, false, true), cam(camC, kDtl) };
        const StateReaction r5 = reactionTo(before, fixedOnly, cams);
        check(r5.boxes.isEmpty() && r5.mirrored.isEmpty() && r5.perspectives.isEmpty() && r5.fixedInPlace.size() == 1,
              "L3g fixed in place alone never applies or withholds a box");
        const StateReaction r6 = reactionTo(flags, before, cams);
        check(r6.boxes.size() == 1 && !r6.boxes[0].box, "L3h a cleared box is cleared from the live instance");
        const StateReaction r7 = reactionTo(before, before, cams);
        check(!r7.listChanged && r7.perspectives.isEmpty() && r7.mirrored.isEmpty() && r7.boxes.isEmpty()
                  && r7.fixedInPlace.isEmpty() && r7.reconnect.isEmpty(),
              "L3i no change: nothing");
    }

    std::printf("\nL4 the manager's writers as requests\n");
    {
        RecordingCommands rig;
        requestPerspective(rig, camA, kImpact);
        requestMirrored(rig, camA, true);
        const QRectF set = requestBox(rig, camA, QRectF(0.9, 0.5, -0.2, 0.7));
        const QRectF cleared = requestBox(rig, camA, QRectF(1.2, 0.2, 0.3, 0.3));
        requestClearBox(rig, camB);
        requestAlias(rig, camA, "  Face on  ");
        requestAlias(rig, camB, "   ");
        requestExcluded(rig, camC, true);
        requestExcluded(rig, camC, false);
        const QStringList want = {
            "perspective " + camA.persistedCameraKey() + " 4",
            "mirrored " + camA.persistedCameraKey() + " 1",
            "box " + camA.persistedCameraKey() + " 0.7,0.5,0.2,0.5",
            "clear " + camA.persistedCameraKey(),
            "clear " + camB.persistedCameraKey(),
            "alias " + camA.persistedCameraKey() + " [Face on]",
            "alias " + camB.persistedCameraKey() + " []",
            "enabled " + camC.persistedCameraKey() + " 0",
            "enabled " + camC.persistedCameraKey() + " 1",
        };
        for (int i = 0; i < want.size(); ++i)
            if (i >= rig.calls.size() || rig.calls[i] != want[i])
                std::printf("    want %s\n     got %s\n", qPrintable(want[i]), i < rig.calls.size() ? qPrintable(rig.calls[i]) : "(none)");
        check(rig.calls == want, "L4a one command each, in order");
        check(qFuzzyCompare(set.x(), 0.7) && qFuzzyCompare(set.y(), 0.5) && qFuzzyCompare(set.width(), 0.2)
                  && qFuzzyCompare(set.height(), 0.5),
              "L4b the box is normalised (a negative width) and clipped to the frame, and handed back for the live instance");
        check(cleared.isEmpty(), "L4c a box outside the frame has no area: a clear, and an empty box for the live instance");
    }

    std::printf("\nL5 the alias seeded at launch\n");
    {
        RecordingCommands rig;
        RigParts p;
        p.other.aliases = { { camA, "Face on" } };
        const DeviceKey piped = DeviceKey::camera("Cam|Model", "", "dev-1");
        const int asked = requestAliasSeeds(rig, p, { camA, camB, piped, DeviceKey() });
        check(asked == 2
                  && rig.calls == QStringList{ "alias " + camB.persistedCameraKey() + " [Chameleon3 CM3-U3-13Y3M 87654321]",
                                               "alias " + piped.persistedCameraKey() + " [Cam Model dev-1]" },
              "L5a a camera with no alias gets its key with every '|' a space; one with an alias, and a null key, none");
    }

    std::printf("\nL6 through the real Rig\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        requestPerspective(rig, camA, kImpact);
        requestPerspective(rig, camB, kFaceOn);
        const RigState before = rig.parts().state;
        int savesAtEvent = -1;
        StateReaction r;
        QObject::connect(&rig, &Rig::rigChanged, &rig, [&] {
            savesAtEvent = repo.saves;
            r = reactionTo(before, rig.parts().state, { { camA, true, true }, { camB, true, true } });
        });
        const int saves = repo.saves;
        requestPerspective(rig, camB, kImpact);
        check(factsOf(rig.parts(), camA).perspectiveCode == kNone && factsOf(rig.parts(), camB).perspectiveCode == kImpact,
              "L6a Impact is exclusive: the camera that held it is None (the Rig's invariant 1)");
        check(savesAtEvent == saves + 1 && r.reconnect == QList<DeviceKey>{ camA, camB } && r.listChanged,
              "L6b the reaction runs after the save, and reconnects both cameras whose Impact-ness changed");
        const QRectF live = requestBox(rig, camA, box);
        check(live == box && factsOf(rig.parts(), camA).hittingArea == box && !factsOf(rig.parts(), camA).fixedInPlace,
              "L6c a box on a camera not fixed is held by the Rig, and handed back for the live instance");
    }

    std::printf("\nL8 a camera's enabled flag changed\n");
    {
        const EnabledReaction off = enabledReaction({ true, false, true }, false, false);
        check(!off.nothing && off.excluded && off.sessionAdd && !off.sessionRemove && off.deselect && !off.select,
              "L8a disabled on this machine: its flag, the session's list gains it, the camera deselected");
        const EnabledReaction on = enabledReaction({ true, true, false }, true, true);
        check(!on.nothing && !on.excluded && on.sessionRemove && !on.sessionAdd && on.select && !on.deselect,
              "L8b enabled again: its flag, the session's list loses it, the camera selected");
        const EnabledReaction same = enabledReaction({ true, true, false }, true, false);
        check(same.nothing && !same.sessionAdd && !same.select && !same.deselect,
              "L8c a listed camera whose flag already says so: nothing");
        const EnabledReaction unlisted = enabledReaction({ false, false, false }, false, false);
        check(!unlisted.nothing && unlisted.sessionAdd && !unlisted.select && !unlisted.deselect,
              "L8d a camera the manager does not list: the session's list only");
        const EnabledReaction offUnselected = enabledReaction({ true, false, false }, true, false);
        check(!offUnselected.nothing && offUnselected.excluded && !offUnselected.sessionAdd && !offUnselected.deselect,
              "L8e disabled while not selected and already off for the session: its flag only");
    }

    std::printf("\nL7 app.rig\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        RigPresenter view(rig);
        int changes = 0;
        QObject::connect(&view, &RigPresenter::camerasChanged, &view, [&] { ++changes; });
        const QString a = camA.persistedCameraKey(), b = camB.persistedCameraKey();
        view.assignPerspective(a, kFaceOn);
        view.setMirrored(b, true);
        view.setFixedInPlace(a, true);
        view.setHittingArea(a, box);
        view.setAlias(a, "Face on");
        view.setEnabledOnThisMachine(b, false);
        check(changes == 6, "L7a one camerasChanged per change (four versioned, an alias, an enabled flag)");
        check(view.perspectiveOf(a) == kFaceOn && view.perspectiveOf(b) == kNone && view.hittingAreaOf(a) == box
                  && view.hittingAreaOf(b).isEmpty() && view.aliasOf(a) == "Face on" && view.aliasOf(b).isEmpty(),
              "L7b each camera's perspective, box and alias, read by its camera key");
        check(view.camerasMirrored() == QStringList{ b } && view.camerasFixedInPlace() == QStringList{ a }
                  && view.camerasDisabled() == QStringList{ b },
              "L7c the mirrored, fixed and disabled cameras, as lists of camera keys");
        check(factsOf(rig.parts(), camA).fixedInPlace && !factsOf(rig.parts(), camB).enabledOnThisMachine,
              "L7d each command reached the Rig");
        view.clearHittingArea(a);
        view.setFixedInPlace(a, false);
        view.setMirrored(b, false);
        view.setEnabledOnThisMachine(b, true);
        check(view.hittingAreaOf(a).isEmpty() && view.camerasFixedInPlace().isEmpty() && view.camerasMirrored().isEmpty()
                  && view.camerasDisabled().isEmpty() && changes == 10,
              "L7e each undone, one change each");
        const int before = changes;
        view.setMirrored(QStringLiteral("FO1"), true);
        view.assignPerspective(QString(), kImpact);
        view.setMirrored(a, false);   // already so: the Rig changes nothing
        check(changes == before && view.camerasMirrored().isEmpty(),
              "L7f a key that is not a camera key, or a change that changes nothing: no change, no signal");
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
