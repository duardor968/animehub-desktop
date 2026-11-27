#include "downloads_view.h"

#include "display.h"
#include "shell.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

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

// What DownloadManager pauses: anything unfinished that JDownloader does not have yet.
bool pausable(const Task& task) {
    return !task.handedOver() &&
           (task.state == TaskState::Queued || task.state == TaskState::Resolving ||
            task.state == TaskState::Downloading || task.state == TaskState::Waiting);
}

bool retryable(const Task& task) {
    return task.state == TaskState::Failed && !task.unconfirmed();
}

// Waiting to try again by itself: "Reintentar ahora" does not wait for its time.
bool retryableNow(const Task& task) {
    return task.state == TaskState::Waiting && !task.unconfirmed();
}

// Where a JDownloader download ends up, said with the engine: native ones go where they
// always do, so their row says nothing of it.
QString jdFolder(const Task& task) {
    return native(task) ? QString()
                        : QDir::toNativeSeparators(QFileInfo(task.targetPath).absolutePath());
}

QString engineLine(const QStringList& folders) {
    return folders.isEmpty()
               ? QString()
               : QStringLiteral("JDownloader · ") + folders.join(QStringLiteral(" · "));
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
    if (task.enginePhase == u"launching")
        return QStringLiteral("Abriendo JDownloader");
    return QStringLiteral("Descargando");
}

