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

#include <QObject>
#include <QVariantList>

// ReanalysisController — the single funnel for "re-analyse this shot" from the
// carousel's PpShotActionBar. The bar emits reanalyseShot(); the carousel
// forwards it here as a one-element id list. This is the SEAM only: the actual
// pipeline (load the saved swing, re-run pose/biomech, re-score, re-persist) is
// a follow-up. For now reanalyse() logs the request to PpMessageLog and emits
// reanalyseQueued(count) so the host can raise a toast; the reanalysing
// property is reserved for the future progress affordance (bar binds it later).
//
// Registered in main.cpp as the QML context property `reanalysisController`.
// Kept separate from ShotProcessor deliberately: that class owns the strict
// live-shot state machine that gates `armed`, and a stub must not perturb it.
class ReanalysisController : public QObject
{
    Q_OBJECT
    // Reserved progress hook — true while a re-analysis is in flight. Always
    // false in this stubbed phase (no work runs yet); the eventual pipeline
    // flips it via setReanalysing().
    Q_PROPERTY(bool reanalysing READ reanalysing NOTIFY reanalysingChanged)

public:
    explicit ReanalysisController(QObject *parent = nullptr);

    bool reanalysing() const { return m_reanalysing; }

    // The one entry point for both the (future) carousel re-analyse paths. Takes
    // the id list so a single funnel serves focused-shot today and any wider
    // scope later. Empty list is a no-op. Stub: logs + emits reanalyseQueued().
    Q_INVOKABLE void reanalyse(const QVariantList &ids);

signals:
    void reanalysingChanged();
    // Emitted once per accepted reanalyse() with the number of shots queued — the
    // host turns this into a user-facing toast (the bar/carousel owns the toast).
    void reanalyseQueued(int count);

private:
    void setReanalysing(bool on);   // reserved for the real pipeline

    bool m_reanalysing = false;
};
