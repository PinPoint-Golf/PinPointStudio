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
#include <QQmlEngine>
#include <QSortFilterProxyModel>

// ShotFilterProxyModel — per-carousel filter over the session-global
// ShotListModel.  Each PpShotCarousel instantiates its own:
//
//     ShotFilterProxyModel { sourceModel: shotModel }
//
// so filter state is per-screen while the shot data stays shared.  Filters
// AND together; trashed shots are always rejected.  All filter logic lives
// here — QML only binds the Q_PROPERTYs.

class ShotFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    // Exact quality band, e.g. 75/100.  -1 = any.
    Q_PROPERTY(int  qualityLo    READ qualityLo    WRITE setQualityLo    NOTIFY filterChanged)
    Q_PROPERTY(int  qualityHi    READ qualityHi    WRITE setQualityHi    NOTIFY filterChanged)
    // Exact star rating (only shots rated exactly N).  0 = any.
    Q_PROPERTY(int  ratingFilter READ ratingFilter WRITE setRatingFilter NOTIFY filterChanged)
    Q_PROPERTY(bool hasVideoOnly READ hasVideoOnly WRITE setHasVideoOnly NOTIFY filterChanged)

    // "N of M" — visible rows vs non-trashed source rows.
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
    Q_PROPERTY(int sourceCount  READ sourceCount  NOTIFY countsChanged)
    // Filter-combo label: "N shots" unfiltered, "N of M shots" filtered.
    // Every filter change also emits countsChanged, so one NOTIFY suffices.
    Q_PROPERTY(QString countLabel READ countLabel NOTIFY countsChanged)

    // Filter-pill presentation, computed here so QML stays logic-free.
    Q_PROPERTY(bool    filterActive  READ filterActive  NOTIFY filterChanged)
    Q_PROPERTY(QString filterSummary READ filterSummary NOTIFY filterChanged)

public:
    explicit ShotFilterProxyModel(QObject *parent = nullptr);

    int  qualityLo()    const { return m_qualityLo; }
    int  qualityHi()    const { return m_qualityHi; }
    int  ratingFilter() const { return m_ratingFilter; }
    bool hasVideoOnly() const { return m_hasVideoOnly; }

    void setQualityLo(int lo);
    void setQualityHi(int hi);
    void setRatingFilter(int n);
    void setHasVideoOnly(bool on);

    int visibleCount() const { return rowCount(); }
    int sourceCount()  const;
    QString countLabel() const;

    bool    filterActive() const;
    QString filterSummary() const;

    // Set both band edges atomically — assigning qualityLo and qualityHi as
    // two property writes from QML re-evaluates the caller's selection binding
    // between the writes, corrupting the second value.
    Q_INVOKABLE void setQualityBand(int lo, int hi);
    Q_INVOKABLE void clearAll();
    // Shot ids of the currently visible (filtered) rows — the bulk-action set.
    Q_INVOKABLE QVariantList visibleShotIds() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    void onSourceModelChanged();

    int  m_qualityLo    = -1;
    int  m_qualityHi    = -1;
    int  m_ratingFilter = 0;
    bool m_hasVideoOnly = false;

signals:
    void filterChanged();
    void countsChanged();
};
