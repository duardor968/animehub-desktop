#include "platform/credentials.h"
#include "platform/process_job.h"
#include "platform/trash.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <string_view>
#include <unistd.h>

using namespace animehub;

namespace {

// Started by the test as AnimeHub would start aria2, then gone without cleaning up, as a crash.
int startHelperAndDie() {
    QProcess helper;
    platform::prepareHelperProcess(helper);
    helper.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    if (!helper.waitForStarted(5000)) {
        return 1;
    }
    std::printf("%lld\n", static_cast<long long>(helper.processId()));
    std::fflush(stdout);
    _exit(0);
}

// Gone, or a zombie nobody reaped yet (a container's init may not).
bool ended(qint64 pid) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) {
        return true;
    }
    const auto fields = stat.readAll();
    const auto state = fields.mid(fields.lastIndexOf(')') + 2, 1);
    return state == "Z" || state == "X";
}

} // namespace

class PlatformLinuxTest final : public QObject {
    Q_OBJECT

private slots:
    void restoresAFileWithoutLeavingItInTheTrash() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        // A trash of the test's own, on the same file system as the file. Qt creates the Trash
        // folder, not the data folder that holds it.
        qputenv("XDG_DATA_HOME", root.filePath(QStringLiteral("data")).toUtf8());
        QDir().mkpath(root.filePath(QStringLiteral("data")));
        QDir().mkpath(root.filePath(QStringLiteral("library")));
        const auto path = root.filePath(QStringLiteral("library/Episodio 1.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("video");
        file.close();

        const auto trashed = platform::moveToTrash(path);
        qunsetenv("XDG_DATA_HOME");
        QVERIFY(trashed.has_value());
        QVERIFY(!QFile::exists(path));
        QVERIFY(QFile::exists(*trashed));
        const QFileInfo data(*trashed);
        const auto info = QDir(data.absolutePath() + QStringLiteral("/../info"))
                              .filePath(data.fileName() + QStringLiteral(".trashinfo"));
        QVERIFY2(QFile::exists(info), qPrintable(info));

        QVERIFY(platform::restoreFromTrash(*trashed, path));
        QVERIFY(QFile::exists(path));
        QVERIFY(!QFile::exists(*trashed));
        QVERIFY(!QFile::exists(info));
    }

    void endsHelpersWithAnimeHub() {
        QProcess parent;
        parent.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--start-helper")});
        QVERIFY(parent.waitForFinished(10000));
        QCOMPARE(parent.exitCode(), 0);
        bool ok = false;
        const auto pid = parent.readAllStandardOutput().trimmed().toLongLong(&ok);
        QVERIFY(ok && pid > 0);
        QDeadlineTimer deadline(5000);
        while (!ended(pid) && !deadline.hasExpired()) {
            QTest::qWait(50);
        }
        QVERIFY2(ended(pid), "the helper outlived the process that started it");
    }

    // Only against a throwaway Secret Service (dbus-run-session with gnome-keyring), never the
    // keyring of whoever runs the tests.
    void keepsOneCredentialPerTarget() {
        if (qEnvironmentVariable("ANIMEHUB_TEST_KEYRING") != u"private") {
            QSKIP("needs ANIMEHUB_TEST_KEYRING=private and a throwaway keyring");
        }
        const auto target = QStringLiteral("AnimeHubTests/credential");
        platform::removeCredential(target);
        QVERIFY(!platform::readCredential(target));

        const QByteArray binary("\x00\x01key\xff", 6);
        QVERIFY(platform::storeCredential(target, {QStringLiteral("ana@example.com"), binary}));
        auto read = platform::readCredential(target);
        QVERIFY(read.has_value());
        QCOMPARE(read->user, QStringLiteral("ana@example.com"));
        QCOMPARE(read->secret, binary);

        // Another user replaces the first, as a Windows credential would.
        QVERIFY(platform::storeCredential(target, {QStringLiteral("eva@example.com"), "two"}));
        read = platform::readCredential(target);
        QVERIFY(read.has_value());
        QCOMPARE(read->user, QStringLiteral("eva@example.com"));
        QCOMPARE(read->secret, QByteArray("two"));

        platform::removeCredential(target);
        QVERIFY(!platform::readCredential(target));
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc > 1 && std::string_view(argv[1]) == "--start-helper") {
        return startHelperAndDie();
    }
    PlatformLinuxTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "platform_linux_test.moc"
