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

// THE REUSE GATE — which recorded stage products a re-analysis may reload instead of running the stage
// again (analysis_versions.h; information_model.md R-ANA-5, R-ANA-7; refactor step 1.6). Pure: it reads
// the recorded `analysis.versions` and the facts of this run, and decides. The loading itself (the
// typed readers over the recorded block) is the caller's (reanalyzeSwingDir, swing_reanalyzer.cpp).
//
// THE RULES
//   pose     reused when its stage version (kPoseStageVersion) and scan scope match (a "full" record
//            covers a span request, not the reverse), a face-on camera is there to put it on, and no
//            pose./ball./address-scan tuning override is in play. MARK'S POSE RULE (14 Nov 2026,
//            DEC-64): the recorded MODEL identity is NOT compared — a saved pose is reused whatever
//            pose model file this machine has; a new model ships with a kPoseStageVersion bump. A
//            missing stamp or an empty track re-runs (nothing to reuse).
//   poseDtl  the same rule against versions.poseDtl (kDtlPoseStageVersion) and a DTL camera.
//   ball     on top of a reloaded pose, when kBallStageVersion matches.
//   shaft    the shaft track and the ladder, on top of a reloaded ball, when kShaftStageVersion
//            matches, no tuning override at all is in play, THE LEAD SIDE MATCHES (the tracker reads
//            the lead elbow from the handedness, shaft_tracker.cpp, and the pose's lead/trail hands:
//            a stamp whose handedness leads with the other hand re-runs it, "handedness changed"),
//            and THE CLUB INPUTS MATCH: the recorded
//            input stamp's club, its length and source equal this run's (AnalysisInputs::sameClub).
//            A recorded analysis with no input stamp (before step 1.6) was computed with the captured
//            club — no analysis applied the golfer's correction before 1.6 — so it matches exactly
//            when this run's club is not a correction (step 1 brief §5.3: only corrected swings
//            change). A stamp of another kAnalysisInputsVersion is not trusted: no match.
//   forced   SwingLab's --force-rerun: nothing is reused, every reason "forced".
// Pose and ball never depend on the club, the height or the session type. The pose's keypoints do
// not depend on the handedness either; only its lead/trail hand labels do (pose_runner.cpp: lead =
// the left hand unless the golfer is left-handed). So a reused pose whose stamp led with the other
// hand is RELABELLED, never re-inferred (Mark's pose rule): poseLeadSwapped() says so, and the
// caller swaps each frame's lead and trail hands — exactly what a fresh run would label them.
//
// STALENESS (R-ANA-5): analysisInputsStaleness() says why a recorded analysis is stale against the
// effective context now — no stamp, a stamp of another version, or which input differs. Staleness is
// a fact for the reader (the library sweep; the golfer's re-analyse); it moves no k*StageVersion (D6).
//
// Rules ring: pure, no I/O.

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariantMap>

#include "analysis_versions.h"

namespace pinpoint::analysis {

struct ReuseGateFacts {
    QJsonObject    versions;                 // the recorded analysis.versions ({} = none)
    bool           hasBallBlock    = false;  // the recorded analysis carries `ball`
    bool           hasClubBlock    = false;  // … `club` (the shaft track)
    bool           hasPoseDtlBlock = false;  // … `poseDtl`
    bool           forceRerun      = false;  // SwingLab --force-rerun
    QVariantMap    tuningOverrides;          // SwingLab --params; empty in production
    bool           fullWindow      = false;  // the run wants the full window (a span pose then re-runs)
    bool           hasFaceOnCamera = false;
    bool           hasDtlCamera    = false;
    AnalysisInputs inputsNow;                // the stamp this run will write (job.inputs)
    bool           clubCorrectedNow = false; // this run's club is the golfer's correction (EffectiveClub)
};

// What the caller actually reloaded (a reloaded track can still come back empty).
struct ReuseReloaded {
    bool pose = false, poseDtl = false, ball = false, shaft = false, ladder = false;
};

class ReuseGate {
public:
    explicit ReuseGate(ReuseGateFacts facts);

    bool    mayReloadPose() const;
    QString recordedPoseScope() const;     // versions.pose.scope
    QString recordedPoseModel() const;     // versions.pose.model (provenance, carried onto the reuse)
    bool    mayReloadPoseDtl() const;
    QString recordedPoseDtlModel() const;
    bool    mayReloadBall(bool poseReloaded) const;
    bool    clubInputsMatch() const;
    bool    leadSideMatches() const;     // the recorded stamp led with the same hand (no stamp: yes)
    bool    poseLeadSwapped() const;     // a reloaded pose's (and DTL pose's) lead/trail labels must swap
    bool    mayReloadShaft(bool ballReloaded) const;   // the shaft track and, with it, the ladder

    // analysis.path.reuse: "reused", or the first reason each product was not.
    QMap<QString, QString> reasons(const ReuseReloaded &reloaded) const;

private:
    ReuseGateFacts f_;
    QJsonObject    vp_, vpd_, vb_, vs_;
    bool           poseOverride_ = false;
    QString        wantScope_;
};

// "" when `versions.inputs` matches `now`; else why the analysis is stale ("no input stamp",
// "input stamp version N, want M", or the inputs that differ, e.g. "club DRIVER → 7 IRON, handedness").
QString analysisInputsStaleness(const QJsonObject &versions, const AnalysisInputs &now);

} // namespace pinpoint::analysis
