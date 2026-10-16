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

#include "trust_presenter.h"
#include "trust_readings.h"

#include "../charts/session_series_model.h"

#include <QDir>

namespace pinpoint::trust {

namespace {
QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }
}

TrustPresenter::TrustPresenter(QObject *parent) : QObject(parent)
{
    connect(this, &TrustPresenter::inputsChanged, this, &TrustPresenter::recompute);
}

QObject *TrustPresenter::model() const { return m_model.data(); }

void TrustPresenter::setModel(QObject *m)
{
    auto *sm = qobject_cast<charts::SessionSeriesModel *>(m);
    if (sm == m_model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = sm;
    if (m_model) connect(m_model, &charts::SessionSeriesModel::snapshotChanged, this, &TrustPresenter::recompute);
    emit inputsChanged();
}

void TrustPresenter::setPickedSwingDir(const QString &d)
{
    if (d == m_pickedSwingDir) return;
    m_pickedSwingDir = d;
    emit inputsChanged();
}

void TrustPresenter::setPickedDetail(const QVariantMap &d)
{
    if (d == m_pickedDetail) return;
    m_pickedDetail = d;
    emit inputsChanged();
}

void TrustPresenter::recompute()
{
    const std::shared_ptr<const charts::SessionSnapshot> snap = m_model ? m_model->snapshot() : nullptr;
    // the picked shot: the session's copy when it is in the session, else the live detail map
    std::shared_ptr<const charts::ShotSeries> picked;
    const QString want = cleanDir(m_pickedSwingDir);
    if (snap && !want.isEmpty())
        for (const auto &sh : snap->shots)
            if (sh && cleanDir(sh->swingDir) == want) { picked = sh; break; }
    if (!picked && !m_pickedDetail.isEmpty() && !want.isEmpty()) {
        auto sh = std::make_shared<charts::ShotSeries>();
        sh->swingDir = want;
        sh->ordinal = 0;
        sh->kinematicSequence = m_pickedDetail.value(QStringLiteral("kinematicSequence")).toMap();
        sh->gatesVersion = m_pickedDetail.value(QStringLiteral("gatesVersion")).toInt();
        sh->gates = m_pickedDetail.value(QStringLiteral("gates")).toList();
        picked = sh;
    }
    QVariantMap shot;
    if (picked) {
        shot = trustShot(*picked);
        const int n = snap ? snap->count() : 0;
        QString kicker = picked->ordinal > 0 && n > 0 ? QStringLiteral("Shot %1 of %2").arg(picked->ordinal).arg(n) : QStringLiteral("This shot");
        // the club as Charts says it ("7 iron"): lower-cased, ASCII only
        QString club = picked->club;
        for (QChar &ch : club) if (ch.unicode() < 128) ch = ch.toLower();
        if (!club.isEmpty()) kicker += QStringLiteral("  ·  ") + club;
        shot.insert(QStringLiteral("kicker"), kicker);
        shot.insert(QStringLiteral("ordinal"), picked->ordinal);
        shot.insert(QStringLiteral("swingDir"), picked->swingDir);
    }
    shot.insert(QStringLiteral("available"), bool(picked));
    m_shot = shot;
    m_session = snap ? trustSession(*snap) : QVariantMap{ { QStringLiteral("n"), 0 } };
    emit resultsChanged();
}

} // namespace pinpoint::trust
