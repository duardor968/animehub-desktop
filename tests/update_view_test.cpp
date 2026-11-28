#include "app/app_settings.h"
#include "app/update_view.h"
#include "http_test_server.h"
#include "monocypher-ed25519.h"
#include "update/update_manifest.h"
#include "update/update_service.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <array>

using namespace animehub;
using app::AppSettings;
using app::UpdateView;
using testing::HttpTestServer;
using namespace animehub::update;

namespace {

struct KeyPair {
    std::array<std::uint8_t, 64> secret{};
    Ed25519PublicKey pub{};
};

KeyPair makeKeyPair(std::uint8_t seed_fill) {
    std::array<std::uint8_t, 32> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] = static_cast<std::uint8_t>(seed_fill ^ static_cast<std::uint8_t>(i * 17 + 3));
    }
    KeyPair kp;
    crypto_ed25519_key_pair(kp.secret.data(), kp.pub.data(), seed.data());
    return kp;
}

QByteArray signEnvelope(const QJsonObject& payload_obj, const QString& key_id, const KeyPair& kp) {
    const QByteArray raw_payload = QJsonDocument(payload_obj).toJson(QJsonDocument::Compact);
    QByteArray msg;
    msg.append(kManifestDomainPrefix.data(), static_cast<qsizetype>(kManifestDomainPrefix.size()));
    msg.append(raw_payload);
    Ed25519Signature sig{};
    crypto_ed25519_sign(sig.data(), kp.secret.data(),
                        reinterpret_cast<const std::uint8_t*>(msg.constData()),
                        static_cast<std::size_t>(msg.size()));
    const QByteArray sig_bytes(reinterpret_cast<const char*>(sig.data()),
                               static_cast<qsizetype>(sig.size()));
    const QJsonObject env{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("keyId"), key_id},
        {QStringLiteral("signature"), QString::fromLatin1(sig_bytes.toBase64())},
        {QStringLiteral("payload"), QString::fromLatin1(raw_payload.toBase64())},
    };
    return QJsonDocument(env).toJson(QJsonDocument::Compact);
}

QByteArray apiWrapperForEnvelope(const QString& platform, const QByteArray& env_bytes) {
    const QJsonObject platform_block{
        {QStringLiteral("envelope"), QJsonDocument::fromJson(env_bytes).object()},
    };
    const QJsonObject root{
        {QStringLiteral("channel"), QStringLiteral("stable")},
        {QStringLiteral("paused"), false},
        {QStringLiteral("manifests"), QJsonObject{{platform, platform_block}}},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

void writeBytes(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(data), data.size());
    f.close();
}

QJsonObject buildWin64Manifest(const QString& version, int seq, bool is_major, const QUrl& pack_url,
                               const QByteArray& exe_payload) {
    const QString exe_sha = sha256Hex(exe_payload);
    return QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("platform"), QStringLiteral("win64")},
        {QStringLiteral("channel"), QStringLiteral("stable")},
        {QStringLiteral("version"), version},
        {QStringLiteral("releaseSeq"), seq},
        {QStringLiteral("isMajor"), is_major},
        {QStringLiteral("minUpdaterProtocol"), 1},
        {QStringLiteral("requiresFullInstaller"), false},
        {QStringLiteral("notes"),
         QStringLiteral("• Corrección de errores y mejoras de velocidad.")},
        {QStringLiteral("releaseUrl"),
         QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/tag/v") + version},
        {QStringLiteral("pack"),
         QJsonObject{
             {QStringLiteral("filename"), QStringLiteral("AnimeHub-%1-win64.ahpack").arg(version)},
             {QStringLiteral("size"), exe_payload.size()},
             {QStringLiteral("sha256"), exe_sha},
             {QStringLiteral("urls"), QJsonArray{pack_url.toString()}},
         }},
        {QStringLiteral("files"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("AnimeHub.exe")},
                 {QStringLiteral("kind"), QStringLiteral("file")},
                 {QStringLiteral("encoding"), QStringLiteral("raw")},
                 {QStringLiteral("size"), exe_payload.size()},
                 {QStringLiteral("sha256"), exe_sha},
                 {QStringLiteral("packOffset"), 0},
                 {QStringLiteral("packSize"), exe_payload.size()},
                 {QStringLiteral("group"), QStringLiteral("app")},
                 {QStringLiteral("executable"), true},
             },
         }},
    };
}

} // namespace

