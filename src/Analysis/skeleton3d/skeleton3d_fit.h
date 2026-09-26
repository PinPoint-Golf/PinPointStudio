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

// skeleton3d — a rigid, jointed skeleton fitted to every sensor that saw the swing
// (docs/design/swing_3d_viz_design.md §3). Qt-free and Eigen-free at this interface
// (Eigen lives in the .cpp) so the stage, swinglab and the synthetic test all call the
// same function with plain data.
//
// THE POINT, in one line: the skeleton is the unknown and the cameras are observations
// of it — bone lengths are ONE value per swing (not per frame), knees and elbows are
// hinges, joints have ranges — so the anatomy constrains the coordinates instead of
// being checked against them afterwards.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "skeleton3d_rig.h"

namespace pinpoint::skeleton3d {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// One keypoint observation in one view (pixels). sigma ≤ 0 ⇒ not observed.
struct KpObs { double u = 0, v = 0, sigma = 0; };

struct ViewObs {
    std::array<KpObs, kMarkerCount> kp {};
    double shaftTheta = kNaN;      // image angle butt → head (rad, atan2, y down); NaN = none
    double shaftSigma = 0;         // rad
    // The MEASURED clubhead (px), headSigma ≤ 0 = none. A shaft direction cannot see the
    // hands roll about the shaft's own axis; the head, ~0.9 m down that axis, can.
    double headU = 0, headV = 0, headSigma = 0;
};

// An orientation sensor on a segment: world-from-sensor rotations, gravity-aligned
// (+Z up) with an unknown heading. The fit solves the heading and the mount.
struct ImuTrack {
    int joint = -1;                // ybot joint the sensor rides on
    std::vector<Q> q;              // per frame
    std::vector<uint8_t> valid;    // per frame
    // The sensor's mount in the segment's frame, from a calibration record. Without it the
    // mount is solved — and then a CONSTANT rotation of the segment is indistinguishable from
    // a different mount: the sensor pins how the segment MOVES, not where it points.
    bool hasMount = false;
    Q    mount;
};

struct FitConfig {
    bool   enabled      = true;
    bool   useDtl       = true;
    bool   useLimits    = true;
    bool   useContact   = true;
    bool   useShaft     = true;
    bool   useGrip      = true;
    bool   useClubhead  = true;
    bool   useSmooth    = true;
    bool   useImu       = true;
    bool   useHm        = true;
    bool   fitCameras   = true;    // false ⇒ camera geometry frozen at the initialiser
    bool   fitDtlRoll   = true;    // the DTL camera's roll about its optical axis
    // false (the default) ⇒ length scales frozen at the Y-bot's proportions × height. ⚠ FITTED
    // lengths are not trustworthy on real data: ~10 shared scales against thousands of keypoint
    // residuals means the prior carries no weight, and any camera-model error (no lens
    // distortion, an assumed principal point) is soaked up as bone length — the corpus fit read
    // a 1.96× spine and a 0.2× head. On synthetic data (no model error) they recover to 2 %.
    bool   fitLengths   = false;
    bool   labelSwap    = true;

