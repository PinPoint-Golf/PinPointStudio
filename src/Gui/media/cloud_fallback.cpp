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

#include "cloud_fallback.h"

#include <utility>

CloudFallback::CloudFallback(Policy policy, Hooks hooks)
    : m_policy(std::move(policy))
    , m_hooks(std::move(hooks))
{
}

QString CloudFallback::key() const
{
    for (const QString &id : m_policy.keyIds) {
        const QString k = m_hooks.readSecret(id);
        if (!k.isEmpty())
            return k;
    }
    return QString();
}

void CloudFallback::apply()
{
    const std::optional<bool> preferCloud = m_hooks.preference();
    if (!preferCloud)
        return;
    const QString key  = this->key();
    const bool    want = (*preferCloud || (m_policy.noLocalGpuMeansCloud && !m_hooks.localGpu()))
                         && !key.isEmpty();
    // Rebuild when the desired backend changes, OR when staying on cloud but the
    // key value changed (e.g. the user corrected a bad key).
    if (want == m_usingCloud && (!want || key == m_loadedKey))
        return;
    if (want) {
        m_usingCloud = true;
        m_loadedKey  = key;
        m_hooks.switchToCloud(key);
    } else {
        m_loadedKey.clear();
        m_hooks.switchToLocal();
    }
}

void CloudFallback::refresh()
{
    const bool avail = keyPresent();
    if (avail != m_available) {
        m_available = avail;
        m_hooks.availabilityChanged();
    }
    // A newly-entered (or corrected) key may enable a force-cloud preference that
    // was waiting on it, or change the key of the live cloud backend.
    apply();
}

void CloudFallback::toggle()
{
    // The persisted preference is the single source of truth — flipping it drives
    // apply() (and keeps the Settings toggle in sync).
    if (!m_available)
        return;
    const std::optional<bool> preferCloud = m_hooks.preference();
    if (!preferCloud)
        return;
    // No local GPU → cloud is mandatory (toggle locked ON in the UI); guard here too
    // so the badge can't force an unusable local engine.
    if (m_policy.noLocalGpuMeansCloud && !m_hooks.localGpu())
        return;
    m_hooks.setPreference(!*preferCloud);
}
