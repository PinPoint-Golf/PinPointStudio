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

// THE STUDIO PROFILE'S REPOSITORY (information_model.md OBJ-STUDIO-PROFILE, SET-20, FILE-APP-06; P11: an object's
// persistence is its own concern). StudioProfile (studio_profile.h) is the one writer of its keys and its folder, and
// touches neither itself: it reaches them through this interface, which the composition root (main.cpp) gives it over
// the one store that implements it (src/Stores/studio_profile_store.h). Application ring, beside its owner, so the
// owner includes no store (rings point inward: information_model.md §4).
//
// What it holds:
//   - the keys, SET-20: `Studio/<key>` in the app's settings file, one string each, as the owner wrote it. The owner
//     keeps their meaning (an absent key is the shipped default; `practiceSteps` is JSON text; `logo` and `coachPhoto`
//     are the paths of the copies). Every write is at once (synced), one key at a time, so a key the owner did not
//     change is never rewritten.
//   - the folder, FILE-APP-06: the copied logo and coach's photo, `<baseName>.<ext>` in `<AppData>/studio/`
//     (`PINPOINT_STUDIO_DIR` overrides it, for a probe or a test).

#include <QByteArray>
#include <QString>

#include <optional>

// One SET-20 key. The store maps each to its key's name.
enum class StudioKey {
    Name, Logo, Accent, Address, Phone, Email, Website, Booking, PracticeSteps,
    EmailSubject, EmailBody, CoachName, CoachTitle, CoachEmail, CoachPhone, CoachPhoto,
};

class StudioProfileRepository
{
public:
    virtual ~StudioProfileRepository() = default;

    // ── the keys (SET-20) ──────────────────────────────────────────────────────────────────────
    // The key as stored; empty when it is absent.
    virtual QString value(StudioKey key) const = 0;
    // Empty removes the key; anything else sets it. Synced before it returns.
    virtual void store(StudioKey key, const QString &value) = 0;

    // ── the folder (FILE-APP-06) ───────────────────────────────────────────────────────────────
    // A copy is still on disk (the app data folder cleared, a restore from another machine: it may not be).
    virtual bool copyExists(const QString &path) const = 0;
    // A copy's bytes, for the profile's stamp; empty when it cannot be read.
    virtual QByteArray copyBytes(const QString &path) const = 0;
    // The file the coach picked, read whole; nothing when it is not a readable file. Read before any copy is
    // replaced, so re-importing the copy itself never deletes its own source.
    virtual std::optional<QByteArray> readPicked(const QString &localPath) const = 0;
    // `bytes` written whole (atomically) as `<baseName>.<ext>` in the folder, made if missing; an earlier copy under
    // another of the three extensions (png, jpg, svg) removed, so the folder holds one. The copy's path, or empty when
    // it could not be written (logged; nothing changed).
    virtual QString writeCopy(const QString &baseName, const QString &ext, const QByteArray &bytes) = 0;
    // A copy removed; nothing when it is already gone.
    virtual void removeCopy(const QString &path) = 0;
};
