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

// THE CLUB EVIDENCE RULE — whether a shot's own launch monitor readings contradict the club
// it was recorded under. Pure, header-only, QtCore only (QString). Sibling of
// lm_session_reductions.h and lm_inferred_reads.h; same rule: QML draws, C++ decides.
//
// WHY THE PANEL NEEDS IT. The device's club is whatever was selected on the device, and a
// golfer who forgets to change it records a session of "Driver" shots hit with a 7 iron. The
// launch monitor board then scopes by that label, so a 7-iron strike is graded against the
// driver corridors and averaged with the real drives: on the 4 Aug fixture (Wrist_05) shot 8
// read "Heel" against the Driver scope for exactly this reason. The readings themselves
// know better — an iron does not launch with 12° of dynamic loft and 1.45 smash, and a
// driver does not come off at 35° with 5,000 rpm.
//
// THE RULE (docs/design/lm_panel_fsx.md, "Club evidence"), three tests, each voting WOOD,
// IRON or abstaining:
//   dynamic loft   ≤ 17°  says wood,   ≥ 20°  says iron
//   smash factor   ≥ 1.42 says wood,   ≤ 1.40 says iron
//   spin rate      ≤ 3500 says wood,   ≥ 4000 says iron
// TWO VOTES DECIDE. A shot with one vote each way, or one vote and two abstentions, reads as
// neither and nothing is said about it — a single test is too easy to fool (a thin iron
// comes off at driver spin; a toed driver at iron smash), and the gaps between the two
// thresholds of each test are where the families genuinely overlap.
//
// WHO IS CHECKED, and the one place this is narrower than "wood vs iron". Only a shot
// recorded as a DRIVER (wood family) or as an IRON or WEDGE (iron family) is checked.
// Fairway woods and hybrids are NOT: a 5 wood launches at 20°+ dynamic loft with 4,500 rpm,
// so it would collect two iron votes and be "corrected" to an iron it never was. The mock
// made the same choice ("Woods, hybrids and unknown clubs are never checked"). An unknown
// label is never checked either — this rule may relabel a club it can name, never guess at
// one it cannot.
//
// WHAT IT SUGGESTS. The session's most common RECORDED club of the other family — the
// driver shots of a session that also holds seventeen 7-iron shots read like the 7 iron,
// not like "an iron". Fairway woods DO count as candidates on the wood side (a 7-iron shot
// that reads wood-like in a session of 3-wood shots is the 3 wood); only the checking is
// narrow. When the session holds no club of the other family, the suggestion is GENERIC —
// "an iron", "a wood" — and `specific` is false: the shot is flagged, never relabelled,
// because relabelling to a club nobody hit would replace one wrong label with another.
//
// NOTHING HERE RELABELS ANYTHING. This answers "what do the readings say"; the model decides
// what to do with it (LmSessionModel: a specific suggestion becomes the effective club,
// shown in a note with Undo, and the user's choice always wins).
//
// Unit-tested standalone in src/Analysis/tests/lm_club_evidence_test.cpp.

#include <QRegularExpression>
#include <QString>

#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// The thresholds, as data. Named so a test quotes them and a change is a visible edit.
struct LmClubEvidenceRule {
    double loftWoodMax  = 17.0;    // dynamic loft, degrees
    double loftIronMin  = 20.0;
    double smashWoodMin = 1.42;
    double smashIronMax = 1.40;
    double spinWoodMax  = 3500.0;  // rpm
    double spinIronMin  = 4000.0;
    int    votesToDecide = 2;
};

enum class LmClubFamily { None, Wood, Iron };

inline QString lmClubFamilyName(LmClubFamily f)
{
    switch (f) {
    case LmClubFamily::Wood: return QStringLiteral("wood");
    case LmClubFamily::Iron: return QStringLiteral("iron");
    case LmClubFamily::None: break;
    }
    return QString();
}

