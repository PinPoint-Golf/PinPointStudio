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

#include "shot_filter_proxy_model.h"
#include "shot_list_model.h"

#include <QStringList>

ShotFilterProxyModel::ShotFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // Counts depend on both proxy rows (visibleCount) and source data
    // (sourceCount via the model's activeCount); proxy row signals cover both
    // since a trash/restore re-filters the flagged row.
    connect(this, &QAbstractItemModel::rowsInserted,  this, &ShotFilterProxyModel::countsChanged);
    connect(this, &QAbstractItemModel::rowsRemoved,   this, &ShotFilterProxyModel::countsChanged);
    connect(this, &QAbstractItemModel::modelReset,    this, &ShotFilterProxyModel::countsChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ShotFilterProxyModel::countsChanged);
    connect(this, &ShotFilterProxyModel::sourceModelChanged,
            this, &ShotFilterProxyModel::onSourceModelChanged);
}

void ShotFilterProxyModel::onSourceModelChanged()
{
    // activeCount changes (trash/restore) must refresh sourceCount even when
    // the trashed row was already filtered out of this proxy's view.
    if (auto *shots = qobject_cast<ShotListModel *>(sourceModel()))
        connect(shots, &ShotListModel::activeCountChanged,
                this, &ShotFilterProxyModel::countsChanged, Qt::UniqueConnection);
    emit countsChanged();
}

void ShotFilterProxyModel::setQualityLo(int lo)
{
    if (m_qualityLo == lo)
        return;
    beginFilterChange();
    m_qualityLo = lo;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
    emit countsChanged();
}

void ShotFilterProxyModel::setQualityHi(int hi)
{
    if (m_qualityHi == hi)
        return;
    beginFilterChange();
    m_qualityHi = hi;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
    emit countsChanged();
}

void ShotFilterProxyModel::setRatingFilter(int n)
{
    if (m_ratingFilter == n)
        return;
    beginFilterChange();
    m_ratingFilter = n;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
    emit countsChanged();
}

void ShotFilterProxyModel::setHasVideoOnly(bool on)
{
    if (m_hasVideoOnly == on)
        return;
    beginFilterChange();
    m_hasVideoOnly = on;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
    emit countsChanged();
}

void ShotFilterProxyModel::setQualityBand(int lo, int hi)
{
    if (m_qualityLo == lo && m_qualityHi == hi)
        return;
    beginFilterChange();
    m_qualityLo = lo;
    m_qualityHi = hi;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
    emit countsChanged();
}

void ShotFilterProxyModel::clearAll()
{
    if (!filterActive())
        return;
    beginFilterChange();
    m_qualityLo    = -1;
    m_qualityHi    = -1;
    m_ratingFilter = 0;
    m_hasVideoOnly = false;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
    emit countsChanged();
}

QVariantList ShotFilterProxyModel::visibleShotIds() const
{
    QVariantList ids;
    for (int row = 0; row < rowCount(); ++row)
        ids.append(index(row, 0).data(ShotListModel::ShotIdRole));
    return ids;
}

int ShotFilterProxyModel::sourceCount() const
{
    if (auto *shots = qobject_cast<ShotListModel *>(sourceModel()))
        return shots->activeCount();
    return sourceModel() ? sourceModel()->rowCount() : 0;
}

QString ShotFilterProxyModel::countLabel() const
{
    if (filterActive())
        return tr("%1 of %2 shots").arg(visibleCount()).arg(sourceCount());
    return tr("%1 shots").arg(sourceCount());
}

bool ShotFilterProxyModel::filterActive() const
{
    return m_qualityLo >= 0 || m_ratingFilter > 0 || m_hasVideoOnly;
}

QString ShotFilterProxyModel::filterSummary() const
{
    QStringList parts;
    if (m_qualityLo >= 0)
        parts << QStringLiteral("%1–%2").arg(m_qualityLo).arg(m_qualityHi);
    if (m_ratingFilter > 0)
        parts << tr("%1★").arg(m_ratingFilter);
    if (m_hasVideoOnly)
        parts << tr("Has video");
    return parts.isEmpty() ? tr("All shots") : parts.join(QStringLiteral(" · "));
}

bool ShotFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex i = sourceModel()->index(sourceRow, 0, sourceParent);
    if (i.data(ShotListModel::TrashedRole).toBool())
        return false;
    if (m_ratingFilter > 0 && i.data(ShotListModel::RatingRole).toInt() != m_ratingFilter)
        return false;
    if (m_hasVideoOnly && !i.data(ShotListModel::HasVideoRole).toBool())
        return false;
    if (m_qualityLo >= 0) {
        const int s = i.data(ShotListModel::ScoreRole).toInt();
        if (s < m_qualityLo || s > m_qualityHi)
            return false;
    }
    return true;
}
