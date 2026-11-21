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

// THE WRIST CHECKPOINT PROFILE'S ONE RULE (DEC-123; refactor step 3.8-ii): a shot's series and phases in, its
// WristProfile (wrist_profile.h) out. Moved from the Charts view's own grader (src/Gui/charts/session_wrist.cpp,
// shotWristPayload, through 3.8-ii) verbatim in what it decides:
//   · the series: every key that names a wrist DOF (dofForMetricKey), in the swing's own order, the FIRST copy of a
//     key kept; a series with no samples dropped; the face-on elbow (a camera reading of a sensor DOF) never on the
//     grid; per DOF the LAST key wins (buildWristAngleSource's rule);
//   · a missing sample is a gap, never 0: a non-finite value is unavailable, so the sampler never medians it in;
//   · a phase with no confidence reads 1.0;
//   · the wrist engine (WristAssessmentEngine::assess, default configuration) once with Auto, for the style it
//     resolves the swing to, and once per style for its bands; the rows in the display order (hinge, bow / cup,
//     forearm roll, trail wrist, lead elbow), only the DOFs the shot carries.
// Called by fireFindings() (the shot's findings, against the firing inputs' norms at the session's grade policy) and,
// with no band provider, by the Wrist chart for the ★ reference swing of another session (its Δ only: nothing graded;
// J-58 / J-ii3, today's behaviour).
//
// Rules ring: pure, no file, no QObject; any thread.

#include "wrist_profile.h"
#include "wrist_assessment_types.h"   // dofForMetricKey, isCameraWristDofSeries (inline)

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

namespace pinpoint::analysis {

class INormProvider;
struct GradePolicy;

// One shot's wrist readings, as its analysis carries them (any time origin: the engine reads differences).
struct WristInput {
    struct Series {
        QString key, routeId;
        std::vector<qint64> tUs;
        std::vector<double> value;   // NaN: no sample
    };
    struct Phase {
        int    phase = -1;
        qint64 tUs = 0;
        double conf = std::numeric_limits<double>::quiet_NaN();
    };
    std::vector<Series> series;      // every metric of the swing in document order (the rule picks the wrist ones)
    std::vector<Phase>  phases;
    // Some series names a wrist DOF and has samples (the profile would have a row).
    bool hasWristDof() const
    {
        for (const Series &s : series)
            if (!s.tUs.empty() && !s.value.empty() && dofForMetricKey(s.key) && !isCameraWristDofSeries(s.routeId)) return true;
        return false;
    }
};

// The analysis block's wrist metrics and its phases (FILE-SHOT-01 `analysis`), as the firing readers parse them: a
// non-number value is NaN (a gap), the times as written (inline: the readers link no engine).
inline WristInput wristInputOf(const QJsonObject &analysis)
{
    const auto num = [](const QJsonValue &v) { return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN(); };
    WristInput in;
    for (const QJsonValue &mv : analysis.value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject m = mv.toObject();
        WristInput::Series s;
        s.key = m.value(QStringLiteral("key")).toString();
        if (s.key.isEmpty() || !dofForMetricKey(s.key)) continue;   // only a wrist DOF can reach the profile
        s.routeId = m.value(QStringLiteral("routeId")).toString();
        const QJsonArray ts = m.value(QStringLiteral("t_us")).toArray();
        const QJsonArray vs = m.value(QStringLiteral("value")).toArray();
        const qsizetype n = std::min(ts.size(), vs.size());
        s.tUs.reserve(size_t(n));
        s.value.reserve(size_t(n));
        for (qsizetype i = 0; i < n; ++i) {
            s.tUs.push_back(qint64(ts.at(i).toDouble()));
            s.value.push_back(num(vs.at(i)));
        }
        in.series.push_back(std::move(s));
    }
    for (const QJsonValue &pv : analysis.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject p = pv.toObject();
        WristInput::Phase ph;
        ph.phase = p.value(QStringLiteral("phase")).toInt(-1);
        ph.tUs   = qint64(p.value(QStringLiteral("t_us")).toDouble());
        ph.conf  = num(p.value(QStringLiteral("conf")));
        in.phases.push_back(ph);
    }
    return in;
}

// The profile. `norms` null: no band (every cell "none", unbanded; the Δ values only).
WristProfile assessWristProfile(const WristInput &input, const std::shared_ptr<const INormProvider> &norms,
                                const GradePolicy &policy);
WristProfile wristDeltasOnly(const WristInput &input);

} // namespace pinpoint::analysis
