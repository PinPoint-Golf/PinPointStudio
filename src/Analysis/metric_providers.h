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

#include "metric_provider.h"

// Capability declarations for the three live MetricSeries producers (design §6). Each maps its
// producer's keys to per-shot availability, encoding the session-profile gating the analyzer applies
// (wrist/foot metrics run only in the Wrist Motion profile; kinematics runs in every profile). None
// of these compute anything — production stays in the analysis stages. sessionType == -1 (directory
// browse with no shot loaded) is treated as session-agnostic so "available with my setup" is useful.

namespace pinpoint::analysis {

// metric_extractor.cpp — Wrist Motion session only. leadWristFlexExt / leadWristRadUln need
// LeadForearm + LeadHand; forearmPronation / leadArmFlexion additionally need LeadUpperArm.
class WristMetricProvider : public IMetricProvider {
public:
    std::vector<QString> provides() const override;
    MetricAvailability   availability(const QString &key, const ShotContext &ctx) const override;
};

// kinematic_series.cpp — runs in both the wrist and camera-kinematics profiles (any session).
// clubheadSpeed / handSpeed need the club track; lagAngle additionally needs face-on pose.
class KinematicSeriesProvider : public IMetricProvider {
public:
    std::vector<QString> provides() const override;
    MetricAvailability   availability(const QString &key, const ShotContext &ctx) const override;
};

// foot_metrics.cpp — Wrist Motion session only today. All need face-on whole-body pose (foot keypoints).
class FootMetricProvider : public IMetricProvider {
public:
    std::vector<QString> provides() const override;
    MetricAvailability   availability(const QString &key, const ShotContext &ctx) const override;
};

// Summary scores (produced as ScoreBreakdown, not MetricSeries — hence keyed synthetically):
//   wristScore / wristResemblance — LIVE for the Wrist session (WristResemblanceScorer +
//     WristAssessmentEngine, wrist_analyzer.cpp); need the lead forearm + hand IMUs.
//   swingScore — the Swing/GRF/Coach adherence score, ASPIRATIONAL: SwingScorer is dead code and
//     CameraKinematicsAnalyzer returns a stub, so this always resolves Unavailable ("no live scorer").
class ScoreProvider : public IMetricProvider {
public:
    std::vector<QString> provides() const override;
    MetricAvailability   availability(const QString &key, const ShotContext &ctx) const override;
};

} // namespace pinpoint::analysis
