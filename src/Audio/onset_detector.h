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
#include <cmath>
#include <cstdint>

// Acoustic onset detector — pure math, header-only, no Qt, no allocation in
// push(). Unit-tested standalone in src/Audio/tests; the live wrapper
// (AcousticShotDetector) feeds it mono float samples at the device's native
// rate (44.1/48 kHz — the impact "click" is high-frequency, never STT's
// 16 kHz downmix).
//
// Per-sample pipeline (time-domain only; spectral-flux/HFC is a later
// refinement): one-pole high-pass (impact energy is high-frequency; rejects
// rumble and speech fundamentals) → instant-attack/exponential-release
// energy envelope → adaptive-threshold candidate → exponential-decay gate
// (a strike's envelope collapses within the decay window; speech and
// sustained tones do not — that is what rejects them) → refractory.
//
// Confirmation is therefore delayed by decayWindowMs; the reported onset
// sample stays the *first threshold crossing*, which is what makes audio the
// sample-accurate pinpoint modality.
namespace pinpoint {

struct OnsetDetectorConfig {
    float hpCutoffHz      = 1000.0f;  // one-pole high-pass corner
    float releaseMs       = 10.0f;    // envelope release time constant
    float noiseTauMs      = 500.0f;   // noise-floor EMA time constant
    float noiseFloorMin   = 1e-4f;    // floor on the adaptive noise estimate
    float thresholdFactor = 8.0f;     // candidate at env > factor × floor
    float decayWindowMs   = 30.0f;    // decay-gate evaluation horizon
    float decayRatioMax   = 0.5f;     // env at +window must drop below ratio × peak
    float refractoryMs    = 35.0f;    // min inter-onset after evaluation
};

struct OnsetResult {
    bool    onset       = false;
    int64_t onsetSample = 0;     // global index of the first threshold crossing
    float   confidence  = 0.0f;  // peak over threshold, clamped to [0,1]
};

// est_t* = recv_now − samplesAfterOnset/rate − deviceLatency. recvNowUs is
// stamped at buffer *receipt* on the audio thread (EventBuffer::nowMicros
// domain); samplesAfterOnset counts from the onset to the END of the buffer
// being processed when the onset confirmed.
inline int64_t estimateImpactUs(int64_t recvNowUs, int64_t samplesAfterOnset,
                                double rateHz, int64_t deviceLatencyUs)
{
    return recvNowUs
         - static_cast<int64_t>(std::llround(static_cast<double>(samplesAfterOnset) * 1e6 / rateHz))
         - deviceLatencyUs;
}

class OnsetDetector {
public:
    OnsetDetector() = default;
    explicit OnsetDetector(const OnsetDetectorConfig &cfg) : m_cfg(cfg) {}

    OnsetDetectorConfig       &config()       { return m_cfg; }
    const OnsetDetectorConfig &config() const { return m_cfg; }

    // (Re)derive per-sample coefficients for a rate and clear all state.
    // Must be called before push(); the wrapper calls it on format changes.
    void reset(double sampleRateHz)
    {
        m_rate = sampleRateHz;
        const float dt = 1.0f / static_cast<float>(sampleRateHz);
        const float rc = 1.0f / (2.0f * 3.14159265f * m_cfg.hpCutoffHz);
        m_hpA        = rc / (rc + dt);
        m_envRelease = std::exp(-dt / (m_cfg.releaseMs * 1e-3f));
        m_noiseAlpha = 1.0f - std::exp(-dt / (m_cfg.noiseTauMs * 1e-3f));
        m_decayWindowSamples = static_cast<int64_t>(
            std::llround(m_cfg.decayWindowMs * 1e-3 * sampleRateHz));
        m_refractorySamples = static_cast<int64_t>(
            std::llround(m_cfg.refractoryMs * 1e-3 * sampleRateHz));

        m_xPrev = m_yPrev = 0.0f;
        m_env   = 0.0f;
        m_noise = m_cfg.noiseFloorMin;
        m_n     = 0;
        m_tracking        = false;
        m_wasBelow        = true;
        m_refractoryUntil = 0;
    }

    double  sampleRate()       const { return m_rate; }
    int64_t samplesProcessed() const { return m_n; }

    // Feed one mono sample. Returns a confirmed onset decayWindowMs after its
    // threshold crossing (sustained sounds fail the decay gate and report
    // nothing).
    OnsetResult push(float x)
    {
        OnsetResult res;
        if (m_rate <= 0.0)
            return res;   // reset() not called yet

        // One-pole high-pass, then instant-attack / exponential-release envelope.
        const float y = m_hpA * (m_yPrev + x - m_xPrev);
        m_xPrev = x;
        m_yPrev = y;
        m_env = std::max(std::fabs(y), m_env * m_envRelease);

        const int64_t i = m_n++;

        if (m_tracking) {
            // Peak over the whole window — a second hit inside it folds into
            // this candidate instead of double-firing.
            m_peak = std::max(m_peak, m_env);
            if (i - m_candidateStart >= m_decayWindowSamples) {
                m_tracking        = false;
                m_refractoryUntil = i + m_refractorySamples;
                if (m_env < m_cfg.decayRatioMax * m_peak) {
                    res.onset       = true;
                    res.onsetSample = m_candidateStart;
                    res.confidence  = std::min(1.0f, 0.5f * m_peak / m_candidateThreshold);
                }
            }
            return res;
        }

        // Adaptive noise floor — frozen while tracking so the impact burst
        // cannot inflate its own threshold.
        m_noise += m_noiseAlpha * (m_env - m_noise);

        const float threshold =
            m_cfg.thresholdFactor * std::max(m_noise, m_cfg.noiseFloorMin);
        const bool above  = m_env > threshold;
        // A candidate must be an *attack* — a crossing from below. Without
        // this, the abrupt END of a sustained tone re-candidates while the
        // envelope is already high and then passes the decay gate (the
        // envelope does collapse there) — a false onset.
        const bool attack = above && m_wasBelow;
        m_wasBelow = !above;

        if (i < m_refractoryUntil)
            return res;

        if (attack) {
            m_tracking           = true;
            m_candidateStart     = i;
            m_peak               = m_env;
            m_candidateThreshold = threshold;
        }
        return res;
    }

private:
    OnsetDetectorConfig m_cfg;

    double  m_rate = 0.0;
    float   m_hpA = 0.0f, m_envRelease = 0.0f, m_noiseAlpha = 0.0f;
    int64_t m_decayWindowSamples = 0, m_refractorySamples = 0;

    float   m_xPrev = 0.0f, m_yPrev = 0.0f;
    float   m_env   = 0.0f;
    float   m_noise = 0.0f;
    int64_t m_n     = 0;

    bool    m_tracking = false;
    bool    m_wasBelow = true;
    int64_t m_candidateStart = 0;
    float   m_peak = 0.0f;
    float   m_candidateThreshold = 0.0f;
    int64_t m_refractoryUntil = 0;
};

} // namespace pinpoint
