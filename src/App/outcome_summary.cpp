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

#include "outcome_summary.h"

#include "../Diagnostics/derived/shot_factor_words.h"
#include "../DomainModel/session.h"
#include "../DomainModel/session_diagnostics.h"
#include "../DomainModel/shot.h"

#include <cmath>

namespace pinpoint {

ShotOutcomeSummary outcomeSummaryOf(Session *session, const ShotRef &shotRef, bool isSelection)
{
    ShotOutcomeSummary out;
    Shot *shot = session ? session->shot(shotRef) : nullptr;   // null for a ref of another session, or none (J-a4)
    if (!shot) return out;

    const ShotDiagnostics d = shot->diagnostics();
    const bool launchMonitor = d.fired() ? d.hasLaunchMonitor() : !shot->summary().lmDevice.isEmpty();
    if (!launchMonitor) return out;

    out.shown       = true;
    out.selected    = isSelection;
    out.shotRef     = shot->ref().toString();
    out.swingNumber = shot->swingNumber();
    if (const analysis::ShotFactors *f = d.factors()) {
        const analysis::ShotOutcomePack *pack = d.outcomePack();
        const auto words = [&](const analysis::ShotFactor &x, const QString &name, QString &text, QString &tone) {
            const analysis::ShotFactorWords w = analysis::shotFactorWords(x, name, pack);
            text = w.text;
            tone = w.tone;
        };
        out.judged = true;
        words(f->strike,    analysis::shotFactorStrike(),    out.strikeText,    out.strikeTone);
        words(f->direction, analysis::shotFactorDirection(), out.directionText, out.directionTone);
        words(f->flight,    analysis::shotFactorFlight(),    out.flightText,    out.flightTone);
    }
    if (const analysis::ShotRecord *r = d.findings(); r && std::isfinite(r->carry))
        out.carryText = QString::number(r->carry, 'f', 1) + QStringLiteral(" yd");
    return out;
}

void watchOutcomeSummary(Session *session, QObject *context, std::function<void()> changed)
{
    if (!session || !context) return;
    const auto say = [changed] { if (changed) changed(); };
    QObject::connect(session, &Session::shotAdded,   context, say);
    QObject::connect(session, &Session::shotTrashed, context, say);
    if (SessionDiagnostics *owner = session->diagnostics()) {
        QObject::connect(owner, &SessionDiagnostics::resultKept,   context, say);
        QObject::connect(owner, &SessionDiagnostics::shotUnread,   context, say);
        QObject::connect(owner, &SessionDiagnostics::batchSettled, context, say);
    }
}

} // namespace pinpoint
