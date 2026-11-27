#include "http_test_server.h"
#include "monocypher-ed25519.h"
#include "update/ahpack.h"
#include "update/update_manifest.h"
#include "update/zsync_delta.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <array>

using namespace animehub::update;
using animehub::testing::HttpTestServer;

namespace {

struct TestKey {
    std::array<std::uint8_t, 32> seed{};
    std::array<std::uint8_t, 64> secret{};
    Ed25519PublicKey pub{};
    QString seedHex;
    QString pubHex;
};

TestKey makeTestKey(std::uint8_t fill) {
    TestKey k;
    for (std::size_t i = 0; i < k.seed.size(); ++i) {
        k.seed[i] = static_cast<std::uint8_t>(fill + i * 7);
    }
    std::array<std::uint8_t, 32> seed_copy = k.seed;
    crypto_ed25519_key_pair(k.secret.data(), k.pub.data(), seed_copy.data());
    k.seedHex =
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(k.seed.data()), 32).toHex());
    k.pubHex =
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(k.pub.data()), 32).toHex());
    return k;
}

void writeBytes(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(data), data.size());
    f.close();
}

QByteArray readBytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

QByteArray deterministicBlob(int size, std::uint32_t seed) {
    QByteArray out(size, Qt::Uninitialized);
    std::uint32_t x = seed;
    for (int i = 0; i < size; ++i) {
        x = x * 1664525U + 1013904223U;
        out[i] = static_cast<char>((x >> 16U) & 0xFFU);
    }
    return out;
}

bool runUpdateTool(const QStringList& args, QString* output_out = nullptr) {
    const QString script = QDir(QStringLiteral(ANIMEHUB_SOURCE_DIR))
                               .filePath(QStringLiteral("scripts/update-tool.py"));
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("python"), QStringList{script} + args);
    if (!proc.waitForFinished(30'000)) {
        return false;
    }
    if (output_out != nullptr) {
        *output_out = QString::fromUtf8(proc.readAllStandardOutput());
    }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

QByteArray buildZsyncFile(const QByteArray& target_data, int block_size = 2048, int rsum_bytes = 2,
                          int checksum_bytes = 5) {
    QByteArray out;
    out += "zsync: 0.6.2\n";
    out += "Filename: AnimeHub-0.5.0-x86_64.AppImage\n";
    out += "Blocksize: " + QByteArray::number(block_size) + "\n";
    out += "Length: " + QByteArray::number(target_data.size()) + "\n";
    out += "Hash-Lengths: 1," + QByteArray::number(rsum_bytes) + "," +
           QByteArray::number(checksum_bytes) + "\n";
    out +=
        "SHA-1: " + QCryptographicHash::hash(target_data, QCryptographicHash::Sha1).toHex() + "\n";
    out += "\n";

    const qint64 num_blocks = (target_data.size() + block_size - 1) / block_size;
    for (qint64 blk = 0; blk < num_blocks; ++blk) {
        const qint64 off = blk * block_size;
        const qint64 len = std::min<qint64>(block_size, target_data.size() - off);
        QByteArray padded =
            target_data.mid(static_cast<qsizetype>(off), static_cast<qsizetype>(len));
        if (padded.size() < block_size) {
            padded.append(block_size - padded.size(), '\0');
        }
        const std::uint32_t r =
            computeZsyncRsum(reinterpret_cast<const std::uint8_t*>(padded.constData()),
                             static_cast<std::size_t>(padded.size()));
        const std::array<char, 4> r_be = {
            static_cast<char>((r >> 24U) & 0xFFU),
            static_cast<char>((r >> 16U) & 0xFFU),
            static_cast<char>((r >> 8U) & 0xFFU),
            static_cast<char>(r & 0xFFU),
        };
        out.append(r_be.data() + (4 - rsum_bytes), rsum_bytes);
        out.append(computeZsyncStrongHash(reinterpret_cast<const std::uint8_t*>(padded.constData()),
                                          static_cast<std::size_t>(padded.size()), block_size,
                                          checksum_bytes));
    }
    return out;
}

} // namespace

