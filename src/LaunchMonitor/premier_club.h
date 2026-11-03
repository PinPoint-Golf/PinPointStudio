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

// Foresight Premier's club → PinPoint's club. Pure, header-only.
//
// Premier writes the club selected in Premier as a short code on every shot (`ClubName`):
// seen on 2 Nov 2026 were "4h", "9i", "4i", "5w" and "Gw". The number is the club and the
// letter its kind (w wood, h hybrid, i iron); wedges are a letter and "w". The driver and the
// putter were not seen, so both spellings Foresight uses elsewhere are accepted ("1w"/"Dr",
// "Pt"/"Putter").
//
// The answer is always one of clubVocabulary(), or empty. Empty means "a club PinPoint has no
// name for" (a 7 wood, a 2 iron, a degree-marked wedge) or a code not understood, and the
// caller then leaves PinPoint's club alone: a guess would replace one wrong label with another.
//
// FSX2020 is deliberately NOT mapped. Its LastShot.CSV names only the family ("1w", "Fw", "Irn",
// "Wdg"), and in the library it disagreed with the club actually hit on most shots: FSX2020's
// club is rarely changed, because nothing in it depends on the club. Premier's must be right for
// its own simulation.

#include "../Core/club_vocabulary.h"

#include <QRegularExpression>
#include <QString>

namespace pinpoint::lm {

inline QString premierClubToVocabulary(const QString &code)
{
    const QString c = code.trimmed().toLower();
    if (c.isEmpty())
        return {};

    QString name;
    if (c == QLatin1String("1w") || c == QLatin1String("dr") || c == QLatin1String("driver"))
        name = QStringLiteral("DRIVER");
    else if (c == QLatin1String("pt") || c == QLatin1String("putter"))
        name = QStringLiteral("PUTTER");
    else if (c == QLatin1String("pw")) name = QStringLiteral("PITCHING WEDGE");
    else if (c == QLatin1String("gw") || c == QLatin1String("aw")) name = QStringLiteral("GAP WEDGE");
    else if (c == QLatin1String("sw")) name = QStringLiteral("SAND WEDGE");
    else if (c == QLatin1String("lw")) name = QStringLiteral("LOB WEDGE");
    else {
        static const QRegularExpression numbered(QStringLiteral("^(\\d{1,2})([whi])$"));
        const QRegularExpressionMatch m = numbered.match(c);
        if (!m.hasMatch())
            return {};
        const QString kind = m.captured(2) == QLatin1String("w") ? QStringLiteral("WOOD")
                           : m.captured(2) == QLatin1String("h") ? QStringLiteral("HYBRID")
                                                                 : QStringLiteral("IRON");
        name = m.captured(1) + QLatin1Char(' ') + kind;
    }
    // Only a club PinPoint names: "7 WOOD" is a real club but not one of ours.
    return pinpoint::clubVocabulary().contains(name) ? name : QString();
}

} // namespace pinpoint::lm
