// Hand-track cleaning for the face-on shaft tracker (tracker_robustness, 2026-10-01).
//
// The tracker's ray origin, its phase model and its stillness tests all read ONE
// signal: the grip, the mean of the pose's two hand centroids. Two things the
// pose does to that signal broke whole tracks on 16 Sept 2026 (W02 s2, ViTPose-B):
//
//   1. THE PAIR DISAGREES. The lead-hand centroid sat on the lead WRIST, 100 px
//      up the arm from the trail hand, so the grip origin was 42 px off the club
//      and the strongest line through it was origin→hands, not the shaft. Two
//      hands holding one club are never far apart: when the pair is wider than
//      `pairTolPx`, the hand nearer the forearm's expected grip point
//      (wrist-mid + forearmLen along the lead forearm) is the grip for both.
//   2. A HAND GLITCHES. Every ~80 ms one hand jumped ~85 px and came straight
//      back (both models, same frames). The lerp turned each into an 8-frame
//      speed burst — the "fidget" the phase model's bridging/no-return machinery
//      exists to fight, and what put the top 194 ms early. A jump larger than
//      `glitchPx` that RETURNS on the next frame (the next frame is back within
//      glitchPx/2 of the frame before the jump) is replaced by its neighbours'
//      mean. Real motion never comes back, so a real takeaway is untouched.
//
// Pure functions over plain vectors (one entry per pose frame), so the rule is
// unit-testable without a pose model; the tracker applies it to a COPY of the
// pose track before deriving the grip. Both rules are byte-identical no-ops on a
// track that shows neither fault.
#pragma once

