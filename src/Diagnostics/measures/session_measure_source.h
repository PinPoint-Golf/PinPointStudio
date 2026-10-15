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

// THE SESSION'S READINGS, PRESENTED TO THE ENGINE (trunk_imu_sequence_grf_design.md §10.8 E2).
//
// A session-scope measure (MeasureScope::Session) has no per-swing value: the session model reduces
// it from the session's shot records and hands the number in here, and the engine then evaluates the
// pack's own signals and conditions on it with the SAME detect() every swing goes through — so the
// threshold test, the σ → pFire rule and the ANY/ALL combination are the engine's, not a copy.
//
// It answers ONLY for session-scope measures of the pack it was given. A per-swing measure asked of
// it is absent, which is the truth: this source is not a swing.

#include "../engine/characteristic_engine.h"
#include "../packs/characteristic_pack.h"

#include <QHash>
#include <QString>

#include <optional>

namespace pinpoint::analysis {

class SessionMeasureSource final : public IMeasureSource {
public:
    explicit SessionMeasureSource(const CharacteristicPack &pack) : m_pack(pack) {}

    // Set one session measure's reading. Ignored for a measure that is not session-scope.
    void set(const QString &measureId, const MeasureReading &r)
    {
        const Measure *m = m_pack.measure(measureId);
        if (!m || m->scope != MeasureScope::Session) return;
        m_readings.insert(measureId, r);
    }

    std::optional<MeasureReading> read(const QString &measureId) const override
    {
        const auto it = m_readings.constFind(measureId);
        if (it == m_readings.constEnd()) return std::nullopt;
        return it.value();
    }

private:
    const CharacteristicPack       &m_pack;
    QHash<QString, MeasureReading>  m_readings;
};

} // namespace pinpoint::analysis