class UpdateViewTest final : public QObject {
    Q_OBJECT

private slots:
    void previewStatesAndPillMatrixFollowLockedRules() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QNetworkAccessManager network;
        InstallContext ctx;
        ctx.installDir = temp.filePath(QStringLiteral("app"));
        ctx.dataDir = temp.filePath(QStringLiteral("data"));
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        UpdateService svc(network, ctx);
        AppSettings settings(temp.filePath(QStringLiteral("settings.ini")), temp.path());

        QUrl opened;
        UpdateView view(svc, settings, [&opened](const QUrl& u) { opened = u; });

        // At rest: pill hidden.
        QCOMPARE(view.state(), QStringLiteral("uptodate"));
        QVERIFY(!view.pillVisible());
        QVERIFY(view.lastCheckedText().contains(QStringLiteral("Automáticas")));

        // Preview downloading in Auto mode: NEVER shows pill; clean subtitle with zero filler.
        view.previewState(QStringLiteral("downloading"));
        QCOMPARE(view.state(), QStringLiteral("downloading"));
        QVERIFY(view.downloading());
        QVERIFY(!view.pillVisible());
        QCOMPARE(view.progressPercent(), 79);
        QCOMPARE(view.progressDetailText(), QStringLiteral("1,1 MB de 1,4 MB · 3,8 MB/s · 79 %"));
        QVERIFY(!view.progressDetailText().contains(QStringLiteral("modificados")));

        // Preview ready in Auto mode: shows pill with e777 and direct label.
        view.previewState(QStringLiteral("ready"));
        QVERIFY(view.ready());
        QVERIFY(view.pillVisible());
        QCOMPARE(view.pillIcon(), QStringLiteral("e777"));
        QCOMPARE(view.pillLabel(), QStringLiteral("Actualizar a 0.6.0"));
        QCOMPARE(view.readySubtitleText(), QStringLiteral("Se instalará al reiniciar o al salir"));

        // Preview ready in Manual mode: NEVER shows pill.
        view.previewState(QStringLiteral("ready"), QStringLiteral("manual"));
        QVERIFY(!view.pillVisible());
        QCOMPARE(view.readySubtitleText(), QStringLiteral("Lista para instalar al reiniciar"));

        // Preview notify-available: pill shown with download icon.
        view.previewState(QStringLiteral("notify-available"));
        QVERIFY(view.available());
        QVERIFY(view.pillVisible());
        QCOMPARE(view.pillIcon(), QStringLiteral("e896"));
        QCOMPARE(view.pillLabel(), QStringLiteral("Nueva versión 0.6.0"));
        QCOMPARE(view.sizeText(), QStringLiteral("1,4 MB"));

        // Preview notify-downloading: pill shown with progress %, click opens Acerca de.
        view.previewState(QStringLiteral("notify-downloading"));
        QVERIFY(view.downloading());
        QVERIFY(view.pillVisible());
        QCOMPARE(view.pillIcon(), QStringLiteral("e896"));
        QCOMPARE(view.pillLabel(), QStringLiteral("Descargando 0.6.0 · 79 %"));
        QSignalSpy about_spy(&view, &UpdateView::openAboutRequested);
        view.handlePillClick();
        QCOMPARE(about_spy.size(), 1);

        // Preview error: pill hidden, error detail cites current version.
        view.previewState(QStringLiteral("error"));
        QVERIFY(view.error());
        QVERIFY(!view.pillVisible());
        QCOMPARE(view.errorDetailText(),
                 QStringLiteral("Sin conexión · Sigues en la versión 0.4.0"));

