#include "api/api_client.h"
#include "http_test_server.h"
#include "library/library_service.h"
#include "remote/remote_service.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
using namespace animehub::remote;
using namespace animehub;
using animehub::testing::HttpTestServer;

namespace {

HttpTestServer::Response json(const QJsonObject& value) {
    return {.body = QJsonDocument(value).toJson(QJsonDocument::Compact)};
}

QJsonObject journalEntry(const QString& directory, const QString& id) {
    QFile file(QDir(directory).filePath(QStringLiteral("remote-journal.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll())
        .object()
        .value(QStringLiteral("requests"))
        .toObject()
        .value(id)
        .toObject();
}

// The device side of the API for one pending request, "request-1", for "frieren" episodes 1
// and 2 on profile "library" revision 1. It keeps what the PC published and acknowledged.
struct FakeInbox {
    QString state = QStringLiteral("pending");
    QList<QJsonObject> published;
    QList<QJsonObject> acks;

    void install(HttpTestServer& server) {
        server.route("PUT", "/api/v1/remote/devices/me/profiles", [this](const auto& req) {
            const auto body = QJsonDocument::fromJson(req.body).object();
            published.append(body);
            return json({{QStringLiteral("revision"), body.value(QStringLiteral("revision"))}});
        });
        server.route("GET", "/api/v1/remote/devices/me/inbox", [this](const auto&) {
            return json({{QStringLiteral("requests"),
                          state == u"pending" ? QJsonArray{request()} : QJsonArray{}}});
        });
        server.route("GET", "/api/v1/remote/devices/me/inbox/request-1",
                     [this](const auto&) { return json(request()); });
        server.route("POST", "/api/v1/remote/devices/me/inbox/request-1/ack",
                     [this](const auto& req) {
                         const auto ack = QJsonDocument::fromJson(req.body).object();
                         acks.append(ack);
                         state = ack.value(QStringLiteral("status")).toString();
                         return json(request());
                     });
    }

    [[nodiscard]] QJsonObject request() const {
        return {{QStringLiteral("id"), QStringLiteral("request-1")},
                {QStringLiteral("deviceId"), QStringLiteral("device-1")},
                {QStringLiteral("animeSlug"), QStringLiteral("frieren")},
                {QStringLiteral("episodeNumbers"), QJsonArray{1, 2}},
                {QStringLiteral("profileId"), QStringLiteral("library")},
                {QStringLiteral("profileRevision"), 1},
                {QStringLiteral("engine"), QStringLiteral("direct")},
                {QStringLiteral("status"), state}};
    }
};

Profile libraryProfile(const QString& destination) {
    return {QStringLiteral("library"),
            QStringLiteral("Biblioteca"),
            QStringLiteral("direct"),
            destination,
            {},
            {},
            true,
            QStringLiteral("sub")};
}

void signIn(RemoteService& service) {
    service.setCredentialStore(
        [] {
            return std::optional<Credentials>{
                {QStringLiteral("device-1"), QByteArray("test-secret")}};
        },
        [](const Credentials&) { return true; });
}

} // namespace

class RemoteServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void scopesCredentialsByInstallationAndApi() {
        QVERIFY(RemoteService::credentialTarget(QUrl(QStringLiteral("https://a/api/v1/")),
                                                QStringLiteral("C:/a")) !=
                RemoteService::credentialTarget(QUrl(QStringLiteral("https://a/api/v1/")),
                                                QStringLiteral("C:/b")));
        QVERIFY(RemoteService::credentialTarget(QUrl(QStringLiteral("https://a/api/v1/")),
                                                QStringLiteral("C:/a")) !=
                RemoteService::credentialTarget(QUrl(QStringLiteral("https://b/api/v1/")),
                                                QStringLiteral("C:/a")));
    }
    void noConsentNoNetwork() {
        HttpTestServer server;
        QVERIFY(server.listen());
        QNetworkAccessManager network;
        QTemporaryDir dir;
        RemoteService service(network, server.url(QStringLiteral("/api/v1/")), dir.path(), {}, {});
        service.setCredentialStore([] { return std::optional<Credentials>{}; },
                                   [](const Credentials&) { return false; });
        service.start();
        service.poll();
        QTest::qWait(30);
        QVERIFY(server.requests().isEmpty());
    }
    void durableHandshake_data() {
        QTest::addColumn<QString>("scenario");
        for (const auto* value : {"normal", "lost-ack", "cancel-race", "stale-profile",
                                  "enqueue-retry", "lost-ack-audio", "legacy-audio"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void durableHandshake() {
        QFETCH(QString, scenario);
        HttpTestServer server;
        QVERIFY(server.listen());
        QNetworkAccessManager network;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString state = QStringLiteral("pending");
        bool sawDurableBeforeAck = false;
        int enqueueCalls = 0;
        QString localAudio = QStringLiteral("dub");
        QStringList submittedAudio;
        QJsonObject request{{QStringLiteral("id"), QStringLiteral("request-1")},
                            {QStringLiteral("deviceId"), QStringLiteral("device-1")},
                            {QStringLiteral("animeSlug"), QStringLiteral("frieren")},
                            {QStringLiteral("episodeNumbers"), QJsonArray{1, 2}},
                            {QStringLiteral("profileId"), QStringLiteral("library")},
                            {QStringLiteral("profileRevision"),
                             scenario == QStringLiteral("stale-profile") ? 2 : 1},
                            {QStringLiteral("engine"), QStringLiteral("direct")},
                            {QStringLiteral("status"), state}};
        auto response = [](const QJsonObject& value) {
            return HttpTestServer::Response{
                .body = QJsonDocument(value).toJson(QJsonDocument::Compact)};
        };
        server.route("PUT", "/api/v1/remote/devices/me/profiles", [&](const auto& req) {
            const auto body = QJsonDocument::fromJson(req.body).object();
            const auto profile =
                body.value(QStringLiteral("profiles")).toArray().first().toObject();
            if (profile.contains(QStringLiteral("destination")) ||
                profile.contains(QStringLiteral("connection")) ||
                profile.contains(QStringLiteral("audio")))
                return HttpTestServer::Response{.status = 400};
            return response({{QStringLiteral("revision"), 1}});
        });
        server.route("GET", "/api/v1/remote/devices/me/inbox", [&](const auto&) {
            request.insert(QStringLiteral("status"), state);
            return response({{QStringLiteral("requests"), QJsonArray{request}}});
        });
        server.route("GET", "/api/v1/remote/devices/me/inbox/request-1", [&](const auto&) {
            request.insert(QStringLiteral("status"), state);
            return response(request);
        });
        server.route("POST", "/api/v1/remote/devices/me/inbox/request-1/ack", [&](const auto& req) {
            QFile journal(dir.filePath(QStringLiteral("remote-journal.json")));
            sawDurableBeforeAck =
                journal.open(QIODevice::ReadOnly) && journal.readAll().contains("held");
            const auto ack = QJsonDocument::fromJson(req.body).object();
            if (scenario == QStringLiteral("cancel-race")) {
                state = QStringLiteral("cancelled");
                return HttpTestServer::Response{.status = 409};
            }
            state = ack.value(QStringLiteral("status")).toString();
            request.insert(QStringLiteral("status"), state);
            if (scenario.startsWith(QStringLiteral("lost-ack")) ||
                scenario == QStringLiteral("legacy-audio"))
                return HttpTestServer::Response{.drop = true};
            return response(request);
        });
        auto make = [&] {
            auto service = std::make_unique<RemoteService>(
                network, server.url(QStringLiteral("/api/v1/")), dir.path(),
                [](const QString&, std::function<void(Readiness)> done) { done(Readiness::Ready); },
                [&](const Submission& job) {
                    ++enqueueCalls;
                    submittedAudio.append(job.profile.audio);
                    if (job.requestId != QStringLiteral("request-1") ||
                        job.profile.destination != dir.path() || job.episodes.size() != 2)
                        return false;
                    return scenario != QStringLiteral("enqueue-retry") || enqueueCalls > 1;
                });
            service->setCredentialStore(
                [] {
                    return std::optional<Credentials>{
                        {QStringLiteral("device-1"), QByteArray("test-secret")}};
                },
                [](const Credentials&) { return true; });
            service->setProfiles(1, {{QStringLiteral("library"),
                                      QStringLiteral("Biblioteca"),
                                      QStringLiteral("direct"),
                                      dir.path(),
                                      {},
                                      {},
                                      true,
                                      localAudio}});
            service->start();
            return service;
        };
        auto service = make();
        QTRY_VERIFY(server.count("/api/v1/remote/devices/me/inbox/request-1/ack") >= 1);
        QTRY_VERIFY(!service->busy());
        if (scenario == QStringLiteral("stale-profile")) {
            QCOMPARE(enqueueCalls, 0);
            QCOMPARE(state, QStringLiteral("requires_attention"));
            return;
        }
        QVERIFY(sawDurableBeforeAck);
        if (scenario == QStringLiteral("cancel-race")) {
            QCOMPARE(enqueueCalls, 0);
            service->poll();
            QTRY_VERIFY(!service->busy());
            QCOMPARE(enqueueCalls, 0);
            return;
        }
        if (scenario.startsWith(QStringLiteral("lost-ack")) ||
            scenario == QStringLiteral("legacy-audio"))
            QCOMPARE(enqueueCalls, 0);
        service.reset();
        localAudio = QStringLiteral("sub");
        if (scenario == QStringLiteral("legacy-audio")) {
            QFile file(dir.filePath(QStringLiteral("remote-journal.json")));
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto journal = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            auto requests = journal.value(QStringLiteral("requests")).toObject();
            auto entry = requests.value(QStringLiteral("request-1")).toObject();
            auto profile = entry.value(QStringLiteral("profile")).toObject();
            profile.remove(QStringLiteral("audio"));
            entry.insert(QStringLiteral("profile"), profile);
            requests.insert(QStringLiteral("request-1"), entry);
            journal.insert(QStringLiteral("requests"), requests);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(journal).toJson());
            file.close();
        }
        service = make();
        QTRY_VERIFY(!service->busy());
        if (scenario == QStringLiteral("legacy-audio")) {
            QCOMPARE(enqueueCalls, 0);
            QVERIFY(service->error().contains(QStringLiteral("idioma original")));
            return;
        }
        for (const auto& audio : submittedAudio)
            QCOMPARE(audio, QStringLiteral("dub"));
        QCOMPARE(enqueueCalls, scenario == QStringLiteral("enqueue-retry") ? 2 : 1);
        service->poll();
        QTRY_VERIFY(!service->busy());
        QCOMPARE(enqueueCalls, scenario == QStringLiteral("enqueue-retry") ? 2 : 1);
        for (const auto& req : server.requests())
            QCOMPARE(req.headers.value("authorization"), QByteArray("Bearer test-secret"));
    }

    // A request is accepted only once the PC can record it. Until then it stays held here and
    // pending there; one it can never record goes back for attention and is never queued.
    void acceptsOnlyWhatItCanRecord_data() {
        QTest::addColumn<bool>("recordable");
        QTest::newRow("later-ready") << true;
        QTest::newRow("never") << false;
    }

    void acceptsOnlyWhatItCanRecord() {
        QFETCH(bool, recordable);
        HttpTestServer server;
        QVERIFY(server.listen());
        FakeInbox inbox;
        inbox.install(server);
        QNetworkAccessManager network;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto answer = Readiness::Later;
        QStringList asked;
        int queued = 0;
        RemoteService service(
            network, server.url(QStringLiteral("/api/v1/")), dir.path(),
            [&](const QString& slug, std::function<void(Readiness)> done) {
                asked.append(slug);
                done(answer);
            },
            [&](const Submission&) {
                ++queued;
                return true;
            });
        signIn(service);
        service.setProfiles(1, {libraryProfile(dir.path())});
        service.start();
        QTRY_VERIFY(asked.size() == 1 && !service.busy());
        QCOMPARE(asked.first(), QStringLiteral("frieren"));
        QVERIFY(inbox.acks.isEmpty());
        QCOMPARE(inbox.state, QStringLiteral("pending"));
        const auto state = [&] {
            return journalEntry(dir.path(), QStringLiteral("request-1"))
                .value(QStringLiteral("state"))
                .toString();
        };
        QCOMPARE(state(), QStringLiteral("held"));

        answer = recordable ? Readiness::Ready : Readiness::Never;
        service.poll();
        QTRY_VERIFY(inbox.acks.size() == 1 && !service.busy());
        QCOMPARE(asked.size(), 2);
        const auto ack = inbox.acks.first();
        if (recordable) {
            QCOMPARE(ack.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
            QCOMPARE(queued, 1);
            QCOMPARE(state(), QStringLiteral("queued"));
            return;
        }
        QCOMPARE(ack.value(QStringLiteral("status")).toString(),
                 QStringLiteral("requires_attention"));
        QCOMPARE(ack.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("local_record_unavailable"));
        service.poll();
        QTRY_COMPARE(state(), QStringLiteral("requires_attention"));
        QTRY_VERIFY(!service.busy());
        QCOMPARE(queued, 0);
        QCOMPARE(inbox.acks.size(), 1);
    }

    // The profiles go to the API once per session and again only when they change; a change
    // goes at once.
    void publishesProfilesOnlyWhenTheyChange() {
        HttpTestServer server;
        QVERIFY(server.listen());
        FakeInbox inbox;
        inbox.state = QStringLiteral("cancelled"); // an empty inbox
        inbox.install(server);
        QNetworkAccessManager network;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RemoteService service(
            network, server.url(QStringLiteral("/api/v1/")), dir.path(),
            [](const QString&, std::function<void(Readiness)> done) { done(Readiness::Ready); },
            [](const Submission&) { return true; });
        signIn(service);
        service.setProfiles(1, {libraryProfile(dir.path())});
        service.start();
        const QByteArray look("/api/v1/remote/devices/me/inbox");
        QTRY_VERIFY(server.count(look) == 1 && !service.busy());
        service.poll();
        QTRY_VERIFY(server.count(look) == 2 && !service.busy());
        service.poll();
        QTRY_VERIFY(server.count(look) == 3 && !service.busy());
        QCOMPARE(inbox.published.size(), 1);

        const QList<Profile> changed{libraryProfile(dir.filePath(QStringLiteral("changed")))};
        service.setProfiles(2, changed);
        QTRY_VERIFY(server.count(look) == 4 && !service.busy());
        QCOMPARE(inbox.published.size(), 2);
        QCOMPARE(inbox.published.last().value(QStringLiteral("revision")).toInt(), 2);
        service.setProfiles(2, changed); // settings saved again, nothing new
        service.poll();
        QTRY_VERIFY(server.count(look) == 5 && !service.busy());
        QCOMPARE(inbox.published.size(), 2);
    }

    // The anime of a request this PC has never seen comes from the API and is remembered, not
    // added to the library; one the catalog does not have can never be recorded.
    void remembersTheAnimeOfARequest() {
        HttpTestServer server;
        QVERIFY(server.listen());
        const auto fixture = [](const char* name) {
            QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/api/") + QLatin1StringView(name));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        server.json("/api/v1/anime/sousou-no-frieren", fixture("anime.json"));
        server.json("/api/v1/anime/missing", fixture("notfound.json"), 404);
        server.route("GET", "/api/v1/anime/broken",
                     [](const auto&) { return HttpTestServer::Response{.status = 500}; });
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto opened = storage::Database::open(dir.filePath(QStringLiteral("library.sqlite3")));
        QVERIFY(opened);
        auto database = std::move(*opened);
        library::LibraryRepository repository(*database);
        QNetworkAccessManager network;
        api::ApiClient api(network, {.baseUrl = server.url(QStringLiteral("/api/v1/")),
                                     .cacheDirectory = dir.filePath(QStringLiteral("cache"))});
        library::LibraryService library(repository, api);
        QObject context;
        std::optional<Readiness> answer;
        const auto ask = [&](const QString& slug) {
            answer.reset();
            rememberAnime(api, library, slug, &context,
                          [&answer](Readiness readiness) { answer = readiness; });
        };

        ask(QStringLiteral("sousou-no-frieren"));
        QTRY_VERIFY(answer.has_value());
        QVERIFY(*answer == Readiness::Ready);
        const auto saved = repository.anime(QStringLiteral("sousou-no-frieren"));
        QVERIFY(saved && *saved);
        QCOMPARE((*saved)->title, QStringLiteral("Sousou no Frieren"));
        QVERIFY(!(*saved)->inLibrary);
        ask(QStringLiteral("sousou-no-frieren")); // known now: answered without the network
        QVERIFY(answer.has_value());
        QVERIFY(*answer == Readiness::Ready);
        QCOMPARE(server.count("/api/v1/anime/sousou-no-frieren"), 1);

        ask(QStringLiteral("missing"));
        QTRY_VERIFY(answer.has_value());
        QVERIFY(*answer == Readiness::Never);
        ask(QStringLiteral("broken"));
        QTRY_VERIFY(answer.has_value());
        QVERIFY(*answer == Readiness::Later);
        const auto missing = repository.anime(QStringLiteral("missing"));
        QVERIFY(missing && !*missing);
    }
};
QTEST_GUILESS_MAIN(RemoteServiceTest)
#include "remote_service_test.moc"
