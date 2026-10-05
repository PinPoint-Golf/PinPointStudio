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

// Driver for the session-setup UI suite (H1 in session_wizard_refactor_design.md §7.1) —
// the tst_*.qml files beside this one.
//
// It is a SEPARATE binary from qml_ui_test for two reasons, both about what must not leak
// into the other suites:
//   - the wizard resolves cameraManager, imuManager, athleteController, liveWrist and
//     sessionController as unqualified context properties, so the fakes have to be
//     injected under exactly those names — in a CHILD context per instance, through
//     `harness.createWithContext()`, so two hosts can be given different fakes;
//   - the app-only enum carriers CameraInstance and SessionController are provided by
//     test-only stand-ins (setup_enum_standins.h).
//
// What the setup object supplies, beyond what qml_ui_test does:
//   testLog   — every QtWarningMsg / QtCriticalMsg, so each test can assert in cleanup()
//               that loading and driving the wizard produced no warning it did not expect.
//               A binding failing at runtime has no build error; this is what catches it.
//   harness   — createWithContext() / destroyNow().

#include <QtQuickTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QRectF>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <QFontDatabase>
#include <QMutex>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>

#include "app_settings.h"

// Same as qml_ui_test: the bundled faces, so text measures the way the app draws it.
static void loadBundledFonts()
{
    const QDir dir(QStringLiteral(":/fonts"));
    const QStringList faces = dir.entryList({ QStringLiteral("*.ttf") }, QDir::Files, QDir::Name);
    if (faces.isEmpty())
        qFatal("session_setup_ui_test: no fonts under :/fonts — check the test_fonts resource.");
    for (const QString &face : faces) {
        if (QFontDatabase::addApplicationFont(dir.filePath(face)) < 0)
            qFatal("session_setup_ui_test: failed to load bundled font %s", qPrintable(face));
    }
}

// ── testLog ─────────────────────────────────────────────────────────────────────
//
// ⚠ INSTALLED LAZILY, NOT IN THE CONSTRUCTOR. QuickTest starts QTestLog when the first
// TestCase runs (testcase.qml → qtest_results.startLogging()), and QTestLog installs its
// OWN message handler at that moment, which does not forward to whatever was there before.
// A handler installed earlier would simply stop receiving messages. So install() runs on
// first use (every invokable here and harness.createWithContext call it), by which time
// QTestLog's handler is in place — and this one CHAINS to it, so QTest's own
// ignoreWarning()/failOnWarning() and the console output keep working.
class SetupTestLog : public QObject
{
    Q_OBJECT
public:
    explicit SetupTestLog(QObject *parent = nullptr) : QObject(parent) { s_self = this; }
    ~SetupTestLog() override
    {
        if (s_self != this) return;
        if (s_installed) {
            qInstallMessageHandler(s_previous);
            s_installed = false;
        }
        s_self = nullptr;
    }

    // Idempotent. Re-installs if something replaced the handler since (QTestLog restores
    // its predecessor at stopLogging, and a later startLogging would shadow this one).
    Q_INVOKABLE void install()
    {
        QtMessageHandler current = qInstallMessageHandler(&SetupTestLog::handler);
        if (current != &SetupTestLog::handler)
            s_previous = current;
        s_installed = true;
    }

    // Every unexpected warning/critical since the last take, oldest first, then cleared.
    Q_INVOKABLE QStringList takeWarnings()
    {
        install();
        QMutexLocker lock(&m_mutex);
        QStringList out = m_records;
        m_records.clear();
        return out;
    }

    // A warning matching `regex` is CONSUMED (counted, not recorded). Expectations persist
    // until reset(); expectedCount(regex) says how many times one matched.
    Q_INVOKABLE void expect(const QString &regex)
    {
        install();
        QMutexLocker lock(&m_mutex);
        m_expected.append({ QRegularExpression(regex), regex, 0 });
    }