        // Preview confirm-restart: exposes activeDownloadsCount == 2 and triggers confirmation.
        view.previewState(QStringLiteral("confirm-restart"));
        QCOMPARE(view.activeDownloadsCount(), 2);
        QSignalSpy confirm_spy(&view, &UpdateView::restartConfirmationRequested);
        view.handlePillClick();
        QCOMPARE(confirm_spy.size(), 1);

        // Manual download uses injectable openUrl callback.
        view.openManualDownload();
        QVERIFY(opened.isValid());
        QVERIFY(opened.toString().contains(QStringLiteral("0.6.0")));
    }

    void notifyOnlyInlineDownloadAndActiveDownloadsRestartGate() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString install_dir = temp.filePath(QStringLiteral("install"));
        const QString data_dir = temp.filePath(QStringLiteral("data"));
        writeBytes(install_dir + QStringLiteral("/AnimeHub.exe"), QByteArray(1024, '1'));

        const QByteArray new_exe(128 * 1024, '2');
        const KeyPair kp = makeKeyPair(0x41);
        HttpTestServer server;
        QVERIFY(server.listen());
        const QUrl pack_url = server.url(QStringLiteral("/dl/AnimeHub-0.5.0-win64.ahpack"));
        const QByteArray env =
            signEnvelope(buildWin64Manifest(QStringLiteral("0.5.0"), 5, false, pack_url, new_exe),
                         QStringLiteral("test-key"), kp);
        server.json("/api/v1/desktop/releases/latest",
                    apiWrapperForEnvelope(QStringLiteral("win64"), env));
        server.file("/dl/AnimeHub-0.5.0-win64.ahpack", new_exe);

        bool updater_launched = false;
        bool quit_requested = false;
        int active_downloads = 2;

        InstallContext ctx;
        ctx.installDir = install_dir;
        ctx.dataDir = data_dir;
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.keyRing = KeyRing{.keys = {TrustedKey{.id = QStringLiteral("test-key"),
                                                  .publicKey = kp.pub,
                                                  .isRecovery = false}}};
        ctx.urlPolicy.allowInsecureLoopback = true;
        ctx.hooks.launchUpdater = [&](const QString&,
                                      const QStringList&) -> std::expected<void, UpdateError> {
            updater_launched = true;
            return {};
        };
        ctx.hooks.requestAppQuit = [&] { quit_requested = true; };

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        AppSettings settings(temp.filePath(QStringLiteral("settings.ini")), temp.path());
        settings.setUpdateMode(AppSettings::UpdateMode::NotifyOnly);
        svc.setUpdateMode(UpdateMode::NotifyOnly);

        UpdateView view(svc, settings, [](const QUrl&) {}, [&] { return active_downloads; });

        QSignalSpy check_spy(&svc, &UpdateService::checkFinished);
        view.checkNow();
        QVERIFY(check_spy.wait(5'000));
        QCOMPARE(view.state(), QStringLiteral("available"));
        QVERIFY(view.pillVisible());
        QCOMPARE(view.pillIcon(), QStringLiteral("e896"));
        QCOMPARE(view.pillLabel(), QStringLiteral("Nueva versión 0.5.0"));
        QVERIFY(!view.notes().isEmpty());

        // 1-click pill in NotifyOnly starts download inline and reaches Ready.
        QSignalSpy ready_spy(&svc, &UpdateService::readyToApply);
        view.handlePillClick();
        QVERIFY(view.downloading() || view.ready());
        if (!view.ready()) {
            QVERIFY(ready_spy.wait(5'000));
        }
        QVERIFY(view.ready());
        QVERIFY(view.pillVisible());
        QCOMPARE(view.pillIcon(), QStringLiteral("e777"));
        QCOMPARE(view.pillLabel(), QStringLiteral("Actualizar a 0.5.0"));
        QCOMPARE(view.readySubtitleText(), QStringLiteral("Lista para instalar al reiniciar"));

        // With active downloads > 0, pill click and requestApply ask confirmation first.
        QSignalSpy confirm_spy(&view, &UpdateView::restartConfirmationRequested);
        view.handlePillClick();
        QCOMPARE(confirm_spy.size(), 1);
        QVERIFY(!updater_launched);
        QVERIFY(!quit_requested);

        // Switching to Manual hides pill even when Ready.
        settings.setUpdateMode(AppSettings::UpdateMode::Manual);
        QVERIFY(!view.pillVisible());

        // Switching to Auto shows pill and updates readySubtitleText.
        settings.setUpdateMode(AppSettings::UpdateMode::Auto);
        QVERIFY(view.pillVisible());
        QCOMPARE(view.readySubtitleText(), QStringLiteral("Se instalará al reiniciar o al salir"));

        // With 0 active downloads, handlePillClick applies immediately.
        active_downloads = 0;
        view.handlePillClick();
        QVERIFY(updater_launched);
        QVERIFY(quit_requested);
    }

    void autoModeMajorUpdateShowsPillAndOpensAboutInsteadOfAutoDownloading() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString install_dir = temp.filePath(QStringLiteral("install"));
        const QString data_dir = temp.filePath(QStringLiteral("data"));
        writeBytes(install_dir + QStringLiteral("/AnimeHub.exe"), QByteArray(1024, '1'));

        const QByteArray new_exe(64 * 1024, '3');
        const KeyPair kp = makeKeyPair(0x52);
        HttpTestServer server;
        QVERIFY(server.listen());
        const QUrl pack_url = server.url(QStringLiteral("/dl/AnimeHub-1.0.0-win64.ahpack"));
        const QByteArray env =
            signEnvelope(buildWin64Manifest(QStringLiteral("1.0.0"), 10, true, pack_url, new_exe),
                         QStringLiteral("test-key"), kp);
        server.json("/api/v1/desktop/releases/latest",
                    apiWrapperForEnvelope(QStringLiteral("win64"), env));

        InstallContext ctx;
        ctx.installDir = install_dir;
        ctx.dataDir = data_dir;
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.keyRing = KeyRing{.keys = {TrustedKey{.id = QStringLiteral("test-key"),
                                                  .publicKey = kp.pub,
                                                  .isRecovery = false}}};
        ctx.urlPolicy.allowInsecureLoopback = true;

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        svc.setUpdateMode(UpdateMode::Auto);
        AppSettings settings(temp.filePath(QStringLiteral("settings.ini")), temp.path());
        UpdateView view(svc, settings, [](const QUrl&) {});

        // Background check on a major jump MUST NOT auto-download, and pill MUST appear in Auto.
        QSignalSpy check_spy(&svc, &UpdateService::checkFinished);
        svc.checkNow(false);
        QVERIFY(check_spy.wait(5'000));
        QCOMPARE(view.state(), QStringLiteral("available"));
        QVERIFY(view.majorUpdate());
        QVERIFY(view.pillVisible());
        QCOMPARE(view.pillLabel(), QStringLiteral("Nueva versión 1.0.0"));

        QSignalSpy about_spy(&view, &UpdateView::openAboutRequested);
        view.handlePillClick();
        QCOMPARE(about_spy.size(), 1);
    }

    void linuxDebManualOnlyUsesDebUrlAndNeverStartsInAppDownload() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const KeyPair kp = makeKeyPair(0x63);
        HttpTestServer server;
        QVERIFY(server.listen());
        const QUrl deb_url = server.url(QStringLiteral("/dl/animehub_0.5.0_amd64.deb"));
        const QJsonObject payload{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("platform"), QStringLiteral("linux-x64")},
            {QStringLiteral("channel"), QStringLiteral("stable")},
            {QStringLiteral("version"), QStringLiteral("0.5.0")},
            {QStringLiteral("releaseSeq"), 6},
            {QStringLiteral("isMajor"), false},
            {QStringLiteral("minUpdaterProtocol"), 1},
            {QStringLiteral("requiresFullInstaller"), false},
            {QStringLiteral("notes"), QStringLiteral("Paquete .deb actualizado.")},
            {QStringLiteral("releaseUrl"),
             QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/tag/v0.5.0")},
            {QStringLiteral("deb"),
             QJsonObject{
                 {QStringLiteral("filename"), QStringLiteral("animehub_0.5.0_amd64.deb")},
                 {QStringLiteral("size"), 5'200'000},
                 {QStringLiteral("sha256"), QString(64, QLatin1Char('b'))},
                 {QStringLiteral("urls"), QJsonArray{deb_url.toString()}},
             }},
            {QStringLiteral("files"), QJsonArray{}},
        };
        server.json("/api/v1/desktop/releases/latest",
                    apiWrapperForEnvelope(QStringLiteral("linux-x64"),
                                          signEnvelope(payload, QStringLiteral("test-key"), kp)));

        InstallContext ctx;
        ctx.installDir = temp.filePath(QStringLiteral("opt-animehub"));
        ctx.dataDir = temp.filePath(QStringLiteral("data"));
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::LinuxDeb;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.keyRing = KeyRing{.keys = {TrustedKey{.id = QStringLiteral("test-key"),
                                                  .publicKey = kp.pub,
                                                  .isRecovery = false}}};
        ctx.urlPolicy.allowInsecureLoopback = true;

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        svc.setUpdateMode(UpdateMode::Auto);
        AppSettings settings(temp.filePath(QStringLiteral("settings.ini")), temp.path());

        QUrl opened;
        UpdateView view(svc, settings, [&opened](const QUrl& u) { opened = u; });

        QSignalSpy check_spy(&svc, &UpdateService::checkFinished);
        svc.checkNow(false);
        QVERIFY(check_spy.wait(5'000));
        QCOMPARE(view.state(), QStringLiteral("available"));
        QVERIFY(view.manualOnly());
        QVERIFY(view.pillVisible());
        QCOMPARE(view.manualDownloadUrl(), deb_url);

        QSignalSpy about_spy(&view, &UpdateView::openAboutRequested);
        view.handlePillClick();
        QCOMPARE(about_spy.size(), 1);

        view.openManualDownload();
        QCOMPARE(opened, deb_url);
    }

    void errorClassificationNeverMisclassifiesUntrustedHostAndNeverLeaksEnglish() {
        const auto blocked_redirect = UpdateView::formatErrorSummary(
            QStringLiteral("Blocked redirect to untrusted host 'evil.example'"));
        QCOMPARE(blocked_redirect, QStringLiteral("Redirección bloqueada por seguridad"));
        QVERIFY(blocked_redirect != u"Sin conexión");

        const auto blocked_url = UpdateView::formatErrorSummary(
            QStringLiteral("Blocked update URL outside allowlist: http://evil.example/a.ahpack"));
        QCOMPARE(blocked_url, QStringLiteral("Origen de actualización bloqueado por seguridad"));
        QVERIFY(blocked_url != u"Sin conexión");

        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Too many redirects while downloading update")),
                 QStringLiteral("Demasiadas redirecciones al descargar la actualización"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Manifest was signed with a revoked key")),
                 QStringLiteral("Firma de actualización revocada"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Manifest was signed with an untrusted key")),
                 QStringLiteral("Firma de actualización no confiable"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Manifest Ed25519 signature verification failed")),
                 QStringLiteral("No se pudo verificar la firma de la actualización"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Rejected downgrade from 0.5.0 to 0.4.0")),
                 QStringLiteral("Actualización rechazada por seguridad"));
        QCOMPARE(
            UpdateView::formatErrorSummary(QStringLiteral("SHA-256 mismatch for AnimeHub.exe")),
            QStringLiteral("Falló la verificación de integridad de la actualización"));
        QCOMPARE(UpdateView::formatErrorSummary(QStringLiteral("Truncated zstd stream")),
                 QStringLiteral("El paquete de actualización llegó dañado o incompleto"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Manifest has no valid .ahpack artifact")),
                 QStringLiteral("No hay un paquete de actualización disponible para este sistema"));
        QCOMPARE(UpdateView::formatErrorSummary(QStringLiteral("Invalid update manifest JSON")),
                 QStringLiteral("La información de actualización no es válida"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("Could not launch updater helper: permission denied")),
                 QStringLiteral("No se pudo iniciar el asistente de actualización"));
        QCOMPARE(UpdateView::formatErrorSummary(QStringLiteral("Could not back up settings.ini")),
                 QStringLiteral("No se pudo crear la copia de seguridad previa"));
        QCOMPARE(
            UpdateView::formatErrorSummary(QStringLiteral("Could not commit staged files")),
            QStringLiteral("No se pudieron guardar los archivos de actualización en el disco"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("HTTP 404 from https://api.animehub.example/release")),
                 QStringLiteral("No se encontró la actualización en el servidor"));
        QCOMPARE(UpdateView::formatErrorSummary(
                     QStringLiteral("HTTP 503 from https://api.animehub.example/release")),
                 QStringLiteral("El servidor de actualizaciones no está disponible"));
        QCOMPARE(
            UpdateView::formatErrorSummary(QStringLiteral("Host api.animehub.example not found")),
            QStringLiteral("Sin conexión"));
        QCOMPARE(UpdateView::formatErrorSummary(QStringLiteral("Connection refused")),
                 QStringLiteral("Sin conexión"));

        const auto unknown =
            UpdateView::formatErrorSummary(QStringLiteral("Unexpected internal frobnitz error"));
        QCOMPARE(unknown, QStringLiteral("No se pudo completar la actualización"));
        QVERIFY(!unknown.contains(QStringLiteral("frobnitz"), Qt::CaseInsensitive));

        // End-to-end check: a server redirect to an untrusted host surfaces as a security error,
        // NEVER "Sin conexión" and NEVER leaking the raw English message.
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        HttpTestServer server;
        QVERIFY(server.listen());
        const auto redirect_to_evil = [](const HttpTestServer::Request&) {
            return HttpTestServer::Response{
                .status = 302,
                .body = {},
                .headers = {{"Location", "https://evil.example/manifest.json"}},
            };
        };
        server.route("GET", "/api/v1/desktop/releases/latest", redirect_to_evil);
        server.route("GET", "/github-fallback.json", redirect_to_evil);
        InstallContext ctx;
        ctx.installDir = temp.filePath(QStringLiteral("install"));
        ctx.dataDir = temp.filePath(QStringLiteral("data"));
        ctx.currentVersion = SemVer{0, 4, 0};
        ctx.installMode = InstallMode::WindowsPortable;
        ctx.apiBaseUrl = server.url(QStringLiteral("/api/v1/"));
        ctx.githubFallbackUrl = server.url(QStringLiteral("/github-fallback.json"));
        ctx.urlPolicy.allowInsecureLoopback = true;

        QNetworkAccessManager network;
        UpdateService svc(network, ctx);
        AppSettings settings(temp.filePath(QStringLiteral("settings.ini")), temp.path());
        UpdateView view(svc, settings, [](const QUrl&) {});

        QSignalSpy check_spy(&svc, &UpdateService::checkFinished);
        view.checkNow();
        QVERIFY(check_spy.wait(5'000));
        QVERIFY(view.error());
        QCOMPARE(
            view.errorDetailText(),
            QStringLiteral("Redirección bloqueada por seguridad · Sigues en la versión 0.4.0"));
        QVERIFY(!view.errorDetailText().contains(QStringLiteral("Sin conexión")));
        QVERIFY(!view.errorDetailText().contains(QStringLiteral("Blocked")));
    }
};

QTEST_GUILESS_MAIN(UpdateViewTest)
#include "update_view_test.moc"
