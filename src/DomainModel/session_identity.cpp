/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "session_identity.h"

#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace pinpoint {

const QUuid SessionIdentity::kLegacySessionNamespace =
    QUuid::fromString(QLatin1StringView("98a52a15-71c0-40f2-b377-f9a758154d28"));

namespace {

// The last and the second-to-last components of a path (string work only: no file system call).
QString lastComponent(const QString &path, int fromEnd)
{
    const QStringList parts = QDir::cleanPath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.size() > fromEnd ? parts.at(parts.size() - 1 - fromEnd) : QString();
}

QDate isoDay(const QString &token)
{
    static const QRegularExpression dayRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    return dayRe.match(token).hasMatch() ? QDate::fromString(token, Qt::ISODate) : QDate();
}

} // namespace

QString SessionIdentity::typeFromToken(const QString &token)
{
    static const QHash<QString, QString> byToken{
        { QStringLiteral("swing"), QStringLiteral("swing") },
        { QStringLiteral("wrist"), QStringLiteral("wrist") },
        { QStringLiteral("grf"), QStringLiteral("grf") },
        { QStringLiteral("coach"), QStringLiteral("coach") },
    };
    return byToken.value(token.toLower());
}

LegacyFolderName SessionIdentity::parseLegacyFolderName(const QString &name, const QString &athleteToken)
{
    LegacyFolderName out;
    QStringList tokens = name.split(QLatin1Char('_'));

    // The first pattern (165f10f1, 5–9 Jun 2026): "<date>_session-NN", untyped.
    static const QRegularExpression firstPattern(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})_session-(\\d+)$"));
    if (const auto m = firstPattern.match(name); m.hasMatch()) {
        out.day = isoDay(m.captured(1));
        if (out.day.isValid()) {
            out.typeToken = QStringLiteral("session");
            out.counter   = m.captured(2).toInt();
        }
        return out;
    }

    // The trailing _NN (every later pattern ends with it).
    if (tokens.size() >= 2) {
        bool      ok = false;
        const int n  = tokens.last().toInt(&ok);
        static const QRegularExpression digits(QStringLiteral("^\\d+$"));
        if (ok && n > 0 && digits.match(tokens.last()).hasMatch()) {
            out.counter = n;
            tokens.removeLast();
        }
    }

    // The date: the first token that is one, wherever the pattern put it.
    qsizetype dateAt = -1;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        if (const QDate d = isoDay(tokens.at(i)); d.isValid()) {
            out.day = d;
            dateAt  = i;
            break;
        }
    }
    if (dateAt < 0)
        return out;   // not a name this app made: no type is read from it
    tokens.removeAt(dateAt);
    if (tokens.isEmpty())
        return out;   // date-only

    // The athlete's own token (which may itself hold '_'), when the caller knows it, is removed
    // wherever it sits as whole tokens: what is left is the type.
    if (!athleteToken.isEmpty()) {
        const QStringList who = athleteToken.split(QLatin1Char('_'));
        for (qsizetype i = 0; i + who.size() <= tokens.size(); ++i) {
            if (tokens.mid(i, who.size()) == who) {
                QStringList rest = tokens.mid(0, i) + tokens.mid(i + who.size());
                out.typeToken = rest.join(QLatin1Char('_'));
                return out;
            }
        }
    }

    // Otherwise the type is a known type word at one end of what is left: the last token first
    // (date-name-type, the default, and name-date-type), then the first (date-type-name). The
    // launch monitor's untyped sessions say "session".
    const auto known = [](const QString &t) {
        return !typeFromToken(t).isEmpty() || t == QLatin1String("session");
    };
    if (known(tokens.last()))
        out.typeToken = tokens.last();
    else if (known(tokens.first()))
        out.typeToken = tokens.first();
    return out;
}

SessionId SessionIdentity::legacySessionId(const AthleteId &athlete, const QString &folderName)
{
    if (athlete.isNull() || folderName.isEmpty())
        return {};
    const QString name = athlete.toString() + QLatin1Char('/')
                         + folderName.normalized(QString::NormalizationForm_C);
    return SessionId(QUuid::createUuidV5(kLegacySessionNamespace, name.toUtf8()));
}

