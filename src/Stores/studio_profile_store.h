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

// StudioProfileStore — THE STUDIO PROFILE'S STORE (information_model.md OBJ-STUDIO-PROFILE, SET-20, FILE-APP-06):
// the one implementation of StudioProfileRepository (src/App/studio_profile_repository.h), and the only code that
// reads or writes the `Studio/` keys of the app's settings file (ppSettings()) or the studio folder. Made once by the
// composition root (main.cpp), which hands it to the one StudioProfile; a test makes its own over a scratch settings
// file and a scratch folder.
//
// The folder is <AppData>/studio/ (QStandardPaths::AppDataLocation); PINPOINT_STUDIO_DIR overrides it. Both are read
// on every call, never cached, so a test or a probe that sets the variable after the store is made is still obeyed.
//
// Adapters ring (src/Stores). GUI thread only, like its owner.

#include "../App/studio_profile_repository.h"

class StudioProfileStore final : public StudioProfileRepository
{
public:
    QString value(StudioKey key) const override;
    void store(StudioKey key, const QString &value) override;

    bool copyExists(const QString &path) const override;
    QByteArray copyBytes(const QString &path) const override;
    std::optional<QByteArray> readPicked(const QString &localPath) const override;
    QString writeCopy(const QString &baseName, const QString &ext, const QByteArray &bytes) override;
    void removeCopy(const QString &path) override;

    // The studio folder (FILE-APP-06), cleaned: PINPOINT_STUDIO_DIR when set, else <AppData>/studio.
    static QString studioDir();
};
