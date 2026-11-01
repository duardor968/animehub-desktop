#include "downloads/mega.h"
#include "http_test_server.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub::downloads;
using animehub::testing::HttpTestServer;

namespace {

QJsonObject vectors() {
    QFile file(QStringLiteral(ANIMEHUB_FIXTURES_DIR "/mega/vectors.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing MEGA vectors");
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray referencePlaintext(qsizetype size) {
    QByteArray plain(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i) {
        plain[i] = static_cast<char>((i * 31 + 7) & 0xff);
    }
    return plain;
}

QString sha256(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

std::span<std::uint8_t> bytes(QByteArray& data, qsizetype from = 0, qsizetype length = -1) {
    if (length < 0) {
        length = data.size() - from;
    }
    return {reinterpret_cast<std::uint8_t*>(data.data()) + from, static_cast<std::size_t>(length)};
}

QUrl linkUrl() {
    return QUrl(QStringLiteral("https://mega.nz/file/STRBjZya#") +
                vectors().value(u"linkKey").toString());
}

} // namespace

class MegaTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesFileLinks() {
        const auto link = mega::parseFileLink(linkUrl());
        QVERIFY(link);
        QCOMPARE(link->handle, QStringLiteral("STRBjZya"));

        const auto key = vectors().value(u"linkKey").toString();
        QVERIFY(mega::parseFileLink(QUrl(QStringLiteral("https://mega.nz/#!STRBjZya!") + key)));
        QVERIFY(
            mega::parseFileLink(QUrl(QStringLiteral("https://mega.co.nz/file/STRBjZya#") + key)));
        QVERIFY(!mega::parseFileLink(QUrl(QStringLiteral("https://evil.nz/file/STRBjZya#") + key)));
        QVERIFY(!mega::parseFileLink(QUrl(QStringLiteral("http://mega.nz/file/STRBjZya#") + key)));
        QVERIFY(!mega::parseFileLink(QUrl(QStringLiteral("https://mega.nz/file/STRBjZya#short"))));
        QVERIFY(!mega::parseFileLink(QUrl(QStringLiteral("https://mega.nz/file/../x#") + key)));
        QVERIFY(
            !mega::parseFileLink(QUrl(QStringLiteral("https://mega.nz/folder/STRBjZya#") + key)));
    }

    void parsesTransferLinks() {
        QCOMPARE(
            mega::parseTransferLink(QUrl(QStringLiteral("https://transfer.it/t/HJGrbGh1nZzI"))),
            std::optional(QStringLiteral("HJGrbGh1nZzI")));
        QVERIFY(!mega::parseTransferLink(QUrl(QStringLiteral("https://transfer.it/t/short"))));
        QVERIFY(
            !mega::parseTransferLink(QUrl(QStringLiteral("http://transfer.it/t/HJGrbGh1nZzI"))));
        QVERIFY(!mega::parseTransferLink(QUrl(QStringLiteral("https://evil.it/t/HJGrbGh1nZzI"))));
        QVERIFY(
            !mega::parseTransferLink(QUrl(QStringLiteral("https://transfer.it/x/HJGrbGh1nZzI"))));
    }

    // The listing and the download URL go to the transfer's scope (`x=`), the key is the node's
    // own, and a transfer with several videos is not guessed.
    void listsATransfer() {
        HttpTestServer server;
        QVERIFY(server.listen());
        const auto attributes = vectors().value(u"attributes").toString();
        const auto key = vectors().value(u"linkKey").toString();
        QStringList queries;
        QStringList commands;
        bool two_videos = false;
        server.route("POST", "/cs", [&](const HttpTestServer::Request& request) {
            queries.append(QString::fromLatin1(request.query));
            const auto body = QJsonDocument::fromJson(request.body).array().first().toObject();
            commands.append(body.value(u"a").toString());
            if (body.value(u"a").toString() == u"f") {
                QJsonArray nodes{QJsonObject{{QStringLiteral("h"), QStringLiteral("folder01")},
                                             {QStringLiteral("t"), 1}},
                                 QJsonObject{{QStringLiteral("h"), QStringLiteral("filenode")},
                                             {QStringLiteral("t"), 0},
                                             {QStringLiteral("s"), 3145733},
                                             {QStringLiteral("a"), attributes},
                                             {QStringLiteral("k"), key}}};
                if (two_videos) {
                    nodes.append(QJsonObject{{QStringLiteral("h"), QStringLiteral("filenod2")},
                                             {QStringLiteral("t"), 0},
                                             {QStringLiteral("s"), 3145733},
                                             {QStringLiteral("a"), attributes},
                                             {QStringLiteral("k"), key}});
                }
                return HttpTestServer::Response{
                    .body = QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("f"), nodes}}})
                                .toJson()};
            }
            if (body.value(u"n").toString() != u"filenode") {
                return HttpTestServer::Response{.body = "[-9]"};
            }
            return HttpTestServer::Response{
                .body = "[{\"s\":3145733,\"g\":\"https://gfs.example/dl/abc\"}]"};
        });
        QNetworkAccessManager network;
        mega::Api api(network, nullptr, server.url(QStringLiteral("/cs")));
        std::optional<std::expected<mega::TransferFile, mega::ApiFailure>> result;
        api.transfer(QStringLiteral("HJGrbGh1nZzI"), this,
                     [&](const auto& answer) { result = answer; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
        QVERIFY(*result);
        QCOMPARE((*result)->file.size, 3145733);
        QCOMPARE((*result)->file.name, QStringLiteral("Sousou no Frieren - 01.mp4"));
        QCOMPARE((*result)->file.url.host(), QStringLiteral("gfs.example"));
        QCOMPARE((*result)->link.handle, QStringLiteral("filenode"));
        QCOMPARE((*result)->link.key, mega::parseFileLink(linkUrl())->key);
        QCOMPARE((*result)->link.metaMac, mega::parseFileLink(linkUrl())->metaMac);
        QCOMPARE(commands, QStringList({QStringLiteral("f"), QStringLiteral("g")}));
        for (const auto& query : queries) {
            QVERIFY2(query.contains(u"x=HJGrbGh1nZzI"), qPrintable(query));
            QVERIFY(!query.contains(u"p="));
        }

        two_videos = true;
        result.reset();
        api.transfer(QStringLiteral("HJGrbGh1nZzI"), this,
                     [&](const auto& answer) { result = answer; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
        QVERIFY(!*result);
        QCOMPARE(result->error().kind, mega::ApiFailure::Kind::Protocol);

        // Over quota, as for a MEGA file.
        server.route("POST", "/cs", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.body = "[{\"e\":-17,\"tl\":600}]"};
        });
        result.reset();
        api.transfer(QStringLiteral("HJGrbGh1nZzI"), this,
                     [&](const auto& answer) { result = answer; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
        QVERIFY(!*result);
        QCOMPARE(result->error().kind, mega::ApiFailure::Kind::OverQuota);
        QCOMPARE(result->error().timeLeft, std::optional(std::chrono::seconds(600)));
    }

    void followsTheChunkLayout() {
        QCOMPARE(mega::chunkLength(0), 128 * 1024);
        QCOMPARE(mega::chunkLength(7), 1024 * 1024);
        QCOMPARE(mega::chunkLength(42), 1024 * 1024);
    }

    void matchesTheReferenceKeystream() {
        const auto v = vectors();
        const auto link = mega::parseFileLink(linkUrl());
        const animehub::platform::Aes128 aes(link->key);
        QVERIFY(aes.valid());
        auto data = referencePlaintext(v.value(u"size").toInteger());
        // Uneven, block-aligned pieces exercise the counter offset.
        const qsizetype cut1 = 16 * 7;
        const qsizetype cut2 = 16 * 70'001;
        QVERIFY(mega::applyKeystream(bytes(data, 0, cut1), 0, aes, link->nonce));
        QVERIFY(mega::applyKeystream(bytes(data, cut1, cut2 - cut1), cut1, aes, link->nonce));
        QVERIFY(mega::applyKeystream(bytes(data, cut2), cut2, aes, link->nonce));
        QCOMPARE(sha256(data), v.value(u"cipherSha256").toString());
        QVERIFY(!mega::applyKeystream(bytes(data, 0, 16), 8, aes, link->nonce));
    }

    void decryptsAndVerifiesAFile() {
        const auto v = vectors();
        const auto link = mega::parseFileLink(linkUrl());
        const animehub::platform::Aes128 aes(link->key);
        auto data = referencePlaintext(v.value(u"size").toInteger());
        QVERIFY(mega::applyKeystream(bytes(data), 0, aes, link->nonce));

        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("episode.mega"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(data);
        }
        std::atomic<bool> cancel{false};
        qint64 last_progress = 0;
        const auto plain = dir.filePath(QStringLiteral("episode.mp4"));
        const auto result = mega::decryptFile(path, plain, *link, cancel,
                                              [&](qint64 done) { last_progress = done; });
        QVERIFY(result);
        QCOMPARE(last_progress, data.size());
        QFile file(plain);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(sha256(file.readAll()), v.value(u"plainSha256").toString());
        QVERIFY(QFile::exists(path)); // the encrypted copy stays until the caller drops it
    }

    void rejectsCorruptedFiles() {
        const auto v = vectors();
        const auto link = mega::parseFileLink(linkUrl());
        const animehub::platform::Aes128 aes(link->key);
        auto data = referencePlaintext(v.value(u"size").toInteger());
        QVERIFY(mega::applyKeystream(bytes(data), 0, aes, link->nonce));
        data[2'000'000] = static_cast<char>(data[2'000'000] ^ 0x01);

        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("episode.mega"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(data);
        out.close();
        std::atomic<bool> cancel{false};
        const auto plain = dir.filePath(QStringLiteral("episode.mp4"));
        const auto result = mega::decryptFile(path, plain, *link, cancel, {});
        QVERIFY(!result);
        QCOMPARE(result.error(), mega::CryptoError::Integrity);
        QVERIFY(!QFile::exists(plain)); // nothing half-written is left behind

        std::atomic<bool> cancelled{true};
        QCOMPARE(mega::decryptFile(path, plain, *link, cancelled, {}).error(),
                 mega::CryptoError::Cancelled);
        QVERIFY(!QFile::exists(plain));
        QCOMPARE(
            mega::decryptFile(dir.filePath(QStringLiteral("missing")), plain, *link, cancel, {})
                .error(),
            mega::CryptoError::Io);
    }

    void decryptsTheFileName() {
        const auto link = mega::parseFileLink(linkUrl());
        QCOMPARE(
            mega::decryptFileName(vectors().value(u"attributes").toString().toLatin1(), link->key),
            QStringLiteral("Sousou no Frieren - 01.mp4"));
        QCOMPARE(mega::decryptFileName("bm90LWJhc2U2NA", link->key), QString());
    }

    void asksTheApiForTheFile() {
        HttpTestServer server;
        QVERIFY(server.listen());
        int calls = 0;
        const auto attributes = vectors().value(u"attributes").toString().toLatin1();
        server.route("POST", "/cs", [&](const HttpTestServer::Request& request) {
            ++calls;
            const auto body = QJsonDocument::fromJson(request.body).array().first().toObject();
            if (body.value(u"p").toString() == u"gone0000") {
                return HttpTestServer::Response{.body = "[-9]"};
            }
            if (calls == 1) {
                return HttpTestServer::Response{.body = "-3"}; // EAGAIN: try again
            }
            return HttpTestServer::Response{.body = "[{\"s\":3145733,\"at\":\"" + attributes +
                                                    "\",\"g\":\"https://gfs.example/dl/abc\"}]"};
        });
        QNetworkAccessManager network;
        mega::Api api(network, nullptr, server.url(QStringLiteral("/cs")));

        const auto link = mega::parseFileLink(linkUrl());
        std::optional<std::expected<mega::RemoteFile, mega::ApiFailure>> result;
        api.file(*link, this, [&](const auto& answer) { result = answer; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
        QVERIFY(*result);
        QCOMPARE((*result)->size, 3145733);
        QCOMPARE((*result)->name, QStringLiteral("Sousou no Frieren - 01.mp4"));
        QCOMPARE((*result)->url.host(), QStringLiteral("gfs.example"));
        QCOMPARE(calls, 2);

        auto gone = *link;
        gone.handle = QStringLiteral("gone0000");
        result.reset();
        api.file(gone, this, [&](const auto& answer) { result = answer; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(!*result);
        QCOMPARE(result->error().kind, mega::ApiFailure::Kind::NotFound);
    }

    // Over its quota, MEGA says when it frees: `tl` in the API's answer, `X-MEGA-Time-Left` on
    // the storage server's 509 (as MEGA's SDK reads them).
    void readsWhenTheQuotaFrees() {
        HttpTestServer server;
        QVERIFY(server.listen());
        server.route("POST", "/cs", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.body = "[{\"e\":-17,\"tl\":1234,\"s\":10}]"};
        });
        server.route("GET", "/dl/over", [](const HttpTestServer::Request& request) {
            return HttpTestServer::Response{
                .status = request.headers.value("range") == "bytes=0-0" ? 509 : 400,
                .headers = {{"X-MEGA-Time-Left", "  610 "}}};
        });
        server.route("GET", "/dl/plain", [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{.status = 509};
        });
        QNetworkAccessManager network;
        mega::Api api(network, nullptr, server.url(QStringLiteral("/cs")));

        std::optional<std::expected<mega::RemoteFile, mega::ApiFailure>> result;
        api.file(*mega::parseFileLink(linkUrl()), this,
                 [&](const auto& answer) { result = answer; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
        QVERIFY(!*result);
        QCOMPARE(result->error().kind, mega::ApiFailure::Kind::OverQuota);
        QCOMPARE(result->error().timeLeft, std::optional(std::chrono::seconds(1234)));

        std::optional<std::optional<std::chrono::seconds>> left;
        api.quotaTimeLeft(server.url(QStringLiteral("/dl/over")), this,
                          [&](auto seconds) { left = seconds; });
        QTRY_VERIFY_WITH_TIMEOUT(left.has_value(), 5000);
        QCOMPARE(*left, std::optional(std::chrono::seconds(610)));

        left.reset();
        api.quotaTimeLeft(server.url(QStringLiteral("/dl/plain")), this,
                          [&](auto seconds) { left = seconds; });
        QTRY_VERIFY_WITH_TIMEOUT(left.has_value(), 5000);
        QCOMPARE(*left, std::nullopt); // no time stated: none made up

        QCOMPARE(mega::quotaTimeLeft("0"), std::nullopt);
        QCOMPARE(mega::quotaTimeLeft("-5"), std::nullopt);
        QCOMPARE(mega::quotaTimeLeft("abc"), std::nullopt);
    }
};

QTEST_GUILESS_MAIN(MegaTest)
#include "mega_test.moc"
