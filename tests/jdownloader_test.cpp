#include "downloads/jdownloader.h"
#include "http_test_server.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub::downloads;
using animehub::testing::HttpTestServer;

class JDownloaderTest final : public QObject {
    Q_OBJECT
private slots:
    void onlyNumericLoopbackWithoutCredentialsOrQuery() {
        QVERIFY(jd::isLocalEndpoint(QUrl(QStringLiteral("http://127.0.0.1:3128"))));
        QVERIFY(jd::isLocalEndpoint(QUrl(QStringLiteral("http://[::1]:3128/"))));
        for (const auto* url :
             {"http://example.test:3128", "http://192.168.1.1:3128", "https://127.0.0.1:3128",
              "http://user:secret@127.0.0.1:3128", "http://127.0.0.1:3128?secret=value",
              "http://127.0.0.1:3128/path"}) {
            QVERIFY(!jd::isLocalEndpoint(QUrl(QString::fromLatin1(url))));
        }
    }

    void localWireUsesPositionalJsonAndRejectsMissingEnvelope() {
        HttpTestServer server;
        QVERIFY(server.listen());
        QByteArray query;
        server.route("GET", "/downloadsV2/forceDownload", [&](const auto& request) {
            query = request.query;
            return HttpTestServer::Response{.body = "{\"data\":null}"};
        });
        QNetworkAccessManager network;
        Task task;
        task.engine = DownloadEngine::JDownloaderLocal;
        task.engineEndpoint = server.url().toString();
        bool done = false, ok = false;
        jd::request(network, nullptr, task, QStringLiteral("/downloadsV2/forceDownload"),
                    {QJsonArray{qint64(456)}, QJsonArray{}}, this, [&](const auto& result) {
                        done = true;
                        ok = result.has_value();
                    });
        QTRY_VERIFY(done);
        QVERIFY(ok);
        QCOMPARE(QUrl::fromPercentEncoding(query), QStringLiteral("[456]&[]"));
        server.json("/downloadsV2/queryLinks", "{\"unexpected\":true}");
        done = false;
        jd::request(network, nullptr, task, QStringLiteral("/downloadsV2/queryLinks"), {}, this,
                    [&](const auto& result) {
                        done = true;
                        ok = result.has_value();
                    });
        QTRY_VERIFY(done);
        QVERIFY(!ok);
    }

    void refusesRemoteEndpointWithoutSending() {
        QNetworkAccessManager network;
        Task task;
        task.engine = DownloadEngine::JDownloaderLocal;
        task.engineEndpoint = QStringLiteral("http://192.0.2.1:3128");
        bool done = false;
        jd::request(network, nullptr, task, QStringLiteral("/linkgrabberv2/addLinks"), {}, this,
                    [&](const auto& result) {
                        done = true;
                        QVERIFY(!result);
                    });
        QVERIFY(done);
    }

    void mediaMustExistBeNonemptyAndHavePlayableExtension() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        for (const auto* name : {"episode.mkv", "empty.mp4", "archive.rar", "episode.mp4.part"}) {
            QFile file(root.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            if (QByteArray(name) != "empty.mp4")
                file.write("fixture");
        }
        const auto files = jd::verifiedMedia(root.path());
        QCOMPARE(files.size(), 1);
        QCOMPARE(QFileInfo(files.first()).fileName(), QStringLiteral("episode.mkv"));
        QVERIFY(jd::verifiedMedia(root.filePath(QStringLiteral("missing"))).isEmpty());
    }
};
QTEST_GUILESS_MAIN(JDownloaderTest)
#include "jdownloader_test.moc"
