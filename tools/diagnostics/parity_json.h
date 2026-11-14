// parity_json.h — the one JSON writer the refactor's parity dumps share (regrade_ledger --dump,
// athlete_dump). Header-only, tools only; nothing in the app includes it.
//
// A parity dump is DIFFED, byte for byte and field by field (tools/parity, Step G of the refactor plan),
// so it is written to one canonical form, whatever produced the values:
//   - object keys sorted (byte order of their UTF-8), two-space indent, one value per line;
//   - every floating-point number with full precision, "%.17g", so a change in the last bit shows; an
//     integer as an integer; a non-finite number as null (JSON has no NaN);
//   - nothing of the run itself: keys naming the moment a file was written (kRunKeys) are dropped, and
//     a string holding one of the run's absolute folders has that folder replaced by a token
//     ("<session>", "<athlete>"), so two runs on two scratch copies diff clean.
// What the values ARE is never touched: no rounding, no reordering of arrays (an array's order is the
// producer's, and a change of it is a difference worth seeing).

#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMap>
#include <QMetaType>
#include <QPair>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace parity {

// The keys that say WHEN a file was written, never what it says: dropped wherever they occur.
// `fingerprint` is a cache key over file sizes and mtimes (a scratch copy has its own), not an output.
inline const QStringList &kRunKeys()
{
    static const QStringList k{ QStringLiteral("writtenAtMs"), QStringLiteral("updatedAtMs"),
                                QStringLiteral("atMs"),        QStringLiteral("frozenAt"),
                                QStringLiteral("fingerprint") };
    return k;
}

// The run's absolute folders, longest first, each with the token that stands for it.
using PathTokens = QVector<QPair<QString, QString>>;

