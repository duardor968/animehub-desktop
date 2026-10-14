#include "download_manager.h"

#include "library/library_types.h"
#include "util/file_names.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStorageInfo>
#include <QThreadPool>

namespace animehub::downloads {
namespace {

using namespace std::chrono_literals;

constexpr int kMaxNetworkAttempts = 6;
constexpr int kMaxSourceAttempts = 3;
constexpr qint64 kSpaceMargin = 256LL * 1024 * 1024;
constexpr auto kQuotaWait = 30min;
constexpr auto kProgressSaveInterval = 5s;

// aria2 exit statuses (https://aria2.github.io/manual/en/html/aria2c.html#exit-status).
enum Aria2Code {
    kUnknown = 1,
    kTimeout = 2,
    kNotFound = 3,
    kTooSlow = 5,
    kNetwork = 6,
    kResumeUnsupported = 8,
    kNoSpace = 9,
    kFileExists = 13,
    kRenameFailed = 14,
    kOpenFailed = 15,
    kCreateFailed = 16,
    kIoError = 17,
    kDirectoryFailed = 18,
    kNameResolution = 19,
    kHttpUnexpected = 22,
    kHttpAuthorization = 24,
    kServerBusy = 29,
};

bool finished(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Failed ||
           state == TaskState::Cancelled;
}

QString episodeLabel(double number) {
    auto label = library::formatEpisodeNumber(number);
    const auto dot = label.indexOf(u'.');
    const auto whole = dot < 0 ? label.size() : dot;
    if (whole < 2) {
        label.prepend(QString(2 - whole, u'0'));
    }
    return label;
}

} // namespace

DownloadManager::DownloadManager(Services services, std::function<DownloadSettings()> settings,
                                 QObject* parent)
    : QObject(parent), services_(services), settings_(std::move(settings)) {
    poll_timer_.setInterval(700);
    connect(&poll_timer_, &QTimer::timeout, this, &DownloadManager::poll);
    wake_timer_.setSingleShot(true);
    connect(&wake_timer_, &QTimer::timeout, this, &DownloadManager::pump);
    connect(&services_.aria2, &Aria2::ready, this, &DownloadManager::pump);
    connect(&services_.aria2, &Aria2::failed, this, [this](const QString& message) {
        if (stopping_) {
            return;
        }
        // Transfers in flight died with the engine; they resume once it is back.
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->state == TaskState::Downloading) {
                live_[it.key()].gid.clear();
                retryLater(it.key(), TaskIssue::Engine, message);
            }
        }
        emit engineFailed(message);
        QTimer::singleShot(5s, this, [this] {
            if (!stopping_) {
                services_.aria2.start();
            }
        });
    });
}

DownloadManager::~DownloadManager() {
    for (auto& live : live_) {
        if (live.cancel) {
            live.cancel->store(true);
        }
    }
}

bool DownloadManager::isNative(api::Provider provider) {
    return provider == api::Provider::Mega || provider == api::Provider::Pixeldrain;
}

void DownloadManager::start() {
    if (started_) {
        return;
    }
    started_ = true;
    if (auto requeued = services_.repository.requeueInterrupted(); !requeued) {
        qWarning("download queue: %s", qPrintable(requeued.error().message));
    }
    if (auto all = services_.repository.all()) {
        for (const auto& task : *all) {
            tasks_.insert(task.id, task);
        }
    }
    emit loaded();
    services_.aria2.start();
    pump();
}

void DownloadManager::shutdown(std::function<void()> done) {
    stopping_ = true;
    poll_timer_.stop();
    wake_timer_.stop();
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
        if (auto live = live_.find(it.key()); live != live_.end() && live->cancel) {
            live->cancel->store(true);
        }
        if (it->state == TaskState::Downloading || it->state == TaskState::Resolving ||
            it->state == TaskState::Verifying) {
            it->state = TaskState::Queued; // picked up again on the next start
            store(*it, false);
        }
    }
    services_.aria2.shutdown(std::move(done));
}

