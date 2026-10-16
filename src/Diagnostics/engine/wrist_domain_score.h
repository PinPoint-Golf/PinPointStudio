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

#include "../../Core/pp_tuned_constants.h"   // rules::kScoreScale, kSeverityWeight* — the engine's own

#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

// THE WRIST SCORE, COMPUTED OVER THE WRIST CONDITIONS — so the number and the findings beside it can
// never disagree (docs/design/wrist_motion_into_charts.md §7.3: the "84, good, with four points out
// of band" problem). The wrist engine's 0–100 came from its own rule penalties; its rules have left
// the UI and the diagnostics conditions in the wrist domain (Condition::domain == "wrist") are the
// one judge. This scores THEIR verdicts with the engine's arithmetic and the engine's weights:
//
//     penalty = severity weight × rule weight × rules::kScoreScale        (engine: score v2)
//     score   = clamp(round(100 − Σ penalty), 0, 100)
//
// SEVERITY FROM THE TIER. A "pattern" (the ledger's Wilson gate, diagnostic_ledger.h
// conditionTier()) weighs as the engine's Fault (kSeverityWeightFault); "watching", or a firing on
// this shot that the session has not yet made a pattern of, weighs as its Watch
// (kSeverityWeightWatch); "clean" and not fired costs nothing. CONFIDENCE DOES NOT MOVE THE CENTRAL
// VALUE, exactly as the engine refused it (validation B2: a severity × confidence term let a noisier
// swing score higher); it rides along for a surface that draws the interval.
//
// PASS ONLY WHAT WAS ASSESSED. A condition that could not be read on this shot or session (no
// instrument, a placeholder corridor's sibling unavailable) is not a verdict and must not be passed:
// it would score as clean. With nothing passed the score is −1, "not assessable", never 100.
//
// Pure, header-only (no Qt GUI types, no link-line change for any consumer), unit-tested by
// wrist_domain_score_test, which also holds the weight map below to the engine's rule table.