// ── Club labels ─────────────────────────────────────────────────────────────────────
//
// THE ONE PLACE A CLUB LABEL IS READ AS A HEAD. LmClubArt::headFor() answers from here, so
// the artwork the panel draws and the family this rule checks can never disagree about what
// "3 WOOD" is. The labels it must read are the bag vocabulary (club_vocabulary.h: "DRIVER",
// "3 WOOD", "4 HYBRID", "7 IRON", "PITCHING WEDGE"…), the short forms a device or an older
// document may carry ("7i", "3w", "PW", "56°"), and anything else, which draws as the iron.
//
//   driver   driver, "dr", "1w"
//   3wood    any wood, "3w"/"5w"           (the one fairway render stands for every wood)
//   hybrid   hybrid, rescue, "4h"
//   wedge    pitching / gap / sand / lob wedge, "PW"/"GW"/"SW"/"LW"/"AW"/"UW", a loft in
//            degrees ("56°", "56 deg", or a bare 40–69)
//   iron     irons 2–9, and anything unrecognised (it is the neutral head to draw)
inline QString lmClubHead(const QString &club)
{
    const QString s = club.trimmed().toLower();
    static const QRegularExpression reDriver(QStringLiteral("driver|^dr$|^1\\s*w(ood)?$"));
    static const QRegularExpression reHybrid(QStringLiteral("hybrid|rescue|^\\d+\\s*h(y|yb)?$"));
    static const QRegularExpression reWood(QStringLiteral("wood|^\\d+\\s*w$"));
    static const QRegularExpression reWedge(
        QStringLiteral("wedge|^[pgaslu]w$|^(pitch|gap|sand|lob)|°|deg|^[4-6]\\d$"));
    if (reDriver.match(s).hasMatch()) return QStringLiteral("driver");
    if (reHybrid.match(s).hasMatch()) return QStringLiteral("hybrid");
    if (reWood.match(s).hasMatch())   return QStringLiteral("3wood");
    if (reWedge.match(s).hasMatch())  return QStringLiteral("wedge");
    return QStringLiteral("iron");
}

// Whether a label actually NAMES an iron (rather than falling through to the iron head
// because nothing else matched). The art may draw an unknown club as an iron; this rule
// may not check one as an iron.
inline bool lmClubNamesIron(const QString &club)
{
    const QString s = club.trimmed().toLower();
    static const QRegularExpression reIron(QStringLiteral("iron|^[1-9]\\s*i$"));
    return reIron.match(s).hasMatch() && lmClubHead(club) == QLatin1String("iron");
}

// The family a shot recorded under this label is CHECKED as — see the header comment for
// why only the driver, the irons and the wedges are.
inline LmClubFamily lmCheckedFamily(const QString &club)
{
    const QString head = lmClubHead(club);
    if (head == QLatin1String("driver")) return LmClubFamily::Wood;
    if (head == QLatin1String("wedge"))  return LmClubFamily::Iron;
    if (lmClubNamesIron(club))           return LmClubFamily::Iron;
    return LmClubFamily::None;
}

// The family a label is a CANDIDATE for when a shot of the other family reads like it:
// every wood (driver and fairway), every iron and wedge. Hybrids belong to neither.
inline LmClubFamily lmCandidateFamily(const QString &club)
{
    const QString head = lmClubHead(club);
    if (head == QLatin1String("driver") || head == QLatin1String("3wood")) return LmClubFamily::Wood;
    if (head == QLatin1String("wedge") || lmClubNamesIron(club))           return LmClubFamily::Iron;
    return LmClubFamily::None;
}

// Bag order, driver → wedges, for a deterministic tie-break between two equally common
// candidates and for ordering clubs anywhere the panel lists them. The mock's bagRank().
inline int lmClubBagRank(const QString &club)
{
    const QString k = club.trimmed().toLower();
    if (k.contains(QLatin1String("driver"))) return 0;
    static const QRegularExpression reW(QStringLiteral("(\\d+)\\s*(wood|w$)"));
    static const QRegularExpression reH(QStringLiteral("(\\d+)\\s*(hybrid|h$)"));
    static const QRegularExpression reI(QStringLiteral("(\\d+)\\s*(iron|i$)"));
    if (auto m = reW.match(k); m.hasMatch()) return 10 + m.captured(1).toInt();
    if (auto m = reH.match(k); m.hasMatch()) return 20 + m.captured(1).toInt();
    if (auto m = reI.match(k); m.hasMatch()) return 30 + m.captured(1).toInt();
    static const QRegularExpression rePw(QStringLiteral("pitch|^pw"));
    static const QRegularExpression reWedge(QStringLiteral("wedge|^\\d\\d$"));
    if (rePw.match(k).hasMatch())    return 50;
    if (reWedge.match(k).hasMatch()) return 55;
    return 90;
}

// ── The votes ───────────────────────────────────────────────────────────────────────

struct LmClubVotes {
    int wood = 0;   // tests voting wood
    int iron = 0;   // tests voting iron
    int of   = 0;   // tests that had a reading to vote with (0–3)
};