class AhpackTest final : public QObject {
    Q_OBJECT

private slots:
    void packDeltaHttp206AndHttp200Fallback() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const TestKey key = makeTestKey(0x2A);
        const QString key_file = temp.filePath(QStringLiteral("release.key"));
        writeBytes(key_file, key.seedHex.toLatin1());

        const QByteArray unchanged_qt = deterministicBlob(128 * 1024, 1001);
        const QByteArray unchanged_mpv = deterministicBlob(64 * 1024, 2002);
        const QByteArray exe_v1 = deterministicBlob(16 * 1024, 3001);
        const QByteArray exe_v2 = deterministicBlob(18 * 1024, 3002);
        const QByteArray qml_main_v1 = "import QtQuick\nItem { id: v1 }\n";
        const QByteArray qml_main_v2 = "import QtQuick\nItem { id: v2; property int x: 42 }\n";
        const QByteArray qml_theme =
            "pragma Singleton\nQtObject { readonly property color accent: '#2f81f7' }\n";

        // Build v1 installed folder via pack-win64 so it has a real installed-manifest.json.
        const QString v1_dir = temp.filePath(QStringLiteral("install-v1"));
        writeBytes(v1_dir + QStringLiteral("/AnimeHub.exe"), exe_v1);
        writeBytes(v1_dir + QStringLiteral("/Qt6Core.dll"), unchanged_qt);
        writeBytes(v1_dir + QStringLiteral("/engines/mpv/mpv.exe"), unchanged_mpv);
        writeBytes(v1_dir + QStringLiteral("/qml/Main.qml"), qml_main_v1);
        writeBytes(v1_dir + QStringLiteral("/qml/Theme.qml"), qml_theme);
        writeBytes(v1_dir + QStringLiteral("/obsolete.dll"), QByteArray("legacy-dll-bytes"));

        HttpTestServer server;
        QVERIFY(server.listen());
        const QString base_url = server.url(QStringLiteral("/releases")).toString();

        QString tool_out;
        QVERIFY2(
            runUpdateTool({QStringLiteral("pack-win64"), QStringLiteral("--stage"), v1_dir,
                           QStringLiteral("--version"), QStringLiteral("0.4.0"),
                           QStringLiteral("--release-seq"), QStringLiteral("4"),
                           QStringLiteral("--out-dir"), temp.filePath(QStringLiteral("out-v1")),
                           QStringLiteral("--base-url"), base_url, QStringLiteral("--key-file"),
                           key_file, QStringLiteral("--key-id"), QStringLiteral("release-test"),
                           QStringLiteral("--zstd-level"), QStringLiteral("3")},
                          &tool_out),
            qPrintable(tool_out));

        // Build v2 release pack (AnimeHub.exe + qml/Main.qml changed; obsolete.dll removed).
        const QString v2_dir = temp.filePath(QStringLiteral("stage-v2"));
        writeBytes(v2_dir + QStringLiteral("/AnimeHub.exe"), exe_v2);
        writeBytes(v2_dir + QStringLiteral("/Qt6Core.dll"), unchanged_qt);
        writeBytes(v2_dir + QStringLiteral("/engines/mpv/mpv.exe"), unchanged_mpv);
        writeBytes(v2_dir + QStringLiteral("/qml/Main.qml"), qml_main_v2);
        writeBytes(v2_dir + QStringLiteral("/qml/Theme.qml"), qml_theme);

        const QString out_v2 = temp.filePath(QStringLiteral("out-v2"));
        QVERIFY2(runUpdateTool({QStringLiteral("pack-win64"), QStringLiteral("--stage"), v2_dir,
                                QStringLiteral("--version"), QStringLiteral("0.5.0"),
                                QStringLiteral("--release-seq"), QStringLiteral("5"),
                                QStringLiteral("--out-dir"), out_v2, QStringLiteral("--base-url"),
                                base_url, QStringLiteral("--key-file"), key_file,
                                QStringLiteral("--key-id"), QStringLiteral("release-test"),
                                QStringLiteral("--zstd-level"), QStringLiteral("3")},
                               &tool_out),
                 qPrintable(tool_out));