QList<Task> DownloadManager::tasks() const {
    auto list = tasks_.values();
    std::ranges::sort(list, [](const Task& a, const Task& b) {
        const bool a_done = finished(a.state);
        const bool b_done = finished(b.state);
        if (a_done != b_done) {
            return !a_done;
        }
        if (a_done) {
            return a.finishedAt > b.finishedAt;
        }
        return a.queueOrder < b.queueOrder;
    });
    return list;
}

std::optional<Task> DownloadManager::task(qint64 id) const {
    const auto it = tasks_.find(id);
    return it == tasks_.end() ? std::nullopt : std::optional<Task>(*it);
}

std::optional<Task> DownloadManager::activeTaskFor(const QString& slug, double number) const {
    for (const auto& task : tasks_) {
        if (task.slug == slug && task.number == number && task.state != TaskState::Completed &&
            task.state != TaskState::Cancelled) {
            return task;
        }
    }
    return std::nullopt;
}

QString DownloadManager::basePathFor(const QString& anime_title, double number) const {
    const auto folder = util::safeFileName(anime_title);
    return QDir(settings_().directory)
        .filePath(folder + QLatin1Char('/') + folder + QStringLiteral(" - ") +
                  episodeLabel(number));
}

std::expected<qint64, QString> DownloadManager::enqueue(const QString& slug, double number,
                                                        const QString& anime_title,
                                                        TaskOrigin origin) {
    if (const auto existing = activeTaskFor(slug, number)) {
        if (existing->state == TaskState::Failed) {
            retry(existing->id);
        } else if (existing->state == TaskState::Paused) {
            resume(existing->id);
        }
        return existing->id;
    }
    const auto settings = settings_();
    if (settings.directory.isEmpty() || !QDir().mkpath(settings.directory)) {
        return std::unexpected(QStringLiteral("La carpeta de descargas no está disponible."));
    }
    auto created = services_.repository.create(slug, number, anime_title, settings.audio, origin,
                                               basePathFor(anime_title, number));
    if (!created) {
        return std::unexpected(created.error().message);
    }
    tasks_.insert(created->id, *created);
    emit taskAdded(created->id);
    pump();
    return created->id;
}

int DownloadManager::running() const {
    return static_cast<int>(std::ranges::count_if(tasks_, [](const Task& task) {
        return task.state == TaskState::Resolving || task.state == TaskState::Downloading ||
               task.state == TaskState::Verifying;
    }));
}

void DownloadManager::pump() {
    if (stopping_ || !started_) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    QDateTime next_wake;
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
        if (it->state != TaskState::Waiting) {
            continue;
        }
        if (!it->retryAt.isValid() || it->retryAt <= now) {
            it->state = TaskState::Queued;
            store(*it);
        } else if (!next_wake.isValid() || it->retryAt < next_wake) {
            next_wake = it->retryAt;
        }
    }
    if (next_wake.isValid()) {
        wake_timer_.start(std::clamp<qint64>(now.msecsTo(next_wake) + 50, 50, 3'600'000));
    }

    if (services_.aria2.isReady()) {
        const int limit = std::max(1, settings_().maxConcurrent);
        auto queued = tasks_.values();
        queued.removeIf([](const Task& task) { return task.state != TaskState::Queued; });
        std::ranges::sort(queued, {}, &Task::queueOrder);
        for (const auto& task : queued) {
            if (running() >= limit) {
                break;
            }
            resolve(task.id);
        }
    }
    const bool transferring = std::ranges::any_of(
        tasks_, [](const Task& task) { return task.state == TaskState::Downloading; });
    if (transferring && !poll_timer_.isActive()) {
        poll_timer_.start();
    } else if (!transferring) {
        poll_timer_.stop();
    }
}

bool DownloadManager::isCurrent(qint64 id, quint64 generation, TaskState state) const {
    const auto task = tasks_.find(id);
    const auto live = live_.find(id);
    return !stopping_ && task != tasks_.end() && live != live_.end() &&
           live->generation == generation && task->state == state;
}

