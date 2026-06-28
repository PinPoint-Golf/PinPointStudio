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

#include "assessment_rules.h"

#include <algorithm>

namespace pinpoint::analysis {

using Sev = PpFindingSeverity;
using Out = std::optional<std::pair<Sev, double>>;

namespace {

constexpr auto FE  = PpJointDof::LeadWristFlexExt;   // + bowed / − cupped
constexpr auto RU  = PpJointDof::LeadWristRadUln;    // + ulnar (set/lag)
constexpr auto FA  = PpJointDof::LeadForearmRot;     // + pronation / − supination
constexpr auto EL  = PpJointDof::LeadElbowFlex;      // + flexion magnitude
constexpr auto TW  = PpJointDof::TrailWristFlexExt;  // trail "tray" extension (corroboration)

constexpr auto P2 = PpSwingPosition::P2;
constexpr auto P3 = PpSwingPosition::P3;
constexpr auto P4 = PpSwingPosition::P4;
constexpr auto P5 = PpSwingPosition::P5;
constexpr auto P6 = PpSwingPosition::P6;
constexpr auto P7 = PpSwingPosition::P7;
constexpr auto P8 = PpSwingPosition::P8;

// How far Δ sits below / above its green corridor at a cell (0 when inside / unbanded).
double below(const RuleContext &c, PpJointDof d, PpSwingPosition p)
{
    if (!c.banded(d, p)) return 0.0;
    return std::max(0.0, c.cell(d, p).bandLo - c.delta(d, p));
}
double above(const RuleContext &c, PpJointDof d, PpSwingPosition p)
{
    if (!c.banded(d, p)) return 0.0;
    return std::max(0.0, c.delta(d, p) - c.cell(d, p).bandHi);
}
bool allGreen(const RuleContext &c, PpJointDof d, std::initializer_list<PpSwingPosition> ps)
{
    if (!c.present(d)) return false;
    for (PpSwingPosition p : ps)
        if (!c.ok(d, p) || c.rag(d, p) != PpRag::Green) return false;
    return true;
}

// ---- the default rule set (design §7.3) -------------------------------------------------------

std::vector<RuleDef> defaultRuleDefs()
{
    std::vector<RuleDef> r;

    // F1 — Open face at top: lead wrist cupped at P4 (Δ < −15° cup).
    r.push_back({ QStringLiteral("open_face_top"), QStringLiteral("Open face at top"),
        QStringLiteral("face"), { FE }, { P4 }, { QStringLiteral("slice"), QStringLiteral("pull") },
        QStringLiteral("The lead wrist is cupped at the top, opening the face."),
        QStringLiteral("Flatten or bow the lead wrist at the top to square the face."), QString(),
        0.85f, 0.8, [](const RuleContext &c) -> Out {
            // Below the (archetype-aware) face corridor at the top = cupped / open face.
            if (!c.ok(FE, P4) || !c.banded(FE, P4)) return std::nullopt;
            const double mag = below(c, FE, P4);
            if (mag <= 0.0) return std::nullopt;
            return std::make_pair(c.rag(FE, P4) == PpRag::Red ? Sev::Fault : Sev::Watch, mag); } });

    // F2 — Closed face at top: bowed past the corridor at P4 (style-dependent — archetype-aware).
    r.push_back({ QStringLiteral("closed_face_top"), QStringLiteral("Closed face at top"),
        QStringLiteral("face"), { FE }, { P4 }, { QStringLiteral("hook"), QStringLiteral("block") },
        QStringLiteral("The lead wrist is strongly bowed at the top, closing the face."),
        QStringLiteral("Reduce the bow or match the release to it."), QString(),
        0.70f, 0.6, [](const RuleContext &c) -> Out {
            // Above the (archetype-aware) face corridor at the top = bowed / closed face.
            if (!c.ok(FE, P4) || !c.banded(FE, P4)) return std::nullopt;
            const double mag = above(c, FE, P4);
            if (mag <= 0.0) return std::nullopt;
            return std::make_pair(c.rag(FE, P4) == PpRag::Red ? Sev::Fault : Sev::Watch, mag); } });

    // F3 — Flip / scoop at impact: lead wrist adds extension P6→P7 (Δ falls).
    r.push_back({ QStringLiteral("flip"), QStringLiteral("Flip / scoop at impact"),
        QStringLiteral("strike"), { FE }, { P6, P7 },
        { QStringLiteral("thin"), QStringLiteral("weak / high"), QStringLiteral("distance loss") },
        QStringLiteral("Lead-wrist extension increases into impact, adding loft and passing the hands."),
        QStringLiteral("Cover the ball with a flexed lead wrist; keep the handle leading."), QString(),
        0.85f, 0.9, [](const RuleContext &c) -> Out {
            if (!c.ok(FE, P6) || !c.ok(FE, P7)) return std::nullopt;
            // Δ falls into impact = extension added. A good swing releases a few degrees; flag only a
            // clear extension event (a mild P6→P7 drop is normal lead-wrist release, not a flip).
            const double der = c.delta(FE, P7) - c.delta(FE, P6);
            if (der < c.tuning.flipFaultDeg) return std::make_pair(Sev::Fault, -der);
            if (der < c.tuning.flipWatchDeg) return std::make_pair(Sev::Watch, -der);
            return std::nullopt; } });

    // F4 — Casting / early release: lead-wrist radial set dumped below corridor by P6.
    r.push_back({ QStringLiteral("cast"), QStringLiteral("Early release (cast)"),
        QStringLiteral("power"), { RU }, { P5, P6 },
        { QStringLiteral("distance loss"), QStringLiteral("fat / thin") },
        QStringLiteral("The radial set collapses before P6 — the lag is spent early."),
        QStringLiteral("Retain the trail-wrist bend and release the angle late, not from the top."), QString(),
        0.85f, 1.0, [](const RuleContext &c) -> Out {
            if (!c.ok(RU, P6) || !c.banded(RU, P6)) return std::nullopt;
            const double mag = below(c, RU, P6);
            if (mag <= 0.0) return std::nullopt;
            return std::make_pair(c.rag(RU, P6) == PpRag::Red ? Sev::Fault : Sev::Watch, mag); } });

    // F5 — Insufficient set: under-cocked radial deviation at the top.
    r.push_back({ QStringLiteral("insufficient_set"), QStringLiteral("Insufficient set"),
        QStringLiteral("power"), { RU }, { P4 },
        { QStringLiteral("narrow / short") },
        QStringLiteral("The wrist is under-set at the top — little stored angle to release."),
        QStringLiteral("Set the club earlier and fuller into the top."), QString(),
        0.70f, 0.5, [](const RuleContext &c) -> Out {
            if (!c.ok(RU, P4) || !c.banded(RU, P4)) return std::nullopt;
            const double mag = below(c, RU, P4);
            if (mag <= 0.0) return std::nullopt;
            return std::make_pair(c.rag(RU, P4) == PpRag::Red ? Sev::Fault : Sev::Watch, mag); } });

    // F6 — Over-rotation / over-release: forearm pronates past the corridor through P8.
    r.push_back({ QStringLiteral("over_rotation"), QStringLiteral("Over-rotation"),
        QStringLiteral("face"), { FA }, { P6, P7, P8 },
        { QStringLiteral("hook"), QStringLiteral("two-way miss") },
        QStringLiteral("The lead forearm rotates toward pronation too far through release."),
        QStringLiteral("Quieter hands — let the body lead the rotation."), QString(),
        0.75f, 0.7, [](const RuleContext &c) -> Out {
            if (!c.ok(FA, P8) || !c.banded(FA, P8)) return std::nullopt;
            const double mag = above(c, FA, P8);
            if (mag <= 0.0) return std::nullopt;
            return std::make_pair(c.rag(FA, P8) == PpRag::Red ? Sev::Fault : Sev::Watch, mag); } });

    // F7 — Holding off / blocked release: forearm stays supinated below the corridor.
    r.push_back({ QStringLiteral("holding_off"), QStringLiteral("Holding off"),
        QStringLiteral("face"), { FA }, { P7, P8 },
        { QStringLiteral("push"), QStringLiteral("weak fade") },
        QStringLiteral("The lead forearm stays supinated through impact — the face is held open."),
        QStringLiteral("Let the lead forearm rotate and the face release through the ball."), QString(),
        0.75f, 0.6, [](const RuleContext &c) -> Out {
            if (!c.ok(FA, P8) || !c.banded(FA, P8)) return std::nullopt;
            const double mag = below(c, FA, P8);
            if (mag <= 0.0) return std::nullopt;
            return std::make_pair(c.rag(FA, P8) == PpRag::Red ? Sev::Fault : Sev::Watch, mag); } });

    // F8 — Chicken wing (reduced — no shoulder producer): lead elbow folds past the corridor at P8,
    // corroborated by lead-forearm under-rotation. Lower base confidence; needs the elbow DOF.
    r.push_back({ QStringLiteral("chicken_wing"), QStringLiteral("Chicken wing"),
        QStringLiteral("structure"), { EL, FA }, { P7, P8 },
        { QStringLiteral("weak / high"), QStringLiteral("pull-slice") },
        QStringLiteral("The lead arm folds instead of extending and rotating through impact."),
        QStringLiteral("Extend the lead arm and rotate through; hold the width."), QString(),
        0.55f, 0.6, [](const RuleContext &c) -> Out {
            if (!c.present(EL) || !c.ok(EL, P8) || !c.banded(EL, P8)) return std::nullopt;
            const double mag = above(c, EL, P8);
            if (mag <= 0.0) return std::nullopt;
            const bool forearmUnder = c.ok(FA, P8) && c.banded(FA, P8) && below(c, FA, P8) > 0.0;
            return std::make_pair(forearmUnder ? Sev::Fault : Sev::Watch, mag); } });

    // --- Strengths (match-good rules, severity = Good; weight 0 → no penalty) ------------------

    r.push_back({ QStringLiteral("set"), QStringLiteral("Wrist set is well loaded"),
        QStringLiteral("power"), { RU }, { P2, P3, P4 }, {},
        QStringLiteral("The radial set builds smoothly and is fully loaded by the top."), QString(),
        QStringLiteral("Keep your takeaway and set exactly as they are — the fix is holding the angle later, not changing how you build it."),
        0.80f, 0.0, [](const RuleContext &c) -> Out {
            return allGreen(c, RU, { P2, P3, P4 }) ? std::make_optional(std::make_pair(Sev::Good, 0.0))
                                                   : std::nullopt; } });

    r.push_back({ QStringLiteral("face_rotation"), QStringLiteral("Face rotation is on schedule"),
        QStringLiteral("face"), { FA }, { P4, P5, P6 }, {},
        QStringLiteral("Forearm rotation squares the face on a good schedule through transition."), QString(),
        QStringLiteral("Don't add hand rotation to square the face — it's arriving on time."),
        0.75f, 0.0, [](const RuleContext &c) -> Out {
            return allGreen(c, FA, { P4, P5, P6 }) ? std::make_optional(std::make_pair(Sev::Good, 0.0))
                                                   : std::nullopt; } });

    r.push_back({ QStringLiteral("width"), QStringLiteral("Lead arm holds its width"),
        QStringLiteral("structure"), { EL }, { P5, P6, P7 }, {},
        QStringLiteral("The lead arm keeps its structure into impact — no early chicken-wing."), QString(),
        QStringLiteral("Keep extending through the ball; hold this width rather than letting the lead elbow fold."),
        0.70f, 0.0, [](const RuleContext &c) -> Out {
            return allGreen(c, EL, { P5, P6, P7 }) ? std::make_optional(std::make_pair(Sev::Good, 0.0))
                                                   : std::nullopt; } });

    return r;
}

// Mean of the input cells' (already source- and segmentation-aware) confidence.
float meanCellConf(const RuleContext &c, const PpWristFinding &f)
{
    double sum = 0.0;
    int n = 0;
    for (PpJointDof d : f.dofs)
        for (PpSwingPosition p : f.positions)
            if (c.ok(d, p)) { sum += c.cell(d, p).confidence; ++n; }
    return n ? static_cast<float>(sum / n) : 0.6f;
}

PpWristFinding *findById(std::vector<PpWristFinding> &v, const QString &id)
{
    for (PpWristFinding &f : v)
        if (f.id == id) return &f;
    return nullptr;
}

// Trail-wrist flattening into impact (the independent corroboration for the flip — design §7.4).
bool trailWristFlattens(const RuleContext &c)
{
    if (!c.present(TW) || !c.ok(TW, P6) || !c.ok(TW, P7)) return false;
    return (c.delta(TW, P7) - c.delta(TW, P6)) < c.tuning.trailFlattenDeg;
}

// Clean downstream compensation for an open face at the top: the face squares by impact (lead-wrist
// flex-ext at P7 in range, not still open). Suppresses F1 to Watch (the style tolerance).
bool openFaceCompensated(const RuleContext &c)
{
    return c.ok(FE, P7) && c.rag(FE, P7) != PpRag::Red;
}

} // namespace

std::optional<PpWristFinding> PredicateRule::evaluate(const RuleContext &ctx) const
{
    const Out res = m_def.test(ctx);
    if (!res) return std::nullopt;

    PpWristFinding f;
    f.id = m_def.id; f.name = m_def.name; f.category = m_def.category;
    f.severity = res->first; f.magnitudeDeg = res->second;
    f.weight = m_def.weight;
    f.confidence = m_def.ruleBase;            // data/corroboration applied in run()
    f.dofs = m_def.dofs; f.positions = m_def.positions;
    f.ballFlight = m_def.ballFlight;
    f.explanation = m_def.explanation; f.coaching = m_def.coaching; f.protect = m_def.protect;
    return f;
}

AssessmentRuleRegistry AssessmentRuleRegistry::makeDefault()
{
    AssessmentRuleRegistry reg;
    for (RuleDef &d : defaultRuleDefs())
        reg.m_rules.push_back(std::make_unique<PredicateRule>(std::move(d)));
    return reg;
}

std::vector<PpWristFinding> AssessmentRuleRegistry::run(const RuleContext &ctx,
                                                        const RuleTuning &tuning) const
{
    std::vector<PpWristFinding> out;
    for (const auto &rule : m_rules)
        if (auto f = rule->evaluate(ctx))
            out.push_back(*f);

    // Confidence finalisation: ruleBase × data/segmentation confidence (already folded into the
    // cells), capped.
    for (PpWristFinding &f : out)
        f.confidence = std::min(1.0f, f.confidence * meanCellConf(ctx, f));

    // Corroboration: the flip gains confidence when the trail wrist independently flattens.
    if (PpWristFinding *flip = findById(out, QStringLiteral("flip"))) {
        if (trailWristFlattens(ctx)) {
            flip->confidence = std::min(1.0f, float(flip->confidence * (1.0 + tuning.corroborationBoost)));
            flip->corroboratedBy << QStringLiteral("trail-wrist flattening into impact");
        }
    }
    // Cast references the kinematic-sequence corroboration as a documented seam (no data source yet).
    if (PpWristFinding *cast = findById(out, QStringLiteral("cast")))
        cast->corroboratedBy << QStringLiteral("kinematic sequence (not yet wired)");

    // Mutual exclusion: over-rotation and holding-off are opposite ends of the forearm axis — keep
    // only the higher-confidence one if both somehow fired.
    {
        PpWristFinding *over = findById(out, QStringLiteral("over_rotation"));
        PpWristFinding *hold = findById(out, QStringLiteral("holding_off"));
        if (over && hold) {
            const QString drop = (over->confidence >= hold->confidence)
                                 ? QStringLiteral("holding_off") : QStringLiteral("over_rotation");
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [&](const PpWristFinding &f) { return f.id == drop; }), out.end());
        }
    }

    // Suppression: an open face at the top with clean downstream compensation downgrades to Watch.
    if (PpWristFinding *open = findById(out, QStringLiteral("open_face_top"))) {
        if (open->severity == Sev::Fault && openFaceCompensated(ctx)) {
            open->severity = Sev::Watch;
            open->corroboratedBy << QStringLiteral("downstream compensation");
        }
    }

    // Linkage: cast and flip are distinct events that commonly co-occur — surface as a linked pair.
    {
        PpWristFinding *cast = findById(out, QStringLiteral("cast"));
        PpWristFinding *flip = findById(out, QStringLiteral("flip"));
        if (cast && flip) { cast->linkedTo = flip->id; flip->linkedTo = cast->id; }
    }

    // Low-confidence demotion (kept, not dropped).
    for (PpWristFinding &f : out)
        f.lowConfidence = f.confidence < tuning.confidenceFloor;

    // Strengths policy: by default a strength only surfaces alongside a live fault to protect against.
    if (tuning.strengthsRequireAdjacentFault) {
        const bool anyFault = std::any_of(out.begin(), out.end(), [](const PpWristFinding &f) {
            return f.severity != Sev::Good;
        });
        if (!anyFault)
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [](const PpWristFinding &f) { return f.severity == Sev::Good; }),
                      out.end());
    }

    return out;
}

} // namespace pinpoint::analysis
