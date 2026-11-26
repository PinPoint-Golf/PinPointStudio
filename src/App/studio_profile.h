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
// originals never breaks a report. QML reaches it as `app.studioProfile` (src/Gui/app/gui_app.h; R-QML-1).
//
// What is stored, and where:
//   Studio/name, logo, accent, address, phone, email, website, booking, practiceSteps (JSON text),
//   emailSubject, emailBody, coachName, coachTitle, coachEmail, coachPhone, coachPhoto
// `logo` and `coachPhoto` hold the path of the COPY, never the original. practiceSteps, emailSubject and
// emailBody are stored only once the coach has changed them: an absent key reads as the shipped default,
// so a later build that rewrites a default reaches every studio that never touched it — and setting one
// back to empty (or to the default) removes the key again rather than storing a blank the report would print.
//
// ITS PERSISTENCE IS ITS REPOSITORY'S (P11; information_model.md OBJ-STUDIO-PROFILE, SET-20, FILE-APP-06): the
// keys and the folder are read and written through StudioProfileRepository (studio_profile_repository.h), which
// the composition root hands it over the one store (src/Stores/studio_profile_store.h). This class decides what
// is stored (the trimming, the defaults, the accent's one spelling, what an image must be); the store only keeps
// it. The copies live in <AppData>/studio/ (QStandardPaths::AppDataLocation); PINPOINT_STUDIO_DIR overrides
// that folder, so a probe or a test can import a logo without touching the real app data.
//
// ITS QML API IS TYPED (lint A6): the practice steps and the missing details are lists of the gadgets below, read
// in QML as a sequence (`steps[i].head`, a Repeater's `modelData.text`), and QML sets the steps as two lists of
// text (setPracticeStepTexts). The report's payload keeps its maps: SessionReportModel makes them, with the same
// keys, where it reads these.

#include "studio_profile_repository.h"

#include <QList>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

// One of page 8's practice steps: a short head and a sentence or two.
struct StudioPracticeStep
{
    Q_GADGET
    Q_PROPERTY(QString head MEMBER head FINAL)
    Q_PROPERTY(QString body MEMBER body FINAL)
public:
    QString head, body;
    friend bool operator==(const StudioPracticeStep &, const StudioPracticeStep &) = default;
};

// A detail the report would still be missing: the words, and where it is entered (always "studio" here; the
// report adds the golfer's own as "athlete").
struct StudioMissingDetail
{
    Q_GADGET
    Q_PROPERTY(QString text  MEMBER text  FINAL)
    Q_PROPERTY(QString where MEMBER where FINAL)
public:
    QString text, where;
    friend bool operator==(const StudioMissingDetail &, const StudioMissingDetail &) = default;
};

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
    Q_PROPERTY(QList<StudioPracticeStep> practiceSteps READ practiceSteps NOTIFY changed)
    Q_PROPERTY(bool practiceStepsAreDefault READ practiceStepsAreDefault NOTIFY changed)
    Q_PROPERTY(QString emailSubject   READ emailSubject   WRITE setEmailSubject   NOTIFY changed)
    Q_PROPERTY(QString emailBody      READ emailBody      WRITE setEmailBody      NOTIFY changed)
    Q_PROPERTY(QString coachName      READ coachName      WRITE setCoachName      NOTIFY changed)
    Q_PROPERTY(QString coachTitle     READ coachTitle     WRITE setCoachTitle     NOTIFY changed)
    Q_PROPERTY(QString coachEmail     READ coachEmail     WRITE setCoachEmail     NOTIFY changed)
    Q_PROPERTY(QString coachPhone     READ coachPhone     WRITE setCoachPhone     NOTIFY changed)
    Q_PROPERTY(QString coachPhotoPath READ coachPhotoPath                         NOTIFY changed)
    Q_PROPERTY(QString monogram       READ monogram                               NOTIFY changed)
    Q_PROPERTY(QString stamp          READ stamp                                  NOTIFY changed)
    Q_PROPERTY(QList<StudioMissingDetail> missingDetails READ missingDetails      NOTIFY changed)

public:
    // `repository` is the composition root's (main.cpp) and outlives this object; the profile is loaded from it here.
    explicit StudioProfile(StudioProfileRepository &repository, QObject *parent = nullptr);

    QString studioName() const;     void setStudioName(const QString &v);
    QString logoPath() const;
    QString accent() const;         void setAccent(const QString &v);
    QString address() const;        void setAddress(const QString &v);
    QString phone() const;          void setPhone(const QString &v);
    QString email() const;          void setEmail(const QString &v);
    QString website() const;        void setWebsite(const QString &v);
    QString booking() const;        void setBooking(const QString &v);
    QList<StudioPracticeStep> practiceSteps() const; void setPracticeSteps(const QList<StudioPracticeStep> &v);
    bool practiceStepsAreDefault() const;    // the shipped steps: "Use the original text" has nothing to do
    QString emailSubject() const;   void setEmailSubject(const QString &v);
    QString emailBody() const;      void setEmailBody(const QString &v);
    QString coachName() const;      void setCoachName(const QString &v);
    QString coachTitle() const;     void setCoachTitle(const QString &v);
    QString coachEmail() const;     void setCoachEmail(const QString &v);
    QString coachPhone() const;     void setCoachPhone(const QString &v);
    QString coachPhotoPath() const;
    QString monogram() const;
    QString stamp() const;
    QList<StudioMissingDetail> missingDetails() const;

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

    // The practice steps as Settings → Studio edits them: the i-th head and body of each list make the i-th step
    // (a missing body is empty). Trimmed, and a step left wholly blank dropped, as setPracticeSteps does.
    Q_INVOKABLE void setPracticeStepTexts(const QStringList &heads, const QStringList &bodies);
    // Back to the shipped steps ("Use the original text"): the override key goes.
    Q_INVOKABLE void resetPracticeSteps();

    // The shipped defaults, for "Reset to the default" and for a test to compare against.
    Q_INVOKABLE QList<StudioPracticeStep> defaultPracticeSteps() const;
    Q_INVOKABLE QString defaultEmailSubject() const;
    Q_INVOKABLE QString defaultEmailBody() const;

    // Fill a template's {first} {date} {coach} {studio} (any {key} in `values`, replaced in the keys' order); an
    // unknown placeholder is left as written so a typo shows in the composed message instead of vanishing. C++ only:
    // the report composes its email subject with it (SessionReportModel).
    QString fill(const QString &tpl, const QMap<QString, QString> &values) const;

signals:
    void changed();

private:
    void load();
    void store(StudioKey key, const QString &value);     // empty removes the key
    void touch();                                        // recompute the stamp, emit changed()
    bool importImage(const QString &path, const QString &baseName, StudioKey key, QString &member);
    void clearImage(StudioKey key, QString &member);
    QString computeStamp() const;

    StudioProfileRepository &m_repo;
    QString m_studioName, m_logoPath, m_accent, m_address, m_phone, m_email, m_website, m_booking;
    QList<StudioPracticeStep> m_practiceSteps;           // empty = the default
    QString m_emailSubject, m_emailBody;                 // empty = the default
    QString m_coachName, m_coachTitle, m_coachEmail, m_coachPhone, m_coachPhotoPath;
    QString m_stamp;
};
