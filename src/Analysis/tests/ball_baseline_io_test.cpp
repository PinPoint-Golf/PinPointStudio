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

// loadBallBaselineBlob (ball_baseline_io.h) contract: the persisted live-baseline
// blob loader BallRunner uses to reconstruct the tracker. Happy path (round-trips
// w*h float32 into a CV_32F Mat byte-exactly) plus every reject that forces the
// self-seed fallback — truncated file, missing file, wrong dims, degenerate dims.

#include "../ball_baseline_io.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;

#define CHECK(label, cond)                                                    \
    do {                                                                      \
        const bool checkOk_ = (cond);   /* distinct name: callers pass `ok` */ \
        std::printf("  [%s] %s\n", checkOk_ ? "PASS" : "FAIL", label);        \
        if (!checkOk_) ++g_fail;                                              \
    } while (0)

static bool writeFloats(const QString &path, const std::vector<float> &v)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const qint64 n = qint64(v.size()) * qint64(sizeof(float));
    return f.write(reinterpret_cast<const char *>(v.data()), n) == n;
}

int main()
{
    std::printf("loadBallBaselineBlob — happy path + rejects:\n");

    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::printf("  cannot create temp dir\n"); return 1; }

    const int w = 8, h = 6;                        // 48 floats, 192 bytes
    std::vector<float> ref(size_t(w) * h);
    for (int i = 0; i < w * h; ++i) ref[size_t(i)] = float(i) * 0.5f - 3.0f;

    const QString good = tmp.path() + "/good.f32";
    if (!writeFloats(good, ref)) { std::printf("  cannot write fixture\n"); return 1; }

    // 1. Happy path — dims, type, and byte-exact values.
    cv::Mat m;
    const bool ok = loadBallBaselineBlob(good, w, h, m);
    CHECK("happy: returns true", ok);
    CHECK("happy: dims + type", m.rows == h && m.cols == w && m.type() == CV_32F);
    bool valuesMatch = ok && m.isContinuous()
                    && std::memcmp(m.data, ref.data(), size_t(w) * h * sizeof(float)) == 0;
    CHECK("happy: values byte-exact", valuesMatch);

    // 2. Truncated file (one float short) — hard reject.
    const QString trunc = tmp.path() + "/trunc.f32";
    {
        QFile f(trunc);
        f.open(QIODevice::WriteOnly);
        const QByteArray raw(int((w * h - 1) * sizeof(float)), '\0');
        f.write(raw);
    }
    cv::Mat mt;
    CHECK("truncated: returns false", !loadBallBaselineBlob(trunc, w, h, mt));

    // 3. Missing file — reject.
    cv::Mat mm;
    CHECK("missing: returns false", !loadBallBaselineBlob(tmp.path() + "/nope.f32", w, h, mm));

    // 4. Wrong dims (product differs from the file's) — reject.
    cv::Mat mw;
    CHECK("wrong dims: returns false", !loadBallBaselineBlob(good, w + 1, h, mw));

    // 5. Degenerate dims — reject before touching the file.
    cv::Mat md;
    CHECK("zero dims: returns false", !loadBallBaselineBlob(good, 0, h, md));

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
