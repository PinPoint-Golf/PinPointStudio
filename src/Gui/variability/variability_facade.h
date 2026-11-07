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

// THE VARIABILITY FAÇADE (docs/design/variability-mark/spec.md §4.1, §5, §7.5, §8): what QML may know
// about a variability grade, and nothing it could grade with. QML never grades, never computes ρ and
// never builds a basis string; a presenter grades in C++ (src/Metrics/variability/) and hands QML a
// `variabilityResult` built here. PpVariabilityMark and PpVariabilityBadge take its `grade`; their words
// come from the vocabulary singletons in QML (§7.1 – §7.4), not from this file.
//
// Large payloads (a session of results, a funnel's per-swing series) go to QML through QmlPayload
// (src/Gui/shot/qml_payload.h), not as a QVariantMap property: this value type is for ONE result.

#include "../../Metrics/variability/variability_result.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cmath>

namespace pinpoint::variability {

// One VariabilityResult as QML reads it: read-only, every value computed in C++.
//
// ⚠ `grade` IS THE COMPUTED GRADE, ALSO WHEN `measurementLimited` IS SET. §8.6 has the golfer layer show
// the grade-0 "Checking" mark in its place, and the technical layer show the grade with
// "· measurement-limited" (already in `basisText`). Which layer a surface is drawing is the surface's
// business, so the substitution is NOT made here: the surface (or the component it passes `grade` to)
// reads `measurementLimited` and chooses. Baking it in would leave the technical layer no grade to show.
//
// Tokens, spelled once here and nowhere else in C++:
//   basis           "scalar" | "trace" | "ordinal" | "measurement"
//   domain          "consistency" (scalar, trace, ordinal) | "measurement" (the Needle, §4.6)
//   ungradedReason  "" (none) | "tooFewSwings" | "noScale" | "nonPositiveForRatio" | "noUncertainty"
//                   | "scaleTypeMismatch" | "malformedInput"
//                   Empty when there is no reason, so `if (r.ungradedReason)` reads as "is there one".
class VariabilityResultView
{
    Q_GADGET
    QML_VALUE_TYPE(variabilityResult)

    Q_PROPERTY(int     grade              READ grade              CONSTANT FINAL)
    Q_PROPERTY(bool    graded             READ graded             CONSTANT FINAL)
    Q_PROPERTY(QString basis              READ basis              CONSTANT FINAL)
    Q_PROPERTY(QString ungradedReason     READ ungradedReason     CONSTANT FINAL)
    Q_PROPERTY(bool    measurementLimited READ measurementLimited CONSTANT FINAL)
    Q_PROPERTY(int     n                  READ n                  CONSTANT FINAL)
    Q_PROPERTY(double  rho                READ rho                CONSTANT FINAL)
    Q_PROPERTY(bool    hasRho             READ hasRho             CONSTANT FINAL)
    Q_PROPERTY(QString basisText          READ basisText          CONSTANT FINAL)
    Q_PROPERTY(QString domain             READ domain             CONSTANT FINAL)

public:
    // An ungraded scalar result: no mark (`graded` false), the state a surface shows before a
    // presenter has handed it anything.
    VariabilityResultView() = default;

    // `unit` and `decimals` are the measure's, as its descriptor states them; they go straight to
    // VariabilityResult::basisString (§7.5), which is the only place a basis string is built.
    static VariabilityResultView fromResult(const VariabilityResult &result, const QString &unit = {},
                                            int decimals = 1);

    // The token spellings, public so a presenter writing a QVariantMap (and the test) names the same
    // words this type does.
    static QString basisToken(Basis basis);
    static QString domainToken(Basis basis);
    static QString ungradedReasonToken(UngradedReason reason);

    int     grade() const              { return m_grade; }
    bool    graded() const             { return m_graded; }
    QString basis() const              { return m_basis; }
    QString ungradedReason() const     { return m_ungradedReason; }
    bool    measurementLimited() const { return m_measurementLimited; }
    int     n() const                  { return m_n; }
    // ρ as the grader computed it (VariabilityResult::rho: scalar ρ, or a trace's max(ρₐ, ρₜ)). NaN when
    // there is none: an ordinal or measurement result, or no grade. QML tests `hasRho` (or isNaN) and
    // never prints a NaN.
    double  rho() const                { return m_rho; }
    bool    hasRho() const             { return std::isfinite(m_rho); }
    // The §7.5 technical-layer string, "" when §7.5 has no row for the result (ungraded, grade 0, or
    // the Needle).
    QString basisText() const          { return m_basisText; }
    QString domain() const             { return m_domain; }

    // By hand, not defaulted: two NaN ρ are the SAME "no ρ", so an ordinal, Needle or ungraded view
    // equals its copy and a NOTIFY guard does not fire on every unchanged write.
    bool operator==(const VariabilityResultView &o) const
    {
        const bool sameRho = (std::isnan(m_rho) && std::isnan(o.m_rho)) || m_rho == o.m_rho;
        return sameRho && m_grade == o.m_grade && m_graded == o.m_graded && m_basis == o.m_basis
            && m_ungradedReason == o.m_ungradedReason && m_measurementLimited == o.m_measurementLimited
            && m_n == o.m_n && m_basisText == o.m_basisText && m_domain == o.m_domain;
    }

private:
    int     m_grade              = 0;
    bool    m_graded             = false;
    QString m_basis              = basisToken(Basis::Scalar);
    QString m_ungradedReason;
    bool    m_measurementLimited = false;
    int     m_n                  = 0;
    double  m_rho                = VariabilityResult::kNaN;
    QString m_basisText;
    QString m_domain             = domainToken(Basis::Scalar);
};

// `Variability` in QML: the §8.10 constants a surface may legitimately need ("Not enough swings" counts
// against N_MIN; a window label names N_WINDOW; a trend needs TREND_MIN_SESSIONS). Read from
// variability_constants.h, never copied. Deliberately nothing else: there is no grading Q_INVOKABLE
// here, and there must never be one.
class VariabilityFacade : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Variability)
    QML_SINGLETON

    Q_PROPERTY(int nMin             READ nMin             CONSTANT FINAL)
    Q_PROPERTY(int nWindow          READ nWindow          CONSTANT FINAL)
    Q_PROPERTY(int trendMinSessions READ trendMinSessions CONSTANT FINAL)

public:
    explicit VariabilityFacade(QObject *parent = nullptr) : QObject(parent) {}

    int nMin() const             { return kNMin; }
    int nWindow() const          { return kNWindow; }
    int trendMinSessions() const { return kTrendMinSessions; }
};

} // namespace pinpoint::variability
