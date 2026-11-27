#ifndef ANIMEHUB_CORE_UPDATE_SERVICE_H
#define ANIMEHUB_CORE_UPDATE_SERVICE_H

#include "ahpack.h"
#include "update_http.h"
#include "update_keys.h"
#include "update_manifest.h"
#include "update_types.h"
#include "zsync_delta.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <expected>
#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace animehub::update {

struct UpdateHooks {
    // Creates a consistent SQLite backup at `backup_path` (default: `VACUUM INTO ?`).
    std::function<std::expected<void, UpdateError>(const QString& db_path,
                                                   const QString& backup_path)>
        snapshotDatabase;
    // Returns true if `<install_dir>/engines/mpv/mpv.exe` is currently running.
    std::function<bool(const QString& mpv_exe_path)> isMpvRunning;
    // Launches the detached updater helper process (`AnimeHubUpdater.exe --apply <plan.json>`).
    std::function<std::expected<void, UpdateError>(const QString& helper_path,
                                                   const QStringList& args)>
        launchUpdater;
    // Signals the helper that the newly launched app booted cleanly.
    std::function<bool(const QString& verify_token)> signalHealthyEvent;
    // Requests graceful shutdown of the current app after launching the updater helper.
    std::function<void()> requestAppQuit;
    // Returns current process PID (default: QCoreApplication::applicationPid()).
    std::function<qint64()> currentPid;
};

struct InstallContext {
    QString installDir;
    QString dataDir;
    QString executablePath;
    QString updaterHelperPath; // Windows: defaults to `<installDir>/AnimeHubUpdater.exe`
    SemVer currentVersion;
    InstallMode installMode = InstallMode::Unsupported;
    QString channel = QStringLiteral("stable");
    QUrl apiBaseUrl{QStringLiteral("https://animehub-api.duardo.dev/api/v1/")};
    QUrl githubFallbackUrl; // defaults per platform to duardor968/animehub-desktop latest release
    KeyRing keyRing = KeyRing::production();
    UrlPolicy urlPolicy;
    bool isolatedProfile = false;
    UpdateHooks hooks;
};

// Core in-app self-updater state machine: API control-plane check with GitHub fallback,
// Ed25519 manifest verification, Windows `.ahpack` delta + Win32 helper handoff,
// Linux AppImage `.zsync` delta + atomic rename & rollback, SQLite pre-update snapshot,
// and deferred `engines/mpv` swap when `mpv.exe` is running.
class UpdateService final : public QObject {
    Q_OBJECT

public:
    UpdateService(QNetworkAccessManager& network, InstallContext context,
                  QObject* parent = nullptr);
    ~UpdateService() override;

    // Platform helpers usable at startup before constructing UpdateService.
    [[nodiscard]] static InstallMode detectInstallMode(const QString& install_dir,
                                                       const QString& executable_path,
                                                       const QString& appimage_env = {});
    [[nodiscard]] static QString platformIdForMode(InstallMode mode);
    [[nodiscard]] static QUrl defaultGitHubFallbackUrl(const QString& platform);
    [[nodiscard]] static bool isExecutableRunningAtPath(const QString& exe_path);

    // Persisted preferences & configuration.
    [[nodiscard]] UpdateMode updateMode() const {
        return update_mode_;
    }
    void setUpdateMode(UpdateMode mode);

    [[nodiscard]] InstallMode installMode() const {
        return context_.installMode;
    }
    [[nodiscard]] UpdateState state() const {
        return state_;
    }
    [[nodiscard]] bool isBusy() const {
        return state_ == UpdateState::Checking || state_ == UpdateState::Downloading ||
               state_ == UpdateState::Verifying || state_ == UpdateState::Applying;
    }
    [[nodiscard]] bool updateAvailable() const {
        return available_manifest_.has_value() &&
               (state_ == UpdateState::Available || state_ == UpdateState::Downloading ||
                state_ == UpdateState::Verifying || state_ == UpdateState::Ready ||
                state_ == UpdateState::Applying);
    }
    [[nodiscard]] bool isReadyToApply() const {
        return state_ == UpdateState::Ready;
    }
    [[nodiscard]] bool isMajorUpdate() const;
    [[nodiscard]] bool requiresManualUpdate() const {
        return requires_manual_update_;
    }
    [[nodiscard]] bool isPaused() const {
        return paused_;
    }
    [[nodiscard]] bool minSupportedViolated() const {
        return min_supported_violated_;
    }
    [[nodiscard]] CheckSource lastCheckSource() const {
        return last_check_source_;
    }
    [[nodiscard]] QString currentVersion() const {
        return context_.currentVersion.toString();
    }
    [[nodiscard]] QString availableVersion() const {
        return available_manifest_ ? available_manifest_->version.toString() : QString();
    }
    [[nodiscard]] QString releaseNotes() const {
        return available_manifest_ ? available_manifest_->notes : QString();
    }
    [[nodiscard]] QUrl releaseUrl() const {
        return available_manifest_ ? available_manifest_->releaseUrl : QUrl();
    }
    [[nodiscard]] qint64 downloadedBytes() const {
        return downloaded_bytes_;
    }
    [[nodiscard]] qint64 totalBytes() const {
        return total_bytes_;
    }
    [[nodiscard]] qint64 speedBytesPerSec() const {
        return speed_bytes_per_sec_;
    }
    [[nodiscard]] QString errorMessage() const {
        return error_message_;
    }
    [[nodiscard]] QString lastAppliedVersion() const {
        return last_applied_version_;
    }
    [[nodiscard]] QString lastRollbackVersion() const {
        return last_rollback_version_;
    }
    [[nodiscard]] QString lastRollbackReason() const {
        return last_rollback_reason_;
    }
    [[nodiscard]] qint64 lastCheckedAtMs() const {
        return last_checked_at_ms_;
    }
    [[nodiscard]] const QSet<QString>& revokedKeyIds() const {
        return revoked_key_ids_;
    }
    [[nodiscard]] const QSet<QString>& failedVersions() const {
        return failed_versions_;
    }
    [[nodiscard]] const std::optional<UpdateManifest>& availableManifest() const {
        return available_manifest_;
    }
    [[nodiscard]] const std::optional<UpdatePlan>& preparedPlan() const {
        return prepared_plan_;
    }
    [[nodiscard]] QString preparedPlanPath() const {
        return prepared_plan_path_;
    }

