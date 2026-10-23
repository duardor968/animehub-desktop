#include "download_manager.h"
#include "util/file_names.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace animehub::downloads {
using namespace Qt::StringLiterals;

void DownloadManager::jdRequest(qint64 id, const QString& path, const QJsonArray& params,
                                myjd::Client::Callback<QJsonValue> done) {
    if (!transfers_enabled_)
        return;
    if ((path.endsWith(u"/forceDownload") || path.endsWith(u"/removeLinks") ||
         path.endsWith(u"/moveToDownloadlist")) &&
        (params.size() != 2 || params.first().toArray().isEmpty() ||
         !params.last().toArray().isEmpty())) {
        fail(id, TaskIssue::Engine,
             u"No se puede actuar sin identificadores propios de JDownloader."_s);
        return;
    }
    const auto generation = live_[id].generation;
    jd::request(
        jd_network_, services_.myJDownloader, tasks_[id], path, params, this,
        [this, id, generation, done = std::move(done)](const auto& result) {
            if (stopping_ || !tasks_.contains(id) || !live_.contains(id) ||
                live_[id].generation != generation || !tasks_[id].active())
                return;
            live_[id].jdBusy = false;
            if (tasks_[id].enginePhase == u"cancel_requested" && tasks_[id].engineJobId > 0) {
                tasks_[id].enginePhase = u"cancelling"_s;
                store(tasks_[id]);
                pollJd(id);
                return;
            }
            if (!result) {
                // Never clear submission/job/IDs on disconnect: reconnect reconciles the same job.
                retryLater(
                    id, TaskIssue::Engine,
                    u"JDownloader no está disponible. Se conserva el trabajo para reconciliarlo."_s);
                return;
            }
            done(result);
        });
}

void DownloadManager::submitJd(qint64 id, const api::ResolvedLink& link) {
    auto& task = tasks_[id];
    if ((task.engine == DownloadEngine::JDownloaderLocal &&
         !jd::isLocalEndpoint(QUrl(task.engineEndpoint))) ||
        (task.engine == DownloadEngine::JDownloaderCloud && task.engineDevice.isEmpty())) {
        fail(id, TaskIssue::Engine,
             u"Configura una API local de loopback o un dispositivo MyJDownloader."_s);
        return;
    }
    if (!QDir().mkpath(task.engineDestination)) {
        fail(id, TaskIssue::Storage);
        return;
    }
    task.engineSubmission = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.enginePhase = u"submitting"_s;
    task.provider = link.provider;
    // Write-ahead intent: an uncertain addLinks response must never cause a duplicate add.
    if (!services_.repository.save(task)) {
        task.engineSubmission.clear();
        fail(id, TaskIssue::Storage);
        return;
    }
    live_[id].jdBusy = true;
    jdRequest(
        id, u"/linkgrabberv2/addLinks"_s,
        {QJsonObject{{u"links"_s, link.url.toString(QUrl::FullyEncoded)},
                     {u"packageName"_s, u"AnimeHub-"_s + task.engineSubmission},
                     {u"destinationFolder"_s, QDir::toNativeSeparators(task.engineDestination)},
                     {u"assignJobID"_s, true},
                     {u"autostart"_s, false},
                     {u"autoExtract"_s, true},
                     {u"overwritePackagizerRules"_s, true}}},
        [this, id](const auto& result) {
            auto& task = tasks_[id];
            task.engineJobId = result->toObject().value(u"id").toInteger();
            if (task.engineJobId <= 0) {
                fail(
                    id, TaskIssue::Engine,
                    u"JDownloader no devolvió el identificador del trabajo. No se reenviará automáticamente."_s);
                return;
            }
            task.enginePhase =
                task.enginePhase == u"cancel_requested" ? u"cancelling"_s : u"collecting"_s;
            task.state = TaskState::Downloading;
            if (!services_.repository.save(task)) {
                fail(id, TaskIssue::Storage);
                return;
            }
            emit taskChanged(id);
            pump();
        });
}

