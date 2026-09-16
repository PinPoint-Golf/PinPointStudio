/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#include "device_clock_mapper.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint {

const char *clockMapMethodName(ClockMapMethod m)
{
    switch (m) {
    case ClockMapMethod::None:      return "none";
    case ClockMapMethod::Envelope:  return "envelope";
    case ClockMapMethod::Latch:     return "latch";
    case ClockMapMethod::DevicePts: return "devicePts";
    case ClockMapMethod::HostArrival: return "hostArrival";
    }
    return "none";
}

void DeviceClockMapper::reset()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const Config cfg = m_cfg;
    // Reset every member to its default, keeping the configuration.
    m_haveOrigin = false; m_originX = 0; m_originH = 0; m_lastX = 0; m_lastAbsX = 0; m_haveLastAbsX = false;
    m_hull.clear(); m_sumX = 0; m_sumH = 0; m_n = 0; m_firstX = 0; m_windowStartX = 0;
    m_haveFit = false; m_slope = 1.0; m_anchorX = 0; m_anchorH = 0;
    m_shortBaseline = true; m_degenerate = false; m_implausible = false;
    m_pooledSlope = 1.0; m_pooledWeight = 0.0;
    m_haveLatch = false; m_latchHostUs = 0; m_latchDeviceUs = 0; m_latchRttUs = -1;
    m_haveMinLatency = false; m_minLatencyUs = 0;
    m_havePub = false; m_pubAbsX = 0; m_pubAbsH = 0; m_pubSlope = 1.0; m_warmStartAbsX = 0;
    m_haveLastOut = false; m_lastOut = 0; m_method = ClockMapMethod::None;
    m_frames = 0; m_frameIdGaps = 0; m_streams = 0; m_clampArrival = 0; m_clampMono = 0; m_lastFrameId = -1;
    m_recentLag.clear(); m_recentResid.clear(); m_recentHead = 0;
    m_cfg = cfg;
}

void DeviceClockMapper::seedLatch(int64_t hostUs, int64_t deviceNs, int64_t rttUs)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_haveLatch = true;
    m_latchHostUs = hostUs;
    m_latchDeviceUs = deviceNs / 1000;
    m_latchRttUs = rttUs;
    m_haveMinLatency = false;   // re-calibrated against the envelope at the next refit
}

// ── The window ──────────────────────────────────────────────────────────────────────────────────

void DeviceClockMapper::beginStream()
{
    // Fold the finished window's rate into the pooled estimate — only a real, non-degenerate fit votes.
    if (m_haveFit && m_n >= 64 && !m_shortBaseline && !m_degenerate && !m_implausible) {
        const double span = double(m_lastX - m_firstX);
        if (span > 0.0) {
            const double w = m_pooledWeight + span;
            m_pooledSlope = (m_pooledSlope * m_pooledWeight + m_slope * span) / w;
            m_pooledWeight = w;
        }
    }
    m_hull.clear();
    m_sumX = 0; m_sumH = 0; m_n = 0;
    m_haveFit = false;
    m_shortBaseline = true; m_degenerate = false; m_implausible = false;
    m_slope = (m_pooledWeight > 0.0) ? m_pooledSlope : 1.0;
}

static int64_t cross(const int64_t ox, const int64_t oh, const int64_t ax, const int64_t ah,
                     const int64_t bx, const int64_t bh)
{
    // Relative µs over a ≤ 2-minute window stay under 2^27, so the products fit comfortably.
    return (ax - ox) * (bh - oh) - (ah - oh) * (bx - ox);
}

void DeviceClockMapper::hullMakeRoom()
{
    if (m_hull.size() < m_cfg.hullMax)
        return;
    // Drop the interior vertex whose removal changes the chain least; keep both extremes (the baseline
    // the rate rests on). Same rule as wr_fit's hull_make_room().
    size_t best = 1;
    double bestDev = std::numeric_limits<double>::max();
    for (size_t j = 1; j + 1 < m_hull.size(); ++j) {
        const Pt &a = m_hull[j - 1], &b = m_hull[j], &c = m_hull[j + 1];
        const double dx = double(c.x - a.x);
        if (dx == 0.0) continue;
        const double chord = double(a.h) + (double(b.x - a.x) / dx) * double(c.h - a.h);
        const double dev = std::abs(chord - double(b.h));
        if (dev < bestDev) { bestDev = dev; best = j; }
    }
    m_hull.erase(m_hull.begin() + std::ptrdiff_t(best));
}

void DeviceClockMapper::observe(const Pt &p)
{
    if (m_n == 0) m_firstX = p.x;
    m_sumX += p.x; m_sumH += p.h; ++m_n;
    while (m_hull.size() >= 2) {
        const Pt &o = m_hull[m_hull.size() - 2], &a = m_hull.back();
        if (cross(o.x, o.h, a.x, a.h, p.x, p.h) <= 0) m_hull.pop_back();
        else break;
    }
    hullMakeRoom();
    m_hull.push_back(p);
}