    // Lifecycle & actions.
    void startBackgroundChecks(std::chrono::milliseconds initial_delay = std::chrono::seconds{15},
                               std::chrono::milliseconds interval = std::chrono::hours{6});
    void stopBackgroundChecks();

    void checkNow(bool user_triggered = true);
    void downloadNow();
    void cancelDownload();

    // Applies a Ready update immediately and launches the helper / restarts the app.
    bool applyAndRestart(const QStringList& relaunch_args = {});

    // Called during normal app shutdown: applies a Ready update silently ONLY if
    // (user explicitly requested apply) OR (UpdateMode::Auto AND !isMajor AND source == Api).
    bool applyOnQuitIfReady(const QStringList& relaunch_args = {});

    // Startup maintenance: swaps any deferred `engines/mpv/*` staged while mpv.exe was running.
    bool consumePendingEnginesOnStartup();

    // Called once the updated app has opened its DB and initialized cleanly
    // (`--post-update-verify`).
    bool confirmPostUpdateHealthy(const QString& token);

    // Reads any `.update/rollback.json` written by the helper after an automatic rollback.
    void ingestRollbackMarkerIfPresent();

    void acknowledgeLastAppliedVersion();
    void acknowledgeRollback();

signals:
    void stateChanged(animehub::update::UpdateState state);
    void checkFinished(const animehub::update::UpdateCheckResult& result);
    void progressChanged(qint64 downloaded_bytes, qint64 total_bytes, qint64 speed_bytes_per_sec);
    void readyToApply(const QString& version);
    void errorOccurred(const QString& message);
    void rollbackDetected(const QString& failed_version, const QString& reason);
    void changed();

private:
    void setState(UpdateState next);
    void loadPersistentState();
    void savePersistentState();
    std::uint64_t effectiveInstalledReleaseSeq() const;

    void fetchApiRelease(bool user_triggered);
    void fetchManifestFromUrls(QList<QUrl> urls, CheckSource source,
                               const QHash<QString, QList<QUrl>>& extra_mirrors,
                               std::optional<SemVer> min_supported, bool user_triggered);
    void fetchGitHubFallback(bool user_triggered);
    void processVerifiedEnvelope(const QByteArray& envelope_json, CheckSource source,
                                 const QHash<QString, QList<QUrl>>& extra_mirrors,
                                 std::optional<SemVer> min_supported, bool user_triggered);
    void finishCheck(UpdateCheckResult result);

    std::expected<void, UpdateError> createPreUpdateSnapshots(QString& out_db_backup,
                                                              QString& out_settings_backup);
    void pruneOldBackups(const QString& pattern, int keep_count) const;
    std::expected<QString, UpdateError> writePlanJson(const UpdatePlan& plan) const;
    bool applyPreparedUpdate(const QStringList& relaunch_args, bool launch_restarted_app);
    bool applyLinuxAppImageUpdate(const QStringList& relaunch_args, bool relaunch);

    QString updateWorkDir() const;
    QString stateFilePath() const;
    QString platformId() const;

    QNetworkAccessManager& network_;
    InstallContext context_;
    UpdateMode update_mode_ = UpdateMode::Auto;
    UpdateState state_ = UpdateState::Idle;

    UpdateHttpRequest* check_request_ = nullptr;
    AhpackDownloader* ahpack_downloader_ = nullptr;
    ZsyncDownloader* zsync_downloader_ = nullptr;
    QTimer* initial_timer_ = nullptr;
    QTimer* periodic_timer_ = nullptr;

    CheckSource last_check_source_ = CheckSource::None;
    bool paused_ = false;
    bool min_supported_violated_ = false;
    bool requires_manual_update_ = false;
    bool user_triggered_check_ = false;

    std::optional<UpdateManifest> available_manifest_;
    std::optional<AhpackDiff> current_diff_;
    QList<UpdatePlanFile> staged_files_;
    QString staged_appimage_path_;
    std::optional<UpdatePlan> prepared_plan_;
    QString prepared_plan_path_;

    qint64 downloaded_bytes_ = 0;
    qint64 total_bytes_ = 0;
    qint64 speed_bytes_per_sec_ = 0;
    QString error_message_;

    // Persisted in `<dataDir>/update-state.json`.
    QSet<QString> revoked_key_ids_;
    QSet<QString> failed_versions_;
    std::uint64_t persisted_release_seq_ = 0;
    QString last_applied_version_;
    QString last_rollback_version_;
    QString last_rollback_reason_;
    qint64 last_checked_at_ms_ = 0;
};

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_SERVICE_H