    Q_INVOKABLE int expectedCount(const QString &regex) const
    {
        QMutexLocker lock(&m_mutex);
        for (const Expectation &e : m_expected)
            if (e.source == regex) return e.hits;
        return -1;
    }

    // Drops records AND expectations. Call from init().
    Q_INVOKABLE void reset()
    {
        install();
        QMutexLocker lock(&m_mutex);
        m_records.clear();
        m_expected.clear();
    }

    // Every message seen since the last reset, expected or not (diagnostics only).
    Q_INVOKABLE QStringList allSeen() const
    {
        QMutexLocker lock(&m_mutex);
        return m_seen;
    }

    // Monotonic milliseconds since process start — wall-clock timings for the spikes.
    Q_INVOKABLE double elapsedMs() const { return double(s_clock.nsecsElapsed()) / 1.0e6; }

private:
    struct Expectation { QRegularExpression re; QString source; int hits; };

    static void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {
        if (s_self && (type == QtWarningMsg || type == QtCriticalMsg))
            s_self->record(type, ctx, msg);
        if (s_previous)
            s_previous(type, ctx, msg);
    }

    void record(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {
        const QString line = QStringLiteral("%1[%2] %3")
                                 .arg(type == QtCriticalMsg ? QStringLiteral("CRITICAL ") : QString(),
                                      QString::fromLatin1(ctx.category ? ctx.category : "default"),
                                      msg);
        QMutexLocker lock(&m_mutex);
        m_seen.append(line);
        for (Expectation &e : m_expected) {
            if (e.re.match(msg).hasMatch()) {
                ++e.hits;
                return;
            }
        }
        m_records.append(line);
    }

    mutable QMutex       m_mutex;
    QStringList          m_records;
    QStringList          m_seen;
    QList<Expectation>   m_expected;

    static inline SetupTestLog    *s_self      = nullptr;
    static inline QtMessageHandler s_previous  = nullptr;
    static inline bool             s_installed = false;
    static inline QElapsedTimer    s_clock     = [] { QElapsedTimer t; t.start(); return t; }();
};

// ── harness ─────────────────────────────────────────────────────────────────────
class SetupHarness : public QObject
{
    Q_OBJECT
public:
    SetupHarness(QQmlEngine *engine, SetupTestLog *log, QObject *parent = nullptr)
        : QObject(parent), m_engine(engine), m_log(log) {}

    // Instantiates `type` — a PinPointStudio module type name ("ScreenSessionSetup"), or a
    // URL/path to a .qml file — in a CHILD context of the engine's root context in which
    // every entry of `contextProps` is set as a context property. The production QML's
    // unqualified names (imuManager, cameraManager, …) resolve through that context, which
    // is the whole point: the component is the production file, unmodified.
    //
    // The item is parented (visually AND as a QObject) to `parent` BEFORE completion, so
    // its Component.onCompleted and first layout see the real size and visibility.
    // `initialProps` are applied before completion too. Returns null on failure, with the
    // component's errors emitted as warnings (so testLog records them).
    Q_INVOKABLE QObject *createWithContext(const QString &type, const QVariantMap &contextProps,
                                           QQuickItem *parent, const QVariantMap &initialProps)
    {
        if (m_log) m_log->install();

        auto *ctx = new QQmlContext(m_engine->rootContext());
        for (auto it = contextProps.cbegin(); it != contextProps.cend(); ++it)
            ctx->setContextProperty(it.key(), it.value());

        QQmlComponent component(m_engine);
        if (type.endsWith(QLatin1String(".qml")) || type.contains(QLatin1Char('/')))
            component.loadUrl(QUrl::fromUserInput(type, QDir::currentPath()));
        else
            component.loadFromModule("PinPointStudio", type);

        if (component.isError()) {
            for (const QQmlError &e : component.errors())
                qWarning().noquote() << "harness.createWithContext:" << e.toString();
            delete ctx;
            return nullptr;
        }

        QObject *obj = component.beginCreate(ctx);
        if (!obj) {
            for (const QQmlError &e : component.errors())
                qWarning().noquote() << "harness.createWithContext:" << e.toString();
            delete ctx;
            return nullptr;
        }
        if (!initialProps.isEmpty())
            component.setInitialProperties(obj, initialProps);
        if (auto *item = qobject_cast<QQuickItem *>(obj); item && parent) {
            item->setParentItem(parent);
            item->setParent(parent);
        }
        QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
        component.completeCreate();
        // The context lives exactly as long as the object.
        ctx->setParent(obj);
        muteSounds(obj);
        return obj;
    }

