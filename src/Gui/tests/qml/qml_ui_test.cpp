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

// Driver for the QML UI suite — the tst_*.qml files beside this one.
//
// These are the tests that load a real component, render it offscreen and press it. The other Gui
// suites are C++ model tests that pull in nothing of the app; this one asserts a QML component's
// behaviour, so it needs the app's QML module — and it DECLARES that module rather than borrowing
// it (src/Gui/tests/CMakeLists.txt, from the lists in cmake/PinPointQmlModule.cmake). It used to
// live in the app build for want of any other way to reach the module, which meant it could only
// run after whisper, FFmpeg, OpenCV and espeak had built, and so it ran in no release gate at all.
//
// Two things the setup still has to supply, and they are why this is a binary rather than a bare
// qmltestrunner invocation:
//
//   appSettings. Theme is a singleton every component under test depends on, and it reads its
//   values off the `appSettings` context property in Component.onCompleted. Without one, Theme
//   half-initialises with a ReferenceError and every test runs against a theme that failed — which
//   would either mask real failures or invent ones. The REAL AppSettings is used rather than a stub,
//   so it cannot drift from what the app hands the same singleton, with its QSettings redirected to
//   a scratch directory first so a test run cannot write to the developer's own preferences.
//
//   THE BUNDLED FONTS, and this suite ran for months without them. See loadBundledFonts() below —
//   a layout test that measures a fallback face is measuring a font the application never renders
//   with, and it reached two different wrong answers on two platforms.

#include <QtQuickTest>

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>

// AppSettings only. ChartMetrics and TimelineLabels were included to register them by hand; the
// module registers its own types now, so naming them here would claim a dependency this file
// does not have.
#include "app_settings.h"

// The Charts panel's fixture feeder (below): the presenters' setSnapshot is C++-only.
#include "charts/charts_relations_presenter.h"
#include "charts/charts_traces_presenter.h"
#include "charts/charts_wrist_presenter.h"
#include "charts/charts_posture_presenter.h"
#include "charts/charts_delivery_presenter.h"
#include "charts/session_wrist.h"
#include "tests/charts_maths_fixture.h"

#include <QQmlContext>
#include <QVariantList>
#include <algorithm>
#include <cmath>

// ⚠ THE FONTS ARE THE UNITS THIS SUITE MEASURES IN, AND IT USED TO RUN WITHOUT THEM.
//
// Theme.qml names families — "Geist Mono", "Hanken Grotesk" — and notes that it "falls back to the
// system default if the font file is not installed". main.cpp loads the bundled faces at startup
// (QFontDatabase::addApplicationFont over :/fonts) precisely so those names resolve everywhere. This
// binary does not run main.cpp, so it never did.
//
// That is not a cosmetic difference for a suite whose central assertion is "no annotation escapes
// its card": a fallback face has different advance widths, so the text being measured is not the
// text the application draws. And because the fallback is chosen by the HOST, the suite reached two
// different wrong answers — neither this Mac nor the studio PC has Geist Mono installed, macOS fell
// back to something narrow and passed, Windows fell back to something wider and failed four
// containment assertions in tst_lm_graphics. The failures were real; the cause was here, not in
// PpLmGraphicsBody.
//
// ENUMERATED, NOT LISTED. The faces come from the same cmake/PinPointFonts.cmake list the app
// compiles into :/fonts, and reading the directory back means adding a face does not require
// remembering this file. A missing directory is a hard failure rather than a warning: silently
// measuring the wrong font is the exact failure being fixed, so it must never be the quiet path.
static void loadBundledFonts()
{
    const QDir dir(QStringLiteral(":/fonts"));
    const QStringList faces = dir.entryList({ QStringLiteral("*.ttf") }, QDir::Files, QDir::Name);
    if (faces.isEmpty())
        qFatal("qml_ui_test: no fonts under :/fonts — the layout assertions would measure a "
               "host-chosen fallback face rather than the faces the app ships. Check that the "
               "target compiles in the app_fonts resource (cmake/PinPointFonts.cmake).");

    for (const QString &face : faces) {
        if (QFontDatabase::addApplicationFont(dir.filePath(face)) < 0)
            qFatal("qml_ui_test: failed to load bundled font %s", qPrintable(face));
    }
}