QString taskDetail(const Task& task) {
    using downloads::TaskIssue;
    if (native(task))
        return display::download(task);
    if (task.state == TaskState::Failed && task.unconfirmed())
        return QStringLiteral(
            "Envío sin confirmar · revisa el trabajo en JDownloader antes de volver a enviarlo");
    // What JDownloader never got says why, and what to do.
    if ((task.state == TaskState::Waiting || task.state == TaskState::Failed) &&
        task.issue == TaskIssue::Engine)
        return task.handedOver() || task.issueDetail.isEmpty()
                   ? QStringLiteral("Sin conexión con JDownloader · último estado: ") +
                         phaseName(task)
                   : task.issueDetail;
    // No source reads the same on both engines; JDownloader's own failures carry their reason,
    // and the network and the API keep theirs.
    if (task.state == TaskState::Failed && task.issue == TaskIssue::NoSource)
        return display::issue(task);
    if (task.state == TaskState::Failed && task.issue != TaskIssue::Network &&
        !task.issueDetail.isEmpty())
        return task.issueDetail;
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

QString shortTitle(const QString& title) {
    const auto trimmed = title.trimmed();
    constexpr qsizetype kMaxLen = 24;
    if (trimmed.size() <= kMaxLen) {
        return trimmed;
    }
    for (const QStringView sep :
         {QStringView(u": "), QStringView(u" - "), QStringView(u" – "), QStringView(u" — ")}) {
        if (const auto idx = trimmed.indexOf(sep); idx >= 3 && idx <= kMaxLen) {
            return trimmed.left(idx).trimmed();
        }
    }
    auto cut = trimmed.left(kMaxLen - 1);
    if (const auto space = cut.lastIndexOf(QLatin1Char(' ')); space >= 6) {
        cut.truncate(space);
    }
    return cut.trimmed() + QChar(0x2026);
}

QString shortEta(qint64 seconds) {
    if (seconds <= 0) {
        return {};
    }
    if (seconds < 60) {
        return QStringLiteral("%1 s").arg(seconds);
    }
    if (seconds < 3600) {
        return QStringLiteral("%1 min").arg((seconds + 59) / 60);
    }
    return QStringLiteral("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

bool movingTransfer(const Task& task) {
    return task.state == TaskState::Downloading &&
           (native(task) || task.enginePhase == u"transferring");
}

int percentInt(double fraction) {
    return std::clamp(static_cast<int>(std::lround(fraction * 100.0)), 0, 99);
}

double combinedProgress(const QList<const Task*>& tasks) {
    if (tasks.isEmpty()) {
        return 0.0;
    }
    if (tasks.size() == 1) {
        return fractionOf(*tasks.first());
    }
    bool all_totals = true;
    qint64 done_sum = 0;
    qint64 total_sum = 0;
    double fraction_sum = 0.0;
    for (const auto* task : tasks) {
        if (task->bytesTotal > 0) {
            done_sum += std::clamp<qint64>(bytesOf(*task), 0, task->bytesTotal);
            total_sum += task->bytesTotal;
        } else {
            all_totals = false;
        }
        fraction_sum += task->state == TaskState::Verifying ? 1.0 : fractionOf(*task);
    }
    if (all_totals && total_sum > 0) {
        return std::clamp(static_cast<double>(done_sum) / static_cast<double>(total_sum), 0.0, 1.0);
    }
    return std::clamp(fraction_sum / static_cast<double>(tasks.size()), 0.0, 1.0);
}

QString stateGroupOf(TaskState state) {
    switch (state) {
    case TaskState::Completed:
        return QStringLiteral("completed");
    case TaskState::Resolving:
    case TaskState::Downloading:
    case TaskState::Verifying:
        return QStringLiteral("running");
    case TaskState::Queued:
    case TaskState::Waiting:
        return QStringLiteral("queued");
    case TaskState::Paused:
        return QStringLiteral("paused");
    case TaskState::Failed:
        return QStringLiteral("failed");
    case TaskState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("queued");
}

QString stateTextOf(const Task& task, bool on_disk) {
    switch (task.state) {
    case TaskState::Completed:
        return on_disk ? QStringLiteral("Completado") : QStringLiteral("Archivo eliminado");
    case TaskState::Downloading:
        return movingTransfer(task) ? QStringLiteral("Descargando") : phaseName(task);
    case TaskState::Resolving:
        return task.enginePhase == u"launching" ? QStringLiteral("Abriendo JDownloader")
                                                : QStringLiteral("Buscando");
    case TaskState::Verifying:
        return QStringLiteral("Comprobando");
    case TaskState::Queued:
        return QStringLiteral("En cola");
    case TaskState::Waiting:
        return QStringLiteral("En espera");
    case TaskState::Paused:
        return QStringLiteral("En pausa");
    case TaskState::Failed:
        return QStringLiteral("Error");
    case TaskState::Cancelled:
        return QStringLiteral("Cancelada");
    }
    return {};
}

QString percentTextOf(const Task& task) {
    switch (task.state) {
    case TaskState::Completed:
        return QStringLiteral("100 %");
    case TaskState::Downloading:
        if (movingTransfer(task)) {
            return task.bytesTotal > 0 ? QStringLiteral("%1 %").arg(percentInt(fractionOf(task)))
                                       : QStringLiteral("—");
        }
        return phaseName(task);
    case TaskState::Verifying:
        return task.bytesTotal > 0 ? QStringLiteral("%1 %").arg(percentInt(fractionOf(task)))
                                   : QStringLiteral("Comprobando");
    case TaskState::Resolving:
        return QStringLiteral("Buscando");
    case TaskState::Queued:
        return QStringLiteral("En cola");
    case TaskState::Waiting:
        return QStringLiteral("En espera");
    case TaskState::Paused:
        return task.bytesTotal > 0 && task.bytesDone > 0
                   ? QStringLiteral("%1 %").arg(percentInt(fractionOf(task)))
                   : QStringLiteral("En pausa");
    case TaskState::Failed:
        return QStringLiteral("Error");
    case TaskState::Cancelled:
        return QStringLiteral("Cancelada");
    }
    return {};
}

QString sizeTextOf(const Task& task, bool on_disk, qint64 disk_size) {
    if (task.state == TaskState::Completed) {
        const qint64 sz = task.bytesTotal > 0 ? task.bytesTotal : disk_size;
        if (on_disk) {
            return sz > 0 ? display::bytes(sz) + QStringLiteral(" · En disco")
                          : QStringLiteral("En disco");
        }
        return sz > 0 ? display::bytes(sz) + QStringLiteral(" · Eliminado")
                      : QStringLiteral("Archivo eliminado");
    }
    if (task.bytesTotal > 0) {
        return display::bytes(bytesOf(task)) + QStringLiteral(" de ") +
               display::bytes(task.bytesTotal);
    }
    if (bytesOf(task) > 0) {
        return display::bytes(bytesOf(task));
    }
    return QStringLiteral("—");
}

QString etaTextOf(const Task& task) {
    if (task.state == TaskState::Downloading && task.bytesPerSecond > 0 &&
        task.bytesTotal > task.bytesDone) {
        return shortEta(
            std::max<qint64>(1, (task.bytesTotal - task.bytesDone) / task.bytesPerSecond));
    }
    return {};
}

QString providerTextOf(const Task& task) {
    if (task.provider) {
        return display::provider(*task.provider);
    }
    return native(task) ? QString() : QStringLiteral("JDownloader");
}

QString headerActiveSummaryOf(DownloadsView::Mode mode, const Task* subject,
                              const QList<const Task*>& hero_running, int hero_done, int hero_total,
                              int hero_failed, qint64 hero_speed, const QString& subject_detail) {
    if (!subject || mode == DownloadsView::Mode::Idle) {
        return {};
    }
    if (mode == DownloadsView::Mode::Active && hero_running.size() >= 2) {
        QStringList ep_tokens;
        const qsizetype shown_eps = hero_running.size() <= 3 ? hero_running.size() : 2;
        ep_tokens.reserve(shown_eps);
        for (qsizetype i = 0; i < shown_eps; ++i) {
            const auto* task = hero_running[i];
            if (movingTransfer(*task) && task->bytesTotal > 0) {
                ep_tokens.append(QStringLiteral("Ep. %1 (%2 %)")
                                     .arg(display::episodeNumber(task->number))
                                     .arg(percentInt(fractionOf(*task))));
            } else if (task->state == TaskState::Verifying) {
                ep_tokens.append(QStringLiteral("Ep. %1 (comprobando)")
                                     .arg(display::episodeNumber(task->number)));
            } else if (task->state == TaskState::Resolving) {
                ep_tokens.append(
                    QStringLiteral("Ep. %1 (buscando)").arg(display::episodeNumber(task->number)));
            } else {
                ep_tokens.append(display::shortEpisode(task->number));
            }
        }
        QString eps_part;
        if (hero_running.size() == 2) {
            eps_part = ep_tokens[0] + QStringLiteral(" y ") + ep_tokens[1];
        } else if (hero_running.size() == 3) {
            eps_part = ep_tokens[0] + QStringLiteral(", ") + ep_tokens[1] + QStringLiteral(" y ") +
                       ep_tokens[2];
        } else {
            eps_part = ep_tokens[0] + QStringLiteral(", ") + ep_tokens[1] +
                       QStringLiteral(" y %1 más").arg(hero_running.size() - 2);
        }
        QStringList parts{eps_part};
        if (hero_speed > 0) {
            parts.append(display::speed(hero_speed));
        }
        parts.append(QStringLiteral("%1 de %2 completados").arg(hero_done).arg(hero_total));
        return parts.join(QStringLiteral(" · "));
    }
    if (mode == DownloadsView::Mode::Active && hero_running.size() == 1) {
        const auto* task = hero_running.first();
        QStringList parts{display::shortEpisode(task->number)};
        if (movingTransfer(*task)) {
            if (task->bytesTotal > 0) {
                parts.append(QStringLiteral("%1 %").arg(percentInt(fractionOf(*task))));
            }
            if (task->bytesPerSecond > 0) {
                parts.append(display::speed(task->bytesPerSecond));
            }
            if (task->bytesTotal > 0) {
                parts.append(display::bytes(task->bytesDone) + QStringLiteral(" de ") +
                             display::bytes(task->bytesTotal));
            }
            if (const auto eta = etaTextOf(*task); !eta.isEmpty()) {
                parts.append(QStringLiteral("quedan ") + eta);
            }
            if (parts.size() == 1) {
                parts.append(QStringLiteral("Descargando…"));
            }
        } else if (task->state == TaskState::Verifying) {
            parts.append(
                task->bytesTotal > 0
                    ? QStringLiteral("Comprobando · %1 %").arg(percentInt(fractionOf(*task)))
                    : QStringLiteral("Comprobando el archivo"));
        } else if (task->state == TaskState::Resolving) {
            parts.append(task->enginePhase == u"launching"
                             ? QStringLiteral("Abriendo JDownloader…")
                             : QStringLiteral("Buscando el archivo…"));
        } else {
            parts.append(phaseName(*task));
            if (task->bytesTotal > 0) {
                parts.append(display::bytes(task->bytesDone) + QStringLiteral(" de ") +
                             display::bytes(task->bytesTotal));
            }
        }
        return parts.join(QStringLiteral(" · "));
    }
    if (mode == DownloadsView::Mode::Waiting) {
        return subject_detail.isEmpty() ? display::shortEpisode(subject->number)
                                        : display::shortEpisode(subject->number) +
                                              QStringLiteral(" · ") + subject_detail;
    }
    if (mode == DownloadsView::Mode::Paused) {
        if (hero_done > 0) {
            return QStringLiteral("En pausa · %1 de %2 completados").arg(hero_done).arg(hero_total);
        }
        if (hero_total == 1) {
            return display::shortEpisode(subject->number) + QStringLiteral(" · En pausa");
        }
        return QStringLiteral("En pausa · ") + count(hero_total - hero_done,
                                                     QStringLiteral("1 episodio pendiente"),
                                                     QStringLiteral("%1 episodios pendientes"));
    }
    if (hero_failed == 1) {
        return display::shortEpisode(subject->number) + QStringLiteral(" · ") +
               taskDetail(*subject);
    }
    if (hero_done > 0) {
        return QStringLiteral("%1 con error · %2 de %3 completados")
            .arg(hero_failed)
            .arg(hero_done)
            .arg(hero_total);
    }
    return QStringLiteral("%1 con error").arg(hero_failed);
}

// Why an episode does not go as it should while MEGA's quota lasts, and what it does meanwhile.
QString noticeFor(const Task& task,
                  const std::optional<downloads::DownloadManager::MegaQuota>& quota) {
    using downloads::TaskIssue;
    if (!quota || !task.active()) {
        return {};
    }
    QStringList meanwhile;
    if (task.jdUnreachable) {
        meanwhile.append(QStringLiteral("JDownloader no responde"));
    }
    if (task.state == TaskState::Waiting && task.issue == TaskIssue::Quota) {
        if (!quota->known) {
            meanwhile.append(QStringLiteral("se reintenta a las ") + display::time(task.retryAt));
        }
    } else if (task.detour != TaskIssue::Quota) {
        return {};
    } else if (!native(task)) {
        meanwhile.append(QStringLiteral("enviado a JDownloader"));
    } else if (task.provider && *task.provider != api::Provider::Mega) {
        meanwhile.append(QStringLiteral("sigue con ") + display::provider(*task.provider));
    }
    return display::megaQuota(quota->until, quota->known, meanwhile.join(QStringLiteral(" · ")));
}

// What the top of the page says under its progress: where the episode is, without the speed,
// which the graph's "Actual" gives for every download at once.
QString headerDetailOf(const Task& task) {
    // MEGA's quota and its time are the notice above: one time for one wait.
    if (task.state == TaskState::Waiting && native(task)) {
        return task.issue == downloads::TaskIssue::Quota ? QStringLiteral("En espera")
                                                         : display::retry(task.retryAt);
    }
    if (!native(task) || task.state != TaskState::Downloading) {
        return taskDetail(task);
    }
    QStringList parts;
    if (task.bytesTotal > 0) {
        parts.append(display::bytes(task.bytesDone) + QStringLiteral(" de ") +
                     display::bytes(task.bytesTotal));
    }
    if (task.bytesPerSecond > 0 && task.bytesTotal > task.bytesDone) {
        parts.append(QStringLiteral("quedan ") +
                     display::remaining((task.bytesTotal - task.bytesDone) / task.bytesPerSecond));
    }
    return parts.isEmpty() ? display::download(task) : parts.join(QStringLiteral(" · "));
}

// Where the series' episodes end up, whoever downloads them: JDownloader's own folder goes once
// its videos are moved there.
QString folderOf(const QList<Task>& tasks) {
    for (const auto& task : tasks) {
        const auto folder = QFileInfo(task.targetPath).absolutePath();
        if (QFileInfo(folder).isDir()) {
            return folder;
        }
    }
    return {};
}

} // namespace

DownloadsView::DownloadsView(downloads::DownloadManager& manager, library::LibraryService& library,
                             playback::Player& player, AppSettings& settings, QObject* parent)
    : QObject(parent), manager_(manager), library_(library), player_(player), settings_(settings) {
    rebuild_timer_.setSingleShot(true);
    rebuild_timer_.setInterval(60);
    connect(&rebuild_timer_, &QTimer::timeout, this, &DownloadsView::rebuild);
    const auto full_later = [this] {
        invalidateDiskCache();
        completed_dirty_ = true;
        rebuild_timer_.start();
    };
    connect(&manager_, &downloads::DownloadManager::loaded, this, full_later);
    connect(&manager_, &downloads::DownloadManager::taskAdded, this, full_later);
    connect(&manager_, &downloads::DownloadManager::taskRemoved, this,
            [this, full_later](qint64 id) {
                seen_.remove(id);
                full_later();
            });
    connect(&manager_, &downloads::DownloadManager::taskChanged, this, &DownloadsView::taskChanged);
    connect(&manager_, &downloads::DownloadManager::taskCompleted, this, full_later);
    connect(&manager_, &downloads::DownloadManager::megaQuotaChanged, this, full_later);
    connect(&manager_, &downloads::DownloadManager::taskFailed, this, [this] {
        invalidateDiskCache();
        completed_dirty_ = true;
        considerOffer();
    });
    connect(&library_, &library::LibraryService::animeChanged, this,
            [this, full_later](const QString& slug) {
                infos_.remove(slug);
                full_later();
            });
    connect(&library_, &library::LibraryService::listChanged, this, [this, full_later] {
        infos_.clear();
        full_later();
    });

    samples_.fill(0.0, kSamples);
    native_samples_.fill(0.0, kSamples);
    sample_timer_.setInterval(kSampleInterval);
    connect(&sample_timer_, &QTimer::timeout, this, &DownloadsView::sample);
    rebuild();
}

bool DownloadsView::fileExists(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    if (const auto it = file_exists_cache_.constFind(path); it != file_exists_cache_.cend()) {
        return *it;
    }
    const bool exists = QFileInfo(path).isFile();
    file_exists_cache_.insert(path, exists);
    return exists;
}

bool DownloadsView::dirExists(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    if (const auto it = dir_exists_cache_.constFind(path); it != dir_exists_cache_.cend()) {
        return *it;
    }
    const bool exists = QFileInfo(path).isDir();
    dir_exists_cache_.insert(path, exists);
    return exists;
}

bool DownloadsView::hasExistingFolder(const QList<Task>& tasks) {
    for (const auto& task : tasks) {
        if (dirExists(QFileInfo(task.targetPath).absolutePath())) {
            return true;
        }
    }
    return false;
}

void DownloadsView::invalidateDiskCache() {
    file_exists_cache_.clear();
    dir_exists_cache_.clear();
}

bool DownloadsView::computeJdActive() const {
    const qint64 latest_native =
        native_samples_.isEmpty() ? 0 : static_cast<qint64>(native_samples_.last());
    return jd_downloading_ || shown_speed_ > latest_native;
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
        info.backdrop = anime.backdropUrl != anime.posterUrl ? anime.backdropUrl : QUrl();
        info.poster = anime.posterUrl;
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
    const auto quota = manager_.megaQuota();
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

    const auto prev_bar_speed = barSpeedText();
    const bool rebuild_completed = completed_dirty_;
    completed_dirty_ = false;
    QList<DownloadSeriesRow> queue;
    QList<DownloadSeriesRow> completed;
    QHash<QString, QString> amounts; // "3 de 12 descargados · 1,1 GB de 4,2 GB", for the header
    QHash<QString, int> unfinished_counts;
    for (auto& group : groups) {
        if (!group.inLine && !rebuild_completed) {
            continue;
        }
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
        qint64 series_speed = 0;
        QList<double> partial_numbers; // episodes cancelling would delete bytes of
        QStringList jd_folders;        // where JDownloader puts the episodes it downloads
        bool totals_known = true;
        double fraction = 0;
        QDateTime retry_at;
        int files = 0;
        qint64 file_bytes = 0;
        QDateTime finished_at; // the last episode that finished
        DownloadSeriesRow row{.slug = group.slug,
                              .title = group.title,
                              .poster = series.poster,
                              .backdrop = series.backdrop.toString()};

        for (const auto& task : group.tasks) {
            const auto episode = series.episodes.constFind(task.number);
            const bool on_disk = task.state == TaskState::Completed &&
                                 episode != series.episodes.cend() && fileExists(episode->filePath);
            if (!native(task) && task.issue == downloads::TaskIssue::Engine &&
                (task.state == TaskState::Waiting || task.state == TaskState::Failed))
                ++disconnected;
            switch (task.state) {
            case TaskState::Completed:
                ++done;
                done_bytes += bytesOf(task);
                if (!finished_at.isValid() || task.finishedAt > finished_at) {
                    finished_at = task.finishedAt;
                }
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
                if (task.state == TaskState::Downloading) {
                    series_speed += task.bytesPerSecond;
                }
                break;
            }
            if (inLine(task.state)) {
                ++row.cancelCount;
            }
            if (row.notice.isEmpty()) {
                row.notice = noticeFor(task, quota);
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
            const auto folder = jdFolder(task);
            if (!folder.isEmpty() && !jd_folders.contains(folder)) {
                jd_folders.append(folder);
            }
            row.canPause = row.canPause || pausable(task);
            row.canResume = row.canResume || task.state == TaskState::Paused;
            row.canRetry = row.canRetry || retryable(task);
            row.canRetryNow = row.canRetryNow || retryableNow(task);
            row.episodes.append(
                {.id = task.id,
                 .number = task.number,
                 .episode = display::episodeNumber(task.number),
                 .label = display::episode(task.number),
                 .title = episode != series.episodes.cend() ? episode->title : QString(),
                 .image = episode != series.episodes.cend() ? episode->imageUrl : QUrl(),
                 .state = downloads::stateKey(task.state),
                 .stateGroup = stateGroupOf(task.state),
                 .stateText = stateTextOf(task, on_disk),
                 .progress = fractionOf(task),
                 .percentText = percentTextOf(task),
                 .speedText = task.state == TaskState::Downloading && task.bytesPerSecond > 0
                                  ? display::speed(task.bytesPerSecond)
                                  : QStringLiteral("—"),
                 .sizeText = sizeTextOf(task, on_disk,
                                        episode != series.episodes.cend() ? episode->fileSize : 0),
                 .etaText = etaTextOf(task),
                 .provider = providerTextOf(task),
                 .filePath = on_disk ? episode->filePath : QString(),
                 .detail = task.state == TaskState::Completed && !on_disk
                               ? QStringLiteral("Archivo eliminado")
                               : taskDetail(task),
                 .problem = task.state == TaskState::Failed,
                 .hasFile = on_disk,
                 .canPause = pausable(task),
                 .canResume = task.state == TaskState::Paused,
                 .canRetry = retryable(task),
                 .canRetryNow = retryableNow(task),
                 .canCancel = inLine(task.state) && task.state != TaskState::Failed,
                 .partial = native(task) && task.state != TaskState::Completed && bytesOf(task) > 0
                                ? display::bytes(bytesOf(task))
                                : QString()});
        }
        row.engineLine = engineLine(jd_folders);
        row.running = active > 0;
        row.activeCount = active;
        row.runningSpeedText = series_speed > 0 ? display::speed(series_speed) : QString();
        row.goneCount = done - files;
        row.filesGone = !group.inLine && done > 0 && files == 0;

        const auto episodes = group.tasks.size();
        QStringList summary;
        if (row.filesGone) {
            summary.append(QStringLiteral("Archivos eliminados"));
        }
        if (group.inLine && active == 0) {
            if (disconnected > 0) {
                summary.append(QStringLiteral("JDownloader sin conexión"));
            } else if (paused > 0 && queued + waiting == 0) {
                summary.append(QStringLiteral("En pausa"));
            } else if (waiting > 0 && queued == 0 && row.notice.isEmpty()) {
                summary.append(display::retry(retry_at));
            } else if (queued > 0) {
                summary.append(QStringLiteral("En cola"));
            }
        }
        // A finished series says how many episodes it brought and when, as Steam's completed list.
        summary.append(group.inLine ? QStringLiteral("%1 de %2").arg(done).arg(episodes)
                                    : count(done, QStringLiteral("1 episodio"),
                                            QStringLiteral("%1 episodios")));
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
        if (!group.inLine && finished_at.isValid()) {
            summary.append(display::date(finished_at));
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
        // Without its files there is nothing to watch.
        if (const auto next = row.filesGone ? std::nullopt : nextToWatch(group.slug, group.tasks)) {
            const auto episode = series.episodes.value(*next);
            row.watchText = episode.state == library::WatchState::Watching && episode.positionMs > 0
                                ? QStringLiteral("Continuar · Ep. ") + display::episodeNumber(*next)
                                : QStringLiteral("Ver ep. ") + display::episodeNumber(*next);
        }
        row.hasFolder = files > 0 && hasExistingFolder(group.tasks);
        if (group.inLine) {
            unfinished_counts.insert(group.slug, row.cancelCount);
        }
        (group.inLine ? queue : completed).append(std::move(row));
    }
    if (!queue.isEmpty()) {
        queue.first().first = true;
    }
    line_order_.clear();
    for (const auto& row : queue) {
        line_order_.append(row.slug);
    }

    // What the top of the page and the bar show: something running, else what will start by
    // itself, else what waits for the user, else the failures.
    int queued = 0;
    int waiting = 0;
    int paused = 0;
    int failed = 0;
    int pending_count = 0;
    qint64 speed = 0;
    qint64 native_speed = 0;
    bool jd_downloading = false;
    bool can_pause = false;
    bool can_retry = false;
    QString notice; // the first episode's held back by MEGA's quota, for the bar
    QList<const Task*> running_tasks;
    const Task* shown = nullptr;
    const Task* next = nullptr;
    const Task* held = nullptr;
    const Task* broken = nullptr;
    for (const auto& task : tasks) {
        if (inLine(task.state)) {
            ++pending_count;
        }
        can_pause = can_pause || pausable(task);
        can_retry = can_retry || retryable(task);
        if (notice.isEmpty()) {
            notice = noticeFor(task, quota);
        }
        if (running(task.state)) {
            running_tasks.append(&task);
            if (!shown) {
                shown = &task;
            }
            if (task.state == TaskState::Downloading) {
                speed += task.bytesPerSecond;
                if (native(task)) {
                    native_speed += task.bytesPerSecond;
                } else {
                    jd_downloading = true;
                }
            }
        } else if (task.state == TaskState::Queued || task.state == TaskState::Waiting) {
            if (task.state == TaskState::Queued) {
                ++queued;
            } else {
                ++waiting;
            }
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
        const auto mode = shown  ? Mode::Active
                          : next ? Mode::Waiting
                          : held ? Mode::Paused
                                 : Mode::Failed;
        const auto detail = headerDetailOf(*subject);
        QList<const Task*> hero_running;
        int hero_done = 0;
        int hero_failed = 0;
        int hero_total = 0;
        qint64 hero_speed = 0;
        if (const auto place = places.constFind(subject->slug); place != places.cend()) {
            const auto& hero_group = groups[*place];
            hero_total = static_cast<int>(hero_group.tasks.size());
            for (const auto& task : hero_group.tasks) {
                if (task.state == TaskState::Completed) {
                    ++hero_done;
                } else if (task.state == TaskState::Failed) {
                    ++hero_failed;
                } else if (running(task.state)) {
                    hero_running.append(&task);
                    if (task.state == TaskState::Downloading) {
                        hero_speed += task.bytesPerSecond;
                    }
                }
            }
        }
        const auto& series_info = info(subject->slug);
        header = {
            .mode = mode,
            .slug = subject->slug,
            .title = subject->animeTitle,
            // The series' line, then the episode's under the bar: each figure named.
            .episode = amounts.value(subject->slug),
            .detail = detail.isEmpty()
                          ? display::episode(subject->number)
                          : display::episode(subject->number) + QStringLiteral(" · ") + detail,
            .activeSummary = headerActiveSummaryOf(mode, subject, hero_running, hero_done,
                                                   hero_total, hero_failed, hero_speed, detail),
            // Only its own reason: another series' would read as this episode's state. The
            // bar and that series' row say theirs.
            .notice = noticeFor(*subject, quota),
            .progress =
                !hero_running.isEmpty() ? combinedProgress(hero_running) : fractionOf(*subject),
            .art = series_info.art,
            .backdrop = series_info.backdrop.toString(),
            .poster = series_info.poster,
            .singleEpisode = unfinished_counts.value(subject->slug) == 1,
            .taskCount = hero_total};
    } else {
        header.title = QStringLiteral("Nada descargando");
    }

    // The series on view leaves the line under it, and so do the others transferring: what
    // runs is not "next".
    QList<DownloadSeriesRow> current;
    QList<DownloadSeriesRow> active;
    for (auto it = queue.begin(); it != queue.end();) {
        if (subject && it->slug == subject->slug) {
            current.append(std::move(*it));
        } else if (it->running) {
            active.append(std::move(*it));
        } else {
            ++it;
            continue;
        }
        it = queue.erase(it);
    }
    const int active_series_count = (shown ? 1 : 0) + static_cast<int>(active.size());
    const int active_task_count = static_cast<int>(running_tasks.size());
    header_tasks_.setRows(current.isEmpty() ? QList<DownloadEpisodeRow>{}
                                            : current.first().episodes);
    queue_.setRows(std::move(queue));
    if (rebuild_completed) {
        completed_.setRows(std::move(completed));
    }
    current_.setRows(std::move(current));
    active_.setRows(std::move(active));

    if (shown) {
        const double progress = combinedProgress(running_tasks);
        const bool any_moving_with_total = std::ranges::any_of(running_tasks, [](const Task* task) {
            return movingTransfer(*task) && task->bytesTotal > 0;
        });
        QString text;
        if (active_series_count >= 2) {
            text =
                QStringLiteral("Descargando %1 animes · %2 eps activos · %3 ep. %4")
                    .arg(active_series_count)
                    .arg(active_task_count)
                    .arg(shortTitle(subject->animeTitle), display::episodeNumber(subject->number));
        } else if (active_task_count >= 2) {
            text = QStringLiteral("Descargando %1 · %2 eps activos · Ep. %3")
                       .arg(subject->animeTitle)
                       .arg(active_task_count)
                       .arg(display::episodeNumber(subject->number));
        } else {
            text = QStringLiteral("Descargando %1 · Ep. %2")
                       .arg(subject->animeTitle, display::episodeNumber(subject->number));
        }
        // A percentage only while bytes move and a total size is known; 100 % stays for completed.
        bar = {.mode = Mode::Active,
               .text = text,
               .progress = progress,
               .percent = any_moving_with_total ? QStringLiteral("%1 %").arg(percentInt(progress))
                                                : QString(),
               .poster = info(subject->slug).poster};
    } else if (paused > 0 || queued > 0) {
        bar = {.mode = (queued > 0 || waiting > 0) ? Mode::Waiting : Mode::Paused,
               .text = QStringLiteral("En pausa · ") +
                       count(pending_count, QStringLiteral("1 episodio pendiente"),
                             QStringLiteral("%1 episodios pendientes")),
               .poster = subject ? info(subject->slug).poster : QUrl()};
    } else if (next) {
        const auto ep = display::episodeNumber(next->number);
        QString text;
        if (native(*next)) {
            text = next->issue == downloads::TaskIssue::Quota
                       ? next->animeTitle + QStringLiteral(" · Ep. ") + ep +
                             QStringLiteral(" · En espera")
                       : next->animeTitle + QStringLiteral(" · Ep. ") + ep + QStringLiteral(" · ") +
                             display::retry(next->retryAt);
        } else {
            text = QStringLiteral("En espera · ") +
                   count(pending_count, QStringLiteral("1 episodio pendiente"),
                         QStringLiteral("%1 episodios pendientes"));
        }
        bar = {.mode = Mode::Waiting, .text = text, .poster = info(next->slug).poster};
    } else if (failed > 0) {
        bar = {.mode = Mode::Failed,
               .text = count(failed, QStringLiteral("1 descarga con error"),
                             QStringLiteral("%1 descargas con error"))};
    }
    bar.activeSeriesCount = active_series_count;
    bar.activeTaskCount = active_task_count;
    bar.pendingTaskCount = pending_count;
    if (bar.mode != Mode::Failed && failed > 0) {
        bar.problem = QStringLiteral("%1 con error").arg(failed);
    }
    bar.notice = notice;

    jd_downloading_ = jd_downloading;
    const bool jd_active = computeJdActive();
    const bool changed = header != header_ || bar != bar_ || can_pause_all_ != can_pause ||
                         can_resume_all_ != (paused > 0) || can_retry_failed_ != can_retry ||
                         jd_active_ != jd_active;
    header_ = std::move(header);
    bar_ = std::move(bar);
    can_pause_all_ = can_pause;
    can_resume_all_ = paused > 0;
    can_retry_failed_ = can_retry;
    jd_active_ = jd_active;
    speed_ = speed;
    native_speed_ = native_speed;
    if (changed) {
        emit summaryChanged();
    }

    bool sampled = false;
    if (speed_ > 0 && !sample_timer_.isActive()) {
        sample_timer_.start();
        sample(); // the graph and "Actual" start together
        sampled = true;
    }
    if (!sampled && barSpeedText() != prev_bar_speed) {
        emit speedChanged();
    }
    updateOffer();
}

// Counts the bytes the session downloads: what a transfer adds between two looks at it, and
// the rest of the file when it ends. Decrypting a MEGA file is not downloading.
void DownloadsView::taskChanged(qint64 id) {
    rebuild_timer_.start();
    const auto task = manager_.task(id);
    if (!task) {
        invalidateDiskCache();
        completed_dirty_ = true;
        return;
    }
    const auto last = seen_.constFind(id);
    const bool state_changed = (last == seen_.cend() || last->state != task->state);
    if (state_changed) {
        invalidateDiskCache();
        completed_dirty_ = true;
    } else if (task->state != TaskState::Downloading) {
        completed_dirty_ = true;
    }
    if (last != seen_.cend() && last->state == TaskState::Downloading) {
        qint64 now = -1;
        if (task->state == TaskState::Downloading || task->state == TaskState::Completed) {
            now = task->bytesDone;
        } else if (task->state == TaskState::Verifying) {
            now = task->bytesTotal;
        }
        if (now > last->bytes) {
            session_bytes_ += now - last->bytes;
            emit totalChanged();
        }
    }
    seen_.insert(id, {.state = task->state, .bytes = task->bytesDone});
}

// The graph's newest bar, "Actual" and "Máximo" are one reading of the total speed.
void DownloadsView::sample() {
    recordSpeedSample(speed_, native_speed_);
    if (speed_ == 0 && std::ranges::all_of(samples_, [](qreal value) { return value == 0; })) {
        sample_timer_.stop();
    }
}

void DownloadsView::recordSpeedSample(qint64 total, qint64 native) {
    samples_.removeFirst();
    samples_.append(static_cast<qreal>(total));
    native_samples_.removeFirst();
    native_samples_.append(static_cast<qreal>(native));
    shown_speed_ = total;
    peak_ = std::max(peak_, total);
    emit speedChanged();
    const bool jd_active = computeJdActive();
    if (jd_active_ != jd_active) {
        jd_active_ = jd_active;
        emit summaryChanged();
    }
}

QString DownloadsView::barState() const {
    switch (bar_.mode) {
    case Mode::Idle:
        return QStringLiteral("idle");
    case Mode::Active:
        return QStringLiteral("active");
    case Mode::Waiting:
    case Mode::Paused:
        return QStringLiteral("paused");
    case Mode::Failed:
        return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString DownloadsView::barSpeedText() const {
    if (bar_.mode != Mode::Active) {
        return {};
    }
    const qint64 effective = shown_speed_ > 0 ? shown_speed_ : speed_;
    return effective > 0 ? display::speed(effective) : QString();
}

QString DownloadsView::speedText() const {
    return display::speed(shown_speed_);
}

QString DownloadsView::totalSpeedText() const {
    return speedText();
}

QString DownloadsView::nativeSpeedText() const {
    const qint64 latest =
        native_samples_.isEmpty() ? 0 : static_cast<qint64>(native_samples_.last());
    return display::speed(std::max<qint64>(0, latest));
}

QString DownloadsView::jdSpeedText() const {
    const qint64 latest =
        native_samples_.isEmpty() ? 0 : static_cast<qint64>(native_samples_.last());
    return display::speed(std::max<qint64>(0, shown_speed_ - latest));
}

QString DownloadsView::peakText() const {
    return display::speed(peak_);
}

QString DownloadsView::totalText() const {
    return display::bytes(session_bytes_);
}

QString DownloadsView::offer() const {
    switch (offer_) {
    case Offer::JDownloader:
        return QStringLiteral("jdownloader");
    case Offer::Servers:
        return QStringLiteral("servers");
    case Offer::None:
        break;
    }
    return {};
}

// JDownloader is offered for the servers only it downloads while it is not used; turning on, for
// the servers the user turned off.
std::pair<DownloadsView::Offer, QList<api::Provider>>
DownloadsView::offerFor(const Task& task) const {
    if (task.state != TaskState::Failed || task.issue != downloads::TaskIssue::NoSource) {
        return {Offer::None, {}};
    }
    const bool jd = settings_.jdownloader() != AppSettings::JDownloader::Off;
    const auto disabled = settings_.disabledProviders();
    QList<api::Provider> off;
    QList<api::Provider> jd_only;
    for (const auto server : task.excludedServers) {
        if (!jd && !downloads::DownloadManager::isNative(server)) {
            jd_only.append(server);
        } else if (disabled.contains(api::providerKey(server))) {
            off.append(server);
        }
    }
    if (!off.isEmpty()) {
        return {Offer::Servers, off};
    }
    if (!jd_only.isEmpty()) {
        return {Offer::JDownloader, jd_only};
    }
    return {Offer::None, {}};
}

DownloadsView::OfferMatch DownloadsView::offerMatch(Offer kind) const {
    OfferMatch match;
    for (const auto& task : manager_.tasks()) {
        const auto [offer, servers] = offerFor(task);
        if (offer != kind) {
            continue;
        }
        match.tasks.append(task);
        for (const auto server : servers) {
            if (!match.servers.contains(server)) {
                match.servers.append(server);
            }
        }
    }
    return match;
}

bool DownloadsView::offerHidden(Offer kind) const {
    return kind == Offer::JDownloader ? settings_.jdownloaderOfferHidden()
                                      : settings_.serversOfferHidden();
}

void DownloadsView::considerOffer() {
    if (offer_ != Offer::None || !window_shown_) {
        return;
    }
    for (const auto kind : {Offer::JDownloader, Offer::Servers}) {
        if (offered_.contains(kind) || offerHidden(kind)) {
            continue;
        }
        if (const auto match = offerMatch(kind); !match.tasks.isEmpty()) {
            offered_.append(kind);
            presentOffer(kind, match);
            return;
        }
    }
}

// An open offer follows the episodes it is about, and closes once none is left.
void DownloadsView::updateOffer() {
    if (offer_ == Offer::None || offer_preview_) {
        return;
    }
    const auto match = offerMatch(offer_);
    if (match.tasks.isEmpty()) {
        closeOffer(false);
        return;
    }
    presentOffer(offer_, match);
}

void DownloadsView::presentOffer(Offer kind, const OfferMatch& match) {
    QStringList slugs;
    for (const auto& task : match.tasks) {
        if (!slugs.contains(task.slug)) {
            slugs.append(task.slug);
        }
    }
    const auto episodes = match.tasks.size();
    const auto& first = match.tasks.first();
    const auto title = slugs.size() > 1
                           ? QStringLiteral("%1 episodios").arg(episodes)
                           : first.animeTitle + QStringLiteral(" · ") +
                                 (episodes == 1 ? display::episode(first.number)
                                                : QStringLiteral("%1 episodios").arg(episodes));
    QStringList names;
    for (const auto server : match.servers) {
        names.append(display::provider(server));
    }
    names.sort(Qt::CaseInsensitive);
    const auto last = names.takeLast();
    const auto listed =
        names.isEmpty() ? last : names.join(QStringLiteral(", ")) + QStringLiteral(" y ") + last;
    const bool one = match.servers.size() == 1;
    auto text = (episodes == 1 ? QStringLiteral("Este episodio solo está en ")
                               : QStringLiteral("Estos episodios solo están en ")) +
                listed;
    if (kind == Offer::Servers) {
        text += one ? QStringLiteral(", desactivado en Ajustes.")
                    : QStringLiteral(", desactivados en Ajustes.");
    } else {
        text += one ? QStringLiteral(", que se descarga con JDownloader.")
                    : QStringLiteral(", que se descargan con JDownloader.");
    }
    if (kind == offer_ && title == offer_title_ && text == offer_text_) {
        return;
    }
    offer_ = kind;
    offer_title_ = title;
    offer_text_ = text;
    emit offerChanged();
}

void DownloadsView::closeOffer(bool never) {
    if (offer_ == Offer::None) {
        return;
    }
    if (never && offer_ == Offer::JDownloader) {
        settings_.hideJdownloaderOffer();
    } else if (never) {
        settings_.hideServersOffer();
    }
    offer_ = Offer::None;
    offer_preview_ = false;
    offer_title_.clear();
    offer_text_.clear();
    emit offerChanged();
}

void DownloadsView::setWindowShown(bool shown) {
    if (window_shown_ == shown) {
        return;
    }
    window_shown_ = shown;
    if (shown) {
        invalidateDiskCache();
        completed_dirty_ = true;
        rebuild_timer_.start();
    }
    emit windowShownChanged();
    considerOffer(); // what failed while it was hidden, if it still applies
}

void DownloadsView::dismissOffer(bool never) {
    closeOffer(never);
    considerOffer();
}

void DownloadsView::acceptOffer(bool never) {
    const auto kind = offer_;
    const auto match = offer_preview_ ? OfferMatch{} : offerMatch(kind);
    closeOffer(never);
    if (kind != Offer::Servers) {
        return;
    }
    for (const auto server : match.servers) {
        settings_.setProviderEnabled(api::providerKey(server), true);
    }
    for (const auto& task : match.tasks) {
        manager_.retry(task.id);
    }
}

void DownloadsView::retryWithJdownloader() {
    int retried = 0;
    for (const auto& task : manager_.tasks()) {
        if (task.state == TaskState::Failed && task.issue == downloads::TaskIssue::NoSource &&
            std::ranges::any_of(task.excludedServers, [](api::Provider server) {
                return !downloads::DownloadManager::isNative(server);
            })) {
            manager_.retry(task.id);
            ++retried;
        }
    }
    emit message(retried == 0 ? QStringLiteral("JDownloader conectado")
                              : QStringLiteral("JDownloader conectado · ") +
                                    count(retried, QStringLiteral("1 episodio en cola"),
                                          QStringLiteral("%1 episodios en cola")),
                 false);
}

void DownloadsView::previewOffer(Offer kind, const QString& title, double number,
                                 const QList<api::Provider>& servers) {
    Task task;
    task.animeTitle = title;
    task.number = number;
    presentOffer(kind, {.tasks = {task}, .servers = servers});
    offer_preview_ = true;
}

QList<Task> DownloadsView::tasksOf(const QString& slug) const {
    auto tasks = manager_.tasks();
    tasks.removeIf([&slug](const Task& task) { return task.slug != slug; });
    return tasks;
}

void DownloadsView::pauseAll() {
    manager_.pauseAll();
}

void DownloadsView::resumeAll() {
    manager_.resumeAll();
}

void DownloadsView::retryAllFailed() {
    QStringList slugs;
    for (const auto& task : manager_.tasks()) {
        if (task.state == TaskState::Failed && !slugs.contains(task.slug)) {
            slugs.append(task.slug);
        }
    }
    for (const auto& slug : slugs) {
        manager_.retryFailed(slug);
    }
}

void DownloadsView::pauseSeries(const QString& slug) {
    manager_.pauseSeries(slug);
}

void DownloadsView::resumeSeries(const QString& slug) {
    manager_.resumeSeries(slug);
}

void DownloadsView::retryFailed(const QString& slug) {
    manager_.retryFailed(slug);
}

void DownloadsView::retryWaiting(const QString& slug) {
    manager_.retryWaiting(slug);
}

void DownloadsView::retryMegaQuota() {
    manager_.retryMegaQuota();
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
    QStringList shown;
    for (const auto& row : queue_.rows()) {
        shown.append(row.slug);
    }
    if (from < 0 || to < 0 || from >= shown.size() || to >= shown.size() || from == to) {
        return;
    }
    shown.move(from, to);
    // The series on view and the ones transferring keep their places in line.
    auto order = line_order_;
    qsizetype next = 0;
    for (auto& slug : order) {
        if (shown.contains(slug)) {
            slug = shown[next++];
        }
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
    invalidateDiskCache();
    completed_dirty_ = true;
    manager_.forgetSeries(slug);
    emit message(count(trashed->count, QStringLiteral("1 episodio movido a la Papelera"),
                       QStringLiteral("%1 episodios movidos a la Papelera")) +
                     QStringLiteral(" · ") + display::bytes(trashed->bytes),
                 false);
}

void DownloadsView::downloadAgain(const QString& slug) {
    invalidateDiskCache();
    completed_dirty_ = true;
    const auto& series = info(slug);
    QList<double> numbers;
    QString title;
    for (const auto& task : tasksOf(slug)) {
        const auto episode = series.episodes.constFind(task.number);
        const bool on_disk = episode != series.episodes.cend() && !episode->filePath.isEmpty() &&
                             QFileInfo(episode->filePath).isFile();
        if (task.state == TaskState::Completed && !on_disk) {
            numbers.append(task.number);
            title = task.animeTitle;
            manager_.forget(task.id); // its new download takes the row
        }
    }
    if (numbers.isEmpty()) {
        return;
    }
    const auto queued = manager_.enqueueMany(slug, numbers, title, downloads::TaskOrigin::Manual);
    if (!queued) {
        emit message(queued.error(), true);
        return;
    }
    emit message(count(numbers.size(), QStringLiteral("1 episodio en cola"),
                       QStringLiteral("%1 episodios en cola")),
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
