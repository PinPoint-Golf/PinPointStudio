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

// THE TRUST PANEL'S PRESENTER (docs/design/trust_and_gates_design.md §7). Reads the session through a
// SessionSeriesModel (whose process-wide cache the Charts panel's model shares, so a second instance costs
// a stat per swing) and publishes, for the picked shot and for the session, the readings a gate touched —
// trust_readings.h, the one statement every surface reads.
//
//   shot:    { available, ordinal, kicker ("Shot 4 of 6 · 7 iron"), swingDir, recorded, head, readings, counts }
//   session: { n, recorded, head, gates: [{ gate, title, area, acted, subjects, shots, of, countText }] }
//
// Synchronous on the GUI thread: records are a few dozen small maps per shot, already in the snapshot.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace pinpoint::charts { class SessionSeriesModel; }

namespace pinpoint::trust {

class TrustPresenter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject *model READ model WRITE setModel NOTIFY inputsChanged)
    Q_PROPERTY(QString pickedSwingDir READ pickedSwingDir WRITE setPickedSwingDir NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap pickedDetail READ pickedDetail WRITE setPickedDetail NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap shot READ shot NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap session READ session NOTIFY resultsChanged)

public:
    explicit TrustPresenter(QObject *parent = nullptr);

    QObject *model() const;
    void setModel(QObject *m);
    QString pickedSwingDir() const { return m_pickedSwingDir; }
    void setPickedSwingDir(const QString &d);
    QVariantMap pickedDetail() const { return m_pickedDetail; }
    void setPickedDetail(const QVariantMap &d);
    QVariantMap shot() const { return m_shot; }
    QVariantMap session() const { return m_session; }

signals:
    void inputsChanged();
    void resultsChanged();

private:
    void recompute();

    QPointer<charts::SessionSeriesModel> m_model;
    QString     m_pickedSwingDir;
    QVariantMap m_pickedDetail;
    QVariantMap m_shot, m_session;
};

} // namespace pinpoint::trust
