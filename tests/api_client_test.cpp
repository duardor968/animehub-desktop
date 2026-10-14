#include "api/api_client.h"
#include "http_test_server.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace animehub::api;
using animehub::testing::HttpTestServer;

namespace {

QByteArray fixture(const char* name) {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/") + QLatin1StringView(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing fixture %s", name);
    }
    return file.readAll();
}

template <class T> struct Collected {
    QList<Result<T>> results;
    [[nodiscard]] ApiClient::Callback<T> callback() {
        return [this](const Result<T>& result) { results.append(result); };
    }
};

} // namespace

class ApiClientTest final : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<HttpTestServer> server_;
    std::unique_ptr<QTemporaryDir> cache_dir_;
    std::unique_ptr<QNetworkAccessManager> network_;
    std::unique_ptr<ApiClient> client_;

    void makeClient(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        client_ = std::make_unique<ApiClient>(
            *network_, ApiClient::Options{.baseUrl = server_->url(QStringLiteral("/api/v1/")),
                                          .cacheDirectory = cache_dir_->path(),
                                          .timeout = timeout});
    }

private slots:
    void init() {
        server_ = std::make_unique<HttpTestServer>();
        QVERIFY(server_->listen());
        cache_dir_ = std::make_unique<QTemporaryDir>();
        network_ = std::make_unique<QNetworkAccessManager>();
        makeClient();
    }

    void cleanup() {
        client_.reset();
        network_.reset();
        server_.reset();
        cache_dir_.reset();
    }

    void deliversCacheThenNetwork() {
        server_->json("/api/v1/anime/sousou-no-frieren", fixture("anime.json"));
        Collected<AnimeDetail> first;
        client_->anime(QStringLiteral("sousou-no-frieren"), this, first.callback());
        QTRY_COMPARE(first.results.size(), 1);
        QVERIFY(first.results[0]);
        QCOMPARE(first.results[0]->origin, Origin::Network);

        Collected<AnimeDetail> second;
        client_->anime(QStringLiteral("sousou-no-frieren"), this, second.callback());
        QCOMPARE(second.results.size(), 1); // the saved copy arrives synchronously
        QCOMPARE(second.results[0]->origin, Origin::Cache);
        QTRY_COMPARE(second.results.size(), 2);
        QCOMPARE(second.results[1]->origin, Origin::Network);
        QCOMPARE(second.results[1]->value.title, QStringLiteral("Sousou no Frieren"));
    }

    void fallsBackToTheSavedCopyWhenTheServerFails() {
        server_->json("/api/v1/schedule", fixture("schedule.json"));
        Collected<QList<ScheduleEntry>> warm;
        client_->schedule(this, warm.callback());
        QTRY_COMPARE(warm.results.size(), 1);

        server_->json("/api/v1/schedule", "{\"title\":\"Down\"}", 500);
        Collected<QList<ScheduleEntry>> cold;
        client_->schedule(this, cold.callback());
        QTRY_COMPARE(cold.results.size(), 1);
        QVERIFY(cold.results[0]);
        QCOMPARE(cold.results[0]->origin, Origin::Cache);
        QVERIFY(cold.results[0]->savedAt.isValid());
    }

    void reportsOfflineWhenNothingIsSaved() {
        const auto base = server_->url(QStringLiteral("/api/v1/"));
        server_.reset(); // nothing listens on that port any more
        client_ = std::make_unique<ApiClient>(
            *network_, ApiClient::Options{.baseUrl = base,
                                          .cacheDirectory = cache_dir_->path(),
                                          .timeout = std::chrono::seconds(5)});
        Collected<Home> home;
        client_->home(this, home.callback());
        QTRY_COMPARE(home.results.size(), 1);
        QVERIFY(!home.results[0]);
        QCOMPARE(home.results[0].error().kind, ApiError::Kind::Offline);
    }

    void mapsHttpFailures() {
        server_->json("/api/v1/anime/missing", fixture("notfound.json"), 404);
        Collected<AnimeDetail> missing;
        client_->anime(QStringLiteral("missing"), this, missing.callback(),
                       CachePolicy::NetworkOnly);
        QTRY_COMPARE(missing.results.size(), 1);
        QCOMPARE(missing.results[0].error().kind, ApiError::Kind::NotFound);
        QCOMPARE(missing.results[0].error().message, QStringLiteral("Anime not found."));

        server_->route("GET", "/api/v1/home", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.status = 429,
                                            .body = "{\"title\":\"Too Many Requests\"}",
                                            .headers = {{"Retry-After", "12"}}};
        });
        Collected<Home> limited;
        client_->home(this, limited.callback(), CachePolicy::NetworkOnly);
        QTRY_COMPARE(limited.results.size(), 1);
        QCOMPARE(limited.results[0].error().kind, ApiError::Kind::RateLimited);
        QCOMPARE(limited.results[0].error().retryAfter, std::chrono::seconds(12));
    }

    void reportsContractViolations() {
        server_->json("/api/v1/home", "{\"data\":{}}");
        Collected<Home> home;
        client_->home(this, home.callback(), CachePolicy::NetworkOnly);
        QTRY_COMPARE(home.results.size(), 1);
        QCOMPARE(home.results[0].error().kind, ApiError::Kind::Contract);
        QVERIFY(home.results[0].error().message.startsWith(u"data.featured"));
    }

    void timesOut() {
        makeClient(std::chrono::milliseconds(300));
        server_->route("GET", "/api/v1/home", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.body = "{}", .delayMs = 3000};
        });
        Collected<Home> home;
        client_->home(this, home.callback(), CachePolicy::NetworkOnly);
        QTRY_COMPARE_WITH_TIMEOUT(home.results.size(), 1, 2000);
        QCOMPARE(home.results[0].error().kind, ApiError::Kind::Timeout);
    }

    void postsResolveRequests() {
        server_->route("POST", "/api/v1/anime/frieren/downloads/resolve",
                       [](const HttpTestServer::Request&) {
                           return HttpTestServer::Response{
                               .body = R"({"data":{"packageName":"Frieren","episodes":[]}})"};
                       });
        Collected<Resolution> resolution;
        client_->resolve(QStringLiteral("frieren"),
                         {.episodeNumbers = {3}, .providers = {Provider::Mega}}, this,
                         resolution.callback());
        QTRY_COMPARE(resolution.results.size(), 1);
        QVERIFY(resolution.results[0]);
        const auto& sent = server_->requests().last();
        QCOMPARE(sent.headers.value("content-type"), QByteArray("application/json"));
        const auto body = QJsonDocument::fromJson(sent.body).object();
        QCOMPARE(body[u"providers"].toArray().at(0).toString(), QStringLiteral("MEGA"));
    }

    void refusesUnsafeSlugsWithoutTouchingTheNetwork() {
        Collected<AnimeDetail> anime;
        client_->anime(QStringLiteral("../../admin"), this, anime.callback());
        QCOMPARE(anime.results.size(), 1);
        QCOMPARE(anime.results[0].error().kind, ApiError::Kind::NotFound);
        QCOMPARE(server_->requests().size(), 0);
    }

    void dropsCallbacksOfADeadContext() {
        server_->route("GET", "/api/v1/home", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.body = "{}", .delayMs = 200};
        });
        int calls = 0;
        {
            QObject context;
            client_->home(
                &context, [&](const Result<Home>&) { ++calls; }, CachePolicy::NetworkOnly);
        }
        QTest::qWait(600);
        QCOMPARE(calls, 0);
    }

    void buildsCatalogQueries() {
        const auto url =
            ApiClient::catalogUrl(QUrl(QStringLiteral("https://api.test/api/v1/")),
                                  {.search = QStringLiteral("  frieren   beyond "),
                                   .genres = {QStringLiteral("drama"), QStringLiteral("fantasia")},
                                   .status = AnimeStatus::Airing,
                                   .minYear = 2020,
                                   .order = CatalogOrder::Score,
                                   .page = 900});
        QCOMPARE(url.path(), QStringLiteral("/api/v1/catalog"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("search")), QStringLiteral("frieren beyond"));
        QCOMPARE(query.allQueryItemValues(QStringLiteral("genre")).size(), 2);
        QCOMPARE(query.queryItemValue(QStringLiteral("status")), QStringLiteral("emision"));
        QCOMPARE(query.queryItemValue(QStringLiteral("order")), QStringLiteral("score"));
        QCOMPARE(query.queryItemValue(QStringLiteral("page")), QStringLiteral("500"));
        QVERIFY(!query.hasQueryItem(QStringLiteral("maxYear")));
    }
};

QTEST_GUILESS_MAIN(ApiClientTest)
#include "api_client_test.moc"