    double faceOnDistanceM = 2.0;  // initial face-on camera → golfer distance
    double dtlDistanceM    = 2.2;  // initial DTL camera → golfer distance
    double lengthSigma     = 0.05; // prior σ on each group scale, fraction
    // Prior σ on the OVERALL body scale (the geometric mean of the seen groups' scales) against
    // the athlete's height. Size and distance are nearly one image; only perspective splits
    // them, and perspective is what this camera model knows least (no lens distortion, principal
    // point assumed central). On the corpus, left to perspective, the whole body came out 1.3×
    // its height and 0.35 m further away. The height decides; the groups vary around it.
    double globalScaleSigma = 0.02;
    double cauchyC         = 3.0;  // robust-loss knee, in σ
    double smoothAccRad    = 150.0;   // joint angular acceleration σ, rad/s² (slow phases)
    double smoothAccRootM  = 4.0;     // root acceleration σ, m/s² (slow phases)
    double fastFactor      = 12.0;    // σ × this over top−50 ms → impact+60 ms
    double limitSigmaDeg   = 2.0;
    double shaftSigmaDegFo = 3.0;
    double shaftSigmaDegDtl = 4.0;
    double gripSigmaM      = 0.02;
    double contactSigmaM   = 0.01;
    double imuSigmaDeg     = 4.0;
    double hmSigmaDeg      = 4.0;
    // Posture priors (per frame). The spine's three segments bend TOGETHER (a coupling on
    // each axis) and the trunk hinges mostly at the hips: two hip keypoints cannot see pelvis
    // tilt, so without these the tilt, hip flexion and spine flexion trade freely. The hand's
    // roll ABOUT THE SHAFT is not seen by a shaft direction either (it fixes two of the
    // wrist+forearm's three rotations), so wrist and pronation get a weak pull to neutral —
    // a HackMotion, where worn, overrides it.
    double spineCoupleSigmaDeg = 4.0;
    double spineFlexSigmaDeg   = 12.0;
    double wristSigmaDeg       = 30.0;
    double pronationSigmaDeg   = 45.0;
    double clavicleSigmaDeg    = 10.0;   // the shoulder girdle vs the upper spine: both move the shoulder point
    // A STRAIGHT arm's humeral rotation and forearm pronation spin about the same line, so only
    // their sum is seen (the lead arm, address → impact). A weak pull to neutral splits them.
    double armRotSigmaDeg      = 35.0;
    double hmFlexSign      = 1.0;  // our wrist.flex = sign · hm.leadWristFlexExt (UNVALIDATED)
    double hmRadSign       = 1.0;
    int    stage1Iters     = 12;
    int    stage2Iters     = 25;
    // Tier thresholds (m).
    double measuredSigmaM    = 0.02;
    double constrainedSigmaM = 0.05;
    // Test hook: after stage 1, compare the analytic Jacobian rows of this many frames
    // against central differences (robust loss switched off) — FitResult::debugJacobianErr.
    int    debugJacobianFrames = 0;
};

struct FitInput {
    std::vector<int64_t> t_us;               // the frame grid (face-on instants)
    std::vector<ViewObs> fo;                 // one per frame
    std::vector<ViewObs> dtl;                // one per frame, or EMPTY = face-on only
    int foW = 0, foH = 0, dtlW = 0, dtlH = 0;
    int64_t addressUs = 0, topUs = 0, impactUs = 0;
    bool   leadIsLeft = true;
    double heightM = 0;                      // 0 = unknown
    double clubLengthM = 0;                  // for drawing / reporting; 0 = unknown
    // Per frame, per foot marker (17..22): planted on the floor.
    std::vector<std::array<uint8_t, 6>> footContact;
    std::vector<ImuTrack> imu;
    std::vector<double> hmFlexDeg, hmRadDeg; // lead wrist, per frame; NaN = none
    // Face-on ball at address (px), for the display origin; < 0 = none.
    double ballU = -1, ballV = -1;
    FitConfig cfg;
    // Test hook: start stage 2 from these DoFs (one vector per frame) instead of stage 1.
    const std::vector<std::vector<double>> *debugInitTheta = nullptr;
};

struct Cameras {
    double fF = 0, pF = 0;                   // face-on: focal (px), pitch (rad, + = down); centre at origin
    V3     cD;                               // DTL centre (world)
    double psiD = 0, pD = 0, fD = 0;         // DTL yaw (0 = looking +X), pitch, focal
    double zG = 0;                           // floor height (world Z)
    double rF = 0, rD = 0;                   // roll about each optical axis (rad, + = image clockwise)
};

enum Tier : uint8_t { TierAbsent = 0, TierInferred = 1, TierConstrained = 2, TierMeasured = 3 };
enum FrameFlag : uint8_t { FlagSwapFo = 0x01, FlagSwapDtl = 0x02, FlagLimitHeld = 0x04 };

struct FitResult {
    bool valid = false;
    std::string reason;
    bool   dtlUsed = false;
    bool   foMirrored = false;
    std::string scaleSource;                 // "height" | "default"
    double scaleGlobal = 1.0;
    std::array<double, GroupCount> scale {};
    std::array<std::array<double, 3>, kMarkerCount> markerOffset {};
    Cameras cam;
    double gammaDeg = kNaN;                  // angle between the two views' ground-plane rays
    double rRatio = kNaN;                    // DTL px-per-m ÷ face-on px-per-m, at the golfer
    std::array<double, 3> gripAxisLocal {}, gripOffsetLocal {}, trailGripOffsetLocal {};
    double clubLengthM = kNaN;               // fitted (the club record is its prior)

    std::vector<int64_t> t_us;
    std::vector<std::vector<double>> theta;  // per frame, rig().dofCount() values
    std::vector<std::array<V3, Rig::N>> joints;
    std::vector<std::array<uint8_t, Rig::N>> tier;
    std::vector<std::array<float, Rig::N>> sigmaM;
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
    double footSlipP90Mm = kNaN;
    double debugJacobianErr = kNaN;          // max |analytic − numeric| / (1 + |numeric|)
    std::string debugJacobianWorst;

    // Display frame: origin (ball at address on the floor, else mid-heels) and the
    // stance axis's heading (trail heel → lead heel) in the world XY plane.
    V3     displayOrigin;
    double stanceYawRad = 0;
    bool   ballValid = false;
    V3     ballWorld;
};

FitResult fitSkeleton(const FitInput &in);

// Project a world point through a fitted camera (view 0 = face-on, 1 = DTL). Returns
// false behind the camera. Exposed for tests and the grader.
bool projectPoint(const Cameras &c, int view, int W, int H, const V3 &p, double &u, double &v);

} // namespace pinpoint::skeleton3d
