#include "downloads/download_manager.h"
#include "http_test_server.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub;
using namespace animehub::downloads;
using animehub::testing::HttpTestServer;

namespace {

constexpr int kTimeout = 30'000;
const auto kFrieren = QStringLiteral("sousou-no-frieren");
const auto kBebop = QStringLiteral("cowboy-bebop");
const auto kDandadan = QStringLiteral("dandadan");

using Episode = std::pair<QString, double>;

QJsonObject vectors() {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/mega/vectors.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing MEGA vectors");
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray plaintext() {
    QByteArray plain(vectors().value(u"size").toInteger(), Qt::Uninitialized);
    for (qsizetype i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<char>((i * 31 + 7) & 0xff);
    }
    return plain;
}

QByteArray encrypted() {
    auto data = plaintext();
    const auto link = mega::parseFileLink(QUrl(QStringLiteral("https://mega.nz/file/STRBjZya#") +
                                               vectors().value(u"linkKey").toString()));
    const platform::Aes128 aes(link->key);
    mega::applyKeystream(
        {reinterpret_cast<std::uint8_t*>(data.data()), static_cast<std::size_t>(data.size())}, 0,
        aes, link->nonce);
    return data;
}

QString sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QByteArray resolution(const QList<std::pair<const char*, QString>>& links) {
    QJsonArray array;
    for (const auto& [provider, url] : links) {
        array.append(QJsonObject{{QStringLiteral("provider"), QLatin1StringView(provider)},
                                 {QStringLiteral("url"), url}});
    }
    const QJsonObject episode{{QStringLiteral("episodeNumber"), 1},
                              {QStringLiteral("audio"), QStringLiteral("SUB")},
                              {QStringLiteral("errorCode"), QJsonValue::Null},
                              {QStringLiteral("links"), array}};
    const QJsonObject root{
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("packageName"), QStringLiteral("Sousou no Frieren")},
                     {QStringLiteral("episodes"), QJsonArray{episode}}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString titleOf(const QString& slug) {
    if (slug == kBebop) {
        return QStringLiteral("Cowboy Bebop");
    }
    return slug == kDandadan ? QStringLiteral("Dandadan") : QStringLiteral("Sousou no Frieren");
}

QStringList leftovers(const QString& directory) {
    QStringList found;
    QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto path = it.next();
        if (path.endsWith(u".mega") || path.endsWith(u".part") || path.endsWith(u".aria2")) {
            found.append(path);
        }
    }
    return found;
}

class FakeJd {
public:
    bool moved = false;
    bool finished = false;
    bool extracting = false;
    QString destination;
    int submissions = 0;
    int removed = 0;
    int submitDelay = 0;
    bool scoped = true;
    void install(HttpTestServer& server) {
        server.route("GET", "/linkgrabberv2/addLinks", [this](const auto& r) {
            ++submissions;
            const auto object =
                QJsonDocument::fromJson(QUrl::fromPercentEncoding(r.query).toUtf8()).object();
            destination = object.value(u"destinationFolder").toString();
            scoped = scoped && object.value(u"assignJobID").toBool() &&
                     !object.value(u"autostart").toBool();
            return HttpTestServer::Response{.body = "{\"data\":{\"id\":77}}",
                                            .delayMs = submitDelay};
        });
        server.route("GET", "/downloadsV2/queryLinks", [this](const auto& r) {
            scoped = scoped && QUrl::fromPercentEncoding(r.query).contains(
                                   QStringLiteral("\"jobUUIDs\":[77]"));
            QJsonArray links;
            if (moved)
                links.append(QJsonObject{{QStringLiteral("uuid"), 88},
                                         {QStringLiteral("finished"), finished},
                                         {QStringLiteral("running"), !finished},
                                         {QStringLiteral("bytesLoaded"), finished ? 8 : 2},
                                         {QStringLiteral("bytesTotal"), 8},
                                         {QStringLiteral("extractionStatus"),
                                          extracting ? QStringLiteral("RUNNING") : QString()}});
            return HttpTestServer::Response{
                .body = QJsonDocument(QJsonObject{{QStringLiteral("data"), links}}).toJson()};
        });
        server.json("/linkgrabberv2/queryLinkCrawlerJobs",
                    "{\"data\":[{\"jobId\":77,\"checking\":false,\"crawling\":false}]}");
        server.route("GET", "/linkgrabberv2/queryLinks", [this](const auto& r) {
            scoped = scoped && QUrl::fromPercentEncoding(r.query).contains(
                                   QStringLiteral("\"jobUUIDs\":[77]"));
            return HttpTestServer::Response{
                .body = moved
                            ? QByteArray("{\"data\":[]}")
                            : QByteArray("{\"data\":[{\"uuid\":88,\"availability\":\"ONLINE\"}]}")};
        });
        server.route("GET", "/linkgrabberv2/moveToDownloadlist", [this](const auto& r) {
            scoped = scoped && QUrl::fromPercentEncoding(r.query) == QStringLiteral("[88]&[]");
            moved = true;
            return HttpTestServer::Response{.body = "{\"data\":null}"};
        });
        server.route("GET", "/downloadsV2/forceDownload", [this](const auto& r) {
            scoped = scoped && QUrl::fromPercentEncoding(r.query) == QStringLiteral("[88]&[]");
            return HttpTestServer::Response{.body = "{\"data\":null}"};
        });
        server.route("GET", "/linkgrabberv2/abort", [this](const auto& r) {
            scoped = scoped && QUrl::fromPercentEncoding(r.query) == QStringLiteral("77");
            return HttpTestServer::Response{.body = "{\"data\":true}"};
        });
        for (const auto& prefix : {QByteArray("/linkgrabberv2/"), QByteArray("/downloadsV2/")}) {
            server.route("GET", prefix + "removeLinks", [this](const auto& r) {
                scoped = scoped && QUrl::fromPercentEncoding(r.query) == QStringLiteral("[88]&[]");
                ++removed;
                return HttpTestServer::Response{.body = "{\"data\":null}"};
            });
        }
    }
};

} // namespace

class DownloadManagerTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> root_;
    std::unique_ptr<HttpTestServer> server_;
    std::unique_ptr<QNetworkAccessManager> network_;
    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<library::LibraryRepository> library_;
    std::unique_ptr<DownloadRepository> repository_;
    std::unique_ptr<api::ApiClient> api_;
    std::unique_ptr<mega::Api> mega_;
    std::unique_ptr<pixeldrain::Api> pixeldrain_;
    std::unique_ptr<Aria2> aria2_;
    std::unique_ptr<DownloadManager> manager_;
    QString downloads_;
    int mega_calls_ = 0;
    int max_concurrent_ = 2;
    DownloadEngine engine_ = DownloadEngine::Native;