void DownloadManager::pollJd(qint64 id) {
    auto& task = tasks_[id];
    if (live_[id].jdBusy || stopping_)
        return;
    if (task.engineJobId <= 0) {
        fail(
            id, TaskIssue::Engine,
            u"La entrega a JDownloader quedó sin confirmar. Revisa el paquete AnimeHub antes de crear otro trabajo."_s);
        return;
    }
    if (task.enginePhase == u"cancelling" || task.enginePhase == u"cancel_requested") {
        live_[id].jdBusy = true;
        jdRequest(id, u"/linkgrabberv2/abort"_s, {task.engineJobId},
                  [this, id](const auto&) { cancelJd(id); });
        return;
    }
    live_[id].jdBusy = true;
    QJsonObject query{{u"jobUUIDs"_s, QJsonArray{task.engineJobId}},
                      {u"maxResults"_s, -1},
                      {u"bytesLoaded"_s, true},
                      {u"bytesTotal"_s, true},
                      {u"finished"_s, true},
                      {u"running"_s, true},
                      {u"speed"_s, true},
                      {u"extractionStatus"_s, true},
                      {u"status"_s, true}};
    jdRequest(id, u"/downloadsV2/queryLinks"_s, {query}, [this, id](const auto& result) {
        if (!result->isArray()) {
            fail(id, TaskIssue::Engine);
            return;
        }
        const auto links = result->toArray();
        if (links.isEmpty()) {
            collectJd(id);
        } else {
            if (tasks_[id].enginePhase == u"moving" || tasks_[id].enginePhase == u"starting") {
                live_[id].jdBusy = true;
                jdRequest(id, u"/downloadsV2/forceDownload"_s,
                          {jd::ids(tasks_[id].engineLinkIds), QJsonArray{}},
                          [this, id](const auto&) {
                              tasks_[id].enginePhase = u"transferring"_s;
                              store(tasks_[id]);
                          });
                return;
            }
            observeJd(id, links);
        }
    });
}

void DownloadManager::cancelJd(qint64 id, bool collector) {
    live_[id].jdBusy = true;
    const auto prefix = collector ? u"/linkgrabberv2/"_s : u"/downloadsV2/"_s;
    jdRequest(
        id, prefix + u"queryLinks"_s,
        {QJsonObject{{u"jobUUIDs"_s, QJsonArray{tasks_[id].engineJobId}}, {u"maxResults"_s, -1}}},
        [this, id, collector, prefix](const auto& result) {
            if (!result->isArray()) {
                fail(id, TaskIssue::Engine);
                return;
            }
            QList<qint64> owned;
            for (const auto& item : result->toArray()) {
                const auto uuid = item.toObject().value(u"uuid").toInteger();
                if (uuid <= 0) {
                    fail(id, TaskIssue::Engine);
                    return;
                }
                owned.append(uuid);
            }
            const auto finish = [this, id, collector](const auto&) {
                if (!collector) {
                    cancelJd(id, true);
                    return;
                }
                auto& task = tasks_[id];
                task.state = TaskState::Cancelled;
                task.enginePhase = u"cancelled"_s;
                task.bytesPerSecond = 0;
                task.finishedAt = QDateTime::currentDateTimeUtc();
                store(task);
                live_.remove(id);
                pump();
            };
            if (owned.isEmpty()) {
                finish(QJsonValue());
                return;
            }
            live_[id].jdBusy = true;
            jdRequest(id, prefix + u"removeLinks"_s, {jd::ids(owned), QJsonArray{}}, finish);
        });
}

void DownloadManager::collectJd(qint64 id) {
    live_[id].jdBusy = true;
    jdRequest(id, u"/linkgrabberv2/queryLinkCrawlerJobs"_s,
              {QJsonObject{{u"jobIds"_s, QJsonArray{tasks_[id].engineJobId}},
                           {u"collectorInfo"_s, true}}},
              [this, id](const auto& result) {
                  if (!result->isArray()) {
                      fail(id, TaskIssue::Engine);
                      return;
                  }
                  for (const auto& item : result->toArray()) {
                      const auto job = item.toObject();
                      if (job.value(u"jobId").toInteger() != tasks_[id].engineJobId)
                          continue;
                      if (job.value(u"crawling").toBool() || job.value(u"checking").toBool())
                          return;
                  }
                  live_[id].jdBusy = true;
                  jdRequest(id, u"/linkgrabberv2/queryLinks"_s,
                            {QJsonObject{{u"jobUUIDs"_s, QJsonArray{tasks_[id].engineJobId}},
                                         {u"maxResults"_s, -1},
                                         {u"availability"_s, true}}},
                            [this, id](const auto& answer) {
                                if (!answer->isArray()) {
                                    fail(id, TaskIssue::Engine);
                                    return;
                                }
                                QList<qint64> ids;
                                for (const auto& item : answer->toArray()) {
                                    const auto link = item.toObject();
                                    const auto uuid = link.value(u"uuid").toInteger();
                                    if (uuid <= 0) {
                                        fail(id, TaskIssue::Engine);
                                        return;
                                    }
                                    if (link.value(u"availability").toString() == u"OFFLINE") {
                                        fail(id, TaskIssue::SourceRemoved);
                                        return;
                                    }
                                    ids.append(uuid);
                                }
                                if (ids.isEmpty()) {
                                    // Missing IDs are not completion, even if JD purged its
                                    // history.
                                    tasks_[id].enginePhase = u"waiting"_s;
                                    store(tasks_[id]);
                                    return;
                                }
                                auto& task = tasks_[id];
                                task.engineLinkIds = ids;
                                task.enginePhase = u"moving"_s;
                                if (!services_.repository.save(task)) {
                                    fail(id, TaskIssue::Storage);
                                    return;
                                }
                                live_[id].jdBusy = true;
                                jdRequest(id, u"/linkgrabberv2/moveToDownloadlist"_s,
                                          {jd::ids(ids), QJsonArray{}}, [this, id](const auto&) {
                                              tasks_[id].enginePhase = u"starting"_s;
                                              store(tasks_[id]);
                                              live_[id].jdBusy = true;
                                              jdRequest(
                                                  id, u"/downloadsV2/forceDownload"_s,
                                                  {jd::ids(tasks_[id].engineLinkIds), QJsonArray{}},
                                                  [this, id](const auto&) {
                                                      tasks_[id].enginePhase = u"transferring"_s;
                                                      store(tasks_[id]);
                                                  });
                                          });
                            });
              });
}

