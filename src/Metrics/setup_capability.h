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

#include "metric_catalogue.h"   // MetricCatalogue, ShotContext, CaptureDevice

#include <QString>
#include <QStringList>

#include <vector>

// SetupCapability — what a session WILL record, answered before the first swing
// (session_wizard_refactor_design.md §4.14). The closing page of session setup tells the golfer
// what is mounted and what that does to the recording. The catalogue already knows this per metric
// through its route ladders; the only missing piece was a ShotContext built from the PLANNED setup
// rather than from a recorded swing (ShotReplayController::shotContext). This file is that piece
// and a per-group roll-up over the existing resolver — it re-implements no availability rule.
//
// Pure: Qt Core only, no QML, no settings, no devices. The caller (the setup flow, through the
// MetricCatalog façade) decides which devices count as present.

namespace pinpoint::analysis {

// What the session will have, as known BEFORE a swing.
struct SetupFacts {
    bool faceOnCamera = false;
    bool dtlCamera    = false;
    // Roles assigned to a device that is enabled for this session AND calibrated. A role that is
    // only assigned produces nothing, so it must not be counted here — the caller filters.
    std::vector<SegmentRole> imuRoles;
    // A HackMotion wG3 is in the session. It supplies LeadForearm + LeadHand on its own
    // (contextForSetup adds them), and opens the `hm.*` rungs that a Witmotion pair does not.
    bool hackMotion    = false;
    bool launchMonitor = false;
    // Club and ball tracks are camera PRODUCTS, not known until a swing has been analysed. They are
    // expected exactly when the face-on camera is present — see contextForSetup.
};

// The ShotContext the resolver would see on a swing recorded with this setup, assuming every
// device delivers. Every assumption is stated at the definition.
ShotContext contextForSetup(const SetupFacts &facts);

// One metric group (MetricDescriptor::group), in manifest order.
//
// Every descriptor in the group lands in exactly one of five lists. Only the first three are the
// golfer's answer; the last two are excluded from it and kept so a caller (and the test) can see
// what was left out and why:
//   measured / estimated / unavailable — LIVE metrics, by their resolved state on this setup;
//   planned  — every route is planned: nothing the golfer could mount would produce it today;
//   derived  — every live route needs no capture of its own (tempo off the phase ladder): its
//              availability does not depend on the kit, so it says nothing about the setup.
struct CapabilityGroup {
    QString group;
    int measured = 0, estimated = 0, unavailable = 0;
    QStringList measuredKeys, estimatedKeys, unavailableKeys;
    int planned = 0, derived = 0;
    QStringList plannedKeys, derivedKeys;

    // The most useful single reason, GENERATED from the catalogue, never authored. Each is the
    // commonest value among the group's metrics in that bucket; a tie goes to the first in manifest
    // order. Empty when the bucket is empty.
    QString estimatedBecause;     // the winning route's `summary` (the method that produced it)
    QString unavailableBecause;   // the resolver's "needs …" reason
    QString upgrade;              // MetricAvailability::upgrade, over estimated + unavailable
    // Distinct kit missing for this group's estimated and unavailable metrics — every LIVE rung above
    // the one that fired (or every live rung, when none did), reduced to the devices this setup
    // lacks. CaptureDevice order; ClubTrack and BallTrack never appear (they are camera products, so
    // their gap is reported as the face-on camera's). Labels are captureDeviceLabel(), ids
    // captureDeviceId(), index for index.
    QStringList deviceGaps;
    QStringList deviceGapIds;
};

std::vector<CapabilityGroup> summariseSetup(const MetricCatalogue &catalogue,
                                            const SetupFacts &facts);

} // namespace pinpoint::analysis
