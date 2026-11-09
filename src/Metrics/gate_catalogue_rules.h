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

// The gate catalogue's Rules entries (trust stage 6, docs/design/trust_stage6_plan.md). Included by
// gate_catalogue.h after GateEntry and the detail::add / detail::rule helpers; not a standalone header.

namespace pinpoint::gates::detail {

// THE ON-READ RULES (trust stage 6, Mark 16 Oct 2026): decisions made when a screen is read — a
// session minimum, a statistical test, a chart's sample-size floor — so they leave no per-swing record.
// Each is catalogued here with its explanation and the threshold it compares against (the literal the
// code holds, or the tuned constant; no value changes), and its code site carries
// `// gate rule: PP_GATE("id")` in the file named as producer. The diagnostics' signal refusals
// (characteristic_engine.cpp) are per swing but re-derived on every ledger build, so they are rules too.
inline void appendRules(std::vector<GateEntry> &c)
{
    const QString diag   = QStringLiteral("Session diagnostics");
    const QString charts = QStringLiteral("Charts");
    const QString lmArea = QStringLiteral("Launch monitor");

    const char *ledger    = "src/Coaching/diagnostic_ledger.h";
    const char *engine    = "src/Diagnostics/engine/characteristic_engine.cpp";
    const char *workOns   = "src/Coaching/work_ons.h";
    const char *themes    = "src/Coaching/swing_themes.h";
    const char *seqChecks = "src/Coaching/sequence_checks.h";
    const char *traces    = "src/Gui/charts/session_traces.cpp";
    const char *consistency = "src/Gui/charts/session_consistency.cpp";
    const char *consistencyGrades = "src/Gui/charts/consistency_grades.cpp";    // the table's grades

    const char *diagDesign   = "docs/design/session_diagnostics_design.md";
    const char *chartsDesign = "docs/design/charts_panel_traces_relations.md";
    const char *consistencyDesign = "docs/design/consistency_design.md";
    const char *lmDesign     = "docs/design/lm_panel_fsx.md";

    // ── Session diagnostics: the ledger (diagnostic_ledger.h §4 policy numbers) ──
    rule(c, "rule.diag.patternWilson", diag, "Not yet a pattern", "not yet a pattern",
         QStringLiteral("A fault becomes a pattern only when the cautious end of its firing rate — the lowest rate "
                        "the shots so far support — reaches 30%. A fault seen once or twice stays 'watching': "
                        "one bad swing is not a habit."), ledger, diagDesign)
        .tuning = { tune("ledger.patternWilsonLb", 0.30, ""), tune("ledger.wilsonZ", 1.96, "") };
    rule(c, "rule.diag.patternMinShots", diag, "Too few shots judged", "too few shots judged",
         QStringLiteral("No pattern is claimed until the fault could be judged on at least three shots. Below that "
                        "the rate is just a restatement of the last two swings."), ledger, diagDesign)
        .tuning = { tune("ledger.minAssessableForPattern", 3, "shots") };
    rule(c, "rule.diag.directionAgreement", diag, "No consistent direction", "no consistent direction",
         QStringLiteral("A fault is said to lean one way (too open, too much) only when at least 70% of the times "
                        "it fired agree on the direction. Otherwise it is shown as inconsistency, which is a "
                        "finding of its own."), ledger, diagDesign)
        .tuning = { tune("ledger.directionAgreementGate", 0.70, "") };
    rule(c, "rule.diag.trend", diag, "Too few shots for a trend", "no trend yet",
         QStringLiteral("An arrow saying a fault is getting better or worse needs at least five shots, and a trend "
                        "strong enough that it would rarely come from chance (5%). Shots are not independent, so "
                        "this is a bar to clear, not a proof."), ledger, diagDesign)
        .tuning = { tune("ledger.minPointsForTrend", 5, "shots"), tune("ledger.trendAlpha", 0.05, "") };
    rule(c, "rule.diag.dependence", diag, "Too few shots to link", "too few shots to link",
         QStringLiteral("Two faults are said to go together only after both could be judged on at least eight "
                        "shots, and the overlap would rarely happen by chance (5%). Fewer shots invite seeing a "
                        "link that is not there."), ledger, diagDesign)
        .tuning = { tune("ledger.minPairsForDependence", 8, "shots"), tune("ledger.fisherAlpha", 0.05, "") };
    rule(c, "rule.diag.debounce", diag, "Waiting for the picture to settle", "settling",
         QStringLiteral("The main-cause line stays empty until the set of patterns has not changed for three shots. "
                        "Better nothing than a line that changes its mind every swing."), ledger, diagDesign)
        .tuning = { tune("ledger.driverDebounceShots", 3, "shots") };
    rule(c, "rule.diag.rankBand", diag, "Order held steady", "order held",
         QStringLiteral("A card moves up or down the list only when its rank changes by more than one place, so "
                        "the list does not shuffle under the golfer from one shot to the next."), ledger, diagDesign)
        .tuning = { tune("ledger.rankBandWidth", 1, "places") };
    rule(c, "rule.diag.borderline", diag, "Near the edge", "near the edge",
         QStringLiteral("A shot is borderline when, allowing for how precisely it was measured, the fault had between "
                        "a 20% and 80% chance of firing — or the reading had more than a 20% risk of being badly "
                        "wrong. The panel says 'near the edge' rather than a firm yes or no."), ledger,
         "docs/design/session_diagnostics_design.md §A8")
        .tuning = { tune("diagnostics.uncertainty.borderLo", tuned::diagUncertainty::kBorderLo, ""),
                    tune("diagnostics.uncertainty.borderHi", tuned::diagUncertainty::kBorderHi, ""),
                    tune("diagnostics.uncertainty.borderGross", tuned::diagUncertainty::kBorderGross, "") };
    rule(c, "rule.diag.rootStability", diag, "How firm the cause is", "cause firmness",
         QStringLiteral("A main cause is 'firm' when it stays the cause in at least 80% of re-runs that shake each "
                        "reading within its measurement error, 'likely' from 50%, and 'fragile' below that."),
         "src/Diagnostics/engine/relation_resolver.cpp", "docs/design/session_diagnostics_design.md §A8.6")
        .tuning = { tune("diagnostics.uncertainty.firmShare", tuned::diagUncertainty::kFirmShare, ""),
                    tune("diagnostics.uncertainty.likelyShare", tuned::diagUncertainty::kLikelyShare, "") };

    // ── Session diagnostics: the signal refusals (per swing, re-derived on every ledger build) ──
    rule(c, "rule.diag.noCorridor", diag, "No range to judge against", "no range",
         QStringLiteral("A fault judged against the normal range for the club can only be judged when that range "
                        "exists. Without one the check is not passed — it is not made."), engine, diagDesign);
    rule(c, "rule.diag.implausible", diag, "Reading not believable", "reading not believable",
         QStringLiteral("A reading far outside anything a golfer produces is a capture fault, not a swing fault. The "
                        "check is marked not judged rather than passed, so a bad capture never looks like a clean "
                        "swing."), engine, diagDesign);
    rule(c, "rule.diag.peakAfterImpact", diag, "Hips still speeding up", "hips peak after impact",
         QStringLiteral("Hip braking compares the hips' speed at impact with their peak. When the peak comes after "
                        "impact the hips are still speeding up through the ball, so 'they slowed' cannot be read. "
                        "That lateness is reported by its own check."), engine,
         "docs/design/trunk_imu_sequence_grf_design.md");
    rule(c, "rule.diag.nonPositiveDenominator", diag, "Ratio cannot be formed", "no ratio",
         QStringLiteral("A ratio check divides one reading by another that must be positive. A zero or negative "
                        "divisor would turn 'more than' into 'less than', so the check is not made."), engine,
         "docs/design/trunk_imu_sequence_grf_design.md §10.7");
    rule(c, "rule.diag.mixedScale", diag, "Readings on different scales", "mixed scales",
         QStringLiteral("A ratio of two readings is only meaningful when both come from the same kind of "
                        "measurement. Dividing a camera reading by a sensor reading mixes two scales, so the check "
                        "is not made."), engine, "docs/design/trunk_imu_sequence_grf_design.md §10.7");
    rule(c, "rule.diag.compositeCoverage", diag, "Too few parts read", "too few parts",
         QStringLiteral("A finding about the posture as a whole is read from several readings together. When the "
                        "cameras saw too few of them — less than half of what the finding weighs, or not the one "
                        "it cannot do without — the check is not made. A part that was not seen is never counted "
                        "as fine."), engine, "docs/design/posture_metrics_design.md §4.6");
    rule(c, "rule.diag.clubBaselineMinShots", diag, "Not enough shots with this club",
         "not enough shots with this club yet",
         QStringLiteral("Short carry and lost ball speed are judged against this golfer's own usual numbers with "
                        "this club, from earlier sessions. With too few earlier shots there is no usual to judge "
                        "against, so the check is not made; a topped or duffed shot is still caught by its "
                        "fixed floor."),
         "src/Diagnostics/measures/live_measure_source.cpp", "docs/design/shot_outcomes_design.md §3.3");
    rule(c, "rule.diag.inferredContext", diag, "Club assumed", "club assumed",
         QStringLiteral("When a shot has no club set, its readings are judged against the default club's range and "
                        "trusted less, so a finding is kept but marked: the deviation is real, the range it was "
                        "judged against may not be."), engine, diagDesign)
        .tuning = { tune("diagnostics.inferredContextConfidence", 0.7, "") };

    // ── Session diagnostics: work-ons ──
    rule(c, "rule.diag.workOnLimits", diag, "How many work-ons", "work-on count",
         QStringLiteral("A session names at most five things to work on, and tops the list up to three where the "
                        "evidence allows. More than five is a list nobody works through."), workOns,
         "docs/design/work_ons_design.md")
        .tuning = { tune("workOns.maxPerSession", 5, ""), tune("workOns.minPerSession", 3, "") };
    rule(c, "rule.diag.workOnQuiet", diag, "Quiet this session", "quiet",
         QStringLiteral("A work-on counts as quiet in a session only when it could be judged on at least three shots "
                        "and, at the generous end of what those shots support, it fired on under half of them."),
         workOns, "docs/design/work_ons_design.md")
        .tuning = { tune("workOns.quietWilsonUb", 0.50, ""), tune("ledger.minAssessableForPattern", 3, "shots") };
    rule(c, "rule.diag.workOnClear", diag, "Cleared", "cleared",
         QStringLiteral("A work-on leaves the list after two sessions in a row in which it was judged and stayed "
                        "quiet. One quiet session is 'easing', not fixed."), workOns, "docs/design/work_ons_design.md")
        .tuning = { tune("workOns.clearAfterQuiet", 2, "sessions") };

    // ── Session diagnostics: swing themes ──
    rule(c, "rule.diag.themesMin", diag, "Not enough swings for themes", "too few swings",
         QStringLiteral("Themes — the things that move together across your swings — need at least 60 swings over "
                        "three sessions. Fewer, and what looks like a theme is one day's swings."), themes,
         "docs/design/home_themes_design.md")
        .tuning = { tune("themes.minSwings", 60, "swings"), tune("themes.minSessions", 3, "sessions") };
    rule(c, "rule.diag.themeSessionJudged", diag, "Session judged too few swings", "too few swings judged",
         QStringLiteral("A session counts toward a fault's history only when it judged that fault on at least eight "
                        "swings."), themes, "docs/design/home_themes_design.md")
        .tuning = { tune("themes.minAssessed", 8, "swings") };
    rule(c, "rule.diag.themeCoverage", diag, "Read on too few swings", "read on too few swings",
         QStringLiteral("A reading takes part in the themes only when it was measured on at least 60% of the swings, "
                        "and a swing only when it has at least half of those readings; two readings are compared "
                        "only over ten or more swings that have both."), themes, "docs/design/home_themes_design.md")
        .tuning = { tune("themes.minColumnCoverage", 0.6, ""), tune("themes.minRowCoverage", 0.5, ""),
                    tune("themes.minPairs", 10, "swings") };

    // ── Session diagnostics: the sequence across a session ──
    rule(c, "rule.diag.sequenceSessionMin", diag, "Too few swings for the sequence", "too few swings",
         QStringLiteral("How often the body fires in order, and how steady its timing is, are said for a session only "
                        "once at least five swings had all four peaks placed."), seqChecks,
         "docs/design/kinematic_sequence_design.md")
        .tuning = { tune("sequence.sessionMinSwings", tuned::sequence::kSessionMinSwings, "swings") };
    rule(c, "rule.diag.sequenceOrderShare", diag, "In-order share, cautiously", "in-order share",
         QStringLiteral("The share of swings in order is stated at the cautious end of what the swings support, so a "
                        "few swings in order do not read as 'always in order'."), seqChecks,
         "docs/design/kinematic_sequence_design.md")
        .tuning = { tune("ledger.wilsonZ", 1.96, "") };

    // ── Charts ──
    rule(c, "rule.charts.groupMin", charts, "Too few shots for a band", "too few shots",
         QStringLiteral("A typical curve and its band are drawn for a group only when it has at least three shots. "
                        "With fewer, the band is just the shots themselves."), traces, chartsDesign)
        .tuning = { tune("charts.minShots", 3, "shots") };
    rule(c, "rule.charts.instantMin", charts, "Too thin here", "too thin here",
         QStringLiteral("At each instant the band is drawn only where at least three shots and eight samples were "
                        "measured. Where the cameras lost sight on most shots the band stops rather than guess."),
         traces, chartsDesign)
        .tuning = { tune("charts.minShots", 3, "shots"), tune("charts.minSamples", 8, "samples") };
    rule(c, "rule.charts.kindsMin", charts, "Too few shots for kinds", "too few shots",
         QStringLiteral("Splitting a session into two kinds of swing needs at least six shots. Fewer, and any split "
                        "is noise."), traces, chartsDesign)
        .tuning = { tune("charts.kindsMinShots", 6, "shots") };
    rule(c, "rule.charts.leadToWatch", charts, "A lead to watch", "a lead to watch",
         QStringLiteral("When one of two groups being compared has fewer than five shots, a difference between them is "
                        "offered as a lead to watch, not a finding."), "src/Gui/charts/charts_traces_presenter.cpp",
         chartsDesign)
        .tuning = { tune("charts.findingMinShots", 5, "shots") };
    rule(c, "rule.charts.correlationMin", charts, "Too few shots to relate", "too few shots",
         QStringLiteral("Whether two readings move together is worked out only over five or more shots that have "
                        "both."), "src/Gui/charts/session_relations.cpp", chartsDesign)
        .tuning = { tune("charts.correlationMinShots", 5, "shots") };
    rule(c, "rule.charts.boxMin", charts, "Too few shots for a spread", "too few shots",
         QStringLiteral("A reading's spread is drawn as a box only with at least three shots."),
         "src/Gui/charts/session_relations.cpp", chartsDesign)
        .tuning = { tune("charts.boxMinShots", 3, "shots") };
    rule(c, "rule.charts.kindsAsClubs", charts, "The kinds are the clubs", "kinds are clubs",
         QStringLiteral("When the two kinds of swing in a session line up with two clubs — at least 75% of shots with "
                        "their kind's usual club — they are named as the clubs, not as kinds of swing ('mostly' below "
                        "90%)."), "src/Gui/charts/charts_relations_presenter.cpp", chartsDesign)
        .tuning = { tune("charts.clubAgreement", 0.75, ""), tune("charts.clubAgreementClean", 0.90, "") };
    rule(c, "rule.charts.warpAnchors", charts, "Position not used to line up", "not used to line up",
         QStringLiteral("Shots are lined up on their swing positions. A position is not used when it was found with "
                        "low confidence (under 0.35), runs backwards in time, or makes a stretch more than twice or "
                        "less than half the session's usual length (judged once three shots have it)."),
         "src/Gui/charts/session_warp.cpp", chartsDesign)
        .tuning = { tune("charts.warp.minConf", 0.35, ""), tune("charts.warp.maxSegmentRatio", 2.0, ""),
                    tune("charts.warp.minSegmentShots", 3, "shots") };
    // The Consistency tab's rules (src/Gui/charts/session_consistency.h; the grades, and the first three gates, are src/Gui/charts/consistency_grades.h
    // over src/Metrics/variability/, whose constants — kNMin, kCellLoosestPercentile — they quote). A reading the grader
    // cannot grade is one of two cases, told apart: too few shots (grade 0, consistencyVerdict) or nothing to set the
    // spread against (no mark, consistencyNoScale).
    rule(c, "rule.charts.consistencyVerdict", charts, "Spread not judged", "too few shots",
         QStringLiteral("How much a reading moves from shot to shot is graded by the variability grader. Each shot is "
                        "measured from the middle of its own club's shots, a club with fewer than three shots left out, "
                        "and the spread of what remains is set against the reading's corridor: half the width of its "
                        "norm, else the measure's own variability scale. The ratio of the two gives one of five grades. "
                        "With fewer than eight shots the reading takes grade 0. A spread no bigger than the reading's "
                        "own measurement error keeps its grade and is marked measurement-limited."),
         consistencyGrades, consistencyDesign)
        .tuning = { tune("variability.nMin", 8, "shots"), tune("charts.consistency.minClubShots", 3, "shots") };
    rule(c, "rule.charts.consistencyNoScale", charts, "Nothing to judge the spread against", "no scale",
         QStringLiteral("A reading whose norm has no corridor, and whose measure has no variability scale of its own, "
                        "is not graded at all, however many shots there are: there is nothing to set its spread against. "
                        "A measure with a true zero (a speed, a distance) is no exception: its spread is never taken as "
                        "a fraction of its mean. It has no consistency mark, and its spread is shown as a statistic."),
         consistencyGrades, consistencyDesign);
    rule(c, "rule.charts.consistencyCell", charts, "Nothing to judge here", "nothing to judge",
         QStringLiteral("A cell of the table takes the grade of its most varying third: the 67th-percentile ratio of "
                        "spread to corridor among its graded readings, by nearest rank. When none of them has a ratio "
                        "but one took grade 0, the cell takes grade 0. With readings but none graded it shows \"-\"."),
         consistencyGrades, consistencyDesign)
        .tuning = { tune("variability.cellLoosestPercentile", 0.67, "") };
    rule(c, "rule.charts.consistencyStandOut", charts, "Did not stand out", "did not stand out",
         QStringLiteral("Every shot is measured from the middle of its own club's shots. One shot then stands out on a "
                        "reading only against at least six other shots of any club, itself left out, when it sits at "
                        "least three and a half times their usual scatter from their middle at every point of the "
                        "span, and past twice the reading's own measurement error."),
         consistency, consistencyDesign)
        .tuning = { tune("charts.consistency.minOthers", 6, "shots"), tune("charts.consistency.standOutZ", 3.5, ""),
                    tune("charts.consistency.standOutSigma", 2.0, "") };
    rule(c, "rule.charts.consistencyBallFlag", charts, "Too few shots to flag the ball", "too few shots",
         QStringLiteral("A shot is flagged short or offline against the same club's shots that session, when it falls "
                        "well beyond the middle half of them, by more than one and a half times that middle half's "
                        "width, and only with at least eight shots of that club with the reading."),
         consistency, consistencyDesign)
        .tuning = { tune("charts.consistency.minBallShots", 8, "shots"), tune("charts.consistency.fence", 1.5, "") };
    rule(c, "rule.charts.consistencyLadderGuard", charts, "Timing of the positions looks unusual", "unusual timing",
         QStringLiteral("A shot that was not lined up on its swing positions, kept fewer than four of them from address "
                        "to impact, or lost one of those for running backwards or making a stretch out of all "
                        "proportion, keeps its readings with a warning, but its spans and backswing timing do not "
                        "count as standing out. A lost follow-through position does not count, and a shot with no "
                        "curves has nothing to mis-time."),
         consistency, consistencyDesign)
        .tuning = { tune("charts.consistency.minKeptAnchors", 4, "positions") };

    // ── Launch monitor: the session board ──
    rule(c, "rule.lm.spreadMin", lmArea, "Too few shots for a spread", "too few shots",
         QStringLiteral("A spread (how much the shots vary) is shown only from three shots. Two shots give a number "
                        "that means nothing."), "src/LaunchMonitor/lm_session_reductions.h", lmDesign)
        .tuning = { tune("lm.minShotsForSpread", 3, "shots") };
    rule(c, "rule.lm.clubVotes", lmArea, "Club undecided", "club undecided",
         QStringLiteral("The numbers are said to read like a wood or an iron only when at least two of the signs "
                        "(smash, spin, launch) agree, and more of them point that way than the other."),
         "src/LaunchMonitor/lm_club_evidence.h", lmDesign)
        .tuning = { tune("lm.clubVotesToDecide", 2, "") };
    rule(c, "rule.lm.flightShape", lmArea, "No flight shape", "no flight shape",
         QStringLiteral("A shot shape (push-draw, pull-fade …) needs the start direction and something that speaks to "
                        "the curve — the spin axis, or failing that face to path. Half a shot stated confidently is "
                        "worse than none."), "src/LaunchMonitor/lm_inferred_reads.h", lmDesign)
        .tuning = { tune("lm.startT", 1.0, "°"), tune("lm.curveT", 2.0, "°") };
    rule(c, "rule.lm.strikeQuality", lmArea, "No strike read", "no strike read",
         QStringLiteral("Where the ball was struck on the face is named only when the device measured both the "
                        "heel-to-toe and the low-to-high position."), "src/LaunchMonitor/lm_inferred_reads.h", lmDesign)
        .tuning = { tune("lm.centreMm", 3.0, "mm") };
}

} // namespace pinpoint::gates::detail
