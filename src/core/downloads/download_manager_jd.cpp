#include "download_manager.h"
#include "util/file_names.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>

namespace animehub::downloads {
using namespace Qt::StringLiterals;

namespace {

using namespace std::chrono_literals;

// A JDownloader that AnimeHub started is looked at this often until its API opens. It is
// started up to this many times for one hand-over (Services::jdLaunchWait apart): one that was
// updating itself restarts meanwhile, and a start while it runs does nothing, as it allows one
// instance.
constexpr auto kJdStartPoll = 2s;
constexpr int kJdLaunches = 3;

// The folder JDownloader downloaded into goes once its videos have left it, but only if nothing
// else is in it: whatever stays (an archive, a file AnimeHub does not know) keeps it, untouched.
void removeStaging(const QString& path) {
    QStringList folders;
    QStringList left;
    QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto entry = it.nextFileInfo();
        if (entry.isDir() && !entry.isSymLink() && !entry.isJunction()) {
            folders.append(entry.absoluteFilePath());
        } else {
            left.append(entry.absoluteFilePath());
        }
    }
    if (!left.isEmpty()) {
        qWarning("download queue: kept %s, it still holds %s", qPrintable(path),
                 qPrintable(left.join(u", ")));
        return;
    }
    // Inner folders first: a folder's path is longer than its parent's.
    std::ranges::sort(folders, std::ranges::greater{}, &QString::size);
    folders.append(path);
    for (const auto& folder : folders) {
        if (!QDir().rmdir(folder)) {
            qWarning("download queue: could not remove the empty folder %s", qPrintable(folder));
            return;
        }
    }
}

} // namespace

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
        [this, id, generation, path, params, done](const auto& result) {
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
                const auto failure = result.error();
                // A closed local JDownloader is started, in the tray, for the hand-over.
                if (tasks_[id].engineJobId <= 0 && failure == myjd::Failure::DeviceOffline &&
                    tasks_[id].engine == DownloadEngine::JDownloaderLocal &&
                    services_.launchJDownloader && live_[id].jdLaunches == 0) {
                    launchJd(id, path, params, done);
                    return;
                }
                if (tasks_[id].engineJobId <= 0 && (failure == myjd::Failure::DeviceOffline ||
                                                    failure == myjd::Failure::WrongCredentials ||
                                                    failure == myjd::Failure::EmailNotConfirmed)) {
                    unreachedJd(id, failure);
                    return;
                }
                // Never clear submission/job/IDs on disconnect: reconnect reconciles the same job.
                retryLater(
                    id, TaskIssue::Engine,
                    u"JDownloader no está disponible. Se conserva el trabajo para reconciliarlo."_s);
                return;
            }
            done(result);
        });
}

void DownloadManager::launchJd(qint64 id, const QString& path, const QJsonArray& params,
                               myjd::Client::Callback<QJsonValue> done) {
    auto& live = live_[id];
    ++live.jdLaunches;
    if (!services_.launchJDownloader()) {
        live.jdBusy = false;
        unreachedJd(id, myjd::Failure::DeviceOffline);
        return;
    }
    if (tasks_[id].enginePhase != u"launching") {
        tasks_[id].enginePhase = u"launching"_s;
        store(tasks_[id]);
    }
    live.jdBusy = true;
    awaitJd(id, live.generation,
            QDateTime::currentDateTimeUtc().addMSecs(services_.jdLaunchWait.count()), path, params,
            std::move(done));
}

void DownloadManager::awaitJd(qint64 id, quint64 generation, QDateTime deadline,
                              const QString& path, const QJsonArray& params,
                              myjd::Client::Callback<QJsonValue> done) {
    const auto current = [this, id, generation] {
        return !stopping_ && tasks_.contains(id) && live_.contains(id) &&
               live_[id].generation == generation && tasks_[id].active();
    };
    const auto poll = std::min<std::chrono::milliseconds>(kJdStartPoll, services_.jdLaunchWait);
    QTimer::singleShot(poll, this, [=, this] {
        if (!current())
            return;
        if (tasks_[id].enginePhase == u"cancel_requested") {
            live_[id].jdBusy = false;
            unreachedJd(id, myjd::Failure::DeviceOffline); // nothing reached it: cancelled
            return;
        }
        jd::request(jd_network_, services_.myJDownloader, tasks_[id], u"/jd/version"_s, {}, this,
                    [=, this](const auto& answer) {
                        if (!current())
                            return;
                        if (answer) {
                            jdRequest(id, path, params, done);
                            return;
                        }
                        if (QDateTime::currentDateTimeUtc() >= deadline) {
                            if (live_[id].jdLaunches < kJdLaunches) {
                                launchJd(id, path, params, done);
                                return;
                            }
                            live_[id].jdBusy = false;
                            unreachedJd(id, myjd::Failure::DeviceOffline);
                            return;
                        }
                        awaitJd(id, generation, deadline, path, params, done);
                    });
    });
}

