#ifndef ANIMEHUB_CORE_DOWNLOADS_REPOSITORY_H
#define ANIMEHUB_CORE_DOWNLOADS_REPOSITORY_H

#include "download_types.h"
#include "storage/database.h"

#include <QList>

namespace animehub::downloads {

using storage::Stored;

// The persistent download queue. It never stores URLs or link keys: a resumed task resolves its
// source again.
class DownloadRepository {
public:
    explicit DownloadRepository(storage::Database& database);

    // Fails when the episode already has an active task.
    Stored<Task> create(const QString& slug, double number, const QString& anime_title,
                        api::Audio audio, TaskOrigin origin, const QString& target_path);
    Stored<void> save(const Task& task);
    Stored<void> remove(qint64 id);
    // Active first in queue order, then finished ones, newest first.
    [[nodiscard]] Stored<QList<Task>> all() const;
    [[nodiscard]] Stored<std::optional<Task>> task(qint64 id) const;
    [[nodiscard]] Stored<std::optional<Task>> activeFor(const QString& slug, double number,
                                                        api::Audio audio) const;
    // After a restart nothing is transferring: those tasks go back to the queue.
    Stored<int> requeueInterrupted();
    Stored<int> removeFinished();

private:
    storage::Database& database_;
};

} // namespace animehub::downloads

#endif // ANIMEHUB_CORE_DOWNLOADS_REPOSITORY_H
