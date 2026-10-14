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

    void resolveTo(const QByteArray& body) {
        server_->route("POST", "/api/v1/anime/sousou-no-frieren/downloads/resolve",
                       [body](const HttpTestServer::Request&) {
                           return HttpTestServer::Response{.body = body};
                       });
    }

    static QString megaUrl() {
        return QStringLiteral("https://mega.nz/file/STRBjZya#") +
               vectors().value(u"linkKey").toString();
    }

    void makeManager() {
        aria2_ = std::make_unique<Aria2>(*network_, QStringLiteral(ANIMEHUB_ARIA2_EXECUTABLE),
                                         "AnimeHub-Desktop-Test");
        manager_ = std::make_unique<DownloadManager>(
            DownloadManager::Services{*api_, *aria2_, *mega_, *pixeldrain_, *repository_,
                                      *library_},
            [this] { return DownloadSettings{.directory = downloads_}; });
        manager_->start();
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
        library::AnimeRecord frieren;
        frieren.slug = QStringLiteral("sousou-no-frieren");
        frieren.title = QStringLiteral("Sousou no Frieren");
        QVERIFY(library_->saveSnapshot(frieren));
        api_ = std::make_unique<api::ApiClient>(
            *network_,
            api::ApiClient::Options{.baseUrl = server_->url(QStringLiteral("/api/v1/"))});
        mega_ =
            std::make_unique<mega::Api>(*network_, nullptr, server_->url(QStringLiteral("/cs")));
        pixeldrain_ = std::make_unique<pixeldrain::Api>(*network_, nullptr, server_->url());
        mega_calls_ = 0;
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
        QTRY_VERIFY_WITH_TIMEOUT(leftovers(downloads_).isEmpty(), 6000);
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
};

QTEST_GUILESS_MAIN(DownloadManagerTest)
#include "download_manager_test.moc"
