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

// STANDARD CLUB LENGTH AND LIE, driver → putter, for men, women and juniors — the defaults a club
// takes when nobody has measured it: a new club in the athlete's bag, and a swing that names its
// club but recorded no length (every camera swing before capture.club was persisted).
//
// THERE IS NO GOVERNING-BODY STANDARD. These are the figures the published charts and the makers'
// spec sheets agree on (2026-10-19):
//   - MEN'S LENGTHS: steel irons, graphite woods — driver 45.5", 7 iron 37", ½" per iron, the
//     wedges ¼" apart, putter 34". The app's factory bag used these already.
//   - WOMEN'S LENGTHS: one inch shorter, club for club (every chart says so; PING's G Le line runs
//     ½–1" shorter), putter 33".
//   - JUNIOR LENGTHS: by HEIGHT, not age — PING's Prodi G guideline, 48" to 66" tall in 3" steps
//     (fairwayjockey.com junior club length chart). The 3 and 4 iron, which junior sets do not
//     carry, extend the 5 iron by ½" a club, as adult sets space them.
//   - LIE: irons 60° (3 iron) to 63.5° (PW) in ½° steps — the middle of the published ranges (a 7
//     iron 62–63°); wedges 64°; hybrids 59–59.5° (PING G440 runs 57–59.5° across its hybrids);
//     woods 58.5–59°; putter 70°. Women and juniors take the men's lie: the sources disagree on
//     whether a women's set is flatter or more upright, and a shorter club at the same lie is what
//     the makers' own women's specs mostly show.

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>

namespace pinpoint {

enum class ClubSet { Men, Women, Junior };

struct ClubStandard {
    bool    valid = false;
    int     lengthMm = 0;
    double  lieDeg = 0.0;
    ClubSet set = ClubSet::Men;
};

namespace club_standards_detail {

struct Row { const char *id; double menIn; double womenIn; double lieDeg; int juniorCol; };

// juniorCol: the club's column in kJuniorIn (−1 = the 3 / 4 iron, from the 5 iron).
inline const std::array<Row, 17> &rows()
{
    static const std::array<Row, 17> k = { {
        { "DRIVER",         45.50, 44.50, 58.5,  0 },
        { "3 WOOD",         43.00, 42.00, 58.5,  1 },
        { "5 WOOD",         42.50, 41.50, 59.0,  2 },
        { "3 HYBRID",       40.75, 39.75, 59.0,  3 },
        { "4 HYBRID",       40.00, 39.00, 59.5,  4 },
        { "3 IRON",         39.00, 38.00, 60.0, -1 },
        { "4 IRON",         38.50, 37.50, 60.5, -1 },
        { "5 IRON",         38.00, 37.00, 61.0,  5 },
        { "6 IRON",         37.50, 36.50, 61.5,  6 },
        { "7 IRON",         37.00, 36.00, 62.0,  7 },
        { "8 IRON",         36.50, 35.50, 62.5,  8 },
        { "9 IRON",         36.00, 35.00, 63.0,  9 },
        { "PITCHING WEDGE", 35.75, 34.75, 63.5, 10 },
        { "GAP WEDGE",      35.50, 34.50, 64.0, 11 },
        { "SAND WEDGE",     35.25, 34.25, 64.0, 12 },
        { "LOB WEDGE",      35.00, 34.00, 64.0, 13 },
        { "PUTTER",         34.00, 33.00, 70.0, 14 },
    } };
    return k;
}

// PING Prodi G junior lengths (inches) by player height 48, 51, …, 66 in; columns: driver, fairway 1,
// fairway 2, hybrid 1, hybrid 2, 5–9 iron, PW, GW, SW, LW, putter.
inline const std::array<std::array<double, 15>, 7> &juniorIn()
{
    static const std::array<std::array<double, 15>, 7> k = { {
        { 33.50, 31.75, 31.25, 30.75, 30.50, 30.00, 29.50, 29.00, 28.50, 28.00, 27.75, 27.50, 27.50, 27.50, 26.00 },
        { 35.00, 33.25, 32.75, 32.50, 32.25, 31.50, 31.00, 30.50, 30.00, 29.50, 29.00, 28.75, 28.75, 28.75, 27.00 },
        { 37.00, 35.25, 34.75, 34.00, 33.75, 33.00, 32.50, 32.00, 31.50, 31.00, 30.50, 30.25, 30.25, 30.25, 28.50 },
        { 39.00, 37.25, 36.75, 36.00, 35.50, 34.50, 34.00, 33.50, 33.00, 32.50, 32.00, 31.75, 31.75, 31.75, 30.00 },
        { 41.00, 39.00, 38.50, 37.50, 37.00, 36.00, 35.50, 35.00, 34.50, 34.00, 33.50, 33.25, 33.25, 33.25, 32.00 },
        { 42.00, 40.00, 39.50, 38.50, 38.00, 37.00, 36.50, 36.00, 35.50, 35.00, 34.50, 34.25, 34.25, 34.25, 33.00 },
        { 43.00, 41.50, 41.00, 39.25, 38.75, 38.00, 37.50, 37.00, 36.50, 36.00, 35.50, 35.25, 35.25, 35.25, 34.00 },
    } };
    return k;
}

} // namespace club_standards_detail

// The standard length and lie of `clubId` (the club vocabulary's spelling, club_vocabulary.h) for a
// set. A junior's length comes from their HEIGHT (heightM, interpolated between the 3" rows and
// clamped to 48–66"); a junior with no height gets no standard. An unknown club is invalid.
inline ClubStandard standardClub(const QString &clubId, ClubSet set, double heightM = 0.0)
{
    using namespace club_standards_detail;
    ClubStandard out;
    out.set = set;
    for (const Row &r : rows()) {
        if (clubId.compare(QLatin1String(r.id), Qt::CaseInsensitive) != 0) continue;
        double in = set == ClubSet::Women ? r.womenIn : r.menIn;
        if (set == ClubSet::Junior) {
            if (!(heightM > 0.5)) return out;
            const double h = std::clamp(heightM / 0.0254, 48.0, 66.0);
            const int i0 = std::min(5, int((h - 48.0) / 3.0));
            const double w = (h - (48.0 + 3.0 * i0)) / 3.0;
            const int col = r.juniorCol >= 0 ? r.juniorCol : 5;
            const double extra = r.juniorCol >= 0 ? 0.0 : (r.id[0] == '3' ? 1.0 : 0.5);   // 3 / 4 iron
            in = juniorIn()[size_t(i0)][size_t(col)] * (1 - w) + juniorIn()[size_t(i0 + 1)][size_t(col)] * w + extra;
        }
        out.valid = true;
        out.lengthMm = int(std::lround(in * 25.4));
        out.lieDeg = r.lieDeg;
        return out;
    }
    return out;
}

// Which set an athlete's clubs default to: a junior (under 18 on the day) by height, else the women's
// set for "female", else the men's. "declined" or unknown takes the men's — the charts' own default.
inline ClubSet clubSetFor(const QString &sexToken, bool junior)
{
    if (junior) return ClubSet::Junior;
    return sexToken.compare(QLatin1String("female"), Qt::CaseInsensitive) == 0 ? ClubSet::Women : ClubSet::Men;
}

} // namespace pinpoint