    void serveMega(int bytes_per_tick = 0, int status = 200) {
        server_->route("POST", "/cs", [this](const HttpTestServer::Request&) {
            ++mega_calls_;
            const QJsonObject file{
                {QStringLiteral("s"), vectors().value(u"size")},
                {QStringLiteral("at"), vectors().value(u"attributes")},
                {QStringLiteral("g"), server_->url(QStringLiteral("/files/episode")).toString()}};
            return HttpTestServer::Response{
                .body = QJsonDocument(QJsonArray{file}).toJson(QJsonDocument::Compact)};
        });
        if (status == 200) {
            server_->file("/files/episode", encrypted(), bytes_per_tick);
        } else {
            server_->route("GET", "/files/episode", [status](const HttpTestServer::Request&) {
                return HttpTestServer::Response{.status = status};
            });
        }
    }

    void resolveTo(const QByteArray& body, const QString& slug = kFrieren) {
        server_->route("POST", "/api/v1/anime/" + slug.toLatin1() + "/downloads/resolve",
                       [body](const HttpTestServer::Request&) {
                           return HttpTestServer::Response{.body = body};
                       });
    }

    // Episodes in the order the manager asked the API for their sources.
    QList<Episode> resolved() const {
        QList<Episode> episodes;
        for (const auto& request : server_->requests()) {
            if (request.path.endsWith("/downloads/resolve")) {
                const auto numbers = QJsonDocument::fromJson(request.body)
                                         .object()
                                         .value(u"episodeNumbers")
                                         .toArray();
                episodes.append({QString::fromLatin1(request.path.split('/').value(4)),
                                 numbers.first().toDouble()});
            }
        }
        return episodes;
    }

    QList<Episode> listed() const {
        QList<Episode> episodes;
        for (const auto& task : manager_->tasks()) {
            episodes.append({task.slug, task.number});
        }
        return episodes;
    }

    std::optional<Task> taskFor(const QString& slug, double number) const {
        for (const auto& task : manager_->tasks()) {
            if (task.slug == slug && task.number == number) {
                return task;
            }
        }
        return std::nullopt;
    }

    std::expected<Enqueued, QString> enqueueMany(const QString& slug,
                                                 const QList<double>& numbers) {
        return manager_->enqueueMany(slug, numbers, titleOf(slug), TaskOrigin::Manual);
    }