void DownloadManager::resolve(qint64 id) {
    auto& task = tasks_[id];
    auto& live = live_[id];
    ++live.generation;
    task.state = TaskState::Resolving;
    task.issue = TaskIssue::None;
    task.issueDetail.clear();
    store(task);

    const auto settings = settings_();
    QList<api::Provider> providers;
    for (const auto provider : settings.providers) {
        if (isNative(provider)) {
            providers.append(provider);
        }
    }
    if (providers.isEmpty()) {
        providers = {api::Provider::Mega, api::Provider::Pixeldrain};
    }
    const auto generation = live.generation;
    services_.api.resolve(
        task.slug, {.episodeNumbers = {task.number}, .audio = task.audio, .providers = providers},
        this, [this, id, generation, providers](const api::Result<api::Resolution>& result) {
            if (!isCurrent(id, generation, TaskState::Resolving)) {
                return;
            }
            if (!result) {
                const auto& error = result.error();
                switch (error.kind) {
                case api::ApiError::Kind::RateLimited:
                    wait(id, TaskIssue::RateLimited, std::max(error.retryAfter, 30s));
                    return;
                case api::ApiError::Kind::NotFound:
                case api::ApiError::Kind::Contract:
                    fail(id, TaskIssue::NoSource, error.message);
                    return;
                default:
                    retryLater(id, TaskIssue::Network, error.message);
                    return;
                }
            }
            QList<api::ResolvedLink> candidates;
            for (const auto& episode : result->value.episodes) {
                for (const auto provider : providers) {
                    for (const auto& link : episode.links) {
                        if (link.provider == provider) {
                            candidates.append(link);
                        }
                    }
                }
            }
            if (candidates.isEmpty()) {
                fail(id, TaskIssue::NoSource);
                return;
            }
            tryCandidate(id, generation, candidates, 0, TaskIssue::NoSource);
        });
}

void DownloadManager::tryCandidate(qint64 id, quint64 generation,
                                   QList<api::ResolvedLink> candidates, int index,
                                   TaskIssue last_issue) {
    if (index >= candidates.size()) {
        fail(id, last_issue);
        return;
    }
    const auto link = candidates[index];
    const auto next = [this, id, generation, candidates, index](TaskIssue issue) {
        tryCandidate(id, generation, candidates, index + 1, issue);
    };
    if (link.provider == api::Provider::Mega) {
        const auto file_link = mega::parseFileLink(link.url);
        if (!file_link) {
            next(TaskIssue::NoSource);
            return;
        }
        services_.mega.file(*file_link, this,
                            [this, id, generation, file_link, next](
                                const std::expected<mega::RemoteFile, mega::ApiFailure>& answer) {
                                if (!isCurrent(id, generation, TaskState::Resolving)) {
                                    return;
                                }
                                if (!answer) {
                                    switch (answer.error().kind) {
                                    case mega::ApiFailure::Kind::NotFound:
                                    case mega::ApiFailure::Kind::Blocked:
                                        next(TaskIssue::SourceRemoved);
                                        return;
                                    case mega::ApiFailure::Kind::OverQuota:
                                        wait(id, TaskIssue::Quota, kQuotaWait);
                                        return;
                                    case mega::ApiFailure::Kind::Protocol:
                                        next(TaskIssue::NoSource);
                                        return;
                                    default:
                                        retryLater(id, TaskIssue::Network, answer.error().message);
                                        return;
                                    }
                                }
                                startTransfer(id, api::Provider::Mega, file_link->handle,
                                              answer->url, answer->size, answer->name, file_link);
                            });
        return;
    }
    if (link.provider == api::Provider::Pixeldrain) {
        const auto file_id = pixeldrain::fileId(link.url);
        if (!file_id) {
            next(TaskIssue::NoSource);
            return;
        }
        services_.pixeldrain.info(
            *file_id, this,
            [this, id, generation, file_id,
             next](const std::expected<pixeldrain::Info, pixeldrain::Failure>& info) {
                if (!isCurrent(id, generation, TaskState::Resolving)) {
                    return;
                }
                if (!info) {
                    if (info.error().kind == pixeldrain::Failure::Kind::NotFound) {
                        next(TaskIssue::SourceRemoved);
                    } else if (info.error().kind == pixeldrain::Failure::Kind::Protocol) {
                        next(TaskIssue::NoSource);
                    } else {
                        retryLater(id, TaskIssue::Network, info.error().message);
                    }
                    return;
                }
                startTransfer(id, api::Provider::Pixeldrain, *file_id, info->download, info->size,
                              info->name, std::nullopt);
            });
        return;
    }
    next(last_issue);
}

