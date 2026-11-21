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
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// WHAT THE ATHLETE LEVEL COULD NOT USE, IN A REPORT'S INPUTS STAMP (refactor 3.7-f; REVIEW-f R1-m7, R2-B1): read off the
// session's OWN athlete level (the report's door: the Athlete whose folder holds the session, any athlete, not only the
// current one), on the GUI thread. SessionReportModel's inputsStamp() and fingerprintFunction() (the archive's re-stamp,
// S-D6) both go through here, so the make, the state and the archive read one rule.
//   • "|athlete:left n": n sessions up to this one left out (J-51); empty when none: a complete report's fingerprint is
//     what it always was;
//   • "|athlete:unread": AT A MAKE only, when no athlete level reads the session (R1-m6). In the state and the archive no
//     owner adds nothing, so a correct report of a golfer no Athlete holds now is never marked out of date.

#include "report_tendencies.h"
#include "../../DomainModel/athlete_diagnostics.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

namespace pinpoint::report {

inline QString athleteLeftOutStamp(pinpoint::AthleteLevel *ad, const QString &sessionName, bool atMake)
{
    std::vector<pinpoint::analysis::SessionPlace> places;
    QStringList notUsed;
    if (ad) {
        for (const pinpoint::AthleteSessionSummary &s : ad->summaries()) places.push_back(s.place);
        notUsed = ad->notSummedUp().folders;
    }
    const QString stamp = tendenciesLeftOutStamp(ad != nullptr, places, notUsed, sessionName);
    return (!atMake && stamp == QLatin1String("|athlete:unread")) ? QString() : stamp;
}

// The stamps taken now, for another thread (fingerprintFunction's copy): per athlete folder, every session its athlete
// level lists. A session of none: nothing (not at a make).
class AthleteLeftOutStamps
{
public:
    static QString folderKey(const QString &folder)
    {
        return folder.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    }
    void take(pinpoint::AthleteLevel *ad, const QString &athleteFolder)
    {
        if (!ad) return;
        const QString key = folderKey(athleteFolder);
        for (const pinpoint::AthleteSessionSummary &s : ad->summaries())
            m_stamps.insert(key + QLatin1Char('/') + s.place.folder, athleteLeftOutStamp(ad, s.place.folder, false));
    }
    QString of(const QString &sessionDir) const
    {
        return m_stamps.value(folderKey(QFileInfo(sessionDir).absolutePath()) + QLatin1Char('/') + QFileInfo(sessionDir).fileName());
    }

private:
    QHash<QString, QString> m_stamps;
};

} // namespace pinpoint::report
