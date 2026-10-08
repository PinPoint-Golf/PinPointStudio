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

// camera_roi_test — the crop is per ROLE (cameras/camera_roi.h,
// flir_camera_settings.md §2.1). The impact strip lives under "<key>#impact"
// and every other role reads the plain entry, so choosing an impact mode no
// longer overwrites the down-the-line crop; an Impact camera with no strip of
// its own gets the recommended mode centred, never the full frame; and the
// one-time migration moves an existing impact camera's strip into its own
// entry. The REAL AppSettings runs against a scratch QSettings path, because
// the migration and its marker are properties of what lands on disk.

#include "app/app_settings.h"
#include "cameras/camera_roi.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static int g_run  = 0;

// Quiet on success: a FAIL line names the case, the summary counts them.
static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", qPrintable(label));
        ++g_fail;
    }
}

static bool same(const QRectF &a, const QRectF &b)
{
    return std::fabs(a.x() - b.x()) < 1e-9 && std::fabs(a.y() - b.y()) < 1e-9
        && std::fabs(a.width() - b.width()) < 1e-9 && std::fabs(a.height() - b.height()) < 1e-9;
}

static QVariantMap roi(double x, double y, double w, double h)
{
    return { { "x", x }, { "y", y }, { "w", w }, { "h", h } };
}

static const QString kCam = QStringLiteral("Chameleon3 CM3-U3-13Y3C|18277032");
constexpr int kDtl = 1, kFaceOn = 2, kImpact = pp_camroi::kImpactPerspective;

// A Chameleon3 as enumeration describes it: 1280x1024, impact modes probed.
static CameraCapabilities chameleon3()
{
    CameraCapabilities c;
    c.resolution.widthRange.max  = 1280;
    c.resolution.heightRange.max = 1024;
    QVariantMap m0{ { "w", 640 }, { "h", 240 }, { "fps", 611.7 } };
    QVariantMap m1{ { "w", 640 }, { "h", 320 }, { "fps", 467.4 } };
    c.extensions[QStringLiteral("impact.modes")] = QVariantList{ m0, m1 };
    return c;
}

// ── 1. Keys and lookup ──────────────────────────────────────────────────────

static void testLookup()
{
    check("K1 the impact role has its own key", pp_camroi::key(kCam, kImpact) == kCam + "#impact");
    check("K2 every other role shares the plain key",
          pp_camroi::key(kCam, kDtl) == kCam && pp_camroi::key(kCam, kFaceOn) == kCam
          && pp_camroi::key(kCam, 0) == kCam);

    const CameraCapabilities caps = chameleon3();
    const QVariantMap both{ { kCam, roi(0.2, 0.0, 0.54, 1.0) },
                            { kCam + "#impact", roi(0.25, 0.4, 0.5, 0.234375) } };
    check("L1 DTL reads its own crop, not the strip",
          same(pp_camroi::cropFor(both, kCam, kDtl, &caps), QRectF(0.2, 0.0, 0.54, 1.0)));
    check("L2 Impact reads the strip, not the DTL crop",
          same(pp_camroi::cropFor(both, kCam, kImpact, &caps), QRectF(0.25, 0.4, 0.5, 0.234375)));

    // The October failure the other way round: a strip stored, no DTL crop.
    const QVariantMap stripOnly{ { kCam + "#impact", roi(0.25, 0.4, 0.5, 0.234375) } };
    check("L3 a DTL camera with only a strip stored runs full frame",
          pp_camroi::cropFor(stripOnly, kCam, kDtl, &caps).isEmpty());

    // An Impact camera with nothing of its own: the recommended mode, centred.
    const QVariantMap dtlOnly{ { kCam, roi(0.2, 0.0, 0.54, 1.0) } };
    const QRectF fallback = pp_camroi::cropFor(dtlOnly, kCam, kImpact, &caps);
    check("L4 Impact without a strip gets the recommended mode centred (640x240 of 1280x1024)",
          same(fallback, QRectF(0.25, (1.0 - 240.0 / 1024.0) / 2.0, 0.5, 240.0 / 1024.0)));
    check("L5 without capabilities there is no fallback (full frame, as before)",
          pp_camroi::cropFor(dtlOnly, kCam, kImpact, nullptr).isEmpty());
    CameraCapabilities noModes = caps;
    noModes.extensions.remove(QStringLiteral("impact.modes"));
    check("L6 a camera that published no impact modes has no fallback",
          pp_camroi::cropFor(dtlOnly, kCam, kImpact, &noModes).isEmpty());
    const QVariantMap degenerate{ { kCam + "#impact", roi(0.1, 0.1, 0.0, 0.2) } };
    check("L7 a zero-size stored strip is ignored for the fallback",
          same(pp_camroi::cropFor(degenerate, kCam, kImpact, &caps), fallback));
}

// ── 2. AppSettings: the one-time migration ─────────────────────────────────

static void testMigration()
{
    QTemporaryDir scratch;
    // ⚠ BEFORE the first AppSettings: its constructor reads (and the migration
    // writes) the settings file, and the default path is the user's real one.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());

    const QString dtlCam = QStringLiteral("Chameleon3 CM3-U3-13Y3C|17453937");
    const QString both   = QStringLiteral("Blackfly S BFS-U3-16S2C|99");
    ppSettings().setValue(QStringLiteral("camera/perspective"),
                          QVariantMap{ { kCam, kImpact }, { dtlCam, kDtl }, { both, kImpact } });
    ppSettings().setValue(QStringLiteral("camera/roi"),
                          QVariantMap{ { kCam, roi(0.25, 0.4, 0.5, 0.234375) },        // the strip
                                       { dtlCam, roi(0.2, 0.0, 0.6, 1.0) },            // a DTL crop
                                       { both, roi(0.0, 0.0, 1.0, 1.0) },
                                       { both + "#impact", roi(0.3, 0.3, 0.4, 0.2) } });
    {
        AppSettings s;
        const QVariantMap r = s.cameraRoi();
        check("M1 the impact camera's strip moves to its own entry",
              r.contains(kCam + "#impact") && !r.contains(kCam)
              && same(pp_camroi::rectFrom(r.value(kCam + "#impact").toMap()), QRectF(0.25, 0.4, 0.5, 0.234375)));
        check("M2 a non-impact camera's crop is untouched",
              r.contains(dtlCam) && !r.contains(dtlCam + "#impact"));
        check("M3 a camera that already has its own strip is left alone",
              same(pp_camroi::rectFrom(r.value(both + "#impact").toMap()), QRectF(0.3, 0.3, 0.4, 0.2))
              && r.contains(both));
        check("M4 the migration is on disk",
              ppSettings().value(QStringLiteral("camera/roi")).toMap() == r);
        check("M5 the marker is written",
              ppSettings().value(QStringLiteral("camera/impactRoiMigrated")).toBool());

        // After the migration the operator gives the impact camera a DTL crop
        // of its own again: a later load must not move it.
        QVariantMap again = s.cameraRoi();
        again[kCam] = roi(0.1, 0.0, 0.8, 1.0);
        s.setCameraRoi(again);
    }
    {
        AppSettings s;
        check("M6 a later load does not migrate again (the plain entry stays)",
              s.cameraRoi().contains(kCam) && s.cameraRoi().contains(kCam + "#impact"));
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("camera_roi_test — the crop is per role (flir_camera_settings.md §2.1)\n");
    testLookup();
    testMigration();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
