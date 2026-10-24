#ifndef ANIMEHUB_APP_SETTINGS_H
#define ANIMEHUB_APP_SETTINGS_H

#include "downloads/download_manager.h"
#include "playback/player.h"

#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QtQmlIntegration>

namespace animehub::app {

// Preferences the user sets, persisted in settings.ini next to the library.
class AppSettings final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(
        QString downloadDirectory READ downloadDirectory WRITE setDownloadDirectory NOTIFY changed)
    Q_PROPERTY(QStringList providerOrder READ providerOrder WRITE setProviderOrder NOTIFY changed)
    Q_PROPERTY(bool dubbed READ dubbed WRITE setDubbed NOTIFY changed)
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY changed)
    Q_PROPERTY(bool systemPlayer READ systemPlayer WRITE setSystemPlayer NOTIFY changed)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY changed)
    Q_PROPERTY(bool startWithWindows READ startWithWindows WRITE setStartWithWindows NOTIFY changed)
    Q_PROPERTY(bool isolated READ isolated CONSTANT)
    Q_PROPERTY(bool preferredJDownloader READ preferredJDownloader WRITE setPreferredJDownloader
                   NOTIFY changed)
    Q_PROPERTY(bool cloudJDownloader READ cloudJDownloader WRITE setCloudJDownloader NOTIFY changed)
    Q_PROPERTY(QString jdEndpoint READ jdEndpoint WRITE setJdEndpoint NOTIFY changed)
    Q_PROPERTY(QString jdExecutable READ jdExecutable WRITE setJdExecutable NOTIFY changed)
    Q_PROPERTY(CloseAction closeAction READ closeAction WRITE setCloseAction NOTIFY changed)
    Q_PROPERTY(JDownloader jdownloader READ jdownloader WRITE setJdownloader NOTIFY changed)
    Q_PROPERTY(
        QString jdownloaderDevice READ jdownloaderDevice WRITE setJdownloaderDevice NOTIFY changed)

public:
    enum class CloseAction { Ask, Tray, Quit };
    Q_ENUM(CloseAction)
    enum class JDownloader { Off, Local, Remote };
    Q_ENUM(JDownloader)

    AppSettings(const QString& path, QString default_download_directory, QObject* parent = nullptr);
    void setIsolated(bool value) {
        isolated_ = value;
    }
    bool isolated() const {
        return isolated_;
    }
    bool preferredJDownloader() const;
    void setPreferredJDownloader(bool value);
    bool cloudJDownloader() const;
    void setCloudJDownloader(bool value);
    QString jdEndpoint() const;
    void setJdEndpoint(const QString& value);
    QString jdExecutable() const;
    void setJdExecutable(const QString& value);
    int remoteProfileRevision(const QByteArray& fingerprint);

    [[nodiscard]] QString downloadDirectory() const;
    void setDownloadDirectory(const QString& directory);
    [[nodiscard]] QStringList providerOrder() const;
    void setProviderOrder(const QStringList& order);
    [[nodiscard]] bool dubbed() const;
    void setDubbed(bool dubbed);
    [[nodiscard]] int maxConcurrent() const;
    void setMaxConcurrent(int count);
    [[nodiscard]] bool systemPlayer() const;
    void setSystemPlayer(bool system);
    [[nodiscard]] bool notifications() const;
    void setNotifications(bool enabled);
    // Windows starts this AnimeHub, in the tray, when the user signs in. An entry left by a moved
    // copy or by another library does not count: turning it on writes this one.
    [[nodiscard]] bool startWithWindows() const;
    void setStartWithWindows(bool enabled);
    [[nodiscard]] CloseAction closeAction() const;
    void setCloseAction(CloseAction action);
    [[nodiscard]] JDownloader jdownloader() const;
    void setJdownloader(JDownloader mode);
    [[nodiscard]] QString jdownloaderDevice() const;
    void setJdownloaderDevice(const QString& id);

    // Whether Windows already said, once, that the window went on running in the tray.
    [[nodiscard]] bool trayNoticeShown() const;
    void setTrayNoticeShown();

    [[nodiscard]] downloads::DownloadSettings downloads() const;
    [[nodiscard]] QList<api::Provider> providers() const;

    // A file shipped next to the executable (the license notices of an installed copy), or an
    // empty URL when this copy has none.
    Q_INVOKABLE QUrl bundledFile(const QString& name) const;

    // Window geometry and the library list width survive restarts.
    Q_INVOKABLE QVariant value(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

signals:
    void changed();

private:
    void write(const QString& key, const QVariant& value);
    // "C:\…\AnimeHub.exe" --tray, with this library's folder when it is not the default one.
    [[nodiscard]] QString startCommand() const;

    mutable QSettings settings_;
    QString default_download_directory_;
    bool isolated_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_SETTINGS_H
