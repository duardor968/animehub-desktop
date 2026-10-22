#include "downloads_view.h"

#include "display.h"
#include "shell.h"

#include <QFileInfo>
#include <QLocale>

#include <algorithm>

namespace animehub::app {
namespace {

using downloads::Task;
using downloads::TaskState;

// Two minutes of total speed, one bar every two seconds.
constexpr int kSamples = 60;
constexpr auto kSampleInterval = std::chrono::seconds(2);

bool running(TaskState state) {
    return state == TaskState::Resolving || state == TaskState::Downloading ||
           state == TaskState::Verifying;
}

bool native(const Task& task) {
    return task.engine == downloads::DownloadEngine::Native;
}

bool pausable(const Task& task) {
    return native(task) &&
           (task.state == TaskState::Queued || task.state == TaskState::Resolving ||
            task.state == TaskState::Downloading || task.state == TaskState::Waiting);
}

bool retryable(const Task& task) {
    return task.state == TaskState::Failed &&
           (native(task) || task.engineSubmission.isEmpty() || task.engineJobId > 0);
}

QString engineName(const Task& task) {
    return native(task) ? QStringLiteral("Incorporado") : QStringLiteral("JDownloader");
}

QString phaseName(const Task& task) {
    if (task.enginePhase == u"extracting")
        return QStringLiteral("Extrayendo");
    if (task.enginePhase == u"collecting" || task.enginePhase == u"submitting")
        return QStringLiteral("Recopilando enlaces");
    if (task.enginePhase == u"waiting")
        return QStringLiteral("En espera");
    if (task.enginePhase == u"moving" || task.enginePhase == u"starting")
        return QStringLiteral("Preparando descarga");
    if (task.enginePhase == u"cancelling" || task.enginePhase == u"cancel_requested")
        return QStringLiteral("Cancelación pendiente");
    return QStringLiteral("Descargando");
}

QString taskDetail(const Task& task) {
    if (native(task))
        return display::download(task);
    if (task.state == TaskState::Failed && !retryable(task))
        return QStringLiteral(
            "Envío sin confirmar · revisa el trabajo en JDownloader antes de volver a enviarlo");
    if ((task.state == TaskState::Waiting || task.state == TaskState::Failed) &&
        task.issue == downloads::TaskIssue::Engine)
        return QStringLiteral("Sin conexión con JDownloader · último estado: ") + phaseName(task);
    if (task.state == TaskState::Downloading || task.state == TaskState::Verifying)
        return phaseName(task) + (task.bytesTotal > 0
                                      ? QStringLiteral(" · ") + display::bytes(task.bytesDone) +
                                            QStringLiteral(" de ") + display::bytes(task.bytesTotal)
                                      : QString());
    return display::download(task);
}
// Still in line: what the queue section shows. Completed and cancelled ones are history.
bool inLine(TaskState state) {
    return state != TaskState::Completed && state != TaskState::Cancelled;
}

// Bytes on disk: a MEGA file being checked was already downloaded whole.
qint64 bytesOf(const Task& task) {
    return task.state == TaskState::Completed || task.state == TaskState::Verifying
               ? task.bytesTotal
               : task.bytesDone;
}

double fractionOf(const Task& task) {
    if (task.state == TaskState::Completed) {
        return 1.0;
    }
    return task.bytesTotal > 0
               ? std::clamp(static_cast<double>(task.bytesDone) / task.bytesTotal, 0.0, 1.0)
               : 0.0;
}

QString count(qsizetype n, const QString& one, const QString& many) {
    return n == 1 ? one : many.arg(n);
}

QString folderOf(const QList<Task>& tasks) {
    for (const auto& task : tasks) {
        if (task.engine == downloads::DownloadEngine::JDownloaderCloud)
            continue;
        const auto folder =
            native(task) ? QFileInfo(task.targetPath).absolutePath() : task.engineDestination;
        if (!folder.isEmpty() && QFileInfo(folder).isDir())
            return folder;
    }
    return {};
}

} // namespace

DownloadsView::DownloadsView(downloads::DownloadManager& manager, library::LibraryService& library,
                             playback::Player& player, QObject* parent)
    : QObject(parent), manager_(manager), library_(library), player_(player) {
    rebuild_timer_.setSingleShot(true);
    rebuild_timer_.setInterval(60);
    connect(&rebuild_timer_, &QTimer::timeout, this, &DownloadsView::rebuild);
    const auto later = [this] { rebuild_timer_.start(); };
    connect(&manager_, &downloads::DownloadManager::loaded, this, later);
    connect(&manager_, &downloads::DownloadManager::taskAdded, this, later);
    connect(&manager_, &downloads::DownloadManager::taskRemoved, this, [this, later](qint64 id) {
        seen_.remove(id);
        later();
    });
    connect(&manager_, &downloads::DownloadManager::taskChanged, this, &DownloadsView::taskChanged);
    connect(&library_, &library::LibraryService::animeChanged, this,
            [this, later](const QString& slug) {
                infos_.remove(slug);
                later();
            });
    connect(&library_, &library::LibraryService::listChanged, this, [this, later] {
        infos_.clear();
        later();
    });

    samples_.fill(0.0, kSamples);
    sample_timer_.setInterval(kSampleInterval);
    connect(&sample_timer_, &QTimer::timeout, this, &DownloadsView::sample);
    rebuild();
}

const DownloadsView::SeriesInfo& DownloadsView::info(const QString& slug) {
    if (const auto it = infos_.constFind(slug); it != infos_.cend()) {
        return *it;
    }
    SeriesInfo info;
    const auto& repository = library_.repository();
    if (const auto entry = repository.entry(slug); entry && *entry) {
        const auto& anime = **entry;
        info.art = anime.backdropUrl.isEmpty() ? anime.posterUrl : anime.backdropUrl;
        info.resumeNumber = anime.resumeNumber;
    }
    if (const auto episodes = repository.episodes(slug)) {
        for (const auto& episode : *episodes) {
            info.episodes.insert(episode.number, episode);
        }
    }
    return *infos_.insert(slug, std::move(info));
}

// The episode "Continuar" would play if it is downloaded, else the first downloaded one not
// watched yet.
std::optional<double> DownloadsView::nextToWatch(const QString& slug, const QList<Task>& tasks) {
    const auto& series = info(slug);
    const auto on_disk = [&series](double number) {
        const auto it = series.episodes.constFind(number);
        return it != series.episodes.cend() && !it->filePath.isEmpty();
    };
    if (series.resumeNumber && on_disk(*series.resumeNumber)) {
        return series.resumeNumber;
    }
    QList<double> numbers;
    for (const auto& task : tasks) {
        if (task.state == TaskState::Completed) {
            numbers.append(task.number);
        }
    }
    std::ranges::sort(numbers);
    for (const auto number : numbers) {
        if (on_disk(number) &&
            series.episodes.value(number).state != library::WatchState::Watched) {
            return number;
        }
    }
    return std::nullopt;
}

void DownloadsView::rebuild() {
    struct Group {
        QString slug;
        QString title;
        QList<Task> tasks;
        bool inLine = false;
    };
    // Queue order first, then the finished ones, newest first: a series takes the place of its
    // first task in that order.
    const auto tasks = manager_.tasks();
    QList<Group> groups;
    QHash<QString, qsizetype> places;
    for (const auto& task : tasks) {
        if (task.state == TaskState::Cancelled) {
            continue;
        }
        auto place = places.constFind(task.slug);
        if (place == places.cend()) {
            place = places.insert(task.slug, groups.size());
            groups.append({.slug = task.slug, .title = task.animeTitle});
        }
        auto& group = groups[*place];
        group.tasks.append(task);
        group.inLine = group.inLine || inLine(task.state);
    }

    QList<DownloadSeriesRow> queue;
    QList<DownloadSeriesRow> completed;
    QHash<QString, QString> counts;  // "3 de 12" by series, for the bar
    QHash<QString, QString> amounts; // "3 de 12 descargados · 1,1 GB de 4,2 GB", for the header
    for (auto& group : groups) {
        std::ranges::sort(group.tasks, {}, &Task::number);
        const auto& series = info(group.slug);
        int done = 0;
        int failed = 0;
        int paused = 0;
        int queued = 0;
        int waiting = 0;
        int active = 0;
        int disconnected = 0;
        qint64 bytes = 0;
        qint64 done_bytes = 0;
        qint64 total = 0;
        qint64 partial = 0;
        QList<double> partial_numbers; // episodes cancelling would delete bytes of
        bool totals_known = true;
        double fraction = 0;
        QDateTime retry_at;
        int files = 0;
        qint64 file_bytes = 0;
        DownloadSeriesRow row{.slug = group.slug, .title = group.title, .art = series.art};

        for (const auto& task : group.tasks) {
            const auto episode = series.episodes.constFind(task.number);
            const bool on_disk =
                task.state == TaskState::Completed && episode != series.episodes.cend() &&
                !episode->filePath.isEmpty() && QFileInfo(episode->filePath).isFile();
            if (!native(task) && task.issue == downloads::TaskIssue::Engine &&
                (task.state == TaskState::Waiting || task.state == TaskState::Failed))
                ++disconnected;
            switch (task.state) {
            case TaskState::Completed:
                ++done;
                done_bytes += bytesOf(task);
                break;
            case TaskState::Failed:
                ++failed;
                break;
            case TaskState::Paused:
                ++paused;
                break;
            case TaskState::Queued:
                ++queued;
                break;
            case TaskState::Waiting:
                ++waiting;
                if (!retry_at.isValid() || task.retryAt < retry_at) {
                    retry_at = task.retryAt;
                }
                break;
            default:
                ++active;
                break;
            }
            bytes += bytesOf(task);
            total += task.bytesTotal;
            totals_known = totals_known && task.bytesTotal > 0;
            fraction += task.state == TaskState::Verifying ? 1.0 : fractionOf(task);
            if (native(task) && task.state != TaskState::Completed && bytesOf(task) > 0) {
                partial += bytesOf(task);
                partial_numbers.append(task.number);
            }
            if (on_disk) {
                ++files;
                file_bytes += episode->fileSize;
            }
            row.canPause = row.canPause || pausable(task);
            row.canResume = row.canResume || (native(task) && task.state == TaskState::Paused);
            row.canRetry = row.canRetry || retryable(task);
            if (!row.engines.contains(engineName(task))) {
                if (!row.engines.isEmpty())
                    row.engines += QStringLiteral(" · ");
                row.engines += engineName(task);
            }
            row.episodes.append(
                {.id = task.id,
                 .number = task.number,
                 .label = display::episode(task.number),
                 .image = episode != series.episodes.cend() ? episode->imageUrl : QUrl(),
                 .state = downloads::stateKey(task.state),
                 .progress = fractionOf(task),
                 .detail = taskDetail(task),
                 .problem = task.state == TaskState::Failed,
                 .hasFile = on_disk,
                 .engine = engineName(task),
                 .source = task.provider ? display::provider(*task.provider) : QString(),
                 .connection = native(task) ? QString()
                               : task.engine == downloads::DownloadEngine::JDownloaderLocal
                                   ? QStringLiteral("Local")
                                   : QStringLiteral("MyJDownloader"),
                 .destination = native(task) ? QFileInfo(task.targetPath).absolutePath()
                                             : task.engineDestination,
                 .phase = native(task) ? downloads::stateKey(task.state) : task.enginePhase,
                 .canPause = pausable(task),
                 .canResume = native(task) && task.state == TaskState::Paused,
                 .canRetry = retryable(task),
                 .partial = native(task) && task.state != TaskState::Completed && bytesOf(task) > 0
                                ? display::bytes(bytesOf(task))
                                : QString()});
        }

        QStringList destinations;
        for (const auto& episode : row.episodes) {
            if (!episode.destination.isEmpty() && !destinations.contains(episode.destination))
                destinations.append(episode.destination);
        }
        row.destinations = destinations.join(QStringLiteral(" · "));

        const auto episodes = group.tasks.size();
        QStringList summary;
        if (group.inLine && active == 0) {
            if (disconnected > 0) {
                summary.append(QStringLiteral("JDownloader sin conexión"));
            } else if (paused > 0 && queued + waiting == 0) {
                summary.append(QStringLiteral("En pausa"));
            } else if (waiting > 0 && queued == 0) {
                summary.append(
                    QStringLiteral("Se reintenta a las ") +
                    QLocale(QLocale::Spanish, QLocale::Spain)
                        .toString(retry_at.toLocalTime().time(), QStringLiteral("HH:mm")));
            } else if (queued > 0) {
                summary.append(QStringLiteral("En cola"));
            }
        }
        summary.append(QStringLiteral("%1 de %2").arg(done).arg(episodes));
        // Next to a count of finished episodes, their size; what is half done only shows in
        // the proportion of the whole.
        QString size;
        if (group.inLine && totals_known && total > 0) {
            size = display::bytes(bytes) + QStringLiteral(" de ") + display::bytes(total);
        } else if (done_bytes > 0) {
            size = display::bytes(done_bytes);
        }
        if (!size.isEmpty()) {
            summary.append(size);
        }
        row.summary = summary.join(QStringLiteral(" · "));
        amounts.insert(group.slug, QStringLiteral("%1 de %2 descargados").arg(done).arg(episodes) +
                                       (size.isEmpty() ? QString() : QStringLiteral(" · ") + size));
        row.progress = episodes > 0 ? fraction / static_cast<double>(episodes) : 0.0;
        row.failed = failed;
        row.failedText = failed > 0 ? QStringLiteral("%1 con error").arg(failed) : QString();

        if (partial > 0) {
            row.cancelText = partial_numbers.size() == 1
                                 ? QStringLiteral("Se borrarán los %1 descargados del episodio %2.")
                                       .arg(display::bytes(partial),
                                            display::episodeNumber(partial_numbers.first()))
                                 : QStringLiteral("Se borrarán los %1 descargados de %2 episodios.")
                                       .arg(display::bytes(partial))
                                       .arg(partial_numbers.size());
            if (done > 0) {
                row.cancelText +=
                    done == 1 ? QStringLiteral(" El episodio completo se queda.")
                              : QStringLiteral(" Los %1 episodios completos se quedan.").arg(done);
            }
        }
        row.filesOnDisk = files;
        row.diskText =
            files > 0 ? count(files, QStringLiteral("1 episodio"), QStringLiteral("%1 episodios")) +
                            QStringLiteral(" · ") + display::bytes(file_bytes)
                      : QString();
        if (const auto next = nextToWatch(group.slug, group.tasks)) {
            const auto episode = series.episodes.value(*next);
            row.watchText = episode.state == library::WatchState::Watching && episode.positionMs > 0
                                ? QStringLiteral("Continuar · Ep. ") + display::episodeNumber(*next)
                                : QStringLiteral("Ver ep. ") + display::episodeNumber(*next);
        }
        row.hasFolder = QFileInfo(folderOf(group.tasks)).isDir();
        counts.insert(group.slug, QStringLiteral("%1 de %2").arg(done).arg(episodes));
        (group.inLine ? queue : completed).append(std::move(row));
    }
    if (!queue.isEmpty()) {
        queue.first().first = true;
    }

    // What the top of the page and the bar show: something running, else what will start by
    // itself, else what waits for the user, else the failures.
    int active = 0;
    int pending = 0; // will start or retry by itself
    int paused = 0;
    int failed = 0;
    qint64 speed = 0;
    const Task* shown = nullptr;
    const Task* next = nullptr;
    const Task* held = nullptr;
    const Task* broken = nullptr;
    for (const auto& task : tasks) {
        if (running(task.state)) {
            ++active;
            if (!shown) {
                shown = &task;
            }
            if (task.state == TaskState::Downloading) {
                speed += task.bytesPerSecond;
            }
        } else if (task.state == TaskState::Queued || task.state == TaskState::Waiting) {
            ++pending;
            if (!next) {
                next = &task;
            }
        } else if (task.state == TaskState::Paused) {
            ++paused;
            if (!held) {
                held = &task;
            }
        } else if (task.state == TaskState::Failed) {
            ++failed;
            if (!broken)
                broken = &task;
        }
    }
    Header header;
    Bar bar;
    const auto* subject = shown ? shown : next ? next : held ? held : broken;
    if (subject) {
        header = {.mode = shown  ? Mode::Active
                          : next ? Mode::Waiting
                          : held ? Mode::Paused
                                 : Mode::Failed,
                  .slug = subject->slug,
                  .title = subject->animeTitle,
                  .episode = display::episode(subject->number) + QStringLiteral(" · ") +
                             amounts.value(subject->slug),
                  // The reason is on the episode's line; a single line here says when.
                  .detail = subject->state == TaskState::Waiting && native(*subject)
                                ? display::retry(subject->retryAt)
                                : taskDetail(*subject),
                  .progress = fractionOf(*subject),
                  .art = info(subject->slug).art};
    } else {
        header.title = QStringLiteral("Nada descargando");
    }

    // The series on view leaves the line under it.
    QList<DownloadSeriesRow> current;
    current_place_ = 0;
    if (subject) {
        const auto place = std::ranges::find(queue, subject->slug, &DownloadSeriesRow::slug);
        if (place != queue.end()) {
            current_place_ = std::distance(queue.begin(), place);
            current.append(std::move(*place));
            queue.erase(place);
        }
    }
    queue_.setRows(std::move(queue));
    completed_.setRows(std::move(completed));
    current_.setRows(std::move(current));
    if (shown || next) {
        QStringList parts{subject->animeTitle, display::shortEpisode(subject->number),
                          counts.value(subject->slug)};
        parts.append(subject->state == TaskState::Downloading ? display::speed(speed)
                                                              : header.detail);
        // Left in line after the one shown, paused ones included, as in the paused bar.
        if (const int after = pending + paused - (shown ? 0 : 1); after > 0) {
            parts.append(QStringLiteral("%1 en cola").arg(after));
        }
        bar = {.mode = header.mode,
               .text = parts.join(QStringLiteral(" · ")),
               .progress = header.progress};
    } else if (held) {
        bar = {.mode = Mode::Paused,
               .text = QStringLiteral("Descargas en pausa · %1 en cola").arg(paused)};
    } else if (failed > 0) {
        bar = {.mode = Mode::Failed,
               .text = count(failed, QStringLiteral("1 descarga con error"),
                             QStringLiteral("%1 descargas con error"))};
    }
    if (bar.mode != Mode::Failed && failed > 0) {
        bar.problem = QStringLiteral("%1 con error").arg(failed);
    }

    header_ = std::move(header);
    bar_ = std::move(bar);
    active_count_ = active;
    any_paused_ = paused > 0;
    emit summaryChanged();

    if (speed != speed_ || speed > peak_) {
        speed_ = speed;
        peak_ = std::max(peak_, speed);
        emit speedChanged();
    }
    if (speed_ > 0 && !sample_timer_.isActive()) {
        sample_timer_.start();
    }
}

// Counts the bytes the session downloads: what a transfer adds between two looks at it, and
// the rest of the file when it ends. Decrypting a MEGA file is not downloading.
void DownloadsView::taskChanged(qint64 id) {
    rebuild_timer_.start();
    const auto task = manager_.task(id);
    if (!task) {
        return;
    }
    const auto last = seen_.constFind(id);
    if (last != seen_.cend() && last->state == TaskState::Downloading) {
        qint64 now = -1;
        if (task->state == TaskState::Downloading || task->state == TaskState::Completed) {
            now = task->bytesDone;
        } else if (task->state == TaskState::Verifying) {
            now = task->bytesTotal;
        }
        if (now > last->bytes) {
            session_bytes_ += now - last->bytes;
            emit speedChanged();
        }
    }
    seen_.insert(id, {.state = task->state, .bytes = task->bytesDone});
}

void DownloadsView::sample() {
    samples_.removeFirst();
    samples_.append(static_cast<qreal>(speed_));
    if (speed_ == 0 && std::ranges::all_of(samples_, [](qreal value) { return value == 0; })) {
        sample_timer_.stop();
    }
    emit speedChanged();
}

QString DownloadsView::speedText() const {
    return display::speed(speed_);
}

QString DownloadsView::peakText() const {
    return display::speed(peak_);
}

QString DownloadsView::totalText() const {
    return display::bytes(session_bytes_);
}

QList<Task> DownloadsView::tasksOf(const QString& slug) const {
    auto tasks = manager_.tasks();
    tasks.removeIf([&slug](const Task& task) { return task.slug != slug; });
    return tasks;
}

bool DownloadsView::canPauseNative() const {
    return std::ranges::any_of(manager_.tasks(), pausable);
}
bool DownloadsView::canResumeNative() const {
    return std::ranges::any_of(manager_.tasks(), [](const Task& task) {
        return native(task) && task.state == TaskState::Paused;
    });
}
bool DownloadsView::canRetryFailed() const {
    return std::ranges::any_of(manager_.tasks(), retryable);
}
void DownloadsView::pauseAll() {
    for (const auto& task : manager_.tasks()) {
        if (pausable(task))
            manager_.pause(task.id);
    }
}

void DownloadsView::resumeAll() {
    for (const auto& task : manager_.tasks()) {
        if (native(task) && task.state == TaskState::Paused)
            manager_.resume(task.id);
    }
}

void DownloadsView::retryAllFailed() {
    for (const auto& task : manager_.tasks()) {
        if (retryable(task))
            manager_.retry(task.id);
    }
}

void DownloadsView::pauseSeries(const QString& slug) {
    for (const auto& task : tasksOf(slug)) {
        if (pausable(task))
            manager_.pause(task.id);
    }
}

void DownloadsView::resumeSeries(const QString& slug) {
    for (const auto& task : tasksOf(slug)) {
        if (native(task) && task.state == TaskState::Paused)
            manager_.resume(task.id);
    }
}

void DownloadsView::retryFailed(const QString& slug) {
    for (const auto& task : tasksOf(slug)) {
        if (retryable(task))
            manager_.retry(task.id);
    }
}

// The page shows the new order at once, so a dropped row lands where it was let go.
void DownloadsView::reorderNow() {
    rebuild_timer_.stop();
    rebuild();
}

void DownloadsView::prioritize(const QString& slug) {
    manager_.prioritize(slug);
    reorderNow();
}

void DownloadsView::moveSeries(int from, int to) {
    QStringList order;
    for (const auto& row : queue_.rows()) {
        order.append(row.slug);
    }
    if (from < 0 || to < 0 || from >= order.size() || to >= order.size() || from == to) {
        return;
    }
    order.move(from, to);
    // The series on view keeps its place in line.
    if (!current_.rows().isEmpty()) {
        order.insert(current_place_, current_.rows().first().slug);
    }
    manager_.reorder(order);
    reorderNow();
}

// Cancelled episodes are not history worth keeping: they leave the list at once.
void DownloadsView::forgetCancelled(const QString& slug) {
    for (const auto& task : tasksOf(slug)) {
        if (task.state == TaskState::Cancelled) {
            manager_.forget(task.id);
        }
    }
}

void DownloadsView::cancelSeries(const QString& slug) {
    manager_.cancelSeries(slug);
    forgetCancelled(slug);
}

void DownloadsView::forgetSeries(const QString& slug) {
    manager_.forgetSeries(slug);
}

void DownloadsView::trashSeries(const QString& slug) {
    QList<double> numbers;
    for (const auto& task : tasksOf(slug)) {
        if (task.state == TaskState::Completed) {
            numbers.append(task.number);
        }
    }
    const auto trashed = library_.trashFiles(slug, numbers);
    if (!trashed) {
        emit message(trashed.error().message, true);
        return;
    }
    if (trashed->failed > 0) {
        emit message(count(trashed->failed,
                           QStringLiteral("Un archivo sigue en el disco: está en uso."),
                           QStringLiteral("%1 archivos siguen en el disco: están en uso.")),
                     true);
        return;
    }
    manager_.forgetSeries(slug);
    emit message(count(trashed->count, QStringLiteral("1 episodio movido a la Papelera"),
                       QStringLiteral("%1 episodios movidos a la Papelera")) +
                     QStringLiteral(" · ") + display::bytes(trashed->bytes),
                 false);
}

void DownloadsView::clearCompleted() {
    QStringList slugs;
    for (const auto& row : completed_.rows()) {
        slugs.append(row.slug);
    }
    for (const auto& slug : slugs) {
        manager_.forgetSeries(slug);
    }
    for (const auto& task : manager_.tasks()) {
        if (task.state == TaskState::Cancelled) {
            manager_.forget(task.id);
        }
    }
}

void DownloadsView::openSeriesFolder(const QString& slug) {
    const auto folder = folderOf(tasksOf(slug));
    if (QFileInfo(folder).isDir()) {
        shell::open(folder);
    }
}

void DownloadsView::watchNext(const QString& slug) {
    const auto tasks = tasksOf(slug);
    if (tasks.isEmpty()) {
        return;
    }
    if (const auto number = nextToWatch(slug, tasks)) {
        playEpisode(slug, *number, tasks.first().animeTitle);
    }
}

void DownloadsView::pause(qint64 id) {
    manager_.pause(id);
}

void DownloadsView::resume(qint64 id) {
    manager_.resume(id);
}

void DownloadsView::retry(qint64 id) {
    if (const auto task = manager_.task(id); task && retryable(*task))
        manager_.retry(id);
}

void DownloadsView::cancel(qint64 id) {
    manager_.cancel(id);
    if (const auto task = manager_.task(id); task && task->state == TaskState::Cancelled) {
        manager_.forget(id);
    }
}

void DownloadsView::forget(qint64 id) {
    manager_.forget(id);
}

void DownloadsView::play(qint64 id) {
    if (const auto task = manager_.task(id)) {
        playEpisode(task->slug, task->number, task->animeTitle);
    }
}

void DownloadsView::playEpisode(const QString& slug, double number, const QString& title) {
    const auto episode = library_.repository().episode(slug, number);
    if (!episode || !*episode || (*episode)->filePath.isEmpty()) {
        emit message(QStringLiteral("El archivo ya no está en el disco."), true);
        return;
    }
    const auto& record = **episode;
    const auto started = player_.play(
        {.slug = slug,
         .number = number,
         .filePath = record.filePath,
         .title = title + QStringLiteral(" · ") + display::episode(number),
         .startMs = record.state == library::WatchState::Watching ? record.positionMs : 0});
    if (!started) {
        emit message(started.error(), true);
    }
}

void DownloadsView::showFile(qint64 id) {
    const auto task = manager_.task(id);
    if (task && QFileInfo::exists(task->targetPath)) {
        shell::reveal(task->targetPath);
    }
}

} // namespace animehub::app