void DownloadManager::startTransfer(qint64 id, api::Provider provider, const QString& source_id,
                                    const QUrl& url, qint64 size, const QString& provider_name,
                                    std::optional<mega::FileLink> mega_link) {
    auto& task = tasks_[id];
    auto& live = live_[id];
    const auto directory = settings_().directory;

    // A different source means the bytes on disk belong to another file.
    if (!task.sourceId.isEmpty() && (task.sourceId != source_id || task.provider != provider)) {
        deletePartials(task.targetPath);
        task.bytesDone = 0;
    }
    if (QFileInfo(task.targetPath).suffix().isEmpty() ||
        !QStringList{QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
                     QStringLiteral("webm"), QStringLiteral("mov"), QStringLiteral("m4v"),
                     QStringLiteral("ts")}
             .contains(QFileInfo(task.targetPath).suffix().toLower())) {
        task.targetPath += util::videoExtension(provider_name);
    }
    if (!util::isInside(task.targetPath, directory)) {
        fail(id, TaskIssue::Storage, QStringLiteral("destination outside the download folder"));
        return;
    }
    const QFileInfo target(task.targetPath);
    if (!QDir().mkpath(target.absolutePath())) {
        fail(id, TaskIssue::Storage, target.absolutePath());
        return;
    }
    const bool is_mega = provider == api::Provider::Mega;
    live.partialPath =
        task.targetPath + (is_mega ? QStringLiteral(".mega") : QStringLiteral(".part"));
    live.megaLink = std::move(mega_link);
    const qint64 on_disk = QFileInfo(live.partialPath).size();
    const qint64 needed = (is_mega ? 2 * size : size) - on_disk + kSpaceMargin;
    const QStorageInfo storage(target.absolutePath());
    if (storage.isValid() && storage.bytesAvailable() < needed) {
        fail(id, TaskIssue::NoSpace, QString::number(needed));
        return;
    }

    task.provider = provider;
    task.sourceId = source_id;
    task.bytesTotal = size;
    // A whole file without aria2's control file finished while AnimeHub was not watching
    // (closed or crashed right then): finish it instead of downloading it again.
    if (size > 0 && on_disk == size &&
        !QFileInfo::exists(live.partialPath + QStringLiteral(".aria2"))) {
        task.bytesDone = size;
        store(task);
        finishTransfer(id);
        return;
    }
    const auto generation = live.generation;
    services_.aria2.add({.url = url,
                         .directory = QDir::toNativeSeparators(target.absolutePath()),
                         .fileName = QFileInfo(live.partialPath).fileName(),
                         .connections = is_mega ? 4 : 2},
                        this, [this, id, generation](const std::expected<QString, QString>& gid) {
                            if (!isCurrent(id, generation, TaskState::Resolving)) {
                                if (gid) {
                                    services_.aria2.remove(*gid); // paused or cancelled meanwhile
                                }
                                return;
                            }
                            if (!gid) {
                                retryLater(id, TaskIssue::Engine, gid.error());
                                return;
                            }
                            live_[id].gid = *gid;
                            setState(id, TaskState::Downloading);
                            pump();
                        });
}

