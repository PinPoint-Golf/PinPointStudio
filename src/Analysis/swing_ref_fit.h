// swing_ref_fit.h — P-position fit for the swing-reference model (Phase A).
//
// Deterministic, bounded coordinate-descent fit of the geometric swing-reference
// model (src/Models/swing_reference.{h,cpp}) to the MEASURED coaching P positions
// (P1..P8, image px). Recovers the address-frame hub X/Z, lead-arm length, club
// length and backswing plane offset that best reproduce the measured grip/head px
// at the P anchors under the fixed orthographic projection (origin/scale/xSign
// stay the anthro-measured values — the clubhead-at-ball anchor is preserved by
// the model's own ball-contact IK, not by re-fitting the projection).
//
// Ports the corpus-validated prototype fit_proto.py: 4 sweeps over
// (arm, Lc, hubZ, hubX, Δθ_bs), golden-section line search (24 iters) per
// coordinate, with reach-feasibility floors so the two-circle IK anchor always has
// a solution. No RNG, fixed iteration counts, anchors processed in ascending p —
// running the fit twice on the same input is bit-identical.
//
// House rules: Analysis-side (src/Models must NOT depend on Analysis) — the fit
// passes plain GolferAnthro/ClubSpec/RefConfig params INTO the model factory. Qt
// value types, no OpenCV, quaternions only via the model. Degrades (never throws)
// to fitted=false with the seed returned unchanged when there are < 4 distinct P
// anchors or the projection scale is non-positive.

#pragma once

#include <QPointF>

#include <vector>

#include "../Models/swing_reference.h"   // GolferAnthro, ClubSpec, RefConfig (pinpoint::swingref)

namespace pinpoint::swingref {

// One measured coaching P anchor (image px). `p` is the coaching P-index (1..8);
// anchors outside that range are ignored. gripPx = measured shaft butt/grip px;
// headPx = measured clubhead px; conf weights the anchor in the objective.
struct SwingRefFitAnchor {
    int     p     = 0;
    QPointF gripPx;
    QPointF headPx;
    float   conf  = 1.0f;
};

// Fit inputs. The seed anthro/club/plane come from the anthro estimator + the
// labelled club; hubY (seedAnthro.hub.y()), ballOffsetX, lie/lean, handedness and
// the projection (originPx/pxPerM/xSign) are all HELD FIXED — only hub X/Z, arm,
// club length and Δθ_bs are fitted.
struct SwingRefFitInput {
    GolferAnthro seedAnthro;          // hub (hubY fixed), armLength seed, rightHanded
    double       ballOffsetX = 0.0;   // ball X vs stance centre (m) — fixed
    ClubSpec     seedClub;            // length seed, lieDeg, forwardLeanP7Deg
    RefConfig    cfg;                 // Δθ_bs seed = cfg.backswingPlaneOffsetDeg; carries the build params
    std::vector<SwingRefFitAnchor> anchors;   // measured P anchors (p∈1..8)

    // Fixed orthographic projection (OrthographicProjection convention:
    // u = originPx.x + xSign·pxPerM·X,  v = originPx.y − pxPerM·Z).
    QPointF originPx;
    double  pxPerM = 0.0;
    double  xSign  = 1.0;
};

// Fit outputs. On fitted=true the anthro/clubLengthM/planeOffsetDeg carry the
// fitted values (anthro.hub.y() == seed, anthro.rightHanded == seed). On
// fitted=false everything mirrors the seed unchanged (safe to build the seed model
// from these). rms*Px are the weighted RMS px residuals (sqrt(SSE/Σconf·(Wp+Wh)))
// before and after the fit; -1 when not computed.
struct SwingRefFitResult {
    GolferAnthro anthro;
    double  clubLengthM    = 0.0;
    double  planeOffsetDeg = 0.0;
    double  rmsBeforePx    = -1.0;
    double  rmsAfterPx     = -1.0;
    int     anchorsUsed    = 0;
    bool    fitted         = false;
};

SwingRefFitResult fitSwingReference(const SwingRefFitInput& in);

} // namespace pinpoint::swingref
