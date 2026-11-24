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

// The one canonical golf-bag club vocabulary, driver → putter. SINGLE SOURCE OF
// TRUTH for every club picker in the app — the shot-carousel swing-edit popover
// (ShotListModel::clubOptions, persisted to swing.json review.club) and the Markup
// Lab metadata panel (MarkupController::clubOptions, persisted to truth.json
// meta.club). Both must offer the same labels so one physical swing never carries
// two differently-spelled club strings across its sidecars.
//
// Uppercase to match the carousel display font and stored club ids,
// so an unedited shot's club maps to a picker row. Adding/renaming a club here
// changes it everywhere at once.

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace pinpoint {

inline const QStringList &clubVocabulary()
{
    static const QStringList kClubs = {
        QStringLiteral("DRIVER"),
        QStringLiteral("3 WOOD"),
        QStringLiteral("5 WOOD"),
        QStringLiteral("3 HYBRID"),
        QStringLiteral("4 HYBRID"),
        QStringLiteral("3 IRON"),
        QStringLiteral("4 IRON"),
        QStringLiteral("5 IRON"),
        QStringLiteral("6 IRON"),
        QStringLiteral("7 IRON"),
        QStringLiteral("8 IRON"),
        QStringLiteral("9 IRON"),
        QStringLiteral("PITCHING WEDGE"),
        QStringLiteral("GAP WEDGE"),
        QStringLiteral("SAND WEDGE"),
        QStringLiteral("LOB WEDGE"),
        QStringLiteral("PUTTER"),
    };
    return kClubs;
}

// THERE IS NO CLUB STUB (refactor 6.2; R-CLUB-4, DEC-67; audit B-07, G-07). Until 6.2 capture seeded an undeclared
// club as "DRIVER", which read back as a declared driver: context_tree resolves anything containing "DRIVER" to the
// driver node, so the shot was graded against driver corridors at full confidence. An undeclared club is now EMPTY
// everywhere, and its findings fire as "club assumed". A document saved with the stub (review.club "DRIVER") is read
// as it was saved, as a declared driver (J-82: no migration), except on the launch monitor board (J-106, below).

// A stored club token mapped to the vocabulary above. Handles the legacy picker values
// ("Driver", "3-wood", "5-iron", "Wedge") so old data self-heals on read; canonical values pass
// through. Empty in, empty out; an unknown token comes back uppercased, matching no bag key.
inline QString canonicalClubId(const QString &raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty())
        return QString();

    const QStringList &vocab = clubVocabulary();
    if (vocab.contains(t))
        return t;

    QString up = t.toUpper();
    up.replace(u'-', u' ');
    up = up.simplified();                          // "3-wood" -> "3 WOOD"
    if (vocab.contains(up))
        return up;

    if (up == QLatin1String("WEDGE"))              // legacy catch-all wedge
        return QStringLiteral("SAND WEDGE");
    return up;
}

// ── THE ONE CLUB (refactor 6.2; information_model.md R-CLUB-1, R-CLUB-2, R-CLUB-4, DEC-133) ─────────────────────────
//
// A shot has four club SOURCE FACTS (R-CLUB-1), each kept where its owner wrote it and never overwritten by another:
//   captured    the session's club at the shot (capture.club.name; the Recording's analysisContext.club holds the same
//               club with its geometry). Empty = the session had none.
//   device      the club the launch monitor named on a late reading, which follow-club (SET-08) stores BESIDE the captured
//               club (capture.deviceClub, in capture.club's shape). Only follow-club writes it, and only when it is on, so
//               its presence is the "follow-club on" of DEC-133: a setting toggled later never re-reads a stored shot.
//   declared    the club the golfer picked on the swing-edit popover (review.club). Capture no longer seeds it.
//   correction  the club the golfer set on the launch monitor board (review.clubOverride).
//
// THE RULE (R-CLUB-2): the correction wins; else the declared club; else the device club; else the captured club; else
// the shot is UNDECLARED (empty, never a stub: R-CLUB-4). The club "before the correction" (the uncorrected club) is the
// same rule without its first step: the board's "recorded" club, the one a correction is made against. Every reader asks
// these: the findings (the phase grid's club), the board, the carousel, the Charts, the session's reductions and the
// analysis (effectiveContext(), src/Analysis/effective_context.h). Spellings are passed through TRIMMED, as stored;
// each reader canonicalises as it always has (canonicalClubId). The board's evidence suggestion is never one of the facts.
//
// Vocabulary ring: pure, no I/O, so every ring reads the one rule (the resolver it replaces lived here).
struct ClubFacts {
    QString captured;     // capture.club.name
    QString device;       // capture.deviceClub.name (follow-club)
    QString declared;     // review.club (the popover)
    QString correction;   // review.clubOverride (the board)
};

