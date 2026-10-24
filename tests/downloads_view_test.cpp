#include "app/downloads_view.h"
#include <QDir>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;

namespace {

// A queue, a library and the page over them, with nothing that reaches the network: the API
// and JDownloader point at a closed port and aria2 does not exist.
struct Fixture {
    QTemporaryDir root;
    std::unique_ptr<storage::Database> db;
    std::unique_ptr<library::LibraryRepository> repo;
    std::unique_ptr<downloads::DownloadRepository> queue;
    QNetworkAccessManager network;
    std::unique_ptr<api::ApiClient> api;
    std::unique_ptr<library::LibraryService> library;
    std::unique_ptr<downloads::Aria2> aria;
    std::unique_ptr<downloads::mega::Api> mega;
    std::unique_ptr<downloads::pixeldrain::Api> pixeldrain;
    std::unique_ptr<downloads::DownloadManager> manager;
    std::unique_ptr<playback::Player> player;
    library::AnimeRecord anime;

    bool open() {
        auto opened = storage::Database::open(root.filePath(QStringLiteral("library.sqlite3")));
        if (!opened) {
            return false;
        }
        db = std::move(*opened);
        repo = std::make_unique<library::LibraryRepository>(*db);
        queue = std::make_unique<downloads::DownloadRepository>(*db);
        api = std::make_unique<api::ApiClient>(
            network, api::ApiClient::Options{.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                             .cacheDirectory = root.path()});
        library = std::make_unique<library::LibraryService>(*repo, *api);
        aria = std::make_unique<downloads::Aria2>(
            network, root.filePath(QStringLiteral("missing/aria2c.exe")), QByteArray("test"));
        mega = std::make_unique<downloads::mega::Api>(network);
        pixeldrain = std::make_unique<downloads::pixeldrain::Api>(network);
        manager = std::make_unique<downloads::DownloadManager>(
            downloads::DownloadManager::Services{*api, *aria, *mega, *pixeldrain, *queue, *repo},
            [] { return downloads::DownloadSettings{}; });
        player = std::make_unique<playback::Player>(
            *repo, [] { return playback::PlayerSettings{}; }, [](const QString&) { return false; });
        anime.slug = QStringLiteral("a");
        anime.title = QStringLiteral("A");
        return static_cast<bool>(repo->saveSnapshot(anime));
    }

    std::optional<downloads::Task>
    add(double number, downloads::TaskState state,
        downloads::DownloadEngine engine = downloads::DownloadEngine::Native) {
        auto task = queue->create(anime.slug, number, anime.title, api::Audio::Sub,
                                  downloads::TaskOrigin::Manual,
                                  root.filePath(QStringLiteral("Anime/a/A - %1").arg(number)),
                                  engine, QStringLiteral("http://127.0.0.1:1"));
        if (!task) {
            return std::nullopt;
        }
        task->state = state;
        return queue->save(*task) ? std::optional(*task) : std::nullopt;
    }