inline void addPathToken(PathTokens &t, const QString &dir, const QString &token)
{
    const auto add = [&t, &token](QString d) {
        while (d.endsWith(QLatin1Char('/')) && d.size() > 1) d.chop(1);
        if (d.isEmpty()) return;
        for (const auto &p : std::as_const(t)) if (p.first == d) return;
        t.append({ d, token });
    };
    add(QFileInfo(dir).absoluteFilePath());
    const QString canon = QFileInfo(dir).canonicalFilePath();
    if (!canon.isEmpty()) add(canon);
    std::sort(t.begin(), t.end(), [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
}

inline QString scrubString(QString s, const PathTokens &tokens)
{
    for (const auto &p : tokens) s.replace(p.first, p.second);
    return s;
}

// A value with the run taken out of it: run keys dropped, folders tokenised, every map a QVariantMap.
inline QVariant scrub(const QVariant &v, const PathTokens &tokens)
{
    switch (v.typeId()) {
    case QMetaType::QVariantMap: {
        QVariantMap out;
        const QVariantMap m = v.toMap();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            if (!kRunKeys().contains(it.key())) out.insert(scrubString(it.key(), tokens), scrub(it.value(), tokens));
        return out;
    }
    case QMetaType::QVariantHash: {
        QVariantMap out;
        const QVariantHash h = v.toHash();
        for (auto it = h.constBegin(); it != h.constEnd(); ++it)
            if (!kRunKeys().contains(it.key())) out.insert(scrubString(it.key(), tokens), scrub(it.value(), tokens));
        return out;
    }
    case QMetaType::QVariantList: {
        QVariantList out;
        for (const QVariant &e : v.toList()) out.append(scrub(e, tokens));
        return out;
    }
    case QMetaType::QStringList: {
        QVariantList out;
        for (const QString &e : v.toStringList()) out.append(scrubString(e, tokens));
        return out;
    }
    case QMetaType::QString:
        return scrubString(v.toString(), tokens);
    default:
        return v;
    }
}

inline void appendEscaped(QByteArray &out, const QString &s)
{
    out += '"';
    const QByteArray u = s.toUtf8();
    for (const char c : u) {
        const unsigned char uc = static_cast<unsigned char>(c);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (uc < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", uc);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

// "%.17g", but never in the process's locale: QCoreApplication calls setlocale(LC_ALL, "") on Unix, and
// printf would then write "0,1…" under a comma locale (invalid JSON, and a baseline that differs by machine).
// QByteArray::number is the C locale always; its 'g' at precision 17 is printf's %.17g, exponent included
// ("1e+20", "1.0000000000000001e-07"). Pinned in parity_json_test, under a comma locale too.
inline void appendNumber(QByteArray &out, double d)
{
    if (!std::isfinite(d)) { out += "null"; return; }
    if (d == 0.0) { out += std::signbit(d) ? "-0" : "0"; return; }
    out += QByteArray::number(d, 'g', 17);
}

inline void write(QByteArray &out, const QVariant &v, int depth)
{
    const QByteArray pad(2 * (depth + 1), ' '), padEnd(2 * depth, ' ');
    switch (v.typeId()) {
    case QMetaType::UnknownType:
    case QMetaType::Nullptr:
        out += "null";
        return;
    case QMetaType::Bool:
        out += v.toBool() ? "true" : "false";
        return;
    case QMetaType::Int: case QMetaType::Short: case QMetaType::Long: case QMetaType::LongLong:
    case QMetaType::Char: case QMetaType::SChar:
        out += QByteArray::number(v.toLongLong());
        return;
    case QMetaType::UInt: case QMetaType::UShort: case QMetaType::ULong: case QMetaType::ULongLong:
    case QMetaType::UChar:
        out += QByteArray::number(v.toULongLong());
        return;
    case QMetaType::Double: case QMetaType::Float:
        appendNumber(out, v.toDouble());
        return;
    case QMetaType::QVariantMap: case QMetaType::QVariantHash: {
        QVariantMap m = v.typeId() == QMetaType::QVariantMap ? v.toMap() : QVariantMap();
        if (v.typeId() == QMetaType::QVariantHash) {
            const QVariantHash h = v.toHash();
            for (auto it = h.constBegin(); it != h.constEnd(); ++it) m.insert(it.key(), it.value());
        }
        if (m.isEmpty()) { out += "{}"; return; }
        // QMap orders by QString::operator<, UTF-16; sort by UTF-8 bytes so any reader agrees.
        QList<QByteArray> keys;
        QHash<QByteArray, QString> byUtf8;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            keys << it.key().toUtf8();
            byUtf8.insert(keys.back(), it.key());
        }
        std::sort(keys.begin(), keys.end());
        out += "{\n";
        for (qsizetype i = 0; i < keys.size(); ++i) {
            out += pad;
            appendEscaped(out, byUtf8.value(keys[i]));
            out += ": ";
            write(out, m.value(byUtf8.value(keys[i])), depth + 1);
            out += i + 1 < keys.size() ? ",\n" : "\n";
        }
        out += padEnd;
        out += '}';
        return;
    }
    case QMetaType::QVariantList: case QMetaType::QStringList: {
        const QVariantList l = v.typeId() == QMetaType::QStringList
                                   ? [&v] { QVariantList r; for (const QString &s : v.toStringList()) r << s; return r; }()
                                   : v.toList();
        if (l.isEmpty()) { out += "[]"; return; }
        out += "[\n";
        for (qsizetype i = 0; i < l.size(); ++i) {
            out += pad;
            write(out, l[i], depth + 1);
            out += i + 1 < l.size() ? ",\n" : "\n";
        }
        out += padEnd;
        out += ']';
        return;
    }
    case QMetaType::QString:
        appendEscaped(out, v.toString());
        return;
    default:
        // Anything else the model hands out (a QUrl, a date) is written as its string; never a hash order.
        appendEscaped(out, v.toString());
        return;
    }
}

inline QByteArray toCanonicalJson(const QVariant &v)
{
    QByteArray out;
    write(out, v, 0);
    out += '\n';
    return out;
}

// A JSON file on disk as a variant (null when absent or unreadable — the dump says so in `why`).
inline QVariant readJsonFile(const QString &path, QString *why = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why) *why = QStringLiteral("absent");
        return QVariant();
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        if (why) *why = QStringLiteral("unreadable: %1 at byte %2").arg(err.errorString()).arg(err.offset);
        return QVariant();
    }
    return doc.toVariant();
}

// The content the run graded against, by environment variable: each file's SHA-1, never its path.
// Every PINPOINT_CORE_* the diagnostics read (the pack, the norms, the flight table, the contexts,
// the screens, the drills, the gate words, the references).
inline const QStringList &contentVariables()
{
    static const QStringList k{ QStringLiteral("PINPOINT_CORE_PACK"),        QStringLiteral("PINPOINT_CORE_NORMS"),
                                QStringLiteral("PINPOINT_CORE_FLIGHT_NORMS"), QStringLiteral("PINPOINT_CORE_CONTEXTS"),
                                QStringLiteral("PINPOINT_CORE_SCREENS"),      QStringLiteral("PINPOINT_CORE_DRILLS"),
                                QStringLiteral("PINPOINT_CORE_GATE_WORDS"),   QStringLiteral("PINPOINT_CORE_REFERENCES") };
    return k;
}

// The ones unset or naming no readable file, for the refusal; empty when all are in hand.
inline QStringList missingContent()
{
    QStringList out;
    for (const QString &var : contentVariables()) {
        const QString path = qEnvironmentVariable(var.toLatin1().constData());
        if (path.isEmpty() || !QFileInfo(path).isFile()) out << var;
    }
    return out;
}

inline QVariantMap contentHashes()
{
    QVariantMap out;
    for (const QString &var : contentVariables()) {
        QFile f(qEnvironmentVariable(var.toLatin1().constData()));
        out.insert(var, f.open(QIODevice::ReadOnly)
                            ? QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex())
                            : QString());
    }
    return out;
}

inline bool writeFile(const QString &path, const QByteArray &bytes, QString *why)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (why) *why = out.errorString();
        return false;
    }
    return true;
}

} // namespace parity