void DownloadManager::observeJd(qint64 id, const QJsonArray& links) {
    auto& task = tasks_[id];
    QList<qint64> received;
    bool finished = true;
    bool extracting = false;
    bool running = false;
    qint64 total = 0, done = 0, speed = 0;
    for (const auto& item : links) {
        const auto link = item.toObject();
        const auto uuid = link.value(u"uuid").toInteger();
        if (uuid <= 0) {
            fail(id, TaskIssue::Engine);
            return;
        }
        if (!task.engineLinkIds.isEmpty() && !task.engineLinkIds.contains(uuid))
            continue;
        received.append(uuid);
        finished = finished && link.value(u"finished").toBool(false);
        running = running || link.value(u"running").toBool(false);
        const auto extraction = link.value(u"extractionStatus").toString();
        if (extraction.contains(u"ERROR") || extraction.contains(u"FAILED")) {
            fail(id, TaskIssue::Integrity, u"JDownloader no pudo extraer el archivo."_s);
            return;
        }
        extracting = extracting || (!extraction.isEmpty() && extraction != u"SUCCESSFUL");
        done += link.value(u"bytesLoaded").toInteger();
        total += link.value(u"bytesTotal").toInteger();
        speed += link.value(u"speed").toInteger();
    }
    if (received.isEmpty() ||
        (!task.engineLinkIds.isEmpty() && received.size() != task.engineLinkIds.size())) {
        task.enginePhase = u"waiting"_s;
        store(task);
        return;
    }
    if (task.engineLinkIds.isEmpty())
        task.engineLinkIds = received;
    task.bytesDone = done;
    task.bytesTotal = total;
    task.bytesPerSecond = speed;
    task.enginePhase = extracting ? u"extracting"_s : running ? u"transferring"_s : u"waiting"_s;
    store(task);
    if (!finished || running || extracting)
        return;
    const auto files = jd::verifiedMedia(task.engineDestination);
    if (files.isEmpty()) {
        fail(
            id, TaskIssue::Storage,
            u"JDownloader terminó, pero no hay vídeo local verificable en el destino del trabajo."_s);
        return;
    }
    // Move only verified media after JD has closed the transfer and extractor. The
    // receipt and staging bytes survive failures; retry must reconcile the same job.
    const QFileInfo staging(task.engineDestination);
    if (QDir::cleanPath(staging.absoluteFilePath())
            .compare(staging.canonicalFilePath(), Qt::CaseInsensitive) != 0) {
        fail(id, TaskIssue::Storage, u"El destino de JDownloader redirige fuera del trabajo."_s);
        return;
    }
    QString firstDestination;
    for (const auto& path : files) {
        const auto destination =
            util::uniquePath(task.targetPath + u'.' + QFileInfo(path).suffix());
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) ||
            !QFile::rename(path, destination)) {
            fail(
                id, TaskIssue::Storage,
                u"No se pudo organizar el vídeo terminado. Se conserva el trabajo de JDownloader."_s);
            return;
        }
        if (!services_.library.setEpisodeFile(task.slug, task.number, destination,
                                              QFileInfo(destination).size())) {
            const bool restored = QFile::rename(destination, path);
            fail(id, TaskIssue::Storage,
                 restored ? u"No se pudo registrar el vídeo terminado."_s
                          : u"No se pudo registrar el vídeo; se conserva en: "_s + destination);
            return;
        }
        if (firstDestination.isEmpty())
            firstDestination = destination;
    }
    task.enginePhase = u"completed"_s;
    complete(id, firstDestination);
}
} // namespace animehub::downloads
