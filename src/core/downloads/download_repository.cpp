#include "download_repository.h"

#include <QSqlQuery>
#include <QTimeZone>

#include <array>

namespace animehub::downloads {

using namespace Qt::StringLiterals;
namespace {

using storage::nowMs;
using storage::StorageError;

constexpr auto kColumns =
    "id, slug, number, anime_title, audio, origin, state, provider, source_id, target_path, "
    "bytes_done, bytes_total, error_code, error_detail, attempts, retry_at, queue_order, "
    "created_at, finished_at";

QDateTime timeAt(const QVariant& value) {
    return value.isNull() ? QDateTime()
                          : QDateTime::fromMSecsSinceEpoch(value.toLongLong(), QTimeZone::UTC);
}

QVariant nullableTime(const QDateTime& value) {
    return value.isValid() ? QVariant(value.toMSecsSinceEpoch())
                           : QVariant(QMetaType::fromType<qint64>());
}

QVariant nullableText(const QString& value) {
    return value.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(value);
}

Task readTask(const QSqlQuery& query) {
    Task task;
    task.id = query.value(0).toLongLong();
    task.slug = query.value(1).toString();
    task.number = query.value(2).toDouble();
    task.animeTitle = query.value(3).toString();
    task.audio = query.value(4).toString() == u"DUB" ? api::Audio::Dub : api::Audio::Sub;
    task.origin =
        query.value(5).toString() == u"automatic" ? TaskOrigin::Automatic : TaskOrigin::Manual;
    task.state = stateFromKey(query.value(6).toString());
    if (!query.value(7).isNull()) {
        task.provider = api::providerFromKey(query.value(7).toString());
    }
    task.sourceId = query.value(8).toString();
    task.targetPath = query.value(9).toString();
    task.bytesDone = query.value(10).toLongLong();
    task.bytesTotal = query.value(11).toLongLong();
    task.issue = issueFromKey(query.value(12).toString());
    task.issueDetail = query.value(13).toString();
    task.attempts = query.value(14).toInt();
    task.retryAt = timeAt(query.value(15));
    task.queueOrder = query.value(16).toLongLong();
    task.createdAt = timeAt(query.value(17));
    task.finishedAt = timeAt(query.value(18));
    return task;
}

} // namespace

QString stateKey(TaskState state) {
    switch (state) {
    case TaskState::Queued:
        return QStringLiteral("queued");
    case TaskState::Resolving:
        return QStringLiteral("resolving");
    case TaskState::Downloading:
        return QStringLiteral("downloading");
    case TaskState::Verifying:
        return QStringLiteral("verifying");
    case TaskState::Paused:
        return QStringLiteral("paused");
    case TaskState::Waiting:
        return QStringLiteral("waiting");
    case TaskState::Completed:
        return QStringLiteral("completed");
    case TaskState::Failed:
        return QStringLiteral("failed");
    case TaskState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("queued");
}

TaskState stateFromKey(QStringView key) {
    for (const auto state : {TaskState::Queued, TaskState::Resolving, TaskState::Downloading,
                             TaskState::Verifying, TaskState::Paused, TaskState::Waiting,
                             TaskState::Completed, TaskState::Failed, TaskState::Cancelled}) {
        if (key == stateKey(state)) {
            return state;
        }
    }
    return TaskState::Failed;
}

QString issueKey(TaskIssue issue) {
    switch (issue) {
    case TaskIssue::None:
        return {};
    case TaskIssue::NoSource:
        return QStringLiteral("no_source");
    case TaskIssue::SourceRemoved:
        return QStringLiteral("source_removed");
    case TaskIssue::Quota:
        return QStringLiteral("quota");
    case TaskIssue::RateLimited:
        return QStringLiteral("rate_limited");
    case TaskIssue::Network:
        return QStringLiteral("network");
    case TaskIssue::NoSpace:
        return QStringLiteral("no_space");
    case TaskIssue::Storage:
        return QStringLiteral("storage");
    case TaskIssue::Integrity:
        return QStringLiteral("integrity");
    case TaskIssue::Engine:
        return QStringLiteral("engine");
    }
    return {};
}

TaskIssue issueFromKey(QStringView key) {
    for (const auto issue : {TaskIssue::NoSource, TaskIssue::SourceRemoved, TaskIssue::Quota,
                             TaskIssue::RateLimited, TaskIssue::Network, TaskIssue::NoSpace,
                             TaskIssue::Storage, TaskIssue::Integrity, TaskIssue::Engine}) {
        if (key == issueKey(issue)) {
            return issue;
        }
    }
    return TaskIssue::None;
}

DownloadRepository::DownloadRepository(storage::Database& database) : database_(database) {}

Stored<Task> DownloadRepository::create(const QString& slug, double number,
                                        const QString& anime_title, api::Audio audio,
                                        TaskOrigin origin, const QString& target_path) {
    const auto now = nowMs();
    auto order = database_.run(QStringLiteral("SELECT COALESCE(MAX(queue_order), 0) + 1 FROM "
                                              "download"));
    if (!order) {
        return std::unexpected(order.error());
    }
    order->next();
    const auto queue_order = order->value(0).toLongLong();
    auto inserted = database_.run(
        QStringLiteral("INSERT INTO download (slug, number, anime_title, audio, origin, state, "
                       "target_path, queue_order, created_at, updated_at) "
                       "VALUES (?, ?, ?, ?, ?, 'queued', ?, ?, ?, ?)"),
        {slug, number, anime_title, audio == api::Audio::Dub ? u"DUB"_s : u"SUB"_s,
         origin == TaskOrigin::Automatic ? u"automatic"_s : u"manual"_s, target_path, queue_order,
         now, now});
    if (!inserted) {
        return std::unexpected(inserted.error());
    }
    const auto id = inserted->lastInsertId().toLongLong();
    auto created = task(id);
    if (!created) {
        return std::unexpected(created.error());
    }
    if (!*created) {
        return std::unexpected(StorageError{QStringLiteral("download %1 vanished").arg(id)});
    }
    return **created;
}

Stored<void> DownloadRepository::save(const Task& task) {
    auto result = database_.run(
        QStringLiteral("UPDATE download SET state = ?, provider = ?, source_id = ?, "
                       "target_path = ?, bytes_done = ?, bytes_total = ?, error_code = ?, "
                       "error_detail = ?, attempts = ?, retry_at = ?, queue_order = ?, "
                       "updated_at = ?, finished_at = ? WHERE id = ?"),
        {stateKey(task.state),
         task.provider ? QVariant(api::providerKey(*task.provider))
                       : QVariant(QMetaType::fromType<QString>()),
         nullableText(task.sourceId), task.targetPath, task.bytesDone, task.bytesTotal,
         nullableText(issueKey(task.issue)), nullableText(task.issueDetail), task.attempts,
         nullableTime(task.retryAt), task.queueOrder, nowMs(), nullableTime(task.finishedAt),
         task.id});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> DownloadRepository::remove(qint64 id) {
    auto result = database_.run(QStringLiteral("DELETE FROM download WHERE id = ?"), {id});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<QList<Task>> DownloadRepository::all() const {
    auto query =
        database_.run(QStringLiteral("SELECT %1 FROM download ORDER BY "
                                     "state IN ('completed', 'failed', 'cancelled'), "
                                     "CASE WHEN state IN ('completed', 'failed', 'cancelled') "
                                     "THEN -COALESCE(finished_at, updated_at) ELSE queue_order END")
                          .arg(QLatin1StringView(kColumns)));
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<Task> tasks;
    while (query->next()) {
        tasks.append(readTask(*query));
    }
    return tasks;
}

Stored<std::optional<Task>> DownloadRepository::task(qint64 id) const {
    auto query = database_.run(
        QStringLiteral("SELECT %1 FROM download WHERE id = ?").arg(QLatin1StringView(kColumns)),
        {id});
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        return std::optional<Task>();
    }
    return std::optional<Task>(readTask(*query));
}

Stored<std::optional<Task>> DownloadRepository::activeFor(const QString& slug, double number,
                                                          api::Audio audio) const {
    auto query = database_.run(
        QStringLiteral("SELECT %1 FROM download WHERE slug = ? AND number = ? AND audio = ? AND "
                       "state NOT IN ('completed', 'cancelled')")
            .arg(QLatin1StringView(kColumns)),
        {slug, number, audio == api::Audio::Dub ? u"DUB"_s : u"SUB"_s});
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        return std::optional<Task>();
    }
    return std::optional<Task>(readTask(*query));
}

Stored<int> DownloadRepository::requeueInterrupted() {
    auto result = database_.run(
        QStringLiteral("UPDATE download SET state = 'queued', updated_at = ? WHERE state IN "
                       "('resolving', 'downloading', 'verifying')"),
        {nowMs()});
    if (!result) {
        return std::unexpected(result.error());
    }
    return result->numRowsAffected();
}

Stored<int> DownloadRepository::removeFinished() {
    auto result = database_.run(
        QStringLiteral("DELETE FROM download WHERE state IN ('completed', 'cancelled', 'failed')"));
    if (!result) {
        return std::unexpected(result.error());
    }
    return result->numRowsAffected();
}

} // namespace animehub::downloads