void DownloadManager::releaseJd(qint64 id) {
    auto& task = tasks_[id];
    task.engineSubmission.clear();
    task.engineJobId = 0;
    task.engineLinkIds.clear();
    task.enginePhase.clear();
}

void DownloadManager::unreachedJd(qint64 id, myjd::Failure failure) {
    auto& task = tasks_[id];
    const bool cancelled = task.enginePhase == u"cancel_requested";
    dropStaging(id); // nothing reached JDownloader
    releaseJd(id);
    if (cancelled) {
        task.state = TaskState::Cancelled;
        task.bytesPerSecond = 0;
        task.finishedAt = QDateTime::currentDateTimeUtc();
        store(task);
        live_.remove(id);
        pump();
        return;
    }
    // MEGA's quota sent it there: without JDownloader it goes on with AnimeHub's own servers,
    // or waits for MEGA.
    if (task.detour == TaskIssue::Quota) {
        task.engine = DownloadEngine::Native;
        task.engineEndpoint.clear();
        task.engineDevice.clear();
        task.engineDestination.clear();
        task.provider.reset();
        task.jdUnreachable = true;
        task.state = TaskState::Queued;
        store(task);
        pump();
        return;
    }
    fail(id, TaskIssue::Engine,
         task.engine == DownloadEngine::JDownloaderLocal
             ? u"JDownloader no responde en este PC. Activa su API local o usa MyJDownloader."_s
         : failure == myjd::Failure::DeviceOffline
             ? u"JDownloader no está conectado a MyJDownloader en ese equipo. Ábrelo allí o "
               u"elige otro equipo en Ajustes."_s
             : u"Conecta tu cuenta de MyJDownloader en Ajustes."_s);
}

void DownloadManager::dropStaging(qint64 id) {
    const auto& path = tasks_[id].engineDestination;
    if (!path.isEmpty() && QFileInfo(path).isDir())
        removeStaging(path);
}

void DownloadManager::submitJd(qint64 id, const QList<api::ResolvedLink>& links) {
    auto& task = tasks_[id];
    live_[id].jdLaunches = 0; // each hand-over may start JDownloader again
    if (links.isEmpty()) {
        fail(id, TaskIssue::NoSource);
        return;
    }
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
    task.provider = links.first().provider;
    QStringList urls;
    for (const auto& link : links)
        urls.append(link.url.toString(QUrl::FullyEncoded));
    // Write-ahead intent: an uncertain addLinks response must never cause a duplicate add.
    if (!services_.repository.save(task)) {
        task.engineSubmission.clear();
        fail(id, TaskIssue::Storage);
        return;
    }
    live_[id].jdBusy = true;
    jdRequest(
        id, u"/linkgrabberv2/addLinks"_s,
        {QJsonObject{{u"links"_s, urls.join(u'\n')},
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
                dropStaging(id);
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
                                         {u"availability"_s, true},
                                         {u"bytesTotal"_s, true}}},
                            [this, id](const auto& answer) {
                                if (!answer->isArray()) {
                                    fail(id, TaskIssue::Engine);
                                    return;
                                }
                                keepMirrors(id, answer->toArray());
                            });
              });
}

void DownloadManager::keepMirrors(qint64 id, QJsonArray links) {
    QList<QJsonObject> online;
    QList<qint64> drop;
    for (const auto& item : links) {
        const auto link = item.toObject();
        const auto uuid = link.value(u"uuid").toInteger();
        if (uuid <= 0) {
            fail(id, TaskIssue::Engine);
            return;
        }
        if (link.value(u"availability").toString() == u"OFFLINE")
            drop.append(uuid);
        else
            online.append(link);
    }
    if (online.isEmpty()) {
        if (!drop.isEmpty()) {
            fail(id, TaskIssue::SourceRemoved);
            return;
        }
        // Missing IDs are not completion, even if JD purged its history. Nor is there anything
        // left in JDownloader to wait for: the task lets the job go, so a retry hands the
        // episode over again.
        const auto detail = tasks_[id].engineLinkIds.isEmpty()
                                ? u"JDownloader no encontró ningún archivo en el enlace."_s
                                : u"JDownloader ya no tiene los archivos de este episodio."_s;
        releaseJd(id);
        fail(id, TaskIssue::SourceRemoved, detail);
        return;
    }
    // JDownloader takes links of one package with the same name (and size, when known) as
    // mirrors and downloads one. The first server's file names them; a file of another size is
    // another video and leaves.
    const auto name = online.first().value(u"name").toString();
    const auto size = online.first().value(u"bytesTotal").toInteger();
    QList<qint64> keep;
    QList<std::pair<qint64, QString>> renames;
    for (const auto& link : online) {
        const auto uuid = link.value(u"uuid").toInteger();
        const auto bytes = link.value(u"bytesTotal").toInteger();
        if (size > 0 && bytes > 0 && bytes != size) {
            drop.append(uuid);
            continue;
        }
        keep.append(uuid);
        if (!name.isEmpty() && link.value(u"name").toString().compare(name, Qt::CaseInsensitive))
            renames.append({uuid, name});
    }
    auto& task = tasks_[id];
    task.engineLinkIds = keep;
    task.enginePhase = u"moving"_s;
    if (!services_.repository.save(task)) {
        fail(id, TaskIssue::Storage);
        return;
    }
    prepareJd(id, drop, renames);
}