    // ⚠ The flow plays a real ting through the default audio output on every completed
    // calibration, and a suite that completes dozens of them is unbearable to sit beside.
    // Volume 0 on every TingPlayer under `root`; play() still runs, so nothing else changes.
    // Called on everything createWithContext() makes; call it again after a Loader swaps content.
    Q_INVOKABLE int muteSounds(QObject *root)
    {
        if (!root) return 0;
        int muted = 0;
        const QList<QObject *> all = root->findChildren<QObject *>();
        for (QObject *o : all) {
            if (QByteArray(o->metaObject()->className()) == "TingPlayer") {
                o->setProperty("volume", 0.0);
                ++muted;
            }
        }
        return muted;
    }

    // Synchronous destruction — a View3D-bearing wizard left to deleteLater would still be
    // tearing down while the next test builds its own.
    Q_INVOKABLE void destroyNow(QObject *obj) { delete obj; }

    // ── Render checks (tst_setup_guide_render.qml, design §7.4 RS1/RS2) ─────────────────────
    // The pixel maths for a grab (ItemGrabResult.image), done here rather than through a QML
    // Canvas readback, which never called back for a grab url in the real app.
    //
    // { width, height, mean, std } of the Rec. 601 luminance, 0–255.
    Q_INVOKABLE QVariantMap imageStats(const QVariant &image) const
    {
        const QImage img = qvariant_cast<QImage>(image).convertToFormat(QImage::Format_RGB32);
        QVariantMap out;
        out.insert(QStringLiteral("width"), img.width());
        out.insert(QStringLiteral("height"), img.height());
        if (img.isNull() || img.width() == 0 || img.height() == 0) return out;
        double sum = 0, sum2 = 0;
        const qint64 n = qint64(img.width()) * img.height();
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const double l = 0.299 * qRed(row[x]) + 0.587 * qGreen(row[x]) + 0.114 * qBlue(row[x]);
                sum += l; sum2 += l * l;
            }
        }
        const double mean = sum / double(n);
        out.insert(QStringLiteral("mean"), mean);
        out.insert(QStringLiteral("std"), std::sqrt(std::max(0.0, sum2 / double(n) - mean * mean)));
        return out;
    }
    // Mean absolute luminance difference of two same-size grabs inside `region`, given in
    // normalised coordinates (0–1 of the width and height). -1 when the images differ in size.
    Q_INVOKABLE double imageDiff(const QVariant &a, const QVariant &b, const QRectF &region) const
    {
        const QImage ia = qvariant_cast<QImage>(a).convertToFormat(QImage::Format_RGB32);
        const QImage ib = qvariant_cast<QImage>(b).convertToFormat(QImage::Format_RGB32);
        if (ia.isNull() || ia.size() != ib.size()) return -1.0;
        const int x0 = qBound(0, int(region.left() * ia.width()), ia.width());
        const int x1 = qBound(0, int(region.right() * ia.width()), ia.width());
        const int y0 = qBound(0, int(region.top() * ia.height()), ia.height());
        const int y1 = qBound(0, int(region.bottom() * ia.height()), ia.height());
        if (x1 <= x0 || y1 <= y0) return -1.0;
        auto luma = [](QRgb c) { return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c); };
        double sum = 0;
        for (int y = y0; y < y1; ++y) {
            const QRgb *ra = reinterpret_cast<const QRgb *>(ia.constScanLine(y));
            const QRgb *rb = reinterpret_cast<const QRgb *>(ib.constScanLine(y));
            for (int x = x0; x < x1; ++x) sum += std::abs(luma(ra[x]) - luma(rb[x]));
        }
        return sum / double(qint64(x1 - x0) * (y1 - y0));
    }
    // A grab of `item` as the window shows it: QQuickWindow::grabWindow() (which renders a frame
    // and reads it back — synchronous), cropped to the item's scene rectangle. Stage 5e: an
    // ItemGrabResult (QQuickItem::grabToImage) never called back for a View3D in this binary
    // either — the same symptom the real app's soak probe hit — so the checks read the window.
    Q_INVOKABLE QVariant grabItem(QQuickItem *item) const
    {
        if (!item || !item->window()) return QVariant();
        const QImage win = item->window()->grabWindow();
        if (win.isNull()) return QVariant();
        const qreal dpr = win.width() / qMax<qreal>(1.0, item->window()->width());
        const QRectF r  = item->mapRectToScene(item->boundingRect());
        const QRect  px(qRound(r.x() * dpr), qRound(r.y() * dpr), qRound(r.width() * dpr), qRound(r.height() * dpr));
        return QVariant::fromValue(win.copy(px.intersected(win.rect())));
    }
    // Where the render checks save their grabs: $SETUP_GUIDE_GRABS, or "" (save nothing).
    Q_INVOKABLE QString grabDir() const { return qEnvironmentVariable("SETUP_GUIDE_GRABS"); }
    Q_INVOKABLE bool saveImage(const QVariant &image, const QString &path) const
    {
        const QImage img = qvariant_cast<QImage>(image);
        return !img.isNull() && QDir().mkpath(QFileInfo(path).absolutePath()) && img.save(path);
    }
    // The scene node that carries the RuntimeLoader whose source ends with `glbSuffix` (each body
    // segment is a Node with its .glb loader as a child): { found, rotation, sceneRotation } as
    // quaternions. How a render check names "the lead upper arm" and "the lead forearm" from the
    // scene rather than from pixels.
    Q_INVOKABLE QVariantMap segmentNode(QObject *root, const QString &glbSuffix) const
    {
        QVariantMap out;
        out.insert(QStringLiteral("found"), false);
        if (!root) return out;
        const QList<QObject *> all = root->findChildren<QObject *>();
        for (QObject *o : all) {
            const QVariant src = o->property("source");
            if (!src.isValid() || !src.toUrl().toString().endsWith(glbSuffix)) continue;
            QObject *node = o->parent();
            if (!node || !node->property("sceneRotation").isValid()) continue;
            out.insert(QStringLiteral("found"), true);
            out.insert(QStringLiteral("rotation"), node->property("rotation"));
            out.insert(QStringLiteral("sceneRotation"), node->property("sceneRotation"));
            return out;
        }
        return out;
    }

private:
    QQmlEngine   *m_engine = nullptr;
    SetupTestLog *m_log    = nullptr;
};

class SessionSetupUiTestSetup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        loadBundledFonts();

        // Scratch QSettings BEFORE the first AppSettings — see qml_ui_test.cpp. The fakes write
        // appSettings.imuRoles, so this matters more here than it does there.
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_scratch.path());

        // One engine per tst_*.qml file; each gets its own objects.
        auto *settings = new AppSettings(engine);
        auto *log      = new SetupTestLog(engine);
        auto *harness  = new SetupHarness(engine, log, engine);

        engine->rootContext()->setContextProperty(QStringLiteral("appSettings"), settings);
        engine->rootContext()->setContextProperty(QStringLiteral("testLog"), log);
        engine->rootContext()->setContextProperty(QStringLiteral("harness"), harness);
    }

private:
    QTemporaryDir m_scratch;
};

QUICK_TEST_MAIN_WITH_SETUP(session_setup_ui, SessionSetupUiTestSetup)

#include "session_setup_ui_test.moc"
