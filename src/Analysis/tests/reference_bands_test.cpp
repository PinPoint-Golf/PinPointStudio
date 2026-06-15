// Standalone test for ConfigReferenceBandProvider (Wrist assessment Tier-1 bands).
// Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake --build build/analyzer-tests -j4
//   ctest --test-dir build/analyzer-tests -R reference_bands_test --output-on-failure
//
// Verifies band lookup (hit / centre / amber margin), graceful handling of a missing entry
// (DOF with no producer, or a position with no corridor) and a malformed band, and the factory.

#include "../reference_bands.h"

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void checkNear(const char *label, double got, double want)
{
    const bool ok = std::fabs(got - want) <= 1e-9;
    std::printf("  [%s] %-40s got %8.3f  want %8.3f\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}

int main()
{
    std::printf("=== ConfigReferenceBandProvider ===\n");
    ConfigReferenceBandProvider p;

    // 1. A known band: lead-wrist radial-ulnar @P6 = green [20,40], amber margin 5 → [15,45].
    {
        std::printf("-- 1. band lookup --\n");
        const Band b = p.band(PpJointDof::LeadWristRadUln, PpSwingPosition::P6);
        check(b.valid, "radUln P6 valid");
        checkNear("radUln P6 greenLo", b.greenLo, 20.0);
        checkNear("radUln P6 greenHi", b.greenHi, 40.0);
        checkNear("radUln P6 amberLo", b.amberLo, 15.0);
        checkNear("radUln P6 amberHi", b.amberHi, 45.0);
    }

    // 2. Missing entries → invalid (no false band).
    {
        std::printf("-- 2. missing entries --\n");
        check(!p.band(PpJointDof::TrailWristFlexExt, PpSwingPosition::P8).valid,
              "trail wrist P8 has no corridor → invalid");
        check(!p.band(PpJointDof::TrailShoulderRotation, PpSwingPosition::P3).valid,
              "un-instrumented DOF → invalid");
    }

    // 3. Malformed band handled gracefully by the classifier (no false Green).
    {
        std::printf("-- 3. malformed band → Grey --\n");
        Band invalid;                 // valid == false
        check(classifyDelta(0.0, invalid) == PpRag::Grey, "invalid band → Grey");
        Band inverted{ 20.0, 10.0, 5.0, 25.0, true };   // greenLo > greenHi
        check(classifyDelta(15.0, inverted) == PpRag::Grey, "inverted green range → Grey");
    }

    // 4. Classifier boundaries on a clean band: green core, amber margin, red outside.
    {
        std::printf("-- 4. classifier boundaries --\n");
        const Band b{ 10.0, 20.0, 5.0, 25.0, true };
        check(classifyDelta(15.0, b) == PpRag::Green, "centre → Green");
        check(classifyDelta(10.0, b) == PpRag::Green, "green lower edge → Green");
        check(classifyDelta(20.0, b) == PpRag::Green, "green upper edge → Green");
        check(classifyDelta(7.0,  b) == PpRag::Amber, "in amber margin → Amber");
        check(classifyDelta(25.0, b) == PpRag::Amber, "amber upper edge → Amber");
        check(classifyDelta(4.0,  b) == PpRag::Red,   "below amber → Red");
        check(classifyDelta(30.0, b) == PpRag::Red,   "above amber → Red");
    }

    // 5. Factory returns a working provider.
    {
        std::printf("-- 5. factory --\n");
        auto fp = makeReferenceBandProvider();
        check(fp != nullptr, "factory non-null");
        check(fp->band(PpJointDof::LeadWristRadUln, PpSwingPosition::P6).valid, "factory provider bands");
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
