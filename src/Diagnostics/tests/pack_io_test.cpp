// Standalone tests for the yes/no form of pinpoint::atomicWrite() (src/Core/atomic_write.h), which
// pack_io.h used to wrap from a rules directory and which moved beside the helper it wrapped in refactor
// step 3.2 (its writers write through RuleData's repository now). The mechanism's own tests are
// src/Core/tests/atomic_write_test.cpp. What is checked here is what the yes/no form adds: an empty path
// is reported as "no writable application data location" (what an empty diagnosticsFilePath() means),
// and a write reaches the helper — a missing directory made, the bytes written whole, a failure reported
// in words.
//
//   cmake --build build/tests --target pack_io_test -j 6
//   ctest --test-dir build/tests -R '^pack_io_test$' --output-on-failure

#include "../model/pack_io.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  [FAIL] could not make a temporary directory\n");
        return 1;
    }
    const QByteArray bytes = QByteArrayLiteral("{\"version\":2}");

    std::printf("The forwarder reaches the helper\n");
    {
        const QString path = tmp.filePath(QStringLiteral("made/on/demand/user.json"));
        QString why = QStringLiteral("untouched");
        check(pinpoint::atomicWrite(path, bytes, &why), "a write into a missing directory returns true");
        check(readAll(path) == bytes, "the file holds the bytes");
        check(why == QStringLiteral("untouched"), "whyNot is left alone on success");
    }

    std::printf("A failure is reported in words\n");
    {
        // A path whose parent is a FILE: no directory can be made, so the helper fails and says why.
        const QString blocker = tmp.filePath(QStringLiteral("blocker"));
        QFile f(blocker);
        check(f.open(QIODevice::WriteOnly) && f.write("x") == 1, "seeded a file where a folder would go");
        f.close();
        const QString path = blocker + QStringLiteral("/user.json");
        QString why;
        check(!pinpoint::atomicWrite(path, bytes, &why), "the write returns false");
        check(why.contains(path), "whyNot is the helper's sentence, naming the file");
        check(!pinpoint::atomicWrite(path, bytes, nullptr), "a null whyNot is allowed");
    }

    std::printf("An empty path\n");
    {
        QString why;
        check(!pinpoint::atomicWrite(QString(), bytes, &why), "an empty path returns false");
        check(why == QStringLiteral("No writable application data location."),
              "whyNot says there is no writable application data location");
        check(!pinpoint::atomicWrite(QString(), bytes, nullptr), "a null whyNot is allowed");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