void DownloadManager::prepareJd(qint64 id, QList<qint64> drop,
                                QList<std::pair<qint64, QString>> renames) {
    live_[id].jdBusy = true;
    if (!drop.isEmpty()) {
        jdRequest(id, u"/linkgrabberv2/removeLinks"_s, {jd::ids(drop), QJsonArray{}},
                  [this, id, renames](const auto&) { prepareJd(id, {}, renames); });
        return;
    }
    if (!renames.isEmpty()) {
        const auto [uuid, name] = renames.takeFirst();
        jdRequest(id, u"/linkgrabberv2/renameLink"_s, {uuid, name},
                  [this, id, renames](const auto&) { prepareJd(id, {}, renames); });
        return;
    }
    jdRequest(id, u"/linkgrabberv2/moveToDownloadlist"_s,
              {jd::ids(tasks_[id].engineLinkIds), QJsonArray{}}, [this, id](const auto&) {
                  tasks_[id].enginePhase = u"starting"_s;
                  store(tasks_[id]);
                  live_[id].jdBusy = true;
                  jdRequest(id, u"/downloadsV2/forceDownload"_s,
                            {jd::ids(tasks_[id].engineLinkIds), QJsonArray{}},
                            [this, id](const auto&) {
                                tasks_[id].enginePhase = u"transferring"_s;
                                store(tasks_[id]);
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
    // Mirrors share a name and only one moves bytes: each file counts once, by its furthest link.
    QHash<QString, std::pair<qint64, qint64>> mirrors;
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
        auto& file = mirrors[link.value(u"name").toString().toLower()];
        if (const auto loaded = link.value(u"bytesLoaded").toInteger(); loaded >= file.first)
            file = {loaded, link.value(u"bytesTotal").toInteger()};
        speed += link.value(u"speed").toInteger();
    }
    for (const auto& [loaded, size] : std::as_const(mirrors)) {
        done += loaded;
        total += size;
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
        fail(id, TaskIssue::Storage, u"JDownloader terminó, pero no dejó ningún vídeo."_s);
        return;
    }
    // Move only verified media after JD has closed the transfer and extractor. The
    // receipt and staging bytes survive failures; retry must reconcile the same job.
    const QFileInfo staging(task.engineDestination);
    if (QDir::cleanPath(staging.absoluteFilePath())
            .compare(staging.canonicalFilePath(), Qt::CaseInsensitive) != 0) {
        fail(id, TaskIssue::Storage,
             u"La carpeta de JDownloader lleva fuera de la carpeta de descargas."_s);
        return;
    }
    QString firstDestination;
    for (const auto& path : files) {
        const auto destination =
            util::uniquePath(task.targetPath + u'.' + QFileInfo(path).suffix());
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) ||
            !QFile::rename(path, destination)) {
            fail(id, TaskIssue::Storage, u"No se pudo mover el vídeo terminado a su carpeta."_s);
            return;
        }
        if (!services_.library.setEpisodeFile(task.slug, task.number, destination,
                                              QFileInfo(destination).size())) {
            const bool restored = QFile::rename(destination, path);
            fail(id, TaskIssue::Storage,
                 restored ? u"No se pudo guardar el vídeo en la biblioteca."_s
                          : u"No se pudo guardar el vídeo en la biblioteca; está en "_s +
                                QDir::toNativeSeparators(destination));
            return;
        }
        if (firstDestination.isEmpty())
            firstDestination = destination;
    }
    removeStaging(task.engineDestination);
    task.enginePhase = u"completed"_s;
    complete(id, firstDestination);
}
} // namespace animehub::downloads