#include <QPointF>

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace pinpoint::analysis {

struct HandCleanConfig {
    bool   enabled        = true;
    double pairTolPx      = 64.0;   // pair tolerance when no lead forearm is confident (px)
    double pairTolForearm = 1.0;    // pair tolerance as a multiple of the lead forearm length (elbow→wrist) — the
                                    // scale-aware form: two hands on a grip sit within ~0.65 forearms of each
                                    // other (16 Sept L: 78 px on a 123 px forearm, clean); a centroid on the
                                    // wrist is ~1.8 forearms from the other hand (16 Sept B: 123 px on 69 px)
    double glitchPx       = 40.0;   // a one-frame excursion larger than this that returns is a glitch
    double forearmGain    = 1.0;    // expected grip = wristMid + forearmGain × |elbow→wrist| along the lead forearm
    double stillPx        = 20.0;   // the pair rule fires only where BOTH hands moved less than this since the
                                    // previous pose frame and to the next: the address hold it was built for.
                                    // Mid-swing the pose's hands part for real reasons and the tracker has
                                    // always used their midpoint there; changing that moved the finish start
                                    // 55 frames on a clean taped swing (06-11 s1). The glitch rule needs the
                                    // hand RESTING before and after the jump by the same measure.
    // The glitch rule is applied only to frames BEFORE IMPACT (cleanHandTrack's
    // glitchLimit, set by the tracker from the job's impact time): the address
    // flicker and the backswing flaps it was built for (16 Sept B needs both). The
    // finish hold is a resting hand too, and its one-frame flaps are what extends
    // the phase model's last motion run; fixing them moved the finish start 50
    // frames on the taped 06-11 swings.
    int    pairMinRun     = 4;      // the pair rule fires only on a run of at least this many consecutive
                                    // still, inconsistent frames (~100 ms at 37 Hz): a centroid that has
                                    // climbed the arm stays there (16 Sept B: the whole hold); an isolated
                                    // frame is a flicker, and switching the grip between the midpoint and
                                    // one hand on single frames put a 50 px step into the grip track and
                                    // broke a sane phase model (06-11 s2, taped).
};

struct HandCleanStats {
    int pairFixed   = 0;   // frames where the pair disagreed and one hand was chosen
    int glitchFixed = 0;   // hand samples replaced by their neighbours' mean
};

// One pose frame's inputs, in PIXELS. conf < 0 ⇒ that joint is unusable.
struct HandFrameIn {
    QPointF lead, trail;            // hand centroids (the tracker's grip = their mean)
    QPointF leadWrist, trailWrist;  // COCO wrists
    QPointF leadElbow;              // COCO lead elbow
    float   leadElbowConf  = -1.f;
    float   leadWristConf  = -1.f;
    float   trailWristConf = -1.f;
};

inline double handDist(const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

// Rule 1 — pair consistency. Returns the (possibly replaced) lead/trail pair.
inline bool fixHandPair(HandFrameIn& f, const HandCleanConfig& cfg)
{
    const bool forearmOk = f.leadElbowConf > 0.3f && f.leadWristConf > 0.3f && f.trailWristConf > 0.3f;
    const double forearmLen = forearmOk ? handDist(f.leadWrist, f.leadElbow) : 0.0;
    const double tol = (forearmOk && forearmLen > 8.0 && cfg.pairTolForearm > 0.0)
                           ? cfg.pairTolForearm * forearmLen : cfg.pairTolPx;
    if (!(tol > 0.0) || handDist(f.lead, f.trail) <= tol) return false;
    // Expected grip from the lead forearm: wrist-mid + forearmGain × forearm length along elbow→wrist.
    if (!forearmOk) {
        // No forearm to reason with: the LOWER hand (larger y) is the grip — the hands
        // are the bottom of the arms at address, and a centroid that climbed the arm is
        // the one that is wrong. Applies to the address-like geometry this rule is for.
        const QPointF pick = (f.lead.y() >= f.trail.y()) ? f.lead : f.trail;
        f.lead = pick; f.trail = pick;
        return true;
    }
    const QPointF wristMid((f.leadWrist.x() + f.trailWrist.x()) * 0.5, (f.leadWrist.y() + f.trailWrist.y()) * 0.5);
    const double fx = f.leadWrist.x() - f.leadElbow.x(), fy = f.leadWrist.y() - f.leadElbow.y();
    const double fl = std::hypot(fx, fy);
    QPointF expect = wristMid;
    if (fl > 1.0) expect = QPointF(wristMid.x() + cfg.forearmGain * fx, wristMid.y() + cfg.forearmGain * fy);
    const QPointF pick = (handDist(f.lead, expect) <= handDist(f.trail, expect)) ? f.lead : f.trail;
    f.lead = pick; f.trail = pick;
    return true;
}

// Rule 2 — single-frame glitch rejection on ONE hand series. In place. A glitch is a
// jump over glitchPx that comes straight back, from a hand that was RESTING before
// and after it (its neighbours within stillPx of their own neighbours, where those
// exist): the address-hold flicker. A hand in motion that flaps for one frame is
// left alone — the finish-hold flaps extend the phase model's motion run, and
// smoothing them moved the finish start on most corpus swings.
inline int fixHandGlitches(std::vector<QPointF>& p, double glitchPx, double stillPx = 0.0,
                           size_t limit = size_t(-1))
{
    if (!(glitchPx > 0.0) || p.size() < 3) return 0;
    int fixed = 0;
    for (size_t i = 1; i + 1 < p.size() && i < limit; ++i) {
        const double jump = handDist(p[i], p[i - 1]);
        if (jump <= glitchPx) continue;
        if (handDist(p[i + 1], p[i - 1]) > 0.5 * glitchPx) continue;   // it did not come back: real motion
        if (stillPx > 0.0) {
            if (i >= 2 && handDist(p[i - 1], p[i - 2]) >= stillPx) continue;            // not resting before
            if (i + 2 < p.size() && handDist(p[i + 1], p[i + 2]) >= stillPx) continue;  // not resting after
        }
        p[i] = QPointF((p[i - 1].x() + p[i + 1].x()) * 0.5, (p[i - 1].y() + p[i + 1].y()) * 0.5);
        ++fixed;
    }
    return fixed;
}

// Both rules over a whole track. `frames` is modified in place (lead/trail only).
// glitchLimit = the first frame the glitch rule must NOT touch (the impact frame);
// default = every frame.
inline HandCleanStats cleanHandTrack(std::vector<HandFrameIn>& frames, const HandCleanConfig& cfg,
                                     size_t glitchLimit = size_t(-1))
{
    HandCleanStats st;
    if (!cfg.enabled || frames.empty()) return st;
    std::vector<QPointF> lead(frames.size()), trail(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) { lead[i] = frames[i].lead; trail[i] = frames[i].trail; }
    st.glitchFixed += fixHandGlitches(lead, cfg.glitchPx, cfg.stillPx, glitchLimit);
    st.glitchFixed += fixHandGlitches(trail, cfg.glitchPx, cfg.stillPx, glitchLimit);
    // Stillness from the (de-glitched) hands: both within stillPx of the previous and
    // the next frame. A lone frame counts as still (nothing to compare against).
    auto stillAt = [&](size_t i) {
        auto near = [&](size_t a, size_t b) {
            return handDist(lead[a], lead[b]) < cfg.stillPx && handDist(trail[a], trail[b]) < cfg.stillPx;
        };
        if (i > 0 && !near(i, i - 1)) return false;
        if (i + 1 < frames.size() && !near(i, i + 1)) return false;
        return true;
    };
    // Pass 1: which still frames WOULD fire (on a scratch copy). Pass 2: apply only
    // on runs of >= pairMinRun consecutive firing frames.
    std::vector<char> fires(frames.size(), 0);
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].lead = lead[i]; frames[i].trail = trail[i];
        if (cfg.stillPx > 0.0 && !stillAt(i)) continue;
        HandFrameIn probe = frames[i];
        if (fixHandPair(probe, cfg)) fires[i] = 1;
    }
    const int minRun = std::max(1, cfg.pairMinRun);
    for (size_t i = 0; i < frames.size();) {
        if (!fires[i]) { ++i; continue; }
        size_t j = i;
        while (j < frames.size() && fires[j]) ++j;
        if (int(j - i) >= minRun)
            for (size_t k = i; k < j; ++k)
                if (fixHandPair(frames[k], cfg)) ++st.pairFixed;
        i = j;
    }
    return st;
}

} // namespace pinpoint::analysis
