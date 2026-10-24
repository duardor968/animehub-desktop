#include "app/catalog_view.h"
#include "http_test_server.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using animehub::testing::HttpTestServer;

namespace {

QByteArray fixture(const char* name) {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/") + QLatin1StringView(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", name);
    }
    return file.readAll();
}

// The catalog page over a local API and an in-memory library with one anime of the catalog.
struct Fixture {
    HttpTestServer server;
    QTemporaryDir cache;
    std::unique_ptr<storage::Database> db;
    std::unique_ptr<library::LibraryRepository> repo;
    std::unique_ptr<downloads::DownloadRepository> queue;
    QNetworkAccessManager network;
    std::unique_ptr<api::ApiClient> api;
    std::unique_ptr<library::LibraryService> service;
    std::unique_ptr<downloads::Aria2> aria;
    std::unique_ptr<downloads::mega::Api> mega;
    std::unique_ptr<downloads::pixeldrain::Api> pixeldrain;
    std::unique_ptr<downloads::DownloadManager> manager;
    std::unique_ptr<app::LibraryModel> library;

    bool open(const QString& owned) {
        if (!server.listen()) {
            return false;
        }
        server.json("/api/v1/home", fixture("home.json"));
        server.json("/api/v1/catalog", fixture("catalog.json"));
        auto opened = storage::Database::open(QStringLiteral(":memory:"));
        if (!opened) {
            return false;
        }
        db = std::move(*opened);
        repo = std::make_unique<library::LibraryRepository>(*db);
        queue = std::make_unique<downloads::DownloadRepository>(*db);
        api = std::make_unique<api::ApiClient>(
            network, api::ApiClient::Options{.baseUrl = server.url(QStringLiteral("/api/v1/")),
                                             .cacheDirectory = cache.path()});
        service = std::make_unique<library::LibraryService>(*repo, *api);
        aria = std::make_unique<downloads::Aria2>(network, QString(), QByteArray("test"));
        mega = std::make_unique<downloads::mega::Api>(network);
        pixeldrain = std::make_unique<downloads::pixeldrain::Api>(network);
        manager = std::make_unique<downloads::DownloadManager>(
            downloads::DownloadManager::Services{*api, *aria, *mega, *pixeldrain, *queue, *repo},
            [] { return downloads::DownloadSettings{}; });
        library::AnimeRecord record;
        record.slug = owned;
        record.title = owned;
        if (!repo->saveSnapshot(record) || !repo->setInLibrary(owned, true)) {
            return false;
        }
        library = std::make_unique<app::LibraryModel>(*service, *manager, *api);
        return true;
    }
};

} // namespace

class CatalogViewTest final : public QObject {
    Q_OBJECT

private slots:
    void agoIsCutDown_data() {
        QTest::addColumn<QDateTime>("when");
        QTest::addColumn<QString>("expected");
        const QDateTime now(QDate(2026, 10, 24), QTime(12, 0));
        QTest::newRow("seconds") << now.addSecs(-30) << QStringLiteral("ahora");
        QTest::newRow("minutes") << now.addSecs(-29 * 60 - 59) << QStringLiteral("hace 29 min");
        QTest::newRow("hours") << now.addSecs(-(3 * 3600 + 59 * 60)) << QStringLiteral("hace 3 h");
        QTest::newRow("last night")
            << QDateTime(QDate(2026, 10, 23), QTime(20, 0)) << QStringLiteral("hace 16 h");
        QTest::newRow("yesterday")
            << QDateTime(QDate(2026, 10, 23), QTime(8, 0)) << QStringLiteral("ayer");
        QTest::newRow("days") << QDateTime(QDate(2026, 10, 20), QTime(12, 0))
                              << QStringLiteral("hace 4 días");
        QTest::newRow("this year")
            << QDateTime(QDate(2026, 10, 2), QTime(12, 0)) << QStringLiteral("2 oct");
        QTest::newRow("last year")
            << QDateTime(QDate(2025, 12, 1), QTime(12, 0)) << QStringLiteral("1 dic 2025");
    }
    void agoIsCutDown() {
        QFETCH(QDateTime, when);
        QFETCH(QString, expected);
        QCOMPARE(app::CatalogView::ago(when, QDateTime(QDate(2026, 10, 24), QTime(12, 0))),
                 expected);
    }