// ── chartsFixture: the REAL 4 July session fed to the Charts panel's REAL presenters ─────────────
//
// tst_charts_panel.qml and tst_chart_presets.qml drive the Charts body over ChartsTracesPresenter and
// ChartsRelationsPresenter — the app's own, unmodified — but a presenter is fed a SessionSnapshot only
// from C++ (setSnapshot; in the app the SessionSeriesModel reads swing documents off-thread). This
// object is that C++: it reads the trimmed 4 July fixture the presenter unit tests read
// (src/Gui/tests/data/charts/session_0704.json, through charts_maths_fixture.h), gives every shot a
// swing folder (the carousel identity the panel picks by), and hands the snapshot to the presenters.
// No files are written and no swing document is opened, so the suite stays fast and deterministic.
//
// It also lets a test file put its own QML fakes of the app's controllers (shotReplay, navController,
// sessionReviewController, shotProcessor, reanalysisController) on ITS engine's root context —
// provide(name, object) — so PpChartsPanel's real wiring runs. Only the files that call it get them:
// each tst_*.qml has its own engine, and nothing is put on a context by default.
class ChartsFixture : public QObject
{
    Q_OBJECT

public:
    ChartsFixture(QQmlEngine *engine, QObject *parent) : QObject(parent), m_engine(engine) {}

    // The fixture's session folder and a shot's swing folder (ordinal 1..15). Absolute the way
    // SessionSeriesModel makes it (QFileInfo::absoluteFilePath): "/fx/…" on Linux and macOS,
    // "C:/fx/…" on Windows — a bare "/fx/…" there never compares equal to what the model reports.
    Q_INVOKABLE QString sessionDir() const
    {
        return QDir::cleanPath(QFileInfo(QStringLiteral("/fx/2026-07-04_Mark-Liversedge_Wrist_01")).absoluteFilePath());
    }
    Q_INVOKABLE QString dirOf(int ordinal) const
    {
        return sessionDir() + QStringLiteral("/swing_%1").arg(ordinal, 4, 10, QLatin1Char('0'));
    }
    Q_INVOKABLE int count() { return load() ? int(m_fx.session.shots.size()) : 0; }

