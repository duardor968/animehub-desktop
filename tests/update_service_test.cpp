#include "http_test_server.h"
#include "monocypher-ed25519.h"
#include "update/ahpack.h"
#include "update/update_service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <array>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace animehub::update;
using animehub::testing::HttpTestServer;

namespace {

struct TestKey {
    std::array<std::uint8_t, 32> seed{};
    std::array<std::uint8_t, 64> secret{};
    Ed25519PublicKey pub{};
    QString seedHex;
};

TestKey makeTestKey(std::uint8_t fill) {
    TestKey k;
    for (std::size_t i = 0; i < k.seed.size(); ++i) {
        k.seed[i] = static_cast<std::uint8_t>(fill + i * 11);
    }
    std::array<std::uint8_t, 32> seed_copy = k.seed;
    crypto_ed25519_key_pair(k.secret.data(), k.pub.data(), seed_copy.data());
    k.seedHex =
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(k.seed.data()), 32).toHex());
    return k;
}

void writeFileBytes(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(data), data.size());
    f.close();
}

QByteArray readFileBytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
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

bool createSampleSqliteDb(const QString& db_path, const QString& marker_value) {
    const QString conn = QStringLiteral("test-db-") + QUuid::createUuid().toString(QUuid::Id128);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(db_path);
        if (db.open()) {
            QSqlQuery q(db);
            ok = q.exec(QStringLiteral("CREATE TABLE kv (k TEXT PRIMARY KEY, v TEXT NOT NULL)")) &&
                 q.prepare(QStringLiteral("INSERT INTO kv(k, v) VALUES ('state', ?)"));
            if (ok) {
                q.addBindValue(marker_value);
                ok = q.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

QString readSampleSqliteMarker(const QString& db_path) {
    const QString conn = QStringLiteral("test-db-r-") + QUuid::createUuid().toString(QUuid::Id128);
    QString out;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(db_path);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT v FROM kv WHERE k = 'state'")) && q.next()) {
                out = q.value(0).toString();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return out;
}

// Handles `--post-update-verify <token>` when this test binary is copied as a fake AnimeHub.exe
// inside a QTemporaryDir during real AnimeHubUpdater.exe end-to-end swap & rollback tests.
int maybeRunStubVerifyMode(int argc, char** argv) {
    QString token;
    QString data_dir;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--post-update-verify" && i + 1 < argc) {
            token = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = QString::fromLocal8Bit(argv[++i]);
        }
    }
    if (token.isEmpty()) {
        return -1; // normal Qt Test execution
    }
    QCoreApplication app(argc, argv);
    const QString behavior_path =
        QDir(app.applicationDirPath()).filePath(QStringLiteral("stub-behavior.txt"));
    const QByteArray behavior = readFileBytes(behavior_path).trimmed();
    if (behavior == "crash-after-db-migrate") {
        if (!data_dir.isEmpty()) {
            const QString db = QDir(data_dir).filePath(QStringLiteral("library.sqlite3"));
            QFile f(db);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write("CORRUPTED_MIGRATED_DB_V050");
                f.close();
            }
            QFile wal(db + QStringLiteral("-wal"));
            if (wal.open(QIODevice::WriteOnly)) {
                wal.write("STALE_WAL");
                wal.close();
            }
            QFile shm(db + QStringLiteral("-shm"));
            if (shm.open(QIODevice::WriteOnly)) {
                shm.write("STALE_SHM");
                shm.close();
            }
            QFile settings(QDir(data_dir).filePath(QStringLiteral("settings.ini")));
            if (settings.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                settings.write("broken=true\n");
                settings.close();
            }
        }
        return 42; // crash without signaling healthy event!
    }
    // Default healthy behavior: signal Local\AnimeHubUpdateHealthy_<token>.
#ifdef Q_OS_WIN
    const std::wstring name =
        (QStringLiteral("Local\\AnimeHubUpdateHealthy_") + token).toStdWString();
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (ev != nullptr) {
        SetEvent(ev);
        CloseHandle(ev);
    }
#endif
    return 0;
}

} // namespace

class UpdateServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void apiPausedKillSwitchHonoredWithoutFallback() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        HttpTestServer server;
        QVERIFY(server.listen());
        server.json("/api/v1/desktop/releases/latest",
                    R"({"channel":"stable","paused":true,"minSupportedVersion":"0.3.0"})");
        server.json("/github/update-win64.json", R"({"schemaVersion":1})");

        InstallContext ctx;
        ctx.installDir = temp.filePath(QStringLiteral("app"));
        ctx.dataDir = temp.filePath(QStringLiteral("data"));
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.githubFallbackUrl = server.url(QStringLiteral("/github/update-win64.json"));
        ctx.urlPolicy.allowInsecureLoopback = true;

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        QSignalSpy check_spy(&svc, &UpdateService::checkFinished);
        svc.checkNow(true);
        QVERIFY(check_spy.wait(5'000));
        QVERIFY(svc.isPaused());
        QVERIFY(!svc.updateAvailable());
        QCOMPARE(svc.state(), UpdateState::Idle);
        // Critical: paused API MUST NOT fall back to GitHub!
        QCOMPARE(server.count("/github/update-win64.json"), 0);
    }

    void githubFallbackRefusesSilentAutoApplyAndSupportsCancelRetry() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const TestKey key = makeTestKey(0x6C);
        const QString key_file = temp.filePath(QStringLiteral("rel.key"));
        writeFileBytes(key_file, key.seedHex.toLatin1());

        const QString install_dir = temp.filePath(QStringLiteral("install"));
        const QString stage_v2 = temp.filePath(QStringLiteral("stage-v2"));
        writeFileBytes(install_dir + QStringLiteral("/AnimeHub.exe"), QByteArray(16 * 1024, '1'));
        // Use incompressible pseudo-random bytes so the zstd pack spans multiple ticks and stalls
        // cleanly mid-transfer until cancelDownload() runs.
        QByteArray stage_payload(16 * 1024, Qt::Uninitialized);
        std::uint32_t prng = 0x12345678U;
        for (qsizetype i = 0; i < stage_payload.size(); ++i) {
            prng = prng * 1664525U + 1013904223U;
            stage_payload[i] = static_cast<char>((prng >> 16U) & 0xFFU);
        }
        writeFileBytes(stage_v2 + QStringLiteral("/AnimeHub.exe"), stage_payload);

        HttpTestServer server;
        QVERIFY(server.listen());
        const QString out_v2 = temp.filePath(QStringLiteral("out-v2"));
        QString tool_out;
        QVERIFY2(runUpdateTool({QStringLiteral("pack-win64"), QStringLiteral("--stage"), stage_v2,
                                QStringLiteral("--version"), QStringLiteral("0.5.0"),
                                QStringLiteral("--release-seq"), QStringLiteral("5"),
                                QStringLiteral("--out-dir"), out_v2, QStringLiteral("--base-url"),
                                server.url(QStringLiteral("/dl")).toString(),
                                QStringLiteral("--key-file"), key_file, QStringLiteral("--key-id"),
                                QStringLiteral("release-test"), QStringLiteral("--zstd-level"),
                                QStringLiteral("1")},
                               &tool_out),
                 qPrintable(tool_out));

        const QByteArray env_bytes = readFileBytes(out_v2 + QStringLiteral("/update-win64.json"));
        const QByteArray pack_bytes =
            readFileBytes(out_v2 + QStringLiteral("/AnimeHub-0.5.0-win64.ahpack"));
        QVERIFY(pack_bytes.size() > 4096);

        // API returns 503 -> triggers GitHub fallback.
        server.json("/api/v1/desktop/releases/latest", R"({"error":"down"})", 503);
        server.json("/github/update-win64.json", env_bytes);
        server.file("/dl/AnimeHub-0.5.0-win64.ahpack", pack_bytes, 512, 1024);

        InstallContext ctx;
        ctx.installDir = install_dir;
        ctx.dataDir = temp.filePath(QStringLiteral("data"));
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.githubFallbackUrl = server.url(QStringLiteral("/github/update-win64.json"));
        ctx.keyRing = KeyRing{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                                  .publicKey = key.pub,
                                                  .isRecovery = false}}};
        ctx.urlPolicy.allowInsecureLoopback = true;

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        svc.setUpdateMode(UpdateMode::Auto);

        // Background check via GitHub fallback MUST NOT silently auto-download!
        QSignalSpy check_spy(&svc, &UpdateService::checkFinished);
        svc.checkNow(false);
        QVERIFY(check_spy.wait(5'000));
        QCOMPARE(svc.lastCheckSource(), CheckSource::GitHubFallback);
        QCOMPARE(svc.state(), UpdateState::Available);
        QCOMPARE(svc.availableVersion(), QStringLiteral("0.5.0"));

        // Explicit download -> emit progress -> cancel -> retry to Ready.
        QSignalSpy progress_spy(&svc, &UpdateService::progressChanged);
        QSignalSpy ready_spy(&svc, &UpdateService::readyToApply);
        svc.downloadNow();
        QCOMPARE(svc.state(), UpdateState::Downloading);
        QVERIFY(progress_spy.wait(5'000));
        svc.cancelDownload();
        QCOMPARE(svc.state(), UpdateState::Available);

        // Serve fast for retry.
        server.file("/dl/AnimeHub-0.5.0-win64.ahpack", pack_bytes);
        svc.downloadNow();
        QVERIFY(ready_spy.wait(5'000));
        QCOMPARE(svc.state(), UpdateState::Ready);

        // Even when Ready in Auto mode, GitHubFallback MUST refuse silent applyOnQuitIfReady()!
        QVERIFY(!svc.applyOnQuitIfReady());
    }

    void pendingEnginesDeferredWhenMpvRunningAndConsumedLater() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const TestKey key = makeTestKey(0x7D);
        const QString key_file = temp.filePath(QStringLiteral("rel.key"));
        writeFileBytes(key_file, key.seedHex.toLatin1());

        const QString install_dir = temp.filePath(QStringLiteral("install"));
        const QString data_dir = temp.filePath(QStringLiteral("data"));
        const QString stage_v2 = temp.filePath(QStringLiteral("stage-v2"));
        writeFileBytes(install_dir + QStringLiteral("/AnimeHub.exe"), QByteArray("exe-v1"));
        writeFileBytes(install_dir + QStringLiteral("/engines/mpv/mpv.exe"), QByteArray("mpv-v1"));
        writeFileBytes(stage_v2 + QStringLiteral("/AnimeHub.exe"), QByteArray("exe-v2"));
        writeFileBytes(stage_v2 + QStringLiteral("/engines/mpv/mpv.exe"), QByteArray("mpv-v2"));

        HttpTestServer server;
        QVERIFY(server.listen());
        const QString out_v2 = temp.filePath(QStringLiteral("out-v2"));
        QString tool_out;
        QVERIFY2(runUpdateTool({QStringLiteral("pack-win64"), QStringLiteral("--stage"), stage_v2,
                                QStringLiteral("--version"), QStringLiteral("0.5.0"),
                                QStringLiteral("--release-seq"), QStringLiteral("5"),
                                QStringLiteral("--out-dir"), out_v2, QStringLiteral("--base-url"),
                                server.url(QStringLiteral("/dl")).toString(),
                                QStringLiteral("--key-file"), key_file, QStringLiteral("--key-id"),
                                QStringLiteral("release-test"), QStringLiteral("--zstd-level"),
                                QStringLiteral("1")},
                               &tool_out),
                 qPrintable(tool_out));

        const QJsonObject api_resp{
            {QStringLiteral("channel"), QStringLiteral("stable")},
            {QStringLiteral("paused"), false},
            {QStringLiteral("manifests"),
             QJsonObject{
                 {QStringLiteral("win64"),
                  QJsonObject{{QStringLiteral("envelope"),
                               QJsonDocument::fromJson(
                                   readFileBytes(out_v2 + QStringLiteral("/update-win64.json")))
                                   .object()}}}}},
        };
        server.json("/api/v1/desktop/releases/latest",
                    QJsonDocument(api_resp).toJson(QJsonDocument::Compact));
        server.file("/dl/AnimeHub-0.5.0-win64.ahpack",
                    readFileBytes(out_v2 + QStringLiteral("/AnimeHub-0.5.0-win64.ahpack")));

        bool mpv_running = true;
        QString launched_helper;
        InstallContext ctx;
        ctx.installDir = install_dir;
        ctx.dataDir = data_dir;
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.keyRing = KeyRing{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                                  .publicKey = key.pub,
                                                  .isRecovery = false}}};
        ctx.urlPolicy.allowInsecureLoopback = true;
        ctx.hooks.isMpvRunning = [&](const QString&) { return mpv_running; };
        ctx.hooks.launchUpdater = [&](const QString& helper, const QStringList&) {
            launched_helper = helper;
            return std::expected<void, UpdateError>{};
        };

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        QSignalSpy ready_spy(&svc, &UpdateService::readyToApply);
        svc.checkNow(false); // Auto mode + API source -> auto downloads!
        QVERIFY(ready_spy.wait(5'000));

        QVERIFY(svc.applyAndRestart());
        QVERIFY(!launched_helper.isEmpty());
        // mpv.exe MUST have been deferred into .update/pending-engines and removed from
        // filesToInstall!
        QVERIFY(QFileInfo::exists(install_dir +
                                  QStringLiteral("/.update/pending-engines/engines/mpv/mpv.exe")));
        QCOMPARE(readFileBytes(install_dir + QStringLiteral("/engines/mpv/mpv.exe")),
                 QByteArray("mpv-v1"));
        for (const auto& f : svc.preparedPlan()->filesToInstall) {
            QVERIFY(!f.relativePath.startsWith(QLatin1StringView("engines/mpv/")));
        }

        // Still running -> consumePendingEnginesOnStartup() leaves it alone.
        QVERIFY(!svc.consumePendingEnginesOnStartup());
        QCOMPARE(readFileBytes(install_dir + QStringLiteral("/engines/mpv/mpv.exe")),
                 QByteArray("mpv-v1"));

        // Once mpv exits -> consumePendingEnginesOnStartup() installs it!
        mpv_running = false;
        QVERIFY(svc.consumePendingEnginesOnStartup());
        QCOMPARE(readFileBytes(install_dir + QStringLiteral("/engines/mpv/mpv.exe")),
                 QByteArray("mpv-v2"));
        QVERIFY(!QDir(install_dir + QStringLiteral("/.update/pending-engines")).exists());
    }

#ifdef Q_OS_WIN
    void win32UpdaterEndToEndSwapAndCrashRollback() {
        const QString updater_exe = QStringLiteral(ANIMEHUB_UPDATER_EXECUTABLE);
        QVERIFY2(QFileInfo::exists(updater_exe), qPrintable(updater_exe));
        const QString stub_src = QCoreApplication::applicationFilePath();

        // Helper lambda to run a full staged update through the REAL AnimeHubUpdater.exe.
        auto runScenario = [&](const QString& stub_behavior, int expected_exit, QTemporaryDir& temp,
                               QString& install_dir, QString& data_dir) {
            QVERIFY(temp.isValid());
            install_dir = temp.filePath(QStringLiteral("install"));
            data_dir = temp.filePath(QStringLiteral("data"));
            QDir().mkpath(install_dir);
            QDir().mkpath(data_dir);

            const TestKey key = makeTestKey(0x91);
            const QString key_file = temp.filePath(QStringLiteral("rel.key"));
            writeFileBytes(key_file, key.seedHex.toLatin1());

            // Seed v1 install with real stub executable as AnimeHub.exe + real SQLite DB +
            // settings.
            QVERIFY(QFile::copy(stub_src, install_dir + QStringLiteral("/AnimeHub.exe")));
            writeFileBytes(install_dir + QStringLiteral("/resource.txt"), QByteArray("version-1"));
            writeFileBytes(install_dir + QStringLiteral("/obsolete.txt"), QByteArray("remove-me"));
            writeFileBytes(install_dir + QStringLiteral("/stub-behavior.txt"),
                           QByteArray("healthy"));
            QVERIFY(createSampleSqliteDb(data_dir + QStringLiteral("/library.sqlite3"),
                                         QStringLiteral("v1-intact-row")));
            writeFileBytes(data_dir + QStringLiteral("/settings.ini"),
                           QByteArray("theme=dark\nversion=1\n"));

            HttpTestServer server;
            QVERIFY(server.listen());
            QString tool_out;
            // Pack v1 once so install_dir receives installed-manifest.json.
            QVERIFY2(
                runUpdateTool({QStringLiteral("pack-win64"), QStringLiteral("--stage"), install_dir,
                               QStringLiteral("--version"), QStringLiteral("0.4.0"),
                               QStringLiteral("--release-seq"), QStringLiteral("4"),
                               QStringLiteral("--out-dir"), temp.filePath(QStringLiteral("out-v1")),
                               QStringLiteral("--base-url"),
                               server.url(QStringLiteral("/dl")).toString(),
                               QStringLiteral("--key-file"), key_file, QStringLiteral("--key-id"),
                               QStringLiteral("release-test"), QStringLiteral("--zstd-level"),
                               QStringLiteral("1")},
                              &tool_out),
                qPrintable(tool_out));

            // Prepare v2 stage.
            const QString stage_v2 = temp.filePath(QStringLiteral("stage-v2"));
            QDir().mkpath(stage_v2);
            QVERIFY(QFile::copy(stub_src, stage_v2 + QStringLiteral("/AnimeHub.exe")));
            writeFileBytes(stage_v2 + QStringLiteral("/resource.txt"), QByteArray("version-2"));
            writeFileBytes(stage_v2 + QStringLiteral("/added.txt"), QByteArray("brand-new"));
            writeFileBytes(stage_v2 + QStringLiteral("/stub-behavior.txt"), stub_behavior.toUtf8());

            const QString out_v2 = temp.filePath(QStringLiteral("out-v2"));
            QVERIFY2(
                runUpdateTool({QStringLiteral("pack-win64"), QStringLiteral("--stage"), stage_v2,
                               QStringLiteral("--version"), QStringLiteral("0.5.0"),
                               QStringLiteral("--release-seq"), QStringLiteral("5"),
                               QStringLiteral("--out-dir"), out_v2, QStringLiteral("--base-url"),
                               server.url(QStringLiteral("/dl")).toString(),
                               QStringLiteral("--key-file"), key_file, QStringLiteral("--key-id"),
                               QStringLiteral("release-test"), QStringLiteral("--zstd-level"),
                               QStringLiteral("1")},
                              &tool_out),
                qPrintable(tool_out));

            const QJsonObject api_resp{
                {QStringLiteral("channel"), QStringLiteral("stable")},
                {QStringLiteral("paused"), false},
                {QStringLiteral("manifests"),
                 QJsonObject{
                     {QStringLiteral("win64"),
                      QJsonObject{{QStringLiteral("envelope"),
                                   QJsonDocument::fromJson(
                                       readFileBytes(out_v2 + QStringLiteral("/update-win64.json")))
                                       .object()}}}}},
            };
            server.json("/api/v1/desktop/releases/latest",
                        QJsonDocument(api_resp).toJson(QJsonDocument::Compact));
            server.file("/dl/AnimeHub-0.5.0-win64.ahpack",
                        readFileBytes(out_v2 + QStringLiteral("/AnimeHub-0.5.0-win64.ahpack")));

            QString helper_bin;
            QStringList helper_args;
            InstallContext ctx;
            ctx.installDir = install_dir;
            ctx.dataDir = data_dir;
            ctx.executablePath = install_dir + QStringLiteral("/AnimeHub.exe");
            ctx.updaterHelperPath = updater_exe;
            ctx.currentVersion = SemVer{0, 4, 0};
            ctx.installMode = InstallMode::WindowsPortable;
            ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
            ctx.keyRing = KeyRing{.keys = {TrustedKey{.id = QStringLiteral("release-test"),
                                                      .publicKey = key.pub,
                                                      .isRecovery = false}}};
            ctx.urlPolicy.allowInsecureLoopback = true;
            // Use parentPid = 0 so the helper does not wait for our test runner to exit!
            ctx.hooks.currentPid = [] { return 0LL; };
            ctx.hooks.launchUpdater = [&](const QString& bin, const QStringList& args) {
                helper_bin = bin;
                helper_args = args;
                return std::expected<void, UpdateError>{};
            };

            QNetworkAccessManager network;
            UpdateService svc(network, ctx);
            QSignalSpy ready_spy(&svc, &UpdateService::readyToApply);
            svc.checkNow(false);
            QVERIFY(ready_spy.wait(5'000));
            QVERIFY(svc.applyAndRestart({QStringLiteral("--data-dir"), data_dir}));
            QVERIFY(!helper_bin.isEmpty());

            // Execute the REAL AnimeHubUpdater.exe synchronously and assert exact exit code!
            QProcess helper;
            helper.start(helper_bin,
                         helper_args + QStringList{QStringLiteral("--no-rollback-relaunch"),
                                                   QStringLiteral("--health-timeout-ms"),
                                                   QStringLiteral("4000")});
            QVERIFY(helper.waitForFinished(15'000));
            QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
            QCOMPARE(helper.exitCode(), expected_exit);
        };

        // --- Subcase A: Healthy update swap ---
        {
            QTemporaryDir temp;
            QString install_dir;
            QString data_dir;
            runScenario(QStringLiteral("healthy"), 0, temp, install_dir, data_dir);

            QCOMPARE(readFileBytes(install_dir + QStringLiteral("/resource.txt")),
                     QByteArray("version-2"));
            QCOMPARE(readFileBytes(install_dir + QStringLiteral("/added.txt")),
                     QByteArray("brand-new"));
            QVERIFY(!QFileInfo::exists(install_dir + QStringLiteral("/obsolete.txt")));
            QVERIFY(QFileInfo::exists(data_dir +
                                      QStringLiteral("/library.sqlite3.pre-update-0.4.0.bak")));
            const auto installed =
                loadInstalledManifest(install_dir + QStringLiteral("/installed-manifest.json"));
            QVERIFY(installed.has_value());
            QCOMPARE(installed->version, QStringLiteral("0.5.0"));
            QCOMPARE(installed->releaseSeq, 5ULL);
        }

        // --- Subcase B: Startup crash -> automatic binary + SQLite DB rollback + anti-loop ---
        {
            QTemporaryDir temp;
            QString install_dir;
            QString data_dir;
            runScenario(QStringLiteral("crash-after-db-migrate"), 10, temp, install_dir, data_dir);

            // Binaries & resources restored to v1!
            QCOMPARE(readFileBytes(install_dir + QStringLiteral("/resource.txt")),
                     QByteArray("version-1"));
            QCOMPARE(readFileBytes(install_dir + QStringLiteral("/obsolete.txt")),
                     QByteArray("remove-me"));
            QVERIFY(!QFileInfo::exists(install_dir + QStringLiteral("/added.txt")));

            // Migrated/corrupted DB preserved as .failed-0.5.0, stale WAL/SHM deleted, original v1
            // SQLite DB & settings.ini cleanly restored!
            QCOMPARE(readFileBytes(data_dir + QStringLiteral("/library.sqlite3.failed-0.5.0")),
                     QByteArray("CORRUPTED_MIGRATED_DB_V050"));
            QVERIFY(!QFileInfo::exists(data_dir + QStringLiteral("/library.sqlite3-wal")));
            QVERIFY(!QFileInfo::exists(data_dir + QStringLiteral("/library.sqlite3-shm")));
            QCOMPARE(readSampleSqliteMarker(data_dir + QStringLiteral("/library.sqlite3")),
                     QStringLiteral("v1-intact-row"));
            QCOMPARE(readFileBytes(data_dir + QStringLiteral("/settings.ini")),
                     QByteArray("theme=dark\nversion=1\n"));

            // rollback.json ingested by UpdateService -> adds 0.5.0 to failedVersions anti-loop!
            QVERIFY(QFileInfo::exists(install_dir + QStringLiteral("/.update/rollback.json")));
            InstallContext ctx;
            ctx.installDir = install_dir;
            ctx.dataDir = data_dir;
            ctx.currentVersion = SemVer{0, 4, 0};
            ctx.installMode = InstallMode::WindowsPortable;
            QNetworkAccessManager network;
            UpdateService svc(network, ctx);
            QVERIFY(svc.failedVersions().contains(QStringLiteral("0.5.0")));
            QCOMPARE(svc.lastRollbackVersion(), QStringLiteral("0.5.0"));
            QVERIFY(svc.lastRollbackReason().contains(QLatin1StringView("42")));
            QVERIFY(!QFileInfo::exists(install_dir + QStringLiteral("/.update/rollback.json")));
        }
    }
#endif
};

int main(int argc, char** argv) {
    if (const int stub_exit = maybeRunStubVerifyMode(argc, argv); stub_exit >= 0) {
        return stub_exit;
    }
    QCoreApplication app(argc, argv);
    UpdateServiceTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "update_service_test.moc"