void DeviceClockMapper::refit()
{
    if (m_hull.empty()) { m_haveFit = false; return; }
    const double seed = (m_pooledWeight > 0.0) ? m_pooledSlope : 1.0;
    m_shortBaseline = false; m_degenerate = false; m_implausible = false;

    auto anchorAt = [&](double s) {
        size_t best = 0; double bestV = 0.0;
        for (size_t k = 0; k < m_hull.size(); ++k) {
            const double v = double(m_hull[k].h) - s * double(m_hull[k].x);
            if (k == 0 || v < bestV) { bestV = v; best = k; }
        }
        m_anchorX = m_hull[best].x; m_anchorH = m_hull[best].h;
    };

    const int64_t span = m_lastX - m_firstX;
    if (m_n < 2 || span < m_cfg.minSpanUs) {
        m_slope = seed; anchorAt(m_slope);
        m_shortBaseline = true; m_haveFit = true;
        return;
    }

    // cost(s) = Σ(h − line) is linear in s given the sums; the optimum is on a hull edge.
    bool have = false; double bestCost = 0.0, bestSlope = seed; size_t bestEdge = 0;
    for (size_t k = 0; k + 1 < m_hull.size(); ++k) {
        const double dx = double(m_hull[k + 1].x - m_hull[k].x);
        if (dx <= 0.0) continue;
        const double s = double(m_hull[k + 1].h - m_hull[k].h) / dx;
        const double cost = (double(m_sumH) - double(m_n) * double(m_hull[k].h))
                          - s * (double(m_sumX) - double(m_n) * double(m_hull[k].x));
        if (!have || cost < bestCost) { have = true; bestCost = cost; bestSlope = s; bestEdge = k; }
    }
    if (!have) {
        m_slope = seed; anchorAt(m_slope); m_shortBaseline = true;
    } else if (std::abs(bestSlope - 1.0) * 1e6 > kMaxPlausiblePpm) {
        m_implausible = true; m_slope = seed; anchorAt(m_slope);
    } else {
        m_slope = bestSlope;
        m_anchorX = m_hull[bestEdge].x; m_anchorH = m_hull[bestEdge].h;
        const double edgeSpan = double(m_hull[bestEdge + 1].x - m_hull[bestEdge].x);
        if (m_n >= 64 && span > 0 && edgeSpan < 0.02 * double(span))
            m_degenerate = true;
    }
    m_haveFit = true;
}

double DeviceClockMapper::envelopeAt(int64_t x) const
{
    return double(m_anchorH) + m_slope * double(x - m_anchorX);
}

// ── Publication ─────────────────────────────────────────────────────────────────────────────────

int64_t DeviceClockMapper::publish(int64_t absTargetUs, double slope, int64_t absX, int64_t arrivalUs)
{
    if (!m_havePub) {
        m_havePub = true;
        m_pubAbsX = absX; m_pubAbsH = absTargetUs; m_pubSlope = slope;
        m_warmStartAbsX = absX;
    } else {
        // Where the published line puts this frame, and how far the target disagrees.
        const long double pubHere = m_pubAbsH + (long double)m_pubSlope * (long double)(absX - m_pubAbsX);
        const long double d = (long double)absTargetUs - pubHere;
        const bool warm = (absX - m_warmStartAbsX) >= m_cfg.warmupUs;
        long double corr = d;
        if (warm) {
            const double dtS = std::max(0.0, double(absX - m_pubAbsX)) / 1e6;
            const long double lim = m_cfg.maxSlewUsPerS * std::max(dtS, 1e-4);
            corr = std::clamp(d, -lim, lim);
        }
        m_pubAbsH = pubHere + corr;
        m_pubAbsX = absX;
        m_pubSlope = slope;
    }
    int64_t out = int64_t(std::llround(m_pubAbsH));
    if (out > arrivalUs) { out = arrivalUs; ++m_clampArrival; }
    if (m_haveLastOut && out <= m_lastOut) { out = m_lastOut + 1; ++m_clampMono; }
    m_haveLastOut = true;
    m_lastOut = out;
    return out;
}

void DeviceClockMapper::noteFrame(int64_t outUs, int64_t arrivalUs, int64_t frameId, double residualUs)
{
    ++m_frames;
    if (frameId >= 0) {
        if (m_lastFrameId >= 0 && frameId > m_lastFrameId + 1)
            m_frameIdGaps += frameId - m_lastFrameId - 1;
        m_lastFrameId = frameId;
    }
    const int64_t lag = arrivalUs - outUs;
    const int64_t res = int64_t(std::llround(residualUs));
    if (m_recentLag.size() < m_cfg.recentMax) {
        m_recentLag.push_back(lag);
        m_recentResid.push_back(res);
    } else {
        m_recentLag[m_recentHead] = lag;
        m_recentResid[m_recentHead] = res;
        m_recentHead = (m_recentHead + 1) % m_cfg.recentMax;
    }
}

