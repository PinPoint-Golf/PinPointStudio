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

#include "phase_signals.h"

#include <algorithm>
#include <cmath>

namespace pinpoint::analysis::phase_signals {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Biquad {
    double b0, b1, b2, a1, a2;   // normalized (a0 = 1)
};

// RBJ-cookbook 2nd-order Butterworth low-pass (Q = 1/√2).
Biquad butterworthLp(double fsHz, double fcHz)
{
    const double w0    = 2.0 * kPi * fcHz / fsHz;
    const double alpha = std::sin(w0) / (2.0 * (1.0 / std::sqrt(2.0)));
    const double cw    = std::cos(w0);
    const double a0    = 1.0 + alpha;
    Biquad q;
    q.b0 = (1.0 - cw) / 2.0 / a0;
    q.b1 = (1.0 - cw) / a0;
    q.b2 = q.b0;
    q.a1 = -2.0 * cw / a0;
    q.a2 = (1.0 - alpha) / a0;
    return q;
}

void filterInPlace(std::vector<double> &x, const Biquad &q)
{
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    if (!x.empty()) { x1 = x2 = x.front(); y1 = y2 = x.front(); }   // DC-steady start
    for (double &v : x) {
        const double y = q.b0 * v + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
        x2 = x1; x1 = v;
        y2 = y1; y1 = y;
        v  = y;
    }
}

} // namespace

std::vector<double> lowpassZeroPhase(const std::vector<double> &x,
                                     double fsHz, double fcHz)
{
    if (x.size() < 3 || fcHz <= 0.0 || fsHz <= 0.0 || fcHz >= 0.5 * fsHz)
        return x;

    // Reflection-pad both ends (~2 cutoff periods) so the forward-backward
    // passes settle before they reach real data — edge events stay unbiased.
    const int pad = std::min<int>(int(x.size()) - 1,
                                  std::max(8, int(2.0 * fsHz / fcHz)));
    std::vector<double> w;
    w.reserve(x.size() + 2 * size_t(pad));
    for (int i = pad; i >= 1; --i) w.push_back(2.0 * x.front() - x[size_t(i)]);
    w.insert(w.end(), x.begin(), x.end());
    for (int i = 1; i <= pad; ++i) w.push_back(2.0 * x.back() - x[x.size() - 1 - size_t(i)]);

    const Biquad q = butterworthLp(fsHz, fcHz);
    filterInPlace(w, q);                 // forward
    std::reverse(w.begin(), w.end());
    filterInPlace(w, q);                 // backward — cancels the phase lag
    std::reverse(w.begin(), w.end());

    return std::vector<double>(w.begin() + pad, w.begin() + pad + long(x.size()));
}

std::vector<double> energyEnvelope(const SegmentStream &s, double fsHz, double fcHz)
{
    std::vector<double> mag;
    mag.reserve(s.gyroDps.size());
    for (const QVector3D &g : s.gyroDps)
        mag.push_back(double(g.length()));
    return lowpassZeroPhase(mag, fsHz, fcHz);
}

std::vector<QVector3D> worldGyro(const SegmentStream &s)
{
    std::vector<QVector3D> gw;
    const size_t n = std::min(s.qAnat.size(), s.gyroDps.size());
    gw.reserve(n);
    for (size_t i = 0; i < n; ++i)
        gw.push_back(s.qAnat[i].rotatedVector(s.gyroDps[i]));
    return gw;
}

