#include "downloads_view.h"

#include "display.h"
#include "shell.h"

#include <QFileInfo>

namespace animehub::app {
namespace {

using downloads::Task;
using downloads::TaskState;

int rank(TaskState state) {
    switch (state) {
    case TaskState::Resolving:
    case TaskState::Downloading:
    case TaskState::Verifying:
        return 0;
    case TaskState::Queued:
    case TaskState::Paused:
    case TaskState::Waiting:
        return 1;
    case TaskState::Failed:
        return 2;
    default:
        return 3;
    }
}

} // namespace

DownloadsView::DownloadsView(downloads::DownloadManager& manager,
                             library::LibraryRepository& library, playback::Player& player,
                             QObject* parent)
    : QAbstractListModel(parent), manager_(manager), library_(library), player_(player) {
    connect(&manager_, &downloads::DownloadManager::loaded, this, &DownloadsView::reload);
    connect(&manager_, &downloads::DownloadManager::taskAdded, this, &DownloadsView::reload);
    connect(&manager_, &downloads::DownloadManager::taskRemoved, this, &DownloadsView::reload);
    connect(&manager_, &downloads::DownloadManager::taskChanged, this, &DownloadsView::update);
    reload();
}

void DownloadsView::reload() {
    auto tasks = manager_.tasks();
    std::ranges::stable_sort(tasks, {}, [](const Task& task) { return rank(task.state); });
    beginResetModel();
    tasks_ = std::move(tasks);
    for (const auto& task : tasks_) {
        if (!posters_.contains(task.slug)) {
            const auto anime = library_.anime(task.slug);
            posters_.insert(task.slug, anime && *anime ? (*anime)->posterUrl : QUrl());
        }
    }
    endResetModel();
    emit summaryChanged();
}

void DownloadsView::update(qint64 id) {
    const auto task = manager_.task(id);
    const auto row = std::ranges::find(tasks_, id, &Task::id) - tasks_.begin();
    if (!task || row >= tasks_.size()) {
        reload();
        return;
    }
    if (rank(tasks_[row].state) != rank(task->state)) {
        reload(); // it moves to another section
        return;
    }
    tasks_[row] = *task;
    emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)));
    emit summaryChanged();
}

const Task* DownloadsView::current() const {
    const auto it =
        std::ranges::find_if(tasks_, [](const Task& task) { return rank(task.state) == 0; });
    return it == tasks_.end() ? nullptr : &*it;
}

// Running, queued or due to retry: what goes on by itself. Paused ones wait for the user.
int DownloadsView::activeCount() const {
    return static_cast<int>(std::ranges::count_if(tasks_, [](const Task& task) {
        return rank(task.state) < 2 && task.state != TaskState::Paused;
    }));
}

// Completed and cancelled: what "Quitar terminadas de la lista" removes. Failed ones stay.
int DownloadsView::finishedCount() const {
    return static_cast<int>(
        std::ranges::count_if(tasks_, [](const Task& task) { return rank(task.state) == 3; }));
}

bool DownloadsView::anyPaused() const {
    return std::ranges::any_of(tasks_,
                               [](const Task& task) { return task.state == TaskState::Paused; });
}

QString DownloadsView::currentTitle() const {
    const auto* task = current();
    return task ? task->animeTitle + QStringLiteral(" · ") + display::episode(task->number)
                : QString();
}

QString DownloadsView::currentDetail() const {
    const auto* task = current();
    return task ? display::download(*task) : QString();
}

double DownloadsView::currentProgress() const {
    const auto* task = current();
    return task && task->bytesTotal > 0
               ? static_cast<double>(task->bytesDone) / static_cast<double>(task->bytesTotal)
               : 0.0;
}

QString DownloadsView::queueText() const {
    const auto waiting = std::ranges::count_if(tasks_, [](const Task& task) {
        return task.state == TaskState::Queued || task.state == TaskState::Waiting;
    });
    return waiting == 0 ? QString() : QStringLiteral("%1 en cola").arg(waiting);
}

void DownloadsView::pause(qint64 id) {
    manager_.pause(id);
}

void DownloadsView::resume(qint64 id) {
    manager_.resume(id);
}

void DownloadsView::cancel(qint64 id) {
    manager_.cancel(id);
}

void DownloadsView::retry(qint64 id) {
    manager_.retry(id);
}

void DownloadsView::forget(qint64 id) {
    manager_.forget(id);
}

void DownloadsView::clearFinished() {
    manager_.clearFinished();
}

void DownloadsView::pauseAll() {
    manager_.pauseAll();
}

void DownloadsView::resumeAll() {
    manager_.resumeAll();
}

void DownloadsView::play(qint64 id) {
    const auto task = manager_.task(id);
    if (!task) {
        return;
    }
    const auto episode = library_.episode(task->slug, task->number);
    if (!episode || !*episode || (*episode)->filePath.isEmpty()) {
        emit message(QStringLiteral("El archivo ya no está en el disco."), true);
        return;
    }
    const auto& record = **episode;
    const auto started = player_.play(
        {.slug = task->slug,
         .number = task->number,
         .filePath = record.filePath,
         .title = task->animeTitle + QStringLiteral(" · ") + display::episode(task->number),
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

int DownloadsView::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(tasks_.size());
}

QVariant DownloadsView::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= tasks_.size()) {
        return {};
    }
    const auto& task = tasks_[index.row()];
    switch (role) {
    case IdRole:
        return task.id;
    case SlugRole:
        return task.slug;
    case TitleRole:
    case Qt::DisplayRole:
        return task.animeTitle;
    case EpisodeRole:
        return task.number;
    case EpisodeLabelRole:
        return display::episode(task.number);
    case PosterRole:
        return posters_.value(task.slug);
    case StateRole:
        return downloads::stateKey(task.state);
    case SectionRole:
        switch (rank(task.state)) {
        case 0:
            return QStringLiteral("Descargando");
        case 1:
            return QStringLiteral("Pendientes");
        case 2:
            return QStringLiteral("Fallidas");
        default:
            return QStringLiteral("Terminadas");
        }
    case ProgressRole:
        return task.bytesTotal > 0
                   ? static_cast<double>(task.bytesDone) / static_cast<double>(task.bytesTotal)
                   : 0.0;
    case DetailRole:
        return display::download(task);
    case ProblemRole:
        return task.state == TaskState::Failed;
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadsView::roleNames() const {
    return {{IdRole, "taskId"},
            {SlugRole, "slug"},
            {TitleRole, "title"},
            {EpisodeRole, "episode"},
            {EpisodeLabelRole, "episodeLabel"},
            {PosterRole, "posterUrl"},
            {StateRole, "state"},
            {SectionRole, "section"},
            {ProgressRole, "progress"},
            {DetailRole, "detail"},
            {ProblemRole, "problem"}};
}

} // namespace animehub::app
