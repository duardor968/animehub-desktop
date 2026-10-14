#ifndef ANIMEHUB_CORE_DOWNLOADS_MANAGER_H
#define ANIMEHUB_CORE_DOWNLOADS_MANAGER_H

#include "api/api_client.h"
#include "aria2.h"
#include "download_repository.h"
#include "library/library_repository.h"
#include "mega.h"
#include "pixeldrain.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <memory>

namespace animehub::downloads {

struct DownloadSettings {
    QString directory;
    QList<api::Provider> providers{api::Provider::Mega, api::Provider::Pixeldrain};
    api::Audio audio = api::Audio::Sub;
    int maxConcurrent = 2;
};

// Runs the download queue: resolves a source through the API, transfers it with aria2, decrypts
// and verifies MEGA files, and records finished episodes in the library. State survives restarts;
// transient failures wait and retry by themselves, the rest stop for the user.
class DownloadManager final : public QObject {
    Q_OBJECT

public:
    struct Services {
        api::ApiClient& api;
        Aria2& aria2;
        mega::Api& mega;
        pixeldrain::Api& pixeldrain;
        DownloadRepository& repository;
        library::LibraryRepository& library;
    };

    DownloadManager(Services services, std::function<DownloadSettings()> settings,
                    QObject* parent = nullptr);
    ~DownloadManager() override;

    [[nodiscard]] static bool isNative(api::Provider provider);

    void start();
    // Gives aria2 the chance to save resume data, then calls `done`.
    void shutdown(std::function<void()> done);

    std::expected<qint64, QString> enqueue(const QString& slug, double number,
                                           const QString& anime_title, TaskOrigin origin);
    void pause(qint64 id);
    void resume(qint64 id);
    void cancel(qint64 id);
    void retry(qint64 id);
    void forget(qint64 id);
    void clearFinished();
    void pauseAll();
    void resumeAll();

    [[nodiscard]] QList<Task> tasks() const;
    [[nodiscard]] std::optional<Task> task(qint64 id) const;
    [[nodiscard]] std::optional<Task> activeTaskFor(const QString& slug, double number) const;

signals:
    void loaded(); // the queue saved by the previous session is ready
    void taskAdded(qint64 id);
    void taskChanged(qint64 id);
    void taskRemoved(qint64 id);
    void taskCompleted(qint64 id);
    void taskFailed(qint64 id);
    void engineFailed(const QString& message);

private:
    struct Live {
        quint64 generation = 0;
        QString gid;
        std::optional<mega::FileLink> megaLink;
        QString partialPath;
        std::shared_ptr<std::atomic<bool>> cancel;
        QDateTime savedAt;
    };

    void pump();
    void resolve(qint64 id);
    void tryCandidate(qint64 id, quint64 generation, QList<api::ResolvedLink> candidates, int index,
                      TaskIssue last_issue);
    void startTransfer(qint64 id, api::Provider provider, const QString& source_id, const QUrl& url,
                       qint64 size, const QString& provider_name,
                       std::optional<mega::FileLink> mega_link);
    void poll();
    void finishTransfer(qint64 id);
    void verifyMega(qint64 id);
    void complete(qint64 id, const QString& path);
    void transferFailed(qint64 id, int code, const QString& message);

    void wait(qint64 id, TaskIssue issue, std::chrono::seconds delay, const QString& detail = {});
    void retryLater(qint64 id, TaskIssue issue, const QString& detail = {});
    void fail(qint64 id, TaskIssue issue, const QString& detail = {});
    void setState(qint64 id, TaskState state);
    void store(const Task& task, bool announce = true);
    // Stops whatever moves the task's bytes; `stopped` runs once the files are free.
    void dropTransfer(qint64 id, std::function<void()> stopped = {});
    void deletePartials(const QString& final_path);
    void removeFiles(const QStringList& paths, int attempts_left);
    [[nodiscard]] bool isCurrent(qint64 id, quint64 generation, TaskState state) const;
    [[nodiscard]] int running() const;
    [[nodiscard]] QString basePathFor(const QString& anime_title, double number) const;

    Services services_;
    std::function<DownloadSettings()> settings_;
    QHash<qint64, Task> tasks_;
    QHash<qint64, Live> live_;
    QTimer poll_timer_;
    QTimer wake_timer_;
    bool started_ = false;
    bool stopping_ = false;
};

} // namespace animehub::downloads

#endif // ANIMEHUB_CORE_DOWNLOADS_MANAGER_H
