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

// RigStore: THE RIG'S STORE (information_model.md FILE-APP-07; docs/design/rig_design.md §4.4): the one implementation
// of RigRepository (src/DomainModel/rig_repository.h) and the only code that reads or writes rig.json (lint A4 owns
// the name here). Made once by the composition root (main.cpp), which hands it the folder and gives it to the one
// Rig; a test makes its own over a temporary folder.
//
// THE FOLDER is <AppData>/rig/ (QStandardPaths::AppDataLocation); PINPOINT_RIG_DIR overrides it, for a test or a
// probe, as PINPOINT_STUDIO_DIR does the studio profile's (src/Core/tests/isolated_stores.h sets it for every suite;
// tools/probe/run_headless.sh for every probe). defaultFolder() reads it; the composition root passes the result.
//
// THE FILE, rig.json, schema "pinpoint.rig/1", written whole and atomically, the schema checked on read:
//   schema, rigId;
//   state:  sinceUtc; cameras[] (key, perspective as its integer code, mirrored, fixedInPlace, hittingArea, the
//           calibration records); sensors[] (placement key and role; never an alignment: a worn sensor's alignment is
//           held for the run only, rig_design.md §4.2);
//   other:  aliases[], enabledOnThisMachine[], clubLengthPriors[] (each by its device's own spelling: a camera's
//           camera key, a sensor's key with its description);
//   groupsTaken, keptFromSettings: the store's own (the copy from settings, C-5a.2), written back on every save.
// A member this build does not know, at any level, the fields of each entry it reads included, is kept and written
// back as read, so a later build's addition survives an earlier build's save (`[^rig-unknown]`): each entry is written
// over the entry as read with the same identity (a camera by its key, a record by its subject, kind and method, a
// sensor by its placement, an alias or enabled flag by its device, a prior by its key). And any ENTRY this build cannot
// read, wherever it sits (a camera entry with no camera key, a calibration record it cannot read, a sensor entry whose
// role it does not know or with no placement key, an alias, enabled flag or prior whose device it cannot read), is kept
// exactly as read, outside the rig state and its digest, and written back at every save (the Solution Architect,
// 27 Nov 2026, P2 widened the same day): a record in its camera's entry (an entry of its own when the camera holds
// nothing else this build reads), the others at the end of their list.
//
// THE COPY FROM SETTINGS, one group of keys at a time (rig_design.md §4.4, "From settings to the file" and "Going back
// to an older build"; §9 question 6; rig_design_addendum.md decisions 9 and 10). A group is copied in the item that
// makes the Rig its writer, so the composition root names the groups the Rig writes: the camera group since C-5b; the
// sensor keys and the prior stay AppSettings' until C-6b.1 and C-7. For each group named, at load():
//   - none of the group's keys exists in settings (the settings reset or deleted, or a later stage removing them): the
//     Rig is left as it is, no copy and no wipe (P1, the Solution Architect, 27 Nov 2026);
//   - rig.json missing, or the group not taken yet: the group is read from the settings store (ppSettings(), never an
//     AppSettings, which caches its own maps) and handed to the Rig with the rest, as one load. rig.json records the
//     group as taken, with a FINGERPRINT of the group's decoded values and of the entries it kept verbatim.
//   - taken, and the keys decode to the same fingerprint: nothing happens.
//   - taken, and the fingerprint differs (an older build changed a key after the downgrade): the whole group is copied
//     again, and one ppWarn line says so. The last writer wins for the group: the stated cost.
//   - a read-only file: no copy.
// THE SETTINGS KEYS ARE NEVER WRITTEN OR DELETED by the copy (Stage E removes them). Each map key is parsed into a
// DeviceKey by its own spelling; an entry whose key fits no spelling (a bare "FO1"), or whose value has the wrong
// shape, is NOT a DeviceKey's fact: it is kept verbatim in rig.json (keptFromSettings), matched to no device, never
// dropped, and written back on every save the Rig makes (decision 10). A copy at launch is part of the launch's load:
// the Rig emits nothing for it.
//
// The camera group: camera/perspective, camera/isMirrored, camera/fixedInPlace, camera/ballRoi and camera/alias (maps
// by the camera key) and camera/excluded (a list of camera keys). An absent entry and its default (false, None) read
// the same. The sensors and the club-length prior join in C-6b.1 and C-7.
//
// READ-ONLY. A file of a newer pinpoint.rig/N is read for the fields this build knows and never saved over, nor moved,
// whatever it holds. It says so with ppWarn, once at the load and again at every refused save.
// UNREADABLE (P3, the Solution Architect, 27 Nov 2026). A file this build cannot read at all (it cannot be opened, is
// not JSON, has no pinpoint.rig schema line or no valid rigId) is renamed rig.json.unreadable-<UTC time> (a "-n" when
// that name is taken), kept, and the load goes on as on a machine with nothing stored: a new RigId, the groups copied,
// saved as normal. When the rename fails, the file is read-only as above. Nothing is ever deleted.
//
// Adapters ring (src/Stores). GUI thread only, like its owner.

