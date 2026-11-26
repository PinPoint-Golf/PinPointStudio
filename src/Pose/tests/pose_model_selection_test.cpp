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

// Standalone test for the tier -> pose-model decision (src/Pose/pose_model_selection.h).
// Pure QString logic, no ONNX Runtime — locks the rule that ViTPose++-L runs ONLY
// when the "High" tier is selected AND the (on-demand) L model has been downloaded,
// and that every other combination falls back to ViTPose-B.
// And the model file's fingerprint, pinpoint::modelFileFingerprint (src/Core/pp_model_path.h): the
// text analysis.versions.pose.model holds, pinned to the strings the reader it replaced produced on
// the same files (that reader, run on 26 Nov 2026 at 1e3269a8), so a swing's stamp cannot move.

#include "../pose_model_selection.h"
#include "../../Core/pp_model_path.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using pinpoint::pose::useVitPoseLarge;

static int g_fail = 0;

#define CHECK(label, cond)                                             \
    do {                                                               \
        const bool ok = (cond);                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);       \
        if (!ok) ++g_fail;                                             \
    } while (0)

int main()
{
    std::printf("=== pose_model_selection_test (tier -> model) ===\n");

    // High + model present -> use L.
    CHECK("High + available -> L",           useVitPoseLarge(QStringLiteral("High"),   true)  == true);

    // High but the model has not been downloaded yet -> fall back to B.
    CHECK("High + unavailable -> B",          useVitPoseLarge(QStringLiteral("High"),   false) == false);

    // Medium never uses L, regardless of availability.
    CHECK("Medium + available -> B",          useVitPoseLarge(QStringLiteral("Medium"), true)  == false);
    CHECK("Medium + unavailable -> B",        useVitPoseLarge(QStringLiteral("Medium"), false) == false);

    // "Low" was a real tier (MoveNet) before it was removed 2026-07-13 as
    // vestigial; keep it exercised as a legacy/unknown string so an old QSettings
    // value that somehow slips past the AppSettings migration still degrades
    // safely to B rather than doing something undefined.
    CHECK("Low (legacy) + available -> B",    useVitPoseLarge(QStringLiteral("Low"),    true)  == false);

    // Case-insensitive on the tier name (defensive against stored casing drift).
    CHECK("high (lowercase) + available -> L", useVitPoseLarge(QStringLiteral("high"),   true)  == true);
    CHECK("HIGH (uppercase) + available -> L", useVitPoseLarge(QStringLiteral("HIGH"),   true)  == true);

    // Empty / unknown tiers are safe (fall back to B).
    CHECK("empty tier -> B",                  useVitPoseLarge(QString(),                 true)  == false);
    CHECK("unknown tier -> B",                useVitPoseLarge(QStringLiteral("Ultra"),   true)  == false);

    // The model file's fingerprint: "<file name>@<bytes>", 0 bytes when the file is missing. Every
    // expected string below is what the replaced reader gave for the same file. There is no
    // "no reader installed" case any more: nothing is installed, so there is no such state.
    QTemporaryDir tmp;
    const QString accented = QStringLiteral("mod") + QChar(0x00E8) + QStringLiteral("le fp16.onnx");
    auto makeFile = [&](const QString &name, qint64 bytes) {
        QFile f(tmp.path() + QLatin1Char('/') + name);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        return bytes <= 16 ? f.write(QByteArray(int(bytes), 'x')) == bytes : f.resize(bytes);
    };
    CHECK("fixture: the model files", tmp.isValid()
              && makeFile(QStringLiteral("vitpose-test.onnx"), 7)
              && makeFile(QStringLiteral("empty.onnx"), 0)
              && makeFile(accented, 12)
              && makeFile(QStringLiteral("big.onnx"), 360242862));
    using pinpoint::modelFileFingerprint;
    const QString dir = tmp.path() + QLatin1Char('/');
    CHECK("present: \"<file>@<bytes>\"",
          modelFileFingerprint(dir + QStringLiteral("vitpose-test.onnx")) == QStringLiteral("vitpose-test.onnx@7"));
    CHECK("missing: \"<file>@0\"",
          modelFileFingerprint(dir + QStringLiteral("absent.onnx")) == QStringLiteral("absent.onnx@0"));
    CHECK("present and empty: \"<file>@0\"",
          modelFileFingerprint(dir + QStringLiteral("empty.onnx")) == QStringLiteral("empty.onnx@0"));
    CHECK("a name with an accent and a space, as written",
          modelFileFingerprint(dir + accented) == accented + QStringLiteral("@12"));
    CHECK("the bundled ViTPose-B's size, in full (vitpose-b-wholebody.onnx@360242862 on a real swing)",
          modelFileFingerprint(dir + QStringLiteral("big.onnx")) == QStringLiteral("big.onnx@360242862"));
    CHECK("no path: \"@0\"", modelFileFingerprint(QString()) == QStringLiteral("@0"));

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
