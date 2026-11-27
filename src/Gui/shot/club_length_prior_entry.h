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

// THE LEARNED CLUB-LENGTH PRIOR'S ENTRY, as the capture side reads it at the freeze and writes it after a live shot's
// analysis (SET-05; club_length_fusion.h LengthPriorState). The entry is the value the Rig keeps for one
// ClubLengthPriorKey (rig::RigOtherFacts::clubLengthPriors, saved to rig.json since C-7), with the members
// analysis/clubLenPrior held before: emaPx, varPx, n, disagreeRun, lengthMm, frameW, frameH, lastUpdatedUtc.
//
// Header-only, so the read the analysis input comes from is the same code in capture_context.cpp and in its test
// (club_length_prior_test). Moved here unchanged from capture_context.cpp and shot_processor.cpp (platform refactor
// C-7): the expressions are today's, so the prior a shot joins, and so capture.club.lengthPrior, are unchanged.

#include "../../Club/club_length_fusion.h"   // LengthPriorState

#include <QDateTime>
#include <QString>
#include <QVariantMap>

#include <cmath>
#include <optional>

namespace pinpoint::capture {

// The prior state a shot starts from, read from its entry; nullopt when there is no entry, or when the entry's frame
// size is not the live face-on camera's (the camera-move self-heal: an entry with recorded dims must match the live
// camera's frame size; one that predates the field is trusted as it is). The state joins the shot's analysis as its
// length prior only when n > 0 and emaPx > 0 (joinLengthPrior, below).
inline std::optional<pinpoint::analysis::LengthPriorState> lengthPriorFromEntry(const QVariantMap &prior, int liveFrameW,
                                                                                int liveFrameH)
{
    if (prior.isEmpty()) return std::nullopt;
    const int entryW = prior.value(QStringLiteral("frameW"), 0).toInt();
    const int entryH = prior.value(QStringLiteral("frameH"), 0).toInt();
    const bool dimsOk = (entryW <= 0 || entryH <= 0) || (entryW == liveFrameW && entryH == liveFrameH);
    if (!dimsOk) return std::nullopt;
    pinpoint::analysis::LengthPriorState st;
    st.emaPx       = prior.value(QStringLiteral("emaPx"), -1.0).toDouble();
    st.varPx       = prior.value(QStringLiteral("varPx"), 0.0).toDouble();
    st.n           = prior.value(QStringLiteral("n"), 0).toInt();
    st.disagreeRun = prior.value(QStringLiteral("disagreeRun"), 0).toInt();
    return st;
}

// The prior a shot's analysis joins, from the state it starts from, into the job its recorded analysis context is
// built from (capture_context.cpp's ctxJob, a SwingExportJob; captureAnalysisContext then records it as
// capture.club.lengthPrior): only a prior that has learned a length (n > 0, emaPx > 0); otherwise the job keeps "no
// prior". A template over the job, so this header names no store type.
template <typename Job>
inline void joinLengthPrior(const pinpoint::analysis::LengthPriorState &st, Job &job)
{
    if (st.n > 0 && st.emaPx > 0.0) {
        job.priorClubLenPx    = st.emaPx;
        job.priorClubLenVarPx = st.varPx;
        job.priorClubLenN     = st.n;
    }
}

// What a shot learned, after its live analysis folded its prior-free length into `st` (updateLengthPrior): the members
// Rig::learnClubLength writes over the entry (every other member of the entry stays).
inline QVariantMap learnedLengthPrior(const pinpoint::analysis::LengthPriorState &st, double clubLengthM, int frameW,
                                      int frameH, const QDateTime &nowUtc)
{
    QVariantMap entry;
    entry[QStringLiteral("emaPx")]          = st.emaPx;
    entry[QStringLiteral("varPx")]          = st.varPx;
    entry[QStringLiteral("n")]              = st.n;
    entry[QStringLiteral("disagreeRun")]    = st.disagreeRun;
    entry[QStringLiteral("lengthMm")]       = static_cast<int>(std::lround(clubLengthM * 1000.0));
    entry[QStringLiteral("frameW")]         = frameW;
    entry[QStringLiteral("frameH")]         = frameH;
    entry[QStringLiteral("lastUpdatedUtc")] = nowUtc.toString(Qt::ISODate);
    return entry;
}

} // namespace pinpoint::capture
