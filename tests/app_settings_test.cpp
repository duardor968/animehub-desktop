#include "app/app_settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using app::AppSettings;
using downloads::DownloadEngine;

class AppSettingsTest final : public QObject {
    Q_OBJECT

private slots:
    void nativeSpeedLimitSurvivesDisablingAndRestart() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("settings.ini"));
        AppSettings settings(path, root.path());
        QSignalSpy changed_spy(&settings, &AppSettings::changed);
        QSignalSpy limit_spy(&settings, &AppSettings::speedLimitChanged);

        QVERIFY(!settings.nativeLimitEnabled());
        QCOMPARE(settings.nativeLimitKBps(), 2000);
        QCOMPARE(settings.speedLimitText(), QStringLiteral("2 MB/s"));

        settings.setNativeLimitKBps(750);
        settings.setNativeLimitEnabled(true);
        QCOMPARE(limit_spy.size(), 2);
        QCOMPARE(changed_spy.size(), 0);

        const AppSettings enabled(path, root.path());
        QCOMPARE(enabled.nativeLimitKBps(), 750);
        QVERIFY(enabled.nativeLimitEnabled());

        settings.setNativeLimitEnabled(false);
        QCOMPARE(limit_spy.size(), 3);
        QCOMPARE(changed_spy.size(), 0);
        const AppSettings disabled(path, root.path());
        QCOMPARE(disabled.nativeLimitKBps(), 750);
        QVERIFY(!disabled.nativeLimitEnabled());

        // Redundant write emits no signal.
        settings.setNativeLimitEnabled(false);
        QCOMPARE(limit_spy.size(), 3);

        // Formatting and clamping.
        settings.setNativeLimitKBps(500);
        QCOMPARE(settings.speedLimitText(), QStringLiteral("500 KB/s"));
        settings.setNativeLimitKBps(2000);
        QCOMPARE(settings.speedLimitText(), QStringLiteral("2 MB/s"));
        settings.setNativeLimitKBps(2500);
        QCOMPARE(settings.speedLimitText(), QStringLiteral("2,5 MB/s"));
        settings.setNativeLimitKBps(0);
        QCOMPARE(settings.nativeLimitKBps(), 1);
        QCOMPARE(settings.speedLimitText(), QStringLiteral("1 KB/s"));
        settings.setNativeLimitKBps(2'000'000);
        QCOMPARE(settings.nativeLimitKBps(), 1'000'000);
        QCOMPARE(settings.speedLimitText(), QStringLiteral("1000 MB/s"));
        QCOMPARE(changed_spy.size(), 0);
    }

    // settings.ini as the version with "Enviar a JDownloader" left it: what it chose for the
    // servers AnimeHub does not download now downloads them, followed, the same way.
    void earlierSendingBecomesTheJDownloaderForTheOtherServers_data() {
        QTest::addColumn<QByteArray>("saved");
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("engine");
        QTest::newRow("click-n-load")
            << QByteArray("mode=local\n") << int(AppSettings::JDownloader::Local)
            << int(DownloadEngine::JDownloaderLocal);
        QTest::newRow("myjdownloader")
            << QByteArray("mode=remote\n") << int(AppSettings::JDownloader::Remote)
            << int(DownloadEngine::JDownloaderCloud);
        QTest::newRow("not-sending")
            << QByteArray("mode=off\n") << int(AppSettings::JDownloader::Off)
            << int(DownloadEngine::Native);
        QTest::newRow("never-chosen")
            << QByteArray() << int(AppSettings::JDownloader::Off) << int(DownloadEngine::Native);
    }
    void earlierSendingBecomesTheJDownloaderForTheOtherServers() {
        QFETCH(QByteArray, saved);
        QFETCH(int, mode);
        QFETCH(int, engine);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("settings.ini"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[downloads]\njdownloader=false\n\n[jdownloader]\n" + saved + "device=dev1\n");
        file.close();

        const AppSettings settings(path, root.filePath(QStringLiteral("Anime")));
        QCOMPARE(int(settings.jdownloader()), mode);
        const auto downloads = settings.downloads();
        QCOMPARE(downloads.engine, DownloadEngine::Native); // AnimeHub still downloads MEGA
        QCOMPARE(int(downloads.otherServers), engine);
        // The connection is the engine's: the same local API and MyJDownloader device.
        QCOMPARE(downloads.jdEndpoint, QUrl(QStringLiteral("http://127.0.0.1:3128")));
        QCOMPARE(downloads.jdDeviceId, QStringLiteral("dev1"));
        // Every server but 1Fichier, which is off until the user turns it on; MP4Upload only
        // through JDownloader.
        auto providers = QList<api::Provider>{api::Provider::Mega, api::Provider::Pixeldrain,
                                              api::Provider::TransferIt};
        if (engine != int(DownloadEngine::Native))
            providers.append(api::Provider::Mp4Upload);
        QCOMPARE(downloads.providers, providers);
    }

    // "Descargar con JDownloader" as earlier versions saved it, with its own connection: the
    // same JDownloader, reached the same way, downloads AnimeHub's servers too.
    void anEarlierJDownloaderEngineKeepsItsConnection_data() {
        QTest::addColumn<QByteArray>("saved");
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("engine");
        QTest::newRow("this pc") << QByteArray("cloudEngine=false\nmode=off\n")
                                 << int(AppSettings::JDownloader::Local)
                                 << int(DownloadEngine::JDownloaderLocal);
        QTest::newRow("myjdownloader")
            << QByteArray("cloudEngine=true\n") << int(AppSettings::JDownloader::Remote)
            << int(DownloadEngine::JDownloaderCloud);
    }
    void anEarlierJDownloaderEngineKeepsItsConnection() {
        QFETCH(QByteArray, saved);
        QFETCH(int, mode);
        QFETCH(int, engine);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("settings.ini"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[downloads]\njdownloader=true\n\n[jdownloader]\n" + saved);
        file.close();

        AppSettings settings(path, root.filePath(QStringLiteral("Anime")));
        QCOMPARE(int(settings.jdownloader()), mode);
        QVERIFY(settings.preferredJDownloader());
        QCOMPARE(int(settings.downloads().engine), engine);
        QCOMPARE(int(settings.downloads().otherServers), engine);
        // AnimeHub downloads again; JDownloader keeps the other servers, reached the same way.
        settings.setPreferredJDownloader(false);
        QCOMPARE(int(settings.jdownloader()), mode);
        QCOMPARE(settings.downloads().engine, DownloadEngine::Native);
        QCOMPARE(int(settings.downloads().otherServers), engine);
        // Not used, it is no one's engine.
        settings.setJdownloader(AppSettings::JDownloader::Off);
        settings.setPreferredJDownloader(true);
        QVERIFY(!settings.preferredJDownloader());
        QCOMPARE(settings.downloads().otherServers, DownloadEngine::Native);
    }

    // What each provider switch shows with 0, 1 or many servers on, with and without JDownloader.
    void providerSwitchesSayWhatCanBeUsed() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        AppSettings settings(root.filePath(QStringLiteral("settings.ini")),
                             root.filePath(QStringLiteral("Anime")));
        const auto state = [&settings] {
            QStringList states;
            for (const auto& entry : settings.providerSwitches()) {
                const auto map = entry.toMap();
                states.append(
                    map.value(QStringLiteral("name")).toString() +
                    (map.value(QStringLiteral("checked")).toBool() ? QStringLiteral(":on")
                                                                   : QStringLiteral(":off")) +
                    (map.value(QStringLiteral("enabled")).toBool() ? QString()
                                                                   : QStringLiteral("!")) +
                    map.value(QStringLiteral("reason")).toString());
            }
            return states.join(QLatin1Char(' '));
        };
        const auto withoutJd = QStringLiteral(" MP4Upload:off!Necesita JDownloader"
                                              " 1Fichier:off!Necesita JDownloader");
        // Without JDownloader, MP4Upload is not on although the user never turned it off.
        QCOMPARE(state(), QStringLiteral("MEGA:on Pixeldrain:on transfer.it:on") + withoutJd);
        // The last one on stays on, and says why.
        settings.setProviderEnabled(QStringLiteral("PIXELDRAIN"), false);
        settings.setProviderEnabled(QStringLiteral("TRANSFERIT"), false);
        const auto onlyMega =
            QStringLiteral("MEGA:on!Al menos un proveedor Pixeldrain:off transfer.it:off");
        QCOMPARE(state(), onlyMega + withoutJd);
        settings.setProviderEnabled(QStringLiteral("MEGA"), false);
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Mega});
        // With JDownloader, MP4Upload comes back as it was left, and MEGA may go off.
        settings.setJdownloader(AppSettings::JDownloader::Local);
        QCOMPARE(state(), QStringLiteral(
                              "MEGA:on Pixeldrain:off transfer.it:off MP4Upload:on 1Fichier:off"));
        settings.setProviderEnabled(QStringLiteral("MEGA"), false);
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Mp4Upload});
        // Without JDownloader again nothing would download: the preferred native comes back.
        settings.setJdownloader(AppSettings::JDownloader::Off);
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Mega});
        QCOMPARE(state(), onlyMega + withoutJd);
    }

    // The servers the user turns off are never asked for; the last one on stays on.
    void turnedOffServersAreLeftOutAndOneStays() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        AppSettings settings(root.filePath(QStringLiteral("settings.ini")),
                             root.filePath(QStringLiteral("Anime")));
        QCOMPARE(settings.disabledProviders(), QStringList{QStringLiteral("ONE_FICHIER")});
        settings.setDisabledProviders({QStringLiteral("MEGA"), QStringLiteral("TRANSFERIT"),
                                       QStringLiteral("MP4UPLOAD"), QStringLiteral("ONE_FICHIER")});
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Pixeldrain});
        settings.setDisabledProviders({QStringLiteral("MEGA"), QStringLiteral("PIXELDRAIN"),
                                       QStringLiteral("TRANSFERIT"), QStringLiteral("MP4UPLOAD"),
                                       QStringLiteral("ONE_FICHIER")});
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Pixeldrain});
    }

    void uiPreferencesPersistAndSanitizeInvalidValues() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath(QStringLiteral("settings.ini"));
        AppSettings settings(path, root.path());
        QSignalSpy changed_spy(&settings, &AppSettings::changed);

        // Defaults.
        QCOMPARE(settings.downloadEpisodesStyle(), QStringLiteral("cards"));
        QCOMPARE(settings.scheduleViewMode(), QStringLiteral("day"));
        QVERIFY(!settings.scheduleFollowingOnly());

        // Persist non-default values.
        settings.setDownloadEpisodesStyle(QStringLiteral("compact"));
        settings.setScheduleViewMode(QStringLiteral("week"));
        settings.setScheduleFollowingOnly(true);
        QCOMPARE(changed_spy.size(), 3);

        // Redundant writes emit no signal.
        settings.setDownloadEpisodesStyle(QStringLiteral("compact"));
        settings.setScheduleViewMode(QStringLiteral("week"));
        settings.setScheduleFollowingOnly(true);
        QCOMPARE(changed_spy.size(), 3);

        const AppSettings reloaded(path, root.path());
        QCOMPARE(reloaded.downloadEpisodesStyle(), QStringLiteral("compact"));
        QCOMPARE(reloaded.scheduleViewMode(), QStringLiteral("week"));
        QVERIFY(reloaded.scheduleFollowingOnly());

        // Invalid values fall back to defaults and persist sanitized values.
        settings.setDownloadEpisodesStyle(QStringLiteral("grid"));
        settings.setScheduleViewMode(QStringLiteral("month"));
        settings.setScheduleFollowingOnly(false);
        QCOMPARE(settings.downloadEpisodesStyle(), QStringLiteral("cards"));
        QCOMPARE(settings.scheduleViewMode(), QStringLiteral("day"));
        QVERIFY(!settings.scheduleFollowingOnly());

        const AppSettings sanitized(path, root.path());
        QCOMPARE(sanitized.downloadEpisodesStyle(), QStringLiteral("cards"));
        QCOMPARE(sanitized.scheduleViewMode(), QStringLiteral("day"));
        QVERIFY(!sanitized.scheduleFollowingOnly());

        // Corrupt values already in settings.ini also fall back safely on read.
        const auto corrupt_path = root.filePath(QStringLiteral("corrupt.ini"));
        QFile file(corrupt_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[ui]\ndownloadEpisodesStyle=invalid\nscheduleViewMode=year\n"
                   "scheduleFollowingOnly=true\n");
        file.close();

        const AppSettings corrupt(corrupt_path, root.path());
        QCOMPARE(corrupt.downloadEpisodesStyle(), QStringLiteral("cards"));
        QCOMPARE(corrupt.scheduleViewMode(), QStringLiteral("day"));
        QVERIFY(corrupt.scheduleFollowingOnly());
    }
};

QTEST_GUILESS_MAIN(AppSettingsTest)
#include "app_settings_test.moc"
