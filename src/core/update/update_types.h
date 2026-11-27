#ifndef ANIMEHUB_CORE_UPDATE_TYPES_H
#define ANIMEHUB_CORE_UPDATE_TYPES_H

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <compare>
#include <cstdint>
#include <optional>

namespace animehub::update {

enum class InstallMode {
    WindowsInstalled,
    WindowsPortable,
    LinuxAppImage,
    LinuxDeb,
    Unsupported,
};

enum class UpdateMode {
    Auto,
    NotifyOnly,
    Manual,
};

enum class UpdateState {
    Idle,
    Checking,
    Available,
    Downloading,
    Verifying,
    Ready,
    Applying,
    Error,
};

enum class CheckSource {
    None,
    Api,
    GitHubFallback,
};

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const SemVer&) const = default;

    [[nodiscard]] bool isValid() const {
        return major >= 0 && minor >= 0 && patch >= 0 && (major > 0 || minor > 0 || patch > 0);
    }

    [[nodiscard]] bool isMajorJumpFrom(const SemVer& current) const {
        return major > current.major;
    }

    [[nodiscard]] QString toString() const {
        return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    }

    [[nodiscard]] static std::optional<SemVer> parse(const QString& text) {
        const QString trimmed = text.trimmed();
        const QString normalized = trimmed.startsWith(QLatin1Char('v')) ? trimmed.mid(1) : trimmed;
        const QStringList parts = normalized.split(QLatin1Char('.'));
        if (parts.size() != 3) {
            return std::nullopt;
        }
        int values[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            const QString& part = parts[i];
            if (part.isEmpty() || (part.size() > 1 && part.startsWith(QLatin1Char('0')))) {
                return std::nullopt;
            }
            for (const QChar ch : part) {
                if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) {
                    return std::nullopt;
                }
            }
            bool ok = false;
            const int parsed = part.toInt(&ok);
            if (!ok || parsed < 0) {
                return std::nullopt;
            }
            values[i] = parsed;
        }
        return SemVer{.major = values[0], .minor = values[1], .patch = values[2]};
    }
};

struct UpdateSolidMember {
    QString path;
    qint64 size = 0;
    QByteArray sha256; // 64 lowercase hex ASCII bytes
};

struct UpdateFileEntry {
    QString path;     // relative POSIX path, or group root e.g. "qml" for solid-tar
    QString kind;     // "file" or "solid-tar"
    QString encoding; // "zstd", "raw", or "tar+zstd"
    qint64 size = 0;
    QByteArray sha256; // 64 lowercase hex ASCII bytes
    qint64 packOffset = 0;
    qint64 packSize = 0;
    QString group;
    bool executable = false;
    QList<UpdateSolidMember> members; // populated when kind == "solid-tar"
};

struct UpdateArtifact {
    QString filename;
    qint64 size = 0;
    QByteArray sha256; // 64 lowercase hex ASCII bytes
    QList<QUrl> urls;
    QList<QUrl> zsyncUrls;

    [[nodiscard]] bool isValid() const {
        return !filename.isEmpty() && size > 0 && sha256.size() == 64 && !urls.isEmpty();
    }
};

struct UpdateManifest {
    int schemaVersion = 0;
    QString platform; // "win64" or "linux-x64"
    QString channel;  // "stable"
    SemVer version;
    std::uint64_t releaseSeq = 0;
    QString publishedAt;
    bool isMajor = false;
    int minUpdaterProtocol = 1;
    bool requiresFullInstaller = false;
    std::optional<SemVer> bridgeVersion;
    QString notes;
    QUrl releaseUrl;
    QStringList revokedKeyIds;

    std::optional<UpdateArtifact> pack;
    std::optional<UpdateArtifact> installer;
    std::optional<UpdateArtifact> portable;
    std::optional<UpdateArtifact> appimage;
    std::optional<UpdateArtifact> deb;

    QList<UpdateFileEntry> files;

    // Populated by verification, not read from untrusted payload.
    QString verifiedKeyId;
    bool verifiedByRecoveryKey = false;
};

struct UrlPolicy {
    bool allowInsecureLoopback = false;
    QStringList allowedHosts = {
        QStringLiteral("animehub-api.duardo.dev"),
        QStringLiteral("*.duardo.dev"),
        QStringLiteral("github.com"),
        QStringLiteral("objects.githubusercontent.com"),
        QStringLiteral("release-assets.githubusercontent.com"),
        QStringLiteral("github-releases.githubusercontent.com"),
    };
};

struct UpdateCheckResult {
    bool updateAvailable = false;
    bool paused = false;
    bool minSupportedViolated = false;
    bool requiresManualUpdate = false;
    bool skippedFailedVersion = false;
    CheckSource source = CheckSource::None;
    std::optional<SemVer> minSupportedVersion;
    std::optional<UpdateManifest> manifest;
    qint64 estimatedDownloadBytes = 0;
    QString errorMessage;
};

struct UpdatePlanFile {
    QString relativePath;
    QString stagedPath;
    QByteArray sha256;
    qint64 size = 0;
    bool executable = false;
};

struct UpdatePlan {
    int protocol = 1;
    QString installDir;
    QString dataDir;
    QString stagingDir;
    QString backupDir;
    QString pendingEnginesDir;
    QString oldVersion;
    QString newVersion;
    std::uint64_t newReleaseSeq = 0;
    bool installedMode = false;
    qint64 parentPid = 0;
    QString verifyToken;
    QStringList relaunchArgs;
    QString dbPath;
    QString dbBackupPath;
    QString settingsPath;
    QString settingsBackupPath;
    QString newInstalledManifestPath;
    QList<UpdatePlanFile> filesToInstall;
    QStringList filesToDelete;
};

struct UpdateError {
    QString code;
    QString message;
};

} // namespace animehub::update

#endif // ANIMEHUB_CORE_UPDATE_TYPES_H
