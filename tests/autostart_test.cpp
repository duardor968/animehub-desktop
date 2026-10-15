#include "platform/autostart.h"

#include <QSettings>
#include <QTest>

using namespace animehub;

namespace {

// A key of the tests' own: the user's real Run entries stay untouched.
const auto kTestKey = QStringLiteral(R"(HKEY_CURRENT_USER\Software\AnimeHubTests\Run)");

} // namespace

class AutostartTest final : public QObject {
    Q_OBJECT

private slots:
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
};

QTEST_GUILESS_MAIN(AutostartTest)
#include "autostart_test.moc"