    ~Fixture() {
        if (manager) {
            manager->shutdown([] {});
        }
    }
};

} // namespace

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
        Fixture f;
        QVERIFY(f.open());
        auto native = f.add(1, downloads::TaskState::Paused);
        QVERIFY(native);
        native->bytesDone = 38;
        native->bytesTotal = 100;
        QVERIFY(f.queue->save(*native));
        auto jd = f.add(2, static_cast<downloads::TaskState>(state),
                        downloads::DownloadEngine::JDownloaderLocal);
        QVERIFY(jd);
        jd->engineSubmission = QStringLiteral("intent");
        jd->enginePhase = phase;
        jd->engineJobId = jobId;
        jd->issue = downloads::TaskIssue::Engine;
        jd->bytesDone = 62;
        jd->bytesTotal = 100;
        QVERIFY(f.queue->save(*jd));
        f.manager->start(false); // No aria2, API or JD traffic.
        app::DownloadsView view(*f.manager, *f.library, *f.player);
        // JDownloader has its task: only the paused native one is AnimeHub's to act on.
        QVERIFY(!view.canPauseAll());
        QVERIFY(view.canResumeAll());
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
        // The engine and the destination, on one line, only where JDownloader takes part.
        const auto line = QStringLiteral("JDownloader · ") +
                          QDir::toNativeSeparators(f.root.filePath(QStringLiteral("Anime/a")));
        QCOMPARE(series.episodes[0].engineLine, QString());
        QCOMPARE(series.episodes[1].engineLine, line);
        QCOMPARE(series.engineLine, line);
        for (const auto& episode : series.episodes) {
            QVERIFY(!episode.detail.contains(QStringLiteral("Incorporad")));
        }
        QVERIFY(!series.summary.contains(QStringLiteral("Incorporad")));
    }

    void nativeSeriesSaysNothingOfItsEngine() {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.add(1, downloads::TaskState::Paused));
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player);
        const auto& series = view.current()->rows().first();
        QCOMPARE(series.engineLine, QString());
        QCOMPARE(series.episodes.first().engineLine, QString());
    }

    // "Reanudar todo" brings back only what "Pausar todo" stopped; what the user paused on
    // its own waits for them, unless nothing was paused all together.
    void resumingAllLeavesWhatThePersonPaused() {
        Fixture f;
        QVERIFY(f.open());
        const auto queued = f.add(1, downloads::TaskState::Queued);
        const auto paused = f.add(2, downloads::TaskState::Paused);
        QVERIFY(queued && paused);
        f.manager->start(); // aria2 never starts, so the queued one waits for it
        app::DownloadsView view(*f.manager, *f.library, *f.player);
        const auto state = [&](qint64 id) { return f.manager->task(id)->state; };
        QCOMPARE(state(queued->id), downloads::TaskState::Queued);
        QTRY_VERIFY(view.canPauseAll());

        view.pauseAll();
        QCOMPARE(state(queued->id), downloads::TaskState::Paused);
        QVERIFY(f.manager->task(queued->id)->pausedWithAll);
        QVERIFY(!f.manager->task(paused->id)->pausedWithAll);
        QTRY_VERIFY(!view.canPauseAll() && view.canResumeAll());

        view.resumeAll();
        QCOMPARE(state(queued->id), downloads::TaskState::Queued);
        QCOMPARE(state(paused->id), downloads::TaskState::Paused);

        view.resumeAll();
        QCOMPARE(state(paused->id), downloads::TaskState::Queued);
    }

    // A finished series says how many episodes it brought, their size and when, and its row
    // shows the poster, not the backdrop the top of the page uses.
    void completedSeriesSaysWhenItFinished() {
        Fixture f;
        f.anime.posterUrl = QUrl(QStringLiteral("https://img.test/poster.webp"));
        f.anime.backdropUrl = QUrl(QStringLiteral("https://img.test/backdrop.webp"));
        QVERIFY(f.open());
        const auto finished = QDateTime::currentDateTime();
        for (const auto& [number, bytes] : {std::pair{1.0, 100'000'000LL}, {2.0, 186'000'000LL}}) {
            auto task = f.add(number, downloads::TaskState::Completed);
            QVERIFY(task);
            task->bytesDone = task->bytesTotal = bytes;
            task->finishedAt = finished.addSecs(number == 1.0 ? -60 : 0);
            QVERIFY(f.queue->save(*task));
        }
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player);
        QCOMPARE(view.completed()->rows().size(), 1);
        const auto& series = view.completed()->rows().first();
        QCOMPARE(series.summary, QStringLiteral("2 episodios · 286 MB · hoy ") +
                                     QLocale(QLocale::Spanish, QLocale::Spain)
                                         .toString(finished.time(), QStringLiteral("HH:mm")));
        QCOMPARE(series.poster, f.anime.posterUrl);
        QVERIFY(!view.barVisible());
    }

    // The bar counts the episode on view among the whole line, finished ones of series still
    // in line included, as Steam's: "Descargando 2 de 3 · A ep. 2".
    void barCountsTheLineAsSteam() {
        Fixture f;
        f.anime.posterUrl = QUrl(QStringLiteral("https://img.test/poster.webp"));
        QVERIFY(f.open());
        auto done = f.add(1, downloads::TaskState::Completed);
        QVERIFY(done);
        done->finishedAt = QDateTime::currentDateTime();
        QVERIFY(f.queue->save(*done));
        auto running = f.add(2, downloads::TaskState::Downloading);
        QVERIFY(running);
        running->bytesDone = 45;
        running->bytesTotal = 100;
        QVERIFY(f.queue->save(*running));
        QVERIFY(f.add(3, downloads::TaskState::Failed));
        f.manager->start(false); // the state stays as stored: nothing transfers
        app::DownloadsView view(*f.manager, *f.library, *f.player);
        QVERIFY(view.barVisible());
        QCOMPARE(view.barMode(), app::DownloadsView::Mode::Active);
        QCOMPARE(view.barText(), QStringLiteral("Descargando 2 de 3 · A ep. 2"));
        QCOMPARE(view.barPercent(), QStringLiteral("45 %"));
        QCOMPARE(view.barProblem(), QStringLiteral("1 con error"));
        QCOMPARE(view.barPoster(), f.anime.posterUrl);
    }

    // Only what is still paused counts: one paused all together and then cancelled does not
    // keep the rest waiting.
    void aCancelledTaskDoesNotHoldBackResumingAll() {
        Fixture f;
        QVERIFY(f.open());
        const auto queued = f.add(1, downloads::TaskState::Queued);
        const auto paused = f.add(2, downloads::TaskState::Paused);
        QVERIFY(queued && paused);
        f.manager->start();
        app::DownloadsView view(*f.manager, *f.library, *f.player);
        view.pauseAll();
        f.manager->cancel(queued->id);
        QCOMPARE(f.manager->task(queued->id)->state, downloads::TaskState::Cancelled);
        view.resumeAll();
        QCOMPARE(f.manager->task(paused->id)->state, downloads::TaskState::Queued);
    }
};
QTEST_GUILESS_MAIN(DownloadsViewTest)
#include "downloads_view_test.moc"
