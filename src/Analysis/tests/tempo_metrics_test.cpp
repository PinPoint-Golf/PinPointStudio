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

// Standalone test for the tempo metrics engine (src/Analysis/tempo_metrics.{h,cpp}):
// the Address→Top / Top→Impact basis, every refusal gate (disabled, conf, missing
// events, non-monotone ladder), the emitted series shape the dashboard depends on,
// and the uncertainty propagation — in particular that Top dominates, which is the
// whole reason the sigma exists. Synthetic ladders only, no fixture. Mirrors
// foot_metrics_test.cpp's structure/style.

#include "../tempo_metrics.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A ladder in seconds — the values a real swing carries, so the arithmetic below
// is readable as golf rather than as microseconds.
static Segmentation ladder(double addressS, double topS, double impactS, float conf = 0.9f,
                           float addrConf = 0.9f, float topConf = 0.9f, float impConf = 1.0f)
{
    Segmentation seg;
    seg.conf = conf;
    const auto ev = [](Phase p, double s, float c) {
        PhaseEvent e;
        e.phase = p;
        e.t_us  = int64_t(s * 1e6);
        e.conf  = c;
        return e;
    };
    seg.events.push_back(ev(Phase::Address, addressS, addrConf));
    seg.events.push_back(ev(Phase::Top,     topS,     topConf));
    seg.events.push_back(ev(Phase::Impact,  impactS,  impConf));
    return seg;
}

static const MetricSeries *find(const std::vector<MetricSeries> &v, const char *key)
{
    for (const MetricSeries &m : v)
        if (m.key == QString::fromLatin1(key))
            return &m;
    return nullptr;
}

