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

// Standalone test for the WB1 swing-level person-crop geometry
// (src/Analysis/pose_crop.h): union bbox → 15 % margin → 3:4 aspect → clamp →
// integer pixels, plus the full-frame fallback guards and fromOverrides plumbing.

#include "../pose_crop.h"

#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

int main()
{
    std::printf("=== pose_crop geometry ===\n");
    const PoseCropConfig cfg;   // frozen defaults (enabled, 0.15, 0.90, 3)

    // 1) Centred narrow bbox: locked margin + aspect + clamp + integer math.
    //    x[0.4,0.6] y[0.2,0.8] in a 1920×1080 frame, 10 contributing frames.
    //    px 768..1152 (w384), py 216..864 (h648) → +15 % margin → w499.2 h842.4
    //    → too narrow (0.593 < 0.75) so widen to 0.75·842.4 = 631.8 about cx=960
    //    → x 644.1..1275.9, y 118.8..961.2 → clamp+int {644,118,632,844}.
    {
        auto r = computePoseCropRect(0.4, 0.2, 0.6, 0.8, 10, 1920, 1080, cfg);
        CHECK("centred bbox produces a crop", r.has_value());
        if (r) {
            std::printf("    crop = {%d,%d,%d,%d}\n", r->x, r->y, r->w, r->h);
            CHECK("x == 644", r->x == 644);
            CHECK("y == 118", r->y == 118);
            CHECK("w == 632", r->w == 632);
            CHECK("h == 844", r->h == 844);
            const double aspect = double(r->w) / r->h;
            CHECK("aspect ~ 3:4 (0.75)", std::fabs(aspect - 0.75) < 0.01);
            CHECK("stays in frame", r->x >= 0 && r->y >= 0
                                    && r->x + r->w <= 1920 && r->y + r->h <= 1080);
        }
    }

    // 2) Fallback: too few contributing frames (< minBboxFrames).
    {
        auto r = computePoseCropRect(0.4, 0.2, 0.6, 0.8, 2, 1920, 1080, cfg);
        CHECK("< minBboxFrames ⇒ full-frame fallback", !r.has_value());
    }

    // 3) Fallback: crop disabled.
    {
        PoseCropConfig off = cfg; off.enabled = false;
        auto r = computePoseCropRect(0.4, 0.2, 0.6, 0.8, 10, 1920, 1080, off);
        CHECK("disabled ⇒ full-frame fallback", !r.has_value());
    }

    // 4) Fallback: near-full-frame bbox exceeds maxAreaFrac after margin/clamp.
    {
        auto r = computePoseCropRect(0.02, 0.02, 0.98, 0.98, 20, 1920, 1080, cfg);
        CHECK("≥ maxAreaFrac ⇒ full-frame fallback", !r.has_value());
    }

    // 5) Fallback: empty/degenerate bbox.
    {
        auto r = computePoseCropRect(0.5, 0.5, 0.5, 0.5, 10, 1920, 1080, cfg);
        CHECK("empty bbox ⇒ full-frame fallback", !r.has_value());
    }

    // 6) Edge clamp: a bbox hugging the left edge is clamped to x >= 0 and stays valid.
    {
        auto r = computePoseCropRect(0.0, 0.3, 0.15, 0.7, 10, 1920, 1080, cfg);
        CHECK("edge bbox produces a crop", r.has_value());
        if (r) {
            CHECK("clamped to x >= 0", r->x >= 0);
            CHECK("stays in frame", r->x + r->w <= 1920 && r->y + r->h <= 1080);
        }
    }

    // 7) fromOverrides: dotted-key injection reaches the config fields.
    {
        QVariantMap ov;
        ov.insert(QStringLiteral("pose.crop.enabled"), false);
        ov.insert(QStringLiteral("pose.crop.marginFrac"), 0.25);
        ov.insert(QStringLiteral("pose.crop.minBboxFrames"), 5);
        ov.insert(QStringLiteral("pose.decode.dark"), false);
        const PoseAccuracyConfig acc = PoseAccuracyConfig::fromOverrides(ov);
        CHECK("override: crop.enabled=false", acc.crop.enabled == false);
        CHECK("override: crop.marginFrac=0.25", std::fabs(acc.crop.marginFrac - 0.25) < 1e-9);
        CHECK("override: crop.minBboxFrames=5", acc.crop.minBboxFrames == 5);
        CHECK("override: decode.dark=false", acc.decodeDark == false);
    }

    // 8) Defaults (no overrides) == frozen constants (crop + DARK both ON).
    {
        const PoseAccuracyConfig def = PoseAccuracyConfig::fromOverrides(QVariantMap{});
        CHECK("default crop.enabled==true", def.crop.enabled == true);
        CHECK("default marginFrac==0.15", std::fabs(def.crop.marginFrac - 0.15) < 1e-9);
        CHECK("default maxAreaFrac==0.90", std::fabs(def.crop.maxAreaFrac - 0.90) < 1e-9);
        CHECK("default minBboxFrames==3", def.crop.minBboxFrames == 3);
        CHECK("default decode.dark==true", def.decodeDark == true);
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
