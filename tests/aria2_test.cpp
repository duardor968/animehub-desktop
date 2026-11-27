#include "downloads/aria2.h"
#include "http_test_server.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace animehub::downloads;
using animehub::testing::HttpTestServer;

class Aria2Test final : public QObject {
    Q_OBJECT

private slots:
    void nativeTransfersShareALimitThatCanBeRemovedWhileRunning() {
        HttpTestServer server;
        QVERIFY(server.listen());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray payload(2 * 1024 * 1024, 'x');
        server.file("/episode", payload);
        QNetworkAccessManager network;
        Aria2 engine(network, QStringLiteral(ANIMEHUB_ARIA2_EXECUTABLE), "AnimeHub-Test");
        QSignalSpy ready(&engine, &Aria2::ready);
        QSignalSpy errors(&engine, &Aria2::speedLimitFailed);
        engine.setSpeedLimit(64'000);
        engine.start();
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);

        QStringList gids;
        for (int i = 0; i < 2; ++i) {
            engine.add({.url = server.url(QStringLiteral("/episode")),
                        .directory = dir.path(),
                        .fileName = QString::number(i),
                        .connections = 1},
                       this, [&](const auto& result) {
                           QVERIFY(result);
                           gids.append(*result);
                       });
        }
        QTRY_COMPARE(gids.size(), 2);
        QTRY_COMPARE(server.count("/episode"), 2);
        const auto transferred = [&] {
            QObject receiver;
            std::optional<qint64> bytes;
            engine.status(gids, &receiver, [&](const auto& result) {
                if (!result) {
                    bytes = -1;
                    return;
                }
                qint64 total = 0;
                for (const auto& entry : *result)
                    total += entry.completedBytes;
                bytes = total;
            });
            if (!QTest::qWaitFor([&] { return bytes.has_value(); }, 2000)) {
                return qint64{-1};
            }
            return *bytes;
        };
        QTRY_VERIFY_WITH_TIMEOUT(transferred() > 0, 4000);
        const auto baseline = transferred();
        QTest::qWait(3200);
        const auto delta = transferred() - baseline;
        QVERIFY2(delta > 0 && delta < 360'000, qPrintable(QString::number(delta)));
        engine.setSpeedLimit(0);
        QTRY_COMPARE_WITH_TIMEOUT(transferred(), payload.size() * 2, 7000);
        QVERIFY(errors.isEmpty());

        bool stopped = false;
        engine.shutdown([&] { stopped = true; }, false);
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 2000);
    }

    void clampsNegativeSpeedLimitsToZero() {
        QNetworkAccessManager network;
        Aria2 engine(network, QStringLiteral(ANIMEHUB_ARIA2_EXECUTABLE), "AnimeHub-Test");
        QSignalSpy ready(&engine, &Aria2::ready);
        QSignalSpy failed(&engine, &Aria2::failed);
        QSignalSpy limit_errors(&engine, &Aria2::speedLimitFailed);

        QVERIFY(!engine.isActive());
        engine.setSpeedLimit(-1024);
        QCOMPARE(engine.speedLimit(), 0);
        engine.start();
        QVERIFY(engine.isActive());
        QTRY_VERIFY2_WITH_TIMEOUT(ready.size() == 1 || failed.size() == 1, "no answer", 10000);
        QVERIFY2(failed.isEmpty(),
                 failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QVERIFY(engine.isReady());
        QVERIFY(!engine.isStarting());

        engine.setSpeedLimit(-50);
        QCOMPARE(engine.speedLimit(), 0);
        QCoreApplication::processEvents();
        QVERIFY(limit_errors.isEmpty());

        bool stopped = false;
        engine.shutdown([&] { stopped = true; }, false);
        QTRY_VERIFY_WITH_TIMEOUT(stopped, 2000);
        QVERIFY(!engine.isActive());
    }

    void transfersAFileOverRpc() {
        HttpTestServer server;
        QVERIFY(server.listen());
        QByteArray data(700 * 1024, 'x');
        for (qsizetype i = 0; i < data.size(); ++i) {
            data[i] = static_cast<char>(i % 251);
        }
        server.file("/file.bin", data);

        QNetworkAccessManager network;
        Aria2 aria2(network, QStringLiteral(ANIMEHUB_ARIA2_EXECUTABLE), "AnimeHub-Test");
        QSignalSpy ready(&aria2, &Aria2::ready);
        QSignalSpy failed(&aria2, &Aria2::failed);
        aria2.start();
        QTRY_VERIFY2_WITH_TIMEOUT(ready.size() == 1 || failed.size() == 1, "no answer", 10000);
        QVERIFY2(failed.isEmpty(),
                 failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));

        QTemporaryDir dir;
        std::optional<std::expected<QString, QString>> gid;
        aria2.add({.url = server.url(QStringLiteral("/file.bin")),
                   .directory = dir.path(),
                   .fileName = QStringLiteral("file.bin.part")},
                  this, [&](const auto& result) { gid = result; });
        QTRY_VERIFY(gid.has_value());
        QVERIFY2(*gid, gid->has_value() ? "" : qPrintable(gid->error()));

        std::optional<Aria2::Transfer> last;
        QString error;
        QDeadlineTimer deadline(10'000);
        while (!deadline.hasExpired() &&
               !(last && last->status == Aria2::Transfer::Status::Complete)) {
            bool answered = false;
            aria2.status({**gid}, this, [&](const auto& result) {
                answered = true;
                if (!result) {
                    error = result.error();
                    return;
                }
                last = result->first();
            });
            QTRY_VERIFY_WITH_TIMEOUT(answered, 2000);
            QTest::qWait(100);
        }
        QVERIFY2(last && last->status == Aria2::Transfer::Status::Complete,
                 qPrintable(QStringLiteral("status %1 done %2 code %3 %4 rpc %5")
                                .arg(last ? static_cast<int>(last->status) : -1)
                                .arg(last ? last->completedBytes : -1)
                                .arg(last ? last->errorCode : -1)
                                .arg(last ? last->errorMessage : QString(), error)));
        QFile file(dir.filePath(QStringLiteral("file.bin.part")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), data);

        bool stopped = false;
        aria2.shutdown([&] { stopped = true; });
        QTRY_VERIFY(stopped);
    }
};

QTEST_GUILESS_MAIN(Aria2Test)
#include "aria2_test.moc"
