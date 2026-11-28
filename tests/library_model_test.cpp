#include "app/library_model.h"
#include "app/schedule.h"
#include "http_test_server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace animehub;
using animehub::testing::HttpTestServer;
using app::LibraryModel;

class LibraryModelTest : public QObject {
    Q_OBJECT
private slots:
    void viewsArchiveAndExplicitMembership() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });
        for (const auto& slug : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = slug.toUpper();
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
        }
        auto group = repo.createFranchise(QStringLiteral("Series"));
        QVERIFY(group);
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("a"), true));
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("b"), true));
        LibraryModel model(service, downloads, api);
        QCOMPARE(model.rowCount(), 2); // a real group and an independent anime
        QCOMPARE(model.total(), 3);
        model.setGrouped(false);
        QCOMPARE(model.rowCount(), 3);
        QSet<QString> unique;
        for (int row = 0; row < model.rowCount(); ++row) {
            QVERIFY(model.groupAt(row) == 0);
            unique.insert(model.slugAt(row));
        }
        QCOMPARE(unique.size(), 3);
        model.setGrouped(true);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.indexOfGroup(*group) >= 0); // the group and C, by title
        QVERIFY(model.setFranchiseMember(*group, QStringLiteral("b"), false));
        QCOMPARE(model.rowCount(), 3); // one work is still a personal franchise; b stands alone
        model.toggleGroup(*group);
        QCOMPARE(model.rowCount(), 4);
        model.openFranchise(*group);
        QCOMPARE(model.franchise().value(QStringLiteral("works")).toList().size(), 1);
        QVERIFY(model.archive({QStringLiteral("a")}, true));
        QCOMPARE(model.archivedCount(), 1);
        QCOMPARE(model.total(), 2);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.contains(QStringLiteral("a"))); // archived is a retained personal record
        model.setArchivedOnly(true);
        QCOMPARE(model.total(), 1);
        QCOMPARE(model.rowCount(), 2); // group plus its expanded archived member
        model.setGrouped(false);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("a"));
        QVERIFY(model.archive({QStringLiteral("a")}, false));
        QCOMPARE(model.rowCount(), 0);
        model.setArchivedOnly(false);
        QCOMPARE(model.rowCount(), 3);
        model.setSearch(QStringLiteral("B"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("b"));
        model.setSearch(QString());
        QVERIFY(model.removePersonal({QStringLiteral("c")}));
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.contains(QStringLiteral("c")));
        const auto before = repo.franchises();
        QVERIFY(before);
        QVERIFY(!model.createFranchise(QStringLiteral("   "), QStringLiteral("a")));
        QVERIFY(!model.createFranchise(QStringLiteral("Unknown"), QStringLiteral("missing")));
        QCOMPARE(repo.franchises()->size(), before->size());
        QVERIFY(model.createFranchise(QStringLiteral("Custom"), QStringLiteral("b")));
        const auto personal = model.franchise().value(QStringLiteral("id")).toLongLong();
        QVERIFY(personal != *group);
        QCOMPARE(model.memberChoices(personal).size(), 1);
        QCOMPARE(
            model.memberChoices(personal).first().toMap().value(QStringLiteral("slug")).toString(),
            QStringLiteral("a"));
        QVERIFY(model.setFranchiseMember(personal, QStringLiteral("a"), true));
        model.openFranchise(*group);
        QVERIFY(model.franchise().value(QStringLiteral("works")).toList().isEmpty());
        model.openFranchise(personal);
        QCOMPARE(model.franchise().value(QStringLiteral("works")).toList().size(), 2);
        model.setGrouped(true);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.groupAt(0), personal);
        model.setSearch(QStringLiteral("A"));
        QCOMPARE(model.rowCount(), 2); // search opens the matching member under its personal group
        QCOMPARE(model.groupAt(0), personal);
        QCOMPARE(model.slugAt(1), QStringLiteral("a"));
    }

    // What is followed splits by what it waits for: the next episode on disk, or episodes to
    // download. A series seen up to a skipped episode goes on at it.
    void tasksSplitWhatIsOnDiskFromWhatIsMissing() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });
        // ready: 1 seen, 2 on disk, 3 missing. hole: finished, 1 and 3 seen, 2 missing.
        // done: finished and seen.
        const auto add = [&](const QString& slug, api::AnimeStatus status, int count) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = slug;
            record.status = status;
            record.episodeCount = count;
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            QList<api::Episode> episodes;
            for (int number = 1; number <= count; ++number) {
                api::Episode episode;
                episode.id = slug + QString::number(number);
                episode.number = number;
                episodes.append(episode);
            }
            QVERIFY(repo.syncEpisodes(slug, episodes, false));
        };
        add(QStringLiteral("ready"), api::AnimeStatus::Airing, 3);
        add(QStringLiteral("hole"), api::AnimeStatus::Finished, 3);
        add(QStringLiteral("done"), api::AnimeStatus::Finished, 2);
        QVERIFY(repo.setWatchState(QStringLiteral("ready"), 1, library::WatchState::Watched));
        QVERIFY(repo.setEpisodeFile(QStringLiteral("ready"), 2,
                                    cache.filePath(QStringLiteral("r2.mkv")), 1));
        QVERIFY(repo.setWatchState(QStringLiteral("hole"), 1, library::WatchState::Watched));
        QVERIFY(repo.setWatchState(QStringLiteral("hole"), 3, library::WatchState::Watched));
        QVERIFY(repo.setWatchedThrough(QStringLiteral("done"), 2));

        const auto hole = repo.entry(QStringLiteral("hole"));
        QVERIFY(hole && *hole);
        QCOMPARE((*hole)->resumeNumber, std::optional<double>(2));

        LibraryModel model(service, downloads, api);
        QCOMPARE(model.readyCount(), 1);
        // The first to watch leads the shelf, featured; its line reads as the cards' beside it.
        QCOMPARE(model.featured().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("ready"));
        QCOMPARE(model.featured().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("Ep. 2 · 1/3 vistos"));
        QCOMPARE(model.featured().value(QStringLiteral("actionText")).toString(),
                 QStringLiteral("Continuar"));
        QVERIFY(model.readyShelf()->cards().isEmpty());
        QCOMPARE(model.downloadCount(), 2);
        QCOMPARE(model.pendingCount(), 2); // ready's 3 and hole's 2
        for (const auto& card : model.downloadShelf()->cards()) {
            QCOMPARE(card.caption, card.slug == u"ready" ? QStringLiteral("Ep. 3 · 1/3 vistos")
                                                         : QStringLiteral("Ep. 2 · 2/3 vistos"));
        }

        // "Para ver" as a filter lets through what the home's shelf holds, and counts the same.
        model.setFilter(LibraryModel::Filter::ToWatch);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("ready"));
        QCOMPARE(model.filterCounts().at(static_cast<int>(LibraryModel::Filter::ToWatch)),
                 model.readyCount());
        QCOMPARE(model.shownCount(), 1);

        model.setFilter(LibraryModel::Filter::Watched);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("done"));
        // Under "Vistos" a card says what the work is, not "Visto" again.
        QCOMPARE(model.overview().first().toMap().value(QStringLiteral("caption")).toString(),
                 QStringLiteral("2 ep."));
        // A search looks through the whole library, whatever the filter.
        model.setSearch(QStringLiteral("rea"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("ready"));
        model.setSearch(QString());
        QCOMPARE(model.slugAt(0), QStringLiteral("done"));
        model.setFilter(LibraryModel::Filter::All);
        model.setOnDiskOnly(true);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.slugAt(0), QStringLiteral("ready"));
        QCOMPARE(model.filterCounts().at(static_cast<int>(LibraryModel::Filter::All)), 1);
    }

    // A franchise's page: its works by what sets them apart, where the user is with each, and
    // the main button going on with the one under way.
    void franchiseGoesOnWithTheWorkUnderWay() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });
        const auto add = [&](const QString& slug, const QString& title, int year,
                             api::AnimeStatus status, int count) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = title;
            record.category = QStringLiteral("TV Anime");
            record.startYear = year;
            record.status = status;
            record.episodeCount = count;
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            QList<api::Episode> episodes;
            for (int number = 1; number <= count; ++number) {
                api::Episode episode;
                episode.id = slug + QString::number(number);
                episode.number = number;
                episodes.append(episode);
            }
            QVERIFY(repo.syncEpisodes(slug, episodes, false));
        };
        add(QStringLiteral("first"), QStringLiteral("Series"), 2012, api::AnimeStatus::Finished, 2);
        add(QStringLiteral("second"), QStringLiteral("Series: Part 2"), 2014,
            api::AnimeStatus::Airing, 3);
        QVERIFY(repo.setWatchedThrough(QStringLiteral("first"), 2));
        QVERIFY(repo.setWatchState(QStringLiteral("second"), 1, library::WatchState::Watched));
        QVERIFY(repo.setEpisodeFile(QStringLiteral("second"), 2,
                                    cache.filePath(QStringLiteral("s2.mkv")), 2'000'000'000));
        auto group = repo.createFranchise(QStringLiteral("Series"));
        QVERIFY(group);
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("first"), true));
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("second"), true));

        LibraryModel model(service, downloads, api);
        // The breadcrumb's middle step: the franchise of a work, unless it carries the work's own
        // name, which the page's title already says.
        QCOMPARE(model.franchiseStep(QStringLiteral("second")).value(QStringLiteral("title")),
                 QVariant(QStringLiteral("Series")));
        QVERIFY(model.franchiseStep(QStringLiteral("first")).isEmpty());
        QVERIFY(model.franchiseStep(QStringLiteral("missing")).isEmpty());
        model.openFranchise(*group);
        const auto franchise = model.franchise();
        const auto works = franchise.value(QStringLiteral("works")).toList();
        QCOMPARE(works.size(), 2);
        const auto first = works.at(0).toMap();
        const auto second = works.at(1).toMap();
        QCOMPARE(first.value(QStringLiteral("shortTitle")).toString(), QStringLiteral("Series"));
        QCOMPARE(second.value(QStringLiteral("shortTitle")).toString(), QStringLiteral("Part 2"));
        QCOMPARE(first.value(QStringLiteral("facts")).toString(),
                 QStringLiteral("TV · 2012 · 2 ep."));
        QCOMPARE(first.value(QStringLiteral("state")).toString(), QStringLiteral("Visto"));
        QCOMPARE(second.value(QStringLiteral("state")).toString(),
                 QStringLiteral("Siguiente: Ep. 2"));
        QCOMPARE(second.value(QStringLiteral("action")).toString(), QStringLiteral("play"));
        QCOMPARE(franchise.value(QStringLiteral("facts")).toString(), QStringLiteral("2012–2014"));
        QCOMPARE(franchise.value(QStringLiteral("nextSlug")).toString(), QStringLiteral("second"));
        QCOMPARE(franchise.value(QStringLiteral("nextText")).toString(),
                 QStringLiteral("Ver ep. 2"));
        QCOMPARE(franchise.value(QStringLiteral("nextAction")).toString(), QStringLiteral("play"));
        const auto figures = franchise.value(QStringLiteral("figures")).toList();
        QCOMPARE(figures.size(), 3);
        QCOMPARE(figures.at(0).toMap().value(QStringLiteral("value")).toString(),
                 QStringLiteral("Part 2"));
        // Episodes, as an anime's page counts them: 2 of the first and 1 of the second.
        QCOMPARE(figures.at(1).toMap().value(QStringLiteral("caption")).toString(),
                 QStringLiteral("Vistos"));
        QCOMPARE(figures.at(1).toMap().value(QStringLiteral("value")).toString(),
                 QStringLiteral("3/5"));
        QCOMPARE(figures.at(2).toMap().value(QStringLiteral("caption")).toString(),
                 QStringLiteral("En disco"));
        QCOMPARE(figures.at(2).toMap().value(QStringLiteral("value")).toString(),
                 QStringLiteral("1/5 · 2,0 GB"));

        // Its next episode not on disk: the button brings it.
        QVERIFY(repo.setWatchState(QStringLiteral("second"), 2, library::WatchState::Watched));
        LibraryModel later(service, downloads, api);
        later.openFranchise(*group);
        QCOMPARE(later.franchise().value(QStringLiteral("nextText")).toString(),
                 QStringLiteral("Descargar ep. 3"));
        QCOMPARE(later.franchise().value(QStringLiteral("nextAction")).toString(),
                 QStringLiteral("download"));

        // All seen that has aired: nothing to go on with.
        QVERIFY(repo.setWatchState(QStringLiteral("second"), 3, library::WatchState::Watched));
        LibraryModel done(service, downloads, api);
        done.openFranchise(*group);
        QVERIFY(done.franchise().value(QStringLiteral("nextSlug")).toString().isEmpty());
        QVERIFY(done.franchise().value(QStringLiteral("nextText")).toString().isEmpty());
    }

    // Featured exposes rich hero metadata; upcomingDays exposes isToday/badgeText/finale;
    // setWindowShown(false) pauses the minute clock timer.
    void featuredDetailsUpcomingAgendaAndWindowShown() {
        HttpTestServer server;
        QVERIFY(server.listen());
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = server.url(QStringLiteral("/api/v1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });

        const auto add_anime = [&](const QString& slug, const QString& title,
                                   api::AnimeStatus status, int count, const QString& genres = {},
                                   const QUrl& backdrop = {}) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = title;
            record.status = status;
            record.episodeCount = count;
            record.genres = genres;
            record.backdropUrl = backdrop;
            record.posterUrl =
                QUrl(QStringLiteral("https://img.test/") + slug + QStringLiteral(".webp"));
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            QList<api::Episode> episodes;
            for (int number = 1; number <= count; ++number) {
                api::Episode episode;
                episode.id = slug + QString::number(number);
                episode.number = number;
                episodes.append(episode);
            }
            QVERIFY(repo.syncEpisodes(slug, episodes, false));
        };

        add_anime(QStringLiteral("hero-show"), QStringLiteral("Hero Show"),
                  api::AnimeStatus::Airing, 12, QStringLiteral("Acción, Aventura, Fantasía, Drama"),
                  QUrl(QStringLiteral("https://img.test/hero-bg.webp")));
        add_anime(QStringLiteral("finale-show"), QStringLiteral("Finale Show"),
                  api::AnimeStatus::Finished, 12);
        add_anime(QStringLiteral("future-show"), QStringLiteral("Future Show"),
                  api::AnimeStatus::Airing, 12);

        QVERIFY(repo.setWatchState(QStringLiteral("hero-show"), 1, library::WatchState::Watched));
        QVERIFY(repo.setWatchState(QStringLiteral("hero-show"), 2, library::WatchState::Watching,
                                   600'000, 1'440'000));
        QVERIFY(repo.setEpisodeFile(QStringLiteral("hero-show"), 2,
                                    cache.filePath(QStringLiteral("h2.mkv")), 10));
        QVERIFY(repo.setEpisodeFile(QStringLiteral("hero-show"), 3,
                                    cache.filePath(QStringLiteral("h3.mkv")), 10));

        const auto now = QDateTime::currentDateTimeUtc();
        const auto local_now = now.toLocalTime();
        const auto aired_today =
            local_now.time() > QTime(0, 1)
                ? QDateTime(local_now.date(), QTime(0, 0, 30), local_now.timeZone()).toUTC()
                : now;
        const auto future_basis = now.addDays(-5); // +7 days => 2 days ahead

        const auto make_entry = [](const QString& slug, const QString& title, const QString& status,
                                   int number, const QDateTime& basis, bool is_final) {
            const auto iso = basis.toUTC().toString(Qt::ISODateWithMs);
            return QJsonObject{
                {QStringLiteral("anime"), QJsonObject{{QStringLiteral("id"), slug},
                                                      {QStringLiteral("slug"), slug},
                                                      {QStringLiteral("title"), title},
                                                      {QStringLiteral("status"), status},
                                                      {QStringLiteral("mature"), false}}},
                {QStringLiteral("latestEpisode"),
                 QJsonObject{{QStringLiteral("id"), slug + QString::number(number)},
                             {QStringLiteral("number"), number},
                             {QStringLiteral("publishedAt"), iso}}},
                {QStringLiteral("basisPublishedAt"), iso},
                {QStringLiteral("isFinalEpisode"), is_final},
            };
        };

        bool serve_stale = false;
        server.route("GET", "/api/v1/schedule", [&](const HttpTestServer::Request&) {
            const QJsonArray data{
                make_entry(QStringLiteral("finale-show"), QStringLiteral("Finale Show"),
                           QStringLiteral("FINISHED"), 12, aired_today, true),
                make_entry(QStringLiteral("hero-show"), QStringLiteral("Hero Show"),
                           QStringLiteral("AIRING"), 3, aired_today, false),
                make_entry(QStringLiteral("future-show"), QStringLiteral("Future Show"),
                           QStringLiteral("AIRING"), 3, future_basis, false),
            };
            const auto iso_now = now.toString(Qt::ISODateWithMs);
            const QJsonObject body{
                {QStringLiteral("data"), data},
                {QStringLiteral("meta"), QJsonObject{{QStringLiteral("fetchedAt"), iso_now},
                                                     {QStringLiteral("nextRefreshAt"), iso_now},
                                                     {QStringLiteral("stale"), serve_stale}}},
            };
            return HttpTestServer::Response{.body =
                                                QJsonDocument(body).toJson(QJsonDocument::Compact)};
        });

        LibraryModel model(service, downloads, api);

        // Featured enriched keys.
        const auto featured = model.featured();
        QCOMPARE(featured.value(QStringLiteral("slug")).toString(), QStringLiteral("hero-show"));
        QCOMPARE(featured.value(QStringLiteral("episodeLabel")).toString(),
                 QStringLiteral("Episodio 2"));
        QCOMPARE(featured.value(QStringLiteral("unwatchedOnDisk")).toInt(), 2);
        QCOMPARE(featured.value(QStringLiteral("statusText")).toString(),
                 QStringLiteral("En emisión"));
        QCOMPARE(featured.value(QStringLiteral("genres")).toString(),
                 QStringLiteral("Acción · Aventura · Fantasía"));
        QVERIFY(featured.value(QStringLiteral("hasBackdrop")).toBool());
        QCOMPARE(featured.value(QStringLiteral("remainingText")).toString(),
                 QStringLiteral("Quedan 14 min"));

        // Window shown controls clock_timer_.
        QVERIFY(model.windowShown());
        QVERIFY(!model.clockActive());
        QVERIFY(!model.scheduleLoaded());
        QVERIFY(model.todayHeading().startsWith(QStringLiteral("Hoy · ")));
        QCOMPARE(model.todayWeekday(), local_now.date().dayOfWeek() - 1);
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{2}:\\d{2}$"))
                    .match(model.localTimeText())
                    .hasMatch());
        model.start();
        QVERIFY(model.clockActive());
        QTRY_VERIFY(model.scheduleLoaded());
        // Home right rail shows strictly today's followed entries (not future-show).
        QCOMPARE(model.upcomingCount(), 2);
        QVERIFY(!model.scheduleStale());

        const auto upcoming = model.upcoming();
        QCOMPARE(upcoming.size(), 2);
        const auto days = model.upcomingDays();
        QCOMPARE(days.size(), 1);
        const auto today_group = days.at(0).toMap();
        QCOMPARE(today_group.value(QStringLiteral("day")).toString(), QStringLiteral("Hoy"));
        QVERIFY(today_group.value(QStringLiteral("isToday")).toBool());
        const auto today_items = today_group.value(QStringLiteral("items")).toList();
        QCOMPARE(today_items.size(), 2);

        QVariantMap finale_item;
        QVariantMap aired_item;
        for (const auto& entry : upcoming) {
            const auto map = entry.toMap();
            QCOMPARE(map.value(QStringLiteral("day")).toString(), QStringLiteral("Hoy"));
            if (map.value(QStringLiteral("slug")).toString() == u"finale-show") {
                finale_item = map;
            } else if (map.value(QStringLiteral("slug")).toString() == u"hero-show") {
                aired_item = map;
            }
        }
        QCOMPARE(finale_item.value(QStringLiteral("state")).toString(), QStringLiteral("finale"));
        QCOMPARE(finale_item.value(QStringLiteral("stateLabel")).toString(),
                 QStringLiteral("Final"));
        QCOMPARE(finale_item.value(QStringLiteral("badgeText")).toString(),
                 QStringLiteral("Final"));
        QCOMPARE(finale_item.value(QStringLiteral("episode")).toString(), QStringLiteral("12"));
        QCOMPARE(finale_item.value(QStringLiteral("episodeText")).toString(),
                 QStringLiteral("Ep. 12"));
        QVERIFY(!finale_item.value(QStringLiteral("time")).toString().isEmpty());
        QCOMPARE(aired_item.value(QStringLiteral("state")).toString(), QStringLiteral("aired"));
        QCOMPARE(aired_item.value(QStringLiteral("stateLabel")).toString(),
                 QStringLiteral("Emitido"));
        QCOMPARE(aired_item.value(QStringLiteral("badgeText")).toString(),
                 QStringLiteral("Emitido"));

        // Pausing and resuming clock_timer_ via setWindowShown.
        QSignalSpy shown_spy(&model, &LibraryModel::windowShownChanged);
        model.setWindowShown(false);
        QCOMPARE(shown_spy.size(), 1);
        QVERIFY(!model.windowShown());
        QVERIFY(!model.clockActive());
        model.setWindowShown(true);
        QCOMPARE(shown_spy.size(), 2);
        QVERIFY(model.windowShown());
        QVERIFY(model.clockActive());

        // Server stale flag propagates to scheduleStale even from Origin::Network (no cache).
        serve_stale = true;
        QTemporaryDir stale_cache;
        api::ApiClient stale_api(network, {.baseUrl = server.url(QStringLiteral("/api/v1/")),
                                           .cacheDirectory = stale_cache.path()});
        LibraryModel stale_model(service, downloads, stale_api);
        stale_model.start();
        QTRY_COMPARE(server.count("/api/v1/schedule"), 2);
        QTRY_VERIFY(stale_model.scheduleStale());
    }

    // Franchises collapse and re-expand under active filters (Watching, search, onDiskOnly) and
    // respond to expandAll()/collapseAll() with accurate hasGroups/allExpanded notifications.
    void franchiseCollapseUnderFiltersAndExpandCollapseAll() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });

        const auto add = [&](const QString& slug, const QString& title, bool watching,
                             bool on_disk) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = title;
            record.status = api::AnimeStatus::Airing;
            record.episodeCount = 12;
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            QList<api::Episode> episodes;
            for (int n = 1; n <= 3; ++n) {
                api::Episode ep;
                ep.id = slug + QString::number(n);
                ep.number = n;
                episodes.append(ep);
            }
            QVERIFY(repo.syncEpisodes(slug, episodes, false));
            if (watching) {
                QVERIFY(repo.setWatchState(slug, 1, library::WatchState::Watched));
            }
            if (on_disk) {
                QVERIFY(repo.setEpisodeFile(slug, 2,
                                            cache.filePath(slug + QStringLiteral("-2.mkv")), 10));
            }
        };

        add(QStringLiteral("bc-tv"), QStringLiteral("Black Clover"), true, true);
        add(QStringLiteral("bc-movie"), QStringLiteral("Black Clover: Movie"), true, false);
        add(QStringLiteral("op-tv"), QStringLiteral("One Piece"), true, true);
        add(QStringLiteral("solo"), QStringLiteral("Solo Show"), true, false);

        auto bc_group = repo.createFranchise(QStringLiteral("Black Clover"));
        QVERIFY(bc_group);
        QVERIFY(repo.setFranchiseMember(*bc_group, QStringLiteral("bc-tv"), true));
        QVERIFY(repo.setFranchiseMember(*bc_group, QStringLiteral("bc-movie"), true));

        auto op_group = repo.createFranchise(QStringLiteral("One Piece"));
        QVERIFY(op_group);
        QVERIFY(repo.setFranchiseMember(*op_group, QStringLiteral("op-tv"), true));

        LibraryModel model(service, downloads, api);
        QSignalSpy groups_spy(&model, &LibraryModel::groupsChanged);

        // Under Filter::All, groups start folded; expandAll/collapseAll toggle all groups.
        QVERIFY(model.hasGroups());
        QVERIFY(!model.allExpanded());
        QCOMPARE(model.rowCount(), 3); // 2 folded franchises + 1 standalone
        model.expandAll();
        QVERIFY(model.allExpanded());
        QCOMPARE(model.rowCount(), 6); // 2 groups + 3 members + 1 standalone
        QVERIFY(groups_spy.size() >= 1);
        model.collapseAll();
        QVERIFY(!model.allExpanded());
        QCOMPARE(model.rowCount(), 3);

        // Remember explicit expansion of One Piece under Todos before switching filters.
        model.toggleGroup(*op_group);
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(!model.allExpanded());

        // Under Filter::Watching, franchises start unfolded, and toggleGroup MUST collapse them.
        model.setFilter(LibraryModel::Filter::Watching);
        QVERIFY(model.hasGroups());
        QVERIFY(model.allExpanded());
        QCOMPARE(model.rowCount(), 6);

        const int bc_row = model.indexOfGroup(*bc_group);
        QVERIFY(bc_row >= 0);
        QVERIFY(model.data(model.index(bc_row), LibraryModel::ExpandedRole).toBool());

        // Clicking Black Clover's chevron collapses its 2 members under Watching.
        model.toggleGroup(*bc_group);
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(!model.allExpanded());
        QVERIFY(!model.data(model.index(model.indexOfGroup(*bc_group)), LibraryModel::ExpandedRole)
                     .toBool());

        // Clicking it again re-expands Black Clover.
        model.toggleGroup(*bc_group);
        QCOMPARE(model.rowCount(), 6);
        QVERIFY(model.allExpanded());

        // collapseAll / expandAll under Watching.
        model.collapseAll();
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(!model.allExpanded());
        model.expandAll();
        QCOMPARE(model.rowCount(), 6);
        QVERIFY(model.allExpanded());

        // Active search clears collapsed_filtered_ so matches unfold, yet chevron still folds.
        model.collapseAll();
        QCOMPARE(model.rowCount(), 3);
        model.setSearch(QStringLiteral("Black"));
        QCOMPARE(model.rowCount(), 3); // Black Clover group + 2 matching members
        QVERIFY(model.allExpanded());
        model.toggleGroup(*bc_group);
        QCOMPARE(model.rowCount(), 1); // collapsed down to just the franchise row
        QVERIFY(!model.allExpanded());
        model.toggleGroup(*bc_group);
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(model.allExpanded());

        // Clearing search while still under Watching resets collapsed_filtered_.
        model.setSearch(QString());
        QCOMPARE(model.rowCount(), 6);
        QVERIFY(model.allExpanded());

        // Returning to Filter::All preserves the Todos expansion state (One Piece expanded only).
        model.setFilter(LibraryModel::Filter::All);
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(!model.allExpanded());
        QVERIFY(!model.data(model.index(model.indexOfGroup(*bc_group)), LibraryModel::ExpandedRole)
                     .toBool());
        QVERIFY(model.data(model.index(model.indexOfGroup(*op_group)), LibraryModel::ExpandedRole)
                    .toBool());

        // Ungrouped mode clears hasGroups and allExpanded.
        model.setGrouped(false);
        QVERIFY(!model.hasGroups());
        QVERIFY(!model.allExpanded());
    }

    // Full catalog weekly schedule runs strictly Mon..Sun (0..6), derives the 4 decided states
    // with zero "Ahora", updates inLibrary immediately on archive/removal, handles stale, and
    // throttles refreshSchedule().
    void todayOnlyAgendaAndFullWeeklySchedule() {
        // 1) Pure weekly derivation on a fixed Wednesday 2026-11-25 15:00 Europe/Madrid clock:
        //    covers all 4 states, 3h grace boundary (zero "Ahora"), and stale suppression.
        const QTimeZone madrid(QByteArrayLiteral("Europe/Madrid"));
        QVERIFY(madrid.isValid());
        const QDateTime fixed_now(QDate(2026, 11, 25), QTime(15, 0), madrid); // Wednesday (2)

        const auto slot_at = [&](api::AnimeStatus status, double number, QDate date, QTime time,
                                 bool is_final = false) {
            api::ScheduleEntry entry;
            entry.anime.status = status;
            entry.latestEpisode.number = number;
            entry.basisPublishedAt = QDateTime(date, time, madrid);
            entry.isFinalEpisode = is_final;
            return entry;
        };

        // Monday this week (aired -> Emitido).
        const auto mon_aired = app::schedule::weekly(
            slot_at(api::AnimeStatus::Airing, 8, QDate(2026, 11, 23), QTime(11, 0)), fixed_now);
        QVERIFY(mon_aired);
        QCOMPARE(mon_aired->weekday, 0);
        QCOMPARE(mon_aired->number, 8.0);
        QCOMPARE(mon_aired->state, QStringLiteral("aired"));
        QCOMPARE(mon_aired->stateLabel, QStringLiteral("Emitido"));
        QVERIFY(!mon_aired->isToday);

        // Wednesday today (finale -> Final).
        const auto wed_finale = app::schedule::weekly(
            slot_at(api::AnimeStatus::Finished, 12, QDate(2026, 11, 25), QTime(9, 30), true),
            fixed_now);
        QVERIFY(wed_finale);
        QCOMPARE(wed_finale->weekday, 2);
        QCOMPARE(wed_finale->slotMinutes, 9 * 60 + 30);
        QCOMPARE(wed_finale->time, QStringLiteral("09:30"));
        QCOMPARE(wed_finale->state, QStringLiteral("finale"));
        QCOMPARE(wed_finale->stateLabel, QStringLiteral("Final"));
        QVERIFY(wed_finale->isToday);

        // Wednesday last week at 12:01 -> 2h59m past slot (within 3h grace -> Próximo, NOT Ahora).
        const auto grace_entry =
            slot_at(api::AnimeStatus::Airing, 4, QDate(2026, 11, 18), QTime(12, 1));
        const auto wed_grace = app::schedule::weekly(grace_entry, fixed_now);
        QVERIFY(wed_grace);
        QCOMPARE(wed_grace->weekday, 2);
        QCOMPARE(wed_grace->number, 5.0);
        QCOMPARE(wed_grace->state, QStringLiteral("upcoming"));
        QCOMPARE(wed_grace->stateLabel, QStringLiteral("Próximo"));
        QVERIFY(wed_grace->isToday);

        // Wednesday last week at 12:00 -> exactly +3h00m -> Retrasado.
        const auto late_entry =
            slot_at(api::AnimeStatus::Airing, 4, QDate(2026, 11, 18), QTime(12, 0));
        const auto wed_late = app::schedule::weekly(late_entry, fixed_now);
        QVERIFY(wed_late);
        QCOMPARE(wed_late->weekday, 2);
        QCOMPARE(wed_late->number, 5.0);
        QCOMPARE(wed_late->state, QStringLiteral("late"));
        QCOMPARE(wed_late->stateLabel, QStringLiteral("Retrasado"));

        // Sunday last week -> upcoming on Sunday (weekday 6).
        const auto sun_entry =
            slot_at(api::AnimeStatus::Airing, 6, QDate(2026, 11, 22), QTime(21, 15));
        const auto sun_upcoming = app::schedule::weekly(sun_entry, fixed_now);
        QVERIFY(sun_upcoming);
        QCOMPARE(sun_upcoming->weekday, 6);
        QCOMPARE(sun_upcoming->number, 7.0);
        QCOMPARE(sun_upcoming->state, QStringLiteral("upcoming"));
        QCOMPARE(sun_upcoming->stateLabel, QStringLiteral("Próximo"));

        // Stale suppresses predicted status on un-aired slots, while keeping aired/finale intact.
        const auto stale_late = app::schedule::weekly(late_entry, fixed_now, true);
        QVERIFY(stale_late);
        QCOMPARE(stale_late->state, QString());
        QCOMPARE(stale_late->stateLabel, QString());
        const auto stale_aired = app::schedule::weekly(
            slot_at(api::AnimeStatus::Airing, 8, QDate(2026, 11, 23), QTime(11, 0)), fixed_now,
            true);
        QVERIFY(stale_aired);
        QCOMPARE(stale_aired->state, QStringLiteral("aired"));
        QCOMPARE(stale_aired->stateLabel, QStringLiteral("Emitido"));

        // 2) Full LibraryModel integration: Hoy-only rail vs full Mon–Sun catalog weeklySchedule.
        HttpTestServer server;
        QVERIFY(server.listen());
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = server.url(QStringLiteral("/api/v1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });

        const auto add_lib = [&](const QString& slug, const QString& title, bool archived) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = title;
            record.status = api::AnimeStatus::Airing;
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            if (archived) {
                QVERIFY(repo.setArchived(slug, true));
            }
        };
        add_lib(QStringLiteral("followed-today"), QStringLiteral("Followed Today"), false);
        add_lib(QStringLiteral("followed-other"), QStringLiteral("Followed Other"), false);
        add_lib(QStringLiteral("archived-today"), QStringLiteral("Archived Today"), true);

        const auto now = QDateTime::currentDateTimeUtc();
        const auto local_now = now.toLocalTime();
        const int today_w = local_now.date().dayOfWeek() - 1;
        const QDate week_start = local_now.date().addDays(-today_w);
        const auto aired_today =
            local_now.time() > QTime(0, 1)
                ? QDateTime(local_now.date(), QTime(0, 0, 30), local_now.timeZone()).toUTC()
                : now;
        const auto last_week_day = [&](int w, QTime time) {
            return QDateTime(week_start.addDays(w - 7), time, local_now.timeZone()).toUTC();
        };
        const auto future_basis = now.addDays(-5); // +7d = 2 days ahead, never "Hoy"

        const auto make_entry = [](const QString& slug, const QString& title, const QString& status,
                                   int number, const QDateTime& basis, bool is_final = false) {
            const auto iso = basis.toUTC().toString(Qt::ISODateWithMs);
            return QJsonObject{
                {QStringLiteral("anime"),
                 QJsonObject{{QStringLiteral("id"), slug},
                             {QStringLiteral("slug"), slug},
                             {QStringLiteral("title"), title},
                             {QStringLiteral("posterUrl"),
                              QStringLiteral("https://img.test/") + slug + QStringLiteral(".webp")},
                             {QStringLiteral("backdropUrl"), QStringLiteral("https://img.test/") +
                                                                 slug + QStringLiteral("-bg.webp")},
                             {QStringLiteral("status"), status},
                             {QStringLiteral("mature"), false}}},
                {QStringLiteral("latestEpisode"),
                 QJsonObject{{QStringLiteral("id"), slug + QString::number(number)},
                             {QStringLiteral("number"), number},
                             {QStringLiteral("publishedAt"), iso}}},
                {QStringLiteral("basisPublishedAt"), iso},
                {QStringLiteral("isFinalEpisode"), is_final},
            };
        };

        // Intentionally shuffled Sun -> Fri -> Mon -> today so sorting is genuinely tested.
        server.route("GET", "/api/v1/schedule", [&](const HttpTestServer::Request&) {
            const QJsonArray data{
                make_entry(QStringLiteral("cat-sun"), QStringLiteral("Catalog Sunday"),
                           QStringLiteral("AIRING"), 6, last_week_day(6, QTime(20, 0))),
                make_entry(QStringLiteral("followed-other"), QStringLiteral("Followed Other"),
                           QStringLiteral("AIRING"), 3, future_basis),
                make_entry(QStringLiteral("cat-mon"), QStringLiteral("Catalog Monday"),
                           QStringLiteral("AIRING"), 2, last_week_day(0, QTime(10, 15))),
                make_entry(QStringLiteral("followed-today"), QStringLiteral("Followed Today"),
                           QStringLiteral("AIRING"), 9, aired_today),
                make_entry(QStringLiteral("archived-today"), QStringLiteral("Archived Today"),
                           QStringLiteral("AIRING"), 4, aired_today),
            };
            const auto iso_now = now.toString(Qt::ISODateWithMs);
            const QJsonObject body{
                {QStringLiteral("data"), data},
                {QStringLiteral("meta"), QJsonObject{{QStringLiteral("fetchedAt"), iso_now},
                                                     {QStringLiteral("nextRefreshAt"), iso_now},
                                                     {QStringLiteral("stale"), false}}},
            };
            return HttpTestServer::Response{.body =
                                                QJsonDocument(body).toJson(QJsonDocument::Compact)};
        });

        LibraryModel model(service, downloads, api);
        model.start();
        QTRY_VERIFY(model.scheduleLoaded());

        // Hoy rail includes ONLY followed, non-archived entries airing today.
        QCOMPARE(model.upcomingCount(), 1);
        QCOMPARE(model.upcoming().size(), 1);
        QCOMPARE(model.upcoming().first().toMap().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("followed-today"));

        // Weekly schedule includes ALL 5 catalog entries in strict Mon..Sun (0..6) order.
        const auto weekly = model.weeklySchedule();
        QCOMPARE(weekly.size(), 5);
        int prev_weekday = -1;
        int prev_minutes = -1;
        QHash<QString, QVariantMap> by_slug;
        for (const auto& entry : weekly) {
            const auto map = entry.toMap();
            const int w = map.value(QStringLiteral("weekday")).toInt();
            const int m = map.value(QStringLiteral("slotMinutes")).toInt();
            QVERIFY(w >= 0 && w <= 6);
            QVERIFY(w > prev_weekday || (w == prev_weekday && m >= prev_minutes));
            prev_weekday = w;
            prev_minutes = m;
            QVERIFY(map.value(QStringLiteral("stateLabel")).toString() != u"Ahora");
            QCOMPARE(map.value(QStringLiteral("isToday")).toBool(), w == model.todayWeekday());
            by_slug.insert(map.value(QStringLiteral("slug")).toString(), map);
        }
        // Strict Mon..Sun: Monday (0) is before Sunday (6) regardless of which day Today is.
        QCOMPARE(weekly.first().toMap().value(QStringLiteral("weekday")).toInt(), 0);
        QCOMPARE(weekly.last().toMap().value(QStringLiteral("weekday")).toInt(), 6);

        QVERIFY(by_slug.value(QStringLiteral("followed-today"))
                    .value(QStringLiteral("inLibrary"))
                    .toBool());
        QVERIFY(by_slug.value(QStringLiteral("followed-other"))
                    .value(QStringLiteral("inLibrary"))
                    .toBool());
        QVERIFY(!by_slug.value(QStringLiteral("archived-today"))
                     .value(QStringLiteral("inLibrary"))
                     .toBool());
        QVERIFY(
            !by_slug.value(QStringLiteral("cat-mon")).value(QStringLiteral("inLibrary")).toBool());
        QVERIFY(
            !by_slug.value(QStringLiteral("cat-sun")).value(QStringLiteral("inLibrary")).toBool());
        QCOMPARE(by_slug.value(QStringLiteral("cat-mon")).value(QStringLiteral("time")).toString(),
                 QStringLiteral("10:15"));
        QCOMPARE(
            by_slug.value(QStringLiteral("cat-mon")).value(QStringLiteral("posterUrl")).toString(),
            QStringLiteral("https://img.test/cat-mon.webp"));
        QCOMPARE(by_slug.value(QStringLiteral("cat-mon"))
                     .value(QStringLiteral("backdropUrl"))
                     .toString(),
                 QStringLiteral("https://img.test/cat-mon-bg.webp"));

        // Archiving a library entry immediately flips inLibrary in weeklySchedule and drops Hoy.
        QSignalSpy schedule_spy(&model, &LibraryModel::scheduleChanged);
        QVERIFY(model.archive({QStringLiteral("followed-today")}, true));
        QVERIFY(schedule_spy.size() >= 1);
        QCOMPARE(model.upcomingCount(), 0);
        for (const auto& entry : model.weeklySchedule()) {
            const auto map = entry.toMap();
            if (map.value(QStringLiteral("slug")).toString() == u"followed-today") {
                QVERIFY(!map.value(QStringLiteral("inLibrary")).toBool());
            }
        }

        // refreshSchedule() throttles rapid tab switches within 30s.
        QCOMPARE(server.count("/api/v1/schedule"), 1);
        model.refreshSchedule();
        QTest::qWait(50);
        QCOMPARE(server.count("/api/v1/schedule"), 1);
    }

    void spanishPluralsSingleEpisodeFilmAndFeaturedRemaining() {
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        QVERIFY(opened);
        auto db = std::move(*opened);
        library::LibraryRepository repo(*db);
        downloads::DownloadRepository queue(*db);
        QNetworkAccessManager network;
        QTemporaryDir cache;
        api::ApiClient api(network, {.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1/")),
                                     .cacheDirectory = cache.path()});
        library::LibraryService service(repo, api);
        downloads::Aria2 aria(network, QString(), QByteArray("test"));
        downloads::mega::Api mega(network);
        downloads::pixeldrain::Api pixeldrain(network);
        downloads::DownloadManager downloads({api, aria, mega, pixeldrain, queue, repo},
                                             [] { return downloads::DownloadSettings{}; });

        const auto add_anime = [&](const QString& slug, const QString& title, int count,
                                   const QString& category) {
            library::AnimeRecord record;
            record.slug = slug;
            record.title = title;
            record.category = category;
            record.status = api::AnimeStatus::Finished;
            record.episodeCount = count;
            QVERIFY(repo.saveSnapshot(record));
            QVERIFY(repo.setInLibrary(slug, true));
            QList<api::Episode> episodes;
            for (int number = 1; number <= count; ++number) {
                api::Episode episode;
                episode.id = slug + QString::number(number);
                episode.number = number;
                episodes.append(episode);
            }
            QVERIFY(repo.syncEpisodes(slug, episodes, false));
        };

        add_anime(QStringLiteral("one-disk"), QStringLiteral("Saga One"), 12,
                  QStringLiteral("TV Anime"));
        add_anime(QStringLiteral("two-disk"), QStringLiteral("Saga Two"), 12,
                  QStringLiteral("TV Anime"));
        add_anime(QStringLiteral("film"), QStringLiteral("Saga Film"), 1,
                  QStringLiteral("Película"));

        QVERIFY(repo.setEpisodeFile(QStringLiteral("one-disk"), 1,
                                    cache.filePath(QStringLiteral("o1.mkv")), 10));
        QVERIFY(repo.setWatchState(QStringLiteral("one-disk"), 1, library::WatchState::Watching,
                                   60'000, 120'000)); // 60 s left -> "Queda 1 min"

        QVERIFY(repo.setEpisodeFile(QStringLiteral("two-disk"), 1,
                                    cache.filePath(QStringLiteral("t1.mkv")), 10));
        QVERIFY(repo.setEpisodeFile(QStringLiteral("two-disk"), 2,
                                    cache.filePath(QStringLiteral("t2.mkv")), 10));

        QVERIFY(repo.setEpisodeFile(QStringLiteral("film"), 1,
                                    cache.filePath(QStringLiteral("f1.mkv")), 10));
        QVERIFY(repo.setWatchState(QStringLiteral("film"), 1, library::WatchState::Watching,
                                   100'000, 3'700'000)); // 3600 s left -> "Queda 1 h"

        const auto group = repo.createFranchise(QStringLiteral("Saga"), QStringLiteral("one-disk"));
        QVERIFY(group);
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("two-disk"), true));
        QVERIFY(repo.setFranchiseMember(*group, QStringLiteral("film"), true));

        LibraryModel model(service, downloads, api);
        model.setGrouped(false);
        model.openFranchise(*group);

        QHash<QString, QVariantMap> by_slug;
        for (const auto& item : model.franchise().value(QStringLiteral("works")).toList()) {
            const auto map = item.toMap();
            by_slug.insert(map.value(QStringLiteral("slug")).toString(), map);
        }
        QCOMPARE(
            by_slug.value(QStringLiteral("one-disk")).value(QStringLiteral("diskText")).toString(),
            QStringLiteral("1 episodio en disco"));
        QCOMPARE(
            by_slug.value(QStringLiteral("two-disk")).value(QStringLiteral("diskText")).toString(),
            QStringLiteral("2 episodios en disco"));
        QCOMPARE(by_slug.value(QStringLiteral("film")).value(QStringLiteral("diskText")).toString(),
                 QStringLiteral("En disco"));
        QCOMPARE(
            by_slug.value(QStringLiteral("one-disk")).value(QStringLiteral("state")).toString(),
            QStringLiteral("Continuar ep. 1"));
        QCOMPARE(
            by_slug.value(QStringLiteral("one-disk")).value(QStringLiteral("caption")).toString(),
            QStringLiteral("Continuar ep. 1"));
        QCOMPARE(by_slug.value(QStringLiteral("film")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("Continuar"));
        QCOMPARE(by_slug.value(QStringLiteral("film")).value(QStringLiteral("caption")).toString(),
                 QStringLiteral("Continuar"));

        // Featured remainingText handles singular "Queda 1 min" and exact "Queda 1 h" (no "0 min").
        // Film was touched most recently (and sorts first by title) so it leads featured without
        // "Episodio 1".
        QCOMPARE(model.featured().value(QStringLiteral("slug")).toString(), QStringLiteral("film"));
        QCOMPARE(model.featured().value(QStringLiteral("episodeLabel")).toString(), QString());
        QCOMPARE(model.featured().value(QStringLiteral("remainingText")).toString(),
                 QStringLiteral("Queda 1 h"));

        // Finish film so one-disk deterministically leads featured with 60 s left -> "Queda 1 min".
        QVERIFY(repo.setWatchState(QStringLiteral("film"), 1, library::WatchState::Watched));
        LibraryModel model_min(service, downloads, api);
        QCOMPARE(model_min.featured().value(QStringLiteral("slug")).toString(),
                 QStringLiteral("one-disk"));
        QCOMPARE(model_min.featured().value(QStringLiteral("remainingText")).toString(),
                 QStringLiteral("Queda 1 min"));

        // Update one-disk to 30 s left -> "Queda menos de un minuto".
        QVERIFY(repo.setWatchState(QStringLiteral("one-disk"), 1, library::WatchState::Watching,
                                   90'000, 120'000));
        LibraryModel model_sec(service, downloads, api);
        QCOMPARE(model_sec.featured().value(QStringLiteral("remainingText")).toString(),
                 QStringLiteral("Queda menos de un minuto"));
    }
};

QTEST_GUILESS_MAIN(LibraryModelTest)
#include "library_model_test.moc"
