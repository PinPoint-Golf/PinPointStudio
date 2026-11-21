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

#pragma once

// THE RULE DATA AS THE FIRING READS IT, AND A SESSION SUMMARY'S RULES (refactor 3.7-e round 2; R-STAMP-1, review R1-B2):
// one rule for SessionDiagnostics' firing snapshot and for the catch-up's scan (WorkOnsController), so the summary's
// fingerprint (session_reduction.h summaryFingerprintOf) is computed alike by its writer and its reader.
// Domain model ring, header-only; no QObject, no store type.

#include "rule_data.h"
#include "session_repository.h"   // JsonFileRead
#include "../Coaching/club_distances.h"   // the club baseline file's reader
#include "../Diagnostics/engine/fire_findings.h"
#include "../Diagnostics/engine/session_reduction.h"

#include <QHash>
#include <QString>

#include <optional>

namespace pinpoint {

// The rule data's part of a firing's inputs: the pack and norms (owned), the grade policy (SET-15; "standard" when
// unset), the disabled sets (SET-06), the flight table — what SessionDiagnostics::makeSnapshot puts in.
inline analysis::FiringInputs firingInputsOf(const RuleDataSnapshot &rules)
{
    analysis::FiringInputs in;
    in.pack  = rules.pack;
    in.norms = rules.norms;
    if (!rules.gradePolicy.isEmpty())
        in.gradePolicy = rules.gradePolicy;
    in.disabledNormSets = rules.disabledNormSets;
    if (rules.flightNorms)
        in.flightNorms = *rules.flightNorms;
    return in;
}

// The athlete record's facts a session's norm cohort is read from (DEC-171), as stored: "dob|sex".
inline QString summaryCohortFacts(const QString &dob, const QString &sex)
{
    return dob + QLatin1Char('|') + sex;
}

// A session's frozen club baseline as its owner holds it (SessionDiagnostics' load): a current file's clubs; none for no
// file, an older rule's or an unreadable one. What the summary fingerprint's baseline stamp is taken over.
inline std::optional<QHash<QString, analysis::ClubBaselineEntry>> frozenClubBaselineOf(const JsonFileRead &f)
{
    if (!f.exists || !f.ok)
        return std::nullopt;
    const int schema = f.root.value(QStringLiteral("schema")).toInt(0);
    if (schema > 0 && schema < analysis::kClubBaselineSchemaVersion)
        return std::nullopt;
    bool ok = false;
    const analysis::ClubBaselineSnapshot snap = analysis::clubBaselineFromJson(f.root, &ok);
    if (!ok)
        return std::nullopt;
    return snap.clubs;
}

} // namespace pinpoint
