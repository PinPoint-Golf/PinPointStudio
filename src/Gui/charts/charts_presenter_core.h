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

// THE CHARTS PRESENTERS' ONE BASE (refactor 5.5a; F-08; DUTIES-5.5 §C P1–P6): what the six presenters (Traces,
// Relations, Wrist, Posture, Club delivery, Consistency) and the Trust presenter share, in one place, where each used to
// carry its own drifted copy:
//   • THE MODEL: a typed SeriesSource (the session's own series, or a test's), never a QObject resolved by qobject_cast
//     on every read (F-12); its snapshotChanged the one trigger.
//   • THE SNAPSHOT'S PRECEDENCE (P2): a snapshot set directly (a test, the report) wins; else the model's. Two of the six
//     had it the other way round.
//   • THE IDIOM (P4): a job on the presenter's ChartsLane (charts_work.h: the app's one WorkPool, its value through the
//     one WorkReceiver), its typed value handed back on the GUI thread only while the presenter lives — askLane() below.
//     The stills run on a lane of their own, so a compute never queues behind a decode (P10).
//   • THE SCHEDULER (P3): every presenter coalesces — one job in flight; a change meanwhile marks it dirty, and ONE more
//     job runs with the latest inputs when it lands — and is busy from the moment a job is asked for until its result
//     lands (P11: Consistency's `settling` reads the series' busy and then this one's, with no gap between them).
//   • THE SYNCHRONOUS SEAM (tests, the report's payloads): each presenter keeps its own read-time contract.

#include "charts_work.h"

#include <QPointer>

#include <any>
#include <memory>
#include <type_traits>
#include <utility>

namespace pinpoint::charts {

// The snapshot a presenter's next job reads (P2).
inline std::shared_ptr<const SessionSnapshot> chosenSnapshot(const std::shared_ptr<const SessionSnapshot> &direct,
                                                             const SeriesSource *model)
{
    if (direct) return direct;
    return model ? model->snapshot() : nullptr;
}

// Run `compute` (values only: no `this`) on `lane`; hand its value to `deliver` on the GUI thread (P4).
template <class Compute, class Deliver>
void askLane(ChartsLane &lane, Compute compute, Deliver deliver)
{
    using R = std::decay_t<decltype(compute())>;
    lane.ask([c = std::move(compute)]() mutable { return std::any(c()); },
             [d = std::move(deliver)](std::any &&v) mutable { d(std::any_cast<R>(std::move(v))); });
}

} // namespace pinpoint::charts
