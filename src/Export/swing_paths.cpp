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

#include "swing_paths.h"

#include <QDate>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include "../Core/pp_debug.h"

namespace pinpoint {

QString SwingPaths::sanitise(const QString& raw)
{
    QString s = raw.trimmed();
    // Whitespace and path-hostile characters become '-'.
    static const QRegularExpression hostile(QStringLiteral("[^A-Za-z0-9._-]+"));
    s.replace(hostile, QStringLiteral("-"));
    // Collapse runs of separators and strip them from the ends.
    static const QRegularExpression runs(QStringLiteral("-{2,}"));
    s.replace(runs, QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-')) || s.startsWith(QLatin1Char('.')))
        s.removeFirst();
    while (s.endsWith(QLatin1Char('-')) || s.endsWith(QLatin1Char('.')))
        s.removeLast();
    s.truncate(64);
    return s.isEmpty() ? QStringLiteral("unknown") : s;
}

SwingPaths::Allocation SwingPaths::allocateSwingDir(const QString& libraryRoot,
                                                    const QString& athleteName,
                                                    const QString& athleteUuid,
                                                    const QString& namingPattern,
                                                    const QString& sessionTypeLabel)
{
    Allocation out;

    QString root = libraryRoot.trimmed();
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/swings");
        ppWarn() << "[SwingExport] athleteLibraryPath is empty — falling back to" << root;
    }

    // Athlete folder: name, then uuid, then "unknown".
    const QString athleteToken =
        sanitise(!athleteName.trimmed().isEmpty() ? athleteName : athleteUuid);
    const QString athleteDir = root + QLatin1Char('/') + athleteToken;

    const QString today = QDate::currentDate().toString(Qt::ISODate);

    // Compose the session-folder base name (pre-counter) per the naming pattern.
    // Tokens are filesystem-sanitised; the athlete token doubles as the name.
    const QString nameTok = athleteToken;
    const QString typeTok = sanitise(sessionTypeLabel.trimmed().isEmpty()
                                         ? QStringLiteral("session") : sessionTypeLabel);
    QString base;
    if (namingPattern == QLatin1String("date-type-name"))
        base = today + QLatin1Char('_') + typeTok + QLatin1Char('_') + nameTok;
    else if (namingPattern == QLatin1String("name-date-type"))
        base = nameTok + QLatin1Char('_') + today + QLatin1Char('_') + typeTok;
    else if (namingPattern == QLatin1String("date-only"))
        base = today;
    else   // "date-name-type" (default)
        base = today + QLatin1Char('_') + nameTok + QLatin1Char('_') + typeTok;

    // Session folder: resolved once per (root, athlete, day, base) and cached so
    // all swings of an app run share it; a new day / athlete / root / pattern /
    // session-type reallocates. A "_NN" counter keeps runs distinct.
    if (m_cachedSessionDir.isEmpty() || m_cachedRoot != root ||
        m_cachedAthleteUuid != athleteUuid || m_cachedDate != today ||
        m_cachedBase != base) {
        int maxIndex = 0;
        const QRegularExpression sessionRe(
            QStringLiteral("^%1_(\\d+)$").arg(QRegularExpression::escape(base)));
        const QStringList existing = QDir(athleteDir)
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& name : existing) {
            const auto m = sessionRe.match(name);
            if (m.hasMatch())
                maxIndex = std::max(maxIndex, m.captured(1).toInt());
        }
        m_cachedSessionId  = QStringLiteral("%1_%2")
                                 .arg(base)
                                 .arg(maxIndex + 1, 2, 10, QLatin1Char('0'));
        m_cachedSessionDir = athleteDir + QLatin1Char('/') + m_cachedSessionId;
        m_cachedRoot        = root;
        m_cachedAthleteUuid = athleteUuid;
        m_cachedDate        = today;
        m_cachedBase        = base;
        ppInfo() << "[SwingExport] session folder:" << m_cachedSessionDir;
    }

    // Swing folder: count existing swing_* dirs, then probe upward in case of
    // gaps or collisions until an unused name is found.
    QDir sessionDir(m_cachedSessionDir);
    int index = sessionDir.entryList({QStringLiteral("swing_*")},
                                     QDir::Dirs | QDir::NoDotAndDotDot).size() + 1;
    QString swingId  = QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0'));
    while (sessionDir.exists(swingId)) {
        ++index;
        swingId = QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0'));
    }

    const QString swingDir = m_cachedSessionDir + QLatin1Char('/') + swingId;
    if (!QDir().mkpath(swingDir)) {
        ppError() << "[SwingExport] failed to create" << swingDir;
        return out;   // swingDir empty signals failure
    }

    out.swingDir   = swingDir;
    out.sessionId  = m_cachedSessionId;
    out.swingId    = swingId;
    out.swingIndex = index;
    return out;
}

} // namespace pinpoint
