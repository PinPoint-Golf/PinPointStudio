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

#include "swing_folder_reanalysis.h"
#include "recording_loader.h"   // the swing folder → the Recording (step 1.2)

namespace pinpoint::analysis {

LoadedSwing loadSwingFolder(const QString &swingDir, const SwingLoadOptions &opts)
{
    return SwingDiskLoader::fromRecording(loadRecording(swingDir, SwingDiskLoader::loadOptions(opts)));
}

ReanalyzeResult reanalyzeSwingDir(const QString &swingDir, const ReanalyzeOptions &opts)
{
    // The overrides reach the loader too: the MP4 readers it builds take the decode.* keys (everything else in
    // the map is applied by the stages).
    SwingLoadOptions lopts;
    lopts.tuningOverrides = opts.tuningOverrides;
    return reanalyzeLoaded(loadSwingFolder(swingDir, lopts), opts, swingDir);
}

} // namespace pinpoint::analysis
