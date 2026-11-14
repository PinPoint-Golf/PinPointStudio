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

// Link stubs for the ReanalysisController seam test. The controller calls into the full offline
// analysis pipeline (reanalyzeSwingDir); pulling that in would drag the whole analyzer + OpenCV +
// pose stack into what is deliberately a thin funnel test. The write-back goes through the real
// reanalyse_and_persist.cpp and a RecordingRepository the test never wires (step 1.4), so nothing
// of the swing-file writer is linked or stubbed. The seam test only exercises queue/de-dupe/signal
// behaviour and never reaches the worker body (no event loop runs, so QFutureWatcher::finished
// never fires), so a trivial definition satisfies the linker without faking observable behaviour.

#include "../Analysis/swing_reanalyzer.h"

namespace pinpoint::analysis {

ReanalyzeResult reanalyzeSwingDir(const QString & /*swingDir*/, const ReanalyzeOptions & /*opts*/)
{
    return {};   // ok == false; never observed by the seam test
}

} // namespace pinpoint::analysis