namespace pinpoint::analysis {

struct WristDomainVerdict {
    QString conditionId;
    QString label;
    QString tier;               // "clean" | "watching" | "pattern"
    double  confidence = 1.0;   // carried, never a factor — see above
    bool    fired = false;      // fired on the shot being scored (a shot-scope caller)
};

struct WristDomainContribution {
    QString id;                 // the condition id
    QString label;
    double  penalty = 0;
};

struct WristDomainScoreResult {
    int     score = -1;         // -1 = not assessable (no wrist condition read)
    QString band;               // "good"|"watch"|"weak"|"fault" — WristDiagnosticsModel::scoreBand's cut-offs
    int     base = 100;
    std::vector<WristDomainContribution> contributions;
};

// ── The weight map: engine rule → condition ─────────────────────────────────
//
// The engine's weights (src/Wrist/assessment_rules.cpp, RuleDef::weight) carried to the condition
// each rule folded into (§7.3). Eight conditions are a rule's direct heir; the other seven have no
// engine rule of their own and inherit by ONE of two stated rules, never by a number made up here:
//
//   AXIS PARTNER — the other tail of a corridor the engine judged on one side only takes that
//   side's weight: bowed_lead_wrist ← scooping (flip), excessive_lag ← casting (cast),
//   over_set ← insufficient_set.
//
//   FACE SIDE — the forearm-roll pairs at P2 and P4 take the engine's forearm rules by which way the
//   face goes: the open-face tail ← holding_off (0.6), the shut-face tail ← over_rotation (0.7).
//
//   condition                engine rule (id)        weight   how
//   cupped_at_top            open_face_top            0.8     direct (renamed: the cup reading)
//   bowed_at_top             closed_face_top          0.6     direct
//   scooping                 flip                     0.9     direct
//   bowed_lead_wrist         flip                     0.9     axis partner of scooping
//   casting                  cast                     1.0     direct (hinge at P6 is its 1st signal)
//   excessive_lag            cast                     1.0     axis partner of casting
//   insufficient_set         insufficient_set         0.5     direct
//   over_set                 insufficient_set         0.5     axis partner
//   flipping                 over_rotation            0.7     direct (face rotated shut at impact)
//   face_held_open_impact    holding_off              0.6     direct
//   open_face_top            holding_off              0.6     face side: open
//   shut_face_top            over_rotation            0.7     face side: shut
//   early_face_roll          holding_off              0.6     face side: open
//   face_held_shut_takeaway  over_rotation            0.7     face side: shut
//   chicken_wing             chicken_wing             0.6     direct
//
// A wrist-domain condition missing from this table (a later addition to the domain) weighs
// kWristDomainDefaultWeight, the engine's modal rule weight, until somebody writes its row; the
// test lists the shipped domain against this table so that cannot happen silently.
struct WristDomainWeight {
    const char *conditionId;
    const char *engineRuleId;
    double      weight;
};

inline constexpr double kWristDomainDefaultWeight = 0.6;

inline const std::vector<WristDomainWeight> &wristDomainWeights()
{
    static const std::vector<WristDomainWeight> k = {
        { "cupped_at_top",           "open_face_top",    0.8 },
        { "bowed_at_top",            "closed_face_top",  0.6 },
        { "scooping",                "flip",             0.9 },
        { "bowed_lead_wrist",        "flip",             0.9 },
        { "casting",                 "cast",             1.0 },
        { "excessive_lag",           "cast",             1.0 },
        { "insufficient_set",        "insufficient_set", 0.5 },
        { "over_set",                "insufficient_set", 0.5 },
        { "flipping",                "over_rotation",    0.7 },
        { "face_held_open_impact",   "holding_off",      0.6 },
        { "open_face_top",           "holding_off",      0.6 },
        { "shut_face_top",           "over_rotation",    0.7 },
        { "early_face_roll",         "holding_off",      0.6 },
        { "face_held_shut_takeaway", "over_rotation",    0.7 },
        { "chicken_wing",            "chicken_wing",     0.6 },
    };
    return k;
}

inline double wristDomainWeight(const QString &conditionId)
{
    for (const WristDomainWeight &w : wristDomainWeights())
        if (conditionId == QLatin1String(w.conditionId)) return w.weight;
    return kWristDomainDefaultWeight;
}

// The engine's band cut-offs (WristDiagnosticsModel::scoreBand), so one number reads one way.
inline QString wristDomainBand(int score)
{
    if (score < 0)   return QString();
    if (score >= 75) return QStringLiteral("good");
    if (score >= 50) return QStringLiteral("watch");
    if (score >= 25) return QStringLiteral("weak");
    return QStringLiteral("fault");
}

inline WristDomainScoreResult scoreWristDomain(const std::vector<WristDomainVerdict> &verdicts)
{
    namespace tun = pinpoint::tuned::rules;
    WristDomainScoreResult out;
    if (verdicts.empty())
        return out;                                   // -1, no band: nothing was read

    double penalty = 0.0;
    for (const WristDomainVerdict &v : verdicts) {
        double sevW = 0.0;
        if (v.tier == QLatin1String("pattern"))                   sevW = tun::kSeverityWeightFault;
        else if (v.tier == QLatin1String("watching") || v.fired)  sevW = tun::kSeverityWeightWatch;
        if (sevW <= 0.0)
            continue;
        const double pen = sevW * wristDomainWeight(v.conditionId) * tun::kScoreScale;
        penalty += pen;
        out.contributions.push_back({ v.conditionId, v.label.isEmpty() ? v.conditionId : v.label, pen });
    }
    // Largest first, so a surface listing "what it cost" reads in order; ties keep input order.
    std::stable_sort(out.contributions.begin(), out.contributions.end(),
                     [](const WristDomainContribution &a, const WristDomainContribution &b) {
                         return a.penalty > b.penalty;
                     });
    out.score = std::clamp(static_cast<int>(std::lround(out.base - penalty)), 0, 100);
    out.band  = wristDomainBand(out.score);
    return out;
}

} // namespace pinpoint::analysis
