#include "app_settings.h"

#include "platform/autostart.h"
#include "platform/installed_apps.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <array>

namespace animehub::app {
namespace {

const auto kDownloadDirectory = QStringLiteral("downloads/directory");
const auto kProviders = QStringLiteral("downloads/providers");
const auto kDisabledProviders = QStringLiteral("downloads/disabledProviders");
constexpr std::array kAllProviders{api::Provider::Mega, api::Provider::Pixeldrain,
                                   api::Provider::TransferIt, api::Provider::Mp4Upload,
                                   api::Provider::OneFichier};
const auto kDubbed = QStringLiteral("downloads/dubbed");
const auto kConcurrent = QStringLiteral("downloads/concurrent");
const auto kPlayer = QStringLiteral("playback/player");
// Earlier versions only told the system's player from the bundled one.
const auto kSystemPlayer = QStringLiteral("playback/systemPlayer");
const auto kNotifications = QStringLiteral("notifications/enabled");
const auto kCloseAction = QStringLiteral("window/closeAction");
const auto kTrayNoticeShown = QStringLiteral("window/trayNoticeShown");
const auto kJdOfferHidden = QStringLiteral("notices/jdownloaderOfferHidden");
const auto kServersOfferHidden = QStringLiteral("notices/serversOfferHidden");
const auto kJDownloader = QStringLiteral("jdownloader/mode");
const auto kJDownloaderDevice = QStringLiteral("jdownloader/device");
// JDownloader as the engine of every download, and how it was reached then.
const auto kJDownloaderEngine = QStringLiteral("downloads/jdownloader");
const auto kJDownloaderCloud = QStringLiteral("jdownloader/cloudEngine");

} // namespace

AppSettings::AppSettings(const QString& path, QString default_download_directory, QObject* parent)
    : QObject(parent), settings_(path, QSettings::IniFormat),
      default_download_directory_(std::move(default_download_directory)) {}

void AppSettings::write(const QString& key, const QVariant& value) {
    if (settings_.value(key) == value) {
        return;
    }
    settings_.setValue(key, value);
    settings_.sync();
    emit changed();
}

QString AppSettings::downloadDirectory() const {
    if (isolated_)
        return QDir::cleanPath(default_download_directory_);
    return QDir::cleanPath(
        settings_.value(kDownloadDirectory, default_download_directory_).toString());
}

void AppSettings::setDownloadDirectory(const QString& directory) {
    if (isolated_)
        return;
    if (!directory.isEmpty()) {
        write(kDownloadDirectory, QDir::cleanPath(directory));
    }
}

QStringList AppSettings::providerOrder() const {
    const auto stored = settings_.value(kProviders).toStringList();
    QStringList order;
    for (const auto& key : stored) {
        const auto provider = api::providerFromKey(key);
        if (provider && downloads::DownloadManager::isNative(*provider) && !order.contains(key)) {
            order.append(key);
        }
    }
    for (const auto provider :
         {api::Provider::Mega, api::Provider::Pixeldrain, api::Provider::TransferIt}) {
        if (!order.contains(api::providerKey(provider))) {
            order.append(api::providerKey(provider));
        }
    }
    return order;
}

void AppSettings::setProviderOrder(const QStringList& order) {
    write(kProviders, order);
}

bool AppSettings::dubbed() const {
    return settings_.value(kDubbed, false).toBool();
}

void AppSettings::setDubbed(bool dubbed) {
    write(kDubbed, dubbed);
}

int AppSettings::maxConcurrent() const {
    return std::clamp(settings_.value(kConcurrent, 2).toInt(), 1, 4);
}

void AppSettings::setMaxConcurrent(int count) {
    write(kConcurrent, std::clamp(count, 1, 4));
}

QString AppSettings::player() const {
    const auto stored = settings_.value(kPlayer).toString();
    if (stored == u"potplayer") {
        return potPlayerFound() ? stored : QStringLiteral("system");
    }
    if (stored == u"mpv" || stored == u"system") {
        return stored;
    }
    return settings_.value(kSystemPlayer, false).toBool() ? QStringLiteral("system")
                                                          : QStringLiteral("mpv");
}

void AppSettings::setPlayer(const QString& player) {
    if (player == u"mpv" || player == u"system" || (player == u"potplayer" && potPlayerFound())) {
        write(kPlayer, player);
    }
}

bool AppSettings::potPlayerFound() const {
    return !platform::potPlayerExecutable().isEmpty();
}

playback::PlayerKind AppSettings::playerKind() const {
    const auto chosen = player();
    return chosen == u"potplayer" ? playback::PlayerKind::PotPlayer
           : chosen == u"system"  ? playback::PlayerKind::System
                                  : playback::PlayerKind::Bundled;
}

bool AppSettings::notifications() const {
    return settings_.value(kNotifications, true).toBool();
}

void AppSettings::setNotifications(bool enabled) {
    write(kNotifications, enabled);
}

QString AppSettings::startCommand() const {
    auto command = QLatin1Char('"') + QDir::toNativeSeparators(platform::launcherPath()) +
                   QStringLiteral("\" --tray");
    const QDir data = QFileInfo(settings_.fileName()).absoluteDir();
    if (data != QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))) {
        command += QStringLiteral(" --data-dir \"") +
                   QDir::toNativeSeparators(data.absolutePath()) + QLatin1Char('"');
    }
    return command;
}