void DownloadManager::poll() {
    QStringList gids;
    QHash<QString, qint64> owners;
    for (auto it = live_.begin(); it != live_.end(); ++it) {
        const auto task = tasks_.find(it.key());
        if (task != tasks_.end() && task->state == TaskState::Downloading && !it->gid.isEmpty()) {
            gids.append(it->gid);
            owners.insert(it->gid, it.key());
        }
    }
    if (gids.isEmpty()) {
        poll_timer_.stop();
        return;
    }
    services_.aria2.status(
        gids, this,
        [this, owners](const std::expected<QList<Aria2::Transfer>, QString>& transfers) {
            if (!transfers || stopping_) {
                return; // the engine's own failure signal handles a dead aria2
            }
            const auto now = QDateTime::currentDateTimeUtc();
            for (const auto& transfer : *transfers) {
                const auto id = owners.value(transfer.gid);
                auto task = tasks_.find(id);
                if (task == tasks_.end() || task->state != TaskState::Downloading ||
                    live_[id].gid != transfer.gid) {
                    continue;
                }
                switch (transfer.status) {
                case Aria2::Transfer::Status::Active:
                case Aria2::Transfer::Status::Waiting: {
                    task->bytesDone = transfer.completedBytes;
                    task->bytesPerSecond = transfer.bytesPerSecond;
                    if (transfer.totalBytes > 0) {
                        task->bytesTotal = transfer.totalBytes;
                    }
                    auto& live = live_[id];
                    const bool save = !live.savedAt.isValid() ||
                                      live.savedAt.secsTo(now) >= kProgressSaveInterval.count();
                    if (save) {
                        live.savedAt = now;
                        store(*task, false);
                    }
                    emit taskChanged(id);
                    break;
                }
                case Aria2::Transfer::Status::Complete:
                    task->bytesDone = task->bytesTotal;
                    task->bytesPerSecond = 0;
                    finishTransfer(id);
                    break;
                case Aria2::Transfer::Status::Error:
                    transferFailed(id, transfer.errorCode, transfer.errorMessage);
                    break;
                case Aria2::Transfer::Status::Removed:
                case Aria2::Transfer::Status::Unknown:
                    live_[id].gid.clear();
                    task->bytesPerSecond = 0;
                    task->state = TaskState::Queued; // aria2 lost it; resume from the partial
                    store(*task);
                    pump();
                    break;
                case Aria2::Transfer::Status::Paused:
                    break;
                }
            }
        });
}

void DownloadManager::finishTransfer(qint64 id) {
    auto& live = live_[id];
    if (!live.gid.isEmpty()) {
        services_.aria2.remove(live.gid);
        live.gid.clear();
    }
    QFile::remove(live.partialPath + QStringLiteral(".aria2"));
    if (live.megaLink) {
        verifyMega(id);
        return;
    }
    const auto final_path = util::uniquePath(tasks_[id].targetPath);
    if (final_path.isEmpty() || !QFile::rename(live.partialPath, final_path)) {
        fail(id, TaskIssue::Storage, final_path);
        return;
    }
    complete(id, final_path);
}

void DownloadManager::verifyMega(qint64 id) {
    auto& task = tasks_[id];
    auto& live = live_[id];
    ++live.generation;
    task.state = TaskState::Verifying;
    task.bytesDone = 0;
    store(task);
    pump();

    const auto generation = live.generation;
    const auto source = live.partialPath;
    const auto destination = util::uniquePath(task.targetPath);
    const auto link = *live.megaLink;
    live.cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = live.cancel;
    QPointer<DownloadManager> self(this);
    QThreadPool::globalInstance()->start([self, id, generation, source, destination, link, cancel] {
        auto last_report = std::chrono::steady_clock::now();
        const auto result = mega::decryptFile(source, destination, link, *cancel, [&](qint64 done) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_report < 300ms) {
                return;
            }
            last_report = now;
            QMetaObject::invokeMethod(self, [self, id, generation, done] {
                if (self && self->isCurrent(id, generation, TaskState::Verifying)) {
                    self->tasks_[id].bytesDone = done;
                    emit self->taskChanged(id);
                }
            });
        });
        QMetaObject::invokeMethod(self, [self, id, generation, source, destination, result] {
            if (!self || !self->isCurrent(id, generation, TaskState::Verifying)) {
                return;
            }
            if (result) {
                QFile::remove(source);
                self->complete(id, destination);
                return;
            }
            switch (result.error()) {
            case mega::CryptoError::Integrity: {
                QFile::remove(source);
                auto& task = self->tasks_[id];
                task.sourceId.clear();
                task.bytesDone = 0;
                if (++task.attempts >= kMaxSourceAttempts) {
                    self->fail(id, TaskIssue::Integrity);
                } else {
                    task.state = TaskState::Queued;
                    self->store(task);
                    self->pump();
                }
                return;
            }
            case mega::CryptoError::Io:
                self->fail(id, TaskIssue::Storage, destination);
                return;
            case mega::CryptoError::Platform:
                self->fail(id, TaskIssue::Engine, QStringLiteral("AES"));
                return;
            case mega::CryptoError::Cancelled:
                return;
            }
        });
    });
}

