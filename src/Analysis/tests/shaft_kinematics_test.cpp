// ShaftTracker R6 kinematics (lead-arm → club, double-pendulum wrist-cock model)
// — standalone test.
//
// K0 lands an empty passing stub so the target exists and the suite stays green;
// the K2a phase replaces this body with the seed-table reproduction, branch-flip,
// σ_β-widening, and angle-envelope assertions. See
// docs/implementation/shaft_detection_skeleton_impl.md (phase K2a).
//
// Pure: no Qt, no OpenCV (shaft_kinematics.h is header-only <cmath>, added in K2a).

int main()
{
    return 0;
}
