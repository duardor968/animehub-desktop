#include "platform/autostart.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;

namespace {

#ifdef Q_OS_WIN
// A key of the tests' own: the user's real Run entries stay untouched.
const auto kTestKey = QStringLiteral(R"(HKEY_CURRENT_USER\Software\AnimeHubTests\Run)");
#endif

} // namespace

class AutostartTest final : public QObject {
    Q_OBJECT

private slots:
#ifdef Q_OS_WIN
    void cleanup() {
        QSettings(QStringLiteral(R"(HKEY_CURRENT_USER\Software)"), QSettings::NativeFormat)
            .remove(QStringLiteral("AnimeHubTests"));
    }

    void writesReadsAndRemovesTheEntry() {
        QVERIFY(platform::autostartCommand(kTestKey).isEmpty());

        const auto command = QStringLiteral(R"("C:\Programs\AnimeHub\AnimeHub.exe" --tray)");
        QVERIFY(platform::setAutostartCommand(command, kTestKey));
        QCOMPARE(platform::autostartCommand(kTestKey), command);

        // The entry is AnimeHub's, next to whatever else starts with Windows.
        QSettings run(kTestKey, QSettings::NativeFormat);
        QCOMPARE(run.value(QStringLiteral("AnimeHub")).toString(), command);

        QVERIFY(platform::setAutostartCommand(QString(), kTestKey));
        QVERIFY(platform::autostartCommand(kTestKey).isEmpty());
    }
#else
    void writesReadsAndRemovesTheEntry() {
        // An entry of the test's own: the user's autostart folder stays untouched.
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const auto entry = config.filePath(QStringLiteral("autostart/animehub-test.desktop"));
        QVERIFY(platform::autostartCommand(entry).isEmpty());

        const auto command =
            QStringLiteral(R"("/home/u/Apps/AnimeHub 100%.AppImage" --tray --data-dir "/d\x")");
        QVERIFY(platform::setAutostartCommand(command, entry));
        QCOMPARE(platform::autostartCommand(entry), command);

        // A desktop entry the session runs, with the string and field-code escapes it needs.
        QFile file(entry);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto text = file.readAll();
        QVERIFY(text.startsWith("[Desktop Entry]\n"));
        QVERIFY(text.contains("\nType=Application\n"));
        QVERIFY(text.contains(
            QByteArray("\nExec=") +
            R"("/home/u/Apps/AnimeHub 100%%.AppImage" --tray --data-dir "/d\\x")" + "\n"));
        file.close();

        // Turned off from the desktop's settings, it no longer counts as AnimeHub's.
        QVERIFY(file.open(QIODevice::Append));
        file.write("Hidden=true\n");
        file.close();
        QVERIFY(platform::autostartCommand(entry).isEmpty());

        QVERIFY(platform::setAutostartCommand(QString(), entry));
        QVERIFY(!QFile::exists(entry));
        QVERIFY(platform::autostartCommand(entry).isEmpty());
        QVERIFY(platform::setAutostartCommand(QString(), entry));
    }

    void startsTheAppImageRatherThanItsMount() {
        const auto executable = QCoreApplication::applicationFilePath();
        QCOMPARE(platform::launcherPath(), executable);
        // Variables another AppImage left in the environment do not apply.
        qputenv("APPIMAGE", "/home/u/Other.AppImage");
        qputenv("APPDIR", "/tmp/.mount_Other");
        QCOMPARE(platform::launcherPath(), executable);
        qputenv("APPDIR", QFileInfo(executable).absolutePath().toUtf8());
        QCOMPARE(platform::launcherPath(), QStringLiteral("/home/u/Other.AppImage"));
        qunsetenv("APPIMAGE");
        qunsetenv("APPDIR");
    }
#endif
};

QTEST_GUILESS_MAIN(AutostartTest)
#include "autostart_test.moc"
