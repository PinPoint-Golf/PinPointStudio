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

#pragma once

#include <algorithm>
#include <cstdint>

// Shot arbiter — pure math, header-only, no Qt; unit-tested standalone in
// src/Analysis/tests (the session_summary.h precedent for Gui headers).
// ShotController owns one and supplies the QTimer: candidate → hold → fuse →
// commit. All on the GUI thread, so no locks.
//
// Every auto detector (IMU impact, acoustic onset, ball launch) reports an
// *estimated true-impact instant* (its arrival stamp minus its own latency)
// plus a confidence. The first report opens a hold window (kArbHoldMs); at
// the deadline the candidates are fused:
//   - commit when >=2 modalities agree within matchTolMs, OR a lone
//     candidate is strong (conf >= strongConf — its detector gates already
//     passed);
//   - the committed timestamp comes from the most authoritative agreeing
//     modality: Acoustic (sample-accurate pinpoint) > Imu > Ball (coarse);
//   - a refractory after each commit absorbs echoes the ShotProcessor busy
//     gate might miss around its edges.
// The manual SHOT button never enters the arbiter — ShotController commits
// it directly and only notes the commit here for the refractory.
namespace pinpoint {

// Order IS the timestamp-authority priority.
enum class ArbSource : uint8_t { Acoustic = 0, Imu = 1, Ball = 2 };

struct ArbiterConfig {
    int32_t holdMs       = 200;    // collect window after the first candidate
    int32_t matchTolMs   = 40;     // cross-modal agreement tolerance
    int32_t refractoryMs = 1500;   // min interval between commits
    float   strongConf   = 0.8f;   // lone-candidate commit threshold
};

struct ArbCandidate {
    ArbSource src  = ArbSource::Imu;
    int64_t   t_us = 0;        // estimated true-impact instant (nowMicros domain)
    float     conf = 0.0f;
};

class ShotArbiter {
public:
    struct Decision {
        bool      commit     = false;
        ArbSource src        = ArbSource::Acoustic;  // authoritative modality
        int64_t   t_us       = 0;
        float     conf       = 0.0f;
        int       modalities = 0;   // distinct modalities backing the commit
    };

    ShotArbiter() = default;
    explicit ShotArbiter(const ArbiterConfig &cfg) : m_cfg(cfg) {}

    ArbiterConfig       &config()       { return m_cfg; }
    const ArbiterConfig &config() const { return m_cfg; }

    bool    collecting() const { return m_count > 0; }
    int64_t deadlineUs() const { return m_deadlineUs; }   // valid while collecting

    // Report a candidate at host time nowUs. Returns true when this report
    // OPENED a hold window — the owner starts its deadline timer then.
    // Candidates inside the post-commit refractory are dropped.
    bool report(const ArbCandidate &c, int64_t nowUs)
    {
        if (inRefractory(nowUs))
            return false;
        const bool opened = (m_count == 0);
        if (opened)
            m_deadlineUs = nowUs + static_cast<int64_t>(m_cfg.holdMs) * 1000;
        if (m_count < kMaxCandidates)
            m_ring[m_count++] = c;
        return opened;
    }

    // Fuse and decide at the deadline; always returns to Idle.
    Decision decide(int64_t nowUs)
    {
        Decision d;
        const int n = m_count;
        m_count = 0;
        if (n == 0 || inRefractory(nowUs))
            return d;

        // Highest-confidence candidate per modality.
        bool         has[kModalities]  = {};
        ArbCandidate best[kModalities] = {};
        for (int i = 0; i < n; ++i) {
            const int k = static_cast<int>(m_ring[i].src);
            if (!has[k] || m_ring[i].conf > best[k].conf) {
                has[k]  = true;
                best[k] = m_ring[i];
            }
        }

        // Cross-modal agreement around the most authoritative modality first
        // (enum order IS priority), so the committed timestamp is the best
        // available estimate of the true impact instant.
        const int64_t tolUs = static_cast<int64_t>(m_cfg.matchTolMs) * 1000;
        for (int a = 0; a < kModalities; ++a) {
            if (!has[a]) continue;
            int   agree = 1;
            float cmax  = best[a].conf;
            for (int b = 0; b < kModalities; ++b) {
                if (b == a || !has[b]) continue;
                if (std::abs(best[a].t_us - best[b].t_us) <= tolUs) {
                    ++agree;
                    cmax = std::max(cmax, best[b].conf);
                }
            }
            if (agree >= 2) {
                d.commit     = true;
                d.src        = static_cast<ArbSource>(a);
                d.t_us       = best[a].t_us;
                d.conf       = cmax;
                d.modalities = agree;
                noteCommit(nowUs);
                return d;
            }
        }

        // Lone-strong: a single modality may commit when its detector gates
        // passed decisively. Highest-priority strong candidate wins.
        for (int a = 0; a < kModalities; ++a) {
            if (!has[a] || best[a].conf < m_cfg.strongConf) continue;
            d.commit     = true;
            d.src        = static_cast<ArbSource>(a);
            d.t_us       = best[a].t_us;
            d.conf       = best[a].conf;
            d.modalities = 1;
            noteCommit(nowUs);
            return d;
        }

        return d;
    }

    // Discard a pending window (e.g. trigger disarmed mid-hold).
    void cancel() { m_count = 0; }

    // Record a commit made outside the arbiter (the manual SHOT button) so
    // the refractory still applies to subsequent auto candidates.
    void noteCommit(int64_t nowUs)
    {
        m_hasCommit    = true;
        m_lastCommitUs = nowUs;
    }

private:
    static constexpr int kModalities    = 3;
    static constexpr int kMaxCandidates = 8;

    bool inRefractory(int64_t nowUs) const
    {
        return m_hasCommit &&
               nowUs - m_lastCommitUs <
                   static_cast<int64_t>(m_cfg.refractoryMs) * 1000;
    }

    ArbiterConfig m_cfg;
    ArbCandidate  m_ring[kMaxCandidates] = {};
    int           m_count = 0;
    int64_t       m_deadlineUs = 0;
    bool          m_hasCommit = false;
    int64_t       m_lastCommitUs = 0;
};

} // namespace pinpoint
