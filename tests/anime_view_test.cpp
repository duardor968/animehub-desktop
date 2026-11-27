#include "app/anime_view.h"

#include "api/response_cache.h"
#include "http_test_server.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using app::AnimeView;
using downloads::TaskState;
using Action = app::AnimeView::PrimaryAction;

namespace {

constexpr qint64 kGigabyte = 1'000'000'000;
constexpr auto kFrieren = "/api/v1/anime/sousou-no-frieren";
constexpr auto kFrierenEpisodes = "/api/v1/anime/sousou-no-frieren/episodes";
constexpr auto kTwoEpisodes =
    R"({"data":[{"id":"1","number":1,"title":null,"imageUrl":null,"publishedAt":null},)"
    R"({"id":"2","number":2,"title":null,"imageUrl":null,"publishedAt":null}],)"
    R"("meta":{"page":1,"perPage":50,"totalPages":1,"totalRecords":2,"firstNumber":1,)"
    R"("lastNumber":2}})";

QByteArray frierenDetail() {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/anime.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture anime.json");
    }
    return file.readAll();
}

// `body` after `delay_ms`.
void serve(testing::HttpTestServer& server, const QByteArray& path, const QByteArray& body,
           int delay_ms) {
    server.route("GET", path, [body, delay_ms](const testing::HttpTestServer::Request&) {
        return testing::HttpTestServer::Response{
            .body = body, .headers = {{"Content-Type", "application/json"}}, .delayMs = delay_ms};
    });
}

// An anime of three episodes and its page, with nothing that reaches the network: the API points
// at a closed port (or a local server), aria2 does not exist and no transfer starts.
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
    std::unique_ptr<app::AppSettings> settings;
    std::unique_ptr<AnimeView> view;
    // Where downloads go once transfers are on.
    QString downloads;
    QUrl apiUrl{QStringLiteral("http://127.0.0.1:1/")};

    // Episodes `first` on, `count` of them; a film is one episode of a "Película".
    bool open(api::AnimeStatus status, int first = 1, int count = 3,
              const QString& category = QStringLiteral("TV Anime")) {
        auto opened = storage::Database::open(root.filePath(QStringLiteral("library.sqlite3")));
        if (!opened) {
            return false;
        }
        db = std::move(*opened);
        repo = std::make_unique<library::LibraryRepository>(*db);
        queue = std::make_unique<downloads::DownloadRepository>(*db);
        api = std::make_unique<api::ApiClient>(
            network, api::ApiClient::Options{.baseUrl = apiUrl, .cacheDirectory = root.path()});
        library = std::make_unique<library::LibraryService>(*repo, *api);
        aria = std::make_unique<downloads::Aria2>(
            network, root.filePath(QStringLiteral("missing/aria2c.exe")), QByteArray("test"));
        mega = std::make_unique<downloads::mega::Api>(network);
        pixeldrain = std::make_unique<downloads::pixeldrain::Api>(network);
        manager = std::make_unique<downloads::DownloadManager>(
            downloads::DownloadManager::Services{*api, *aria, *mega, *pixeldrain, *queue, *repo},
            [this] { return downloads::DownloadSettings{.directory = downloads}; });
        player = std::make_unique<playback::Player>(
            *repo, [] { return playback::PlayerSettings{}; }, [](const QString&) { return false; });
        settings = std::make_unique<app::AppSettings>(root.filePath(QStringLiteral("settings.ini")),
                                                      root.filePath(QStringLiteral("Anime")));

        library::AnimeRecord anime;
        anime.slug = QStringLiteral("a");
        anime.title = QStringLiteral("Anime");
        anime.alternativeTitle = QStringLiteral("ANIME!");
        anime.category = category;
        anime.status = status;
        anime.episodeCount = count;
        if (!repo->saveSnapshot(anime) || !repo->setInLibrary(anime.slug, true)) {
            return false;
        }
        QList<api::Episode> episodes;
        for (int number = first; number < first + count; ++number) {
            api::Episode episode;
            episode.id = QStringLiteral("a%1").arg(number);
            episode.number = number;
            episodes.append(episode);
        }
        return static_cast<bool>(repo->syncEpisodes(anime.slug, episodes, false));
    }

    bool watch(double number) {
        return static_cast<bool>(
            repo->setWatchState(QStringLiteral("a"), number, library::WatchState::Watched));
    }

    bool keep(double number) {
        return static_cast<bool>(
            repo->setEpisodeFile(QStringLiteral("a"), number,
                                 root.filePath(QStringLiteral("a%1.mkv").arg(number)), kGigabyte));
    }

    bool add(double number, TaskState state, qint64 done = 0, qint64 total = 0) {
        auto task = queue->create(QStringLiteral("a"), number, QStringLiteral("Anime"),
                                  api::Audio::Sub, downloads::TaskOrigin::Manual,
                                  root.filePath(QStringLiteral("Anime/a%1").arg(number)));
        if (!task) {
            return false;
        }
        task->state = state;
        task->bytesDone = done;
        task->bytesTotal = total;
        if (state == TaskState::Failed) {
            task->issue = downloads::TaskIssue::NoSource;
        }
        return static_cast<bool>(queue->save(*task));
    }

    // Without transfers there is no aria2, API or JDownloader traffic, and the queue refuses
    // new downloads as a test profile does. With them, aria2 is missing and the API a closed
    // port: a download is queued and then fails, without leaving the machine.
    void show(const QString& slug = QStringLiteral("a"), bool transfers = false) {
        manager->start(transfers);
        view = std::make_unique<AnimeView>(
            app::Services{*api, *library, *manager, *player, *settings});
        view->open(slug);
    }

    ~Fixture() {
        view.reset();
        if (manager) {
            manager->shutdown([] {});
        }
    }
};