    // "TV · 2026" for a card; a recent episode, "Ep. 4 · hace 3 h"; never the library in words.
    void captionsAreOneLineOfTheSameShape() {
        api::AnimeSummary anime;
        anime.category = api::Category{.id = QStringLiteral("1"),
                                       .name = QStringLiteral("TV Anime"),
                                       .slug = QStringLiteral("tv-anime")};
        anime.startDate = QDate(2026, 10, 6);
        QCOMPARE(app::CatalogView::captionOf(anime), QStringLiteral("TV · 2026"));
        anime.category->name = QStringLiteral("ONA");
        QCOMPARE(app::CatalogView::captionOf(anime), QStringLiteral("ONA · 2026"));

        const auto now = QDateTime::currentDateTimeUtc();
        api::Episode episode;
        episode.number = 4;
        QCOMPARE(app::CatalogView::captionOf(episode, now), QStringLiteral("Ep. 4"));
        episode.publishedAt = now.addSecs(-3 * 3600 - 10);
        QCOMPARE(app::CatalogView::captionOf(episode, now), QStringLiteral("Ep. 4 · hace 3 h"));
    }

    // The order is the whole catalog's: it keeps the recent episodes. The library shows as a
    // mark on the poster, not in the caption.
    void orderKeepsTheRecentEpisodesAndOwnedIsARole() {
        Fixture f;
        const auto owned = QStringLiteral("battle-spirits-re-zekkai-no-kuu");
        QVERIFY(f.open(owned));
        app::CatalogView view(*f.api, *f.library);
        view.start();
        QTRY_VERIFY(view.results()->rowCount() > 0);
        QTRY_VERIFY(view.recentEpisodes()->rowCount() > 0);
        QVERIFY(!view.homeLoading());
        QVERIFY(view.recentEpisodes()->cards().first().caption.startsWith(QStringLiteral("Ep. ")));

        auto* results = view.results();
        const auto role = results->roleNames().key("owned");
        QVERIFY(role > 0);
        int marked = 0;
        for (int row = 0; row < results->rowCount(); ++row) {
            const auto index = results->index(row);
            const auto slug = results->data(index, app::CardModel::SlugRole).toString();
            QCOMPARE(results->data(index, role).toBool(), slug == owned);
            QVERIFY(!results->data(index, app::CardModel::CaptionRole)
                         .toString()
                         .contains(QStringLiteral("biblioteca")));
            marked += slug == owned ? 1 : 0;
        }
        QCOMPARE(marked, 1);

        view.setOrder(static_cast<int>(api::CatalogOrder::Score));
        QVERIFY(!view.browsing());
        view.setGenre(QStringLiteral("accion"));
        QVERIFY(view.browsing());
    }

    // A refresh that fails leaves what was on view, dimmed, with the reason above it.
    void aFailedRefreshKeepsTheGrid() {
        Fixture f;
        QVERIFY(f.open(QStringLiteral("someone")));
        app::CatalogView view(*f.api, *f.library);
        view.start();
        QTRY_VERIFY(!view.loading() && view.results()->rowCount() > 0);
        const auto before = view.results()->rowCount();
        f.server.route("GET", "/api/v1/catalog", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.status = 500, .body = "{}"};
        });
        view.setGenre(QStringLiteral("drama"));
        QVERIFY(view.refreshing());
        QTRY_VERIFY(!view.loading());
        QVERIFY(!view.problem().isEmpty());
        QVERIFY(!view.problemBelow());
        QVERIFY(view.refreshing());
        QCOMPARE(view.results()->rowCount(), before);
    }
};

QTEST_GUILESS_MAIN(CatalogViewTest)
#include "catalog_view_test.moc"
