#include "platform/autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <array>

#include <windows.h>
#endif

using namespace animehub;

namespace {

#ifdef Q_OS_WIN
// Keys of the tests' own: the user's real Run and StartupApproved entries stay untouched.
const auto kTestKey = QStringLiteral(R"(HKEY_CURRENT_USER\Software\AnimeHubTests\Run)");
constexpr const wchar_t* kTestApprovedSubkey =
    LR"(Software\AnimeHubTests\Explorer\StartupApproved\Run)";
constexpr const wchar_t* kEntryWide = L"AnimeHub";

bool writeStartupApprovedByte(BYTE first_byte) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kTestApprovedSubkey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    std::array<BYTE, 12> data{};
    data[0] = first_byte;
    const auto status = RegSetValueExW(key, kEntryWide, 0, REG_BINARY, data.data(),
                                       static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool hasStartupApprovedEntry() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kTestApprovedSubkey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const auto status = RegQueryValueExW(key, kEntryWide, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
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

    void honorsStartupApprovedDisableAndClearsOverrideOnEnable() {
        const auto command =
            QStringLiteral(R"("C:\Users\u\AppData\Local\Programs\AnimeHub\AnimeHub.exe" --tray)");
        QVERIFY(platform::setAutostartCommand(command, kTestKey));
        QCOMPARE(platform::autostartCommand(kTestKey), command);

        // Even first byte (0x02) means enabled in Task Manager.
        QVERIFY(writeStartupApprovedByte(0x02U));
        QCOMPARE(platform::autostartCommand(kTestKey), command);

        // Odd first byte (0x03 or 0x07) means disabled in Task Manager: must report disabled.
        QVERIFY(writeStartupApprovedByte(0x03U));
        QVERIFY(platform::autostartCommand(kTestKey).isEmpty());

        QVERIFY(writeStartupApprovedByte(0x07U));
        QVERIFY(platform::autostartCommand(kTestKey).isEmpty());

        // Re-enabling from AnimeHub Settings clears the disabled override so Windows runs it again.
        QVERIFY(platform::setAutostartCommand(command, kTestKey));
        QVERIFY(!hasStartupApprovedEntry());
        QCOMPARE(platform::autostartCommand(kTestKey), command);
    }

    void refusesDevAndTempPathsWithoutOverwritingRealEntry() {
        const auto real_command =
            QStringLiteral(R"("C:\Users\u\AppData\Local\Programs\AnimeHub\AnimeHub.exe" --tray)");
        QVERIFY(platform::setAutostartCommand(real_command, kTestKey));

        const auto dev_command = QStringLiteral(
            R"("D:\Proyectos\AnimeHub\animehub-desktop\out\build\windows-debug\AnimeHub.exe" --tray)");
        QVERIFY(!platform::setAutostartCommand(dev_command, kTestKey));
        QCOMPARE(platform::autostartCommand(kTestKey), real_command);

        const auto temp_exe = QDir::toNativeSeparators(
            QDir(QDir::tempPath()).filePath(QStringLiteral("AnimeHub.exe")));
        const auto temp_command = QLatin1Char('"') + temp_exe + QStringLiteral("\" --tray");
        QVERIFY(!platform::setAutostartCommand(temp_command, kTestKey));
        QCOMPARE(platform::autostartCommand(kTestKey), real_command);

        // Even when no entry exists yet, dev/temp paths must not register in Windows Startup.
        QVERIFY(platform::setAutostartCommand(QString(), kTestKey));
        QVERIFY(!platform::setAutostartCommand(dev_command, kTestKey));
        QVERIFY(!platform::setAutostartCommand(temp_command, kTestKey));
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
