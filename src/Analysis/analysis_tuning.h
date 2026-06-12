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

// SwingLab tuning-override application (swinglab_impl.md). The offline runner
// passes "<area>.<field>" → number in ShotAnalysisJob::tuningOverrides; each
// analysis stage applies the keys it owns onto its config struct at run time,
// so parameter sweeps iterate at binary speed with no rebuild. Empty map (the
// production case) is a no-op. Header-only, Qt-types only.

#include <QVariantMap>
#include <cstdint>

namespace pinpoint::analysis::tuning {

inline void apply(const QVariantMap &ov, const char *key, float &field)
{
    const auto it = ov.constFind(QLatin1String(key));
    if (it != ov.cend()) field = float(it->toDouble());
}
inline void apply(const QVariantMap &ov, const char *key, double &field)
{
    const auto it = ov.constFind(QLatin1String(key));
    if (it != ov.cend()) field = it->toDouble();
}
inline void apply(const QVariantMap &ov, const char *key, int &field)
{
    const auto it = ov.constFind(QLatin1String(key));
    if (it != ov.cend()) field = it->toInt();
}
inline void apply(const QVariantMap &ov, const char *key, int64_t &field)
{
    const auto it = ov.constFind(QLatin1String(key));
    if (it != ov.cend()) field = it->toLongLong();
}

} // namespace pinpoint::analysis::tuning