    // A shot as shotReplay.analysisDetail carries it (disk_replay_source.cpp's shape).
    Q_INVOKABLE QVariantMap detailOf(int ordinal)
    {
        using namespace pinpoint::charts;
        if (!load() || ordinal < 1 || ordinal > int(m_fx.session.shots.size())) return {};
        // after a `delivery: true` feed the replay's detail carries the same Club delivery readings as the snapshot
        ShotSeries withDelivery;
        if (m_deliveryFed) { withDelivery = *m_fx.session.shots[size_t(ordinal - 1)]; withDeliveryReadings(withDelivery); }
        const ShotSeries &s = m_deliveryFed ? withDelivery : *m_fx.session.shots[size_t(ordinal - 1)];
        QVariantList series, phases;
        for (const QString &k : s.metricOrder) {
            const MetricSeries *m = s.metric(k);
            if (!m) continue;
            QVariantList ts, vs, vd, ps;
            for (qint64 t : m->tUs) ts << qlonglong(t);
            for (double v : m->value) vs << v;
            for (uint8_t b : m->valid) vd << int(b);
            for (const PhaseSample &p : m->phaseSamples)
                ps << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                                   { QStringLiteral("value"), p.value }, { QStringLiteral("band"), p.band } };
            QVariantMap sm{ { QStringLiteral("key"), m->key }, { QStringLiteral("label"), m->label },
                            { QStringLiteral("unit"), m->unit }, { QStringLiteral("t_us"), ts },
                            { QStringLiteral("value"), vs }, { QStringLiteral("phaseSamples"), ps } };
            if (!m->valid.empty()) sm.insert(QStringLiteral("valid"), vd);
            if (std::isfinite(m->sigma)) sm.insert(QStringLiteral("sigma"), m->sigma);
            series << sm;
        }
        for (const PhaseMark &p : s.phases)
            phases << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                                   { QStringLiteral("conf"), p.conf } };
        return { { QStringLiteral("series"), series }, { QStringLiteral("phases"), phases },
                 { QStringLiteral("kinematicSequence"), s.kinematicSequence } };
    }

    // Feed `presenters` (any mix of ChartsTracesPresenter / ChartsRelationsPresenter /
    // ChartsWristPresenter) one snapshot of the fixture. A traces or wrist presenter is detached from its
    // SessionSeriesModel first (the model's snapshot would win over a direct one). Options, all optional:
    //   none: true           no session at all (an empty snapshot with no folder)
    //   shots: n             the first n shots only (n < 3: the session refusals)
    //   without: [ordinal]   shots left out of the session (a live shot not yet landed)
    //   clubs: { "3": "DRIVER" }                    relabel shots' clubs (mixed clubs)
    //   unmeasured: { key, phase, ordinals: [..] }  drop a reading's phase sample from shots
    //   wrist: true          give every shot its wrist checkpoint profile (ShotSeries::wrist, as
    //                        SessionSeriesModel fills it; the fixture's pose trail wrist), so the Wrist
    //                        tab is offered. Without it the payload stays empty: no shot carries a
    //                        wrist reading and the tab is not offered.
    //   posture: true        feed the Posture presenter the session (the fixture's six posture series: their
    //                        address, top and impact readings — P1, P4, P7 — so every other cell is "-").
    //                        Without it the Posture presenter is fed an empty snapshot: no shot carries a
    //                        posture reading and no Posture section shows, whatever the preset.
    //   picture: true        (with posture) every shot's picture strip reads data/posture/picture_doc.json
    //   delivery: true       give every shot the Club delivery readings (withDeliveryReadings: the swing plane curve,
    //                        the hand plane, the shoulder plane, the steepening and the monitor's path, attack, face and
    //                        face-to-path) and feed the Delivery presenter the session. Without it the Delivery
    //                        presenter is fed an empty snapshot: no Club delivery section shows, whatever the preset.
    // Returns the number of shots in the snapshot fed (−1 when the fixture did not load).
    Q_INVOKABLE int feed(const QVariantList &presenters, const QVariantMap &opts = {})
    {
        using namespace pinpoint::charts;
        if (!load()) return -1;
        auto s = std::make_shared<SessionSnapshot>();
        s->revision = ++m_revision;
        m_deliveryFed = opts.value(QStringLiteral("delivery")).toBool();
        if (!opts.value(QStringLiteral("none")).toBool()) {
            s->sessionDir = sessionDir();
            s->conditions = m_fx.session.conditions;
            const int n = opts.contains(QStringLiteral("shots")) ? opts.value(QStringLiteral("shots")).toInt()
                                                                : int(m_fx.session.shots.size());
            QList<int> without;
            for (const QVariant &v : opts.value(QStringLiteral("without")).toList()) without << v.toInt();
            const QVariantMap clubs = opts.value(QStringLiteral("clubs")).toMap();
            const QVariantMap un = opts.value(QStringLiteral("unmeasured")).toMap();
            const bool wrist = opts.value(QStringLiteral("wrist")).toBool();
            const bool delivery = opts.value(QStringLiteral("delivery")).toBool();
            QList<int> unOrd;
            for (const QVariant &v : un.value(QStringLiteral("ordinals")).toList()) unOrd << v.toInt();
            for (const auto &sh : m_fx.session.shots) {
                if (sh->ordinal > n || without.contains(sh->ordinal)) continue;
                auto c = std::make_shared<ShotSeries>(*sh);
                c->swingDir = dirOf(c->ordinal);
                c->shotId = c->ordinal;
                const QString club = clubs.value(QString::number(c->ordinal)).toString();
                if (!club.isEmpty()) { c->club = club; c->diagnostics.club = club; }
                if (unOrd.contains(c->ordinal)) {
                    const int ph = un.value(QStringLiteral("phase")).toInt();
                    auto &ps = c->metrics[un.value(QStringLiteral("key")).toString()].phaseSamples;
                    ps.erase(std::remove_if(ps.begin(), ps.end(), [ph](const PhaseSample &x) { return x.phase == ph; }), ps.end());
                }
                if (wrist) { withWristCurve(*c); c->wrist = shotWristPayload(*c, -1, {}); }
                else c->wrist = QVariantMap{};
                if (delivery) withDeliveryReadings(*c);
                s->shots.push_back(c);
            }
        }
        for (const QVariant &v : presenters) {
            QObject *o = v.value<QObject *>();
            if (auto *t = qobject_cast<ChartsTracesPresenter *>(o)) { t->setModel(nullptr); t->setSnapshot(s); }
            else if (auto *r = qobject_cast<ChartsRelationsPresenter *>(o)) r->setSnapshot(s);
            else if (auto *w = qobject_cast<ChartsWristPresenter *>(o)) { w->setModel(nullptr); w->setSnapshot(s); }
            else if (auto *d = qobject_cast<ChartsDeliveryPresenter *>(o)) {
                d->setModel(nullptr);
                if (opts.value(QStringLiteral("delivery")).toBool()) d->setSnapshot(s);
                else {
                    auto e = std::make_shared<SessionSnapshot>();
                    e->revision = s->revision;
                    e->sessionDir = s->sessionDir;
                    d->setSnapshot(e);
                }
            }
            else if (auto *p = qobject_cast<ChartsPosturePresenter *>(o)) {
                p->setModel(nullptr);
                if (opts.value(QStringLiteral("posture")).toBool()) {
                    p->setSnapshot(s);
                    // the picture strip's document for every shot (data/posture/picture_doc.json, a real re-analysed
                    // swing trimmed; no video, so the frames draw their lines on an empty frame)
                    if (opts.value(QStringLiteral("picture")).toBool())
                        for (const auto &sh : s->shots)
                            p->setPictureDocumentForTest(sh->swingDir, QStringLiteral(QUICK_TEST_SOURCE_DIR "/../data/posture/picture_doc.json"));
                }
                else {
                    auto e = std::make_shared<SessionSnapshot>();
                    e->revision = s->revision;
                    e->sessionDir = s->sessionDir;
                    p->setSnapshot(e);
                }
            }
        }
        return s->count();
    }

    // THE 4 JULY FIXTURE HAS NO SWING PLANE CURVE (it predates the plane, and its monitor rows were trimmed), so
    // `delivery: true` gives each shot the Club delivery readings of charts_delivery_presenter_test's synthetic shots,
    // laid on the shot's OWN P-positions: the curve flat ±25 ms about each position and linear between (P1…P7 =
    // 58 54 62 65 64 p6 59°, p6 = 58 + 0.5 (ordinal − 1)) on a 5 ms grid from P1 to 40 ms past impact; the hand plane
    // 4° under it except from 50 ms before the top to 25 ms after P6, where the hands carry it; the shoulder plane 68°
    // at address; and the monitor's path following the plane at P6 (−0.92 a degree, ±0.3), its attack, a face and a
    // face-to-path that put the shots in more than one cell of the shot-shape grid. 15 shots: the path fit holds.
    // A shot missing a P-position is left as it is.
    static void withDeliveryReadings(pinpoint::charts::ShotSeries &c)
    {
        using namespace pinpoint::charts;
        constexpr int kPh[7] = { 0, 12, 8, 2, 13, 9, 5 };
        qint64 at[7];
        for (int p = 0; p < 7; ++p) {
            at[p] = -1;
            for (const PhaseMark &m : c.phases) if (m.phase == kPh[p]) at[p] = m.tUs;
            if (at[p] < 0 || (p > 0 && at[p] <= at[p - 1])) return;
        }
        const int i = c.ordinal;
        const double p6 = 58.0 + 0.5 * (i - 1);
        const double val[7] = { 58, 54, 62, 65, 64, p6, 59 };
        MetricSeries curve, hands;
        curve.key = QStringLiteral("swingPlaneCurve"); curve.unit = QStringLiteral("°"); curve.sigma = 0.8;
        hands.key = QStringLiteral("handPlaneCurve");  hands.unit = QStringLiteral("°");
        const qint64 handsFrom = at[3] - 50'000, handsTo = at[5] + 25'000;
        for (qint64 t = at[0]; t <= at[6] + 40'000; t += 5'000) {
            double v = val[6];
            int p = 0;
            while (p < 6 && t > at[p + 1]) ++p;
            if (p < 6) {
                const double a = double(at[p] + 25'000), b = double(at[p + 1] - 25'000);
                v = (b <= a || double(t) <= a) ? (double(t) <= a ? val[p] : val[p + 1])
                  : double(t) >= b ? val[p + 1] : val[p] + (val[p + 1] - val[p]) * (double(t) - a) / (b - a);
            }
            curve.tUs.push_back(t); curve.value.push_back(v);
            hands.tUs.push_back(t); hands.value.push_back(t >= handsFrom && t <= handsTo ? v : v - 4.0);
        }
        const auto put = [&c](MetricSeries m) {
            if (!c.metricOrder.contains(m.key)) c.metricOrder << m.key;
            c.metrics.insert(m.key, std::move(m));
        };
        put(curve); put(hands);
        const auto instant = [&](const QString &key, int phase, double v) {
            MetricSeries m;
            m.key = key; m.unit = QStringLiteral("°");
            PhaseSample ps; ps.phase = phase; ps.value = v;
            for (const PhaseMark &pm : c.phases) if (pm.phase == phase) ps.tUs = pm.tUs;
            m.phaseSamples.push_back(ps);
            put(m);
        };
        instant(QStringLiteral("shoulderPlane"), 0, 68.0);
        instant(QStringLiteral("handPlaneSteepening"), 2, 5.0 + 0.3 * (i - 1));
        instant(QStringLiteral("lm.clubPath"), 5, 55.5 - 0.92 * p6 + (i % 2 ? 0.3 : -0.3));
        instant(QStringLiteral("lm.attackAngle"), 5, 28.0 - 0.5 * p6 + (i % 2 ? 0.2 : -0.2));
        instant(QStringLiteral("lm.faceAngle"), 5, -2.0 + 1.5 * (i % 3));
        instant(QStringLiteral("lm.faceToPath"), 5, 1.5 - 1.2 * (i % 4));
    }

    // ⚠ THE TRIMMED FIXTURE KEEPS NO WRIST CURVE: trim_fixture.py kept curves for the six speeds only, so
    // the pose trail wrist (trailWristFlexExt) carries just its phase readings (address, top, impact,
    // finish) and shotWristPayload — which builds from the series, as SessionSeriesModel does — returns
    // an empty payload. `wrist: true` therefore draws the shot's curve THROUGH those readings: piecewise
    // linear, flat beyond the first and last, at 4 ms over the shot's phase span. Every checkpoint the
    // engine samples reads a value made from this swing's own readings, never an invented one.
    static void withWristCurve(pinpoint::charts::ShotSeries &c)
    {
        auto it = c.metrics.find(QStringLiteral("trailWristFlexExt"));
        if (it == c.metrics.end() || !it->tUs.empty()) return;
        std::vector<std::pair<qint64, double>> at;
        for (const auto &p : it->phaseSamples) if (std::isfinite(p.value)) at.emplace_back(p.tUs, p.value);
        if (at.size() < 2) return;
        std::sort(at.begin(), at.end());
        qint64 t0 = at.front().first, t1 = at.back().first;
        for (const auto &p : c.phases) { t0 = std::min(t0, p.tUs); t1 = std::max(t1, p.tUs); }
        size_t j = 0;
        for (qint64 t = t0; t <= t1; t += 4000) {
            while (j + 1 < at.size() && at[j + 1].first <= t) ++j;
            double v = at[j].second;
            if (t <= at.front().first) v = at.front().second;
            else if (j + 1 < at.size()) {
                const double f = double(t - at[j].first) / double(at[j + 1].first - at[j].first);
                v = at[j].second + f * (at[j + 1].second - at[j].second);
            }
            it->tUs.push_back(t);
            it->value.push_back(v);
        }
    }

    // Put a test's fake controller on this file's engine root context, under the app's name.
    Q_INVOKABLE void provide(const QString &name, QObject *object)
    {
        if (m_engine) m_engine->rootContext()->setContextProperty(name, object);
    }

