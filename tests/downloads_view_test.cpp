#include "app/display.h"
#include "app/downloads_view.h"
#include "http_test_server.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using animehub::testing::HttpTestServer;

namespace {

// What the API answers when asked for the episode's sources: links of these servers.
QByteArray resolution(const QStringList& providers) {
    QJsonArray links;
    for (const auto& provider : providers) {
        links.append(QJsonObject{{QStringLiteral("provider"), provider},
                                 {QStringLiteral("url"), QStringLiteral("https://host.test/x")}});
    }
    const QJsonObject episode{{QStringLiteral("episodeNumber"), 1},
                              {QStringLiteral("audio"), QStringLiteral("SUB")},
                              {QStringLiteral("errorCode"), QJsonValue::Null},
                              {QStringLiteral("links"), links}};
    return QJsonDocument(
               QJsonObject{{QStringLiteral("data"),
                            QJsonObject{{QStringLiteral("packageName"), QStringLiteral("A")},
                                        {QStringLiteral("episodes"), QJsonArray{episode}}}}})
        .toJson(QJsonDocument::Compact);
}

// A queue, a library and the page over them. Nothing leaves the computer: the API is a local
// test server, JDownloader points at a closed port and aria2 does not exist, so only
// JDownloader's tasks look for their sources.
struct Fixture {
    QTemporaryDir root;
    HttpTestServer server;
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
    library::AnimeRecord anime;
    // The servers the downloads use, as AppSettings would give them.
    QList<api::Provider> providers{api::Provider::Mega, api::Provider::Pixeldrain};

    // The episode's sources are on these servers.
    void serve(const QStringList& servers) {
        server.route("POST", "/api/v1/anime/a/downloads/resolve",
                     [body = resolution(servers)](const HttpTestServer::Request&) {
                         return HttpTestServer::Response{.body = body};
                     });
    }
    [[nodiscard]] int resolves() const {
        return server.count("/api/v1/anime/a/downloads/resolve");
    }

    bool open() {
        if (!server.listen()) {
            return false;
        }
        auto opened = storage::Database::open(root.filePath(QStringLiteral("library.sqlite3")));
        if (!opened) {
            return false;
        }
        db = std::move(*opened);
        repo = std::make_unique<library::LibraryRepository>(*db);
        queue = std::make_unique<downloads::DownloadRepository>(*db);
        api = std::make_unique<api::ApiClient>(
            network, api::ApiClient::Options{.baseUrl = server.url(QStringLiteral("/api/v1/")),
                                             .cacheDirectory = root.path()});
        library = std::make_unique<library::LibraryService>(*repo, *api);
        aria = std::make_unique<downloads::Aria2>(
            network, root.filePath(QStringLiteral("missing/aria2c.exe")), QByteArray("test"));
        mega = std::make_unique<downloads::mega::Api>(network);
        pixeldrain = std::make_unique<downloads::pixeldrain::Api>(network);
        manager = std::make_unique<downloads::DownloadManager>(
            downloads::DownloadManager::Services{*api, *aria, *mega, *pixeldrain, *queue, *repo},
            [this] { return downloads::DownloadSettings{.providers = providers}; });
        player = std::make_unique<playback::Player>(
            *repo, [] { return playback::PlayerSettings{}; }, [](const QString&) { return false; });
        settings = std::make_unique<app::AppSettings>(root.filePath(QStringLiteral("settings.ini")),
                                                      root.filePath(QStringLiteral("Anime")));
        anime.slug = QStringLiteral("a");
        anime.title = QStringLiteral("A");
        return static_cast<bool>(repo->saveSnapshot(anime));
    }

    // A completed episode whose video is on disk, as the library records it.
    std::optional<downloads::Task> addOnDisk(double number, qint64 bytes) {
        auto task = add(number, downloads::TaskState::Completed);
        if (!task || !QDir().mkpath(QFileInfo(task->targetPath).absolutePath())) {
            return std::nullopt;
        }
        task->targetPath += QStringLiteral(".mp4");
        QFile file(task->targetPath);
        if (!file.open(QIODevice::WriteOnly) || file.write("video") != 5) {
            return std::nullopt;
        }
        file.close();
        task->bytesDone = task->bytesTotal = bytes;
        task->finishedAt = QDateTime::currentDateTime();
        if (!queue->save(*task) || !repo->setEpisodeFile(anime.slug, number, task->targetPath, 5)) {
            return std::nullopt;
        }
        return task;
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
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
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
        QCOMPARE(series.engineLine, line);
        for (const auto& episode : series.episodes) {
            QVERIFY(!episode.detail.contains(QStringLiteral("Incorporad")));
        }
        QVERIFY(!series.summary.contains(QStringLiteral("Incorporad")));
    }

