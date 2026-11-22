/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// The identity types: what names an athlete, a session and a shot (information_model.md §5.2,
// ID-ATHLETE, ID-SESSION, ID-SHOT; refactor step 2.1). Vocabulary ring, in src/Identity (Mark,
// 15 Nov 2026; DEC-148), so a rule (Coaching, Diagnostics) can carry one without an outward include.
//
// ⚠ AN IDENTITY IS NEVER A FOLDER. None of these has a constructor from a path or a folder name, and
// the only text each one reads is its own uuid (and, for a ShotRef, its own "<uuid>/<n>" form). A
// folder's name is read in two places only (P7, lint A3): `SessionIdentity` (a session older than
// step 2, src/DomainModel/session_identity.h) and `SwingPaths` (a swing folder's number).
//
// Value types: Q_GADGET, hashable (qHash), ordered (operator<), `isNull()` for "no identity". The
// three are distinct types: an AthleteId is never a SessionId, and neither converts from a QUuid or
// a string by accident (every constructor that takes one is explicit).
//
// Header-only. moc sees the gadgets in any target that lists this header among its sources (the app
// does); a target that puts one into a QVariant must list it too, or it links without the metaobject.

#include <QHashFunctions>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringView>
#include <QUuid>

namespace pinpoint {

namespace identity_detail {
// A uuid's text and nothing more: exactly 36 characters, or 38 with a leading '{' and a trailing '}'.
// QUuid::fromString alone reads a uuid PREFIX ("<uuid>/7", "<uuid>_Mark", "{<uuid>"), so a ShotRef's
// text or a folder name that starts with a uuid would silently read as an id.
inline QUuid strictUuid(QStringView text) noexcept
{
    const bool bare   = text.size() == 36;
    const bool braced = text.size() == 38 && text.front() == QLatin1Char('{') && text.back() == QLatin1Char('}');
    return bare || braced ? QUuid::fromString(text) : QUuid();
}
} // namespace identity_detail

// ID-ATHLETE. Minted once when the athlete is created (a v4 uuid); read from the athlete's record or
// a swing header's `athlete.uuid`. The athlete's name and folder are properties, never its identity.
class AthleteId
{
    Q_GADGET
public:
    AthleteId() = default;
    explicit AthleteId(const QUuid &uuid) noexcept : m_uuid(uuid) {}

    // The text of a uuid, braced or not, any case (36 characters, or 38 with both braces). Anything else
    // (a name, a folder, a path, a uuid with anything before or after it) is null.
    static AthleteId fromString(QStringView text) noexcept { return AthleteId(identity_detail::strictUuid(text)); }

    bool  isNull() const noexcept { return m_uuid.isNull(); }
    QUuid uuid() const noexcept { return m_uuid; }
    // Lower case, no braces: the spelling the athlete records and swing headers already use.
    // Empty for a null id.
    Q_INVOKABLE QString toString() const
    {
        return isNull() ? QString() : m_uuid.toString(QUuid::WithoutBraces);
    }

    friend bool operator==(const AthleteId &a, const AthleteId &b) noexcept { return a.m_uuid == b.m_uuid; }
    friend bool operator!=(const AthleteId &a, const AthleteId &b) noexcept { return !(a == b); }
    friend bool operator<(const AthleteId &a, const AthleteId &b) noexcept { return a.m_uuid < b.m_uuid; }

private:
    QUuid m_uuid;
};

// THE UNASSIGNED OWNER'S ID (information_model.md §5.11, R-UNASSIGNED-1; DEC-198; J-66 (coordinator, 22 Nov)): the
// athlete a session captured with no golfer chosen belongs to. One fixed, well-known uuid, the same in every library
// and on every machine, named here once. It is identified by this id only — never by its name or its folder (P7).
inline AthleteId unassignedAthleteId()
{
    static const AthleteId id(QUuid(QStringLiteral("a55164ed-0000-4000-8000-000000000000")));
    return id;
}
inline bool isUnassigned(const AthleteId &id) { return !id.isNull() && id == unassignedAthleteId(); }

// ID-SESSION. A new session's is random (v4), minted at Start (DEC-41); a session older than step 2
// gets a name-based (v5) one from its athlete's id and its folder name (DEC-150), computed by
// `SessionIdentity::legacySessionId` — never here, because that is the one place a folder name is
// read. Kept by a resume (DEC-77).
class SessionId
{
    Q_GADGET
public:
    SessionId() = default;
    explicit SessionId(const QUuid &uuid) noexcept : m_uuid(uuid) {}