bool AppSettings::startWithWindows() const {
    if (isolated_)
        return false;
    return platform::autostartCommand().compare(startCommand(), Qt::CaseInsensitive) == 0;
}

void AppSettings::setStartWithWindows(bool enabled) {
    if (isolated_)
        return;
    if (enabled == startWithWindows()) {
        return;
    }
    platform::setAutostartCommand(enabled ? startCommand() : QString());
    emit changed();
}

AppSettings::CloseAction AppSettings::closeAction() const {
    const auto value = settings_.value(kCloseAction).toString();
    if (value == u"tray") {
        return CloseAction::Tray;
    }
    if (value == u"quit") {
        return CloseAction::Quit;
    }
    return CloseAction::Ask;
}

void AppSettings::setCloseAction(CloseAction action) {
    write(kCloseAction, action == CloseAction::Tray   ? QStringLiteral("tray")
                        : action == CloseAction::Quit ? QStringLiteral("quit")
                                                      : QStringLiteral("ask"));
}

bool AppSettings::trayNoticeShown() const {
    return settings_.value(kTrayNoticeShown, false).toBool();
}

void AppSettings::setTrayNoticeShown() {
    write(kTrayNoticeShown, true);
}

bool AppSettings::jdownloaderOfferHidden() const {
    return settings_.value(kJdOfferHidden, false).toBool();
}

void AppSettings::hideJdownloaderOffer() {
    write(kJdOfferHidden, true);
}

bool AppSettings::serversOfferHidden() const {
    return settings_.value(kServersOfferHidden, false).toBool();
}

void AppSettings::hideServersOffer() {
    write(kServersOfferHidden, true);
}

AppSettings::JDownloader AppSettings::jdownloader() const {
    // As the engine it was reached the way its own setting said.
    if (settings_.value(kJDownloaderEngine, false).toBool()) {
        return settings_.value(kJDownloaderCloud, false).toBool() ? JDownloader::Remote
                                                                  : JDownloader::Local;
    }
    const auto value = settings_.value(kJDownloader).toString();
    if (value == u"local") {
        return JDownloader::Local;
    }
    if (value == u"remote") {
        return JDownloader::Remote;
    }
    return JDownloader::Off;
}

void AppSettings::setJdownloader(JDownloader mode) {
    write(kJDownloader, mode == JDownloader::Local    ? QStringLiteral("local")
                        : mode == JDownloader::Remote ? QStringLiteral("remote")
                                                      : QStringLiteral("off"));
    write(kJDownloaderCloud, mode == JDownloader::Remote);
    if (mode != JDownloader::Off) {
        return;
    }
    write(kJDownloaderEngine, false);
    // Without JDownloader only MEGA, Pixeldrain and transfer.it download: one of them stays on.
    if (auto disabled = disabledProviders(); !anyUsable(disabled)) {
        disabled.removeAll(providerOrder().first());
        write(kDisabledProviders, disabled);
    }
}

QString AppSettings::jdownloaderDevice() const {
    return settings_.value(kJDownloaderDevice).toString();
}

void AppSettings::setJdownloaderDevice(const QString& id) {
    write(kJDownloaderDevice, id);
}

QStringList AppSettings::disabledProviders() const {
    // 1Fichier limits free downloads hard: off until the user turns it on, as on the web.
    return settings_
        .value(kDisabledProviders, QStringList{api::providerKey(api::Provider::OneFichier)})
        .toStringList();
}

bool AppSettings::usable(api::Provider provider) const {
    return downloads::DownloadManager::isNative(provider) || jdownloader() != JDownloader::Off;
}

bool AppSettings::anyUsable(const QStringList& disabled) const {
    return std::ranges::any_of(kAllProviders, [&](api::Provider provider) {
        return usable(provider) && !disabled.contains(api::providerKey(provider));
    });
}

void AppSettings::setDisabledProviders(const QStringList& keys) {
    QStringList known;
    for (const auto& key : keys) {
        if (api::providerFromKey(key) && !known.contains(key))
            known.append(key);
    }
    if (anyUsable(known)) {
        write(kDisabledProviders, known);
    }
}