        // Also smoke-test `update-tool.py verify`.
        QVERIFY2(runUpdateTool({QStringLiteral("verify"), QStringLiteral("--manifest"),
                                out_v2 + QStringLiteral("/update-win64.json"),
                                QStringLiteral("--pubkey"), key.pubHex},
                               &tool_out),
                 qPrintable(tool_out));

        // Parse & verify v2 manifest with C++ Monocypher verifier.
        const KeyRing ring{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                               .publicKey = key.pub,
                                               .isRecovery = false}}};
        UrlPolicy policy;
        policy.allowInsecureLoopback = true;
        const auto manifest =
            verifyAndParseManifestEnvelope(readBytes(out_v2 + QStringLiteral("/update-win64.json")),
                                           ring, {}, QStringLiteral("win64"), policy);
        QVERIFY2(manifest.has_value(),
                 manifest.has_value() ? "" : qPrintable(manifest.error().message));

        // Diff v1_dir against v2 manifest: Qt6Core.dll and mpv.exe MUST be skipped!
        const auto diff = computeAhpackDiff(v1_dir, *manifest);
        QVERIFY(diff.has_value());
        QCOMPARE(diff->entriesToDownload.size(), 2); // AnimeHub.exe + solid-tar qml
        QCOMPARE(diff->coalescedRanges.size(), 1);   // contiguous at start of pack!
        QCOMPARE(diff->filesToDelete, QStringList{QStringLiteral("obsolete.dll")});
        QVERIFY(diff->downloadBytes < manifest->pack->size / 4);

        const QByteArray pack_bytes =
            readBytes(out_v2 + QStringLiteral("/AnimeHub-0.5.0-win64.ahpack"));
        server.file("/releases/AnimeHub-0.5.0-win64.ahpack", pack_bytes);

        // 1) HTTP 206 single-range delta download.
        QNetworkAccessManager network;
        AhpackDownloader downloader(network, policy);
        QSignalSpy finished_spy(&downloader, &AhpackDownloader::finished);
        const QString staging_206 = temp.filePath(QStringLiteral("staging-206"));
        downloader.start(*manifest, *diff, staging_206);
        QVERIFY(finished_spy.wait(10'000));
        QCOMPARE(finished_spy.count(), 1);
        QVERIFY2(finished_spy[0][0].toBool(), qPrintable(finished_spy[0][2].toString()));
        QCOMPARE(readBytes(staging_206 + QStringLiteral("/AnimeHub.exe")), exe_v2);
        QCOMPARE(readBytes(staging_206 + QStringLiteral("/qml/Main.qml")), qml_main_v2);
        QCOMPARE(readBytes(staging_206 + QStringLiteral("/qml/Theme.qml")), qml_theme);
        // Verify HTTP Range header was sent and only 1 request was made!
        QCOMPARE(server.count("/releases/AnimeHub-0.5.0-win64.ahpack"), 1);
        QVERIFY(!server.requests().last().headers.value("range").isEmpty());

        // 2) HTTP 200 OK streaming fallback (proxy ignores Range).
        server.route("GET", "/releases/AnimeHub-0.5.0-win64.ahpack",
                     [pack_bytes](const HttpTestServer::Request&) {
                         return HttpTestServer::Response{
                             .status = 200,
                             .body = pack_bytes,
                             .headers = {{"Content-Type", "application/octet-stream"}},
                             .bytesPerTick = 8192,
                         };
                     });
        finished_spy.clear();
        const QString staging_200 = temp.filePath(QStringLiteral("staging-200"));
        downloader.start(*manifest, *diff, staging_200);
        QVERIFY(finished_spy.wait(10'000));
        QCOMPARE(finished_spy.count(), 1);
        QVERIFY2(finished_spy[0][0].toBool(), qPrintable(finished_spy[0][2].toString()));
        QCOMPARE(readBytes(staging_200 + QStringLiteral("/AnimeHub.exe")), exe_v2);
        QCOMPARE(readBytes(staging_200 + QStringLiteral("/qml/Main.qml")), qml_main_v2);

        // 3) Tampered pack byte -> SHA-256 / zstd decode rejection.
        QByteArray bad_pack = pack_bytes;
        bad_pack[10] = static_cast<char>(bad_pack[10] ^ 0xFF);
        server.file("/releases/AnimeHub-0.5.0-win64.ahpack", bad_pack);
        finished_spy.clear();
        downloader.start(*manifest, *diff, temp.filePath(QStringLiteral("staging-bad")));
        QVERIFY(finished_spy.wait(10'000));
        QCOMPARE(finished_spy.count(), 1);
        QVERIFY(!finished_spy[0][0].toBool());
    }

    void zsyncDeltaMatchesAndVerifiesSha256() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        // Construct v1 (192 KiB) and v2 (192 KiB) sharing >90% of blocks across a 512-byte shift,
        // with unchanged gaps > 32 KiB so coalescing keeps missing ranges tightly bounded.
        const QByteArray common_a = deterministicBlob(80 * 1024, 5001);
        const QByteArray changed_v1 = deterministicBlob(8 * 1024, 5002);
        const QByteArray changed_v2 = deterministicBlob(8 * 1024, 5003);
        const QByteArray common_b = deterministicBlob(104 * 1024, 5004);

        const QByteArray v1 = common_a + changed_v1 + common_b;
        const QByteArray v2 =
            QByteArray(512, 'Z') + common_a + changed_v2 + common_b.left(104 * 1024 - 512);
        QCOMPARE(v2.size(), 192 * 1024);

        const QString seed_path = temp.filePath(QStringLiteral("old.AppImage"));
        writeBytes(seed_path, v1);

        const QByteArray zsync_bytes = buildZsyncFile(v2, 2048, 2, 5);
        const auto parsed = parseZsyncControlFile(zsync_bytes);
        QVERIFY(parsed.has_value());
        const auto plan = planZsyncDelta(*parsed, seed_path);
        QVERIFY(plan.has_value());
        QVERIFY2(plan->matchedBlocks >= 85,
                 qPrintable(QStringLiteral("matched=%1").arg(plan->matchedBlocks)));
        QVERIFY2(plan->downloadBytes < v2.size() / 4,
                 qPrintable(QStringLiteral("downloadBytes=%1 total=%2")
                                .arg(plan->downloadBytes)
                                .arg(v2.size())));

        HttpTestServer server;
        QVERIFY(server.listen());
        server.file("/AnimeHub-0.5.0-x86_64.AppImage", v2);
        server.file("/AnimeHub-0.5.0-x86_64.AppImage.zsync", zsync_bytes);

        UpdateArtifact art;
        art.filename = QStringLiteral("AnimeHub-0.5.0-x86_64.AppImage");
        art.size = v2.size();
        art.sha256 = QCryptographicHash::hash(v2, QCryptographicHash::Sha256).toHex();
        art.urls = {server.url(QStringLiteral("/AnimeHub-0.5.0-x86_64.AppImage"))};
        art.zsyncUrls = {server.url(QStringLiteral("/AnimeHub-0.5.0-x86_64.AppImage.zsync"))};

        UrlPolicy policy;
        policy.allowInsecureLoopback = true;
        QNetworkAccessManager network;
        ZsyncDownloader downloader(network, policy);
        QSignalSpy done_spy(&downloader, &ZsyncDownloader::finished);

        const QString out_path = temp.filePath(QStringLiteral("new.AppImage"));
        downloader.start(art, seed_path, out_path);
        QVERIFY(done_spy.wait(10'000));
        QCOMPARE(done_spy.count(), 1);
        QVERIFY2(done_spy[0][0].toBool(), qPrintable(done_spy[0][3].toString()));
        QCOMPARE(done_spy[0][2].toBool(), true); // used_delta == true
        QCOMPARE(readBytes(out_path), v2);
    }
};

QTEST_GUILESS_MAIN(AhpackTest)
#include "ahpack_test.moc"
