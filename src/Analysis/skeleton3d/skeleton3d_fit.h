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
    // THE LEAN RIG (swing_3d_viz_design.md §13.2 (A)): the fit's own 38 unknowns expand to the
    // rig's 48 angles, θ = M·q. One spine (flex, lat, twist) shared over Spine/Spine1/Spine2 by
    // segment length; the clavicles follow the upper arm (elevation = clavElevGain·arm abduction,
    // protraction = clavProtGain·arm flexion). The rig, the documents and the viewer keep all 48.
    // The gains: 0.25 (a textbook scapulohumeral rhythm) put the synthetic lead forearm in a rolled
    // minimum (roll error 120°, not fixed by more iterations); 0.10 held it at 23°.
    // SPLINE TRAJECTORIES (design §13.2 (B)): each unknown is a cubic B-spline in time, knots every
    // knotFastMs through top − 50 ms → impact + 60 ms and every knotSlowMs elsewhere. The per-frame
    // smoothness becomes a second difference on the coefficients at the same physical σ.
    // Both ON since §13.5–13.6 (lean spine-only + splines graded best of the four on the corpus); the
    // clavicle rhythm failed its gates and stays off.
    bool   splineBasis  = true;
    double knotFastMs   = 10.0;
    double knotSlowMs   = 40.0;
    bool   leanRig      = true;
    bool   leanClavicles = false;  // with leanRig: false keeps the clavicles free (the spine alone is lean, 42 angles)
    double clavElevGain = 0.10;
    double clavProtGain = 0.10;

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
    // THE CLUB IS IN THE HANDS (σ m × body scale, per axis): the grip point against the palm's
    // centre. The images hold the shaft's DIRECTION, not where along it the hands are, so this
    // prior is all that keeps the club in the hands — and at 0.03 it did not. The face-on pitch
    // and the floor's height trade against each other at almost no cost, the club's length is
    // held at the tape and its head on the floor, so whatever the cameras got wrong came out as
    // the club sliding out of the hands: 14–26 cm from the lead wrist on every swing of 5 Oct 2026
    // (a gap wedge at 47° to the ground at address), 8–10 cm on 07-04. At 0.01 on 5 Oct s13:
    // 20 → 12 cm, the address shaft 47° → 52°, reprojection unchanged and the total cost DOWN
    // (74826 → 74719); 07-04 s8 9 → 7 cm, cost +0.03 %.
    double gripOffsetSigmaM = 0.01;
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
    double spineFlexSigmaDeg   = 7.0;
    // The PELVIS's forward and sideways tilt change SLOWLY: their own smoothness σ (rad/s²), with
    // no loosening through the downswing. Two hip keypoints cannot see pelvis tilt, so without it
    // tilt, hip flexion and spine flexion trade freely: on the corpus the pelvis seesawed 5°→55°
    // against the hips (0°→75°) and spine (−17°→+15°) every ~0.3 s — smooth frame to frame, but a
    // wobbling figure no golfer makes (a real pelvis changes tilt ~10–15° over a downswing,
    // ~20 rad/s²). ⚠ Tried first as "stay near the swing's mean tilt": that let the fit tilt the
    // whole world to fake a constant tilt while the golfer turned (synthetic: 1 → 8.6 cm).
    double pelvisTiltAccRad    = 25.0;
    // The pelvis's YAW acceleration σ (rad/s²), like its tilt: its own, never loosened through the
    // downswing. ON at 200 (≈ 11 500 °/s², well above what a pelvis does) since the lower-body
    // follow-up (2026-10-02): with the general σ (smoothAccRad × fastFactor ≈ 1800 rad/s² in the
    // fast window) the pelvis stopped and restarted at the ball on 15/15 07-04 swings, because the
    // face-on view is blind to yaw at square and the down-the-line hips turn end-on — nothing in the
    // data pins it there. On s8: total cost +0.08 %, reprojection unchanged, the stall gone; 100
    // reads the same, 400 keeps a shallow dip (skeleton_rate_k0_20261002.md §10). 0 = the general σ.
    double pelvisYawAccRad     = 200.0;
    // THE SHOD FOOT: how far above the rig's bare-sole foot markers the pose model's toe and heel
    // keypoints sit (m, at unit scale), applied along the foot's up axis. 0 = bare-foot priors.
    // ON since 2026-10-02 (skeleton3d v5): with bare-sole priors the fit put the ankle joint 3.8 cm
    // off the floor and tipped both feet ~20° toes-up (ankle dorsiflexed 20–35° at address); the
    // down-the-line view shows the shoe's toe and heel keypoints only 4–6 cm below the ankle. On
    // 07-04 s8, toe 6 cm / heel 3 cm was the cheapest fit of nine pairs — reprojection 8.06/4.39 →
    // 6.91/3.69 px, slip 21.3 → 19.2 mm, ankle 9–10 cm, the trail foot flat at address and rolling
    // onto its toes after impact (skeleton_rate_k0_20261002.md §13).
    double footToeLiftM        = 0.06;
    double footHeelLiftM       = 0.03;
    // THE CLUB IS GROUNDED AT ADDRESS (σ m, Cauchy; 0 = off). Over address − 200 ms … + 30 ms the
    // fitted clubhead sits groundedClubLiftM above the floor the planted feet give. ON since
    // 2026-10-02 (skeleton3d v6). It is the one thing in the swing that touches the floor half a
    // metre in FRONT of the feet, so it is what holds the world level about the target line: the
    // planted feet are too short a baseline, and the face-on pitch prior and the DTL roll prior
    // are all that held it before. On 07-04 the same fixed face-on camera solved at 1.5° of pitch
    // on swings 1–3 and 8.3° on swings 4–15; on the latter the floor came out 8–10 cm below a
    // full-length club's head and the club was fitted 9–21 cm short to make up the rest. On s8
    // with free cameras: pitch 8.1° → 0.3°, the head 16.4 → 3.6 cm above the floor, the address
    // shaft 46° → 56° to the ground (the DTL image reads 54.5°), reprojection and total cost
    // unchanged — the tilt was a free direction (swing_3d_viz_design.md §14). TWO-CAMERA FITS ONLY,
    // and so is the tighter club length below: a face-on-only fit is left exactly as it was.
    // The lift is 0: the trackers' clubhead point at address is the SOLE (both views, 07-04 s8), so
    // the fitted head is the club's end and rests ON the floor. At 2 cm it cost the club 2.4 cm.
    double groundedClubSigmaM  = 0.02;
    double groundedClubLiftM   = 0.0;
    // A RECORDED CLUB IS NOT FITTED (v7): its length is the tape's, and σ 5 mm holds it there. It
    // was 0.08, which let the fit shorten a 0.94 m club to 0.73, and then 0.03 (v6), which still
    // gave 0.90: the fit has almost nothing to measure a length WITH — a clubhead is handed to it
    // on ~20 of ~750 face-on samples and never at address — so any slack went wherever the cameras'
    // error pushed it. Held, on 07-04 s1 / s8 / s9: 0.938–0.940 m, reprojection unchanged to 0.01 px
    // and the total cost up 0.02 %, the hands 2–3 cm higher. An unrecorded club keeps its loose
    // 0.15 (a driver default must not roll the forearms to fit). `clubLengthSigmaM` > 0 overrides both.
    double clubLengthSigmaKnownM = 0.005;
    double clubLengthSigmaM    = 0.0;
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

    // ── the club's depth branch where the DTL is blind (skeleton3d_shaft_branch_design.md) ──
    // The face-on camera cannot tell a shaft tilted toward it from its mirror tilted away; only
    // the DTL can. On frames the DTL does not see the club, a weak pull keeps the shaft near its
    // swing plane (r_plane, Cauchy), and a branch pass tries the mirror explicitly — the two are
    // separated by a barrier (the measured face-on clubhead) that the solver cannot cross.
    bool   usePlane           = true;
    double planeSigmaDeg      = 10.0;
    int    planeMinFrames     = 8;     // a self-plane rests on at least this many two-view frames…
    double planeMaxRmsDeg     = 5.0;   // …and fits them this well, or it is refused
    bool   useCataloguePlane  = true;  // face-on only / a refused self-plane: the club's catalogue plane
    bool   branchPass         = true;
    double branchSeedDeg      = 2.0;   // seed the mirror where it is nearer the plane by more than this (the cost decides)
    double branchSeedSigmaDeg = 2.0;
    int    branchIters        = 8;     // seed iterations
    // Relax iterations: the mirror must reach ITS OWN minimum before the costs are compared — at 8, a
    // half-settled mirror lost on the face-on shaft angle it images identically (4 July swing 4).
    int    branchRelaxIters   = 25;
    // Inside the BRANCH PASS only (seed, relax, and the comparison): the wrist / pronation /
    // humeral-rotation priors' σ × this after impact + 60 ms. They pull to neutral, and the release
    // genuinely rolls the forearms — left at full weight they outvoted the cameras and the plane and
    // kept the club on its wrong mirror at P8 (design §10). The fit itself keeps them (× 1): loosened
    // for the whole fit they left roll unheld after impact (synthetic roll p90 8 → 13°).
    double branchReleasePriorFactor = 4.0;
    // The CALLER's DTL pose bracket (Skeleton3DStage::observePose, pose_schedule.h bracketAt): a DTL
    // frame pair this close interpolates to the face-on instant, else the nearest frame within
    // dtlNearestUs. Today's constants; with pose.dtlLocalGap they are the floors under 1.5× / 0.75×
    // the DTL track's local spacing (pose_inference_performance_plan.md step 4).
    int64_t dtlBracketUs = 12000;
    int64_t dtlNearestUs = 6000;
    // Test hooks: force the mirror branch in before the branch pass (the pass must return it);
    // hide the DTL shaft angle and clubhead from impact + this (µs, negative = before impact, 0 = off)
    // — the grade's dropout.
    bool    debugForceMirror        = false;
    int64_t debugDropDtlShaftAfterUs = 0;
};

