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
// A member this build does not know, at the top level or inside `state` or `other`, is kept and written back as read,
// so a later build's addition survives an earlier build's save.
//
// THE COPY FROM SETTINGS, one group of keys at a time (rig_design.md §4.4, "From settings to the file" and "Going back
// to an older build"; §9 question 6; rig_design_addendum.md decisions 9 and 10). A group is copied in the item that
// makes the Rig its writer, so the composition root names the groups the Rig writes; until C-5b it names none, and
// AppSettings stays every group's one writer. For each group named, at load():
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
// READ-ONLY. A file of a newer pinpoint.rig/N is read for the fields this build knows and never saved over; so is a
// file this build cannot read at all (not JSON, no schema, no valid rigId), of which nothing is read. Each says so
// with ppWarn, once at the load and again at every refused save. Nothing is ever deleted.
//
// Adapters ring (src/Stores). GUI thread only, like its owner.

#include "../DomainModel/rig_repository.h"

#include <QJsonObject>
#include <QList>
#include <QString>

class RigStore final : public pinpoint::RigRepository
{
public:
    // A group of settings keys the copy takes (rig_design.md §4.4).
    enum class Group { Cameras };

    // `groupsTheRigWrites`: the groups whose writer the Rig is, which load() copies from settings. None until C-5b.
    explicit RigStore(QString folder, QList<Group> groupsTheRigWrites = {});

    pinpoint::RigLoad load() override;
    bool              save(const pinpoint::RigId &id, const pinpoint::rig::RigParts &parts) override;

    QString folder() const { return m_folder; }
    QString filePath() const;   // <folder>/rig.json
    bool    isReadOnly() const { return m_readOnly; }

    // The rig folder (FILE-APP-07), cleaned: PINPOINT_RIG_DIR when set, else <AppData>/rig.
    static QString defaultFolder();

    // The format line this build writes, and the one it reads and writes.
    static constexpr int kSchemaVersion = 1;

    // The group's name in rig.json's groupsTaken.
    static QString groupName(Group g);
    // The fingerprint the settings store's keys of `g` decode to now: SHA-256 hex over the decoded values and the
    // entries a copy would keep verbatim. Reads the settings store; writes nothing.
    static QString settingsFingerprint(Group g);

private:
    void copyGroups(pinpoint::RigLoad &out);   // the groups the Rig writes, from settings, into `out`

    QString     m_folder;
    QList<Group> m_groups;
    bool        m_readOnly = false;
    QJsonObject m_loaded;        // the file as read: members this build does not know are written back from it
    QJsonObject m_groupsTaken;   // the store's own: which groups of settings keys were taken, with each fingerprint
    QJsonObject m_kept;          // the store's own: the settings entries a copy could not type, verbatim
};