    // A fresh random id, for a session started now.
    static SessionId create() { return SessionId(QUuid::createUuid()); }
    // The text of a uuid, braced or not, any case (36 characters, or 38 with both braces). Anything else
    // (a folder name, a path, a ShotRef's "<uuid>/<n>") is null.
    static SessionId fromString(QStringView text) noexcept { return SessionId(identity_detail::strictUuid(text)); }

    bool  isNull() const noexcept { return m_uuid.isNull(); }
    QUuid uuid() const noexcept { return m_uuid; }
    // Lower case, no braces. Empty for a null id.
    Q_INVOKABLE QString toString() const
    {
        return isNull() ? QString() : m_uuid.toString(QUuid::WithoutBraces);
    }

    friend bool operator==(const SessionId &a, const SessionId &b) noexcept { return a.m_uuid == b.m_uuid; }
    friend bool operator!=(const SessionId &a, const SessionId &b) noexcept { return !(a == b); }
    friend bool operator<(const SessionId &a, const SessionId &b) noexcept { return a.m_uuid < b.m_uuid; }

private:
    QUuid m_uuid;
};

// ID-SHOT. A session and a swing number (ID-SWING-NUMBER: ≥ 1, never reissued in its session,
// DEC-91): the only key of a shot outside its own folder. Composed, never parsed from a path.
class ShotRef
{
    Q_GADGET
    Q_PROPERTY(int swingNumber READ swingNumber)
public:
    ShotRef() = default;
    // A null session or a number below 1 gives the null ShotRef (both parts null), so every invalid
    // ref compares equal to ShotRef().
    ShotRef(const SessionId &session, int swingNumber) noexcept
    {
        if (!session.isNull() && swingNumber >= 1) {
            m_session = session;
            m_swing   = swingNumber;
        }
    }

    bool      isNull() const noexcept { return m_session.isNull(); }
    SessionId session() const noexcept { return m_session; }
    int       swingNumber() const noexcept { return m_swing; }

    // "<session uuid>/<swing number>", e.g. "3c852070-1e72-591c-97e1-85b5df9bc128/7": for logs,
    // hashes and QML roles. Empty for a null ref.
    Q_INVOKABLE QString toString() const
    {
        return isNull() ? QString() : m_session.toString() + QLatin1Char('/') + QString::number(m_swing);
    }
    // The inverse of toString() and nothing else: exactly the form it writes (lower-case uuid without
    // braces, '/', the number without sign or leading zeros). A path, a folder name such as
    // "swing_0007", or any other spelling is the null ref.
    static ShotRef fromString(QStringView text)
    {
        const qsizetype slash = text.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return {};
        bool      ok = false;
        const int n  = text.mid(slash + 1).toInt(&ok);
        const ShotRef r(SessionId::fromString(text.left(slash)), ok ? n : 0);
        return r.toString() == text ? r : ShotRef();
    }

    friend bool operator==(const ShotRef &a, const ShotRef &b) noexcept
    {
        return a.m_session == b.m_session && a.m_swing == b.m_swing;
    }
    friend bool operator!=(const ShotRef &a, const ShotRef &b) noexcept { return !(a == b); }
    // By session, then swing number: a session's shots sort in the order they were taken.
    friend bool operator<(const ShotRef &a, const ShotRef &b) noexcept
    {
        if (a.m_session != b.m_session)
            return a.m_session < b.m_session;
        return a.m_swing < b.m_swing;
    }

private:
    SessionId m_session;
    int       m_swing = 0;
};

inline size_t qHash(const AthleteId &id, size_t seed = 0) noexcept { return qHash(id.uuid(), seed); }
inline size_t qHash(const SessionId &id, size_t seed = 0) noexcept { return qHash(id.uuid(), seed); }
inline size_t qHash(const ShotRef &ref, size_t seed = 0) noexcept
{
    return qHashMulti(seed, ref.session().uuid(), ref.swingNumber());
}

} // namespace pinpoint
