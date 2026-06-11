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

// Segmentation v2/v3 signal preparation (design addendum A.4) — every derived
// signal the phase detectors consume, computed once from the fused streams.
// Pure functions over plain vectors: no window access, no Qt event machinery,
// standalone-tested (src/Analysis/tests/phase_signals_test.cpp).
//
// Conventions (docs/design/IMU_FRAME_CONTRACT.md):
//  - World is right-handed, gravity-aligned, +Z up; yaw arbitrary per session.
//  - SegmentStream.gyroDps/accelG are in the ANATOMICAL segment frame (the
//    frame qAnat maps to world); segment long axis is +Y (distal).
//  - Angular rates stay in °/s end-to-end (the ImuSample unit); inclinations
//    are radians. Quaternion math only — no Euler intermediates.

#include <QQuaternion>
#include <QVector3D>
#include <cstdint>
#include <vector>

#include "imu_vision_fuser.h"   // SegmentStream / FusedStreams

namespace pinpoint::analysis::phase_signals {

// Zero-phase 2nd-order Butterworth low-pass: filtered forward then backward
// (no phase lag, so detected event instants carry no systematic bias —
// design A.4 rule 4). Edges are reflection-padded so the swing's start/finish
// regions are usable. fcHz ≥ fsHz/2 or an empty input returns x unchanged.
std::vector<double> lowpassZeroPhase(const std::vector<double> &x,
                                     double fsHz, double fcHz);

// Energy envelope Ω(t) = ‖gyro(t)‖ (°/s), low-passed (zero-phase, fcHz).
std::vector<double> energyEnvelope(const SegmentStream &s,
                                   double fsHz, double fcHz = 10.0);

// World-frame gyro g_w(t) = qAnat·gyro·qAnat⁻¹ (°/s).
std::vector<QVector3D> worldGyro(const SegmentStream &s);

// Principal rotation axis over grid samples in [fromUs, toUs]: the largest
// eigenvector of Σ g_w·g_wᵀ (power iteration — the downswing is near-planar,
// so this is the swing-plane normal when accumulated over it), sign-fixed so
// the mean projection of g_w onto it is positive (positive = downswing
// direction). Returns a null vector when the interval holds no energy.
QVector3D principalRotationAxis(const std::vector<QVector3D> &gw,
                                const std::vector<int64_t> &grid,
                                int64_t fromUs, int64_t toUs);

// Signed rate r(t) = dot(g_w(t), n̂) (°/s).
std::vector<double> signedRate(const std::vector<QVector3D> &gw,
                               const QVector3D &n);

// Gravity-referenced inclination (rad) of a constant segment axis above
// horizontal: asin(dot(qAnat·axis, ẑ_world)). Drift-free (6-axis yaw drift
// never moves a gravity-referenced pitch). Default axis = +Y long/distal.
std::vector<double> inclination(const SegmentStream &s,
                                const QVector3D &segAxis = QVector3D(0, 1, 0));

// Signed axial rate (°/s) about a constant axis in the segment frame — a dot
// product on the measured gyro, no differentiation (design A.4 rule 3).
std::vector<double> axialRate(const SegmentStream &s, const QVector3D &axisSeg);

// still(t): Ω(t) < gyroStillDps AND |‖accel(t)‖ − 1| < accelTolG, required
// across ALL segments (a still hand with a moving pelvis is not "still").
// Empty streams (no segments) yield an all-false mask of grid length.
std::vector<uint8_t> stillMask(const FusedStreams &streams,
                               double gyroStillDps = 15.0,
                               double accelTolG    = 0.08);

// Sub-grid refinement (design A.4 last rule). Both return a FRACTIONAL grid
// index; convert with fracIndexToUs(). refineExtremum fits a parabola through
// (i−1, i, i+1) around a peak/trough at i; refineZeroCrossing interpolates the
// crossing between i and i+1 (y[i] and y[i+1] must straddle zero). Indices at
// the array edge (or degenerate neighbourhoods) return i unchanged.
double refineExtremum(const std::vector<double> &y, int i);
double refineZeroCrossing(const std::vector<double> &y, int i);

// Fractional grid index → absolute µs (linear between grid instants, clamped).
int64_t fracIndexToUs(const std::vector<int64_t> &grid, double fidx);

} // namespace pinpoint::analysis::phase_signals
