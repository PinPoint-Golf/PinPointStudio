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

// THE GOLFER'S INPUT, RESOLVED — ONE RULE (J-33; refactor step 2.5's rule, moved here in step 3.5-i so its two readers
// share it: SessionDiagnosticsFiles::golferInput (src/Export, the old model, the work-ons, the tools) and
// SessionDiagnostics (src/DomainModel)). Values in, values out; nothing is read here.
//
// Each field from its NEW HOME — session_input.json (FILE-SESS-02) for the session level, the shot's review
// (FILE-SHOT-02) for the shot level — else diagnostics.json's OLDER COPY (the envelope's legacy golfer keys), until
// migration M3 moves them (FILE-SESS-03; H-04). A held key is the golfer's answer, an empty one included ("cleared");
// an absent one sends the reader to the older copy. So no golfer key is lost before M3 (§5.1).
//
// The older shape's keys are read exactly as the model always read them (step 2.5): `intent.focusConditionId` /
// `focusFromShot` / `declaredMiss`, `screens`, `intentFrom {fromShot, shape}`, `shotIntent {"<n>": "fade"}`,
// `corrections {"<n>": [{family, from, to}]}`.
//
// Domain model ring: names no store type.

#include "recording_repository.h"   // ShotReview, LabelCorrection
#include "session_input.h"
#include "../Diagnostics/derived/tendencies.h"   // shotIntentsFromJson, shotCorrectionsFromJson (the older keys' readers)

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace pinpoint::golfer_input {

// ── The older copy's keys ─────────────────────────────────────────────────────────────────────────────────

inline DeclaredFocus legacyFocus(const QJsonObject &legacy)
{
    const QJsonObject intent = legacy.value(QStringLiteral("intent")).toObject();
    return { intent.value(QStringLiteral("focusConditionId")).toString(),
             intent.value(QStringLiteral("focusFromShot")).toInt(-1) };
}

inline QString legacyMiss(const QJsonObject &legacy)
{
    return legacy.value(QStringLiteral("intent")).toObject().value(QStringLiteral("declaredMiss")).toString();
}

inline QHash<QString, bool> legacyScreens(const QJsonObject &legacy)
{
    QHash<QString, bool> out;
    const QJsonObject screens = legacy.value(QStringLiteral("screens")).toObject();
    for (auto it = screens.constBegin(); it != screens.constEnd(); ++it) out.insert(it.key(), it.value().toBool());
    return out;
}

inline IntentRun legacyRun(const QJsonObject &legacy)
{
    const QJsonObject run = legacy.value(QStringLiteral("intentFrom")).toObject();
    return { run.value(QStringLiteral("fromShot")).toInt(-1), run.value(QStringLiteral("shape")).toString() };
}

// Shot level, by swing number: an entry only where the older copy says something.
inline QHash<int, QString> legacyShotIntents(const QJsonObject &legacy)
{
    return analysis::shotIntentsFromJson(legacy.value(QStringLiteral("shotIntent")).toObject());
}

inline QHash<int, std::vector<analysis::ShotCorrection>> legacyShotCorrections(const QJsonObject &legacy)
{
    return analysis::shotCorrectionsFromJson(legacy.value(QStringLiteral("corrections")).toObject());
}

inline std::vector<analysis::ShotCorrection> toShotCorrections(const QVector<LabelCorrection> &list)
{
    std::vector<analysis::ShotCorrection> out;
    out.reserve(size_t(list.size()));
    for (const LabelCorrection &c : list) out.push_back(analysis::ShotCorrection{ c.family, c.from, c.to });
    return out;
}

// ── The resolution ────────────────────────────────────────────────────────────────────────────────────────

// The session's one intent run (one run today: the latest): the new home's list when it holds the key (an empty list
// is "no run"), else the older copy's. A run with fromShot < 0 or no shape covers nothing.
inline IntentRun resolveIntentRun(const SessionInput &in, const QJsonObject &legacy)
{
    if (in.intentRuns)
        return in.intentRuns->isEmpty() ? IntentRun() : in.intentRuns->last();
    return legacyRun(legacy);
}

// The session's declared focus, declared miss and screen answers (refactor step 3.5-ii: the diagnosis reads them): the
// new home's field when it holds the key, else the older copy's — SessionDiagnosticsFiles::golferInput's session level.
struct SessionWord {
    DeclaredFocus        focus;
    QString              miss;
    QHash<QString, bool> screens;
    friend bool operator==(const SessionWord &a, const SessionWord &b)
    {
        return a.focus == b.focus && a.miss == b.miss && a.screens == b.screens;
    }
};
inline SessionWord resolveSessionWord(const SessionInput &in, const QJsonObject &legacy)
{
    SessionWord w;
    w.focus = in.declaredFocus ? *in.declaredFocus : legacyFocus(legacy);
    w.miss  = in.declaredMiss ? *in.declaredMiss : legacyMiss(legacy);
    if (in.screenAnswers) {
        for (auto it = in.screenAnswers->constBegin(); it != in.screenAnswers->constEnd(); ++it)
            w.screens.insert(it.key(), it.value());
    } else {
        w.screens = legacyScreens(legacy);
    }
    return w;
}

// One shot's word over the older copy, field by field: a review that holds a key wins (empty: cleared, so no entry);
// one that does not leaves the older copy's entry. Returns whether the review held a shot-level key. A review that
// could not be read (no document) leaves both as they are.
inline bool applyReview(const ShotReview &review, int swing, QHash<int, QString> &intents,
                        QHash<int, std::vector<analysis::ShotCorrection>> &corrections)
{
    if (!review.ok) return false;
    bool fromReview = false;
    if (review.hasShotIntent) {
        fromReview = true;
        if (review.shotIntent.isEmpty()) intents.remove(swing);   // cleared, explicitly
        else                             intents.insert(swing, review.shotIntent);
    }
    if (review.hasLabelCorrections) {
        fromReview = true;
        if (review.labelCorrections.isEmpty()) corrections.remove(swing);   // cleared, explicitly
        else                                   corrections.insert(swing, toShotCorrections(review.labelCorrections));
    }
    return fromReview;
}

// The shot intent a shot's outcome reads: its own (resolved above), else the run that covers it (today's intentFor).
inline QString shotIntentFor(const QHash<int, QString> &intents, const IntentRun &run, int swing)
{
    const auto it = intents.constFind(swing);
    if (it != intents.constEnd()) return it.value();
    return (run.fromShot >= 0 && swing >= run.fromShot) ? run.shape : QString();
}

} // namespace pinpoint::golfer_input
