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

// THE ONE HANDEDNESS GLOSS (refactor 6.3b, audit G-02). Pure, header-only, Rules ring.
//
// HANDEDNESS MIRRORS THE GLOSS, NEVER THE SIGN. The readings, the rows and the fired conditions are target-relative
// and the same for either golfer: a ball that started right of the target started right. What changes for a
// left-hander is what that is CALLED: right of the target is a pull, a curve right is a draw, and the side words of a
// sentence swap. Every surface that names a side asks this header, once, with the golfer's handedness:
//
//   golferHand(shot, athlete)      THE HAND A SHOT IS NAMED IN: the shot's own recorded hand, else the athlete's
//                                  recorded hand (OBJ-ATHLETE-RECORD). A device-only shot records none (J-101)
//   golferIsLeftHanded(h)          the one reading of a stored handedness ("Left", any case; anything else is right)
//   glossedConditionId(id, h)      the condition a left-hander's row reads as (slice → hook, pull → push, …)
//   conditionNamesASide(id)        whether a condition's words name the ball's or the club's side (the table's ids)
//   conditionWordsInHand(id, t, h) a condition's sentence in the golfer's hand: mirrored only for a condition that
//                                  names a side, never any other (its "left" may be "remaining", its "right" "correct")
//   mirrorLeftRightWords(text)     a sentence's left / right words swapped (conditionWordsInHand's, for those ids only)
//   glossedSideIndex(i, left)      a 0 · 1 · 2 (left · middle · right) index as the golfer names it
//   glossedSign(v, left)           a signed reading's side as the golfer names it (in-to-out, open)
//
// Apply it exactly once, at the point a name or a sentence is made: the ids and indices it returns are already the
// golfer's, and glossing them again flips them back (a double flip passes every right-handed test).
//
// THE WORDS ARE GLOSSED BY THE CONDITION, NEVER BY THE WORD (refactor 6.3b fix round, B-1). Only the conditions the
// table maps (the start, the curve, the path and the face to the path) are about a side, and every left / right in
// their sentences names one. Any other condition's sentence is shown as authored: "weight left on the trail side",
// "the club is left behind", "how much of it is right for a golfer" are not sides, and a body condition's ball side
// ("points the swing left of the target") is the Coaching Lead's to word for a left-hander (NOTES-6.3b E-1).

#include <QHash>
#include <QRegularExpression>
#include <QString>

namespace pinpoint::analysis {

// A stored handedness, as every surface reads it: "Left" in any case is a left-hander; "Right", "" and anything
// else is not (a document written before handedness was recorded is right-handed, as it always was).
inline bool golferIsLeftHanded(const QString &handedness)
{
    return handedness.compare(QLatin1String("Left"), Qt::CaseInsensitive) == 0;
}

// THE HAND A SHOT IS NAMED IN (refactor 6.3b fix round, B-3; J-101): the shot's own recorded hand where it records
// one, else the athlete's recorded hand (OBJ-ATHLETE-RECORD). A device-only recording records no hand
// (swing_doc_writer), so without this its shots read right-handed for a left-hander while the launch monitor card,
// which reads the athlete record, reads them left-handed. Resolved in memory where a name is made, never stored.
inline QString golferHand(const QString &shotHandedness, const QString &athleteHandedness)
{
    return shotHandedness.isEmpty() ? athleteHandedness : shotHandedness;
}

namespace handedness_detail {
// The conditions that name a side, each with its mirror for a left-hander.
inline const QHash<QString, QString> &glossTable()
{
    static const QHash<QString, QString> kGloss = {
        { QStringLiteral("pull"),                QStringLiteral("push") },
        { QStringLiteral("push"),                QStringLiteral("pull") },
        { QStringLiteral("slice"),               QStringLiteral("hook") },
        { QStringLiteral("hook"),                QStringLiteral("slice") },
        { QStringLiteral("pull_hook"),           QStringLiteral("push_slice") },
        { QStringLiteral("push_slice"),          QStringLiteral("pull_hook") },
        { QStringLiteral("fade"),                QStringLiteral("draw") },
        { QStringLiteral("draw"),                QStringLiteral("fade") },
        { QStringLiteral("in_to_out_path"),      QStringLiteral("out_to_in_path") },
        { QStringLiteral("out_to_in_path"),      QStringLiteral("in_to_out_path") },
        { QStringLiteral("open_face_to_path"),   QStringLiteral("closed_face_to_path") },
        { QStringLiteral("closed_face_to_path"), QStringLiteral("open_face_to_path") },
    };
    return kGloss;
}
} // namespace handedness_detail

// The condition a left-hander's row should read as. Rows stay target-relative; a left-hander's ball going right is
// a hook and their out-to-in path is on the other side of zero.
inline QString glossedConditionId(const QString &id, const QString &handedness)
{
    if (!golferIsLeftHanded(handedness)) return id;
    return handedness_detail::glossTable().value(id, id);
}

// Whether a condition's words name a side: the ids the gloss maps, and only those (a row id or its glossed id).
inline bool conditionNamesASide(const QString &id)
{
    return handedness_detail::glossTable().contains(id);
}

// The left/right words of a sentence swapped, for a left-hander.
inline QString mirrorLeftRightWords(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\\b(left|right|Left|Right)\\b"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString w = m.captured(0);
        if (w == QLatin1String("left"))       out += QStringLiteral("right");
        else if (w == QLatin1String("right")) out += QStringLiteral("left");
        else if (w == QLatin1String("Left"))  out += QStringLiteral("Right");
        else                                  out += QStringLiteral("Left");
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

// A condition's sentence (its golfer phrase, consequence, story or playTo, already the GLOSSED condition's) as this
// golfer reads it: its left / right words mirrored for a left-hander when, and only when, the condition names a side.
// `id` is the row id or the glossed id (the table is symmetric). Every other sentence is returned as authored.
inline QString conditionWordsInHand(const QString &id, const QString &text, const QString &handedness)
{
    return golferIsLeftHanded(handedness) && conditionNamesASide(id) ? mirrorLeftRightWords(text) : text;
}

// A signed, target-relative reading as the golfer's side reads it: a right-hander's + (in-to-out, open to the path,
// curving right) is a left-hander's −. The reading itself is never changed; only the side it is named on.
inline double glossedSign(double value, bool leftHanded)
{
    return leftHanded ? -value : value;
}

// A three-way side index (0 left of the target or curving left, 1 neither, 2 right), as the golfer names it: for a
// left-hander, right of the target is the pull side, so the two ends swap. The shot-shape grids use it, so the lit
// cell agrees with the words printed beside it.
inline int glossedSideIndex(int index, bool leftHanded)
{
    return leftHanded && (index == 0 || index == 2) ? 2 - index : index;
}

} // namespace pinpoint::analysis
