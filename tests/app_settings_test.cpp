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
        QCOMPARE(downloads.providers,
                 (QList<api::Provider>{api::Provider::Mega, api::Provider::Pixeldrain}));
    }
};

QTEST_GUILESS_MAIN(AppSettingsTest)
#include "app_settings_test.moc"