QVariantList AppSettings::providerSwitches() const {
    const auto disabled = disabledProviders();
    const auto on = [&](api::Provider provider) {
        return usable(provider) && !disabled.contains(api::providerKey(provider));
    };
    const auto count = std::ranges::count_if(kAllProviders, on);
    QVariantList switches;
    for (const auto provider : kAllProviders) {
        const bool checked = on(provider);
        const bool last = checked && count == 1;
        switches.append(QVariantMap{
            {QStringLiteral("key"), api::providerKey(provider)},
            {QStringLiteral("name"), api::providerLabel(provider)},
            {QStringLiteral("checked"), checked},
            {QStringLiteral("enabled"), usable(provider) && !last},
            {QStringLiteral("reason"), !usable(provider) ? QStringLiteral("Necesita JDownloader")
                                       : last            ? QStringLiteral("Al menos un proveedor")
                                                         : QString()}});
    }
    return switches;
}

void AppSettings::setProviderEnabled(const QString& key, bool enabled) {
    auto disabled = disabledProviders();
    disabled.removeAll(key);
    if (!enabled) {
        disabled.append(key);
    }
    setDisabledProviders(disabled);
}

QList<api::Provider> AppSettings::providers() const {
    // The natives in the user's order, then the others.
    auto order = providerOrder();
    for (const auto provider : kAllProviders) {
        if (!order.contains(api::providerKey(provider)))
            order << api::providerKey(provider);
    }
    const auto disabled = disabledProviders();
    QList<api::Provider> providers;
    for (const auto& key : order) {
        if (const auto provider = api::providerFromKey(key);
            provider && usable(*provider) && !disabled.contains(key)) {
            providers.append(*provider);
        }
    }
    return providers;
}

downloads::DownloadSettings AppSettings::downloads() const {
    using downloads::DownloadEngine;
    const auto reach = jdownloader();
    const auto jd = reach == JDownloader::Local    ? DownloadEngine::JDownloaderLocal
                    : reach == JDownloader::Remote ? DownloadEngine::JDownloaderCloud
                                                   : DownloadEngine::Native;
    return {.directory = downloadDirectory(),
            .providers = providers(),
            .audio = dubbed() ? api::Audio::Dub : api::Audio::Sub,
            .maxConcurrent = maxConcurrent(),
            .engine = preferredJDownloader() ? jd : DownloadEngine::Native,
            .otherServers = jd,
            .jdEndpoint = QUrl(jdEndpoint()),
            .jdDeviceId = jdownloaderDevice()};
}

bool AppSettings::preferredJDownloader() const {
    return settings_.value(kJDownloaderEngine, false).toBool();
}
void AppSettings::setPreferredJDownloader(bool value) {
    const auto reach = jdownloader();
    if (value && reach == JDownloader::Off) {
        return;
    }
    // How it is reached is its own setting, kept when JDownloader stops being the engine.
    setJdownloader(reach);
    write(kJDownloaderEngine, value);
}
QString AppSettings::jdEndpoint() const {
    return settings_
        .value(QStringLiteral("jdownloader/endpoint"), QStringLiteral("http://127.0.0.1:3128"))
        .toString();
}
void AppSettings::setJdEndpoint(const QString& value) {
    const QUrl url(value.trimmed());
    if (url.scheme() == QStringLiteral("http") &&
        (url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("::1")) &&
        url.userInfo().isEmpty())
        write(QStringLiteral("jdownloader/endpoint"), url.toString());
}
QString AppSettings::jdExecutable() const {
    return settings_.value(QStringLiteral("jdownloader/executable")).toString();
}
void AppSettings::setJdExecutable(const QString& value) {
    write(QStringLiteral("jdownloader/executable"), value);
}
int AppSettings::remoteProfileRevision(const QByteArray& fingerprint) {
    auto revision = settings_.value(QStringLiteral("remote/profileRevision"), 0).toInt();
    if (settings_.value(QStringLiteral("remote/profileFingerprint")).toByteArray() != fingerprint ||
        revision < 1) {
        ++revision;
        settings_.setValue(QStringLiteral("remote/profileRevision"), revision);
        settings_.setValue(QStringLiteral("remote/profileFingerprint"), fingerprint);
        settings_.sync();
    }
    return revision;
}

QUrl AppSettings::bundledFile(const QString& name) const {
    const QFileInfo file(QDir(QCoreApplication::applicationDirPath()).filePath(name));
    return file.isFile() ? QUrl::fromLocalFile(file.absoluteFilePath()) : QUrl();
}

QVariant AppSettings::value(const QString& key, const QVariant& fallback) const {
    return settings_.value(QStringLiteral("ui/") + key, fallback);
}

void AppSettings::setValue(const QString& key, const QVariant& value) {
    settings_.setValue(QStringLiteral("ui/") + key, value);
}

} // namespace animehub::app
