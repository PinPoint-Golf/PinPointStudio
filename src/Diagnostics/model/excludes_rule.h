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

#include <QString>

// The one rule for which of two mutually-excluding fired conditions is kept. explain() and the shot
// outcome composer both resolve exclusions, and a shot and its session must not disagree about which
// label survived.

namespace pinpoint::analysis {

// Which of two mutually-excluding fired conditions is kept: confidence, then fired-signal count, then
// id. true = keep a.
inline bool keepFirstOfExcludingPair(float confA, int signalsA, const QString &idA,
                                     float confB, int signalsB, const QString &idB)
{
    return (confA != confB) ? (confA > confB)
         : (signalsA != signalsB) ? (signalsA > signalsB)
                                  : (idA < idB);
}

} // namespace pinpoint::analysis