// One shot's three tests. A missing or non-finite reading abstains — it is not a vote for
// either family, and it does not count toward `of`.
inline LmClubVotes lmClubVotes(std::optional<double> dynamicLoft,
                               std::optional<double> smash,
                               std::optional<double> spin,
                               const LmClubEvidenceRule &r = {})
{
    LmClubVotes v;
    if (dynamicLoft && std::isfinite(*dynamicLoft)) {
        ++v.of;
        if (*dynamicLoft <= r.loftWoodMax)      ++v.wood;
        else if (*dynamicLoft >= r.loftIronMin) ++v.iron;
    }
    if (smash && std::isfinite(*smash)) {
        ++v.of;
        if (*smash >= r.smashWoodMin)      ++v.wood;
        else if (*smash <= r.smashIronMax) ++v.iron;
    }
    if (spin && std::isfinite(*spin)) {
        ++v.of;
        if (*spin <= r.spinWoodMax)      ++v.wood;
        else if (*spin >= r.spinIronMin) ++v.iron;
    }
    return v;
}

// Two votes decide. "More than the other side" as well, so a rule tuned to need one vote
// still could not read a 1–1 split as a verdict.
inline LmClubFamily lmReadsLike(const LmClubVotes &v, const LmClubEvidenceRule &r = {})
{
    // gate rule: PP_GATE("rule.lm.clubVotes")
    if (v.wood >= r.votesToDecide && v.wood > v.iron) return LmClubFamily::Wood;
    if (v.iron >= r.votesToDecide && v.iron > v.wood) return LmClubFamily::Iron;
    return LmClubFamily::None;
}

// ── The session ─────────────────────────────────────────────────────────────────────

// One shot, as the rule needs it: the club it was RECORDED under (never the effective one
// — the evidence must not feed on its own conclusions) and the three readings.
struct LmEvidenceShot {
    QString               recordedClub;
    std::optional<double> dynamicLoft;
    std::optional<double> smash;
    std::optional<double> spin;
};

// What the readings say about one shot.
struct LmClubEvidence {
    LmClubFamily reads    = LmClubFamily::None;  // the family two votes decided, or None
    int          votes    = 0;                   // the votes FOR `reads` (0 when it is None)
    int          of       = 0;                   // tests that had a reading
    LmClubVotes  tally;                          // the full count, for the evidence words
    bool         mismatch = false;               // checked, and `reads` is the other family
    // The club the readings point at: a club the session holds (specific), or "an iron" /
    // "a wood" when it holds none (generic). Empty unless `mismatch`.
    QString      suggest;
    bool         specific = false;               // `suggest` names a club → may relabel
};

// Every shot's evidence, index for index with `shots`. The candidates are counted over the
// whole session's RECORDED clubs, so the same session always suggests the same club whatever
// order its shots arrive in; a tie goes to the club earlier in the bag.
inline std::vector<LmClubEvidence> lmClubEvidence(const std::vector<LmEvidenceShot> &shots,
                                                  const LmClubEvidenceRule &r = {})
{
    // The most common recorded club of each family, by count, then bag order.
    auto mostCommon = [&shots](LmClubFamily fam) -> QString {
        std::map<QString, std::pair<int, QString>> counts;   // upper-cased → (count, first spelling)
        for (const LmEvidenceShot &s : shots) {
            const QString c = s.recordedClub.trimmed();
            if (c.isEmpty() || lmCandidateFamily(c) != fam) continue;
            auto &e = counts[c.toUpper()];
            if (e.first == 0) e.second = c;
            ++e.first;
        }
        QString best;
        int     bestN = 0, bestRank = 1 << 30;
        for (const auto &[key, e] : counts) {
            const int rank = lmClubBagRank(e.second);
            if (e.first > bestN || (e.first == bestN && rank < bestRank)) {
                best = e.second; bestN = e.first; bestRank = rank;
            }
        }
        return best;
    };
    const QString woodClub = mostCommon(LmClubFamily::Wood);
    const QString ironClub = mostCommon(LmClubFamily::Iron);

    std::vector<LmClubEvidence> out;
    out.reserve(shots.size());
    for (const LmEvidenceShot &s : shots) {
        LmClubEvidence e;
        e.tally = lmClubVotes(s.dynamicLoft, s.smash, s.spin, r);
        e.of    = e.tally.of;
        e.reads = lmReadsLike(e.tally, r);
        e.votes = e.reads == LmClubFamily::Wood ? e.tally.wood
                : e.reads == LmClubFamily::Iron ? e.tally.iron : 0;

        const LmClubFamily checked = lmCheckedFamily(s.recordedClub);
        e.mismatch = checked != LmClubFamily::None && e.reads != LmClubFamily::None
                     && e.reads != checked;
        if (e.mismatch) {
            const QString club = e.reads == LmClubFamily::Iron ? ironClub : woodClub;
            if (!club.isEmpty()) {
                e.suggest  = club;
                e.specific = true;
            } else {
                e.suggest  = e.reads == LmClubFamily::Iron ? QStringLiteral("an iron")
                                                           : QStringLiteral("a wood");
            }
        }
        out.push_back(e);
    }
    return out;
}

} // namespace pinpoint::analysis