    void restart() {
        bool stopped = false;
        manager_->shutdown([&] { stopped = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 8000);
        manager_.reset();
        aria2_.reset();
        makeManager();
    }

    // Resumed transfers continue past the bytes already on disk: the latest range start asked
    // for `path` since request `since` (aria2 first probes the size with a plain GET).
    qint64 resumedAt(const QByteArray& path, qsizetype since) const {
        qint64 resumed_at = -1;
        for (auto it = server_->requests().begin() + since; it != server_->requests().end(); ++it) {
            const auto range = it->headers.value("range");
            if (it->path == path && !range.isEmpty()) {
                resumed_at = std::max(resumed_at, range.mid(6).split('-').first().toLongLong());
            }
        }
        return resumed_at;
    }

    static QString megaUrl() {
        return QStringLiteral("https://mega.nz/file/STRBjZya#") +
               vectors().value(u"linkKey").toString();
    }

    void makeManager(bool transfers = true) {
        aria2_ = std::make_unique<Aria2>(*network_, QStringLiteral(ANIMEHUB_ARIA2_EXECUTABLE),
                                         "AnimeHub-Desktop-Test");
        manager_ = std::make_unique<DownloadManager>(
            DownloadManager::Services{*api_, *aria2_, *mega_, *pixeldrain_, *repository_,
                                      *library_},
            [this] {
                return DownloadSettings{.directory = downloads_,
                                        .maxConcurrent = max_concurrent_,
                                        .engine = engine_,
                                        .jdEndpoint = server_->url()};
            });
        manager_->start(transfers);
    }

    std::optional<Task> taskOf(qint64 id) const {
        return manager_->task(id);
    }

private slots:
    void init() {
        root_ = std::make_unique<QTemporaryDir>();
        downloads_ = root_->filePath(QStringLiteral("Anime"));
        server_ = std::make_unique<HttpTestServer>();
        QVERIFY(server_->listen());
        network_ = std::make_unique<QNetworkAccessManager>();
        auto opened = storage::Database::open(root_->filePath(QStringLiteral("library.sqlite3")));
        QVERIFY(opened);
        database_ = std::move(*opened);
        library_ = std::make_unique<library::LibraryRepository>(*database_);
        repository_ = std::make_unique<DownloadRepository>(*database_);
        for (const auto& slug : {kFrieren, kBebop, kDandadan}) {
            library::AnimeRecord anime;
            anime.slug = slug;
            anime.title = titleOf(slug);
            QVERIFY(library_->saveSnapshot(anime));
        }
        api_ = std::make_unique<api::ApiClient>(
            *network_,
            api::ApiClient::Options{.baseUrl = server_->url(QStringLiteral("/api/v1/"))});
        mega_ =
            std::make_unique<mega::Api>(*network_, nullptr, server_->url(QStringLiteral("/cs")));
        pixeldrain_ = std::make_unique<pixeldrain::Api>(*network_, nullptr, server_->url());
        mega_calls_ = 0;
        max_concurrent_ = 2;
        engine_ = DownloadEngine::Native;
        makeManager();
    }

    void cleanup() {
        bool stopped = false;
        manager_->shutdown([&] { stopped = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 8000);
        manager_.reset();
        aria2_.reset();
        api_.reset();
        mega_.reset();
        pixeldrain_.reset();
        repository_.reset();
        library_.reset();
        database_.reset();
        network_.reset();
        server_.reset();
        root_.reset();
    }

    void readOnlyStartupLoadsQueueWithoutWorkersOrFileMutations() {
        bool stopped = false;
        manager_->shutdown([&] { stopped = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 8000);
        manager_.reset();
        aria2_.reset();
        auto seed =
            repository_->create(kFrieren, 1, titleOf(kFrieren), api::Audio::Sub, TaskOrigin::Manual,
                                root_->filePath(QStringLiteral("keep")));
        QVERIFY(seed);
        seed->state = TaskState::Failed;
        QVERIFY(repository_->save(*seed));
        QFile partial(seed->targetPath + QStringLiteral(".part"));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("keep");
        partial.close();
        makeManager(false);
        QCOMPARE(taskOf(seed->id)->state, TaskState::Failed);
        manager_->retry(seed->id);
        manager_->forget(seed->id);
        manager_->cancel(seed->id);
        manager_->prioritize(kFrieren);
        QVERIFY(!manager_->enqueue(kFrieren, 2, titleOf(kFrieren), TaskOrigin::Manual));
        QCoreApplication::processEvents();
        QVERIFY(!aria2_->isReady());
        QVERIFY(QFileInfo::exists(partial.fileName()));
        QCOMPARE(taskOf(seed->id)->state, TaskState::Failed);
        QCOMPARE(server_->requests().size(), 0);
        QCOMPARE((**repository_->task(seed->id)).state, TaskState::Failed);
    }

    void remoteReplaySurvivesCancellationAndQueueCleanup() {
        const auto request = QStringLiteral("remote-fixture-request");
        DownloadSettings pinned{.directory = root_->filePath(QStringLiteral("Pinned")),
                                .jdEndpoint = server_->url()};
        const auto first = manager_->enqueueMany(kFrieren, {1, 2}, titleOf(kFrieren),
                                                 TaskOrigin::Manual, request, pinned);
        QVERIFY(first);
        QCOMPARE(first->added, 2);
        QCOMPARE(first->taskIds.size(), 2);
        for (const auto id : first->taskIds)
            manager_->cancel(id);
        manager_->clearFinished();
        QVERIFY(manager_->tasks().isEmpty());
        pinned.directory = root_->filePath(QStringLiteral("Changed"));
        pinned.engine = DownloadEngine::JDownloaderLocal;
        pinned.audio = api::Audio::Dub; // replay after restart must retain the accepted SUB job
        const auto replay = manager_->enqueueMany(kFrieren, {1, 2}, titleOf(kFrieren),
                                                  TaskOrigin::Manual, request, pinned);
        QVERIFY(replay);
        QCOMPARE(replay->added, 0);
        QCOMPARE(replay->restarted, 0);
        QCOMPARE(replay->taskIds, first->taskIds);
        QVERIFY(manager_->tasks().isEmpty());
    }

    void remoteCompletionReplayKeepsPinnedNativeDestination() {
        serveMega();
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        const auto request = QStringLiteral("remote-completed-fixture");
        DownloadSettings pinned{.directory = root_->filePath(QStringLiteral("RemotePinned")),
                                .jdEndpoint = server_->url()};
        const auto first = manager_->enqueueMany(kFrieren, {1}, titleOf(kFrieren),
                                                 TaskOrigin::Manual, request, pinned);
        QVERIFY(first);
        QCOMPARE(first->taskIds.size(), 1);
        const auto id = first->taskIds.first();
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(id)->state, TaskState::Completed, kTimeout);
        const auto original = taskOf(id)->targetPath;
        QVERIFY(original.startsWith(pinned.directory));
        QCOMPARE(taskOf(id)->remoteRequestId, request);
        pinned.engine = DownloadEngine::JDownloaderLocal;
        pinned.directory = root_->filePath(QStringLiteral("Changed"));
        const auto replay = manager_->enqueueMany(kFrieren, {1}, titleOf(kFrieren),
                                                  TaskOrigin::Manual, request, pinned);
        QVERIFY(replay);
        QCOMPARE(replay->added, 0);
        QCOMPARE(replay->taskIds, first->taskIds);
        QCOMPARE(taskOf(id)->state, TaskState::Completed);
        QCOMPARE(taskOf(id)->targetPath, original);
        QCOMPARE(taskOf(id)->engine, DownloadEngine::Native);
        QCOMPARE(mega_calls_, 1);
    }

    void jdReconcilesAfterRestartAndWaitsForExtraction() {
        engine_ = DownloadEngine::JDownloaderLocal;
        FakeJd jd;
        jd.install(*server_);
        resolveTo(
            resolution({{"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/test1234")}}));
        auto created = manager_->enqueue(kFrieren, 1, titleOf(kFrieren), TaskOrigin::Manual);
        QVERIFY(created);
        const auto id = *created;
        QTRY_VERIFY_WITH_TIMEOUT(jd.moved, kTimeout);
        QCOMPARE(taskOf(id)->engineJobId, 77);
        QCOMPARE(taskOf(id)->engineLinkIds, QList<qint64>{88});
        const auto destination = taskOf(id)->engineDestination;
        engine_ = DownloadEngine::Native; // settings changes cannot reroute the persisted task
        restart();
        QCOMPARE(taskOf(id)->engine, DownloadEngine::JDownloaderLocal);
        QCOMPARE(taskOf(id)->engineDestination, destination);
        QCOMPARE(jd.submissions, 1);
        jd.finished = true;
        jd.extracting = true;
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(id)->enginePhase, QStringLiteral("extracting"), kTimeout);
        QVERIFY(taskOf(id)->state != TaskState::Completed);
        QFile media(QDir(destination).filePath(QStringLiteral("episode.mkv")));
        QVERIFY(media.open(QIODevice::WriteOnly));
        QCOMPARE(media.write("12345678"), 8);
        media.close();
        jd.extracting = false;
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(id)->state, TaskState::Completed, kTimeout);
        QCOMPARE(jd.submissions, 1);
        QVERIFY(jd.scoped);
        QVERIFY(library_->episodeLocations(kFrieren, 1)->size() == 1);
        for (const auto& r : server_->requests()) {
            QVERIFY(r.path != "/downloadcontroller/start" && r.path != "/downloadcontroller/stop" &&
                    r.path != "/downloadcontroller/pause");
        }
    }

    void jdCancellationDuringSubmissionWaitsForOwnedIds() {
        engine_ = DownloadEngine::JDownloaderLocal;
        FakeJd jd;
        jd.submitDelay = 300;
        jd.install(*server_);
        resolveTo(
            resolution({{"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/test1234")}}));
        const auto id = manager_->enqueue(kFrieren, 1, titleOf(kFrieren), TaskOrigin::Manual);
        QVERIFY(id);
        QTRY_COMPARE(jd.submissions, 1);
        manager_->cancel(*id);
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(*id)->state, TaskState::Cancelled, kTimeout);
        QVERIFY(jd.scoped);
        QCOMPARE(jd.removed, 1);
        QVERIFY(!jd.moved);
    }

    void jdFinishedWithoutLocalMediaDoesNotComplete() {
        engine_ = DownloadEngine::JDownloaderLocal;
        FakeJd jd;
        jd.install(*server_);
        jd.finished = true;
        resolveTo(
            resolution({{"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/test1234")}}));
        auto id = manager_->enqueue(kFrieren, 1, titleOf(kFrieren), TaskOrigin::Manual);
        QVERIFY(id);
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(*id)->state, TaskState::Failed, kTimeout);
        QCOMPARE(taskOf(*id)->issue, TaskIssue::Storage);
        QVERIFY(library_->episodeLocations(kFrieren, 1)->isEmpty());
        QCOMPARE(jd.submissions, 1);
    }

    void jdUncertainSubmissionNeverResubmitsOnRestart() {
        engine_ = DownloadEngine::JDownloaderLocal;
        server_->route("GET", "/linkgrabberv2/addLinks", [](const auto&) {
            return HttpTestServer::Response{.body = "{\"data\":null}"};
        });
        resolveTo(
            resolution({{"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/test1234")}}));
        auto id = manager_->enqueue(kFrieren, 1, titleOf(kFrieren), TaskOrigin::Manual);
        QVERIFY(id);
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(*id)->state, TaskState::Failed, kTimeout);
        QVERIFY(!taskOf(*id)->engineSubmission.isEmpty());
        restart();
        manager_->retry(*id);
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(*id)->state, TaskState::Failed, kTimeout);
        QCOMPARE(server_->count("/linkgrabberv2/addLinks"), 1);
        QCOMPARE(server_->count("/api/v1/anime/sousou-no-frieren/downloads/resolve"), 1);
    }

    void downloadsAndVerifiesAMegaEpisode() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        const auto id = manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                          QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QVERIFY(id);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, kTimeout);
        const auto task = taskOf(*id);
        QCOMPARE(task->state, TaskState::Completed);
        QCOMPARE(task->provider, api::Provider::Mega);
        const auto expected_path =
            downloads_ + QStringLiteral("/Sousou no Frieren/Sousou no Frieren - 01.mp4");
        QCOMPARE(task->targetPath, expected_path);
        QCOMPARE(sha256File(expected_path), vectors().value(u"plainSha256").toString());
        const auto episode = library_->episode(QStringLiteral("sousou-no-frieren"), 1);
        QVERIFY(episode && *episode);
        QCOMPARE((*episode)->filePath, expected_path);
        QTRY_VERIFY(leftovers(downloads_).isEmpty());
    }

    void downloadsFromPixeldrainAndKeepsTheExtension() {
        resolveTo(resolution({{"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/abc123")}}));
        const auto data = plaintext();
        server_->json("/api/file/abc123/info",
                      QByteArrayLiteral("{\"name\":\"[Sub] Frieren 01.mkv\",\"size\":") +
                          QByteArray::number(data.size()) + '}');
        server_->file("/api/file/abc123", data);
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        const auto id = manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                          QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, kTimeout);
        const auto task = taskOf(*id);
        QVERIFY(task->targetPath.endsWith(u" - 01.mkv"));
        QCOMPARE(sha256File(task->targetPath), vectors().value(u"plainSha256").toString());
    }

    void fallsBackToTheNextProvider() {
        resolveTo(resolution({{"MEGA", megaUrl()},
                              {"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/abc123")}}));
        server_->route("POST", "/cs", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.body = "[-9]"};
        });
        const auto data = plaintext();
        server_->json("/api/file/abc123/info", QByteArrayLiteral("{\"name\":\"a.mp4\",\"size\":") +
                                                   QByteArray::number(data.size()) + '}');
        server_->file("/api/file/abc123", data);
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        const auto id = manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                          QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, kTimeout);
        QCOMPARE(taskOf(*id)->provider, api::Provider::Pixeldrain);
    }

    void failsWhenNoNativeProviderHasTheEpisode() {
        resolveTo(resolution({}));
        QSignalSpy failed(manager_.get(), &DownloadManager::taskFailed);
        const auto id = manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                          QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, kTimeout);
        QCOMPARE(taskOf(*id)->issue, TaskIssue::NoSource);
    }

    void waitsWhenTheMegaQuotaRunsOut() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega(0, 509);
        const auto id = manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                          QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(*id)->state, TaskState::Waiting, kTimeout);
        QCOMPARE(taskOf(*id)->issue, TaskIssue::Quota);
        const auto wait = QDateTime::currentDateTimeUtc().secsTo(taskOf(*id)->retryAt);
        QVERIFY2(wait > 25 * 60 && wait <= 30 * 60, qPrintable(QString::number(wait)));
    }

    void pausesAndResumes() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega(16 * 1024); // ~1.6 MB/s: long enough to pause mid-transfer
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        const auto id = *manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                           QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(taskOf(id)->bytesDone > 0, kTimeout);
        manager_->pause(id);
        QCOMPARE(taskOf(id)->state, TaskState::Paused);
        QTest::qWait(1500);
        const auto paused_at = taskOf(id)->bytesDone;
        QTest::qWait(1000);
        QCOMPARE(taskOf(id)->bytesDone, paused_at);
        // Asking for the episode again resumes the same task.
        QCOMPARE(*manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                    QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual),
                 id);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, kTimeout);
        QCOMPARE(sha256File(taskOf(id)->targetPath), vectors().value(u"plainSha256").toString());
    }

    void cancellingRemovesThePartialFiles() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega(8 * 1024);
        const auto id = *manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                           QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(taskOf(id)->bytesDone > 0, kTimeout);
        QVERIFY(!leftovers(downloads_).isEmpty());
        manager_->cancel(id);
        QCOMPARE(taskOf(id)->state, TaskState::Cancelled);
        // aria2 lets go of the files once it has stopped, later on a slow machine.
        QTRY_VERIFY_WITH_TIMEOUT(leftovers(downloads_).isEmpty(), kTimeout);
        // A cancelled episode can be queued again.
        QVERIFY(manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                  QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual));
    }

    void resumesAfterARestart() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        // The first run stalls after 1 MiB, so it is surely unfinished when AnimeHub closes.
        server_->file("/files/episode", encrypted(), 64 * 1024, 1024 * 1024);
        const auto id = *manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                           QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(taskOf(id)->bytesDone >= 1024 * 1024, kTimeout);
        QTest::qWait(1500); // aria2 saves its progress every second
        const auto resumed_from = taskOf(id)->bytesDone;
        bool stopped = false;
        manager_->shutdown([&] { stopped = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 8000);
        manager_.reset();
        aria2_.reset();
        QCOMPARE(leftovers(downloads_).size(), 2); // the partial file and aria2's control file

        server_->file("/files/episode", encrypted());
        const auto before = server_->requests().size();
        makeManager();
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, kTimeout);
        QCOMPARE(sha256File(taskOf(id)->targetPath), vectors().value(u"plainSha256").toString());
        // aria2 first probes the size with a plain GET it drops at once; the transfer itself
        // continues past the bytes already on disk and never starts over.
        qint64 resumed_at = -1;
        for (auto it = server_->requests().begin() + before; it != server_->requests().end();
             ++it) {
            const auto range = it->headers.value("range");
            if (it->path != "/files/episode" || range.isEmpty()) {
                continue;
            }
            const auto first = range.mid(6).split('-').first().toLongLong();
            QVERIFY2(first > 0, range.constData());
            resumed_at = std::max(resumed_at, first);
        }
        QVERIFY2(resumed_at >= resumed_from,
                 qPrintable(QStringLiteral("resumed at %1, %2 bytes were on disk")
                                .arg(resumed_at)
                                .arg(resumed_from)));
    }

    void finishesATransferThatCompletedWhileClosed() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        // A whole encrypted file without aria2's control file: the transfer ended while
        // AnimeHub was not looking. Nothing needs to be downloaded again.
        const auto folder = downloads_ + QStringLiteral("/Sousou no Frieren");
        QVERIFY(QDir().mkpath(folder));
        auto created = repository_->create(QStringLiteral("sousou-no-frieren"), 1,
                                           QStringLiteral("Sousou no Frieren"), api::Audio::Sub,
                                           TaskOrigin::Manual,
                                           folder + QStringLiteral("/Sousou no Frieren - 01.mp4"));
        QVERIFY(created);
        created->provider = api::Provider::Mega;
        created->sourceId = QStringLiteral("STRBjZya");
        QVERIFY(repository_->save(*created));
        QFile partial(created->targetPath + QStringLiteral(".mega"));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write(encrypted());
        partial.close();

        bool stopped = false;
        manager_->shutdown([&] { stopped = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 8000);
        manager_.reset();
        aria2_.reset();
        makeManager();
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, kTimeout);
        QCOMPARE(server_->count("/files/episode"), 0);
        QCOMPARE(sha256File(created->targetPath), vectors().value(u"plainSha256").toString());
    }

    void downloadsAgainWhenTheFileIsCorrupt() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        auto broken = encrypted();
        broken[1000] = static_cast<char>(broken[1000] ^ 0x5a);
        server_->file("/files/episode", broken);
        QSignalSpy failed(manager_.get(), &DownloadManager::taskFailed);
        const auto id = *manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                                           QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, kTimeout * 2);
        QCOMPARE(taskOf(id)->issue, TaskIssue::Integrity);
        QCOMPARE(mega_calls_, 3);
        QVERIFY(!QFileInfo::exists(taskOf(id)->targetPath));
    }

    void refusesADuplicateAndKeepsTheQueueAcrossRestarts() {
        resolveTo(resolution({}));
        const auto first =
            manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                              QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        const auto second =
            manager_->enqueue(QStringLiteral("sousou-no-frieren"), 1,
                              QStringLiteral("Sousou no Frieren"), TaskOrigin::Manual);
        QCOMPARE(*first, *second);
        QCOMPARE(repository_->all()->size(), 1);
    }

    void runsTheQueueSeriesBySeriesInEpisodeOrder() {
        max_concurrent_ = 1;
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        resolveTo(resolution({{"MEGA", megaUrl()}}), kBebop);
        serveMega();
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        const auto frieren = enqueueMany(kFrieren, {3, 1, 2, 1});
        QVERIFY(frieren);
        QCOMPARE(frieren->added, 3);
        QCOMPARE(frieren->restarted, 0);
        QVERIFY(manager_->enqueue(kBebop, 1, titleOf(kBebop), TaskOrigin::Manual));
        // A later episode joins its series, ahead of the series that arrived after it.
        QCOMPARE(enqueueMany(kFrieren, {4})->added, 1);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 5, kTimeout * 2);
        QCOMPARE(resolved(),
                 (QList<Episode>{
                     {kFrieren, 1}, {kFrieren, 2}, {kFrieren, 3}, {kFrieren, 4}, {kBebop, 1}}));
        for (const auto& task : manager_->tasks()) {
            QCOMPARE(sha256File(task.targetPath), vectors().value(u"plainSha256").toString());
        }
    }

    void queueingNeverStopsARunningTransfer() {
        max_concurrent_ = 1;
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        server_->file("/files/episode", encrypted(), 64 * 1024, 1024 * 1024); // never ends
        const auto running = *manager_->enqueue(kFrieren, 5, titleOf(kFrieren), TaskOrigin::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(taskOf(running)->bytesDone > 0, kTimeout);
        QCOMPARE(enqueueMany(kFrieren, {1, 2})->added, 2);
        QCOMPARE(enqueueMany(kBebop, {1})->added, 1);
        QTest::qWait(1000);
        QCOMPARE(taskOf(running)->state, TaskState::Downloading);
        QCOMPARE(taskFor(kFrieren, 1)->state, TaskState::Queued);
        QCOMPARE(listed(),
                 (QList<Episode>{{kFrieren, 1}, {kFrieren, 2}, {kFrieren, 5}, {kBebop, 1}}));
    }

    void reordersTheSeriesAndKeepsTheOrderAcrossRestarts() {
        QVERIFY(enqueueMany(kFrieren, {1, 2}));
        QVERIFY(enqueueMany(kBebop, {1}));
        QVERIFY(enqueueMany(kDandadan, {2, 1}));
        manager_->pauseAll(); // nothing needs to run here
        QCOMPARE(listed(),
                 (QList<Episode>{
                     {kFrieren, 1}, {kFrieren, 2}, {kBebop, 1}, {kDandadan, 1}, {kDandadan, 2}}));
        // Unknown series are ignored; the ones left out follow in their order.
        manager_->reorder({kDandadan, QStringLiteral("unknown"), kFrieren});
        const QList<Episode> reordered{
            {kDandadan, 1}, {kDandadan, 2}, {kFrieren, 1}, {kFrieren, 2}, {kBebop, 1}};
        QCOMPARE(listed(), reordered);
        restart();
        QCOMPARE(listed(), reordered);
        // A new series goes last; a new episode joins its series.
        QVERIFY(manager_->enqueueMany(QStringLiteral("another"), {1}, QStringLiteral("Another"),
                                      TaskOrigin::Manual));
        QVERIFY(enqueueMany(kFrieren, {3}));
        manager_->pauseAll();
        QCOMPARE(listed(), (QList<Episode>{{kDandadan, 1},
                                           {kDandadan, 2},
                                           {kFrieren, 1},
                                           {kFrieren, 2},
                                           {kFrieren, 3},
                                           {kBebop, 1},
                                           {QStringLiteral("another"), 1}}));
    }

    void keepsTheSeriesTogetherInAQueueFromAnEarlierVersion() {
        // Earlier versions numbered each task on its own: Frieren 1, Cowboy Bebop 1, Frieren 5.
        qint64 order = 0;
        for (const auto& [slug, number] :
             QList<Episode>{{kFrieren, 1}, {kBebop, 1}, {kFrieren, 5}}) {
            auto task = repository_->create(
                slug, number, titleOf(slug), api::Audio::Sub, TaskOrigin::Manual,
                downloads_ + QStringLiteral("/%1 - %2").arg(slug).arg(number));
            QVERIFY(task);
            task->state = TaskState::Paused;
            task->queueOrder = ++order;
            QVERIFY(repository_->save(*task));
        }
        restart();
        // Frieren keeps its place when its first episode leaves the queue.
        manager_->cancel(taskFor(kFrieren, 1)->id);
        QCOMPARE(listed(), (QList<Episode>{{kFrieren, 5}, {kBebop, 1}, {kFrieren, 1}}));
    }

    void prioritizingTakesTheSlotAndTheOthersKeepTheirBytes() {
        max_concurrent_ = 1;
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        // Frieren stalls after 1 MiB, so it is surely unfinished when it gives way.
        server_->file("/files/episode", encrypted(), 64 * 1024, 1024 * 1024);
        resolveTo(resolution({{"PIXELDRAIN", QStringLiteral("https://pixeldrain.com/u/bebop1")}}),
                  kBebop);
        const auto data = plaintext();
        server_->json("/api/file/bebop1/info", QByteArrayLiteral("{\"name\":\"b.mp4\",\"size\":") +
                                                   QByteArray::number(data.size()) + '}');
        server_->file("/api/file/bebop1", data);
        const auto frieren = *manager_->enqueue(kFrieren, 1, titleOf(kFrieren), TaskOrigin::Manual);
        const auto bebop = *manager_->enqueue(kBebop, 1, titleOf(kBebop), TaskOrigin::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(taskOf(frieren)->bytesDone >= 1024 * 1024, kTimeout);
        QTest::qWait(1500); // aria2 saves its progress every second
        QCOMPARE(taskOf(bebop)->state, TaskState::Queued);
        const auto kept = taskOf(frieren)->bytesDone;
        const auto before = server_->requests().size();

        manager_->prioritize(kBebop);
        QCOMPARE(taskOf(frieren)->state, TaskState::Queued);
        QCOMPARE(listed().first(), (Episode{kBebop, 1}));
        server_->file("/files/episode", encrypted());
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(bebop)->state, TaskState::Completed, kTimeout);
        QTRY_COMPARE_WITH_TIMEOUT(taskOf(frieren)->state, TaskState::Completed, kTimeout);
        QCOMPARE(sha256File(taskOf(frieren)->targetPath),
                 vectors().value(u"plainSha256").toString());
        const auto resumed_at = resumedAt("/files/episode", before);
        QVERIFY2(
            resumed_at >= kept,
            qPrintable(
                QStringLiteral("resumed at %1, %2 bytes were kept").arg(resumed_at).arg(kept)));
    }

    void pausesAndResumesASeries() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        serveMega();
        server_->file("/files/episode", encrypted(), 64 * 1024, 1024 * 1024);
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        QCOMPARE(enqueueMany(kFrieren, {1, 2, 3})->added, 3);
        QTRY_VERIFY_WITH_TIMEOUT(
            taskFor(kFrieren, 1)->bytesDone > 0 && taskFor(kFrieren, 2)->bytesDone > 0, kTimeout);
        manager_->pauseSeries(kFrieren);
        for (const auto& task : manager_->tasks()) {
            QCOMPARE(task.state, TaskState::Paused);
        }
        QTest::qWait(1000);
        QCOMPARE(taskFor(kFrieren, 3)->state, TaskState::Paused); // its slot stayed free
        QVERIFY(!resolved().contains(Episode{kFrieren, 3}));

        server_->file("/files/episode", encrypted());
        manager_->resumeSeries(kFrieren);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 3, kTimeout * 2);
        for (const auto& task : manager_->tasks()) {
            QCOMPARE(sha256File(task.targetPath), vectors().value(u"plainSha256").toString());
        }
    }

    void resumingAllLeavesWhatWasPausedOnItsOwn() {
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        resolveTo(resolution({{"MEGA", megaUrl()}}), kBebop);
        serveMega();
        server_->file("/files/episode", encrypted(), 64 * 1024, 1024 * 1024);
        QVERIFY(enqueueMany(kFrieren, {1}));
        QVERIFY(enqueueMany(kBebop, {1}));
        manager_->pauseSeries(kBebop);
        manager_->pauseAll();
        QCOMPARE(taskFor(kFrieren, 1)->state, TaskState::Paused);

        // Also after a restart: Frieren goes on, Cowboy Bebop waits to be resumed on its own.
        restart();
        manager_->resumeAll();
        QVERIFY(taskFor(kFrieren, 1)->state != TaskState::Paused);
        QCOMPARE(taskFor(kBebop, 1)->state, TaskState::Paused);

        // With nothing paused all together, resuming all resumes the rest.
        manager_->resumeAll();
        QVERIFY(taskFor(kBebop, 1)->state != TaskState::Paused);
    }

    void retriesTheFailedEpisodesOfASeries() {
        resolveTo(resolution({}));
        resolveTo(resolution({}), kBebop);
        QSignalSpy failed(manager_.get(), &DownloadManager::taskFailed);
        QVERIFY(enqueueMany(kFrieren, {1, 2}));
        QVERIFY(enqueueMany(kBebop, {1}));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 3, kTimeout);

        resolveTo(resolution({{"MEGA", megaUrl()}}));
        resolveTo(resolution({{"MEGA", megaUrl()}}), kBebop);
        serveMega();
        QSignalSpy completed(manager_.get(), &DownloadManager::taskCompleted);
        manager_->retryFailed(kFrieren);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, kTimeout);
        QCOMPARE(taskFor(kFrieren, 1)->state, TaskState::Completed);
        QCOMPARE(taskFor(kFrieren, 2)->state, TaskState::Completed);
        QCOMPARE(taskFor(kBebop, 1)->state, TaskState::Failed);

        // Asking for the series again restarts its failed episode and adds the new one.
        const auto again = enqueueMany(kBebop, {1, 2});
        QCOMPARE(again->added, 1);
        QCOMPARE(again->restarted, 1);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 4, kTimeout);
    }

    void cancellingASeriesRemovesItsPartialFilesOnly() {
        max_concurrent_ = 1;
        resolveTo(resolution({{"MEGA", megaUrl()}}));
        resolveTo(resolution({{"MEGA", megaUrl()}}), kBebop);
        serveMega();
        server_->file("/files/episode", encrypted(), 64 * 1024, 1024 * 1024);
        QVERIFY(enqueueMany(kFrieren, {1, 2}));
        QVERIFY(enqueueMany(kBebop, {1}));
        QTRY_VERIFY_WITH_TIMEOUT(taskFor(kFrieren, 1)->bytesDone > 0, kTimeout);
        // Frieren 1 gives way and waits in line with its partial file, without a transfer.
        manager_->prioritize(kBebop);
        QTRY_VERIFY_WITH_TIMEOUT(taskFor(kBebop, 1)->bytesDone > 0, kTimeout);
        const auto frieren_folder = downloads_ + QStringLiteral("/Sousou no Frieren");
        QVERIFY(!leftovers(frieren_folder).isEmpty());

        manager_->cancelSeries(kFrieren);
        QCOMPARE(taskFor(kFrieren, 1)->state, TaskState::Cancelled);
        QCOMPARE(taskFor(kFrieren, 2)->state, TaskState::Cancelled);
        QTRY_VERIFY_WITH_TIMEOUT(leftovers(frieren_folder).isEmpty(), kTimeout);
        QCOMPARE(taskFor(kBebop, 1)->state, TaskState::Downloading);
        QVERIFY(!leftovers(downloads_ + QStringLiteral("/Cowboy Bebop")).isEmpty());
    }

    void forgetsTheFinishedRowsOfASeries() {
        const auto folder = downloads_ + QStringLiteral("/Sousou no Frieren");
        QVERIFY(QDir().mkpath(folder));
        const auto seed = [&](const QString& slug, double number, TaskState state) {
            auto task = repository_->create(
                slug, number, titleOf(slug), api::Audio::Sub, TaskOrigin::Manual,
                folder + QStringLiteral("/%1 - %2.mp4").arg(slug).arg(number));
            task->state = state;
            task->finishedAt = QDateTime::currentDateTimeUtc();
            QVERIFY(repository_->save(*task));
        };
        seed(kFrieren, 1, TaskState::Completed);
        seed(kFrieren, 2, TaskState::Cancelled);
        seed(kFrieren, 3, TaskState::Failed);
        seed(kFrieren, 4, TaskState::Paused);
        seed(kBebop, 1, TaskState::Completed);
        const auto completed_file = folder + QStringLiteral("/sousou-no-frieren - 1.mp4");
        const auto failed_partial = folder + QStringLiteral("/sousou-no-frieren - 3.mp4.mega");
        for (const auto& path : {completed_file, failed_partial}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        restart();

        manager_->forgetSeries(kFrieren);
        QCOMPARE(listed(), (QList<Episode>{{kFrieren, 4}, {kBebop, 1}}));
        QCOMPARE(repository_->all()->size(), 2);
        QTRY_VERIFY(!QFileInfo::exists(failed_partial));
        QVERIFY(QFileInfo::exists(completed_file)); // the list only; never the episode itself
    }
};

QTEST_GUILESS_MAIN(DownloadManagerTest)
#include "download_manager_test.moc"