SessionIdentity SessionIdentity::fromSessionFile(const SessionId &id, const QString &folder, const QString &type,
                                                 const QDateTime &startedAtUtc, const QTimeZone &zone)
{
    SessionIdentity s;
    if (id.isNull())
        return s;
    s.m_id           = id;
    s.m_folder       = QDir::cleanPath(folder);
    s.m_type         = type;
    s.m_startedAtUtc = startedAtUtc.isValid() ? startedAtUtc.toUTC() : QDateTime();
    s.m_localDay     = s.m_startedAtUtc.isValid() ? s.m_startedAtUtc.toTimeZone(zone).date() : QDate();
    return s;
}

SessionIdentity SessionIdentity::fromLegacyFolder(const QString &folder, const AthleteId &athlete,
                                                  const QDateTime &startedAtUtc, const QTimeZone &zone)
{
    SessionIdentity s;
    const QString name = lastComponent(folder, 0);
    const SessionId id = legacySessionId(athlete, name);
    if (id.isNull())
        return s;
    const LegacyFolderName parsed = parseLegacyFolderName(name, lastComponent(folder, 1));
    s = fromSessionFile(id, folder, typeFromToken(parsed.typeToken), startedAtUtc, zone);
    if (!s.m_localDay.isValid())
        s.m_localDay = parsed.day;
    s.m_counter = parsed.counter;
    s.m_legacy  = true;
    return s;
}

QString SessionIdentity::folderName() const
{
    return lastComponent(m_folder, 0);
}

void SessionIdentity::assignDayOrder(QList<SessionIdentity> &sessions)
{
    QHash<QDate, QList<qsizetype>> byDay;
    for (qsizetype i = 0; i < sessions.size(); ++i) {
        sessions[i].m_nOfDay = 0;
        if (sessions.at(i).m_localDay.isValid())
            byDay[sessions.at(i).m_localDay].append(i);
    }
    for (auto it = byDay.begin(); it != byDay.end(); ++it) {
        QList<qsizetype> &day = it.value();
        const bool allTimed = std::all_of(day.cbegin(), day.cend(), [&](qsizetype i) {
            return sessions.at(i).m_startedAtUtc.isValid();
        });
        std::sort(day.begin(), day.end(), [&](qsizetype a, qsizetype b) {
            const SessionIdentity &x = sessions.at(a);
            const SessionIdentity &y = sessions.at(b);
            if (allTimed && x.m_startedAtUtc != y.m_startedAtUtc)
                return x.m_startedAtUtc < y.m_startedAtUtc;
            if (!allTimed && x.m_counter != y.m_counter)
                return x.m_counter < y.m_counter;
            return x.m_folder < y.m_folder;
        });
        for (qsizetype n = 0; n < day.size(); ++n)
            sessions[day.at(n)].m_nOfDay = int(n + 1);
    }
}

QString SessionIdentity::ordinal(int n)
{
    if (n <= 0)
        return {};
    const int mod100 = n % 100;
    const int mod10  = n % 10;
    const char *suffix = (mod100 >= 11 && mod100 <= 13) ? "th"
                         : mod10 == 1                   ? "st"
                         : mod10 == 2                   ? "nd"
                         : mod10 == 3                   ? "rd"
                                                        : "th";
    return QString::number(n) + QLatin1StringView(suffix);
}

QString SessionIdentity::label() const
{
    static const QHash<QString, QString> words{
        { QStringLiteral("swing"), QStringLiteral("Swing") },
        { QStringLiteral("wrist"), QStringLiteral("Wrist") },
        { QStringLiteral("grf"), QStringLiteral("GRF") },
        { QStringLiteral("coach"), QStringLiteral("Coach") },
    };
    QStringList parts{ words.value(m_type, QStringLiteral("Session")) };
    if (m_localDay.isValid())
        parts << m_localDay.toString(QStringLiteral("d MMM"));   // the C locale's month: "14 Nov"
    if (m_nOfDay > 0)
        parts << ordinal(m_nOfDay);
    return parts.join(QStringLiteral(" · "));
}

} // namespace pinpoint
