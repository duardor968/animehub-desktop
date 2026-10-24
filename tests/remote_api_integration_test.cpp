#include "downloads/download_manager.h"
#include "http_test_server.h"
#include "remote/remote_service.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <memory>

using namespace animehub::remote;
using namespace animehub;
using namespace animehub::downloads;
using animehub::testing::HttpTestServer;

// Opt-in contract integration. The API must use a disposable database. No download engine starts,
// OS credential store, browser account, or graphical application is involved.
class RemoteApiIntegrationTest final : public QObject {
    Q_OBJECT
    const QUrl base_{QStringLiteral("http://127.0.0.1:18091/api/v1/remote/")};
    QNetworkAccessManager http_;
    struct Result {
        int status;
        QJsonObject body;
    };
    Result call(const QByteArray& method, const QString& path, const QJsonObject& body = {},
                const QByteArray& token = {}, const QByteArray& key = {}) {
        QNetworkRequest request(base_.resolved(QUrl(path)));
        request.setTransferTimeout(5000);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!token.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + token);
        if (!key.isEmpty())
            request.setRawHeader("Idempotency-Key", key);
        auto* reply = http_.sendCustomRequest(
            request, method, method == "GET" ? QByteArray{} : QJsonDocument(body).toJson());
        QEventLoop loop;
        QElapsedTimer elapsed;
        elapsed.start();
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        Result result{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                      QJsonDocument::fromJson(reply->readAll()).object()};
        qInfo().noquote() << method << path << result.status << elapsed.elapsed() << "ms";
        reply->deleteLater();
        return result;
    }
    static QJsonObject journal(const QString& directory) {
        QFile file(QDir(directory).filePath(QStringLiteral("remote-journal.json")));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }

private slots:
    void deleteTransport() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray wire;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this,
                    [&, socket] { wire.append(socket->readAll()); });
        });
        QNetworkAccessManager network;
        QTemporaryDir directory;
        RemoteService service(
            network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(server.serverPort())),
            directory.path(), {}, {});
        service.setCredentialStore(
            [] { return std::optional<Credentials>{{QStringLiteral("device"), "fake"}}; },
            [](const Credentials&) { return true; });
        service.start();
        service.revokeLink(QStringLiteral("browser"));
        QTRY_VERIFY_WITH_TIMEOUT(wire.contains("\r\n\r\n"), 2000);
        QTest::qWait(100);
        const auto headers = wire.left(wire.indexOf("\r\n\r\n")).toLower();
        QVERIFY(headers.startsWith("delete /api/v1/remote/devices/me/links/browser http/1.1"));
        QVERIFY(!headers.contains("content-type:"));
        QVERIFY(!headers.contains("content-length:") || headers.contains("content-length: 0"));
        QVERIFY(wire.endsWith("\r\n\r\n"));
    }
    void nativeServiceAgainstRealApi() {
        if (qEnvironmentVariable("ANIMEHUB_REMOTE_INTEGRATION") != QStringLiteral("1"))
            QSKIP("Requires explicitly enabled disposable API on loopback:18091");
        const auto root = qEnvironmentVariable("ANIMEHUB_REMOTE_TEST_ROOT");
        QVERIFY2(QDir(root).exists() && QDir::isAbsolutePath(root),
                 "Set ANIMEHUB_REMOTE_TEST_ROOT to the task evidence directory");
        QTemporaryDir directory(QDir(root).filePath(QStringLiteral("native-remote-XXXXXX")));
        QVERIFY(directory.isValid());
        HttpTestServer engineServer;
        QVERIFY(engineServer.listen());
        QNetworkAccessManager engineNetwork;
        api::ApiClient catalog(engineNetwork, {.baseUrl = engineServer.url()});
        mega::Api mega(engineNetwork, nullptr, engineServer.url());
        pixeldrain::Api pixeldrain(engineNetwork, nullptr, engineServer.url());
        Aria2 aria(engineNetwork, directory.filePath(QStringLiteral("never-started.exe")), "test");
        const auto databasePath = directory.filePath(QStringLiteral("queue.sqlite3"));
        auto opened = storage::Database::open(databasePath);
        QVERIFY(opened);
        auto database = std::move(*opened);
        auto library = std::make_unique<library::LibraryRepository>(*database);
        auto repository = std::make_unique<DownloadRepository>(*database);
        auto makeQueue = [&] {
            // Before start(), enqueue is durable but pump cannot resolve or start an engine.
            return std::make_unique<DownloadManager>(
                DownloadManager::Services{catalog, aria, mega, pixeldrain, *repository, *library},
                [&] { return DownloadSettings{.directory = directory.path()}; });
        };
        auto queue = makeQueue();
        HttpTestServer proxy;
        QVERIFY(proxy.listen());
        bool dropAck = false;
        bool sawHeld = false;
        int acceptedAcks = 0;
        auto route = [&](const QByteArray& method, const QString& path) {
            proxy.route(method, (QStringLiteral("/api/v1/remote/") + path).toUtf8(),
                        [&, path](const HttpTestServer::Request& request) {
                            if (path.endsWith(QStringLiteral("/ack"))) {
                                const auto id = path.section(u'/', -2, -2);
                                sawHeld = journal(directory.path())
                                              .value(QStringLiteral("requests"))
                                              .toObject()
                                              .value(id)
                                              .toObject()
                                              .value(QStringLiteral("state"))
                                              .toString() == QStringLiteral("held");
                            }
                            const auto result = call(request.method, path,
                                                     QJsonDocument::fromJson(request.body).object(),
                                                     request.headers.value("authorization").mid(7));
                            const bool ack = path.endsWith(QStringLiteral("/ack"));
                            if (ack && result.status == 200)
                                ++acceptedAcks;
                            return HttpTestServer::Response{.status = result.status,
                                                            .body =
                                                                QJsonDocument(result.body).toJson(),
                                                            .drop = ack && dropAck};
                        });
        };
        route("POST", QStringLiteral("devices"));
        route("PUT", QStringLiteral("devices/me/profiles"));
        route("POST", QStringLiteral("devices/me/pairing"));
        route("GET", QStringLiteral("devices/me/inbox"));
        std::optional<Credentials> credentials;
        QList<Submission> submissions;
        bool acceptedBeforeEnqueue = true;
        bool receiptSaved = true;
        int revision = 1;
        QString audio = QStringLiteral("dub");
        QString destination = directory.path();
        bool directApi = false;
        QNetworkAccessManager nativeNetwork;
        auto make = [&] {
            auto service = std::make_unique<RemoteService>(
                nativeNetwork,
                directApi ? QUrl(QStringLiteral("http://127.0.0.1:18091/api/v1/"))
                          : proxy.url(QStringLiteral("/api/v1/")),
                directory.path(),
                // The fixture anime is not in the catalog; its queue rows need no anime row.
                [](const QString&, std::function<void(Readiness)> done) { done(Readiness::Ready); },
                [&](const Submission& submission) {
                    const auto state =
                        call("GET", QStringLiteral("devices/me/inbox/") + submission.requestId, {},
                             credentials->token);
                    acceptedBeforeEnqueue &=
                        state.status == 200 &&
                        state.body.value(QStringLiteral("status")).toString() ==
                            QStringLiteral("accepted");
                    const DownloadSettings fixed{.directory = submission.profile.destination,
                                                 .audio = submission.profile.audio ==
                                                                  QStringLiteral("dub")
                                                              ? api::Audio::Dub
                                                              : api::Audio::Sub,
                                                 .engine = DownloadEngine::Native};
                    const auto batch = queue->enqueueMany(
                        submission.animeSlug, submission.episodes, QStringLiteral("Native fixture"),
                        TaskOrigin::Manual, submission.requestId, fixed);
                    const bool saved = batch.has_value();
                    receiptSaved &= saved;
                    if (saved)
                        submissions.append(submission);
                    return saved;
                });
            service->setCredentialStore([&] { return credentials; },
                                        [&](const Credentials& value) {
                                            credentials = value;
                                            return true;
                                        });
            service->setProfiles(revision, {{QStringLiteral("library"),
                                             QStringLiteral("Library"),
                                             QStringLiteral("direct"),
                                             destination,
                                             {},
                                             {},
                                             true,
                                             audio}});
            service->start();
            return service;
        };
        auto service = make();
        service->registerDevice(QStringLiteral("Isolated native integration"));
        QTRY_VERIFY_WITH_TIMEOUT(service->registered() && !service->busy(), 10000);
        QVERIFY(credentials.has_value());
        service->generatePairingCode();
        QTRY_VERIFY_WITH_TIMEOUT(!service->code().isEmpty() && !service->busy(), 10000);
        const auto browser = call("POST", QStringLiteral("sessions"));
        QCOMPARE(browser.status, 201);
        const auto token = browser.body.value(QStringLiteral("token")).toString().toUtf8();
        QVERIFY(!token.isEmpty());
        QCOMPARE(call("POST", QStringLiteral("links"), {{QStringLiteral("code"), service->code()}},
                      token)
                     .status,
                 201);
        const auto devices = call("GET", QStringLiteral("devices"), {}, token);
        QCOMPARE(devices.status, 200);
        const auto deviceList = devices.body.value(QStringLiteral("devices")).toArray();
        QCOMPARE(deviceList.size(), 1);
        const auto profileList =
            deviceList.first().toObject().value(QStringLiteral("profiles")).toArray();
        QCOMPARE(profileList.size(), 1);
        const auto advertised = profileList.first().toObject();
        QCOMPARE(advertised.value(QStringLiteral("engine")).toString(), QStringLiteral("direct"));
        QVERIFY(!advertised.contains(QStringLiteral("destination")));
        QVERIFY(!advertised.contains(QStringLiteral("audio")));
        QVERIFY(!advertised.contains(QStringLiteral("connection")));

        QJsonObject request{
            {QStringLiteral("deviceId"), credentials->id},
            {QStringLiteral("animeSlug"), QStringLiteral("native-contract-fixture")},
            {QStringLiteral("episodeNumbers"), QJsonArray{1, 2}},
            {QStringLiteral("profileId"), QStringLiteral("library")},
            {QStringLiteral("profileRevision"), 1},
            {QStringLiteral("engine"), QStringLiteral("direct")}};
        for (int scenario = 0; scenario < 2; ++scenario) {
            const QList<double> numbers = scenario == 0 ? QList<double>{1, 2} : QList<double>{3, 4};
            request.insert(QStringLiteral("episodeNumbers"), QJsonArray{numbers[0], numbers[1]});
            const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
            const auto created = call("POST", QStringLiteral("requests"), request, token, key);
            QCOMPARE(created.status, 201);
            const auto id = created.body.value(QStringLiteral("id")).toString();
            QVERIFY(!id.isEmpty());
            const auto replay = call("POST", QStringLiteral("requests"), request, token, key);
            QCOMPARE(replay.status, 201);
            QCOMPARE(replay.body.value(QStringLiteral("id")).toString(), id);
            auto different = request;
            different.insert(QStringLiteral("episodeNumbers"), QJsonArray{99});
            QCOMPARE(call("POST", QStringLiteral("requests"), different, token, key).status, 409);
            route("GET", QStringLiteral("devices/me/inbox/") + id);
            route("POST", QStringLiteral("devices/me/inbox/") + id + QStringLiteral("/ack"));
            dropAck = scenario == 1;
            const auto before = acceptedAcks;
            service->poll();
            QTRY_VERIFY_WITH_TIMEOUT(acceptedAcks > before && !service->busy(), 10000);
            QVERIFY(sawHeld);
            QCOMPARE(submissions.size(), 1);
            if (dropAck) {
                QCOMPARE(journal(directory.path())
                             .value(QStringLiteral("requests"))
                             .toObject()
                             .value(id)
                             .toObject()
                             .value(QStringLiteral("state"))
                             .toString(),
                         QStringLiteral("held"));
                service.reset();
                revision = 2;
                audio = QStringLiteral("sub");
                destination = QDir(directory.path()).filePath(QStringLiteral("changed"));
                service = make();
                QTRY_VERIFY_WITH_TIMEOUT(submissions.size() == 2 && !service->busy(), 10000);
            }
            QVERIFY(acceptedBeforeEnqueue);
            QVERIFY(receiptSaved);
            QCOMPARE(submissions.last().profile.audio, QStringLiteral("dub"));
            QCOMPARE(submissions.last().profile.destination, directory.path());
            QCOMPARE(submissions.last().profile.engine, QStringLiteral("direct"));
            QCOMPARE(submissions.last().episodes, numbers);
            QCOMPARE(submissions.last().revision, 1);
        }
        service.reset();
        directApi = true;
        service = make();
        QTRY_VERIFY_WITH_TIMEOUT(!service->busy(), 10000);
        QCOMPARE(submissions.size(), 2); // queued receipt is not delivered again after restart

        request.insert(QStringLiteral("profileRevision"), 2);
        const auto pending = call("POST", QStringLiteral("requests"), request, token,
                                  QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
        QCOMPARE(pending.status, 201);
        const auto sessionId = browser.body.value(QStringLiteral("id")).toString();
        service->revokeLink(sessionId);
        QTRY_VERIFY_WITH_TIMEOUT(!service->busy(), 10000);
        QVERIFY2(service->error().isEmpty(), qPrintable(service->error()));
        QVERIFY(service->links().isEmpty());
        const auto cancelled = call("GET",
                                    QStringLiteral("devices/me/inbox/") +
                                        pending.body.value(QStringLiteral("id")).toString(),
                                    {}, credentials->token);
        QCOMPARE(cancelled.status, 200);
        QCOMPARE(cancelled.body.value(QStringLiteral("status")).toString(),
                 QStringLiteral("cancelled"));
        QCOMPARE(call("POST",
                      QStringLiteral("devices/me/inbox/") +
                          pending.body.value(QStringLiteral("id")).toString() +
                          QStringLiteral("/ack"),
                      {{QStringLiteral("status"), QStringLiteral("accepted")}}, credentials->token)
                     .status,
                 409);
        service->poll();
        QTRY_VERIFY_WITH_TIMEOUT(!service->busy(), 10000);
        QCOMPARE(submissions.size(), 2);
        QCOMPARE(call("POST", QStringLiteral("requests"), request, token,
                      QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8())
                     .status,
                 404);
        service.reset();
        queue.reset();
        repository.reset();
        library.reset();
        database.reset();
        opened = storage::Database::open(databasePath);
        QVERIFY(opened);
        database = std::move(*opened);
        library = std::make_unique<library::LibraryRepository>(*database);
        repository = std::make_unique<DownloadRepository>(*database);
        queue = makeQueue();
        for (const auto& submission : submissions) {
            const auto ids = repository->remoteTaskIds(submission.requestId);
            QVERIFY(ids);
            QCOMPARE(ids->size(), 2);
            const DownloadSettings changed{.directory = destination,
                                           .audio = api::Audio::Sub,
                                           .engine = DownloadEngine::JDownloaderLocal};
            const auto replay = queue->enqueueMany(
                submission.animeSlug, submission.episodes, QStringLiteral("Changed title"),
                TaskOrigin::Manual, submission.requestId, changed);
            QVERIFY(replay);
            QCOMPARE(replay->added, 0);
            QCOMPARE(replay->taskIds, *ids);
            for (const auto id : *ids) {
                const auto stored = repository->task(id);
                QVERIFY(stored && stored->has_value());
                QCOMPARE((**stored).remoteRequestId, submission.requestId);
                QCOMPARE((**stored).audio, api::Audio::Dub);
                QCOMPARE((**stored).engine, DownloadEngine::Native);
                QVERIFY((**stored).targetPath.startsWith(directory.path() + u'/'));
                QVERIFY(!(**stored).targetPath.startsWith(destination + u'/'));
            }
        }
        queue->start(false);
        QCOMPARE(queue->tasks().size(), 4);
        QVERIFY(!aria.isReady());
        QVERIFY(engineServer.requests().isEmpty());
    }
};
QTEST_GUILESS_MAIN(RemoteApiIntegrationTest)
#include "remote_api_integration_test.moc"