int main()
{
    std::printf("=== tempo metrics ===\n");

    // ---------------------------------------------------------------- the basis
    {
        // A textbook 3:1 swing: 0.9 s back (address→top), 0.3 s down (top→impact).
        const TempoResult r = measureTempo(ladder(10.0, 10.9, 11.2));
        CHECK("nominal ladder is valid",        r.valid);
        CHECK("backswing == 0.9 s",             near(r.backswingS, 0.9, 1e-9));
        CHECK("downswing == 0.3 s",             near(r.downswingS, 0.3, 1e-9));
        CHECK("ratio == 3.0",                   near(r.ratio, 3.0, 1e-9));
        CHECK("anchored at the impact instant", r.impactUs == int64_t(11.2 * 1e6));

        // The basis is ADDRESS→Top, not Takeaway→Top. A Takeaway event sitting
        // between them must NOT be picked up — this is the one assertion that
        // pins the documented (and deliberately non-standard) definition.
        Segmentation withTakeaway = ladder(10.0, 10.9, 11.2);
        PhaseEvent tk;
        tk.phase = Phase::Takeaway;
        tk.t_us  = int64_t(10.2 * 1e6);
        tk.conf  = 0.9f;
        withTakeaway.events.insert(withTakeaway.events.begin() + 1, tk);
        const TempoResult rt = measureTempo(withTakeaway);
        CHECK("Takeaway is ignored (basis is Address)", near(rt.ratio, 3.0, 1e-9));
    }

    // ------------------------------------------------------------ refusal gates
    {
        TempoConfig off;
        off.enabled = false;
        CHECK("disabled ⇒ refused", !measureTempo(ladder(10.0, 10.9, 11.2), off).valid);
        CHECK("disabled ⇒ no series",
              buildTempoSeries(ladder(10.0, 10.9, 11.2), off).empty());

        // conf == 0 is the IMU clampFallback signature (Address pinned to the
        // window edge, no Top). Refuse the whole ladder, don't trust part of it.
        CHECK("seg.conf == 0 ⇒ refused",
              !measureTempo(ladder(10.0, 10.9, 11.2, /*conf*/ 0.0f)).valid);

        // Missing events, one at a time.
        for (const Phase drop : { Phase::Address, Phase::Top, Phase::Impact }) {
            Segmentation seg = ladder(10.0, 10.9, 11.2);
            for (auto it = seg.events.begin(); it != seg.events.end(); ++it)
                if (it->phase == drop) { seg.events.erase(it); break; }
            const bool refused = !measureTempo(seg).valid;
            CHECK("missing event ⇒ refused", refused);
        }

        // Non-monotone ladders. The segmenter's monotone pass can drop events, so
        // an out-of-order pair is reachable, not merely defensive.
        CHECK("Top before Address ⇒ refused", !measureTempo(ladder(10.9, 10.0, 11.2)).valid);
        CHECK("Impact before Top ⇒ refused",  !measureTempo(ladder(10.0, 10.9, 10.5)).valid);
        CHECK("Top == Address ⇒ refused",     !measureTempo(ladder(10.0, 10.0, 11.2)).valid);
        CHECK("Impact == Top ⇒ refused",      !measureTempo(ladder(10.0, 10.9, 10.9)).valid);
    }

    // ------------------------------------------------------------- series shape
    {
        const std::vector<MetricSeries> out = buildTempoSeries(ladder(10.0, 10.9, 11.2));
        CHECK("emits exactly two series", out.size() == 2);

        const MetricSeries *ratio = find(out, "tempoRatio");
        const MetricSeries *back  = find(out, "tempoBackswing");
        CHECK("tempoRatio present",      ratio != nullptr);
        CHECK("tempoBackswing present",  back  != nullptr);
        if (ratio && back) {
            // Summary scalars: EMPTY curve, one phaseSample. Anything else breaks
            // the degenerate-MetricSeries convention the readers rely on.
            CHECK("ratio has no curve",   ratio->t_us.empty() && ratio->value.empty());
            CHECK("ratio has 1 sample",   ratio->phaseSamples.size() == 1);
            // The dashboard's Verdict tile samples tempoRatio at phase 5 (Impact).
            // If this ever moves, the tile silently stops rendering.
            CHECK("ratio sample is at Impact",
                  ratio->phaseSamples[0].phase == Phase::Impact);
            CHECK("ratio value == 3.0",   near(ratio->phaseSamples[0].value, 3.0, 1e-9));
            CHECK("ratio unit is ':1'",   ratio->unit == QStringLiteral(":1"));
            CHECK("backswing value == 0.9 s",
                  near(back->phaseSamples[0].value, 0.9, 1e-9));
            CHECK("backswing unit is 's'", back->unit == QStringLiteral("s"));
            CHECK("both carry a sigma",   ratio->sigma.has_value() && back->sigma.has_value());
            CHECK("sigma is positive",    ratio->sigma.value_or(-1.0) > 0.0);
        }
    }

    // ------------------------------------------------------------- uncertainty
    {
        // Confidence must WIDEN the interval and never move the value — the
        // score_uncertainty rule this engine inherits.
        const TempoResult sure   = measureTempo(ladder(10.0, 10.9, 11.2, 0.9f, 0.9f, 0.9f));
        const TempoResult unsure = measureTempo(ladder(10.0, 10.9, 11.2, 0.9f, 0.9f, 0.25f));
        CHECK("low Top conf leaves the ratio unchanged",
              near(sure.ratio, unsure.ratio, 1e-12));
        CHECK("low Top conf widens the ratio sigma",
              unsure.ratioSigma > sure.ratioSigma);

        // Top dominates: it appears in BOTH halves of the ratio with opposite
        // sign, so degrading it must hurt more than degrading Address by the same
        // amount. This is the property the correlated propagation exists to
        // capture — treating B and D as independent would lose it.
        const TempoResult badAddr = measureTempo(ladder(10.0, 10.9, 11.2, 0.9f, 0.25f, 0.9f));
        CHECK("Top uncertainty dominates Address uncertainty",
              unsure.ratioSigma > badAddr.ratioSigma);

        // Sanity on magnitude: with the frozen 20 ms floor and a 0.3 s downswing,
        // a ~30 ms Top error should move the ratio by order 10 % — the headline
        // sensitivity the design notes warn about.
        CHECK("sigma is the right order of magnitude (0.05–1.0 on a 3:1 swing)",
              sure.ratioSigma > 0.05 && sure.ratioSigma < 1.0);

        // A slower downswing is measured more precisely — σ scales with 1/D.
        const TempoResult slowDown = measureTempo(ladder(10.0, 10.9, 11.5));
        CHECK("longer downswing ⇒ tighter ratio sigma", slowDown.ratioSigma < sure.ratioSigma);

        // Backswing sigma is the plain quadrature of its two endpoints.
        CHECK("backswing sigma is positive and modest",
              sure.backswingSigmaS > 0.0 && sure.backswingSigmaS < 0.2);
    }

    // ---------------------------------------------------------- tuning plumbing
    {
        QVariantMap ov;
        ov[QStringLiteral("tempo.enabled")]     = false;
        ov[QStringLiteral("tempo.minConf")]     = 0.75;
        ov[QStringLiteral("tempo.baseSigmaS")]  = 0.05;
        ov[QStringLiteral("tempo.confInflate")] = 2.0;
        const TempoConfig c = TempoConfig::fromOverrides(ov);
        CHECK("tempo.enabled override",     c.enabled == false);
        CHECK("tempo.minConf override",     near(c.minConf, 0.75, 1e-12));
        CHECK("tempo.baseSigmaS override",  near(c.baseSigmaS, 0.05, 1e-12));
        CHECK("tempo.confInflate override", near(c.confInflate, 2.0, 1e-12));

        // minConf actually gates: a 0.5-conf vision ladder is refused above it.
        TempoConfig strict;
        strict.minConf = 0.75;
        CHECK("minConf refuses a 0.5-conf vision ladder",
              !measureTempo(ladder(10.0, 10.9, 11.2, 0.5f), strict).valid);
        CHECK("minConf accepts a 0.9-conf ladder",
              measureTempo(ladder(10.0, 10.9, 11.2, 0.9f), strict).valid);
    }

    std::printf(g_fail ? "=== FAILED (%d) ===\n" : "=== OK ===\n", g_fail);
    return g_fail ? 1 : 0;
}