QVector3D principalRotationAxis(const std::vector<QVector3D> &gw,
                                const std::vector<int64_t> &grid,
                                int64_t fromUs, int64_t toUs)
{
    // Accumulate the 3×3 outer-product sum over the interval.
    double m[3][3] = {};
    int used = 0;
    const size_t n = std::min(gw.size(), grid.size());
    for (size_t i = 0; i < n; ++i) {
        if (grid[i] < fromUs || grid[i] > toUs)
            continue;
        const double v[3] = { double(gw[i].x()), double(gw[i].y()), double(gw[i].z()) };
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                m[r][c] += v[r] * v[c];
        ++used;
    }
    const double trace = m[0][0] + m[1][1] + m[2][2];
    if (used < 3 || trace <= 1e-9)
        return QVector3D();

    // Power iteration — symmetric PSD 3×3, the dominant eigenvector converges
    // fast; start from the largest diagonal to avoid an orthogonal start.
    int d = 0;
    if (m[1][1] > m[d][d]) d = 1;
    if (m[2][2] > m[d][d]) d = 2;
    double v[3] = { m[0][d], m[1][d], m[2][d] };
    for (int it = 0; it < 50; ++it) {
        double w[3] = {
            m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
        };
        const double len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        if (len <= 1e-12)
            return QVector3D();
        v[0] = w[0] / len; v[1] = w[1] / len; v[2] = w[2] / len;
    }
    QVector3D axis{ float(v[0]), float(v[1]), float(v[2]) };

    // Sign-fix: mean projection positive over the same interval.
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i)
        if (grid[i] >= fromUs && grid[i] <= toUs)
            mean += double(QVector3D::dotProduct(gw[i], axis));
    return mean >= 0.0 ? axis : -axis;
}

std::vector<double> signedRate(const std::vector<QVector3D> &gw, const QVector3D &n)
{
    std::vector<double> r;
    r.reserve(gw.size());
    for (const QVector3D &g : gw)
        r.push_back(double(QVector3D::dotProduct(g, n)));
    return r;
}

std::vector<double> inclination(const SegmentStream &s, const QVector3D &segAxis)
{
    std::vector<double> th;
    th.reserve(s.qAnat.size());
    for (const QQuaternion &q : s.qAnat) {
        const QVector3D a = q.rotatedVector(segAxis);
        th.push_back(std::asin(std::clamp(double(a.z()), -1.0, 1.0)));
    }
    return th;
}

std::vector<double> axialRate(const SegmentStream &s, const QVector3D &axisSeg)
{
    const QVector3D a = axisSeg.normalized();
    std::vector<double> r;
    r.reserve(s.gyroDps.size());
    for (const QVector3D &g : s.gyroDps)
        r.push_back(double(QVector3D::dotProduct(g, a)));
    return r;
}

std::vector<uint8_t> stillMask(const FusedStreams &streams,
                               double gyroStillDps, double accelTolG)
{
    const size_t n = streams.timeGrid.size();
    std::vector<uint8_t> still(n, streams.segments.empty() ? 0 : 1);
    for (const SegmentStream &s : streams.segments) {
        const size_t m = std::min({ n, s.gyroDps.size(), s.accelG.size() });
        for (size_t i = 0; i < m; ++i) {
            if (!still[i])
                continue;
            if (double(s.gyroDps[i].length()) >= gyroStillDps
                || std::abs(double(s.accelG[i].length()) - 1.0) >= accelTolG)
                still[i] = 0;
        }
        // Streams shorter than the grid leave the tail unjudged — not still.
        for (size_t i = m; i < n; ++i)
            still[i] = 0;
    }
    return still;
}

double refineExtremum(const std::vector<double> &y, int i)
{
    if (i <= 0 || i >= int(y.size()) - 1)
        return double(i);
    const double a = y[size_t(i) - 1], b = y[size_t(i)], c = y[size_t(i) + 1];
    const double denom = a - 2.0 * b + c;
    if (std::abs(denom) <= 1e-12)
        return double(i);
    const double off = 0.5 * (a - c) / denom;
    return double(i) + std::clamp(off, -0.5, 0.5);
}

double refineZeroCrossing(const std::vector<double> &y, int i)
{
    if (i < 0 || i >= int(y.size()) - 1)
        return double(i);
    const double a = y[size_t(i)], b = y[size_t(i) + 1];
    if (a == 0.0)
        return double(i);
    if ((a > 0.0) == (b > 0.0) || a == b)
        return double(i);   // not a straddle — leave at the sample
    return double(i) + a / (a - b);
}

int64_t fracIndexToUs(const std::vector<int64_t> &grid, double fidx)
{
    if (grid.empty())
        return 0;
    if (fidx <= 0.0)
        return grid.front();
    if (fidx >= double(grid.size() - 1))
        return grid.back();
    const int    lo = int(fidx);
    const double f  = fidx - double(lo);
    return grid[size_t(lo)]
         + int64_t(std::llround(f * double(grid[size_t(lo) + 1] - grid[size_t(lo)])));
}

} // namespace pinpoint::analysis::phase_signals
