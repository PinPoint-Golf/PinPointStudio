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

#include "pp_debug.h"
#include "pp_settings.h"

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>

#include <cmath>

namespace {

constexpr auto kGroup = "Studio";

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
QVariantList builtInPracticeSteps()
{
    auto step = [](const QString &head, const QString &body) {
        QVariantMap m;
        m.insert(QStringLiteral("head"), head);
        m.insert(QStringLiteral("body"), body);
        return QVariant(m);
    };
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
QString stepsToJson(const QVariantList &steps)
{
    QJsonArray arr;
    for (const QVariant &v : steps) {
        const QVariantMap m = v.toMap();
        QJsonObject o;
        o.insert(QStringLiteral("head"), m.value(QStringLiteral("head")).toString());
        o.insert(QStringLiteral("body"), m.value(QStringLiteral("body")).toString());
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList stepsFromJson(const QString &text)
{
    QVariantList out;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray()) return out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m.insert(QStringLiteral("head"), o.value(QStringLiteral("head")).toString());
        m.insert(QStringLiteral("body"), o.value(QStringLiteral("body")).toString());
        out.append(m);
    }
    return out;
}

// Steps normalised for comparison and storage: only head/body, both trimmed, wholly blank steps
// dropped. A QML caller hands over JS objects that may carry anything else (an index, a delegate's
// bookkeeping); none of it belongs in the INI or in the stamp.
QVariantList cleanSteps(const QVariantList &steps)
{
    QVariantList out;
    for (const QVariant &v : steps) {
        const QVariantMap m = v.toMap();
        const QString head = m.value(QStringLiteral("head")).toString().trimmed();
        const QString body = m.value(QStringLiteral("body")).toString().trimmed();
        if (head.isEmpty() && body.isEmpty()) continue;
        QVariantMap c;
        c.insert(QStringLiteral("head"), head);
        c.insert(QStringLiteral("body"), body);
        out.append(c);
    }
    return out;
}

} // namespace

StudioProfile::StudioProfile(QObject *parent) : QObject(parent)
{
    load();
    m_stamp = computeStamp();
}

QString StudioProfile::studioDir()
{
    const QString env = qEnvironmentVariable("PINPOINT_STUDIO_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/studio"));
}

void StudioProfile::load()
{
    QSettings s = ppSettings();
    s.beginGroup(QLatin1String(kGroup));
    auto str = [&s](const char *key) { return s.value(QLatin1String(key)).toString(); };
    m_studioName  = str("name");
    m_accent      = str("accent");
    m_address     = str("address");
    m_phone       = str("phone");
    m_email       = str("email");
    m_website     = str("website");
    m_booking     = str("booking");
    m_practiceSteps = stepsFromJson(str("practiceSteps"));
    m_emailSubject = str("emailSubject");
    m_emailBody    = str("emailBody");
    m_coachName   = str("coachName");
    m_coachTitle  = str("coachTitle");
    m_coachEmail  = str("coachEmail");
    m_coachPhone  = str("coachPhone");
    // A copy that has gone (the app data folder cleared, a restore from another machine) reads as no
    // logo at all: the report then draws the monogram, rather than an empty frame where a logo was.
    const QString logo  = str("logo");
    const QString photo = str("coachPhoto");
    m_logoPath       = (!logo.isEmpty()  && QFileInfo::exists(logo))  ? logo  : QString();
    m_coachPhotoPath = (!photo.isEmpty() && QFileInfo::exists(photo)) ? photo : QString();
    s.endGroup();
}

