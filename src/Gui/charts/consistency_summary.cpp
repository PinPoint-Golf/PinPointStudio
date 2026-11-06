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

#include "consistency_summary.h"

#include "session_consistency.h"
#include "session_series_model.h"
#include "session_warp.h"

#include "../../Core/pp_debug.h"
#include "../../Export/swing_store.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>

#include <algorithm>
#include <memory>

namespace pinpoint::charts {

namespace {

const QString kSummaryFile = QStringLiteral("consistency.json");

// Strike order, SessionSeriesModel::buildSnapshot's: the folder number, then the name for anything unnumbered.
void sortInStrikeOrder(QStringList &dirs)
{
    std::stable_sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        const int na = SessionSeriesModel::folderShotId(a), nb = SessionSeriesModel::folderShotId(b);
        if (na >= 0 && nb >= 0 && na != nb) return na < nb;
        if ((na >= 0) != (nb >= 0)) return na >= 0;
        return a < b;
    });
}

// The wall clock of a swing document, 0 without one.
qint64 wallclockMs(const QJsonObject &root)
{
    const QString text = root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("wallclock")).toString();
    const QDateTime when = QDateTime::fromString(text, Qt::ISODateWithMs);
    return when.isValid() ? when.toMSecsSinceEpoch() : 0;
}

bool stopped(const std::atomic<bool> *cancel) { return cancel && cancel->load(); }

} // namespace

QString consistencySummaryPath(const QString &sessionDir)
{
    return QDir(sessionDir).filePath(kSummaryFile);
}

// As the work-on record dates a session with no clock. One with no date at all would sort behind every other.
qint64 consistencyFolderDateMs(const QString &sessionDir)
{
    const QFileInfo fi(sessionDir);
    const QDate d = QDate::fromString(fi.fileName().left(10), Qt::ISODate);
    return d.isValid() ? d.startOfDay().toMSecsSinceEpoch() : fi.lastModified().toMSecsSinceEpoch();
}

QJsonObject consistencySummaryToJson(const ConsistencySummary &s)
{
    QJsonArray cells;
    for (const ConsistencyCell &c : s.cells)
        cells.append(QJsonObject{ { QStringLiteral("area"), c.area }, { QStringLiteral("column"), c.column },
                                  { QStringLiteral("word"), consistencyWordToken(c.word) } });
    return QJsonObject{
        { QStringLiteral("schema"),        kConsistencySummarySchema },
        { QStringLiteral("derivedFrom"),   QJsonObject{ { QStringLiteral("fingerprint"), s.fingerprint },
                                                        { QStringLiteral("ruleVersion"), s.ruleVersion } } },
        { QStringLiteral("shots"),         s.shots },
        { QStringLiteral("sessionDate"),   double(s.sessionDateMs) },
        { QStringLiteral("afterImpactMs"), s.afterImpactMs },
        { QStringLiteral("cells"),         cells },
    };
}

bool consistencySummaryFromJson(const QJsonObject &root, ConsistencySummary *out)
{
    if (root.value(QStringLiteral("schema")).toInt() != kConsistencySummarySchema) return false;
    const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
    if (cells.isEmpty()) return false;

    ConsistencySummary s;
    const QJsonObject from = root.value(QStringLiteral("derivedFrom")).toObject();
    s.fingerprint   = from.value(QStringLiteral("fingerprint")).toString();
    s.ruleVersion   = from.value(QStringLiteral("ruleVersion")).toInt();
    s.shots         = root.value(QStringLiteral("shots")).toInt();
    s.sessionDateMs = qint64(root.value(QStringLiteral("sessionDate")).toDouble());
    s.afterImpactMs = root.value(QStringLiteral("afterImpactMs")).toDouble();
    s.cells.reserve(size_t(cells.size()));
    for (const QJsonValue &v : cells) {
        const QJsonObject o = v.toObject();
        ConsistencyCell c;
        c.area   = o.value(QStringLiteral("area")).toString();
        c.column = o.value(QStringLiteral("column")).toInt(-1);
        c.word   = consistencyWordFromToken(o.value(QStringLiteral("word")).toString());
        if (c.word == ConsistencyWord::Blank || !consistencyApplies(c.area, c.column)) return false;
        s.cells.push_back(std::move(c));
    }
    *out = std::move(s);
    return true;
}

bool deriveConsistencySummary(const QString &sessionDir, int documents, const ConsistencyEnv &env,
                              const std::atomic<bool> *cancel, ConsistencySummary *out)
{
    const QDir dir(sessionDir);
    QStringList swings;
    for (const QString &name : dir.entryList(QStringList{ QStringLiteral("swing_*") },
                                             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        swings << dir.filePath(name);
    sortInStrikeOrder(swings);

    SessionSnapshot session;
    session.sessionDir = sessionDir;
    qint64 played = 0;
    out->unread = 0;
    int found = 0;
    for (const QString &swing : std::as_const(swings)) {
        if (stopped(cancel)) return false;
        if (!pinpoint::SwingStore::info(swing).exists()) continue;
        ++found;
        QString why;
        QJsonObject root = pinpoint::SwingStore::loadAnalysis(swing, &why);
        if (root.isEmpty()) {
            ppWarn() << "[Consistency] left out, could not read" << qPrintable(swing) << "-" << qPrintable(why);
            ++out->unread;
            continue;
        }
        if (!played) played = wallclockMs(root);
        std::shared_ptr<ShotSeries> shot = SessionSeriesModel::extractShot(root, swing, &why);
        root = QJsonObject();       // the document goes before the next one is read
        if (!shot) continue;        // not analysed: the tab skips it too
        shot->ordinal = session.count() + 1;
        session.shots.push_back(std::move(shot));
    }
    out->unread += std::max(0, documents - found);
    if (session.shots.empty() || stopped(cancel)) return false;

    const ConsistencyInput input = consistencyInput(session, alignSession(session), env);
    if (stopped(cancel)) return false;
    const ConsistencyResult result = consistencyResult(input);

    out->shots         = session.count();
    out->sessionDateMs = played ? played : consistencyFolderDateMs(sessionDir);
    out->afterImpactMs = input.afterImpactMs;
    out->cells.clear();
    out->cells.reserve(result.cells.size());
    for (const ConsistencyCell &c : result.cells) {
        ConsistencyCell kept;
        kept.area   = c.area;
        kept.column = c.column;
        kept.word   = c.word;
        out->cells.push_back(std::move(kept));
    }
    return true;
}

} // namespace pinpoint::charts
