#include "episode_model.h"

#include "display.h"

#include <algorithm>

namespace animehub::app {

using downloads::TaskState;

namespace {

bool sameNumbers(const QList<library::EpisodeRecord>& a, const QList<library::EpisodeRecord>& b) {
    return std::ranges::equal(a, b, {}, &library::EpisodeRecord::number,
                              &library::EpisodeRecord::number);
}

} // namespace

int EpisodeModel::rowsOn(qsizetype size, int page) {
    return static_cast<int>(
        std::clamp<qsizetype>(size - qsizetype{page} * kPageSize, 0, kPageSize));
}

int EpisodeModel::pageCount() const {
    return static_cast<int>((episodes_.size() + kPageSize - 1) / kPageSize);
}

QStringList EpisodeModel::pageLabels() const {
    QStringList labels;
    for (qsizetype start = 0; start < episodes_.size(); start += kPageSize) {
        const auto end = std::min(start + kPageSize, episodes_.size()) - 1;
        const auto from = display::episodeNumber(episodes_[start].number);
        labels.append(start == end
                          ? from
                          : from + QChar(0x2013) + display::episodeNumber(episodes_[end].number));
    }
    return labels;
}

void EpisodeModel::load(QList<library::EpisodeRecord> episodes, std::optional<double> shown) {
    if (episodes.isEmpty() && episodes_.isEmpty()) {
        return;
    }
    beginResetModel();
    episodes_ = std::move(episodes);
    selected_.clear();
    anchor_.reset();
    page_ = 0;
    if (shown) {
        if (const int index = indexOf(*shown); index >= 0) {
            page_ = index / kPageSize;
        }
    }
    endResetModel();
    emit countChanged();
    emit pageChanged();
    emit selectionChanged();
}

void EpisodeModel::setEpisodes(QList<library::EpisodeRecord> episodes) {
    const bool selection_shrinks = std::ranges::any_of(selected_, [&episodes](double number) {
        return std::ranges::find(episodes, number, &library::EpisodeRecord::number) ==
               episodes.end();
    });
    if (selection_shrinks) {
        QSet<double> kept;
        for (const auto& episode : episodes) {
            if (selected_.contains(episode.number)) {
                kept.insert(episode.number);
            }
        }
        selected_ = std::move(kept);
    }
    if (episodes.size() == episodes_.size() && sameNumbers(episodes, episodes_)) {
        episodes_ = std::move(episodes);
        refreshPage();
        emit selectionChanged(); // what can be downloaded may have changed
        return;
    }
    // Episodes only ever get appended (a new one airs, the next page arrives): insert them
    // without resetting, so the list keeps its scroll position.
    const bool appended = episodes.size() > episodes_.size() &&
                          sameNumbers(episodes_, episodes.mid(0, episodes_.size()));
    if (appended) {
        const int before = rowCount();
        const int after = rowsOn(episodes.size(), page_);
        if (after > before) {
            beginInsertRows({}, before, after - 1);
            episodes_ = std::move(episodes);
            endInsertRows();
        } else {
            episodes_ = std::move(episodes);
        }
        refreshPage();
        emit countChanged();
        emit pageChanged();
        emit selectionChanged();
        return;
    }
    beginResetModel();
    episodes_ = std::move(episodes);
    page_ = std::clamp(page_, 0, std::max(0, pageCount() - 1));
    endResetModel();
    emit countChanged();
    emit pageChanged();
    emit selectionChanged();
}

void EpisodeModel::setPage(int page) {
    page = std::clamp(page, 0, std::max(0, pageCount() - 1));
    if (page == page_) {
        return;
    }
    beginResetModel();
    page_ = page;
    endResetModel();
    emit countChanged();
    emit pageChanged();
    emit selectionChanged();
}

int EpisodeModel::indexOf(double number) const {
    const auto it = std::ranges::find(episodes_, number, &library::EpisodeRecord::number);
    return it == episodes_.end() ? -1 : static_cast<int>(it - episodes_.begin());
}

int EpisodeModel::rowOf(double number) const {
    const int index = indexOf(number);
    return index >= first() && index < first() + rowCount() ? index - first() : -1;
}

void EpisodeModel::refresh(double number) {
    if (const int row = rowOf(number); row >= 0) {
        emit dataChanged(index(row), index(row));
    }
}

void EpisodeModel::refreshPage() {
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
}

void EpisodeModel::setTask(const downloads::Task& task) {
    if (task.state == TaskState::Cancelled || task.state == TaskState::Completed) {
        if (!tasks_.remove(task.number)) {
            return;
        }
    } else {
        tasks_.insert(task.number, task);
    }
    refresh(task.number);
    if (selected_.contains(task.number)) {
        emit selectionChanged();
    }
}

void EpisodeModel::setTasks(const QList<downloads::Task>& tasks) {
    tasks_.clear();
    for (const auto& task : tasks) {
        if (task.state != TaskState::Cancelled && task.state != TaskState::Completed) {
            tasks_.insert(task.number, task);
        }
    }
    refreshPage();
    emit selectionChanged();
}

std::optional<library::EpisodeRecord> EpisodeModel::episode(double number) const {
    const int index = indexOf(number);
    return index >= 0 ? std::optional(episodes_[index]) : std::nullopt;
}

std::optional<downloads::Task> EpisodeModel::task(double number) const {
    const auto it = tasks_.find(number);
    return it == tasks_.end() ? std::nullopt : std::optional(*it);
}

bool EpisodeModel::missing(const library::EpisodeRecord& episode) const {
    if (!episode.filePath.isEmpty()) {
        return false;
    }
    const auto it = tasks_.find(episode.number);
    return it == tasks_.end() || it->state == TaskState::Paused || it->state == TaskState::Failed;
}

int EpisodeModel::selectedMissing() const {
    return static_cast<int>(std::ranges::count_if(episodes_, [this](const auto& episode) {
        return selected_.contains(episode.number) && missing(episode);
    }));
}

int EpisodeModel::pageSelection() const {
    int selected = 0;
    for (int row = 0; row < rowCount(); ++row) {
        selected += selected_.contains(episodes_[first() + row].number) ? 1 : 0;
    }
    if (selected == 0) {
        return Qt::Unchecked;
    }
    return selected == rowCount() ? Qt::Checked : Qt::PartiallyChecked;
}

QList<double> EpisodeModel::selection() const {
    QList<double> numbers;
    for (const auto& episode : episodes_) {
        if (selected_.contains(episode.number)) {
            numbers.append(episode.number);
        }
    }
    return numbers;
}

void EpisodeModel::toggle(double number, bool extend) {
    const int to = indexOf(number);
    if (to < 0) {
        return;
    }
    const int from = extend && anchor_ ? indexOf(*anchor_) : -1;
    if (from >= 0) {
        for (int index = std::min(from, to); index <= std::max(from, to); ++index) {
            selected_.insert(episodes_[index].number);
        }
        refreshPage();
    } else {
        if (!selected_.remove(number)) {
            selected_.insert(number);
        }
        refresh(number);
    }
    anchor_ = number;
    emit selectionChanged();
}

void EpisodeModel::selectPage(bool selected) {
    for (int row = 0; row < rowCount(); ++row) {
        const auto number = episodes_[first() + row].number;
        if (selected) {
            selected_.insert(number);
        } else {
            selected_.remove(number);
        }
    }
    anchor_.reset();
    refreshPage();
    emit selectionChanged();
}

void EpisodeModel::clearSelection() {
    if (selected_.isEmpty()) {
        return;
    }
    selected_.clear();
    anchor_.reset();
    refreshPage();
    emit selectionChanged();
}

int EpisodeModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rowsOn(episodes_.size(), page_);
}

QVariant EpisodeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const auto& episode = episodes_[first() + index.row()];
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
                       ? display::clock(episode.positionMs) + QStringLiteral(" de ") +
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
    case SelectedRole:
        return selected_.contains(episode.number);
    case FirstRole:
        return first() + index.row() == 0;
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
            {DownloadTextRole, "downloadText"},
            {SelectedRole, "selected"},
            {FirstRole, "first"}};
}

} // namespace animehub::app