int64_t DeviceClockMapper::map(int64_t deviceNs, int64_t arrivalUs, int64_t frameId)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const int64_t absX = deviceNs / 1000;

    // A device counter that went backwards is a camera restart: new origin, new window, pooled rate kept.
    // The published line is on the old counter, so it is re-anchored (still monotonic — see publish()).
    if (m_haveLastAbsX && absX < m_lastAbsX - m_cfg.resetJumpUs) {
        beginStream();
        m_haveOrigin = false;
        m_haveLatch = false;       // the latch was on the old counter
        m_havePub = false;
        m_lastFrameId = -1;
    }
    if (!m_haveOrigin) {
        m_haveOrigin = true;
        m_originX = absX; m_originH = arrivalUs;
        m_windowStartX = 0;
        ++m_streams;
        if (m_n != 0) beginStream();
    }
    // Out-of-order device time within a stream: deliver, never fit.
    Pt p{ absX - m_originX, arrivalUs - m_originH };
    const bool inOrder = (m_n == 0) || p.x >= m_lastX;
    if (inOrder) {
        if (p.x - m_windowStartX > m_cfg.windowUs) {
            beginStream();
            m_windowStartX = p.x;
        }
        observe(p);
        m_lastX = p.x;
        refit();
    }
    m_lastAbsX = absX; m_haveLastAbsX = true;

    // Envelope line at this frame (absolute host µs).
    const double envAbs = double(m_originH) + envelopeAt(p.x);

    // The latch calibrates the constant once the envelope has a baseline: envelope at the latch instant
    // minus the latch's host time. Clamped at 0 — a negative latency means the bracket was wrong.
    if (m_haveLatch && !m_haveMinLatency && m_haveFit && !m_shortBaseline) {
        const double envAtLatch = double(m_originH) + envelopeAt(m_latchDeviceUs - m_originX);
        const int64_t lat = int64_t(std::llround(envAtLatch - double(m_latchHostUs)));
        if (lat > kMaxPlausibleLatencyUs) {
            // No camera delivers this slowly on its fastest frame: the bracket is wrong, not the link.
            m_haveLatch = false;
        } else {
            m_minLatencyUs = std::max<int64_t>(0, lat);
            m_haveMinLatency = true;
            // The switch from the warm-up line to the calibrated one is a one-off at connect, not a drift
            // correction — let it land now rather than slew for tens of seconds.
            m_warmStartAbsX = absX;
        }
    }

    // ⚠ Never later than the envelope. The envelope is the earliest any frame has been delivered at this
    // device instant, so a capture instant above it is impossible — a latch that says otherwise is wrong.
    double target;
    if (m_haveLatch && m_haveMinLatency) {
        target = envAbs - double(m_minLatencyUs);
        m_method = ClockMapMethod::Latch;
    } else if (m_haveLatch) {
        // Warm-up: the latch line at the seeded rate, until the envelope can calibrate the constant.
        target = std::min(envAbs, double(m_latchHostUs) + m_slope * double(absX - m_latchDeviceUs));
        m_method = ClockMapMethod::Latch;
    } else {
        target = envAbs;
        m_method = ClockMapMethod::Envelope;
    }

    const int64_t out = publish(int64_t(std::llround(target)), m_slope, absX, arrivalUs);
    noteFrame(out, arrivalUs, frameId, double(arrivalUs) - envAbs);
    return out;
}

int64_t DeviceClockMapper::mapDirect(int64_t captureUs, int64_t arrivalUs, ClockMapMethod method,
                                     int64_t frameId)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_method = method;
    if (!m_haveOrigin) { m_haveOrigin = true; ++m_streams; }
    int64_t out = captureUs;
    if (out > arrivalUs) { out = arrivalUs; ++m_clampArrival; }
    if (m_haveLastOut && out <= m_lastOut) { out = m_lastOut + 1; ++m_clampMono; }
    m_haveLastOut = true;
    m_lastOut = out;
    noteFrame(out, arrivalUs, frameId, double(arrivalUs - out));
    return out;
}

DeviceClockStats DeviceClockMapper::stats() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    DeviceClockStats s;
    s.method = m_method;
    s.haveFit = m_haveFit;
    s.shortBaseline = m_shortBaseline;
    s.degenerate = m_degenerate;
    s.implausible = m_implausible;
    s.skewPpm = (m_slope - 1.0) * 1e6;
    s.fitSpanS = m_n > 0 ? double(m_lastX - m_firstX) / 1e6 : 0.0;
    s.latchRttUs = m_haveLatch ? m_latchRttUs : -1;
    s.minLatencyUs = m_haveMinLatency ? m_minLatencyUs : -1;
    s.frames = m_frames;
    s.frameIdGaps = m_frameIdGaps;
    s.streams = m_streams;
    s.clampedToArrival = m_clampArrival;
    s.clampedMonotonic = m_clampMono;

    auto pct = [](std::vector<int64_t> v, double q) -> int64_t {
        if (v.empty()) return 0;
        const size_t k = std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5));
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
        return v[k];
    };
    s.lagP50Us = pct(m_recentLag, 0.50);
    s.lagP99Us = pct(m_recentLag, 0.99);
    s.lagMaxUs = m_recentLag.empty() ? 0 : *std::max_element(m_recentLag.begin(), m_recentLag.end());
    s.residualP50Us = pct(m_recentResid, 0.50);
    s.residualP99Us = pct(m_recentResid, 0.99);
    return s;
}

} // namespace pinpoint
