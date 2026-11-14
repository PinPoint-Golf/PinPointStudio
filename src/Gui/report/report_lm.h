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

// THE SESSION REPORT'S LAUNCH MONITOR FIGURES, from ONE source (audit F-03; session_report_design.md §3, "Launch
// monitor figures: one source").
//
// Every launch monitor number the report prints — the cover's ball tiles, the numbers table and its Typical row, the
// swing grid's carry bars, the reference swing's carry tie-break and words, and the appendix — is read from the launch
// monitor board's own shots (LmSessionModel::boardShots(): pinpoint::lm::LmBoardShot), joined to the report's swings by
// folder. So the report honours what the golfer did on the board, the same way the board does:
//   - a shot LEFT OUT ("Leave out", review.excluded) is in no median, range, Typical value, carry bar or reference
//     tie-break; it is still listed in the tables, marked "out", and the cover says which swing it was;
//   - a shot RE-LABELLED (review.clubOverride, or a specific club-evidence relabel) is counted under its EFFECTIVE club
//     everywhere, marked "*" in the tables;
//   - the readings are the board's (the analysis' lm.* entries, as the import's gates left them), not the raw block.
// Which shots count is LmBoardShot::inFigures() — the board's rule, never restated here. Columns and units come from
// lm::fieldDefs(); every page names a reading by its GOLFER name (report_words.h lmLabel: "Smash", "Launch", "Club
// path" — the cover's tile, the numbers table's head, the appendix's head and summary row, the notes), never the board's
// compact on-screen heading (FieldDef::abbrev) — Mark, 14 Nov 2026. The golfer pages print whole units except smash
// factor and the club's delivery angles (§0 rule 5: "whole units, or the model's own decimals").
//
// Pure: no QObject, no file. SessionReportModel::build() fills it; report_lm_test drives it from a real
// LmSessionModel over an in-memory shot list.

#include "../launchmonitor/lm_session_model.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace pinpoint::report {

// One report swing's launch monitor facts: the board's shot for it, if the board has one.
struct LmSwing {
    int     number = 0;       // the report's swing number, 1-based
    QString ledgerClub;       // the diagnostics ledger's club — the club only when the board has no shot for the swing
    std::optional<pinpoint::lm::LmBoardShot> shot;

    QString club() const { return shot ? shot->club : ledgerClub; }   // the EFFECTIVE club
    bool    measured() const { return shot && !shot->values.isEmpty(); }
    bool    out() const { return shot && shot->excluded; }
    bool    inFigures() const { return shot && shot->inFigures(); }
    bool    relabelled() const { return shot && shot->relabelled(); }
    // The reading as the board has it, or NaN. inFigures() decides whether it may count.
    double  reading(const QString &key) const;
};

// The session's launch monitor figures as the report quotes them.
struct LmFigures {
    std::vector<LmSwing> swings;   // one per report swing, in report order
    QStringList clubs;             // the effective clubs of the swings, in bag order (the board's); a blank club left out
    QString     device;            // "GC Quad", or "" (the words say "the launch monitor")
    bool        leftHanded = false;
    int         measured = 0;      // swings the monitor read (the byline), left out or not
    int         counted = 0;       // swings in the figures: read, and not left out
    QList<int>  leftOut;           // report numbers of the swings left out, ascending
    bool        anyRelabelled = false;

    bool has() const { return measured > 0; }
    int  N() const { return int(swings.size()); }
    // The readings of `key` over the swings in the figures, in report order: `club` null for every club, else that
    // effective club's.
    std::vector<double> series(const QString &key, const QString *club = nullptr) const;
};

// Joins the board's shots to the report's swings. `swingDirs[k]` is report swing k + 1's folder (cleaned), and
// `ledgerClubs[k]` its ledger club.
LmFigures lmFiguresOf(const std::vector<pinpoint::lm::LmBoardShot> &board, const QStringList &swingDirs,
                      const QStringList &ledgerClubs, const QString &device, bool leftHanded);

// The precision the golfer pages print a reading at: whole units, except a ratio (smash factor) and the club's delivery
// angles, which keep the board's own decimals (lmDecimals).
int lmGolferDecimals(const QString &key);

// The cover's "The ball" block (§3: eight figures, per club when several) — null without a reading. Each tile carries
// its reading's `key` beside its `label`.
QVariant lmBallBlock(const LmFigures &f);

// "Your numbers" (page 8): a row per swing (left-out swings listed, "out"; re-labelled ones "*" and under their effective
// club), then a Typical row per club over the swings in the figures when `typical`. `cols` are the heads (golfer names),
// `keys` the reading under each ("" for Swing and Club). `habitsCells[k]`, when given, is
// swing k + 1's Habits cell; `workOnsPerSwing` how many work-ons it counts. Null without a reading.
QVariant lmNumbersBlock(const LmFigures &f, bool typical, const QStringList &habitsCells, int workOnsPerSwing);

// The swing grid's carry row: per swing the carry in the figures (a number) or null, and the label over it ("152",
// "out", "-"). Empty without a reading.
QVariantList lmGridCarry(const LmFigures &f);
QStringList  lmGridCarryText(const LmFigures &f);

// The reference swing's carries (Coaching/session_report.h referenceSwing): NaN for a swing not in the figures, so a
// left-out misread can neither win the tie-break nor be "your longest".
std::vector<double> lmReferenceCarry(const LmFigures &f);
// "152" / "-": a reading of report swing k + 1 for the reference swing's words — "-" when it is not in the figures.
QString lmReferenceFigure(const LmFigures &f, int k, const QString &key);

// The launch monitor appendix (§3, appendix A) from the board's `graphics` (LmSessionModel::graphics(), the figures it
// draws) and the same shots. Null without a reading.
QVariant lmAppendixBlock(const LmFigures &f, const QVariantMap &graphics);

} // namespace pinpoint::report