private:
    bool load()
    {
        if (!m_tried) {
            m_tried = true;
            m_fx = charts_test::loadFixture(QStringLiteral(QUICK_TEST_SOURCE_DIR "/../data/charts/session_0704.json"));
            if (!m_fx.ok) qWarning("chartsFixture: the 4 July fixture did not load");
        }
        return m_fx.ok;
    }

    QQmlEngine *m_engine = nullptr;
    charts_test::Fixture m_fx;
    bool m_tried = false;
    quint64 m_revision = 100;
    bool m_deliveryFed = false;          // the last feed gave the shots the Club delivery readings (detailOf follows)
};

class QmlUiTestSetup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        // Before anything renders — a face registered after a component has laid out does not
        // re-measure it.
        loadBundledFonts();

        // Redirected BEFORE the first AppSettings is constructed — its constructor reads every key
        // through ppSettings(), and QSettings::setPath only affects instances made after it. A test
        // that quietly rewrote ui/themeIndex in the developer's own ini would be a poor trade for
        // the convenience of using the real class.
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_scratch.path());

        m_settings = new AppSettings(this);
        engine->rootContext()->setContextProperty(QStringLiteral("appSettings"), m_settings);

        // The Charts fixture feeder — a new name, so no other suite's lookups change.
        engine->rootContext()->setContextProperty(QStringLiteral("chartsFixture"), new ChartsFixture(engine, engine));

        // NO addImportPath, AND NO qmlRegisterType. Both were needed while this binary borrowed a
        // staged copy of the app's module: the copy carried .qml and a qmldir but none of the C++,
        // so ChartMetrics and TimelineLabels had to be registered here by hand and everything else
        // in the module stayed "unavailable" — which is why the chart family had UI coverage and
        // the rest of the app's types had none.
        //
        // The module is now built into this target from the same PP_QML_SOURCES the app uses, so
        // every QML_ELEMENT type registers itself exactly as it does in the app, and the import
        // resolves out of this binary's own resources. A component needing another type needs
        // nothing added here.
    }

private:
    // Cleaned up with the process. Nothing here is meant to survive a run: the point is that each
    // one starts from the defaults compiled into AppSettings.
    QTemporaryDir m_scratch;
    AppSettings  *m_settings = nullptr;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_ui, QmlUiTestSetup)

#include "qml_ui_test.moc"
