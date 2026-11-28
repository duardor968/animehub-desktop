#ifndef ANIMEHUB_APP_UPDATE_VIEW_H
#define ANIMEHUB_APP_UPDATE_VIEW_H

#include "app_settings.h"
#include "update/update_service.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration>

#include <functional>
#include <optional>

namespace animehub::app {

// Thin QML adapter for the in-app self-updater: exposes formatted Spanish state, header pill
// visibility/copy according to the active update mode, and actions for Main and Ajustes.
class UpdateView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool error READ error NOTIFY changed)
    Q_PROPERTY(bool majorUpdate READ majorUpdate NOTIFY changed)
    Q_PROPERTY(bool manualOnly READ manualOnly NOTIFY changed)
    Q_PROPERTY(QString targetVersion READ targetVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY changed)
    Q_PROPERTY(QUrl manualDownloadUrl READ manualDownloadUrl NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(QString progressDetailText READ progressDetailText NOTIFY changed)
    Q_PROPERTY(QString lastCheckedText READ lastCheckedText NOTIFY changed)
    Q_PROPERTY(QString readySubtitleText READ readySubtitleText NOTIFY changed)
    Q_PROPERTY(QString errorDetailText READ errorDetailText NOTIFY changed)
    Q_PROPERTY(bool pillVisible READ pillVisible NOTIFY changed)
    Q_PROPERTY(QString pillIcon READ pillIcon NOTIFY changed)
    Q_PROPERTY(QString pillLabel READ pillLabel NOTIFY changed)
    Q_PROPERTY(QString pillTooltip READ pillTooltip NOTIFY changed)
    Q_PROPERTY(int activeDownloadsCount READ activeDownloadsCount NOTIFY changed)

public:
    UpdateView(update::UpdateService& service, AppSettings& settings,
               std::function<void(const QUrl&)> open_url,
               std::function<int()> active_downloads = {}, QObject* parent = nullptr);

    [[nodiscard]] QString state() const;
    [[nodiscard]] bool checking() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool available() const;
    [[nodiscard]] bool downloading() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool error() const;
    [[nodiscard]] bool majorUpdate() const;
    [[nodiscard]] bool manualOnly() const;
    [[nodiscard]] QString targetVersion() const;
    [[nodiscard]] QString notes() const;
    [[nodiscard]] QUrl releaseUrl() const;
    [[nodiscard]] QUrl manualDownloadUrl() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] int progressPercent() const;
    [[nodiscard]] QString sizeText() const;
    [[nodiscard]] QString progressDetailText() const;
    [[nodiscard]] QString lastCheckedText() const;
    [[nodiscard]] QString readySubtitleText() const;
    [[nodiscard]] QString errorDetailText() const;
    [[nodiscard]] bool pillVisible() const;
    [[nodiscard]] QString pillIcon() const;
    [[nodiscard]] QString pillLabel() const;
    [[nodiscard]] QString pillTooltip() const;
    [[nodiscard]] int activeDownloadsCount() const;

    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void downloadNow();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void requestApply();
    Q_INVOKABLE void applyAndRestart();
    Q_INVOKABLE void openManualDownload();
    Q_INVOKABLE void handlePillClick();

    // Emits restartConfirmationRequested() for callers such as the tray menu or smoke capture.
    void requestRestartConfirmation();
    // Notifies bindings when active download count changes.
    void notifyActiveDownloadsChanged();

    // Translates technical updater errors into concise Spanish copy (never leaks raw English).
    [[nodiscard]] static QString formatErrorSummary(const QString& raw);

    // Smoke-capture helper used ONLY with --smoke-update-state under --smoke-screenshot.
    void previewState(const QString& state, const QString& mode = {});

signals:
    void changed();
    void openAboutRequested();
    void restartConfirmationRequested();

private:
    struct Preview {
        update::UpdateState state = update::UpdateState::Idle;
        AppSettings::UpdateMode mode = AppSettings::UpdateMode::Auto;
        update::CheckSource source = update::CheckSource::Api;
        bool majorUpdate = false;
        bool manualOnly = false;
        QString targetVersion;
        QString notes;
        QUrl releaseUrl;
        QUrl manualDownloadUrl;
        qint64 downloadedBytes = 0;
        qint64 totalBytes = 0;
        qint64 speedBytesPerSec = 0;
        qint64 lastCheckedAtMs = 0;
        QString errorMessage;
        int activeDownloads = 0;
    };

    [[nodiscard]] update::UpdateState effectiveState() const;
    [[nodiscard]] AppSettings::UpdateMode effectiveMode() const;
    [[nodiscard]] update::CheckSource effectiveSource() const;
    [[nodiscard]] qint64 effectiveDownloadedBytes() const;
    [[nodiscard]] qint64 effectiveTotalBytes() const;
    [[nodiscard]] qint64 effectiveSpeedBytesPerSec() const;
    [[nodiscard]] qint64 effectiveLastCheckedAtMs() const;
    [[nodiscard]] QString effectiveErrorMessage() const;

    update::UpdateService& service_;
    AppSettings& settings_;
    std::function<void(const QUrl&)> open_url_;
    std::function<int()> active_downloads_;
    std::optional<Preview> preview_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_UPDATE_VIEW_H