void StudioProfile::store(const char *key, const QString &value)
{
    QSettings s = ppSettings();
    s.beginGroup(QLatin1String(kGroup));
    if (value.isEmpty()) s.remove(QLatin1String(key));
    else                 s.setValue(QLatin1String(key), value);
    s.endGroup();
    s.sync();
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

PP_STUDIO_TEXT(studioName, setStudioName, m_studioName, "name")
PP_STUDIO_TEXT(address,    setAddress,    m_address,    "address")
PP_STUDIO_TEXT(phone,      setPhone,      m_phone,      "phone")
PP_STUDIO_TEXT(email,      setEmail,      m_email,      "email")
PP_STUDIO_TEXT(website,    setWebsite,    m_website,    "website")
PP_STUDIO_TEXT(booking,    setBooking,    m_booking,    "booking")
PP_STUDIO_TEXT(coachName,  setCoachName,  m_coachName,  "coachName")
PP_STUDIO_TEXT(coachTitle, setCoachTitle, m_coachTitle, "coachTitle")
PP_STUDIO_TEXT(coachEmail, setCoachEmail, m_coachEmail, "coachEmail")
PP_STUDIO_TEXT(coachPhone, setCoachPhone, m_coachPhone, "coachPhone")

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
    store("accent", norm);
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

QVariantList StudioProfile::defaultPracticeSteps() const { return builtInPracticeSteps(); }

QVariantList StudioProfile::practiceSteps() const
{
    return m_practiceSteps.isEmpty() ? builtInPracticeSteps() : m_practiceSteps;
}

void StudioProfile::setPracticeSteps(const QVariantList &v)
{
    QVariantList clean = cleanSteps(v);
    if (clean == builtInPracticeSteps()) clean.clear();     // the default again: drop the override
    if (clean == m_practiceSteps) return;
    m_practiceSteps = clean;
    store("practiceSteps", clean.isEmpty() ? QString() : stepsToJson(clean));
    touch();
}

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
    store("emailSubject", t);
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
    store("emailBody", t);
    touch();
}

QString StudioProfile::fill(const QString &tpl, const QVariantMap &values) const
{
    QString out = tpl;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        out.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value().toString());
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
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::ReadOnly)) h.addData(f.readAll());
        add(QString());
    }
    return QString::fromLatin1(h.result().toHex().left(12));
}

QString StudioProfile::stamp() const { return m_stamp; }

QVariantList StudioProfile::missingDetails() const
{
    QVariantList out;
    auto miss = [&out](const QString &text) {
        QVariantMap m;
        m.insert(QStringLiteral("text"), text);
        m.insert(QStringLiteral("where"), QStringLiteral("studio"));
        out.append(m);
    };
    if (m_studioName.isEmpty()) miss(tr("No studio name yet — add it in Settings → Studio"));
    if (m_coachName.isEmpty())  miss(tr("No coach name yet — add it in Settings → Studio"));
    if (m_coachEmail.isEmpty()) miss(tr("No coach email yet, so replies have nowhere to go — add it in Settings → Studio"));
    return out;
}

// ── the logo and the coach's photo ─────────────────────────────────────────────────────────────

bool StudioProfile::importImage(const QString &path, const QString &baseName, const char *key, QString &member)
{
    QString local = path.trimmed();
    if (local.startsWith(QLatin1String("file:"))) local = QUrl(local).toLocalFile();
    const QFileInfo fi(local);
    QString ext = fi.suffix().toLower();
    if (ext == QLatin1String("jpeg")) ext = QStringLiteral("jpg");
    if (!fi.isFile() || !fi.isReadable()
        || !(ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("svg"))) {
        ppWarn() << "StudioProfile: cannot import" << local << "— not a readable PNG, JPEG or SVG file";
        return false;
    }

    // Read it whole BEFORE removing the old copy: re-importing the file that is already the logo
    // (the picker started in the studio folder) must not delete its own source.
    QFile in(local);
    if (!in.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = in.readAll();
    in.close();

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

    const QString dir = studioDir();
    if (!QDir().mkpath(dir)) {
        ppWarn() << "StudioProfile: cannot create" << dir;
        return false;
    }
    const QString dest = dir + QLatin1Char('/') + baseName + QLatin1Char('.') + ext;
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        ppWarn() << "StudioProfile: cannot write" << dest << out.errorString();
        return false;
    }
    // The earlier copy under another extension (logo.png → logo.svg) goes, so the folder holds one.
    for (const char *other : { "png", "jpg", "svg" }) {
        const QString p = dir + QLatin1Char('/') + baseName + QLatin1Char('.') + QLatin1String(other);
        if (p != dest) QFile::remove(p);
    }
    member = dest;
    store(key, dest);
    touch();   // the path may be unchanged; the bytes, and so the stamp, are not
    return true;
}

void StudioProfile::clearImage(const char *key, QString &member)
{
    if (member.isEmpty()) return;
    QFile::remove(member);
    member.clear();
    store(key, QString());
    touch();
}

bool StudioProfile::importLogo(const QString &path)       { return importImage(path, QStringLiteral("logo"),  "logo",       m_logoPath); }
bool StudioProfile::importCoachPhoto(const QString &path) { return importImage(path, QStringLiteral("coach"), "coachPhoto", m_coachPhotoPath); }
void StudioProfile::clearLogo()                           { clearImage("logo",       m_logoPath); }
void StudioProfile::clearCoachPhoto()                     { clearImage("coachPhoto", m_coachPhotoPath); }
