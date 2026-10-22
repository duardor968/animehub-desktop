#include "http_test_server.h"
#include "remote/remote_service.h"
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
using namespace animehub::remote;
using animehub::testing::HttpTestServer;
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
        RemoteService service(network, server.url(QStringLiteral("/api/v1/")), dir.path(), {});
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
};
QTEST_GUILESS_MAIN(RemoteServiceTest)
#include "remote_service_test.moc"