// What a session pool fixes in one swing's fit (design §13.2 (C)): the golfer's shared values —
// bone scales, the shoulder/hip surface offsets, the grip, the club — and the cameras of the swing's
// camera epoch. Pooled at re-analysis and session end; live shots never carry one.
struct SkeletonCalib {
    bool hasScale = false;
    std::array<double, GroupCount> scale {};
    bool hasSym = false;
    std::array<double, 2 * kSymGroups> sym {};
    bool hasGrip = false;
    std::array<double, 9> grip {};           // lead axis (3), lead offset (3), trail offset (3), hand-local
    double clubToHeadM = std::numeric_limits<double>::quiet_NaN();
    bool hasCam = false;
    std::array<double, 10> cam {};           // fF pF cDx cDy cDz psiD pD fD rF rD
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
    bool   clubLengthKnown = false;          // clubLengthM is the club record's (tape), not a default
    // Per frame, per foot marker (17..22): planted on the floor.
    std::vector<std::array<uint8_t, 6>> footContact;
    std::vector<ImuTrack> imu;
    std::vector<double> hmFlexDeg, hmRadDeg; // lead wrist, per frame; NaN = none
    // Face-on ball at address (px), for the display origin; < 0 = none.
    double ballU = -1, ballV = -1;
    FitConfig cfg;
    // Test hook: start stage 2 from these DoFs (one vector per frame) instead of stage 1.
    const std::vector<std::vector<double>> *debugInitTheta = nullptr;
    // A session pool's values, fixed in this fit (null: the swing fits its own).
    const SkeletonCalib *fixedCalib = nullptr;
};

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

FitResult fitSkeleton(const FitInput &in);

// Drop every frame before `fromUs`. The first ~100 ms of any fit are weakly held (smoothness has
// neighbours on one side only) — swing 2 of 4 July read a 56° pelvis tilt on its first frame — so
// the stage fits from well before address and keeps only what it will show.
inline void trimResultBefore(FitResult &r, int64_t fromUs)
{
    size_t k = 0;
    while (k < r.t_us.size() && r.t_us[k] < fromUs) ++k;
    if (k == 0 || k >= r.t_us.size()) return;
    auto cut = [k](auto &v) { if (v.size() > k) v.erase(v.begin(), v.begin() + long(k)); };
    cut(r.t_us); cut(r.theta); cut(r.joints); cut(r.tier); cut(r.sigmaM); cut(r.flags);
    cut(r.grip); cut(r.shaftDir); cut(r.shaftTier);
}

// Project a world point through a fitted camera (view 0 = face-on, 1 = DTL). Returns
// false behind the camera. Exposed for tests and the grader.
bool projectPoint(const Cameras &c, int view, int W, int H, const V3 &p, double &u, double &v);

} // namespace pinpoint::skeleton3d
