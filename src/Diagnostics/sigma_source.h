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

#include <QString>

#include <cstdint>

namespace pinpoint::analysis {

// Where a reading's measurement σ came from (session_diagnostics_design.md §A8.3). ORDERED weakest
// first, so a budget built from several terms can carry the weakest of them with std::min.
//   None    — no σ was stated; the reading's verdict stays hard
//   Noise   — a series σ with sigmaKind Unspecified: frame-to-frame noise, a LOWER BOUND on the error
//   Series  — a σ the producer characterised for the whole curve (MetricSeries::sigma, sigmaKind set)
//   Reading — the producer's per-instant budget (PhaseSample::sigma, the shaft uncertainty work)
enum class SigmaSource : uint8_t { None = 0, Noise = 1, Series = 2, Reading = 3 };

inline QString sigmaSourceName(SigmaSource s)
{
    switch (s) {
    case SigmaSource::Reading: return QStringLiteral("reading");
    case SigmaSource::Series:  return QStringLiteral("series");
    case SigmaSource::Noise:   return QStringLiteral("noise");
    case SigmaSource::None:    break;
    }
    return QStringLiteral("none");
}

} // namespace pinpoint::analysis
