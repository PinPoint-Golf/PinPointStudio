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

// A SWING FOLDER, RE-ANALYSED BY ITS PATH — for the tests and tools that hold a folder and no Shot (refactor step
// 3.4b-ii; P11). The re-analysis itself is src/Analysis/swing_reanalyzer.h, which reads a shot only through its
// RecordingSource; reading a folder by its path is file access, so it lives here, in the Export ring, beside the
// Recording loader it calls (recording_loader.h), and hands the loaded swing to the analysis.
//
//   loadSwingFolder(dir)      the folder → the Recording (loadRecording) → the job (SwingDiskLoader::fromRecording)
//   reanalyzeSwingDir(dir)    the same, then the re-analysis (reanalyzeLoaded): the rules and the result of
//                             reanalyzeShot, for a folder (swing_window_parity_test, raw_reencode, the parity
//                             digest's doc-blocks reference)
//
// The app never calls these: its re-analysis reads each shot through the Shot's RecordingSource.
//
// Adapters ring (src/Stores).

#include <QString>

#include "../Analysis/swing_reanalyzer.h"   // LoadedSwing, SwingLoadOptions, ReanalyzeOptions, ReanalyzeResult

namespace pinpoint::analysis {

LoadedSwing     loadSwingFolder(const QString &swingDir, const SwingLoadOptions &opts = {});
ReanalyzeResult reanalyzeSwingDir(const QString &swingDir, const ReanalyzeOptions &opts = {});

} // namespace pinpoint::analysis