    // A hand-over that never reached JDownloader says why, and the episode can go again.
    void handOverThatNeverReachedJDownloaderSaysWhy() {
        Fixture f;
        QVERIFY(f.open());
        auto jd =
            f.add(1, downloads::TaskState::Failed, downloads::DownloadEngine::JDownloaderLocal);
        QVERIFY(jd);
        jd->issue = downloads::TaskIssue::Engine;
        jd->issueDetail = QStringLiteral(
            "JDownloader no responde en este PC. Activa su API local o usa MyJDownloader.");
        QVERIFY(f.queue->save(*jd));
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        const auto& episode = view.current()->rows().first().episodes.first();
        QCOMPARE(episode.detail, jd->issueDetail);
        QVERIFY(episode.canRetry);
        QCOMPARE(view.current()->rows().first().engineLine,
                 QStringLiteral("JDownloader · ") +
                     QDir::toNativeSeparators(f.root.filePath(QStringLiteral("Anime/a"))));
    }

    void nativeSeriesSaysNothingOfItsEngine() {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.add(1, downloads::TaskState::Paused));
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        const auto& series = view.current()->rows().first();
        QCOMPARE(series.engineLine, QString());
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
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
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
        QVERIFY(f.addOnDisk(1, 100'000'000));
        QVERIFY(f.addOnDisk(2, 186'000'000));
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        QCOMPARE(view.completed()->rows().size(), 1);
        const auto& series = view.completed()->rows().first();
        // One date format on the page: "31 oct", as every date it shows.
        QCOMPARE(series.summary, QStringLiteral("2 episodios · 286 MB · ") +
                                     QLocale(QLocale::Spanish, QLocale::Spain)
                                         .toString(QDate::currentDate(), QStringLiteral("d MMM")));
        QCOMPARE(series.poster, f.anime.posterUrl);
        QVERIFY(series.hasFolder);
        QVERIFY(!series.filesGone);
        QVERIFY(series.episodes.first().hasFile);
        QVERIFY(!view.barVisible());
    }

    // Dark Summoner's case: its videos were deleted and its folder is empty. The row says so,
    // offers to download them again, and has nothing to watch nor a folder to open.
    void completedSeriesWithoutItsFilesSaysTheyAreGone() {
        Fixture f;
        QVERIFY(f.open());
        const auto first = f.addOnDisk(1, 100'000'000);
        const auto second = f.addOnDisk(2, 100'000'000);
        QVERIFY(first && second);
        QVERIFY(QFile::remove(first->targetPath) && QFile::remove(second->targetPath));
        QVERIFY(QDir(QFileInfo(first->targetPath).absolutePath()).isEmpty());
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        QCOMPARE(view.completed()->rows().size(), 1);
        const auto& series = view.completed()->rows().first();
        QVERIFY(series.filesGone);
        QVERIFY(series.summary.startsWith(QStringLiteral("Archivos eliminados · 2 episodios")));
        QCOMPARE(series.watchText, QString());
        QVERIFY(!series.hasFolder);
        QCOMPARE(series.filesOnDisk, 0);
        QCOMPARE(series.episodes.first().detail, QStringLiteral("Archivo eliminado"));
        QVERIFY(!series.episodes.first().hasFile);
    }

    // MEGA's quota, kept across a restart by the episode waiting for it, is said where the
    // download shows: the top of the page, its row and the bar.
    void megaQuotaIsSaidWhereTheDownloadShows() {
        Fixture f;
        QVERIFY(f.open());
        auto waiting = f.add(1, downloads::TaskState::Waiting);
        QVERIFY(waiting);
        waiting->issue = downloads::TaskIssue::Quota;
        waiting->issueDetail = downloads::kMegaStatedTime;
        waiting->retryAt = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(f.queue->save(*waiting));
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        const auto quota = f.manager->megaQuota();
        QVERIFY(quota && quota->known);
        const auto said = QStringLiteral("MEGA llegó a su límite · se libera a las ") +
                          app::display::time(waiting->retryAt);
        QCOMPARE(view.headerNotice(), said);
        QCOMPARE(view.barNotice(), said);
        QCOMPARE(view.current()->rows().first().notice, said);
        QCOMPARE(view.current()->rows().first().episodes.first().detail, said);
        // Under the bar, the episode without a second time: the notice above says it.
        QCOMPARE(view.headerDetail(), QStringLiteral("Episodio 1 · En espera"));
        // Waiting, it can try now, from its row and from its episode.
        QVERIFY(view.current()->rows().first().canRetryNow);
        QVERIFY(view.current()->rows().first().episodes.first().canRetryNow);
        QVERIFY(!view.current()->rows().first().canRetry);
        QCOMPARE(
            app::display::megaQuota(quota->until, false, QStringLiteral("sigue con Pixeldrain")),
            QStringLiteral("MEGA llegó a su límite · sigue con Pixeldrain"));
    }

