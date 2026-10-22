#include "app/downloads_view.h"
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
class DownloadsViewTest : public QObject {
    Q_OBJECT
private slots:
    void mixedEnginesAndUncertainSubmission_data() {
        QTest::addColumn<int>("state");
        QTest::addColumn<QString>("phase");
        QTest::addColumn<qint64>("jobId");
        QTest::addColumn<QString>("expectedDetail");
        QTest::newRow("uncertain-send")
            << int(downloads::TaskState::Failed) << QStringLiteral("submitting") << qint64(0)
            << QStringLiteral("sin confirmar");
        QTest::newRow("offline-progress")
            << int(downloads::TaskState::Waiting) << QStringLiteral("transferring") << qint64(42)
            << QStringLiteral("Sin conexión");
        QTest::newRow("extracting")
            << int(downloads::TaskState::Downloading) << QStringLiteral("extracting") << qint64(42)
            << QStringLiteral("Extrayendo");
    }
    void mixedEnginesAndUncertainSubmission() {
        QFETCH(int, state);
        QFETCH(QString, phase);
        QFETCH(qint64, jobId);
        QFETCH(QString, expectedDetail);
        QTemporaryDir root;
        auto opened = storage::Database::open(root.filePath(QStringLiteral("library.sqlite3")));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = root.path()});
        library::LibraryService library(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager manager({api, aria, mega, pixeldrain, queue, repo},
                                           [] { return downloads::DownloadSettings{}; });
        playback::Player player(
            repo, [] { return playback::PlayerSettings{}; }, [](const QString&) { return false; });
        library::AnimeRecord anime;
        anime.slug = QStringLiteral("a");
        anime.title = QStringLiteral("A");
        QVERIFY(repo.saveSnapshot(anime));
        auto native =
            queue.create(anime.slug, 1, anime.title, api::Audio::Sub, downloads::TaskOrigin::Manual,
                         root.filePath(QStringLiteral("one.mp4")));
        QVERIFY(native);
        native->state = downloads::TaskState::Paused;
        native->bytesDone = 38;
        native->bytesTotal = 100;
        QVERIFY(queue.save(*native));
        auto jd = queue.create(
            anime.slug, 2, anime.title, api::Audio::Sub, downloads::TaskOrigin::Manual,
            root.filePath(QStringLiteral("two.mp4")), downloads::DownloadEngine::JDownloaderLocal,
            QStringLiteral("http://127.0.0.1:1"));
        QVERIFY2(jd, jd ? "" : qPrintable(jd.error().message));
        jd->state = static_cast<downloads::TaskState>(state);
        jd->engineSubmission = QStringLiteral("intent");
        jd->enginePhase = phase;
        jd->engineJobId = jobId;
        jd->issue = downloads::TaskIssue::Engine;
        jd->bytesDone = 62;
        jd->bytesTotal = 100;
        jd->engineDestination = root.path();
        QVERIFY(queue.save(*jd));
        manager.start(false); // No aria2, API or JD traffic.
        app::DownloadsView view(manager, library, player);
        QCOMPARE(view.activeCount(), state == int(downloads::TaskState::Downloading) ? 1 : 0);
        QVERIFY(!view.canPauseNative());
        QVERIFY(view.canResumeNative());
        QVERIFY(!view.canRetryFailed());
        QCOMPARE(view.current()->rows().size(), 1);
        const auto& series = view.current()->rows().first();
        QCOMPARE(series.episodes.size(), 2);
        QVERIFY(series.canResume);
        QVERIFY(!series.canPause);
        QVERIFY(!series.canRetry);
        QCOMPARE(series.episodes[0].progress, .38);
        QCOMPARE(series.episodes[1].progress, .62);
        QVERIFY(!series.episodes[1].canPause);
        QVERIFY(!series.episodes[1].canResume);
        QVERIFY(!series.episodes[1].canRetry);
        QVERIFY(series.episodes[1].detail.contains(expectedDetail));
        QCOMPARE(series.episodes[1].destination, root.path());
        QCOMPARE(series.destinations, root.path());
        QVERIFY(series.engines.contains(QStringLiteral("JDownloader")));
        QVERIFY(series.engines.contains(QStringLiteral("Incorporado")));
        QCOMPARE(series.episodes[1].connection, QStringLiteral("Local"));
        manager.shutdown([] {});
    }
};
QTEST_GUILESS_MAIN(DownloadsViewTest)
#include "downloads_view_test.moc"