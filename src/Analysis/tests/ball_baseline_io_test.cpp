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

// ballBaselineFromBytes (ball_baseline_io.h) contract: the live-baseline blob
// reader BallRunner uses to reconstruct the tracker. Since refactor step 7.2 the
// stage opens no file: a recorded swing's blob is read by the Recording loader
// into BallBaselineRef::bytes (recording_contract_test Test 5b), the live one is
// in memory, and both reach the stage as bytes. Happy path (w*h float32 into a
// CV_32F Mat byte-exactly) plus every reject that forces the self-seed fallback:
// a truncated blob, an unreadable file (no bytes), wrong dims, degenerate dims.

#include "../../Ball/ball_baseline_io.h"

#include <QByteArray>

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

static QByteArray bytesOf(const std::vector<float> &v)
{
    return QByteArray(reinterpret_cast<const char *>(v.data()), int(v.size() * sizeof(float)));
}

int main()
{
    std::printf("ballBaselineFromBytes — happy path + rejects:\n");

    const int w = 8, h = 6;                        // 48 floats, 192 bytes
    std::vector<float> ref(size_t(w) * h);
    for (int i = 0; i < w * h; ++i) ref[size_t(i)] = float(i) * 0.5f - 3.0f;
    const QByteArray good = bytesOf(ref);

    // 1. Happy path — dims, type, and byte-exact values.
    cv::Mat m;
    const bool ok = ballBaselineFromBytes(good, w, h, m);
    CHECK("happy: returns true", ok);
    CHECK("happy: dims + type", m.rows == h && m.cols == w && m.type() == CV_32F);
    bool valuesMatch = ok && m.isContinuous()
                    && std::memcmp(m.data, ref.data(), size_t(w) * h * sizeof(float)) == 0;
    CHECK("happy: values byte-exact", valuesMatch);

    // 2. Truncated blob (one float short) — hard reject, `out` untouched.
    cv::Mat mt;
    CHECK("truncated: returns false", !ballBaselineFromBytes(good.left(good.size() - int(sizeof(float))), w, h, mt));
    CHECK("truncated: out untouched", mt.empty());

    // 3. No bytes (the loader could not read the file) — reject.
    cv::Mat mm;
    CHECK("unread file (no bytes): returns false", !ballBaselineFromBytes(QByteArray(), w, h, mm));

    // 4. Wrong dims (product differs from the blob's) — reject.
    cv::Mat mw;
    CHECK("wrong dims: returns false", !ballBaselineFromBytes(good, w + 1, h, mw));

    // 5. Degenerate dims — reject.
    cv::Mat md;
    CHECK("zero dims: returns false", !ballBaselineFromBytes(good, 0, h, md));

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
