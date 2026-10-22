#ifndef ANIMEHUB_CORE_DOWNLOADS_TYPES_H
#define ANIMEHUB_CORE_DOWNLOADS_TYPES_H

#include "api/api_types.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace animehub::downloads {

enum class TaskState {
    Queued,      // waiting for a free slot
    Resolving,   // asking the API and the provider for a source
    Downloading, // aria2 is transferring
    Verifying,   // decrypting and checking a MEGA file
    Paused,      // stopped by the user
    Waiting,     // will retry by itself at `retryAt` (quota, rate limit, network)
    Completed,
    Failed, // needs the user (no source left, no space, repeated errors)
    Cancelled,
};

enum class TaskOrigin { Manual, Automatic };
enum class DownloadEngine { Native, JDownloaderLocal, JDownloaderCloud };

// Why a task failed or waits; the UI turns it into words.
enum class TaskIssue {
    None,
    NoSource,      // no native provider has the episode
    SourceRemoved, // the provider removed the file
    Quota,         // MEGA transfer quota exhausted
    RateLimited,   // AnimeHub API asked to slow down
    Network,       // repeated connection failures
    NoSpace,       // not enough free disk space
    Storage,       // the download folder cannot be written
    Integrity,     // the decrypted file does not match its MAC
    Engine,        // aria2 is missing or stopped
};

struct Task {
    qint64 id = 0;
    QString slug;
    double number = 0;
    QString animeTitle;
    api::Audio audio = api::Audio::Sub;
    TaskOrigin origin = TaskOrigin::Manual;
    TaskState state = TaskState::Queued;
    std::optional<api::Provider> provider;
    QString sourceId;   // provider file id (MEGA handle, Pixeldrain id); never a key or URL
    QString targetPath; // final path; without extension until the source is known
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    qint64 bytesPerSecond = 0; // live only, not stored
    TaskIssue issue = TaskIssue::None;
    QString issueDetail;
    int attempts = 0;
    QDateTime retryAt;
    qint64 queueOrder = 0;
    QDateTime createdAt;
    QDateTime finishedAt;
    bool pausedWithAll = false; // paused by pauseAll(), so resumeAll() brings it back
    DownloadEngine engine = DownloadEngine::Native;
    QString engineEndpoint;
    QString engineDevice;
    qint64 engineJobId = 0;
    QList<qint64> engineLinkIds;
    QString engineSubmission; // local intent UUID; never a URL or credential
    QString enginePhase;
    QString engineDestination;
    QString remoteRequestId;

    [[nodiscard]] bool active() const {
        return state != TaskState::Completed && state != TaskState::Cancelled &&
               state != TaskState::Failed;
    }
};

[[nodiscard]] QString stateKey(TaskState state);
[[nodiscard]] TaskState stateFromKey(QStringView key);
[[nodiscard]] QString issueKey(TaskIssue issue);
[[nodiscard]] TaskIssue issueFromKey(QStringView key);

} // namespace animehub::downloads

#endif // ANIMEHUB_CORE_DOWNLOADS_TYPES_H