#include "../DomainModel/rig_repository.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

class RigStore final : public pinpoint::RigRepository
{
public:
    // A group of settings keys the copy takes (rig_design.md §4.4).
    enum class Group { Cameras };

    // `groupsTheRigWrites`: the groups whose writer the Rig is, which load() copies from settings (the composition
    // root's: the cameras, since C-5b; a test's store may name none).
    explicit RigStore(QString folder, QList<Group> groupsTheRigWrites = {});

    pinpoint::RigLoad load() override;
    bool              save(const pinpoint::RigId &id, const pinpoint::rig::RigParts &parts) override;

    QString folder() const { return m_folder; }
    QString filePath() const;   // <folder>/rig.json
    bool    isReadOnly() const { return m_readOnly; }

    // The rig folder (FILE-APP-07), cleaned: PINPOINT_RIG_DIR when set, else <AppData>/rig.
    static QString defaultFolder();

    // A scratch copy of the rig folder for the camera role probe (PINPOINT_PROBE_CAMERA_ROLES; F29): a new folder under
    // the system's temporary folder, holding a copy of `folder`'s rig.json when there is one. The probe's role changes
    // are saved there, never to the machine's rig.json. Returns the new folder, or an empty string when none can be
    // made (the composition root then does not start the probe).
    static QString scratchCopyForProbe(const QString &folder);

    // The format line this build writes, and the one it reads and writes.
    static constexpr int kSchemaVersion = 1;

    // The group's name in rig.json's groupsTaken.
    static QString groupName(Group g);
    // The fingerprint the settings store's keys of `g` decode to now: SHA-256 hex over the decoded values and the
    // entries a copy would keep verbatim. Reads the settings store; writes nothing.
    static QString settingsFingerprint(Group g);

    // Whether the settings store holds at least one key of `g` (P1). Reads the settings store; writes nothing.
    static bool settingsHoldAnyKeyOf(Group g);

private:
    void    copyGroups(pinpoint::RigLoad &out);   // the groups the Rig writes, from settings, into `out`
    QString asidePath() const;                    // rig.json.unreadable-<UTC time>[-n], a name not yet taken (P3)

    QString     m_folder;
    QList<Group> m_groups;
    bool        m_readOnly = false;
    QJsonObject m_loaded;        // the file as read: members this build does not know are written back from it
    QJsonObject m_groupsTaken;   // the store's own: which groups of settings keys were taken, with each fingerprint
    QJsonObject m_kept;          // the store's own: the settings entries a copy could not type, verbatim
    // What the file held that this build cannot read (P2), written back at every save exactly as read, outside the rig
    // state and its digest: camera entries with no key, calibration records by their camera's key, sensor entries.
    QJsonArray                m_unreadCameras;
    QMap<QString, QJsonArray> m_unreadRecords;
    QJsonArray                m_unreadSensors;
    QMap<QString, QJsonArray> m_unreadOther;   // by the other facts' list: entries whose device this build cannot read
};
