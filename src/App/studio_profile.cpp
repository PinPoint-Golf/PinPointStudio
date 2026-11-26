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

#include "studio_profile.h"

#include "../Core/pp_debug.h"

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QtGlobal>

#include <cmath>

namespace {

// The accent a studio gets before it chooses one: Folio light's own accent, so an unbranded report
// looks exactly like the app it came from rather than like somebody else's brand.
const QString kDefaultAccent = QStringLiteral("#2573d1");

// Words a monogram skips: "The Golf Lab" is GL, not TG. Lower case, compared case-insensitively.
const QStringList &monogramStopWords()
{
    static const QStringList k = { QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
                                   QStringLiteral("of"),  QStringLiteral("and"), QStringLiteral("at"),
                                   QStringLiteral("&"),   QStringLiteral("for"), QStringLiteral("by") };
    return k;
}

// A colour as typed: a CSS/SVG name, #rgb, #rrggbb, or a bare six-digit hex — what people paste out
// of a brand guide. Invalid QColor when it is none of those.
QColor parseColour(const QString &typed)
{
    const QString t = typed.trimmed();
    if (t.isEmpty()) return {};
    static const QRegularExpression bareHex(QStringLiteral("^[0-9A-Fa-f]{6}$"));
    return QColor::fromString(bareHex.match(t).hasMatch() ? QStringLiteral("#") + t : t);
}

double channelLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// WCAG 2 relative luminance of an sRGB colour.
double luminance(const QColor &c)
{
    return 0.2126 * channelLinear(c.redF()) + 0.7152 * channelLinear(c.greenF())
         + 0.0722 * channelLinear(c.blueF());
}

// The four steps of page 8, word for word from the mock (tools/session-report-mock/assemble.py,
// "How to practise a change") — the text Mark approved. The coach edits it once, here.
QList<StudioPracticeStep> builtInPracticeSteps()
{
    auto step = [](const QString &head, const QString &body) { return StudioPracticeStep{ head, body }; };
    return {
        step(QStringLiteral("Rehearse without a ball"),
             QStringLiteral("Slow swings, ten at a time, feeling the change. This is where the new move is learned.")),
        step(QStringLiteral("Small swings with a ball"),
             QStringLiteral("Half swings at half speed. Keep the feel; ignore where the ball goes.")),
        step(QStringLiteral("Mix it in"),
             QStringLiteral("Full swings: one with the feel, then two normal ones. Film every fifth swing.")),
        step(QStringLiteral("One thing at a time"),
             QStringLiteral("Stay with work-on 1 until your next session. The rest of the list waits.")),
    };
}

// The steps as the INI stores them: JSON text, readable by a person opening the file, rather than
// QSettings' @Variant blob for a list of maps.
QString stepsToJson(const QList<StudioPracticeStep> &steps)
{
    QJsonArray arr;
    for (const StudioPracticeStep &step : steps) {
        QJsonObject o;
        o.insert(QStringLiteral("head"), step.head);
        o.insert(QStringLiteral("body"), step.body);
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// The steps as stored, read back: only head and body, whatever else the text holds (as written, not trimmed: what
// the INI holds is what the coach set). Not an array, or not JSON at all, reads as no override.
QList<StudioPracticeStep> stepsFromJson(const QString &text)
{
    QList<StudioPracticeStep> out;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray()) return out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        out.append({ o.value(QStringLiteral("head")).toString(), o.value(QStringLiteral("body")).toString() });
    }
    return out;
}

// Steps normalised for comparison and storage: both parts trimmed, wholly blank steps dropped, so a stray space or an
// empty fourth row typed on Settings → Studio neither reaches the INI nor moves the stamp.
QList<StudioPracticeStep> cleanSteps(const QList<StudioPracticeStep> &steps)
{
    QList<StudioPracticeStep> out;
    for (const StudioPracticeStep &step : steps) {
        const QString head = step.head.trimmed();
        const QString body = step.body.trimmed();
        if (head.isEmpty() && body.isEmpty()) continue;
        out.append({ head, body });
    }
    return out;
}

} // namespace

StudioProfile::StudioProfile(StudioProfileRepository &repository, QObject *parent)
    : QObject(parent), m_repo(repository)
{
    load();
    m_stamp = computeStamp();
}