    // One time for one wait: an episode waiting for its own retry does not borrow another
    // series' MEGA quota and its time; a wait whose time went by while AnimeHub was closed is
    // over, and the episode is back in line.
    void oneTimeForOneWait() {
        Fixture f;
        QVERIFY(f.open());
        const auto wait = [&f](const QString& slug, double number, downloads::TaskIssue issue,
                               qint64 seconds) -> std::optional<downloads::Task> {
            auto task = f.queue->create(slug, number, slug.toUpper(), api::Audio::Sub,
                                        downloads::TaskOrigin::Manual,
                                        f.root.filePath(slug + QString::number(number)));
            if (!task)
                return std::nullopt;
            task->state = downloads::TaskState::Waiting;
            task->issue = issue;
            if (issue == downloads::TaskIssue::Quota)
                task->issueDetail = downloads::kMegaStatedTime;
            task->retryAt = QDateTime::currentDateTimeUtc().addSecs(seconds);
            if (!f.queue->save(*task))
                return std::nullopt;
            return *task;
        };
        const auto own = wait(QStringLiteral("a"), 1, downloads::TaskIssue::Connection, 600);
        const auto gone = wait(QStringLiteral("a"), 2, downloads::TaskIssue::Quota, -600);
        const auto quota = wait(QStringLiteral("b"), 1, downloads::TaskIssue::Quota, 3600);
        QVERIFY(own && gone && quota);
        f.manager->start(false);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        QCOMPARE(view.headerSlug(), QStringLiteral("a"));
        QCOMPARE(view.headerNotice(), QString());
        QCOMPARE(view.headerDetail(), QStringLiteral("Episodio 1 · Se reintenta a las ") +
                                          app::display::time(own->retryAt));
        // The page's bar still says what holds MEGA back.
        QVERIFY(view.barNotice().startsWith(QStringLiteral("MEGA llegó a su límite")));
        QCOMPARE(f.manager->task(gone->id)->state, downloads::TaskState::Queued);
        QCOMPARE(f.manager->task(gone->id)->issue, downloads::TaskIssue::None);
        QCOMPARE(view.current()->rows().first().episodes.at(1).detail, QStringLiteral("En cola"));
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
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        QVERIFY(view.barVisible());
        QCOMPARE(view.barMode(), app::DownloadsView::Mode::Active);
        QCOMPARE(view.barText(), QStringLiteral("Descargando 2 de 3 · A ep. 2"));
        QCOMPARE(view.barPercent(), QStringLiteral("45 %"));
        // The top of the page: the series' figures on its line, the episode's under the bar,
        // each named once.
        QVERIFY2(view.headerEpisode().startsWith(QStringLiteral("1 de 3 descargados")),
                 qPrintable(view.headerEpisode()));
        QVERIFY(!view.headerEpisode().contains(QStringLiteral("Episodio")));
        QCOMPARE(view.headerDetail(), QStringLiteral("Episodio 2 · 0 KB de 0 KB"));
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
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        view.pauseAll();
        f.manager->cancel(queued->id);
        QCOMPARE(f.manager->task(queued->id)->state, downloads::TaskState::Cancelled);
        view.resumeAll();
        QCOMPARE(f.manager->task(paused->id)->state, downloads::TaskState::Queued);
    }

