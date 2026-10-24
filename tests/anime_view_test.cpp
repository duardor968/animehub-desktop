#include "app/anime_view.h"

#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using app::AnimeView;
using downloads::TaskState;
using Action = app::AnimeView::PrimaryAction;

namespace {

constexpr qint64 kGigabyte = 1'000'000'000;

// An anime of three episodes and its page, with nothing that reaches the network: the API and
// MyJDownloader point at a closed port, aria2 does not exist and no transfer starts.
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
    std::unique_ptr<downloads::ClickNLoad> click_n_load;
    std::unique_ptr<downloads::myjd::Client> myjd;
    std::unique_ptr<app::JDownloaderView> jdownloader;
    std::unique_ptr<AnimeView> view;

    bool open(api::AnimeStatus status) {
        auto opened = storage::Database::open(root.filePath(QStringLiteral("library.sqlite3")));
        if (!opened) {
            return false;
        }
        db = std::move(*opened);
        repo = std::make_unique<library::LibraryRepository>(*db);
        queue = std::make_unique<downloads::DownloadRepository>(*db);
        const QUrl closed(QStringLiteral("http://127.0.0.1:1/"));
        api = std::make_unique<api::ApiClient>(
            network, api::ApiClient::Options{.baseUrl = closed, .cacheDirectory = root.path()});
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
        settings = std::make_unique<app::AppSettings>(root.filePath(QStringLiteral("settings.ini")),
                                                      root.filePath(QStringLiteral("Anime")));
        click_n_load = std::make_unique<downloads::ClickNLoad>(network);
        myjd = std::make_unique<downloads::myjd::Client>(network, nullptr, closed);
        jdownloader = std::make_unique<app::JDownloaderView>(
            *settings, *click_n_load, *myjd, QStringLiteral("AnimeHub/Test/anime-view"));

        library::AnimeRecord anime;
        anime.slug = QStringLiteral("a");
        anime.title = QStringLiteral("Anime");
        anime.alternativeTitle = QStringLiteral("ANIME!");
        anime.category = QStringLiteral("TV Anime");
        anime.status = status;
        anime.episodeCount = 3;
        if (!repo->saveSnapshot(anime) || !repo->setInLibrary(anime.slug, true)) {
            return false;
        }
        QList<api::Episode> episodes;
        for (int number = 1; number <= 3; ++number) {
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

    void show() {
        manager->start(false); // no aria2, API or JDownloader traffic
        view = std::make_unique<AnimeView>(
            app::Services{*api, *library, *manager, *jdownloader, *player, *settings});
        view->open(QStringLiteral("a"));
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

        const int airing = int(api::AnimeStatus::Airing);
        const int finished = int(api::AnimeStatus::Finished);
        QTest::newRow("nothing yet")
            << finished << QList<double>{} << QList<double>{} << -1 << int(Action::Download)
            << QStringLiteral("Descargar ep. 1") << QString();
        QTest::newRow("on disk") << finished << QList<double>{} << QList<double>{1} << -1
                                 << int(Action::Play) << QStringLiteral("Ver ep. 1") << QString();
        QTest::newRow("a gap") << finished << QList<double>{1, 3} << QList<double>{1, 3} << -1
                               << int(Action::Download) << QStringLiteral("Descargar ep. 2")
                               << QString();
        QTest::newRow("queued") << finished << QList<double>{1} << QList<double>{}
                                << int(TaskState::Queued) << int(Action::Queued)
                                << QStringLiteral("En cola")
                                << QStringLiteral("Pausar la descarga del episodio 2");
        QTest::newRow("paused") << finished << QList<double>{1} << QList<double>{}
                                << int(TaskState::Paused) << int(Action::Paused)
                                << QStringLiteral("En pausa · Reanudar")
                                << QStringLiteral("Reanudar la descarga del episodio 2");
        QTest::newRow("failed") << finished << QList<double>{1} << QList<double>{}
                                << int(TaskState::Failed) << int(Action::Failed)
                                << QStringLiteral("Error · Reintentar")
                                << QStringLiteral("Ni MEGA ni Pixeldrain tienen este episodio.");
        QTest::newRow("all seen") << finished << QList<double>{1, 2, 3} << QList<double>{1} << -1
                                  << int(Action::Rewatch) << QStringLiteral("Ver de nuevo")
                                  << QString();
        QTest::newRow("up to date")
            << airing << QList<double>{1, 2, 3} << QList<double>{} << -1 << int(Action::Upcoming)
            << QStringLiteral("Al día") << QString();
    }
    void mainButtonFollowsTheNextEpisode() {
        QFETCH(int, status);
        QFETCH(QList<double>, watched);
        QFETCH(QList<double>, files);
        QFETCH(int, taskState);
        QFETCH(int, action);
        QFETCH(QString, text);
        QFETCH(QString, hint);
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
        if (taskState == int(TaskState::Paused)) {
            QCOMPARE(f.view->primaryProgress(), 0.45);
        }
    }

    // One episode is named, several are counted; the bar's figures say only what applies.
    void textsAndFigures() {
        Fixture f;
        QVERIFY(f.open(api::AnimeStatus::Finished));
        QVERIFY(f.watch(1));
        QVERIFY(f.keep(1));
        QVERIFY(f.keep(2));
        QVERIFY(f.add(3, TaskState::Failed));
        f.show();
        QCOMPARE(f.view->alternativeTitle(), QString()); // only punctuation and case differ
        QCOMPARE(f.view->primaryText(), QStringLiteral("Ver ep. 2"));
        QCOMPARE(f.view->missingText(), QStringLiteral("ep. 3"));
        const auto figures = f.view->figures();
        QCOMPARE(value(figures, QStringLiteral("Vistos")), QStringLiteral("1/3"));
        QCOMPARE(value(figures, QStringLiteral("En disco")), QStringLiteral("2 · 2,0 GB"));
        QCOMPARE(value(figures, QStringLiteral("Con error")), QStringLiteral("1 episodio"));
        QVERIFY(value(figures, QStringLiteral("Descargando")).isEmpty());

        auto* episodes = f.view->episodes();
        episodes->toggle(3, false);
        QCOMPARE(episodes->selectedMissingText(), QStringLiteral("ep. 3"));
        episodes->clearSelection();
        QCOMPARE(app::EpisodeModel::countText({4, 5, 6, 7, 8, 9, 10}),
                 QStringLiteral("7 episodios"));
    }
};

QTEST_GUILESS_MAIN(AnimeViewTest)
#include "anime_view_test.moc"