void StudioProfile::load()
{
    auto str = [this](StudioKey key) { return m_repo.value(key); };
    m_studioName  = str(StudioKey::Name);
    m_accent      = str(StudioKey::Accent);
    m_address     = str(StudioKey::Address);
    m_phone       = str(StudioKey::Phone);
    m_email       = str(StudioKey::Email);
    m_website     = str(StudioKey::Website);
    m_booking     = str(StudioKey::Booking);
    m_practiceSteps = stepsFromJson(str(StudioKey::PracticeSteps));
    m_emailSubject = str(StudioKey::EmailSubject);
    m_emailBody    = str(StudioKey::EmailBody);
    m_coachName   = str(StudioKey::CoachName);
    m_coachTitle  = str(StudioKey::CoachTitle);
    m_coachEmail  = str(StudioKey::CoachEmail);
    m_coachPhone  = str(StudioKey::CoachPhone);
    // A copy that has gone (the app data folder cleared, a restore from another machine) reads as no
    // logo at all: the report then draws the monogram, rather than an empty frame where a logo was. The key
    // itself stays as it is: nothing is written on a load.
    const QString logo  = str(StudioKey::Logo);
    const QString photo = str(StudioKey::CoachPhoto);
    m_logoPath       = m_repo.copyExists(logo)  ? logo  : QString();
    m_coachPhotoPath = m_repo.copyExists(photo) ? photo : QString();
}

void StudioProfile::store(StudioKey key, const QString &value)
{
    m_repo.store(key, value);
}

void StudioProfile::touch()
{
    m_stamp = computeStamp();
    emit changed();
}

// ── plain text fields ─────────────────────────────────────────────────────────────────────────────
//
// Trimmed on the way in: a trailing space typed into the studio name would otherwise print, move
// the stamp, and put every report out of date for nothing. An unchanged value writes nothing and
// emits nothing, so a field's editingFinished on focus loss does not churn the INI.

#define PP_STUDIO_TEXT(Getter, Setter, member, key)                                                 \
    QString StudioProfile::Getter() const { return member; }                                       \
    void StudioProfile::Setter(const QString &v)                                                   \
    {                                                                                              \
        const QString t = v.trimmed();                                                             \
        if (t == member) return;                                                                   \
        member = t;                                                                                \
        store(key, t);                                                                             \
        touch();                                                                                   \
    }

PP_STUDIO_TEXT(studioName, setStudioName, m_studioName, StudioKey::Name)
PP_STUDIO_TEXT(address,    setAddress,    m_address,    StudioKey::Address)
PP_STUDIO_TEXT(phone,      setPhone,      m_phone,      StudioKey::Phone)
PP_STUDIO_TEXT(email,      setEmail,      m_email,      StudioKey::Email)
PP_STUDIO_TEXT(website,    setWebsite,    m_website,    StudioKey::Website)
PP_STUDIO_TEXT(booking,    setBooking,    m_booking,    StudioKey::Booking)
PP_STUDIO_TEXT(coachName,  setCoachName,  m_coachName,  StudioKey::CoachName)
PP_STUDIO_TEXT(coachTitle, setCoachTitle, m_coachTitle, StudioKey::CoachTitle)
PP_STUDIO_TEXT(coachEmail, setCoachEmail, m_coachEmail, StudioKey::CoachEmail)
PP_STUDIO_TEXT(coachPhone, setCoachPhone, m_coachPhone, StudioKey::CoachPhone)

#undef PP_STUDIO_TEXT

QString StudioProfile::logoPath() const       { return m_logoPath; }
QString StudioProfile::coachPhotoPath() const { return m_coachPhotoPath; }

// ── the accent ──────────────────────────────────────────────────────────────────────────────────

QString StudioProfile::accent() const { return m_accent.isEmpty() ? kDefaultAccent : m_accent; }

void StudioProfile::setAccent(const QString &v)
{
    // Stored as #rrggbb, lower case, whatever was typed ("1F5A4C", "#1f5a4c", "darkgreen"): one
    // spelling per colour, so the stamp does not move when the same colour is typed differently.
    // Empty goes back to the default. Anything that is not a colour is refused and changes nothing —
    // the field says so in place; a half-typed hex must not reach the report.
    QString norm;
    if (!v.trimmed().isEmpty()) {
        QColor c = parseColour(v);
        if (!c.isValid()) return;
        c.setAlpha(255);
        norm = c.name(QColor::HexRgb).toLower();
        if (norm == kDefaultAccent) norm.clear();
    }
    if (norm == m_accent) return;
    m_accent = norm;
    store(StudioKey::Accent, norm);
    touch();
}

bool StudioProfile::isColour(const QString &colour) const
{
    return parseColour(colour).isValid();
}

bool StudioProfile::accentTooLight(const QString &colour) const
{
    const QColor c = parseColour(colour);
    if (!c.isValid()) return false;
    const double ratio = 1.05 / (luminance(c) + 0.05);   // white's luminance is 1
    return ratio < 3.0;
}

