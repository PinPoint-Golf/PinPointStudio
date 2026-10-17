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

#include "trust_words.h"
#include "trust_readings.h"

#include "../../Metrics/gate_catalogue.h"

namespace pinpoint::trust {

QVariantMap TrustWords::gateFor(const QVariant &gates, const QString &key) const
{
    return trust::gateFor(gates.toList(), key);
}

QString TrustWords::words(const QString &gateId) const
{
    const gates::GateEntry *e = gates::gateEntry(gateId);
    return e ? gates::claimWords(*e) : QString();
}

} // namespace pinpoint::trust
