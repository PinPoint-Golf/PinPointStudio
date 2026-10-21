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

// THE STUDIO AND ITS ONE COACH (docs/design/session_report_design.md §7.1, §7.2) — entered once on
// Settings → Studio, read by SessionReportModel whenever a report is made. Stored in the app INI under
// `Studio/`; the logo and the coach's photo are COPIED into <AppData>/studio/ so moving or deleting the
// originals never breaks a report. Context property `studioProfile`.
//
// What is stored, and where:
//   Studio/name, logo, accent, address, phone, email, website, booking, practiceSteps (JSON text),
//   emailSubject, emailBody, coachName, coachTitle, coachEmail, coachPhone, coachPhoto
// `logo` and `coachPhoto` hold the path of the COPY, never the original. practiceSteps, emailSubject and
// emailBody are stored only once the coach has changed them: an absent key reads as the shipped default,
// so a later build that rewrites a default reaches every studio that never touched it — and setting one
// back to empty (or to the default) removes the key again rather than storing a blank the report would print.
//
// The copies live in <AppData>/studio/ (QStandardPaths::AppDataLocation). PINPOINT_STUDIO_DIR overrides
// that folder, so a probe or a test can import a logo without touching the real app data.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class StudioProfile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString studioName     READ studioName     WRITE setStudioName     NOTIFY changed)
    Q_PROPERTY(QString logoPath       READ logoPath                               NOTIFY changed)
    Q_PROPERTY(QString accent         READ accent         WRITE setAccent         NOTIFY changed)
    Q_PROPERTY(QString address        READ address        WRITE setAddress        NOTIFY changed)
    Q_PROPERTY(QString phone          READ phone          WRITE setPhone          NOTIFY changed)
    Q_PROPERTY(QString email          READ email          WRITE setEmail          NOTIFY changed)
    Q_PROPERTY(QString website        READ website        WRITE setWebsite        NOTIFY changed)
    Q_PROPERTY(QString booking        READ booking        WRITE setBooking        NOTIFY changed)
    Q_PROPERTY(QVariantList practiceSteps READ practiceSteps WRITE setPracticeSteps NOTIFY changed)
    Q_PROPERTY(QString emailSubject   READ emailSubject   WRITE setEmailSubject   NOTIFY changed)
    Q_PROPERTY(QString emailBody      READ emailBody      WRITE setEmailBody      NOTIFY changed)
    Q_PROPERTY(QString coachName      READ coachName      WRITE setCoachName      NOTIFY changed)
    Q_PROPERTY(QString coachTitle     READ coachTitle     WRITE setCoachTitle     NOTIFY changed)
    Q_PROPERTY(QString coachEmail     READ coachEmail     WRITE setCoachEmail     NOTIFY changed)
    Q_PROPERTY(QString coachPhone     READ coachPhone     WRITE setCoachPhone     NOTIFY changed)
    Q_PROPERTY(QString coachPhotoPath READ coachPhotoPath                         NOTIFY changed)
    Q_PROPERTY(QString monogram       READ monogram                               NOTIFY changed)
    Q_PROPERTY(QString stamp          READ stamp                                  NOTIFY changed)
    Q_PROPERTY(QVariantList missingDetails READ missingDetails                    NOTIFY changed)

public:
    explicit StudioProfile(QObject *parent = nullptr);

    QString studioName() const;     void setStudioName(const QString &v);
    QString logoPath() const;
    QString accent() const;         void setAccent(const QString &v);
    QString address() const;        void setAddress(const QString &v);
    QString phone() const;          void setPhone(const QString &v);
    QString email() const;          void setEmail(const QString &v);
    QString website() const;        void setWebsite(const QString &v);
    QString booking() const;        void setBooking(const QString &v);
    QVariantList practiceSteps() const; void setPracticeSteps(const QVariantList &v);
    QString emailSubject() const;   void setEmailSubject(const QString &v);
    QString emailBody() const;      void setEmailBody(const QString &v);
    QString coachName() const;      void setCoachName(const QString &v);
    QString coachTitle() const;     void setCoachTitle(const QString &v);
    QString coachEmail() const;     void setCoachEmail(const QString &v);
    QString coachPhone() const;     void setCoachPhone(const QString &v);
    QString coachPhotoPath() const;
    QString monogram() const;
    QString stamp() const;
    QVariantList missingDetails() const;

    // Copy a PNG, SVG or JPEG into the studio folder as logo.<ext> / coach.<ext>, replacing any earlier
    // copy (whatever its extension). False — and nothing changed — when the file is missing, unreadable,
    // not one of those three, or does not decode as what its name says.
    Q_INVOKABLE bool importLogo(const QString &path);
    Q_INVOKABLE bool importCoachPhoto(const QString &path);
    Q_INVOKABLE void clearLogo();
    Q_INVOKABLE void clearCoachPhoto();

    // The accent is printed as text (kickers, rank numerals) on white paper, so a pale one cannot be read.
    // True when `colour` has a contrast ratio below 3:1 against white (WCAG relative luminance) — the
    // large-text threshold, since the accent only ever sets display-size marks. An unparseable string is
    // not "too light" (it is not a colour at all); isColour() says that.
    Q_INVOKABLE bool accentTooLight(const QString &colour) const;
    Q_INVOKABLE bool isColour(const QString &colour) const;
    // The swatch row on Settings → Studio: a few dark, quiet colours that all pass accentTooLight().
    Q_INVOKABLE QStringList accentPresets() const;

    // The shipped defaults, for "Reset to the default" and for a test to compare against.
    Q_INVOKABLE QVariantList defaultPracticeSteps() const;
    Q_INVOKABLE QString defaultEmailSubject() const;
    Q_INVOKABLE QString defaultEmailBody() const;

    // Fill a template's {first} {date} {coach} {studio} (any {key} in `values`); an unknown placeholder
    // is left as written so a typo shows in the composed message instead of vanishing.
    Q_INVOKABLE QString fill(const QString &tpl, const QVariantMap &values) const;

signals:
    void changed();

private:
    void load();
    void store(const char *key, const QString &value);   // empty removes the key
    void touch();                                        // recompute the stamp, emit changed()
    bool importImage(const QString &path, const QString &baseName, const char *key, QString &member);
    void clearImage(const char *key, QString &member);
    QString computeStamp() const;
    static QString studioDir();

    QString m_studioName, m_logoPath, m_accent, m_address, m_phone, m_email, m_website, m_booking;
    QVariantList m_practiceSteps;                        // empty = the default
    QString m_emailSubject, m_emailBody;                 // empty = the default
    QString m_coachName, m_coachTitle, m_coachEmail, m_coachPhone, m_coachPhotoPath;
    QString m_stamp;
};