void DownloadManager::complete(qint64 id, const QString& path) {
    auto& task = tasks_[id];
    const auto size = QFileInfo(path).size();
    task.targetPath = path;
    task.bytesDone = size;
    task.bytesTotal = size;
    task.bytesPerSecond = 0;
    task.state = TaskState::Completed;
    task.issue = TaskIssue::None;
    task.issueDetail.clear();
    task.finishedAt = QDateTime::currentDateTimeUtc();
    live_.remove(id);
    if (auto recorded = services_.library.setEpisodeFile(task.slug, task.number, path, size);
        !recorded) {
        qWarning("library: %s", qPrintable(recorded.error().message));
    }
    store(task);
    emit taskCompleted(id);
    pump();
}

void DownloadManager::transferFailed(qint64 id, int code, const QString& message) {
    auto& live = live_[id];
    if (!live.gid.isEmpty()) {
        services_.aria2.remove(live.gid);
        live.gid.clear();
    }
    auto& task = tasks_[id];
    task.bytesPerSecond = 0;
    switch (code) {
    case kNoSpace:
        fail(id, TaskIssue::NoSpace, message);
        return;
    case kFileExists:
    case kRenameFailed:
    case kOpenFailed:
    case kCreateFailed:
    case kIoError:
    case kDirectoryFailed:
        fail(id, TaskIssue::Storage, message);
        return;
    case kResumeUnsupported:
        deletePartials(task.targetPath);
        task.bytesDone = 0;
        retryLater(id, TaskIssue::Network, message);
        return;
    case kHttpUnexpected:
        if (message.contains(u"509")) { // MEGA: transfer quota exhausted
            wait(id, TaskIssue::Quota, kQuotaWait, message);
            return;
        }
        [[fallthrough]];
    case kNotFound:
    case kHttpAuthorization:
        // Temporary URLs expire: resolve again, a few times, before giving up on the source.
        if (++task.attempts >= kMaxSourceAttempts) {
            fail(id, TaskIssue::SourceRemoved, message);
        } else {
            task.state = TaskState::Queued;
            store(task);
            pump();
        }
        return;
    default:
        retryLater(id, TaskIssue::Network, message);
        return;
    }
}

void DownloadManager::wait(qint64 id, TaskIssue issue, std::chrono::seconds delay,
                           const QString& detail) {
    auto& task = tasks_[id];
    ++live_[id].generation;
    task.state = TaskState::Waiting;
    task.issue = issue;
    task.issueDetail = detail;
    task.retryAt = QDateTime::currentDateTimeUtc().addSecs(delay.count());
    store(task);
    pump();
}

void DownloadManager::retryLater(qint64 id, TaskIssue issue, const QString& detail) {
    auto& task = tasks_[id];
    if (++task.attempts >= kMaxNetworkAttempts) {
        fail(id, issue, detail);
        return;
    }
    const auto delay = std::chrono::seconds(std::min(30 << (task.attempts - 1), 1800));
    wait(id, issue, delay, detail);
}

void DownloadManager::fail(qint64 id, TaskIssue issue, const QString& detail) {
    auto& task = tasks_[id];
    ++live_[id].generation;
    task.state = TaskState::Failed;
    task.issue = issue;
    task.issueDetail = detail;
    task.bytesPerSecond = 0;
    task.finishedAt = QDateTime::currentDateTimeUtc();
    store(task);
    emit taskFailed(id);
    pump();
}

void DownloadManager::setState(qint64 id, TaskState state) {
    auto& task = tasks_[id];
    task.state = state;
    store(task);
}

