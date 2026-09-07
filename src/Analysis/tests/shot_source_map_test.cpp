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

// shot_source_map.h — how a committed shot records the detector that found it.
//
// ⚠ THE ORACLES ARE ROUND-TRIP AND A FROZEN WIRE FORMAT, both stated in
// shot_controller.h: "THE ORDINALS ARE PERSISTED AND ONE OF THEM IS
// LOAD-BEARING. `Acoustic` is 4, and it is written into `swing.json` as
// `capture.shotSource`; `swing_reanalyzer` gates a microphone time-of-flight
// de-bias on `shotSource == 4`." A value that changes meaning here silently
// re-times swings that were captured years earlier, which is the one correction
// the protocol treats as never permissible.

#include "shot/shot_source_map.h"

#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const char *label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_fail;
}

static const char *srcName(ShotController::Source s)
{
    switch (s) {
    case ShotController::Source::Manual:   return "Manual";
    case ShotController::Source::Imu:      return "Imu";
    case ShotController::Source::Pose:     return "Pose";
    case ShotController::Source::Ball:     return "Ball";
    case ShotController::Source::Acoustic: return "Acoustic";
    case ShotController::Source::Ppcp:     return "Ppcp";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// W — the wire format is frozen
// ---------------------------------------------------------------------------

static void testWireFormat()
{
    std::printf("\nW — capture.shotSource ordinals are frozen\n");

    check("W1 Manual == 0",   static_cast<int>(ShotController::Source::Manual)   == 0);
    check("W2 Imu == 1",      static_cast<int>(ShotController::Source::Imu)      == 1);
    check("W3 Pose == 2",     static_cast<int>(ShotController::Source::Pose)     == 2);
    check("W4 Ball == 3",     static_cast<int>(ShotController::Source::Ball)     == 3);
    // The load-bearing one: swing_reanalyzer's mic time-of-flight de-bias.
    check("W5 Acoustic == 4 (swing_reanalyzer gates the mic de-bias on this)",
          static_cast<int>(ShotController::Source::Acoustic) == 4);
    check("W6 Ppcp == 5 (appended, so an arbitrated shot is not offered the de-bias)",
          static_cast<int>(ShotController::Source::Ppcp)     == 5);

    // The arbiter's own enum order IS its authority priority (shot_arbiter.h),
    // so it is equally load-bearing: Acoustic outranks Imu outranks Ball.
    check("W7 arbiter priority is Acoustic < Imu < Ball",
          static_cast<int>(ArbSource::Acoustic) < static_cast<int>(ArbSource::Imu) &&
          static_cast<int>(ArbSource::Imu)      < static_cast<int>(ArbSource::Ball));
}

// ---------------------------------------------------------------------------
// R — a detector's own commit must be recorded as that detector
// ---------------------------------------------------------------------------
//
// A source that goes into the arbiter and comes back out is the same shot found
// by the same detector, so the label the document records must be the label the
// detector reported. Anything else is provenance the swing did not earn.

static void testRoundTrip()
{
    std::printf("\nR — Source -> ArbSource -> Source is identity\n");

    const ShotController::Source arbitrated[] = {
        ShotController::Source::Acoustic,
        ShotController::Source::Imu,
        ShotController::Source::Ball,
        ShotController::Source::Pose,
    };

    for (ShotController::Source s : arbitrated) {
        ArbSource a{};
        char label[160];
        if (!toArbSource(s, a)) {
            std::snprintf(label, sizeof label,
                          "R %s enters the arbiter", srcName(s));
            check(label, false);
            continue;
        }
        const ShotController::Source back = fromArbSource(a);
        std::snprintf(label, sizeof label,
                      "R %s survives the arbiter (came back as %s)",
                      srcName(s), srcName(back));
        check(label, back == s);
    }
}

// ---------------------------------------------------------------------------
// M — modality membership
// ---------------------------------------------------------------------------

static void testModalityMembership()
{
    std::printf("\nM — which sources have a modality\n");

    ArbSource a{};
    check("M1 Manual has no modality — it commits directly",
          !toArbSource(ShotController::Source::Manual, a));
    check("M2 Ppcp has no modality — it was already arbitrated",
          !toArbSource(ShotController::Source::Ppcp, a));

    check("M3 Acoustic has one", toArbSource(ShotController::Source::Acoustic, a));
    check("M4 Imu has one",      toArbSource(ShotController::Source::Imu, a));
    check("M5 Ball has one",     toArbSource(ShotController::Source::Ball, a));
    check("M6 Pose has one",     toArbSource(ShotController::Source::Pose, a));

    // ⭐ Two detectors sharing one modality slot can never corroborate each
    // other: ShotArbiter keeps the highest-confidence candidate PER MODALITY,
    // so a pose detection and a ball detection of the same strike collapse to
    // one entry and the "two modalities agree" rule can never see them. The
    // arbiter's whole purpose is cross-modal agreement; a detector that cannot
    // participate in it is a detector that can only ever commit lone-strong.
    ArbSource pose{}, ball{};
    toArbSource(ShotController::Source::Pose, pose);
    toArbSource(ShotController::Source::Ball, ball);
    check("M7 Pose and Ball occupy different modality slots, so they can corroborate",
          pose != ball);

    // Every modality must name exactly one source, or the arbiter's committed
    // timestamp is attributed to a detector that did not produce it.
    check("M8 each modality maps back to a distinct source",
          fromArbSource(ArbSource::Acoustic) != fromArbSource(ArbSource::Imu) &&
          fromArbSource(ArbSource::Imu)      != fromArbSource(ArbSource::Ball) &&
          fromArbSource(ArbSource::Acoustic) != fromArbSource(ArbSource::Ball));

    // And no modality may map back to Manual — Manual never enters the arbiter,
    // so a shot labelled Manual on the way out is a shot with no provenance.
    check("M9 no modality maps back to Manual",
          fromArbSource(ArbSource::Acoustic) != ShotController::Source::Manual &&
          fromArbSource(ArbSource::Imu)      != ShotController::Source::Manual &&
          fromArbSource(ArbSource::Ball)     != ShotController::Source::Manual);
}

int main()
{
    std::printf("shot_source_map_test — shot provenance across the arbiter\n");
    testWireFormat();
    testRoundTrip();
    testModalityMembership();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
