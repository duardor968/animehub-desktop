#include "download_repository.h"
#include "jdownloader.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

#include <array>

namespace animehub::downloads {

using namespace Qt::StringLiterals;
namespace {

using storage::nowMs;
using storage::StorageError;

constexpr auto kColumns =
    "id, slug, number, anime_title, audio, origin, state, provider, source_id, target_path, "
    "bytes_done, bytes_total, error_code, error_detail, attempts, retry_at, queue_order, "
    "created_at, finished_at, paused_with_all, engine, engine_endpoint, engine_device, "
    "engine_job_id, engine_link_ids, engine_submission, engine_phase, engine_destination, "
    "remote_request_id";

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
    task.pausedWithAll = query.value(19).toBool();
    const auto engine = query.value(20).toString();
    task.engine = engine == u"jd_local"   ? DownloadEngine::JDownloaderLocal
                  : engine == u"jd_cloud" ? DownloadEngine::JDownloaderCloud
                                          : DownloadEngine::Native;
    task.engineEndpoint = query.value(21).toString();
    task.engineDevice = query.value(22).toString();
    task.engineJobId = query.value(23).toLongLong();
    for (const auto& id : QJsonDocument::fromJson(query.value(24).toByteArray()).array()) {
        if (id.toInteger() > 0)
            task.engineLinkIds.append(id.toInteger());
    }
    task.engineSubmission = query.value(25).toString();
    task.enginePhase = query.value(26).toString();
    task.engineDestination = query.value(27).toString();
    task.remoteRequestId = query.value(28).toString();
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
                                        TaskOrigin origin, const QString& target_path,
                                        DownloadEngine engine, const QString& endpoint,
                                        const QString& device) {
    auto created = createMany(slug, {{number, target_path}}, anime_title, audio, origin, engine,
                              endpoint, device);
    if (!created) {
        return std::unexpected(created.error());
    }
    return created->first();
}

Stored<QList<Task>> DownloadRepository::createMany(
    const QString& slug, const QList<std::pair<double, QString>>& episodes,
    const QString& anime_title, api::Audio audio, TaskOrigin origin, DownloadEngine engine,
    const QString& endpoint, const QString& device, const QString& sourceRequestId) {
    if (engine == DownloadEngine::JDownloaderLocal && !jd::isLocalEndpoint(QUrl(endpoint))) {
        return std::unexpected(StorageError{
            u"La API local debe ser HTTP de loopback, sin credenciales ni parámetros."_s});
    }
    QList<qint64> ids;
    auto inserted = database_.transaction([&]() -> Stored<void> {
        auto order = database_.run(
            QStringLiteral("SELECT COALESCE((SELECT MIN(queue_order) FROM download WHERE slug = ? "
                           "AND state NOT IN ('completed', 'cancelled')), (SELECT "
                           "COALESCE(MAX(queue_order), 0) + 1 FROM download))"),
            {slug});
        if (!order) {
            return std::unexpected(order.error());
        }
        order->next();
        const auto queue_order = order->value(0).toLongLong();
        order->finish();
        const auto now = nowMs();
        const auto audio_key = audio == api::Audio::Dub ? u"DUB"_s : u"SUB"_s;
        const auto receipt = [&](double number, qint64 task_id) -> Stored<void> {
            if (sourceRequestId.isEmpty())
                return {};
            auto saved = database_.run(
                QStringLiteral("INSERT INTO download_request_receipt "
                               "(remote_request_id,number,audio,slug,task_id) VALUES (?,?,?,?,?)"),
                {sourceRequestId, number, audio_key, slug, task_id});
            if (!saved)
                return std::unexpected(saved.error());
            return {};
        };
        for (const auto& [number, target_path] : episodes) {
            if (!sourceRequestId.isEmpty()) {
                // A remote request identifies the accepted episode, independent of current
                // audio preferences. Keep its receipt even when the queue row was removed.
                auto seen =
                    database_.run(QStringLiteral("SELECT slug FROM download_request_receipt WHERE "
                                                 "remote_request_id=? AND number=?"),
                                  {sourceRequestId, number});
                if (!seen)
                    return std::unexpected(seen.error());
                if (seen->next()) {
                    if (seen->value(0).toString() != slug)
                        return std::unexpected(
                            StorageError{u"El pedido remoto ya pertenece a otra obra."_s});
                    continue;
                }
                seen->finish();
                auto active = activeFor(slug, number, audio);
                if (!active)
                    return std::unexpected(active.error());
                if (*active) {
                    if (auto saved = receipt(number, (**active).id); !saved)
                        return saved;
                    continue;
                }
            }
            auto row = database_.run(
                QStringLiteral(
                    "INSERT INTO download (slug, number, anime_title, audio, origin, "
                    "state, target_path, queue_order, created_at, updated_at, engine, "
                    "engine_endpoint, engine_device, engine_destination, remote_request_id) "
                    "VALUES (?, ?, ?, ?, ?, 'queued', ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                {slug, number, anime_title, audio == api::Audio::Dub ? u"DUB"_s : u"SUB"_s,
                 origin == TaskOrigin::Automatic ? u"automatic"_s : u"manual"_s, target_path,
                 queue_order, now, now,
                 engine == DownloadEngine::JDownloaderLocal   ? u"jd_local"_s
                 : engine == DownloadEngine::JDownloaderCloud ? u"jd_cloud"_s
                                                              : u"native"_s,
                 engine == DownloadEngine::JDownloaderLocal ? endpoint : u""_s,
                 device.isNull() ? u""_s : device,
                 engine == DownloadEngine::Native
                     ? QFileInfo(target_path).absolutePath()
                     : target_path + u" - "_s + QUuid::createUuid().toString(QUuid::WithoutBraces),
                 sourceRequestId.isNull() ? u""_s : sourceRequestId});
            if (!row) {
                return std::unexpected(row.error());
            }
            ids.append(row->lastInsertId().toLongLong());
            if (auto saved = receipt(number, ids.last()); !saved)
                return saved;
        }
        return {};
    });
    if (!inserted) {
        return std::unexpected(inserted.error());
    }
    QList<Task> tasks;
    for (const auto id : ids) {
        auto created = task(id);
        if (!created) {
            return std::unexpected(created.error());
        }
        if (!*created) {
            return std::unexpected(StorageError{QStringLiteral("download %1 vanished").arg(id)});
        }
        tasks.append(**created);
    }
    return tasks;
}

Stored<QList<qint64>> DownloadRepository::remoteTaskIds(const QString& sourceRequestId) const {
    auto rows = database_.run(QStringLiteral("SELECT task_id FROM download_request_receipt WHERE "
                                             "remote_request_id=? ORDER BY number,audio"),
                              {sourceRequestId});
    if (!rows)
        return std::unexpected(rows.error());
    QList<qint64> ids;
    while (rows->next())
        ids.append(rows->value(0).toLongLong());
    return ids;
}

Stored<void> DownloadRepository::save(const QList<Task>& tasks) {
    if (tasks.isEmpty()) {
        return {};
    }
    return database_.transaction([&]() -> Stored<void> {
        for (const auto& task : tasks) {
            if (auto saved = save(task); !saved) {
                return saved;
            }
        }
        return {};
    });
}

Stored<void> DownloadRepository::save(const Task& task) {
    QJsonArray ids;
    for (const auto id : task.engineLinkIds)
        ids.append(id);
    auto result = database_.run(
        QStringLiteral(
            "UPDATE download SET state = ?, provider = ?, source_id = ?, "
            "target_path = ?, bytes_done = ?, bytes_total = ?, error_code = ?, "
            "error_detail = ?, attempts = ?, retry_at = ?, queue_order = ?, "
            "updated_at = ?, finished_at = ?, paused_with_all = ?, engine = ?, "
            "engine_endpoint = ?, engine_device = ?, engine_job_id = ?, engine_link_ids = ?, "
            "engine_submission = ?, engine_phase = ?, engine_destination = ? WHERE id = ?"),
        {stateKey(task.state),
         task.provider ? QVariant(api::providerKey(*task.provider))
                       : QVariant(QMetaType::fromType<QString>()),
         nullableText(task.sourceId),
         task.targetPath,
         task.bytesDone,
         task.bytesTotal,
         nullableText(issueKey(task.issue)),
         nullableText(task.issueDetail),
         task.attempts,
         nullableTime(task.retryAt),
         task.queueOrder,
         nowMs(),
         nullableTime(task.finishedAt),
         task.pausedWithAll ? 1 : 0,
         task.engine == DownloadEngine::JDownloaderLocal   ? u"jd_local"_s
         : task.engine == DownloadEngine::JDownloaderCloud ? u"jd_cloud"_s
                                                           : u"native"_s,
         task.engineEndpoint.isNull() ? u""_s : task.engineEndpoint,
         task.engineDevice.isNull() ? u""_s : task.engineDevice,
         task.engineJobId,
         QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)),
         task.engineSubmission.isNull() ? u""_s : task.engineSubmission,
         task.enginePhase.isNull() ? u""_s : task.enginePhase,
         task.engineDestination.isNull() ? u""_s : task.engineDestination,
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