QString value(const QVariantList& figures, const QString& caption) {
    for (const auto& figure : figures) {
        const auto map = figure.toMap();
        if (map.value(QStringLiteral("caption")).toString() == caption) {
            return map.value(QStringLiteral("value")).toString();
        }
    }
    return {};
}

} // namespace

class AnimeViewTest : public QObject {
    Q_OBJECT
private slots:
    // The main button follows the episode to watch next, through its download, to the end.
    void mainButtonFollowsTheNextEpisode_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QList<double>>("watched");
        QTest::addColumn<QList<double>>("files");
        QTest::addColumn<int>("taskState"); // of episode 2; < 0: none
        QTest::addColumn<int>("action");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("hint");
        // Where its download is, beside the button.
        QTest::addColumn<QString>("state");

        const int airing = int(api::AnimeStatus::Airing);
        const int finished = int(api::AnimeStatus::Finished);
        // Not begun, nothing on disk or on its way: the whole series.
        QTest::newRow("nothing yet")
            << finished << QList<double>{} << QList<double>{} << -1 << int(Action::DownloadAll)
            << QStringLiteral("Descargar todo (3)") << QString() << QString();
        QTest::newRow("on disk") << finished << QList<double>{} << QList<double>{1} << -1
                                 << int(Action::Play) << QStringLiteral("Ver ep. 1") << QString()
                                 << QString();
        QTest::newRow("a gap") << finished << QList<double>{1, 3} << QList<double>{1, 3} << -1
                               << int(Action::DownloadAll) << QStringLiteral("Descargar ep. 2")
                               << QString() << QString();
        QTest::newRow("queued") << finished << QList<double>{1} << QList<double>{1, 3}
                                << int(TaskState::Queued) << int(Action::PauseAll)
                                << QStringLiteral("Pausar descargas")
                                << QStringLiteral("Pausar la descarga del episodio 2")
                                << QStringLiteral("En cola (1.º)");
        QTest::newRow("downloading")
            << finished << QList<double>{1} << QList<double>{1, 3} << int(TaskState::Downloading)
            << int(Action::PauseAll) << QStringLiteral("Pausar · 45 %")
            << QStringLiteral("Pausar la descarga del episodio 2") << QString();
        QTest::newRow("paused") << finished << QList<double>{1} << QList<double>{1, 3}
                                << int(TaskState::Paused) << int(Action::ResumeAll)
                                << QStringLiteral("Reanudar descargas")
                                << QStringLiteral("Reanudar la descarga del episodio 2")
                                << QStringLiteral("En pausa · 45 %");
        QTest::newRow("failed") << finished << QList<double>{1} << QList<double>{1, 3}
                                << int(TaskState::Failed) << int(Action::Failed)
                                << QStringLiteral("Reintentar fallidos")
                                << QStringLiteral("Reintentar la descarga del episodio 2")
                                << QStringLiteral("Sin servidores disponibles.");
        QTest::newRow("all seen") << finished << QList<double>{1, 2, 3} << QList<double>{1} << -1
                                  << int(Action::Rewatch) << QStringLiteral("Volver a ver")
                                  << QString() << QString();
        // Caught up on an airing series with missing files: still offers DownloadAll.
        QTest::newRow("up to date, missing on disk")
            << airing << QList<double>{1, 2, 3} << QList<double>{} << -1 << int(Action::DownloadAll)
            << QStringLiteral("Descargar todo (3)") << QString() << QString();
        // Up to date with an airing series and all episodes on disk: Upcoming (no button).
        QTest::newRow("up to date")
            << airing << QList<double>{1, 2, 3} << QList<double>{1, 2, 3} << -1
            << int(Action::Upcoming) << QString() << QString() << QString();
        // Paused between seasons, it still gets new episodes: as an airing one.
        QTest::newRow("up to date, between seasons")
            << int(api::AnimeStatus::Paused) << QList<double>{1, 2, 3} << QList<double>{1, 2, 3}
            << -1 << int(Action::Upcoming) << QString() << QString() << QString();
    }
    void mainButtonFollowsTheNextEpisode() {
        QFETCH(int, status);
        QFETCH(QList<double>, watched);
        QFETCH(QList<double>, files);
        QFETCH(int, taskState);
        QFETCH(int, action);
        QFETCH(QString, text);
        QFETCH(QString, hint);
        QFETCH(QString, state);
        Fixture f;
        QVERIFY(f.open(static_cast<api::AnimeStatus>(status)));
        for (const auto number : watched) {
            QVERIFY(f.watch(number));
        }
        for (const auto number : files) {
            QVERIFY(f.keep(number));
        }
        if (taskState >= 0) {
            QVERIFY(f.add(2, static_cast<TaskState>(taskState), 45, 100));
        }
        f.show();
        QCOMPARE(int(f.view->primaryAction()), action);
        QCOMPARE(f.view->primaryText(), text);
        QCOMPARE(f.view->primaryHint(), hint);
        QCOMPARE(f.view->primaryEnabled(), action != int(Action::Upcoming));
        QCOMPARE(value(f.view->figures(), QStringLiteral("Descarga")), state);
        if (taskState == int(TaskState::Paused)) {
            QCOMPARE(f.view->primaryProgress(), 0.45);
        }
    }

    // Everything watched and nothing on disk or on its way: the button downloads the series as
    // one not begun does, never an episode the user did not choose; a film, the film.
    void everythingWatchedWithNothingOnDisk_data() {
        QTest::addColumn<int>("first");
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("category");
        QTest::addColumn<int>("action");
        QTest::addColumn<QString>("text");
        QTest::newRow("from episode 1")
            << 1 << 3 << QStringLiteral("TV Anime") << int(Action::DownloadAll)
            << QStringLiteral("Descargar todo (3)");
        QTest::newRow("with an episode 0")
            << 0 << 23 << QStringLiteral("TV Anime") << int(Action::DownloadAll)
            << QStringLiteral("Descargar todo (23)");
        QTest::newRow("a film") << 1 << 1 << QStringLiteral("Película") << int(Action::Download)
                                << QStringLiteral("Descargar película");
        QTest::newRow("a single episode") << 1 << 1 << QStringLiteral("OVA")
                                          << int(Action::Download) << QStringLiteral("Descargar");
    }
    void everythingWatchedWithNothingOnDisk() {
        QFETCH(int, first);
        QFETCH(int, count);
        QFETCH(QString, category);
        QFETCH(int, action);
        QFETCH(QString, text);
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished, first, count, category));
        for (int number = first; number < first + count; ++number) {
            QVERIFY(f.watch(number));
        }
        f.show();
        QCOMPARE(int(f.view->primaryAction()), action);
        QCOMPARE(f.view->primaryText(), text);
        QVERIFY(f.view->primaryEnabled());
    }

    // A series between seasons says so as the web does, and its header shows only the semantic
    // status chip alongside plain metadata.
    void aPausedSeriesIsBetweenSeasons() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Paused));
        f.show();
        QCOMPARE(f.view->facts(), QStringLiteral("TV · En pausa · 3 episodios"));
        QCOMPARE(f.view->statusText(), QStringLiteral("En pausa"));
        QCOMPARE(f.view->statusTone(), QStringLiteral("paused"));
        QCOMPARE(f.view->metaLine(), QStringLiteral("TV · 3 episodios"));
        QVERIFY(f.view->airing());
        // The header's chips: status chip only, no grey static metadata pills.
        const auto chips = f.view->chips();
        QCOMPARE(chips.size(), 1);
        QCOMPARE(chips[0].toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("En pausa"));
        QCOMPARE(chips[0].toMap().value(QStringLiteral("tone")).toString(),
                 QStringLiteral("paused"));
        const auto details = f.view->details();
        QCOMPARE(details.first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Tipo"));

        Fixture unknown;
        QVERIFY(unknown.open(api::AnimeStatus::Unknown, 1, 1, QStringLiteral("Película")));
        unknown.show();
        QCOMPARE(unknown.view->statusText(), QString());
        QCOMPARE(unknown.view->statusTone(), QString());
        QCOMPARE(unknown.view->metaLine(), QStringLiteral("Película"));
        QVERIFY(unknown.view->chips().isEmpty());

        Fixture finished;
        QVERIFY(finished.open(api::AnimeStatus::Finished));
        if (auto stored = finished.repo->anime(QStringLiteral("a")); stored && *stored) {
            auto record = **stored;
            record.startYear = 2026;
            QVERIFY(finished.repo->saveSnapshot(record));
        }
        finished.show();
        QCOMPARE(finished.view->statusText(), QStringLiteral("Finalizado"));
        QCOMPARE(finished.view->statusTone(), QStringLiteral("finished"));
        QCOMPARE(finished.view->metaLine(), QStringLiteral("TV · 2026 · 3 episodios"));
        QCOMPARE(finished.view->chips().size(), 1);
    }

    // A download that only waits to try again offers to try now, beside where it says so.
    void aWaitingDownloadCanTryNow() {
        const auto action = [](const QVariantList& figures) {
            for (const auto& figure : figures) {
                const auto map = figure.toMap();
                if (map.value(QStringLiteral("caption")).toString() == u"Descarga") {
                    return map.value(QStringLiteral("action")).toString();
                }
            }
            return QString();
        };
        const auto wait = [](Fixture& f, double number) {
            auto task = f.queue->create(QStringLiteral("a"), number, QStringLiteral("Anime"),
                                        api::Audio::Sub, downloads::TaskOrigin::Manual,
                                        f.root.filePath(QStringLiteral("Anime/a%1").arg(number)));
            if (!task) {
                return false;
            }
            task->state = TaskState::Waiting;
            task->issue = downloads::TaskIssue::Network;
            task->retryAt = QDateTime::currentDateTimeUtc().addSecs(600);
            return static_cast<bool>(f.queue->save(*task));
        };
        Fixture one;
        QVERIFY(one.open(api::AnimeStatus::Finished));
        QVERIFY(wait(one, 1));
        one.show();
        QCOMPARE(action(one.view->figures()), QStringLiteral("Reintentar ahora"));

        Fixture batch;
        QVERIFY(batch.open(api::AnimeStatus::Finished));
        QVERIFY(wait(batch, 1));
        QVERIFY(wait(batch, 2));
        batch.show();
        QCOMPARE(value(batch.view->figures(), QStringLiteral("Descarga")),
                 QStringLiteral("En espera · 0/2"));
        QCOMPARE(action(batch.view->figures()), QStringLiteral("Reintentar ahora"));

        // In line, it has nothing to retry.
        Fixture queued;
        QVERIFY(queued.open(api::AnimeStatus::Finished));
        QVERIFY(queued.add(1, TaskState::Queued));
        queued.show();
        QCOMPARE(action(queued.view->figures()), QString());
    }

    // A queued episode says how many wait their turn before it, other series' included.
    void queuedEpisodeKnowsItsPlace() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished));
        QVERIFY(f.keep(2));
        QVERIFY(f.keep(3));
        auto other = f.queue->create(QStringLiteral("b"), 1, QStringLiteral("Otro"),
                                     api::Audio::Sub, downloads::TaskOrigin::Manual,
                                     f.root.filePath(QStringLiteral("Anime/b1")));
        QVERIFY(other);
        QVERIFY(f.add(1, TaskState::Queued));
        f.show();
        QCOMPARE(f.view->primaryAction(), Action::PauseAll);
        QCOMPARE(f.view->primaryText(), QStringLiteral("Pausar descargas"));
        QCOMPARE(value(f.view->figures(), QStringLiteral("Descarga")),
                 QStringLiteral("En cola (2.º)"));
        QCOMPARE(f.view->facts(), QStringLiteral("TV · Finalizado · 3 episodios"));
    }

    // When every missing episode is already in the queue, the button pauses, resumes, or retries
    // the series; paused and failed tasks are never double-counted in missingCount.
    void mainButtonActsOnTheBatch() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished));
        for (int number = 1; number <= 3; ++number) {
            QVERIFY(f.add(number, TaskState::Paused));
        }
        f.show();
        QCOMPARE(f.view->missingCount(), 0);
        QCOMPARE(f.view->primaryAction(), Action::ResumeAll);
        QCOMPARE(f.view->primaryText(), QStringLiteral("Reanudar descargas"));
        QCOMPARE(f.view->primaryHint(), QStringLiteral("Reanudar la descarga de 3 episodios"));
        const auto figures = f.view->figures();
        QCOMPARE(figures.size(), 1);
        QCOMPARE(value(figures, QStringLiteral("Descarga")), QStringLiteral("En pausa · 0/3"));

        Fixture queued;
        QVERIFY(queued.open(api::AnimeStatus::Finished));
        QVERIFY(queued.keep(3));
        QVERIFY(queued.add(1, TaskState::Queued));
        QVERIFY(queued.add(2, TaskState::Paused));
        queued.show();
        QCOMPARE(queued.view->missingCount(), 0);
        QCOMPARE(queued.view->primaryAction(), Action::PauseAll);
        QCOMPARE(queued.view->primaryText(), QStringLiteral("Pausar descargas"));
        QCOMPARE(value(queued.view->figures(), QStringLiteral("Descarga")),
                 QStringLiteral("En cola · 0/2"));

        Fixture failed;
        QVERIFY(failed.open(api::AnimeStatus::Finished));
        for (int number = 1; number <= 3; ++number) {
            QVERIFY(failed.add(number, TaskState::Failed));
        }
        failed.show();
        QCOMPARE(failed.view->missingCount(), 0);
        QCOMPARE(failed.view->primaryAction(), Action::Failed);
        QCOMPARE(failed.view->primaryText(), QStringLiteral("Reintentar fallidos"));
        QCOMPARE(value(failed.view->figures(), QStringLiteral("Con error")),
                 QStringLiteral("3 episodios"));
    }

    // Dual-Role ActionBar (Round 3): global DownloadAll stays stable when an individual episode
    // starts downloading/pausing/failing; switches to Play + secondary SplitButton once the next
    // episode is on disk; uses singular "Descargar ep. X" for 1 left; never hides downloads behind
    // Upcoming when caught up.
    void dualRoleActionBar() {
        // 1) 8 episodes, 0 on disk, idle -> "Descargar todo (8)".
        Fixture idle;
        QVERIFY(idle.open(api::AnimeStatus::Airing, 1, 8));
        idle.show();
        QCOMPARE(idle.view->primaryAction(), Action::DownloadAll);
        QCOMPARE(idle.view->primaryText(), QStringLiteral("Descargar todo (8)"));
        QCOMPARE(idle.view->batchDownloadText(), QStringLiteral("Descargar todo (8)"));
        QVERIFY(!idle.view->showSecondaryDownload());
        QCOMPARE(idle.view->missingCount(), 8);

        // 2) User clicks download on individual ep. 1 (23%) with 7 left -> stays DownloadAll
        //    "Descargar restantes (7)", NEVER mutating into "Pausar · 23 %".
        Fixture one_downloading;
        QVERIFY(one_downloading.open(api::AnimeStatus::Airing, 1, 8));
        QVERIFY(one_downloading.add(1, TaskState::Downloading, 23, 100));
        one_downloading.show();
        QCOMPARE(one_downloading.view->primaryAction(), Action::DownloadAll);
        QCOMPARE(one_downloading.view->primaryText(), QStringLiteral("Descargar restantes (7)"));
        QCOMPARE(one_downloading.view->batchDownloadText(),
                 QStringLiteral("Descargar restantes (7)"));
        QVERIFY(!one_downloading.view->showSecondaryDownload());
        QCOMPARE(one_downloading.view->missingCount(), 7);
        QVERIFY(one_downloading.view->hasDownloads());
        QCOMPARE(value(one_downloading.view->figures(), QStringLiteral("Descarga")),
                 QStringLiteral("Ep. 1 · 23 %"));

        // 3) Paused and failed individual tasks are NOT double-counted in missingCount.
        Fixture paused_and_failed;
        QVERIFY(paused_and_failed.open(api::AnimeStatus::Airing, 1, 8));
        QVERIFY(paused_and_failed.add(1, TaskState::Paused, 23, 100));
        QVERIFY(paused_and_failed.add(2, TaskState::Failed));
        paused_and_failed.show();
        QCOMPARE(paused_and_failed.view->missingCount(), 6);
        QCOMPARE(paused_and_failed.view->primaryAction(), Action::DownloadAll);
        QCOMPARE(paused_and_failed.view->primaryText(), QStringLiteral("Descargar restantes (6)"));
        paused_and_failed.view->episodes()->toggle(1, false);
        paused_and_failed.view->episodes()->toggle(2, false);
        QCOMPARE(paused_and_failed.view->episodes()->selectedMissing(), 0);

        // 4) Cancelled task counts as unqueued missing again.
        Fixture cancelled;
        QVERIFY(cancelled.open(api::AnimeStatus::Airing, 1, 8));
        QVERIFY(cancelled.add(1, TaskState::Cancelled));
        cancelled.show();
        QCOMPARE(cancelled.view->missingCount(), 8);
        QCOMPARE(cancelled.view->primaryText(), QStringLiteral("Descargar todo (8)"));

        // 5) 1 on disk + 7 missing -> Play "Ver ep. 1" + showSecondaryDownload=true.
        Fixture partial;
        QVERIFY(partial.open(api::AnimeStatus::Airing, 1, 8));
        QVERIFY(partial.keep(1));
        partial.show();
        QCOMPARE(partial.view->primaryAction(), Action::Play);
        QCOMPARE(partial.view->primaryText(), QStringLiteral("Ver ep. 1"));
        QVERIFY(partial.view->showSecondaryDownload());
        QCOMPARE(partial.view->batchDownloadText(), QStringLiteral("Descargar restantes (7)"));

        // 6) Singular phrasing: 1 missing episode -> "Descargar ep. 8", NEVER "Descargar todo (1)".
        Fixture one_left;
        QVERIFY(one_left.open(api::AnimeStatus::Airing, 1, 8));
        for (int n = 1; n <= 7; ++n) {
            QVERIFY(one_left.keep(n));
        }
        QVERIFY(one_left.watch(1));
        QVERIFY(one_left.repo->setWatchState(QStringLiteral("a"), 2, library::WatchState::Watching,
                                             60'000, 1'440'000));
        one_left.show();
        QCOMPARE(one_left.view->primaryAction(), Action::Play);
        QCOMPARE(one_left.view->primaryText(), QStringLiteral("Continuar ep. 2"));
        QVERIFY(one_left.view->showSecondaryDownload());
        QCOMPARE(one_left.view->batchDownloadText(), QStringLiteral("Descargar ep. 8"));

        // 7) Caught-up airing (8/8 watched) with missing files -> DownloadAll (not Upcoming).
        Fixture caught_up_airing;
        QVERIFY(caught_up_airing.open(api::AnimeStatus::Airing, 1, 8));
        for (int n = 1; n <= 8; ++n) {
            QVERIFY(caught_up_airing.watch(n));
        }
        caught_up_airing.show();
        QCOMPARE(caught_up_airing.view->primaryAction(), Action::DownloadAll);
        QCOMPARE(caught_up_airing.view->primaryText(), QStringLiteral("Descargar todo (8)"));
        QVERIFY(!caught_up_airing.view->showSecondaryDownload());

        // 8) Single-episode film preserves direct Download / Downloading / Paused / Failed / Play.
        Fixture film;
        QVERIFY(film.open(api::AnimeStatus::Finished, 1, 1, QStringLiteral("Película")));
        QVERIFY(film.add(1, TaskState::Downloading, 45, 100));
        film.show();
        QCOMPARE(film.view->primaryAction(), Action::Downloading);
        QCOMPARE(film.view->primaryText(), QStringLiteral("Pausar · 45 %"));
        QVERIFY(!film.view->showSecondaryDownload());
    }

    // A refused download leaves the anime out of the library; a queued one brings it in, once.
    void onlyAQueuedDownloadJoinsTheLibrary() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished));
        QVERIFY(f.repo->setInLibrary(QStringLiteral("a"), false));
        f.show();
        QVERIFY(!f.view->inLibrary());
        QSignalSpy messages(f.view.get(), &AnimeView::message);
        QSignalSpy added(f.view.get(), &AnimeView::addedToLibrary);
        f.view->download(1); // "Las descargas están desactivadas en este perfil."
        QCOMPARE(messages.size(), 1);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(!f.view->inLibrary());
        QCOMPARE(added.size(), 0);

        Fixture g;
        QVERIFY(g.open(api::AnimeStatus::Finished));
        QVERIFY(g.repo->setInLibrary(QStringLiteral("a"), false));
        g.downloads = g.root.filePath(QStringLiteral("Anime"));
        g.show(QStringLiteral("a"), true);
        QSignalSpy joined(g.view.get(), &AnimeView::addedToLibrary);
        g.view->download(1);
        QVERIFY(g.view->inLibrary());
        QCOMPARE(joined.size(), 1);
        QCOMPARE(joined.first().at(0).toString(), QStringLiteral("a"));
        g.view->download(2); // already there: nothing more to say
        QCOMPARE(joined.size(), 1);
    }

    // Opened with neither a saved record nor a card, the page still adds what the API told it.
    void addsFromTheLoadedDetail() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished));
        QFile fixture(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/anime.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        api::ResponseCache(f.root.path())
            .write(QUrl(QStringLiteral("http://127.0.0.1:1/anime/sousou-no-frieren")),
                   fixture.readAll());
        f.show(QStringLiteral("sousou-no-frieren"));
        QVERIFY(!f.view->loaded()); // no record: only the API's answer, from its saved copy
        QCOMPARE(f.view->title(), QString());
        QSignalSpy added(f.view.get(), &AnimeView::addedToLibrary);
        f.view->setInLibrary(true);
        QVERIFY(f.view->inLibrary());
        QCOMPARE(added.size(), 1);
        QCOMPARE(f.view->title(), QStringLiteral("Sousou no Frieren"));
    }

    // The detail and the episodes are asked for at once and either may answer first: an anime
    // never seen before shows both, its episodes waiting for the detail that saves it.
    void loadsTheDetailAndTheEpisodesAtOnce_data() {
        QTest::addColumn<int>("detailDelay");
        QTest::addColumn<int>("episodesDelay");
        QTest::newRow("episodes first") << 600 << 0;
        QTest::newRow("detail first") << 0 << 600;
    }

    void loadsTheDetailAndTheEpisodesAtOnce() {
        QFETCH(int, detailDelay);
        QFETCH(int, episodesDelay);
        testing::HttpTestServer server;
        QVERIFY(server.listen());
        serve(server, kFrieren, frierenDetail(), detailDelay);
        serve(server, kFrierenEpisodes, kTwoEpisodes, episodesDelay);
        Fixture f;
        f.apiUrl = server.url(QStringLiteral("/api/v1/"));
        QVERIFY(f.open(api::AnimeStatus::Finished));
        f.show(QStringLiteral("sousou-no-frieren"));
        QTRY_COMPARE(server.count(kFrierenEpisodes), 1);
        QCOMPARE(server.count(kFrieren), 1);
        if (detailDelay > 0) {
            // Asked for before the detail answered; they wait for it.
            QVERIFY(!f.view->loaded());
            QVERIFY(f.view->refreshing());
            QCOMPARE(f.view->primaryAction(), Action::None);
        }
        QTRY_VERIFY(!f.view->refreshing());
        QVERIFY(f.view->loaded());
        QCOMPARE(f.view->title(), QStringLiteral("Sousou no Frieren"));
        QVERIFY(!f.view->score().isEmpty());
        QCOMPARE(f.view->episodes()->total(), 2);
        QCOMPARE(f.view->primaryAction(), Action::DownloadAll);
        QVERIFY(f.view->problem().isEmpty());
        QVERIFY(!f.view->inLibrary());
        QCOMPARE(server.count(kFrierenEpisodes), 1);
    }

    // Another anime opened while one loads: the page shows only the new one, and the first
    // one's answers are still saved for when it is opened again.
    void anotherAnimeOpenedMidLoad() {
        testing::HttpTestServer server;
        QVERIFY(server.listen());
        serve(server, kFrieren, frierenDetail(), 300);
        serve(server, kFrierenEpisodes, kTwoEpisodes, 300);
        Fixture f;
        f.apiUrl = server.url(QStringLiteral("/api/v1/"));
        QVERIFY(f.open(api::AnimeStatus::Finished));
        f.show(QStringLiteral("sousou-no-frieren"));
        QTRY_COMPARE(server.count(kFrierenEpisodes), 1);
        f.view->open(QStringLiteral("a"));
        QTRY_VERIFY(!f.library->isRefreshing(QStringLiteral("sousou-no-frieren")));
        QTRY_COMPARE(f.repo->episodes(QStringLiteral("sousou-no-frieren"))->size(), 2);
        QTRY_VERIFY(!f.view->refreshing());
        QCOMPARE(f.view->slug(), QStringLiteral("a"));
        QCOMPARE(f.view->title(), QStringLiteral("Anime"));
        QCOMPARE(f.view->episodes()->total(), 3);
        QVERIFY(f.view->score().isEmpty());
        QCOMPARE(f.view->related()->rowCount(), 0);
        QVERIFY(f.view->problem().isEmpty()); // its own detail failed, but its episodes are here
    }

    // Downloads go to the anime's own folder: named by its title, or the one its files are in.
    void folderIsTheWorks() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished));
        f.show();
        QCOMPARE(f.view->downloadFolder(),
                 QDir::toNativeSeparators(f.root.filePath(QStringLiteral("Anime/Anime"))));
        QVERIFY(!f.view->hasFolder());
        const auto kept = f.root.filePath(QStringLiteral("Anime/a"));
        QVERIFY(QDir().mkpath(kept));
        QVERIFY(f.repo->setEpisodeFile(QStringLiteral("a"), 1, kept + QStringLiteral("/1.mkv"), 1));
        QCOMPARE(f.view->downloadFolder(), QDir::toNativeSeparators(kept));
        QVERIFY(f.view->hasFolder());
    }

    // One episode is named, several are counted; the bar's figures say only what applies, and a
    // failed task is not double-counted as unqueued missing.
    void textsAndFigures() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished, 1, 4));
        QVERIFY(f.watch(1));
        QVERIFY(f.keep(1));
        QVERIFY(f.keep(2));
        QVERIFY(f.add(3, TaskState::Failed));
        f.show();
        QCOMPARE(f.view->alternativeTitle(), QString()); // only punctuation and case differ
        QCOMPARE(f.view->primaryText(), QStringLiteral("Ver ep. 2"));
        QVERIFY(f.view->showSecondaryDownload());
        QCOMPARE(f.view->batchDownloadText(), QStringLiteral("Descargar ep. 4"));
        QCOMPARE(f.view->missingCount(), 1);
        QCOMPARE(f.view->missingText(), QStringLiteral("ep. 4"));
        const auto figures = f.view->figures();
        QCOMPARE(value(figures, QStringLiteral("Vistos")), QStringLiteral("1/4"));
        QCOMPARE(value(figures, QStringLiteral("En disco")), QStringLiteral("2/4 · 2,0 GB"));
        QCOMPARE(value(figures, QStringLiteral("Con error")), QStringLiteral("1 episodio"));
        QVERIFY(value(figures, QStringLiteral("Descargando")).isEmpty());

        auto* episodes = f.view->episodes();
        episodes->toggle(3, false); // failed task is already in queue, not unqueued missing
        QCOMPARE(episodes->selectedMissing(), 0);
        QCOMPARE(episodes->selectedMissingText(), QString());
        episodes->toggle(4, false);
        QCOMPARE(episodes->selectedMissing(), 1);
        QCOMPARE(episodes->selectedMissingText(), QStringLiteral("ep. 4"));
        episodes->clearSelection();
        QCOMPARE(app::EpisodeModel::countText({4, 5, 6, 7, 8, 9, 10}),
                 QStringLiteral("7 episodios"));
    }

    // Rich Catalog detail properties: genreChips ({name, slug}), sourceUrl, mature, scoreText,
    // votesText, scoreSource == "AnimeAV1", Emisión date formatting, and explicit AnimeAV1
    // attribution in details() with offline fallback.
    void richCatalogDetailsAndAttribution() {
        const auto detailValue = [](const QVariantList& rows, const QString& label) {
            for (const auto& item : rows) {
                const auto map = item.toMap();
                if (map.value(QStringLiteral("label")).toString() == label &&
                    map.value(QStringLiteral("name")).toString() == label) {
                    return map.value(QStringLiteral("value")).toString();
                }
            }
            return QString();
        };

        // 1) Full online detail from the Frieren fixture.
        testing::HttpTestServer server;
        QVERIFY(server.listen());
        serve(server, kFrieren, frierenDetail(), 0);
        serve(server, kFrierenEpisodes, kTwoEpisodes, 0);
        Fixture online;
        online.apiUrl = server.url(QStringLiteral("/api/v1/"));
        QVERIFY(online.open(api::AnimeStatus::Finished));
        online.show(QStringLiteral("sousou-no-frieren"));
        QTRY_VERIFY(!online.view->refreshing());

        QCOMPARE(online.view->scoreSource(), QStringLiteral("AnimeAV1"));
        QCOMPARE(online.view->scoreText(), QStringLiteral("9,25"));
        QCOMPARE(online.view->votesText(), QStringLiteral("917.992 votos"));
        QCOMPARE(online.view->sourceUrl(),
                 QStringLiteral("https://animeav1.com/media/sousou-no-frieren"));
        QVERIFY(!online.view->mature());

        const auto chips = online.view->genreChips();
        QCOMPARE(chips.size(), 4);
        QCOMPARE(chips.at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Aventura"));
        QCOMPARE(chips.at(0).toMap().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("aventura"));
        QCOMPARE(chips.at(2).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Fantasía"));
        QCOMPARE(chips.at(2).toMap().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("fantasia"));

        QVERIFY(online.view->hasDetails());
        const auto rows = online.view->details();
        QCOMPARE(detailValue(rows, QStringLiteral("Tipo")), QStringLiteral("TV"));
        QCOMPARE(detailValue(rows, QStringLiteral("Emisión")),
                 QStringLiteral("Sep 2023 – Mar 2024"));
        QCOMPARE(detailValue(rows, QStringLiteral("Estado")), QStringLiteral("Finalizado"));
        QCOMPARE(detailValue(rows, QStringLiteral("Episodios")), QStringLiteral("28"));
        QCOMPARE(detailValue(rows, QStringLiteral("Puntuación (AnimeAV1)")),
                 QStringLiteral("★ 9,25 (917.992 votos)"));

        // 2) Mature anime with same-year range, 4-digit vote grouping ("1.842 votos"), and 7,4.
        constexpr auto kMatureBody =
            R"({"data":{"id":"42","slug":"mature-show","title":"Mature Show",)"
            R"("synopsis":"Sinopsis.","posterUrl":null,"backdropUrl":null,"backdropSrcSet":null,)"
            R"("category":{"id":"1","name":"TV Anime","slug":"tv-anime"},"status":"AIRING",)"
            R"("startDate":"2026-10-01T00:00:00.000Z","endDate":"2026-12-24T00:00:00.000Z",)"
            R"("mature":true,"alternativeTitle":null,"trailerUrl":null,"nextEpisodeAt":null,)"
            R"("episodeCount":8,"score":7.4,"votes":1842,)"
            R"("sourceUrl":"https://animeav1.com/media/mature-show",)"
            R"("genres":[{"id":"1","name":"Acción","slug":"accion"}],"relations":[]}})";
        serve(server, "/api/v1/anime/mature-show", kMatureBody, 0);
        serve(server, "/api/v1/anime/mature-show/episodes", kTwoEpisodes, 0);
        online.view->open(QStringLiteral("mature-show"));
        QTRY_VERIFY(!online.view->refreshing());
        QVERIFY(online.view->mature());
        QCOMPARE(online.view->scoreText(), QStringLiteral("7,4"));
        QCOMPARE(online.view->votesText(), QStringLiteral("1.842 votos"));
        QCOMPARE(online.view->sourceUrl(),
                 QStringLiteral("https://animeav1.com/media/mature-show"));
        const auto mature_rows = online.view->details();
        QCOMPARE(detailValue(mature_rows, QStringLiteral("Emisión")),
                 QStringLiteral("Oct – Dic 2026"));
        QCOMPARE(detailValue(mature_rows, QStringLiteral("Episodios")), QStringLiteral("8"));
        QCOMPARE(detailValue(mature_rows, QStringLiteral("Puntuación (AnimeAV1)")),
                 QStringLiteral("★ 7,4 (1.842 votos)"));

        // 3) Singular vote ("1 voto") and startDate-only ("Oct 2026").
        constexpr auto kSingleVoteBody =
            R"({"data":{"id":"43","slug":"single-vote","title":"Single Vote",)"
            R"("synopsis":null,"posterUrl":null,"backdropUrl":null,"backdropSrcSet":null,)"
            R"("category":{"id":"2","name":"ONA","slug":"ona"},"status":"UPCOMING",)"
            R"("startDate":"2026-10-05T00:00:00.000Z","endDate":null,)"
            R"("mature":false,"alternativeTitle":null,"trailerUrl":null,"nextEpisodeAt":null,)"
            R"("episodeCount":null,"score":7.4,"votes":1,)"
            R"("sourceUrl":"https://animeav1.com/media/single-vote",)"
            R"("genres":[],"relations":[]}})";
        serve(server, "/api/v1/anime/single-vote", kSingleVoteBody, 0);
        serve(server, "/api/v1/anime/single-vote/episodes",
              R"({"data":[],"meta":{"page":1,"perPage":50,"totalPages":0,"totalRecords":0,)"
              R"("firstNumber":null,"lastNumber":null}})",
              0);
        online.view->open(QStringLiteral("single-vote"));
        QTRY_VERIFY(!online.view->refreshing());
        QCOMPARE(online.view->votesText(), QStringLiteral("1 voto"));
        const auto single_rows = online.view->details();
        QCOMPARE(detailValue(single_rows, QStringLiteral("Tipo")), QStringLiteral("ONA"));
        QCOMPARE(detailValue(single_rows, QStringLiteral("Emisión")), QStringLiteral("Oct 2026"));
        QCOMPARE(detailValue(single_rows, QStringLiteral("Estado")),
                 QStringLiteral("Próximamente"));
        QCOMPARE(detailValue(single_rows, QStringLiteral("Episodios")), QString());
        QCOMPARE(detailValue(single_rows, QStringLiteral("Puntuación (AnimeAV1)")),
                 QStringLiteral("★ 7,4 (1 voto)"));

        // 4) Score with no votes omits parentheses ("★ 8").
        constexpr auto kScoreOnlyBody =
            R"({"data":{"id":"44","slug":"score-only","title":"Score Only",)"
            R"("synopsis":null,"posterUrl":null,"backdropUrl":null,"backdropSrcSet":null,)"
            R"("category":{"id":"1","name":"TV Anime","slug":"tv-anime"},"status":"AIRING",)"
            R"("startDate":"2026-04-01T00:00:00.000Z","endDate":"2026-04-01T00:00:00.000Z",)"
            R"("mature":false,"alternativeTitle":null,"trailerUrl":null,"nextEpisodeAt":null,)"
            R"("episodeCount":null,"score":8.0,"votes":null,)"
            R"("sourceUrl":"https://animeav1.com/media/score-only",)"
            R"("genres":[],"relations":[]}})";
        serve(server, "/api/v1/anime/score-only", kScoreOnlyBody, 0);
        serve(server, "/api/v1/anime/score-only/episodes",
              R"({"data":[],"meta":{"page":1,"perPage":50,"totalPages":0,"totalRecords":0,)"
              R"("firstNumber":null,"lastNumber":null}})",
              0);
        online.view->open(QStringLiteral("score-only"));
        QTRY_VERIFY(!online.view->refreshing());
        QCOMPARE(online.view->scoreText(), QStringLiteral("8"));
        QCOMPARE(online.view->votesText(), QString());
        const auto score_only_rows = online.view->details();
        QCOMPARE(detailValue(score_only_rows, QStringLiteral("Emisión")),
                 QStringLiteral("Abr 2026"));
        QCOMPARE(detailValue(score_only_rows, QStringLiteral("Puntuación (AnimeAV1)")),
                 QStringLiteral("★ 8"));

        // 5) Offline fallback: stored genres split & slugified, startYear fallback, no score row.
        Fixture offline;
        QVERIFY(offline.open(api::AnimeStatus::Finished, 1, 12));
        if (auto stored = offline.repo->anime(QStringLiteral("a")); stored && *stored) {
            auto record = **stored;
            record.genres = QStringLiteral("Acción, Ciencia Ficción");
            record.startYear = 2025;
            QVERIFY(offline.repo->saveSnapshot(record));
        }
        offline.show();
        QCOMPARE(offline.view->scoreSource(), QStringLiteral("AnimeAV1"));
        QCOMPARE(offline.view->scoreText(), QString());
        QCOMPARE(offline.view->votesText(), QString());
        QCOMPARE(offline.view->sourceUrl(), QString());
        QVERIFY(!offline.view->mature());
        const auto offline_chips = offline.view->genreChips();
        QCOMPARE(offline_chips.size(), 2);
        QCOMPARE(offline_chips.at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Acción"));
        QCOMPARE(offline_chips.at(0).toMap().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("accion"));
        QCOMPARE(offline_chips.at(1).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Ciencia Ficción"));
        QCOMPARE(offline_chips.at(1).toMap().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("ciencia-ficcion"));
        const auto offline_rows = offline.view->details();
        QCOMPARE(detailValue(offline_rows, QStringLiteral("Emisión")), QStringLiteral("2025"));
        QCOMPARE(detailValue(offline_rows, QStringLiteral("Puntuación (AnimeAV1)")), QString());
        for (const auto& item : offline_rows) {
            const auto val = item.toMap().value(QStringLiteral("value")).toString();
            QVERIFY(!val.isEmpty());
            QVERIFY(val != u"—");
        }
    }
};

QTEST_GUILESS_MAIN(AnimeViewTest)
#include "anime_view_test.moc"
