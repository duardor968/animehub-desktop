#ifndef ANIMEHUB_CORE_DOWNLOADS_MANAGER_H
#define ANIMEHUB_CORE_DOWNLOADS_MANAGER_H

#include "api/api_client.h"
#include "aria2.h"
#include "download_repository.h"
#include "jdownloader.h"
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
    // MEGA and Pixeldrain, in the user's order; JDownloader also gets MP4Upload and 1Fichier.
    QList<api::Provider> providers{api::Provider::Mega, api::Provider::Pixeldrain};
    api::Audio audio = api::Audio::Sub;
    int maxConcurrent = 2;
    DownloadEngine engine = DownloadEngine::Native;
    // While AnimeHub downloads, who takes an episode that neither MEGA nor Pixeldrain has, from
    // the servers AnimeHub does not download: nobody (Native) or JDownloader.
    DownloadEngine otherServers = DownloadEngine::Native;
    QUrl jdEndpoint{QStringLiteral("http://127.0.0.1:3128")};
    QString jdDeviceId;
};

// What a request for several episodes did.
struct Enqueued {
    int added = 0;
    int restarted = 0; // paused or failed ones that go on
    QList<qint64> taskIds;
};

// Runs the download queue: resolves a source through the API, transfers it with aria2, decrypts
// and verifies MEGA files, and records finished episodes in the library. State survives restarts;
// transient failures wait and retry by themselves, the rest stop for the user. An episode that
// neither MEGA nor Pixeldrain has goes, as the same task, to the JDownloader the settings name.
//
// The queue keeps each series together: series go in the order they arrived, or the one set by
// prioritize() and reorder(), and each series goes by episode number. Queueing never stops a
// running transfer; only prioritize() does.
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
        myjd::Client* myJDownloader = nullptr;
    };

    DownloadManager(Services services, std::function<DownloadSettings()> settings,
                    QObject* parent = nullptr);
    ~DownloadManager() override;

    [[nodiscard]] static bool isNative(api::Provider provider);

    // Read-only startup presents a cloned queue without resolving or controlling either engine.
    void start(bool transfers = true);
    // Gives aria2 the chance to save resume data, then calls `done`.
    void shutdown(std::function<void()> done);

    std::expected<qint64, QString> enqueue(const QString& slug, double number,
                                           const QString& anime_title, TaskOrigin origin);
    // Queues the episodes without a task and restarts the paused or failed ones.
    std::expected<Enqueued, QString>
    enqueueMany(const QString& slug, const QList<double>& numbers, const QString& anime_title,
                TaskOrigin origin, const QString& sourceRequestId = {},
                std::optional<DownloadSettings> fixedSettings = std::nullopt);
    void pause(qint64 id);
    void resume(qint64 id);
    void cancel(qint64 id);
    void retry(qint64 id);
    void forget(qint64 id);
    void clearFinished();
    void pauseAll();
    void resumeAll();

    void pauseSeries(const QString& slug);
    void resumeSeries(const QString& slug);
    void retryFailed(const QString& slug);  // failed and waiting tasks of the series
    void cancelSeries(const QString& slug); // every task not completed; partial files go
    void forgetSeries(const QString& slug); // completed, cancelled and failed rows leave the list
    // "Descargar ahora": the series goes first, its paused tasks go on, and it takes the running
    // slots it can use from the series last in line; those continue later from their partial
    // files.
    void prioritize(const QString& slug);
    // New order of the series in the queue; series left out follow in their current order.
    void reorder(const QStringList& slugs);

    // In queue order the tasks still in line (waiting, running, paused or failed), then the
    // completed and cancelled ones, newest first.
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
        bool jdBusy = false;
    };

    void pump();
    void resolve(qint64 id);
    void tryCandidate(qint64 id, quint64 generation, QList<api::ResolvedLink> candidates, int index,
                      TaskIssue last_issue);
    void startTransfer(qint64 id, api::Provider provider, const QString& source_id, const QUrl& url,
                       qint64 size, const QString& provider_name,
                       std::optional<mega::FileLink> mega_link);
    void poll();
    void pollJdTasks();
    // Points a task JDownloader does not have yet at the JDownloader the settings name; false
    // when they name none.
    bool connectJd(qint64 id, const DownloadSettings& settings);
    void submitJd(qint64 id, const api::ResolvedLink& link);
    // JDownloader does not have the episode: the task lets the hand-over go, so a retry sends it
    // again.
    void releaseJd(qint64 id);
    // The hand-over never reached JDownloader: the task stops with the reason and what to do.
    void unreachedJd(qint64 id, myjd::Failure failure);
    void pollJd(qint64 id);
    void collectJd(qint64 id);
    void jdRequest(qint64 id, const QString& path, const QJsonArray& params,
                   myjd::Client::Callback<QJsonValue> done);
    void observeJd(qint64 id, const QJsonArray& links);
    void cancelJd(qint64 id, bool collector = false);
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
    // Tasks holding one of the `maxConcurrent` slots of their kind: native ones looking for
    // their file, transferring or being checked; for JDownloader, the ones being handed over.
    // What JDownloader already has runs on its own and holds none.
    [[nodiscard]] int running(bool native) const;

    // The single-task steps behind the public calls; the callers pump once at the end.
    bool pauseTask(qint64 id, bool with_all = false);
    bool resumeTask(qint64 id);
    bool retryTask(qint64 id);
    void cancelTask(qint64 id);
    void requeue(qint64 id);

    [[nodiscard]] QList<qint64> idsOf(const QString& slug) const;
    void sortByQueue(QList<Task>& tasks) const;
    [[nodiscard]] QStringList seriesOrder() const;
    void applySeriesOrder(const QStringList& order, bool announce = true);

    Services services_;
    std::function<DownloadSettings()> settings_;
    QHash<qint64, Task> tasks_;
    QHash<qint64, Live> live_;
    QTimer poll_timer_;
    QTimer jd_poll_timer_;
    QTimer wake_timer_;
    QNetworkAccessManager jd_network_;
    bool started_ = false;
    bool transfers_enabled_ = true;
    bool stopping_ = false;
};

} // namespace animehub::downloads

#endif // ANIMEHUB_CORE_DOWNLOADS_MANAGER_H
