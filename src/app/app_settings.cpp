#include "app_settings.h"

#include "platform/autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace animehub::app {
namespace {

const auto kDownloadDirectory = QStringLiteral("downloads/directory");
const auto kProviders = QStringLiteral("downloads/providers");
const auto kDubbed = QStringLiteral("downloads/dubbed");
const auto kConcurrent = QStringLiteral("downloads/concurrent");
const auto kSystemPlayer = QStringLiteral("playback/systemPlayer");
const auto kNotifications = QStringLiteral("notifications/enabled");
const auto kCloseAction = QStringLiteral("window/closeAction");
const auto kTrayNoticeShown = QStringLiteral("window/trayNoticeShown");
const auto kJDownloader = QStringLiteral("jdownloader/mode");
const auto kJDownloaderDevice = QStringLiteral("jdownloader/device");

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
    return QDir::cleanPath(
        settings_.value(kDownloadDirectory, default_download_directory_).toString());
}

void AppSettings::setDownloadDirectory(const QString& directory) {
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
    for (const auto provider : {api::Provider::Mega, api::Provider::Pixeldrain}) {
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

bool AppSettings::systemPlayer() const {
    return settings_.value(kSystemPlayer, false).toBool();
}

void AppSettings::setSystemPlayer(bool system) {
    write(kSystemPlayer, system);
}

bool AppSettings::notifications() const {
    return settings_.value(kNotifications, true).toBool();
}

void AppSettings::setNotifications(bool enabled) {
    write(kNotifications, enabled);
}

QString AppSettings::startCommand() const {
    auto command = QLatin1Char('"') +
                   QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
                   QStringLiteral("\" --tray");
    const QDir data = QFileInfo(settings_.fileName()).absoluteDir();
    if (data != QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))) {
        command += QStringLiteral(" --data-dir \"") +
                   QDir::toNativeSeparators(data.absolutePath()) + QLatin1Char('"');
    }
    return command;
}

bool AppSettings::startWithWindows() const {
    return platform::autostartCommand().compare(startCommand(), Qt::CaseInsensitive) == 0;
}

void AppSettings::setStartWithWindows(bool enabled) {
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

AppSettings::JDownloader AppSettings::jdownloader() const {
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
}

QString AppSettings::jdownloaderDevice() const {
    return settings_.value(kJDownloaderDevice).toString();
}

void AppSettings::setJdownloaderDevice(const QString& id) {
    write(kJDownloaderDevice, id);
}

QList<api::Provider> AppSettings::providers() const {
    QList<api::Provider> providers;
    for (const auto& key : providerOrder()) {
        if (const auto provider = api::providerFromKey(key)) {
            providers.append(*provider);
        }
    }
    return providers;
}

downloads::DownloadSettings AppSettings::downloads() const {
    return {.directory = downloadDirectory(),
            .providers = providers(),
            .audio = dubbed() ? api::Audio::Dub : api::Audio::Sub,
            .maxConcurrent = maxConcurrent()};
}

QVariant AppSettings::value(const QString& key, const QVariant& fallback) const {
    return settings_.value(QStringLiteral("ui/") + key, fallback);
}

void AppSettings::setValue(const QString& key, const QVariant& value) {
    settings_.setValue(QStringLiteral("ui/") + key, value);
}

} // namespace animehub::app