QStringList StudioProfile::accentPresets() const
{
    // Folio's blue first (the default), then quiet, dark brand colours a golf studio might use. Each
    // clears 3:1 on white with room to spare (report_studio_test checks every one).
    return { kDefaultAccent,
             QStringLiteral("#1f5a4c"),    // course green
             QStringLiteral("#1f3a5f"),    // navy
             QStringLiteral("#24646b"),    // teal
             QStringLiteral("#8a2433"),    // claret
             QStringLiteral("#5b3a70"),    // plum
             QStringLiteral("#a4481c"),    // rust
             QStringLiteral("#3a3a38") };  // charcoal
}

// ── the practice steps and the email templates ─────────────────────────────────────────────────

QList<StudioPracticeStep> StudioProfile::defaultPracticeSteps() const { return builtInPracticeSteps(); }

QList<StudioPracticeStep> StudioProfile::practiceSteps() const
{
    return m_practiceSteps.isEmpty() ? builtInPracticeSteps() : m_practiceSteps;
}

bool StudioProfile::practiceStepsAreDefault() const
{
    // What is shown, not what is stored: an INI that holds the shipped text itself (a hand edit) is the default too.
    return practiceSteps() == builtInPracticeSteps();
}

void StudioProfile::setPracticeSteps(const QList<StudioPracticeStep> &v)
{
    QList<StudioPracticeStep> clean = cleanSteps(v);
    if (clean == builtInPracticeSteps()) clean.clear();     // the default again: drop the override
    if (clean == m_practiceSteps) return;
    m_practiceSteps = clean;
    store(StudioKey::PracticeSteps, clean.isEmpty() ? QString() : stepsToJson(clean));
    touch();
}

void StudioProfile::setPracticeStepTexts(const QStringList &heads, const QStringList &bodies)
{
    QList<StudioPracticeStep> steps;
    for (qsizetype i = 0; i < qMax(heads.size(), bodies.size()); ++i)
        steps.append({ heads.value(i), bodies.value(i) });
    setPracticeSteps(steps);
}

void StudioProfile::resetPracticeSteps() { setPracticeSteps(builtInPracticeSteps()); }

QString StudioProfile::defaultEmailSubject() const
{
    return QStringLiteral("Your session report — {date}");
}

QString StudioProfile::defaultEmailBody() const
{
    // Design §8.3, as written. The coach's note is in the PDF and not repeated here.
    return QStringLiteral("Hi {first},\n\n"
                          "Here is your report from today's session. Page 1 has the short version; "
                          "your first work-on is on page 4.\n\n"
                          "{coach}\n"
                          "{studio}");
}

QString StudioProfile::emailSubject() const
{
    return m_emailSubject.isEmpty() ? defaultEmailSubject() : m_emailSubject;
}

QString StudioProfile::emailBody() const
{
    return m_emailBody.isEmpty() ? defaultEmailBody() : m_emailBody;
}

void StudioProfile::setEmailSubject(const QString &v)
{
    QString t = v.trimmed();
    if (t == defaultEmailSubject()) t.clear();
    if (t == m_emailSubject) return;
    m_emailSubject = t;
    store(StudioKey::EmailSubject, t);
    touch();
}

void StudioProfile::setEmailBody(const QString &v)
{
    // Not trimmed inside: the blank lines between paragraphs are the message's shape. Only the
    // whitespace around the whole message goes.
    QString t = v.trimmed();
    if (t == defaultEmailBody()) t.clear();
    if (t == m_emailBody) return;
    m_emailBody = t;
    store(StudioKey::EmailBody, t);
    touch();
}

QString StudioProfile::fill(const QString &tpl, const QMap<QString, QString> &values) const
{
    QString out = tpl;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        out.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
    return out;
}

// ── derived ─────────────────────────────────────────────────────────────────────────────────────

