// Standalone test for the Metric Catalogue layer (metric_catalogue / manifest / providers /
// resolver). Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests --target metric_catalogue_test --parallel 4
//   ctest --test-dir build/analyzer-tests -R metric_catalogue --output-on-failure
//
// Covers: manifest completeness (the 12 live keys, unique, correct types/groups), query filtering
// (type / group / scored / availableOnly), per-shot resolve() across ShotContexts (session gating,
// IMU-role gating, club-track / face-on gating), and corridor() delegation (DOF → reference_bands,
// non-DOF → nullopt, non-checkpoint phase → nullopt).

#include "../metric_catalogue.h"
#include "../reference_bands.h"

#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void checkEqI(int got, int want, const char *label)
{
    const bool ok = (got == want);
    std::printf("  [%s] %-40s got %d  want %d\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}

static int countType(const MetricCatalogue &cat, MetricType t)
{
    MetricQuery q;
    q.type = t;
    return static_cast<int>(cat.query(q).size());
}

// A Wrist-Motion shot with the given IMU roles bound and (optionally) a face-on camera + club track.
static ShotContext wristShot(std::vector<SegmentRole> roles, bool faceOn = false, bool club = false)
{
    ShotContext c;
    c.sessionType  = 1;               // Wrist
    c.imuRoles     = std::move(roles);
    c.hasFaceOn    = faceOn;
    c.hasClubTrack = club;
    c.tier         = ReconstructionTier::Mono3DPlusImu;
    return c;
}

int main()
{
    std::printf("=== metric catalogue ===\n");
    const MetricCatalogue cat = makeMetricCatalogue();

    // 1. Manifest completeness — the full design catalogue (18 live + 20 planned), each resolvable.
    {
        checkEqI(static_cast<int>(cat.all().size()), 42, "descriptor count == 42");
        const char *live[] = { "leadWristFlexExt", "leadWristRadUln", "forearmPronation",
                               "leadArmFlexion",  "clubheadSpeed",   "handSpeed", "lagAngle",
                               "impactShaftLean", "stanceWidth",     "leadFootFlare",
                               "trailFootFlare",  "toeLineAngle",    "leadHeelLift",
                               "headSway",        "headLift",        "headTilt",
                               "wristScore",      "wristResemblance" };
        bool allPresent = true;
        for (const char *k : live)
            if (!cat.descriptor(QString::fromLatin1(k))) { allPresent = false;
                std::printf("    missing live descriptor: %s\n", k); }
        check(allPresent, "all 18 live keys have a descriptor");
        check(cat.descriptor(QStringLiteral("tempo")) == nullptr, "tempo absent (use tempoBackswing)");
        check(cat.descriptor(QStringLiteral("ballPosition")) == nullptr, "ballPosition absent");
    }

    // 2. Type / group / scored filtering.
    {
        checkEqI(countType(cat, MetricType::TimeSeries),  24, "TimeSeries count");
        checkEqI(countType(cat, MetricType::PointInTime), 12, "PointInTime count");
        checkEqI(countType(cat, MetricType::Summary),      5, "Summary count");
        checkEqI(countType(cat, MetricType::Sequence),     1, "Sequence count (kinematicSequence)");

        MetricQuery gq; gq.group = QStringLiteral("Wrist & forearm");
        checkEqI(static_cast<int>(cat.query(gq).size()), 4, "group 'Wrist & forearm' == 4");

        MetricQuery scq; scq.group = QStringLiteral("Score");
        checkEqI(static_cast<int>(cat.query(scq).size()), 3, "group 'Score' == 3");

        MetricQuery hq; hq.group = QStringLiteral("Head");
        checkEqI(static_cast<int>(cat.query(hq).size()), 3, "group 'Head' == 3");

        MetricQuery brq; brq.group = QStringLiteral("Body rotation");
        checkEqI(static_cast<int>(cat.query(brq).size()), 5, "group 'Body rotation' == 5");

        MetricQuery alq; alq.group = QStringLiteral("Alignment");
        checkEqI(static_cast<int>(cat.query(alq).size()), 4, "group 'Alignment' == 4");

        MetricQuery sq; sq.scored = true;
        checkEqI(static_cast<int>(cat.query(sq).size()), 4, "scored == true → 4 (wrist DOFs)");
    }

    // 3. resolve() — session + IMU-role gating (wrist).
    {
        const ShotContext core = wristShot({ SegmentRole::LeadForearm, SegmentRole::LeadHand });
        check(cat.resolve(QStringLiteral("leadWristFlexExt"), core).state == MetricAvailability::Measured,
              "bow/cup Measured with forearm+hand");
        check(cat.resolve(QStringLiteral("forearmPronation"), core).state == MetricAvailability::Unavailable,
              "roll Unavailable without upper-arm");

        const ShotContext full = wristShot({ SegmentRole::LeadForearm, SegmentRole::LeadHand,
                                             SegmentRole::LeadUpperArm });
        check(cat.resolve(QStringLiteral("forearmPronation"), full).state == MetricAvailability::Measured,
              "roll Measured with upper-arm added");

        ShotContext swing = core; swing.sessionType = 0;   // Swing session
        const MetricAvailability sw = cat.resolve(QStringLiteral("leadWristFlexExt"), swing);
        check(sw.state == MetricAvailability::Unavailable, "wrist Unavailable in a Swing session");
        check(sw.reason.contains(QStringLiteral("Wrist Motion")), "reason names Wrist Motion");

        // sessionType -1 (directory browse) is session-agnostic → available if sensors present.
        ShotContext browse = core; browse.sessionType = -1;
        check(cat.resolve(QStringLiteral("leadWristFlexExt"), browse).state == MetricAvailability::Measured,
              "wrist Measured when browsing (no session) with sensors");
    }

    // 3b. resolve() — Summary scores (ScoreProvider).
    {
        const ShotContext core = wristShot({ SegmentRole::LeadForearm, SegmentRole::LeadHand });
        check(cat.resolve(QStringLiteral("wristScore"), core).state == MetricAvailability::Measured,
              "wristScore Measured on a Wrist shot with forearm+hand");
        check(cat.resolve(QStringLiteral("wristResemblance"), core).state == MetricAvailability::Measured,
              "wristResemblance Measured on a Wrist shot with forearm+hand");

        ShotContext swing = core; swing.sessionType = 0;
        check(cat.resolve(QStringLiteral("wristScore"), swing).state == MetricAvailability::Unavailable,
              "wristScore Unavailable in a Swing session");

        // swingScore is aspirational — no live scorer, always Unavailable.
        const MetricAvailability sw = cat.resolve(QStringLiteral("swingScore"), swing);
        check(sw.state == MetricAvailability::Unavailable, "swingScore Unavailable (no live scorer)");
        check(sw.reason.contains(QStringLiteral("scorer")), "swingScore reason names the missing scorer");
    }

    // 3c. resolve() — the newly-cataloged live producers (head-track, shaft-lean).
    {
        const ShotContext cam = wristShot({}, /*faceOn*/ true);
        check(cat.resolve(QStringLiteral("headSway"), cam).state == MetricAvailability::Measured,
              "headSway Measured on a Wrist shot with a face-on camera");
        const ShotContext noCam = wristShot({}, /*faceOn*/ false);
        check(cat.resolve(QStringLiteral("headSway"), noCam).state == MetricAvailability::Unavailable,
              "headSway Unavailable without a camera");

        const ShotContext club = wristShot({}, /*faceOn*/ true, /*club*/ true);
        check(cat.resolve(QStringLiteral("impactShaftLean"), club).state == MetricAvailability::Measured,
              "impactShaftLean Measured with face-on + club track");
        check(cat.descriptor(QStringLiteral("headSway"))->planned == false, "headSway not planned");
    }

    // 3d. Planned placeholders — declared, flagged, and always resolving 'planned'.
    {
        const char *planned[] = { "pelvisRotation", "thoraxRotation", "xFactor", "xFactorStretch",
                                  "hipInternalRotation", "spineForwardBend", "spineSideBend",
                                  "secondaryAxisTilt", "pelvisSway", "pelvisThrust", "pelvisLift",
                                  "swingPlane", "clubPath", "attackAngle", "faceAngle",
                                  "lowPointAhead", "tempoBackswing", "tempoRatio",
                                  "kinematicSequence", "swingScore",
                                  "shoulderAlignment", "elbowAlignment", "hipAlignment",
                                  "feetAlignment" };
        int flagged = 0, unavailable = 0;
        // Use a fully-capable context to prove these are gated by "no producer", not missing sensors.
        ShotContext capable = wristShot({ SegmentRole::Pelvis, SegmentRole::Thorax,
                                          SegmentRole::LeadForearm, SegmentRole::LeadHand,
                                          SegmentRole::LeadThigh, SegmentRole::TrailThigh },
                                        /*faceOn*/ true, /*club*/ true);
        capable.hasBallTrack = true;
        capable.tier = ReconstructionTier::ClubInstrumented;
        for (const char *k : planned) {
            const MetricDescriptor *d = cat.descriptor(QString::fromLatin1(k));
            if (d && d->planned) ++flagged;
            const MetricAvailability a = cat.resolve(QString::fromLatin1(k), capable);
            if (a.state == MetricAvailability::Unavailable) ++unavailable;
        }
        checkEqI(flagged, 24, "all 24 planned metrics carry .planned == true");
        checkEqI(unavailable, 24, "all 24 planned metrics resolve Unavailable even fully-equipped");
        check(cat.resolve(QStringLiteral("pelvisRotation"), capable).reason.contains(QStringLiteral("planned")),
              "planned reason says 'planned'");
    }

    // 4. resolve() — club-track / face-on gating (kinematics + foot).
    {
        ShotContext noClub = wristShot({}, /*faceOn*/ true, /*club*/ false);
        check(cat.resolve(QStringLiteral("clubheadSpeed"), noClub).state == MetricAvailability::Unavailable,
              "clubheadSpeed Unavailable without club track");

        ShotContext club = wristShot({}, /*faceOn*/ true, /*club*/ true);
        check(cat.resolve(QStringLiteral("clubheadSpeed"), club).state == MetricAvailability::Measured,
              "clubheadSpeed Measured with club track + face-on");
        check(cat.resolve(QStringLiteral("lagAngle"), club).state == MetricAvailability::Measured,
              "lagAngle Measured with club track + face-on pose");

        ShotContext clubNoCam = wristShot({}, /*faceOn*/ false, /*club*/ true);
        check(cat.resolve(QStringLiteral("lagAngle"), clubNoCam).state == MetricAvailability::Unavailable,
              "lagAngle Unavailable without face-on pose");

        ShotContext feet = wristShot({}, /*faceOn*/ true);
        check(cat.resolve(QStringLiteral("stanceWidth"), feet).state == MetricAvailability::Measured,
              "stanceWidth Measured with face-on camera");
        ShotContext noCam = wristShot({}, /*faceOn*/ false);
        check(cat.resolve(QStringLiteral("stanceWidth"), noCam).state == MetricAvailability::Unavailable,
              "stanceWidth Unavailable without face-on camera");
    }

    // 5. query availableOnly gates on the resolved context.
    {
        MetricQuery aq; aq.availableOnly = true;
        const ShotContext core = wristShot({ SegmentRole::LeadForearm, SegmentRole::LeadHand });
        const auto avail = cat.query(aq, &core);
        // forearm+hand, no camera, no club → bow/cup + hinge + wristScore + wristResemblance.
        checkEqI(static_cast<int>(avail.size()), 4, "availableOnly (forearm+hand only) → 4");
        check(cat.query(aq, nullptr).empty(), "availableOnly without ctx → empty");
    }

    // 6. corridor() — DOF delegation matches reference_bands; non-DOF + non-checkpoint → nullopt.
    {
        const auto dofC = cat.corridor(QStringLiteral("leadWristFlexExt"), Phase::Impact);
        check(dofC.has_value(), "corridor(bow/cup, Impact) has a value");

        const std::unique_ptr<IReferenceBandProvider> rb =
            makeReferenceBandProvider(BandProviderKind::Archetype);
        const Band b = rb->band(PpJointDof::LeadWristFlexExt, PpSwingPosition::P7);
        if (dofC && b.valid)
            check(dofC->greenLo == b.greenLo && dofC->greenHi == b.greenHi,
                  "corridor delegates to reference_bands (P7 green bounds match)");
        else
            check(false, "reference band for bow/cup @P7 is valid");

        check(!cat.corridor(QStringLiteral("clubheadSpeed"), Phase::Impact).has_value(),
              "corridor(clubheadSpeed) → nullopt (no DOF, no inline)");
        check(!cat.corridor(QStringLiteral("wristScore"), Phase::Impact).has_value(),
              "corridor(wristScore) → nullopt (Summary, no normative)");
        check(!cat.corridor(QStringLiteral("leadWristFlexExt"), Phase::Finish).has_value(),
              "corridor(bow/cup, Finish) → nullopt (not an assessment checkpoint)");
        check(!cat.corridor(QStringLiteral("nope"), Phase::Impact).has_value(),
              "corridor(unknown key) → nullopt");
    }

    std::printf("=== %s ===\n", g_fail == 0 ? "ALL PASS" : "FAILURES");
    return g_fail ? 1 : 0;
}
