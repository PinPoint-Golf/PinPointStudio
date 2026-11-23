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

// THE DATA VIEWER'S TYPED VALUES (refactor 5.8a, lint A6): what SwingDataSource hands QML, as Q_GADGETs named as the
// old maps' keys, so the QML reads them as it read the maps. moc reaches this header through swing_data_source.cpp's
// #include of its moc file.

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace pinpoint::dataview {

// A phase-bounded window of the table ("Full" first; "P1→P2", …).
struct Segment
{
    Q_GADGET
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(qint64  startUs MEMBER startUs FINAL)
    Q_PROPERTY(qint64  endUs   MEMBER endUs   FINAL)
public:
    QString label;
    qint64  startUs = 0;
    qint64  endUs   = 0;
    friend bool operator==(const Segment &, const Segment &) = default;
};

// One resolved lane of the region, as its chip shows it.
struct SourceChip
{
    Q_GADGET
    Q_PROPERTY(QString label     MEMBER label     FINAL)
    Q_PROPERTY(QString colorKey  MEMBER colorKey  FINAL)
    Q_PROPERTY(QString kind      MEMBER kind      FINAL)
    Q_PROPERTY(QString ref       MEMBER ref       FINAL)
    Q_PROPERTY(QString part      MEMBER part      FINAL)
    Q_PROPERTY(bool    removable MEMBER removable FINAL)
public:
    QString label, colorKey, kind, ref, part;
    bool    removable = true;
    friend bool operator==(const SourceChip &, const SourceChip &) = default;
};

// One phase event of the analysis, window-relative.
struct Phase
{
    Q_GADGET
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(int     phase MEMBER phase FINAL)
    Q_PROPERTY(qint64  t_us  MEMBER t_us  FINAL)
    Q_PROPERTY(QString kind  MEMBER kind  FINAL)
public:
    QString label;
    int     phase = 0;
    qint64  t_us  = 0;
    QString kind  = QStringLiteral("phase");
    friend bool operator==(const Phase &, const Phase &) = default;
};

// One Properties row and one group of them.
struct PropertyRow
{
    Q_GADGET
    Q_PROPERTY(QString k MEMBER k FINAL)
    Q_PROPERTY(QString v MEMBER v FINAL)
public:
    QString k, v;
    friend bool operator==(const PropertyRow &, const PropertyRow &) = default;
};
struct PropertyGroup
{
    Q_GADGET
    Q_PROPERTY(QString group MEMBER group FINAL)
    Q_PROPERTY(QList<pinpoint::dataview::PropertyRow> rows MEMBER rows FINAL)
public:
    QString            group;
    QList<PropertyRow> rows;
    friend bool operator==(const PropertyGroup &, const PropertyGroup &) = default;
};

// A lane the region asks for that a gate kept out, in the gate's words.
struct NotShown
{
    Q_GADGET
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(QString subject MEMBER subject FINAL)
    Q_PROPERTY(QString words   MEMBER words   FINAL)
public:
    QString label, subject, words;
    friend bool operator==(const NotShown &, const NotShown &) = default;
};

} // namespace pinpoint::dataview