QString StudioProfile::monogram() const
{
    // Initials of the first two significant words. Punctuation goes first ("St. Andrews" → St,
    // Andrews) and stop words are skipped; if a name is ALL stop words, they count after all rather
    // than leaving no monogram. One word gives one letter: "F" for "Fairway" reads as a mark, "FA" as
    // an abbreviation nobody chose.
    QString name = m_studioName.trimmed();
    if (name.isEmpty()) name = QStringLiteral("PinPoint Studio");
    QStringList words;
    for (const QString &w : name.split(QRegularExpression(QStringLiteral("[\\s\\-_/]+")), Qt::SkipEmptyParts)) {
        QString letters;
        for (const QChar c : w) if (c.isLetterOrNumber() || c == u'&') letters += c;
        if (!letters.isEmpty()) words << letters;
    }
    QStringList significant;
    for (const QString &w : std::as_const(words))
        if (!monogramStopWords().contains(w.toLower())) significant << w;
    if (significant.isEmpty()) significant = words;
    QString out;
    for (const QString &w : std::as_const(significant)) {
        if (!w[0].isLetterOrNumber()) continue;
        out += w[0].toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("PS") : out;
}

QString StudioProfile::computeStamp() const
{
    // Everything the PDF prints, plus the logo's and the photo's BYTES (a logo re-imported under the
    // same name is still a different logo). The email subject and message are left out on purpose:
    // they are not in the PDF, and editing the covering note must not put every report out of date.
    QCryptographicHash h(QCryptographicHash::Sha1);
    auto add = [&h](const QString &v) { h.addData(v.toUtf8()); h.addData(QByteArrayView("\x1f", 1)); };
    add(m_studioName); add(accent()); add(m_address); add(m_phone); add(m_email); add(m_website);
    add(m_booking); add(stepsToJson(practiceSteps()));
    add(m_coachName); add(m_coachTitle); add(m_coachEmail); add(m_coachPhone);
    for (const QString &path : { m_logoPath, m_coachPhotoPath }) {
        add(QFileInfo(path).suffix().toLower());
        h.addData(m_repo.copyBytes(path));                  // nothing for no copy, or one that cannot be read
        add(QString());
    }
    return QString::fromLatin1(h.result().toHex().left(12));
}

QString StudioProfile::stamp() const { return m_stamp; }

QList<StudioMissingDetail> StudioProfile::missingDetails() const
{
    QList<StudioMissingDetail> out;
    auto miss = [&out](const QString &text) { out.append({ text, QStringLiteral("studio") }); };
    if (m_studioName.isEmpty()) miss(tr("No studio name yet — add it in Settings → Studio"));
    if (m_coachName.isEmpty())  miss(tr("No coach name yet — add it in Settings → Studio"));
    if (m_coachEmail.isEmpty()) miss(tr("No coach email yet, so replies have nowhere to go — add it in Settings → Studio"));
    return out;
}

// ── the logo and the coach's photo ─────────────────────────────────────────────────────────────

bool StudioProfile::importImage(const QString &path, const QString &baseName, StudioKey key, QString &member)
{
    QString local = path.trimmed();
    if (local.startsWith(QLatin1String("file:"))) local = QUrl(local).toLocalFile();
    QString ext = QFileInfo(local).suffix().toLower();
    if (ext == QLatin1String("jpeg")) ext = QStringLiteral("jpg");
    // Read it whole BEFORE the old copy is replaced: re-importing the file that is already the logo
    // (the picker started in the studio folder) must not delete its own source.
    const bool known = ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("svg");
    const std::optional<QByteArray> picked = known ? m_repo.readPicked(local) : std::nullopt;
    if (!picked) {
        ppWarn() << "StudioProfile: cannot import" << local << "— not a readable PNG, JPEG or SVG file";
        return false;
    }
    const QByteArray &bytes = *picked;

    // It must be what its name says. A raster is decoded; an SVG is checked for an <svg element, which
    // is as far as Qt Gui can go without the Svg module — the register's Image does the real decode.
    if (ext == QLatin1String("svg")) {
        if (!bytes.left(4096).contains("<svg")) {
            ppWarn() << "StudioProfile: cannot import" << local << "— no <svg> element";
            return false;
        }
    } else {
        QBuffer buf;
        buf.setData(bytes);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, ext == QLatin1String("png") ? "png" : "jpeg");
        if (reader.read().isNull()) {
            ppWarn() << "StudioProfile: cannot import" << local << "—" << reader.errorString();
            return false;
        }
    }

    // The copy, and the earlier one under another extension gone (logo.png → logo.svg): the folder holds one.
    const QString dest = m_repo.writeCopy(baseName, ext, bytes);
    if (dest.isEmpty()) return false;
    member = dest;
    store(key, dest);
    touch();   // the path may be unchanged; the bytes, and so the stamp, are not
    return true;
}

void StudioProfile::clearImage(StudioKey key, QString &member)
{
    if (member.isEmpty()) return;
    m_repo.removeCopy(member);
    member.clear();
    store(key, QString());
    touch();
}

bool StudioProfile::importLogo(const QString &path)       { return importImage(path, QStringLiteral("logo"),  StudioKey::Logo,       m_logoPath); }
bool StudioProfile::importCoachPhoto(const QString &path) { return importImage(path, QStringLiteral("coach"), StudioKey::CoachPhoto, m_coachPhotoPath); }
void StudioProfile::clearLogo()                           { clearImage(StudioKey::Logo,       m_logoPath); }
void StudioProfile::clearCoachPhoto()                     { clearImage(StudioKey::CoachPhoto, m_coachPhotoPath); }
