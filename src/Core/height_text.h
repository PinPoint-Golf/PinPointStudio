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

// WHAT A TYPED HEIGHT MEANS — the athlete form's Height field, read into a number in the field's unit.
//
// The field sits beside an ft | cm toggle and its placeholder promises "e.g. 5'11\"" (ft) or "e.g. 180"
// (cm). Until 14 Nov 2026 the form read it with JavaScript parseFloat, which stops at the first
// non-digit: 5'11" was stored as 5 ft, so every height typed the way the placeholder asks became whole
// feet, and the 3-D skeleton, which is scaled by the height, was up to 11 in short.
//
// parseHeightText(text, unit) returns the height in `unit` ("cm", anything else is "ft", as
// AthleteController::saveAthlete reads it), or 0 when the text is blank or not a height. The form
// treats blank as "unknown" and refuses anything else that returns 0 (the field's error state), so a
// typo never replaces a stored height with 0. It reads:
//   · feet and inches   5'11"  5' 11"  5'11  5'11''  5′11″  5’11”  5ft 11in  5 ft 11 in  5 feet 11 inches
//                       5'  5ft  5'11.5"   (the inches below 12; whole feet when inches are given)
//   · inches alone      71"  71in  71 inches
//   · metric            180cm  180 cm  1.80m  1.80 m  1,80 m   (a comma is read as the decimal point)
//   · a bare number     5.92  180  — in the field's unit, so the value the form shows back for a stored
//                       height (decimal feet to 2 places, or cm to 1) reads as that same number.
// A height written in the other unit is converted, so 180 cm typed with the toggle on ft is 5.906 ft.
// Header-only and Qt Core only, in Core (Vocabulary: units) so a ctest can reach it without the GUI.

#include <QRegularExpression>
#include <QString>

namespace pinpoint {

inline double parseHeightText(const QString &text, const QString &unit)
{
    constexpr double kCmPerFoot = 30.48;
    const bool wantCm = unit == QLatin1String("cm");

    QString t = text.trimmed().toLower();
    // The marks a keyboard, autocorrect or a copy-paste gives for feet and inches, made straight first;
    // then two apostrophes (straight or curly, 5'11'' or 5’11’’) are the inch mark people type when the
    // keyboard has no double quote handy.
    for (const QChar c : {QChar(0x2019), QChar(0x2018), QChar(0x2032), QChar(0x00B4)})
        t.replace(c, u'\'');
    for (const QChar c : {QChar(0x201D), QChar(0x201C), QChar(0x2033)})
        t.replace(c, u'"');
    t.replace(QStringLiteral("''"), QStringLiteral("\""));
    t.replace(u',', u'.');
    if (t.isEmpty()) return 0.0;

    // A number: 5, 5.92, .92, 5. — digits only, never a sign.
    const QString num = QStringLiteral("(\\d+(?:\\.\\d*)?|\\.\\d+)");
    const QString feet = QStringLiteral("(?:'|ft|feet|foot)");
    const QString inch = QStringLiteral("(?:\"|in|inch|inches)");

    static const QRegularExpression bare(QStringLiteral("^%1$").arg(num));
    static const QRegularExpression feetInches(
        QStringLiteral("^%1\\s*%2(?:[\\s-]*%1\\s*%3?)?$").arg(num, feet, inch));
    static const QRegularExpression inchesOnly(QStringLiteral("^%1\\s*%2$").arg(num, inch));
    static const QRegularExpression centimetres(
        QStringLiteral("^%1\\s*(?:cm|centimet(?:er|re)s?)$").arg(num));
    static const QRegularExpression metres(QStringLiteral("^%1\\s*(?:m|met(?:er|re)s?)$").arg(num));

    double inUnit = 0.0;   // the height in the field's unit
    QRegularExpressionMatch m;
    if ((m = bare.match(t)).hasMatch()) {
        inUnit = m.captured(1).toDouble();
    } else if ((m = feetInches.match(t)).hasMatch()) {
        const QString f = m.captured(1), i = m.captured(2);
        double ft = f.toDouble();
        if (!i.isEmpty()) {
            const double in = i.toDouble();
            if (f.contains(u'.') || !(in < 12.0)) return 0.0;   // 5.5'6" or 5'13" is not a height
            ft += in / 12.0;
        }
        inUnit = wantCm ? ft * kCmPerFoot : ft;
    } else if ((m = inchesOnly.match(t)).hasMatch()) {
        const double ft = m.captured(1).toDouble() / 12.0;
        inUnit = wantCm ? ft * kCmPerFoot : ft;
    } else if ((m = centimetres.match(t)).hasMatch()) {
        const double cm = m.captured(1).toDouble();
        inUnit = wantCm ? cm : cm / kCmPerFoot;
    } else if ((m = metres.match(t)).hasMatch()) {
        const double cm = m.captured(1).toDouble() * 100.0;
        inUnit = wantCm ? cm : cm / kCmPerFoot;
    }
    return inUnit > 0.0 ? inUnit : 0.0;
}

} // namespace pinpoint