// The club before the golfer's correction: declared, else device, else captured; "" = undeclared.
inline QString uncorrectedClub(const ClubFacts &f)
{
    for (const QString *c : { &f.declared, &f.device, &f.captured }) {
        const QString t = c->trimmed();
        if (!t.isEmpty())
            return t;
    }
    return QString();
}

// THE one club, from the uncorrected club and the correction: the correction wins. "" = undeclared.
inline QString effectiveClub(const QString &uncorrected, const QString &correction)
{
    const QString c = correction.trimmed();
    return c.isEmpty() ? uncorrected.trimmed() : c;
}
inline QString effectiveClub(const ClubFacts &f) { return effectiveClub(uncorrectedClub(f), f.correction); }

// The four facts as a swing document holds them.
inline ClubFacts clubFactsOfDocument(const QJsonObject &root)
{
    const QJsonObject capture = root.value(QStringLiteral("capture")).toObject();
    const QJsonObject review  = root.value(QStringLiteral("review")).toObject();
    ClubFacts f;
    f.captured   = capture.value(QStringLiteral("club")).toObject().value(QStringLiteral("name")).toString().trimmed();
    f.device     = capture.value(QStringLiteral("deviceClub")).toObject().value(QStringLiteral("name")).toString().trimmed();
    f.declared   = review.value(QStringLiteral("club")).toString().trimmed();
    f.correction = review.value(QStringLiteral("clubOverride")).toString().trimmed();
    return f;
}

// A document's uncorrected club (the summary's `club`, the Charts' shot club before the board's annotation) and its
// effective club (the findings' club: the phase grid's). Empty = undeclared, never the stub.
inline QString swingDocDeclaredClub(const QJsonObject &root) { return uncorrectedClub(clubFactsOfDocument(root)); }
inline QString swingDocEffectiveClub(const QJsonObject &root) { return effectiveClub(clubFactsOfDocument(root)); }

// ── THE LEGACY STUB EXCEPTION (J-106; INTERIM, pending the Solution Architect; information_model.md R-CLUB-4's footnote) ──
//
// Until 6.2, capture seeded an undeclared club as review.club "DRIVER" (the stub). Such a shot is still read as saved, a
// declared driver (J-82), by every reader of the one club above. The ONE exception is the launch monitor board, which
// keeps its pre-6.2 behaviour for these shots only: when the readings point specifically at another club, the board
// counts the shot under that club (lm_session_model.cpp resolveClubs, source "evidence", with its Undo). Nothing else
// relabels by the evidence, and the exception writes nothing.
//
// A LEGACY STUB, as the document can tell: the popover's club reads DRIVER and nothing else declared the shot (no captured
// club, no device club), and it is not a device-only shot (whose review.club is the session's real club, never a seed).
// The correction is not part of the test: it outranks the exception on the board (its first step). New captures never
// write a stub. RESIDUAL (logged): a shot captured since 6.2 in a session with no club, on which the golfer then picks
// Driver on the popover, holds the same facts and reads as a stub; nothing stored tells the two apart.
inline bool swingDocIsLegacyClubStub(const QJsonObject &root)
{
    const ClubFacts f = clubFactsOfDocument(root);
    if (canonicalClubId(f.declared) != QLatin1String("DRIVER") || !f.captured.isEmpty() || !f.device.isEmpty())
        return false;
    const QString source = root.value(QStringLiteral("capture")).toObject().value(QStringLiteral("shotSource")).toString();
    return source != QLatin1String("launchMonitor");
}

} // namespace pinpoint