void DownloadManager::store(const Task& task, bool announce) {
    if (auto saved = services_.repository.save(task); !saved) {
        qWarning("download queue: %s", qPrintable(saved.error().message));
    }
    if (announce) {
        emit taskChanged(task.id);
    }
}

void DownloadManager::dropTransfer(qint64 id) {
    auto& live = live_[id];
    ++live.generation;
    if (live.cancel) {
        live.cancel->store(true);
    }
    if (!live.gid.isEmpty()) {
        services_.aria2.remove(live.gid);
        live.gid.clear();
    }
}

void DownloadManager::deletePartials(const QString& final_path) {
    removeFiles({final_path + QStringLiteral(".part"), final_path + QStringLiteral(".part.aria2"),
                 final_path + QStringLiteral(".mega"), final_path + QStringLiteral(".mega.aria2")},
                10);
}

void DownloadManager::removeFiles(const QStringList& paths, int attempts_left) {
    // aria2 releases the files a moment after a removal; try again for a few seconds.
    QStringList pending;
    for (const auto& path : paths) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            pending.append(path);
        }
    }
    if (!pending.isEmpty() && attempts_left > 0) {
        QTimer::singleShot(500ms, this, [this, pending, attempts_left] {
            removeFiles(pending, attempts_left - 1);
        });
    }
}

void DownloadManager::pause(qint64 id) {
    const auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return;
    }
    switch (it->state) {
    case TaskState::Downloading:
        services_.aria2.pause(live_[id].gid);
        it->bytesPerSecond = 0;
        setState(id, TaskState::Paused);
        break;
    case TaskState::Resolving:
    case TaskState::Queued:
    case TaskState::Waiting:
        ++live_[id].generation;
        setState(id, TaskState::Paused);
        break;
    default:
        return;
    }
    pump();
}

void DownloadManager::resume(qint64 id) {
    const auto it = tasks_.find(id);
    if (it == tasks_.end() || it->state != TaskState::Paused) {
        return;
    }
    auto& live = live_[id];
    if (!live.gid.isEmpty() && services_.aria2.isReady()) {
        services_.aria2.resume(live.gid);
        setState(id, TaskState::Downloading);
    } else {
        live.gid.clear();
        setState(id, TaskState::Queued);
    }
    pump();
}

void DownloadManager::cancel(qint64 id) {
    const auto it = tasks_.find(id);
    if (it == tasks_.end() || finished(it->state)) {
        return;
    }
    dropTransfer(id);
    deletePartials(it->targetPath);
    it->state = TaskState::Cancelled;
    it->bytesPerSecond = 0;
    it->finishedAt = QDateTime::currentDateTimeUtc();
    store(*it);
    live_.remove(id);
    pump();
}

void DownloadManager::retry(qint64 id) {
    const auto it = tasks_.find(id);
    if (it == tasks_.end() || (it->state != TaskState::Failed && it->state != TaskState::Waiting)) {
        return;
    }
    it->attempts = 0;
    it->issue = TaskIssue::None;
    it->issueDetail.clear();
    it->retryAt = {};
    it->finishedAt = {};
    setState(id, TaskState::Queued);
    pump();
}

void DownloadManager::forget(qint64 id) {
    const auto it = tasks_.find(id);
    if (it == tasks_.end() || !finished(it->state)) {
        return;
    }
    if (it->state == TaskState::Failed) {
        deletePartials(it->targetPath);
    }
    if (auto removed = services_.repository.remove(id); !removed) {
        qWarning("download queue: %s", qPrintable(removed.error().message));
        return;
    }
    tasks_.erase(it);
    live_.remove(id);
    emit taskRemoved(id);
}

void DownloadManager::clearFinished() {
    const auto ids = tasks_.keys();
    for (const auto id : ids) {
        const auto state = tasks_.value(id).state;
        if (state == TaskState::Completed || state == TaskState::Cancelled) {
            forget(id);
        }
    }
}

void DownloadManager::pauseAll() {
    for (const auto id : tasks_.keys()) {
        pause(id);
    }
}

void DownloadManager::resumeAll() {
    for (const auto id : tasks_.keys()) {
        resume(id);
    }
}

} // namespace animehub::downloads
