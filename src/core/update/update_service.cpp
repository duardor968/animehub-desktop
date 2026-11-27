#include "update_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <array>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <tlhelp32.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace animehub::update {
namespace {

constexpr int kKeepPreUpdateBackups = 3;

std::expected<void, UpdateError> defaultSnapshotDatabase(const QString& db_path,
                                                         const QString& backup_path) {
    if (!QFileInfo::exists(db_path)) {
        return {};
    }
    if (QFileInfo::exists(backup_path)) {
        QFile::remove(backup_path);
    }
    const QString conn_name =
        QStringLiteral("animehub-update-backup-") + QUuid::createUuid().toString(QUuid::Id128);
    std::expected<void, UpdateError> result{};
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn_name);
        db.setDatabaseName(db_path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!db.open()) {
            result = std::unexpected(
                UpdateError{QStringLiteral("db_open"), db.lastError().text().trimmed()});
        } else {
            QSqlQuery q(db);
            if (!q.prepare(QStringLiteral("VACUUM INTO ?"))) {
                result = std::unexpected(
                    UpdateError{QStringLiteral("db_vacuum"), q.lastError().text().trimmed()});
            } else {
                q.addBindValue(backup_path);
                if (!q.exec()) {
                    result = std::unexpected(
                        UpdateError{QStringLiteral("db_vacuum"), q.lastError().text().trimmed()});
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn_name);
    return result;
}

bool defaultSignalHealthyEvent(const QString& token) {
    if (token.isEmpty()) {
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring name =
        (QStringLiteral("Local\\AnimeHubUpdateHealthy_") + token).toStdWString();
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (ev == nullptr) {
        return false;
    }
    const bool ok = SetEvent(ev) != FALSE;
    CloseHandle(ev);
    return ok;
#else
    return true;
#endif
}

std::expected<void, UpdateError> defaultLaunchUpdater(const QString& helper_path,
                                                      const QStringList& args) {
    qint64 pid = 0;
    if (!QProcess::startDetached(helper_path, args, QFileInfo(helper_path).absolutePath(), &pid)) {
        return std::unexpected(
            UpdateError{QStringLiteral("launch_helper"),
                        QStringLiteral("Could not launch updater helper '%1'").arg(helper_path)});
    }
    return {};
}

QUrl resolveApiEndpoint(const QUrl& base, const QString& relative_path, const QString& channel) {
    QUrl base_dir = base;
    if (!base_dir.path().endsWith(QLatin1Char('/'))) {
        base_dir.setPath(base_dir.path() + QLatin1Char('/'));
    }
    QUrl url = base_dir.resolved(QUrl(relative_path));
    url.setQuery(QStringLiteral("channel=") +
                 QString::fromLatin1(QUrl::toPercentEncoding(channel)));
    return url;
}

} // namespace

InstallMode UpdateService::detectInstallMode(const QString& install_dir,
                                             const QString& executable_path,
                                             const QString& appimage_env) {
#ifdef Q_OS_WIN
    Q_UNUSED(appimage_env);
    if (QFileInfo::exists(QDir(install_dir).filePath(QStringLiteral("unins000.exe")))) {
        return InstallMode::WindowsInstalled;
    }
    if (!executable_path.isEmpty() && QFileInfo::exists(executable_path)) {
        return InstallMode::WindowsPortable;
    }
    return InstallMode::Unsupported;
#else
    Q_UNUSED(install_dir);
    if (!appimage_env.isEmpty() && QFileInfo::exists(appimage_env)) {
        return InstallMode::LinuxAppImage;
    }
    const QString cleaned = QDir::cleanPath(executable_path);
    if (cleaned.startsWith(QLatin1StringView("/opt/animehub/"))) {
        return InstallMode::LinuxDeb;
    }
    return InstallMode::Unsupported;
#endif
}

QString UpdateService::platformIdForMode(InstallMode mode) {
    switch (mode) {
    case InstallMode::WindowsInstalled:
    case InstallMode::WindowsPortable:
        return QStringLiteral("win64");
    case InstallMode::LinuxAppImage:
    case InstallMode::LinuxDeb:
        return QStringLiteral("linux-x64");
    case InstallMode::Unsupported:
        break;
    }
#ifdef Q_OS_WIN
    return QStringLiteral("win64");
#else
    return QStringLiteral("linux-x64");
#endif
}

QUrl UpdateService::defaultGitHubFallbackUrl(const QString& platform) {
    return QUrl(QStringLiteral("https://github.com/duardor968/animehub-desktop/releases/latest/"
                               "download/update-%1.json")
                    .arg(platform));
}

bool UpdateService::isExecutableRunningAtPath(const QString& exe_path) {
    if (exe_path.isEmpty()) {
        return false;
    }
#ifdef Q_OS_WIN
    const QFileInfo target_fi(exe_path);
    const QString target_canonical = QDir::toNativeSeparators(
        target_fi.canonicalFilePath().isEmpty() ? target_fi.absoluteFilePath()
                                                : target_fi.canonicalFilePath());
    const QString target_name = target_fi.fileName();
    if (target_name.isEmpty()) {
        return false;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    struct SnapGuard {
        HANDLE h;
        ~SnapGuard() {
            CloseHandle(h);
        }
    } guard{snap};

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snap, &entry)) {
        return false;
    }
    do {
        if (QString::fromWCharArray(entry.szExeFile).compare(target_name, Qt::CaseInsensitive) !=
            0) {
            continue;
        }
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (proc == nullptr) {
            continue;
        }
        std::array<wchar_t, 32768> path_buf{};
        auto size = static_cast<DWORD>(path_buf.size());
        const bool ok = QueryFullProcessImageNameW(proc, 0, path_buf.data(), &size) != FALSE;
        CloseHandle(proc);
        if (!ok) {
            continue;
        }
        const QString running = QDir::toNativeSeparators(QString::fromWCharArray(path_buf.data()));
        if (running.compare(target_canonical, Qt::CaseInsensitive) == 0) {
            return true;
        }
    } while (Process32NextW(snap, &entry));
    return false;
#else
    return false;
#endif
}

UpdateService::UpdateService(QNetworkAccessManager& network, InstallContext context,
                             QObject* parent)
    : QObject(parent), network_(network), context_(std::move(context)),
      check_request_(new UpdateHttpRequest(network_, context_.urlPolicy, this)),
      ahpack_downloader_(new AhpackDownloader(network_, context_.urlPolicy, this)),
      zsync_downloader_(new ZsyncDownloader(network_, context_.urlPolicy, this)),
      initial_timer_(new QTimer(this)), periodic_timer_(new QTimer(this)) {
    if (!context_.githubFallbackUrl.isValid()) {
        context_.githubFallbackUrl = defaultGitHubFallbackUrl(platformId());
    }
    if (context_.updaterHelperPath.isEmpty() && !context_.installDir.isEmpty()) {
        context_.updaterHelperPath =
            QDir(context_.installDir).filePath(QStringLiteral("AnimeHubUpdater.exe"));
    }

    initial_timer_->setSingleShot(true);
    connect(initial_timer_, &QTimer::timeout, this, [this] {
        checkNow(false);
        if (periodic_timer_->interval() > 0) {
            periodic_timer_->start();
        }
    });
    connect(periodic_timer_, &QTimer::timeout, this, [this] { checkNow(false); });

    connect(ahpack_downloader_, &AhpackDownloader::progressChanged, this,
            [this](qint64 done, qint64 total, qint64 speed) {
                downloaded_bytes_ = done;
                total_bytes_ = total;
                speed_bytes_per_sec_ = speed;
                emit progressChanged(done, total, speed);
                emit changed();
            });
    connect(
        ahpack_downloader_, &AhpackDownloader::finished, this,
        [this](bool ok, const QList<UpdatePlanFile>& staged, const QString& err) {
            if (!ok) {
                error_message_ = err;
                setState(UpdateState::Error);
                emit errorOccurred(err);
                return;
            }
            setState(UpdateState::Verifying);
            staged_files_ = staged;
            if (!available_manifest_ || !current_diff_) {
                error_message_ = QStringLiteral("Update manifest disappeared during download");
                setState(UpdateState::Error);
                emit errorOccurred(error_message_);
                return;
            }
            const QString staged_manifest_path =
                QDir(updateWorkDir()).filePath(QStringLiteral("installed-manifest.new.json"));
            const QString staging_dir = QDir(updateWorkDir()).filePath(QStringLiteral("staging"));
            if (auto written = writeInstalledManifestForTarget(
                    staged_manifest_path, *available_manifest_, context_.installDir, staging_dir);
                !written) {
                error_message_ = written.error().message;
                setState(UpdateState::Error);
                emit errorOccurred(error_message_);
                return;
            }
            UpdatePlan plan;
            plan.protocol = kUpdaterProtocolVersion;
            plan.installDir = context_.installDir;
            plan.dataDir = context_.dataDir;
            plan.stagingDir = staging_dir;
            plan.backupDir = QDir(updateWorkDir()).filePath(QStringLiteral("backup"));
            plan.pendingEnginesDir =
                QDir(updateWorkDir()).filePath(QStringLiteral("pending-engines"));
            plan.oldVersion = context_.currentVersion.toString();
            plan.newVersion = available_manifest_->version.toString();
            plan.newReleaseSeq = available_manifest_->releaseSeq;
            plan.installedMode = context_.installMode == InstallMode::WindowsInstalled;
            plan.verifyToken = QUuid::createUuid().toString(QUuid::Id128);
            plan.newInstalledManifestPath = staged_manifest_path;
            plan.filesToInstall = staged_files_;
            plan.filesToDelete = current_diff_->filesToDelete;
            prepared_plan_ = std::move(plan);

            downloaded_bytes_ = total_bytes_;
            setState(UpdateState::Ready);
            emit readyToApply(available_manifest_->version.toString());
        });

    connect(zsync_downloader_, &ZsyncDownloader::progressChanged, this,
            [this](qint64 done, qint64 total, qint64 speed) {
                downloaded_bytes_ = done;
                total_bytes_ = total;
                speed_bytes_per_sec_ = speed;
                emit progressChanged(done, total, speed);
                emit changed();
            });
    connect(zsync_downloader_, &ZsyncDownloader::finished, this,
            [this](bool ok, const QString& path, bool /*used_delta*/, const QString& err) {
                if (!ok) {
                    error_message_ = err;
                    setState(UpdateState::Error);
                    emit errorOccurred(err);
                    return;
                }
                staged_appimage_path_ = path;
                downloaded_bytes_ = total_bytes_;
                setState(UpdateState::Ready);
                if (available_manifest_) {
                    emit readyToApply(available_manifest_->version.toString());
                }
            });

    loadPersistentState();
    ingestRollbackMarkerIfPresent();
}

UpdateService::~UpdateService() {
    stopBackgroundChecks();
    cancelDownload();
}

void UpdateService::setUpdateMode(UpdateMode mode) {
    if (update_mode_ == mode) {
        return;
    }
    update_mode_ = mode;
    if (update_mode_ == UpdateMode::Manual) {
        stopBackgroundChecks();
    }
    emit changed();
}

bool UpdateService::isMajorUpdate() const {
    if (!available_manifest_) {
        return false;
    }
    return available_manifest_->isMajor ||
           available_manifest_->version.isMajorJumpFrom(context_.currentVersion);
}

void UpdateService::startBackgroundChecks(std::chrono::milliseconds initial_delay,
                                          std::chrono::milliseconds interval) {
    stopBackgroundChecks();
    if (context_.isolatedProfile || update_mode_ == UpdateMode::Manual ||
        context_.installMode == InstallMode::Unsupported) {
        return;
    }
    periodic_timer_->setInterval(static_cast<int>(interval.count()));
    initial_timer_->start(static_cast<int>(initial_delay.count()));
}

void UpdateService::stopBackgroundChecks() {
    initial_timer_->stop();
    periodic_timer_->stop();
}

void UpdateService::checkNow(bool user_triggered) {
    if (isBusy()) {
        return;
    }
    user_triggered_check_ = user_triggered;
    error_message_.clear();
    paused_ = false;
    min_supported_violated_ = false;
    requires_manual_update_ = false;
    setState(UpdateState::Checking);
    fetchApiRelease(user_triggered);
}

void UpdateService::downloadNow() {
    if (!available_manifest_ || requires_manual_update_) {
        return;
    }
    if (state_ == UpdateState::Downloading || state_ == UpdateState::Verifying ||
        state_ == UpdateState::Applying) {
        return;
    }
    error_message_.clear();
    downloaded_bytes_ = 0;
    speed_bytes_per_sec_ = 0;
    prepared_plan_.reset();
    prepared_plan_path_.clear();

    if (context_.installMode == InstallMode::WindowsInstalled ||
        context_.installMode == InstallMode::WindowsPortable) {
        if (!current_diff_) {
            auto diff = computeAhpackDiff(context_.installDir, *available_manifest_);
            if (!diff) {
                error_message_ = diff.error().message;
                setState(UpdateState::Error);
                emit errorOccurred(error_message_);
                return;
            }
            current_diff_ = std::move(*diff);
        }
        total_bytes_ = current_diff_->downloadBytes;
        const QString staging_dir = QDir(updateWorkDir()).filePath(QStringLiteral("staging"));
        QDir(staging_dir).removeRecursively();
        setState(UpdateState::Downloading);
        ahpack_downloader_->start(*available_manifest_, *current_diff_, staging_dir);
        return;
    }

    if (context_.installMode == InstallMode::LinuxAppImage) {
        if (!available_manifest_->appimage || !available_manifest_->appimage->isValid()) {
            error_message_ = QStringLiteral("Release manifest has no valid AppImage artifact");
            setState(UpdateState::Error);
            emit errorOccurred(error_message_);
            return;
        }
        total_bytes_ = available_manifest_->appimage->size;
        const QFileInfo exe_fi(context_.executablePath);
        const QString staged_path =
            QDir(exe_fi.absolutePath())
                .filePath(QStringLiteral(".animehub-update-%1.AppImage.new")
                              .arg(available_manifest_->version.toString()));
        setState(UpdateState::Downloading);
        zsync_downloader_->start(*available_manifest_->appimage, context_.executablePath,
                                 staged_path);
        return;
    }

    requires_manual_update_ = true;
    emit changed();
}

void UpdateService::cancelDownload() {
    if (check_request_) {
        check_request_->cancel();
    }
    if (ahpack_downloader_) {
        ahpack_downloader_->cancel();
    }
    if (zsync_downloader_) {
        zsync_downloader_->cancel();
    }
    if (state_ == UpdateState::Downloading || state_ == UpdateState::Verifying) {
        downloaded_bytes_ = 0;
        speed_bytes_per_sec_ = 0;
        setState(available_manifest_ ? UpdateState::Available : UpdateState::Idle);
    }
}

bool UpdateService::applyAndRestart(const QStringList& relaunch_args) {
    return applyPreparedUpdate(relaunch_args, true);
}

bool UpdateService::applyOnQuitIfReady(const QStringList& relaunch_args) {
    if (state_ != UpdateState::Ready || !available_manifest_) {
        return false;
    }
    // Silent auto-apply on quit is forbidden on major jumps, non-Auto modes, isolated profiles,
    // and whenever metadata came from GitHub fallback instead of the API control plane.
    if (context_.isolatedProfile || update_mode_ != UpdateMode::Auto || isMajorUpdate() ||
        last_check_source_ != CheckSource::Api) {
        return false;
    }
    return applyPreparedUpdate(relaunch_args, false);
}

bool UpdateService::consumePendingEnginesOnStartup() {
    const QString pending_dir = QDir(updateWorkDir()).filePath(QStringLiteral("pending-engines"));
    if (!QDir(pending_dir).exists()) {
        return false;
    }
    const QString mpv_exe =
        QDir(context_.installDir).filePath(QStringLiteral("engines/mpv/mpv.exe"));
    const bool mpv_running = context_.hooks.isMpvRunning ? context_.hooks.isMpvRunning(mpv_exe)
                                                         : isExecutableRunningAtPath(mpv_exe);
    if (mpv_running) {
        return false;
    }

    bool all_ok = true;
    bool moved_any = false;
    QDirIterator it(pending_dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = QDir(pending_dir).relativeFilePath(src);
        if (!isSafeRelativeUpdatePath(rel)) {
            continue;
        }
        const QString dst = QDir(context_.installDir).filePath(rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (QFileInfo::exists(dst)) {
            QFile::remove(dst);
        }
        if (QFile::rename(src, dst)) {
            moved_any = true;
        } else {
            all_ok = false;
        }
    }
    if (all_ok) {
        QDir(pending_dir).removeRecursively();
    }
    return moved_any && all_ok;
}

bool UpdateService::confirmPostUpdateHealthy(const QString& token) {
    const bool signaled = context_.hooks.signalHealthyEvent
                              ? context_.hooks.signalHealthyEvent(token)
                              : defaultSignalHealthyEvent(token);
    last_applied_version_ = context_.currentVersion.toString();
    // Clean up Linux AppImage `.prev` backup once the new version booted cleanly.
    if (context_.installMode == InstallMode::LinuxAppImage && !context_.executablePath.isEmpty()) {
        QFile::remove(context_.executablePath + QStringLiteral(".prev"));
    }
    // Remove temporary helper copy if left behind.
    QFile::remove(QDir(updateWorkDir()).filePath(QStringLiteral("AnimeHubUpdater-run.exe")));
    savePersistentState();
    emit changed();
    return signaled;
}

void UpdateService::ingestRollbackMarkerIfPresent() {
    const QString marker_path = QDir(updateWorkDir()).filePath(QStringLiteral("rollback.json"));
    QFile file(marker_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const QString failed_ver = obj.value(QStringLiteral("failedVersion")).toString().trimmed();
    const QString reason = obj.value(QStringLiteral("reason")).toString().trimmed();
    QFile::remove(marker_path);

    if (!failed_ver.isEmpty()) {
        failed_versions_.insert(failed_ver);
        last_rollback_version_ = failed_ver;
        last_rollback_reason_ = reason;
        savePersistentState();
        emit rollbackDetected(failed_ver, reason);
        emit changed();
    }
}

void UpdateService::acknowledgeLastAppliedVersion() {
    if (last_applied_version_.isEmpty()) {
        return;
    }
    last_applied_version_.clear();
    savePersistentState();
    emit changed();
}

void UpdateService::acknowledgeRollback() {
    if (last_rollback_version_.isEmpty() && last_rollback_reason_.isEmpty()) {
        return;
    }
    last_rollback_version_.clear();
    last_rollback_reason_.clear();
    savePersistentState();
    emit changed();
}

void UpdateService::setState(UpdateState next) {
    if (state_ == next) {
        return;
    }
    state_ = next;
    emit stateChanged(state_);
    emit changed();
}

void UpdateService::loadPersistentState() {
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& v : root.value(QStringLiteral("revokedKeyIds")).toArray()) {
        const QString id = v.toString().trimmed();
        if (!id.isEmpty()) {
            revoked_key_ids_.insert(id);
        }
    }
    for (const QJsonValue& v : root.value(QStringLiteral("failedVersions")).toArray()) {
        const QString ver = v.toString().trimmed();
        if (!ver.isEmpty()) {
            failed_versions_.insert(ver);
        }
    }
    persisted_release_seq_ = static_cast<std::uint64_t>(
        std::max<qint64>(0, root.value(QStringLiteral("installedReleaseSeq")).toInteger(0)));
    last_applied_version_ = root.value(QStringLiteral("lastAppliedVersion")).toString();
    last_rollback_version_ = root.value(QStringLiteral("lastRollbackVersion")).toString();
    last_rollback_reason_ = root.value(QStringLiteral("lastRollbackReason")).toString();
    last_checked_at_ms_ = root.value(QStringLiteral("lastCheckedAtMs")).toInteger(0);
}

void UpdateService::savePersistentState() {
    if (context_.dataDir.isEmpty()) {
        return;
    }
    QDir().mkpath(context_.dataDir);
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);

    QJsonArray revoked;
    QStringList revoked_list = revoked_key_ids_.values();
    revoked_list.sort();
    for (const QString& id : revoked_list) {
        revoked.append(id);
    }
    root.insert(QStringLiteral("revokedKeyIds"), revoked);

    QJsonArray failed;
    QStringList failed_list = failed_versions_.values();
    failed_list.sort();
    for (const QString& ver : failed_list) {
        failed.append(ver);
    }
    root.insert(QStringLiteral("failedVersions"), failed);

    root.insert(QStringLiteral("installedReleaseSeq"),
                static_cast<qint64>(effectiveInstalledReleaseSeq()));
    root.insert(QStringLiteral("lastAppliedVersion"), last_applied_version_);
    root.insert(QStringLiteral("lastRollbackVersion"), last_rollback_version_);
    root.insert(QStringLiteral("lastRollbackReason"), last_rollback_reason_);
    root.insert(QStringLiteral("lastCheckedAtMs"), last_checked_at_ms_);

    QSaveFile out(stateFilePath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.commit();
    }
}

std::uint64_t UpdateService::effectiveInstalledReleaseSeq() const {
    std::uint64_t seq = persisted_release_seq_;
    if (!context_.installDir.isEmpty()) {
        if (const auto installed = loadInstalledManifest(
                QDir(context_.installDir).filePath(QStringLiteral("installed-manifest.json")));
            installed) {
            seq = std::max(seq, installed->releaseSeq);
        }
    }
    return seq;
}

void UpdateService::fetchApiRelease(bool user_triggered) {
    if (!context_.apiBaseUrl.isValid()) {
        fetchGitHubFallback(user_triggered);
        return;
    }
    const QUrl endpoint = resolveApiEndpoint(
        context_.apiBaseUrl, QStringLiteral("desktop/releases/latest"), context_.channel);
    check_request_->start({endpoint}, std::nullopt, [this, user_triggered](HttpFetchResult res) {
        if (!res.ok) {
            fetchGitHubFallback(user_triggered);
            return;
        }
        auto parsed = parseApiReleaseResponse(res.body, platformId(), context_.urlPolicy);
        if (!parsed) {
            fetchGitHubFallback(user_triggered);
            return;
        }
        if (parsed->paused) {
            paused_ = true;
            last_check_source_ = CheckSource::Api;
            available_manifest_.reset();
            current_diff_.reset();
            last_checked_at_ms_ = QDateTime::currentMSecsSinceEpoch();
            savePersistentState();
            UpdateCheckResult out;
            out.paused = true;
            out.source = CheckSource::Api;
            out.minSupportedVersion = parsed->minSupportedVersion;
            if (parsed->minSupportedVersion &&
                context_.currentVersion < *parsed->minSupportedVersion) {
                min_supported_violated_ = true;
                out.minSupportedViolated = true;
            }
            setState(UpdateState::Idle);
            finishCheck(std::move(out));
            return;
        }
        if (!parsed->inlineEnvelopeJson.isEmpty()) {
            processVerifiedEnvelope(parsed->inlineEnvelopeJson, CheckSource::Api,
                                    parsed->extraAssetUrls, parsed->minSupportedVersion,
                                    user_triggered);
            return;
        }
        fetchManifestFromUrls(parsed->manifestUrls, CheckSource::Api, parsed->extraAssetUrls,
                              parsed->minSupportedVersion, user_triggered);
    });
}

void UpdateService::fetchManifestFromUrls(QList<QUrl> urls, CheckSource source,
                                          const QHash<QString, QList<QUrl>>& extra_mirrors,
                                          std::optional<SemVer> min_supported,
                                          bool user_triggered) {
    check_request_->start(
        std::move(urls), std::nullopt,
        [this, source, extra_mirrors, min_supported, user_triggered](HttpFetchResult res) {
            if (!res.ok) {
                if (source == CheckSource::Api) {
                    fetchGitHubFallback(user_triggered);
                    return;
                }
                error_message_ = res.errorMessage;
                setState(user_triggered ? UpdateState::Error : UpdateState::Idle);
                UpdateCheckResult out;
                out.source = source;
                out.errorMessage = res.errorMessage;
                if (user_triggered) {
                    emit errorOccurred(error_message_);
                }
                finishCheck(std::move(out));
                return;
            }
            processVerifiedEnvelope(res.body, source, extra_mirrors, min_supported, user_triggered);
        });
}

void UpdateService::fetchGitHubFallback(bool user_triggered) {
    if (!context_.githubFallbackUrl.isValid()) {
        error_message_ = QStringLiteral("No fallback release URL configured");
        setState(user_triggered ? UpdateState::Error : UpdateState::Idle);
        UpdateCheckResult out;
        out.errorMessage = error_message_;
        if (user_triggered) {
            emit errorOccurred(error_message_);
        }
        finishCheck(std::move(out));
        return;
    }
    fetchManifestFromUrls({context_.githubFallbackUrl}, CheckSource::GitHubFallback, {},
                          std::nullopt, user_triggered);
}

void UpdateService::processVerifiedEnvelope(const QByteArray& envelope_json, CheckSource source,
                                            const QHash<QString, QList<QUrl>>& extra_mirrors,
                                            std::optional<SemVer> min_supported,
                                            bool user_triggered) {
    last_check_source_ = source;
    last_checked_at_ms_ = QDateTime::currentMSecsSinceEpoch();

    auto verified = verifyAndParseManifestEnvelope(
        envelope_json, context_.keyRing, revoked_key_ids_, platformId(), context_.urlPolicy);
    if (!verified) {
        error_message_ = verified.error().message;
        savePersistentState();
        setState(user_triggered ? UpdateState::Error : UpdateState::Idle);
        UpdateCheckResult out;
        out.source = source;
        out.errorMessage = error_message_;
        if (user_triggered) {
            emit errorOccurred(error_message_);
        }
        finishCheck(std::move(out));
        return;
    }

    UpdateManifest manifest = std::move(*verified);
    if (manifest.verifiedByRecoveryKey && !manifest.revokedKeyIds.isEmpty()) {
        for (const QString& id : manifest.revokedKeyIds) {
            revoked_key_ids_.insert(id);
        }
    }
    if (!extra_mirrors.isEmpty()) {
        mergeExtraMirrorUrls(manifest, extra_mirrors, context_.urlPolicy);
    }

    UpdateCheckResult out;
    out.source = source;
    out.minSupportedVersion = min_supported;
    if (min_supported && context_.currentVersion < *min_supported) {
        min_supported_violated_ = true;
        out.minSupportedViolated = true;
    }

    auto eligible =
        checkUpgradeEligibility(manifest, context_.currentVersion, effectiveInstalledReleaseSeq());
    if (!eligible) {
        error_message_ = eligible.error().message;
        savePersistentState();
        setState(user_triggered ? UpdateState::Error : UpdateState::Idle);
        out.errorMessage = error_message_;
        if (user_triggered) {
            emit errorOccurred(error_message_);
        }
        finishCheck(std::move(out));
        return;
    }
    if (!*eligible) {
        available_manifest_.reset();
        current_diff_.reset();
        savePersistentState();
        setState(UpdateState::Idle);
        finishCheck(std::move(out));
        return;
    }

    // Anti-loop guard: never silently retry a version that already triggered a rollback.
    const QString ver_str = manifest.version.toString();
    if (!user_triggered && failed_versions_.contains(ver_str)) {
        out.skippedFailedVersion = true;
        savePersistentState();
        setState(UpdateState::Idle);
        finishCheck(std::move(out));
        return;
    }

    const bool manual_only = manifest.minUpdaterProtocol > kUpdaterProtocolVersion ||
                             manifest.requiresFullInstaller ||
                             context_.installMode == InstallMode::LinuxDeb ||
                             context_.installMode == InstallMode::Unsupported;
    requires_manual_update_ = manual_only;

    qint64 est_bytes = 0;
    if (!manual_only && (context_.installMode == InstallMode::WindowsInstalled ||
                         context_.installMode == InstallMode::WindowsPortable)) {
        auto diff = computeAhpackDiff(context_.installDir, manifest);
        if (!diff) {
            error_message_ = diff.error().message;
            savePersistentState();
            setState(user_triggered ? UpdateState::Error : UpdateState::Idle);
            out.errorMessage = error_message_;
            if (user_triggered) {
                emit errorOccurred(error_message_);
            }
            finishCheck(std::move(out));
            return;
        }
        est_bytes = diff->downloadBytes;
        current_diff_ = std::move(*diff);
    } else if (!manual_only && context_.installMode == InstallMode::LinuxAppImage &&
               manifest.appimage) {
        est_bytes = manifest.appimage->size;
    }

    available_manifest_ = manifest;
    total_bytes_ = est_bytes;
    downloaded_bytes_ = 0;
    speed_bytes_per_sec_ = 0;
    savePersistentState();

    out.updateAvailable = true;
    out.requiresManualUpdate = manual_only;
    out.manifest = manifest;
    out.estimatedDownloadBytes = est_bytes;

    setState(UpdateState::Available);
    finishCheck(out);

    // Silent background download ONLY in Auto mode, non-major, API source (never GitHub fallback),
    // and non-isolated profile.
    if (!user_triggered && !manual_only && !context_.isolatedProfile &&
        update_mode_ == UpdateMode::Auto && !isMajorUpdate() && source == CheckSource::Api) {
        downloadNow();
    }
}

void UpdateService::finishCheck(UpdateCheckResult result) {
    emit checkFinished(result);
    emit changed();
}

std::expected<void, UpdateError>
UpdateService::createPreUpdateSnapshots(QString& out_db_backup, QString& out_settings_backup) {
    if (context_.dataDir.isEmpty()) {
        return {};
    }
    const QString old_ver = context_.currentVersion.toString();
    const QString db_path = QDir(context_.dataDir).filePath(QStringLiteral("library.sqlite3"));
    if (QFileInfo::exists(db_path)) {
        const QString db_bak =
            QDir(context_.dataDir)
                .filePath(QStringLiteral("library.sqlite3.pre-update-%1.bak").arg(old_ver));
        auto snap = context_.hooks.snapshotDatabase
                        ? context_.hooks.snapshotDatabase(db_path, db_bak)
                        : defaultSnapshotDatabase(db_path, db_bak);
        if (!snap) {
            return snap;
        }
        out_db_backup = db_bak;
        pruneOldBackups(QStringLiteral("library.sqlite3.pre-update-*.bak"), kKeepPreUpdateBackups);
    }

    const QString settings_path = QDir(context_.dataDir).filePath(QStringLiteral("settings.ini"));
    if (QFileInfo::exists(settings_path)) {
        const QString settings_bak =
            QDir(context_.dataDir)
                .filePath(QStringLiteral("settings.ini.pre-update-%1.bak").arg(old_ver));
        QFile::remove(settings_bak);
        if (!QFile::copy(settings_path, settings_bak)) {
            return std::unexpected(
                UpdateError{QStringLiteral("settings_backup"),
                            QStringLiteral("Could not back up settings.ini before update")});
        }
        out_settings_backup = settings_bak;
        pruneOldBackups(QStringLiteral("settings.ini.pre-update-*.bak"), kKeepPreUpdateBackups);
    }
    return {};
}

void UpdateService::pruneOldBackups(const QString& pattern, int keep_count) const {
    QDir dir(context_.dataDir);
    const QFileInfoList files =
        dir.entryInfoList({pattern}, QDir::Files, QDir::Time); // newest first
    for (qsizetype i = keep_count; i < files.size(); ++i) {
        QFile::remove(files[i].absoluteFilePath());
    }
}

std::expected<QString, UpdateError> UpdateService::writePlanJson(const UpdatePlan& plan) const {
    QJsonObject root;
    root.insert(QStringLiteral("protocol"), plan.protocol);
    root.insert(QStringLiteral("installDir"), plan.installDir);
    root.insert(QStringLiteral("dataDir"), plan.dataDir);
    root.insert(QStringLiteral("stagingDir"), plan.stagingDir);
    root.insert(QStringLiteral("backupDir"), plan.backupDir);
    root.insert(QStringLiteral("pendingEnginesDir"), plan.pendingEnginesDir);
    root.insert(QStringLiteral("oldVersion"), plan.oldVersion);
    root.insert(QStringLiteral("newVersion"), plan.newVersion);
    root.insert(QStringLiteral("newReleaseSeq"), static_cast<qint64>(plan.newReleaseSeq));
    root.insert(QStringLiteral("installedMode"), plan.installedMode);
    root.insert(QStringLiteral("parentPid"), plan.parentPid);
    root.insert(QStringLiteral("verifyToken"), plan.verifyToken);
    root.insert(QStringLiteral("dbPath"), plan.dbPath);
    root.insert(QStringLiteral("dbBackupPath"), plan.dbBackupPath);
    root.insert(QStringLiteral("settingsPath"), plan.settingsPath);
    root.insert(QStringLiteral("settingsBackupPath"), plan.settingsBackupPath);
    root.insert(QStringLiteral("newInstalledManifestPath"), plan.newInstalledManifestPath);

    QJsonArray args;
    for (const QString& a : plan.relaunchArgs) {
        args.append(a);
    }
    root.insert(QStringLiteral("relaunchArgs"), args);

    QJsonArray install_arr;
    for (const auto& f : plan.filesToInstall) {
        QJsonObject obj;
        obj.insert(QStringLiteral("relativePath"), f.relativePath);
        obj.insert(QStringLiteral("stagedPath"), f.stagedPath);
        obj.insert(QStringLiteral("sha256"), QString::fromLatin1(f.sha256));
        obj.insert(QStringLiteral("size"), f.size);
        obj.insert(QStringLiteral("executable"), f.executable);
        install_arr.append(obj);
    }
    root.insert(QStringLiteral("filesToInstall"), install_arr);

    QJsonArray del_arr;
    for (const QString& p : plan.filesToDelete) {
        del_arr.append(p);
    }
    root.insert(QStringLiteral("filesToDelete"), del_arr);

    const QString plan_path = QDir(updateWorkDir()).filePath(QStringLiteral("plan.json"));
    QDir().mkpath(updateWorkDir());
    QSaveFile out(plan_path);
    if (!out.open(QIODevice::WriteOnly)) {
        return std::unexpected(
            UpdateError{QStringLiteral("plan_write"),
                        QStringLiteral("Could not open '%1' for writing").arg(plan_path)});
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (out.write(bytes) != bytes.size() || !out.commit()) {
        return std::unexpected(UpdateError{
            QStringLiteral("plan_write"), QStringLiteral("Failed committing '%1'").arg(plan_path)});
    }
    return plan_path;
}

bool UpdateService::applyPreparedUpdate(const QStringList& relaunch_args,
                                        bool launch_restarted_app) {
    if (state_ != UpdateState::Ready || !available_manifest_) {
        return false;
    }

    if (context_.installMode == InstallMode::LinuxAppImage) {
        return applyLinuxAppImageUpdate(relaunch_args, launch_restarted_app);
    }

    if (!prepared_plan_) {
        return false;
    }

    setState(UpdateState::Applying);

    QString db_bak;
    QString settings_bak;
    if (auto snapped = createPreUpdateSnapshots(db_bak, settings_bak); !snapped) {
        error_message_ = snapped.error().message;
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }

    prepared_plan_->parentPid = context_.hooks.currentPid ? context_.hooks.currentPid()
                                                          : QCoreApplication::applicationPid();
    prepared_plan_->relaunchArgs = relaunch_args;
    prepared_plan_->dbPath = QDir(context_.dataDir).filePath(QStringLiteral("library.sqlite3"));
    prepared_plan_->dbBackupPath = db_bak;
    prepared_plan_->settingsPath = QDir(context_.dataDir).filePath(QStringLiteral("settings.ini"));
    prepared_plan_->settingsBackupPath = settings_bak;

    // If mpv.exe is already running now, move any staged engines/mpv/* into pending-engines
    // proactively (the helper also checks again right before swapping).
    const QString mpv_exe =
        QDir(context_.installDir).filePath(QStringLiteral("engines/mpv/mpv.exe"));
    const bool mpv_running = context_.hooks.isMpvRunning ? context_.hooks.isMpvRunning(mpv_exe)
                                                         : isExecutableRunningAtPath(mpv_exe);
    if (mpv_running) {
        QList<UpdatePlanFile> remaining;
        for (const auto& f : prepared_plan_->filesToInstall) {
            if (f.relativePath.startsWith(QLatin1StringView("engines/mpv/"), Qt::CaseInsensitive)) {
                const QString deferred_dst =
                    QDir(prepared_plan_->pendingEnginesDir).filePath(f.relativePath);
                QDir().mkpath(QFileInfo(deferred_dst).absolutePath());
                QFile::remove(deferred_dst);
                if (!QFile::rename(f.stagedPath, deferred_dst)) {
                    remaining.append(f);
                }
            } else {
                remaining.append(f);
            }
        }
        prepared_plan_->filesToInstall = std::move(remaining);
    }

    auto plan_path = writePlanJson(*prepared_plan_);
    if (!plan_path) {
        error_message_ = plan_path.error().message;
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }
    prepared_plan_path_ = *plan_path;

    // Copy helper outside the install root so the helper can even update AnimeHubUpdater.exe.
    QString helper_to_run = context_.updaterHelperPath;
    if (QFileInfo::exists(context_.updaterHelperPath)) {
        const QString copied_helper =
            QDir(updateWorkDir()).filePath(QStringLiteral("AnimeHubUpdater-run.exe"));
        QFile::remove(copied_helper);
        if (QFile::copy(context_.updaterHelperPath, copied_helper)) {
            helper_to_run = copied_helper;
        }
    }

    QStringList helper_args{QStringLiteral("--apply"), *plan_path};
    if (!launch_restarted_app) {
        helper_args.append(QStringLiteral("--no-relaunch"));
    }

    auto launched = context_.hooks.launchUpdater
                        ? context_.hooks.launchUpdater(helper_to_run, helper_args)
                        : defaultLaunchUpdater(helper_to_run, helper_args);
    if (!launched) {
        error_message_ = launched.error().message;
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }

    persisted_release_seq_ = available_manifest_->releaseSeq;
    savePersistentState();

    if (launch_restarted_app && context_.hooks.requestAppQuit) {
        context_.hooks.requestAppQuit();
    }
    return true;
}

bool UpdateService::applyLinuxAppImageUpdate(const QStringList& relaunch_args, bool relaunch) {
    if (staged_appimage_path_.isEmpty() || !QFileInfo::exists(staged_appimage_path_) ||
        context_.executablePath.isEmpty()) {
        error_message_ = QStringLiteral("Staged AppImage is missing");
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }
    setState(UpdateState::Applying);

    QString db_bak;
    QString settings_bak;
    if (auto snapped = createPreUpdateSnapshots(db_bak, settings_bak); !snapped) {
        error_message_ = snapped.error().message;
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }

    // Re-verify SHA-256 of staged AppImage right before atomic rename.
    if (available_manifest_->appimage) {
        const auto sha = computeFileSha256(staged_appimage_path_);
        if (!sha || *sha != available_manifest_->appimage->sha256) {
            error_message_ = QStringLiteral("Staged AppImage failed final SHA-256 verification");
            setState(UpdateState::Error);
            emit errorOccurred(error_message_);
            return false;
        }
    }

#ifndef Q_OS_WIN
    ::chmod(staged_appimage_path_.toLocal8Bit().constData(), 0755);
#endif

    const QString prev_path = context_.executablePath + QStringLiteral(".prev");
    QFile::remove(prev_path);
    if (!QFile::rename(context_.executablePath, prev_path)) {
        error_message_ = QStringLiteral("Could not move current AppImage aside for atomic swap");
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }
    if (!QFile::rename(staged_appimage_path_, context_.executablePath)) {
        // Atomic rollback of the rename itself.
        QFile::rename(prev_path, context_.executablePath);
        error_message_ = QStringLiteral("Could not rename staged AppImage into place");
        setState(UpdateState::Error);
        emit errorOccurred(error_message_);
        return false;
    }

    persisted_release_seq_ = available_manifest_->releaseSeq;
    last_applied_version_ = available_manifest_->version.toString();
    savePersistentState();

    if (relaunch) {
        QStringList args{QStringLiteral("--post-update-verify"),
                         QUuid::createUuid().toString(QUuid::Id128)};
        args.append(relaunch_args);
        auto launched = context_.hooks.launchUpdater
                            ? context_.hooks.launchUpdater(context_.executablePath, args)
                            : defaultLaunchUpdater(context_.executablePath, args);
        if (!launched) {
            // Rollback immediately if new AppImage fails to spawn.
            QFile::remove(context_.executablePath);
            QFile::rename(prev_path, context_.executablePath);
            failed_versions_.insert(available_manifest_->version.toString());
            savePersistentState();
            error_message_ = launched.error().message;
            setState(UpdateState::Error);
            emit errorOccurred(error_message_);
            return false;
        }
        if (context_.hooks.requestAppQuit) {
            context_.hooks.requestAppQuit();
        }
    }
    return true;
}

QString UpdateService::updateWorkDir() const {
    return QDir(context_.installDir).filePath(QStringLiteral(".update"));
}

QString UpdateService::stateFilePath() const {
    return QDir(context_.dataDir).filePath(QStringLiteral("update-state.json"));
}

QString UpdateService::platformId() const {
    return platformIdForMode(context_.installMode);
}

} // namespace animehub::update
