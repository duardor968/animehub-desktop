#include "episode_model.h"

#include "display.h"

namespace animehub::app {

using downloads::TaskState;

void EpisodeModel::setEpisodes(QList<library::EpisodeRecord> episodes) {
    const bool same_rows =
        episodes.size() == episodes_.size() &&
        std::ranges::equal(episodes, episodes_, {}, &library::EpisodeRecord::number,
                           &library::EpisodeRecord::number);
    if (same_rows) {
        episodes_ = std::move(episodes);
        if (!episodes_.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(episodes_.size()) - 1));
        }
        return;
    }
    // Episodes only ever get appended (a new one airs, the next page arrives): insert them
    // without resetting, so the list keeps its scroll position.
    const bool appended =
        episodes.size() > episodes_.size() &&
        std::ranges::equal(episodes_, episodes | std::views::take(episodes_.size()), {},
                           &library::EpisodeRecord::number, &library::EpisodeRecord::number);
    if (appended) {
        const auto first = static_cast<int>(episodes_.size());
        beginInsertRows({}, first, static_cast<int>(episodes.size()) - 1);
        episodes_ = std::move(episodes);
        endInsertRows();
        if (first > 0) {
            emit dataChanged(index(0), index(first - 1));
        }
        emit countChanged();
        return;
    }
    beginResetModel();
    episodes_ = std::move(episodes);
    endResetModel();
    emit countChanged();
}

int EpisodeModel::rowOf(double number) const {
    for (int row = 0; row < episodes_.size(); ++row) {
        if (episodes_[row].number == number) {
            return row;
        }
    }
    return -1;
}

void EpisodeModel::setTask(const downloads::Task& task) {
    if (task.state == TaskState::Cancelled || task.state == TaskState::Completed) {
        clearTask(task.number);
        return;
    }
    tasks_.insert(task.number, task);
    if (const int row = rowOf(task.number); row >= 0) {
        emit dataChanged(index(row), index(row));
    }
}

void EpisodeModel::clearTask(double number) {
    if (tasks_.remove(number)) {
        if (const int row = rowOf(number); row >= 0) {
            emit dataChanged(index(row), index(row));
        }
    }
}

void EpisodeModel::setTasks(const QList<downloads::Task>& tasks) {
    tasks_.clear();
    for (const auto& task : tasks) {
        if (task.state != TaskState::Cancelled && task.state != TaskState::Completed) {
            tasks_.insert(task.number, task);
        }
    }
    if (!episodes_.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(episodes_.size()) - 1));
    }
}

std::optional<library::EpisodeRecord> EpisodeModel::episode(double number) const {
    const int row = rowOf(number);
    return row >= 0 ? std::optional(episodes_[row]) : std::nullopt;
}

std::optional<downloads::Task> EpisodeModel::task(double number) const {
    const auto it = tasks_.find(number);
    return it == tasks_.end() ? std::nullopt : std::optional(*it);
}

int EpisodeModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(episodes_.size());
}

QVariant EpisodeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= episodes_.size()) {
        return {};
    }
    const auto& episode = episodes_[index.row()];
    const auto task = tasks_.find(episode.number);
    const bool has_task = task != tasks_.end();
    switch (role) {
    case NumberRole:
        return episode.number;
    case LabelRole:
    case Qt::DisplayRole:
        return display::episode(episode.number);
    case TitleRole:
        return episode.title;
    case ImageRole:
        return episode.imageUrl;
    case WatchStateRole:
        return static_cast<int>(episode.state);
    case ProgressRole:
        return episode.state == library::WatchState::Watching && episode.durationMs > 0
                   ? static_cast<double>(episode.positionMs) /
                         static_cast<double>(episode.durationMs)
                   : -1.0;
    case ProgressTextRole:
        if (episode.state == library::WatchState::Watching && episode.positionMs > 0) {
            return episode.durationMs > 0
                       ? display::clock(episode.positionMs) + QStringLiteral(" / ") +
                             display::clock(episode.durationMs)
                       : display::clock(episode.positionMs);
        }
        return QString();
    case FreshRole:
        return episode.isNew;
    case HasFileRole:
        return !episode.filePath.isEmpty();
    case DownloadStateRole:
        return has_task ? downloads::stateKey(task->state) : QString();
    case DownloadProgressRole:
        return has_task && task->bytesTotal > 0
                   ? static_cast<double>(task->bytesDone) / static_cast<double>(task->bytesTotal)
                   : 0.0;
    case DownloadTextRole:
        return has_task ? display::download(*task) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> EpisodeModel::roleNames() const {
    return {{NumberRole, "number"},
            {LabelRole, "label"},
            {TitleRole, "title"},
            {ImageRole, "imageUrl"},
            {WatchStateRole, "watchState"},
            {ProgressRole, "progress"},
            {ProgressTextRole, "progressText"},
            {FreshRole, "fresh"},
            {HasFileRole, "hasFile"},
            {DownloadStateRole, "downloadState"},
            {DownloadProgressRole, "downloadProgress"},
            {DownloadTextRole, "downloadText"}};
}

} // namespace animehub::app
