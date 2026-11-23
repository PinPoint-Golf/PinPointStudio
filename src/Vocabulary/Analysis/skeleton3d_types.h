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

// The skeleton3d fit's RESULT (docs/design/swing_3d_viz_design.md §3): the fitted rig per frame, its
// cameras, the club planes and the fit's diagnostics, with the few rig facts its arrays are sized
// by (the point type, the length groups, the marker and symmetric-group counts, the joint count).
// Plain value types the shot's analysis record holds (SwingAnalysis::skeleton3d, swing_analysis.h),
// so they sit in the vocabulary; the rig's kinematics stay in Skeleton3d/skeleton3d_rig.h and the
// fit in Skeleton3d/skeleton3d_fit.h, which include this. Pure std. Moved out of those two headers
// unchanged (refactor 7.3, A-11), but for one spelling: FitResult's per-joint arrays are sized by
// kRigJointCount, which skeleton3d_rig.h checks against Rig::N (ybot::JointCount, generated).

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace pinpoint::skeleton3d {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The Y-bot's joint count (ybot_rig.h, generated: ybot::JointCount; Rig::N), which sizes FitResult's
// per-joint arrays. skeleton3d_rig.h static_asserts the two agree.
constexpr int kRigJointCount = 27;

struct V3 {
    double x = 0, y = 0, z = 0;
    V3 operator+(const V3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator-(const V3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator*(double s) const { return { x * s, y * s, z * s }; }
    V3 operator-() const { return { -x, -y, -z }; }
    V3 &operator+=(const V3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    double dot(const V3 &o) const { return x * o.x + y * o.y + z * o.z; }
    V3 cross(const V3 &o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    double norm() const { return std::sqrt(dot(*this)); }
    V3 unit() const { const double n = norm(); return n > 0 ? (*this) * (1.0 / n) : *this; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};

enum LengthGroup : int {
    GPelvis = 0, GSpine, GHead, GClavicle, GUpperArm, GForearm, GHand, GThigh, GShank, GFoot,
    GroupCount
};

// COCO-WholeBody body + feet markers the fit reads (0–16 body, 17–22 feet).
constexpr int kMarkerCount = 23;

constexpr int kSymGroups = 2;

struct Cameras {
    double fF = 0, pF = 0;                   // face-on: focal (px), pitch (rad, + = down); centre at origin
    V3     cD;                               // DTL centre (world)
    double psiD = 0, pD = 0, fD = 0;         // DTL yaw (0 = looking +X), pitch, focal
    double zG = 0;                           // floor height (world Z)
    double rF = 0, rD = 0;                   // roll about each optical axis (rad, + = image clockwise)
};

enum Tier : uint8_t { TierAbsent = 0, TierInferred = 1, TierConstrained = 2, TierMeasured = 3 };
enum FrameFlag : uint8_t { FlagSwapFo = 0x01, FlagSwapDtl = 0x02, FlagLimitHeld = 0x04,
                           FlagShaftBranch = 0x08 };   // the branch pass put the club on its other depth branch

// A reference plane for the club (through the origin: a plane of DIRECTIONS).
struct ClubPlane {
    bool   valid = false;
    V3     n;                                // unit normal
    int    count = 0;                        // frames it rests on (self-planes)
    double rmsDeg = std::numeric_limits<double>::quiet_NaN();
    std::string source;                      // "self" | "catalogue" | ""
};

struct FitResult {
    bool valid = false;
    std::string reason;
    // THE GATE BEHIND A REFUSAL (trust stage 6): the catalogue id and a short code per reason
    // ("fewFrames", "noScale", "noHips", "noConverge", "noDtl", "dtlDropped"), set where the fit refuses — empty when it
    // did not, or when it was switched off (a switch is not a gate). Plain strings: the solver
    // builds without Qt; Skeleton3DStage turns them into the swing's gate record.
    std::string gateId;
    std::string reasonCode;
    int faceOnFrames = 0;                    // the face-on frames the fit was handed
    int minFaceOnFrames = 10;                // the floor it compared them against
    bool   dtlUsed = false;
    // Why a DTL view that was supplied did not make it into the fit ("" when it did, or none was
    // supplied) — the caller logs it; the fit used to drop the camera without a word.
    std::string dtlDropReason;
    bool   foMirrored = false;
    std::string scaleSource;                 // "height" | "default"
    double scaleGlobal = 1.0;
    std::array<double, GroupCount> scale {};
    std::array<std::array<double, 3>, kMarkerCount> markerOffset {};
    Cameras cam;
    double gammaDeg = kNaN;                  // angle between the two views' ground-plane rays
    double rRatio = kNaN;                    // DTL px-per-m ÷ face-on px-per-m, at the golfer
    std::array<double, 3> gripAxisLocal {}, gripOffsetLocal {}, trailGripOffsetLocal {};
    std::array<double, 2 * kSymGroups> symOffsets {};   // the shoulder/hip pairs' width, height offsets
    bool calibFixed = false;                  // the shared values came from a session pool
    double clubLengthM = kNaN;               // fitted (the club record is its prior)

    std::vector<int64_t> t_us;
    std::vector<std::vector<double>> theta;  // per frame, rig().dofCount() values
    std::vector<std::array<V3, kRigJointCount>> joints;
    std::vector<std::array<uint8_t, kRigJointCount>> tier;
    std::vector<std::array<float, kRigJointCount>> sigmaM;
    std::vector<uint8_t> flags;
    std::vector<V3> grip, shaftDir;          // lead-hand grip point, shaft direction (butt → head)
    std::vector<uint8_t> shaftTier;          // 0 none, 1 inferred (model only), 2 one view, 3 both views

    // Diagnostics (design §8.2).
    double costInit = 0, costFinal = 0;
    int    iterations = 0;
    double ms = 0;
    double reprojMedPxFo = kNaN, reprojMedPxDtl = kNaN;
    std::array<double, GroupCount> rawLengthCv {};   // the unconstrained triangulation's length CV
    int    nSwapFo = 0, nSwapDtl = 0, nLimitHeld = 0;
    // The cross-view pass (FitConfig::crossViewMissMm): keypoints whose σ it inflated, per view, and
    // the frames with at least one.
    int    nCrossViewFo = 0, nCrossViewDtl = 0, nCrossViewFrames = 0;
    double footSlipP90Mm = kNaN;
    // THE HANDS' LOCK (Mark, 19 Oct: both hands are locked together on the grip — anything else means
    // the fit is wrong): how far the FIT left the trail grip point from its locked place (on the
    // shaft's line, the swing's spacing down it), p90 and worst, mm, over the frames the view shows
    // (address − 150 ms on) — before FitConfig::gripLockExact moves the trail arm onto it. NaN = no
    // grip term.
    double gripLockP90Mm = kNaN, gripLockMaxMm = kNaN;
    double debugJacobianErr = kNaN;          // max |analytic − numeric| / (1 + |numeric|)
    std::string debugJacobianWorst;
    // The depth branch (skeleton3d_shaft_branch_design.md): the planes used (address → top, top →
    // finish), the catalogue inclination where one stood in, how many blind frames the plane term
    // held, and what the branch pass tried and kept.
    ClubPlane planeBack, planeDown;
    double catalogueInclDeg = kNaN;
    bool   catalogueUncalibrated = false;
    int    nPlaneFrames = 0, nBranchRuns = 0, nBranchKept = 0;
    int    nUnknowns = 0;                    // the per-swing motion unknowns (per-frame or spline) + the free shared ones
    double branchMs = 0;

    // Display frame: origin (ball at address on the floor, else mid-heels) and the
    // stance axis's heading (trail heel → lead heel) in the world XY plane.
    V3     displayOrigin;
    double stanceYawRad = 0;
    bool   ballValid = false;
    V3     ballWorld;
};

} // namespace pinpoint::skeleton3d
