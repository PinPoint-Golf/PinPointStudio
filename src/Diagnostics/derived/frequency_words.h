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

// FREQUENCY WORDS — pure, header-only. The one rule for how often a share (0..1) of swings or shots is
// said to a golfer, read by Your swing (themeFrequency / themeHowOften, swing_themes.h) and by
// Tendencies (tendencyFrequencyWords, tendencies.h). Stage 0.13 (14 Nov 2026): Your swing used to floor
// at "more than half your swings", so a habit on a 0.39 share was said to be on more than half; and
// Tendencies kept its own copy of the thresholds. Now one band rule, words that never claim more than
// the share:
//
//   share ≥ everySwing (0.995)   every swing
//   share ≥ almostEvery (0.9)    almost every swing
//   share ≥ mostSwings (0.7)     most swings
//   share ≥ 0.5                  more than half your swings
//   share ≥ 0.4                  fairly often
//   share ≥ 0.25                 sometimes
//   otherwise (and NaN)          rarely
//
// The three upper thresholds are configurable (ThemeOptions carries them for Your swing and defaults
// them from FrequencyThresholds{}, which Tendencies reads); the lower three are fixed, since "half" is
// what the words say. A configured threshold is never let below the band beneath it, so a mis-set
// option cannot make "most swings" mean less than half.
//
// VERSIONS — the two readers persist differently. Your swing makes these words at publish from the
// stored share (swingSummaryView), so swing_themes.json holds no words and kThemeRuleVersion need not
// move for a wording change. Tendencies STORES them: tendencyFactorToJson writes each factor's
// "frequency" into <athlete>/tendencies.json, and tendencyFactorFromJson reads it back as stored, the
// file gated only by kTendencyRuleVersion. So ANY change here — a band, a word, a fixed threshold or
// the FrequencyThresholds{} defaults — needs kTendencyRuleVersion bumped, with a dated line, or Home's
// Tendencies keeps showing the old words.
//
// The Python reference (tools/themes/theme_summary.py, frequency_band) follows this rule.
// Unit-tested in src/Analysis/tests/swing_themes_test.cpp (every band over shares 0..1).

#include <QString>

#include <algorithm>

namespace pinpoint::analysis {

struct FrequencyThresholds {
    double everySwing  = 0.995;
    double almostEvery = 0.9;
    double mostSwings  = 0.7;
};

inline constexpr double kFrequencyMoreThanHalf = 0.5;
inline constexpr double kFrequencyFairlyOften  = 0.4;
inline constexpr double kFrequencySometimes    = 0.25;

enum class FrequencyBand { Every, AlmostEvery, Most, MoreThanHalf, FairlyOften, Sometimes, Rarely };

inline FrequencyBand frequencyBand(double share, const FrequencyThresholds &t = {})
{
    const double most   = std::max(t.mostSwings, kFrequencyMoreThanHalf);
    const double almost = std::max(t.almostEvery, most);
    const double every  = std::max(t.everySwing, almost);
    if (share >= every)                  return FrequencyBand::Every;
    if (share >= almost)                 return FrequencyBand::AlmostEvery;
    if (share >= most)                   return FrequencyBand::Most;
    if (share >= kFrequencyMoreThanHalf) return FrequencyBand::MoreThanHalf;
    if (share >= kFrequencyFairlyOften)  return FrequencyBand::FairlyOften;
    if (share >= kFrequencySometimes)    return FrequencyBand::Sometimes;
    return FrequencyBand::Rarely;
}

// The band said on its own: "every swing" … "rarely".
inline QString frequencyWords(FrequencyBand b)
{
    switch (b) {
    case FrequencyBand::Every:        return QStringLiteral("every swing");
    case FrequencyBand::AlmostEvery:  return QStringLiteral("almost every swing");
    case FrequencyBand::Most:         return QStringLiteral("most swings");
    case FrequencyBand::MoreThanHalf: return QStringLiteral("more than half your swings");
    case FrequencyBand::FairlyOften:  return QStringLiteral("fairly often");
    case FrequencyBand::Sometimes:    return QStringLiteral("sometimes");
    case FrequencyBand::Rarely:       break;
    }
    return QStringLiteral("rarely");
}

inline QString frequencyWords(double share, const FrequencyThresholds &t = {})
{
    return frequencyWords(frequencyBand(share, t));
}

} // namespace pinpoint::analysis
