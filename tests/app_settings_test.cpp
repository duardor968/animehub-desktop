#include "app/app_settings.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using app::AppSettings;
using downloads::DownloadEngine;

class AppSettingsTest final : public QObject {
    Q_OBJECT

private slots:
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
        auto providers = QList<api::Provider>{api::Provider::Mega, api::Provider::Pixeldrain};
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
        QCOMPARE(state(), QStringLiteral("MEGA:on Pixeldrain:on") + withoutJd);
        // The last one on stays on, and says why.
        settings.setProviderEnabled(QStringLiteral("PIXELDRAIN"), false);
        const auto onlyMega = QStringLiteral("MEGA:on!Al menos un proveedor Pixeldrain:off");
        QCOMPARE(state(), onlyMega + withoutJd);
        settings.setProviderEnabled(QStringLiteral("MEGA"), false);
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Mega});
        // With JDownloader, MP4Upload comes back as it was left, and MEGA may go off.
        settings.setJdownloader(AppSettings::JDownloader::Local);
        QCOMPARE(state(), QStringLiteral("MEGA:on Pixeldrain:off MP4Upload:on 1Fichier:off"));
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
        settings.setDisabledProviders(
            {QStringLiteral("MEGA"), QStringLiteral("MP4UPLOAD"), QStringLiteral("ONE_FICHIER")});
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Pixeldrain});
        settings.setDisabledProviders({QStringLiteral("MEGA"), QStringLiteral("PIXELDRAIN"),
                                       QStringLiteral("MP4UPLOAD"), QStringLiteral("ONE_FICHIER")});
        QCOMPARE(settings.providers(), QList<api::Provider>{api::Provider::Pixeldrain});
    }
};

QTEST_GUILESS_MAIN(AppSettingsTest)
#include "app_settings_test.moc"
