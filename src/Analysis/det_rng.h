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

// A deterministic random source for the uncertainty Monte Carlo and bootstraps
// (shaft_uncertainty_propagation_design.md principle 7). std::mt19937_64's raw output is
// specified by the standard, so it is identical on every platform; std::normal_distribution
// and std::uniform_*_distribution are NOT (each library implements its own algorithm), and
// the byte-identity gates compare the Mac against the studio. So the uniforms and normals
// are made here, from raw words, by fixed formulas.

#include <cmath>
#include <cstdint>
#include <random>

namespace pinpoint::analysis {

class DetRng {
public:
    explicit DetRng(uint64_t seed) : m_eng(seed) {}

    // Uniform in [0, 1): the top 53 bits of one raw word.
    double uniform() { return double(m_eng() >> 11) * (1.0 / 9007199254740992.0); }

    // Uniform integer in [0, n), n > 0 — modulo of a raw word (the bias is < n / 2^64).
    uint64_t below(uint64_t n) { return n ? m_eng() % n : 0; }

    // Standard normal by Box–Muller; the second value of each pair is cached.
    double normal()
    {
        if (m_haveSpare) { m_haveSpare = false; return m_spare; }
        double u1 = uniform();
        while (u1 <= 0.0) u1 = uniform();
        const double u2  = uniform();
        const double r   = std::sqrt(-2.0 * std::log(u1));
        const double ang = 6.283185307179586476925 * u2;
        m_spare = r * std::sin(ang);
        m_haveSpare = true;
        return r * std::cos(ang);
    }

private:
    std::mt19937_64 m_eng;
    double m_spare = 0.0;
    bool   m_haveSpare = false;
};

} // namespace pinpoint::analysis
