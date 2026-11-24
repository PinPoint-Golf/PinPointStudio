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

// THE POSE MODEL FILE'S FINGERPRINT, READ AT THE ADAPTER (P11; refactor step 7.2): "<file>@<bytes>", 0
// bytes when the file is missing, the string analysis.versions.pose.model has always held. The pose stage
// names the file and asks for this through the reader its composition root installs once at start-up:
//
//   pinpoint::pose::installModelFileFingerprint(&pinpoint::readModelFileFingerprint);
//
// (src/Gui/main.cpp, swinglab_run, raw_reencode; pose_model_selection.h says what an uninstalled reader
// stamps). Header-only, so installing it changes no link line. The model files are bundled resources,
// not domain files (information_model.md §8.3, EXT-14).

#include <QFileInfo>
#include <QString>

namespace pinpoint {

inline QString readModelFileFingerprint(const QString &path)
{
    const QFileInfo fi(path);
    return fi.fileName() + QLatin1Char('@') + QString::number(fi.exists() ? fi.size() : 0);
}

} // namespace pinpoint