    // An episode only on servers JDownloader downloads, while it is not used, offers it: only
    // while the window shows and once a session. Connected, the episode goes again; only on a
    // server turned off, it offers turning it on, which "No volver a mostrar" keeps away.
    void anEpisodeNoEnabledServerHasOffersWhatWouldDownloadIt() {
        Fixture f;
        QVERIFY(f.open());
        f.serve({QStringLiteral("MP4UPLOAD"), QStringLiteral("ONE_FICHIER")});
        const auto task =
            f.add(1, downloads::TaskState::Queued, downloads::DownloadEngine::JDownloaderLocal);
        QVERIFY(task);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        const auto state = [&] { return f.manager->task(task->id)->state; };
        f.manager->start();
        QTRY_COMPARE(state(), downloads::TaskState::Failed);
        QCOMPARE(f.manager->task(task->id)->issue, downloads::TaskIssue::NoSource);
        QCOMPARE(view.offer(), QString()); // the window is hidden

        view.setWindowShown(true);
        QCOMPARE(view.offer(), QStringLiteral("jdownloader"));
        QCOMPARE(view.offerTitle(), QStringLiteral("A · Episodio 1"));
        QCOMPARE(view.offerText(),
                 QStringLiteral("Este episodio solo está en 1Fichier y MP4Upload, "
                                "que se descargan con JDownloader."));
        view.dismissOffer(false);
        QCOMPARE(view.offer(), QString());
        QVERIFY(!f.settings->jdownloaderOfferHidden());

        // Failing again in the same session, it does not come back.
        f.manager->retry(task->id);
        QTRY_COMPARE(f.resolves(), 2);
        QTRY_COMPARE(state(), downloads::TaskState::Failed);
        QCOMPARE(view.offer(), QString());

        // The setup connected JDownloader: the episode goes again, and the API looks again.
        f.settings->setJdownloader(app::AppSettings::JDownloader::Local);
        QSignalSpy messages(&view, &app::DownloadsView::message);
        view.retryWithJdownloader();
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().first().toString(),
                 QStringLiteral("JDownloader conectado · 1 episodio en cola"));
        QTRY_COMPARE(f.resolves(), 3);
        QVERIFY(QJsonDocument::fromJson(f.server.requests().last().body)
                    .object()
                    .value(u"refresh")
                    .toBool());

        // With JDownloader, 1Fichier is still off: turning it on is offered.
        QTRY_COMPARE(state(), downloads::TaskState::Failed);
        QCOMPARE(view.offer(), QStringLiteral("servers"));
        QCOMPARE(view.offerText(),
                 QStringLiteral("Este episodio solo está en 1Fichier, desactivado en Ajustes."));
        view.acceptOffer(true);
        QCOMPARE(view.offer(), QString());
        QVERIFY(!f.settings->disabledProviders().contains(QStringLiteral("ONE_FICHIER")));
        QTRY_COMPARE(f.resolves(), 4);
        QVERIFY(QJsonDocument::fromJson(f.server.requests().last().body)
                    .object()
                    .value(u"refresh")
                    .toBool());
        // Kept for later sessions.
        QVERIFY(app::AppSettings(f.root.filePath(QStringLiteral("settings.ini")), f.root.path())
                    .serversOfferHidden());
    }

    // "No volver a mostrar" holds in a new session; and an episode no longer failed when the
    // window shows offers nothing.
    void offersStayAwayWhenHiddenOrNoLongerWanted() {
        Fixture f;
        QVERIFY(f.open());
        f.settings->hideJdownloaderOffer();
        f.serve({QStringLiteral("MP4UPLOAD")});
        const auto first =
            f.add(1, downloads::TaskState::Queued, downloads::DownloadEngine::JDownloaderLocal);
        QVERIFY(first);
        app::DownloadsView view(*f.manager, *f.library, *f.player, *f.settings);
        view.setWindowShown(true);
        f.manager->start();
        QTRY_COMPARE(f.manager->task(first->id)->state, downloads::TaskState::Failed);
        QCOMPARE(view.offer(), QString());

        Fixture g;
        QVERIFY(g.open());
        g.serve({QStringLiteral("MP4UPLOAD")});
        const auto second =
            g.add(1, downloads::TaskState::Queued, downloads::DownloadEngine::JDownloaderLocal);
        QVERIFY(second);
        app::DownloadsView later(*g.manager, *g.library, *g.player, *g.settings);
        g.manager->start();
        QTRY_COMPARE(g.manager->task(second->id)->state, downloads::TaskState::Failed);
        g.manager->forget(second->id);
        later.setWindowShown(true);
        QCOMPARE(later.offer(), QString());
    }
};
QTEST_GUILESS_MAIN(DownloadsViewTest)
#include "downloads_view_test.moc"
