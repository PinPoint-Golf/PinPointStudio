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

// THE STAND-IN DEVICES' SWITCHES. A stand-in replaces a real device's lowest layer inside the real app, so a quit test
// can make that device's thread busy with no hardware (testing_developer_guide.md, "The quit pins"). Built into the
// Debug app only: the option PP_SYNTHETIC_DEVICES (the root CMakeLists.txt) is on by default in a Debug build and is
// refused in every other build type, so a Release build, an installer or a local release never holds one.
//
// A STAND-IN STARTS only when all three hold: the build has them (kBuilt), the app runs under the headless start
// (PINPOINT_HEADLESS_PROBE=1, headless_probe.h), and PINPOINT_PROBE_SYNTHETIC names its part. Its gate sits beside its
// owner's "[HeadlessProbe] not started: ..." gate; with the switch off every real device keeps refusing as today.
//
//   PINPOINT_PROBE_SYNTHETIC=<part>[,<part>]...    the stand-ins to start, e.g. "camera" or "camera,imu"
//   PINPOINT_PROBE_SYNTHETIC_SLOW=<part>.<step>:<ms>
//                                                   one call of one stand-in made to take <ms> ms (1 to 120000), e.g.
//                                                   "camera.stop:3000": its stop, its model load or its disconnect,
//                                                   so a test can show what the owner does while it waits
//
// A name is lower-case letters, digits and '-', starting with a letter and not ending with '-', at most 32 characters.
// Parsed strictly: no spaces, no empty item, no repeated part, no sign, no leading zero, no unit. A malformed value is
// logged once per read ("[HeadlessProbe] synthetic switch ignored: ...") and ignored as a whole, never acted on in
// part. Set but empty is malformed too, where the platform keeps an empty variable; on Windows an empty assignment
// removes the variable, so it reads as unset. Both are read from the environment on every call: each stand-in asks
// once, as it starts.
//
// THE LOG CONVENTION. Each stand-in, as it starts, logs exactly one line through logStarted():
// "[HeadlessProbe] synthetic: <part> <what>". The quit pins' driver looks for it before the quit; without it a pin
// that needs the stand-in is Not Run, never passed (src/Gui/tests/quit_pins/quit_pin.py).
//
// The option reaches this header through the app's generated pp_synthetic_devices.h (on the app's include path);
// anything else that includes it (a test tree) has no stand-ins unless it defines PP_SYNTHETIC_DEVICES=1 itself. Ask
// kBuilt, never the macro: a file that tests the macro without including this header reads it as 0.

#include "headless_probe.h"
#include "pp_debug.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <optional>

#ifndef PP_SYNTHETIC_DEVICES
#  if __has_include("pp_synthetic_devices.h")
#    include "pp_synthetic_devices.h"
#  endif
#endif
#ifndef PP_SYNTHETIC_DEVICES
#  define PP_SYNTHETIC_DEVICES 0
#endif

namespace pinpoint::synthetic {

inline constexpr bool kBuilt = PP_SYNTHETIC_DEVICES == 1;
inline constexpr int kSlowMaxMs = 120000;   // two minutes: past any pin's own timeout, so never the limit in practice
inline constexpr int kNameMax = 32;
inline constexpr const char *kPartsVar = "PINPOINT_PROBE_SYNTHETIC";
inline constexpr const char *kSlowVar = "PINPOINT_PROBE_SYNTHETIC_SLOW";

struct Slow {
    QByteArray call;   // "<part>.<step>"
    int ms = 0;
};

// A part's or a step's name (the rule above).
inline bool isName(QByteArrayView s)
{
    if (s.isEmpty() || s.size() > kNameMax || s.front() < 'a' || s.front() > 'z' || s.back() == '-')
        return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}

// "<part>[,<part>]..." -> the parts, in order; nullopt when malformed.
inline std::optional<QList<QByteArray>> parseParts(QByteArrayView v)
{
    QList<QByteArray> parts;
    for (QByteArrayView item : QByteArray(v.data(), v.size()).split(',')) {
        if (!isName(item))
            return std::nullopt;
        const QByteArray p = item.toByteArray();
        if (parts.contains(p))
            return std::nullopt;
        parts << p;
    }
    return parts;
}

// "<part>.<step>:<ms>" -> the call and its delay; nullopt when malformed.
inline std::optional<Slow> parseSlow(QByteArrayView v)
{
    const qsizetype colon = v.indexOf(':');
    if (colon < 0 || v.indexOf(':', colon + 1) >= 0)
        return std::nullopt;
    const QByteArrayView call = v.first(colon), digits = v.sliced(colon + 1);
    const qsizetype dot = call.indexOf('.');
    if (dot < 0 || !isName(call.first(dot)) || !isName(call.sliced(dot + 1)))
        return std::nullopt;
    if (digits.isEmpty() || digits.size() > 6 || digits.front() == '0')
        return std::nullopt;
    int ms = 0;
    for (char c : digits) {
        if (c < '0' || c > '9')
            return std::nullopt;
        ms = ms * 10 + (c - '0');
    }
    if (ms > kSlowMaxMs)
        return std::nullopt;
    return Slow{call.toByteArray(), ms};
}

namespace detail {

// The value of a switch that is to be read: nullopt (and nothing said) unless the app is a headless probe and the
// switch is set; set in a build without the stand-ins, it is said once per read and ignored.
inline std::optional<QByteArray> readSwitch(const char *var)
{
    if (!headlessProbe() || !qEnvironmentVariableIsSet(var))
        return std::nullopt;
    if (!kBuilt) {
        ppWarn() << "[HeadlessProbe] synthetic switch ignored:" << var
                 << "is set, but this build has no stand-in devices (a Debug build has them: PP_SYNTHETIC_DEVICES)";
        return std::nullopt;
    }
    return qgetenv(var);
}

inline void malformed(const char *var, const QByteArray &value, const char *want)
{
    ppWarn() << "[HeadlessProbe] synthetic switch ignored:" << var << "=" << value << "is malformed (want" << want
             << "): nothing is started or slowed by it";
}

} // namespace detail

// Whether the stand-in for `part` is to start.
inline bool on(const char *part)
{
    const auto v = detail::readSwitch(kPartsVar);
    if (!v)
        return false;
    const auto parts = parseParts(*v);
    if (!parts) {
        detail::malformed(kPartsVar, *v, "<part>[,<part>]...");
        return false;
    }
    return parts->contains(QByteArray(part));
}

// How long the stand-in's call "<part>.<step>" is to take, in ms; 0 for "as fast as it is".
inline int slowMs(const char *call)
{
    const auto v = detail::readSwitch(kSlowVar);
    if (!v)
        return 0;
    const auto slow = parseSlow(*v);
    if (!slow) {
        detail::malformed(kSlowVar, *v, "<part>.<step>:<ms>, ms 1 to 120000");
        return 0;
    }
    return slow->call == QByteArray(call) ? slow->ms : 0;
}

// The one line a stand-in logs as it starts (the log convention above). `what` says what it stands in for.
inline void logStarted(const char *part, const char *what)
{
    ppInfo() << "[HeadlessProbe] synthetic:" << part << what;
}

} // namespace pinpoint::synthetic
