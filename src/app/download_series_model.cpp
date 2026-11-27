#include "download_series_model.h"

#include <QQmlEngine>
#include <QSet>

namespace animehub::app {
namespace {

// Turns `current` into `next` through removals, moves and insertions keyed by `key_of`, so a
// view keeps the delegates of the rows that stay. The callbacks change `current` themselves.
template <typename Row, typename KeyOf, typename Remove, typename Move, typename Insert,
          typename Update>
void reconcile(const QList<Row>& current, const QList<Row>& next, KeyOf key_of, Remove remove,
               Move move, Insert insert, Update update) {
    QSet<decltype(key_of(Row{}))> wanted;
    for (const auto& row : next) {
        wanted.insert(key_of(row));
    }
    for (auto i = current.size() - 1; i >= 0; --i) {
        if (!wanted.contains(key_of(current[i]))) {
            remove(static_cast<int>(i));
        }
    }
    for (int i = 0; i < next.size(); ++i) {
        const auto key = key_of(next[i]);
        if (i < current.size() && key_of(current[i]) == key) {
            update(i, next[i]);
            continue;
        }
        int found = -1;
        for (auto j = i + 1; j < current.size(); ++j) {
            if (key_of(current[j]) == key) {
                found = static_cast<int>(j);
                break;
            }
        }
        if (found >= 0) {
            move(found, i);
            update(i, next[i]);
        } else {
            insert(i, next[i]);
        }
    }
}

} // namespace

void DownloadEpisodesModel::setRows(const QList<DownloadEpisodeRow>& rows) {
    const auto count = rows_.size();
    reconcile(
        rows_, rows, [](const DownloadEpisodeRow& row) { return row.id; },
        [this](int i) {
            beginRemoveRows({}, i, i);
            rows_.removeAt(i);
            endRemoveRows();
        },
        [this](int from, int to) {
            beginMoveRows({}, from, from, {}, to);
            rows_.move(from, to);
            endMoveRows();
        },
        [this](int i, const DownloadEpisodeRow& row) {
            beginInsertRows({}, i, i);
            rows_.insert(i, row);
            endInsertRows();
        },
        [this](int i, const DownloadEpisodeRow& row) {
            if (rows_[i] != row) {
                rows_[i] = row;
                emit dataChanged(index(i), index(i));
            }
        });
    if (rows_.size() != count) {
        emit countChanged();
    }
}

int DownloadEpisodesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant DownloadEpisodesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_[index.row()];
    switch (role) {
    case IdRole:
        return row.id;
    case NumberRole:
        return row.number;
    case EpisodeRole:
        return row.episode;
    case LabelRole:
    case Qt::DisplayRole:
        return row.label;
    case TitleRole:
        return row.title;
    case ImageRole:
    case StillUrlRole:
        return row.image;
    case StateRole:
        return row.state;
    case StateGroupRole:
        return row.stateGroup;
    case StateTextRole:
        return row.stateText;
    case ProgressRole:
        return row.progress;
    case PercentTextRole:
        return row.percentText;
    case SpeedTextRole:
        return row.speedText;
    case SizeTextRole:
        return row.sizeText;
    case EtaTextRole:
        return row.etaText;
    case ProviderRole:
        return row.provider;
    case FilePathRole:
        return row.filePath;
    case DetailRole:
        return row.detail;
    case ProblemRole:
        return row.problem;
    case HasFileRole:
    case CanPlayRole:
        return row.hasFile;
    case CanPauseRole:
        return row.canPause;
    case CanResumeRole:
        return row.canResume;
    case CanRetryRole:
        return row.canRetry;
    case CanRetryNowRole:
        return row.canRetryNow;
    case CanCancelRole:
        return row.canCancel;
    case PartialRole:
        return row.partial;
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadEpisodesModel::roleNames() const {
    return {{IdRole, "taskId"},
            {NumberRole, "number"},
            {EpisodeRole, "episode"},
            {LabelRole, "label"},
            {TitleRole, "title"},
            {ImageRole, "image"},
            {StillUrlRole, "stillUrl"},
            {StateRole, "taskState"},
            {StateGroupRole, "stateGroup"},
            {StateTextRole, "stateText"},
            {ProgressRole, "progress"},
            {PercentTextRole, "percentText"},
            {SpeedTextRole, "speedText"},
            {SizeTextRole, "sizeText"},
            {EtaTextRole, "etaText"},
            {ProviderRole, "provider"},
            {FilePathRole, "filePath"},
            {DetailRole, "detail"},
            {ProblemRole, "problem"},
            {HasFileRole, "hasFile"},
            {CanPlayRole, "canPlay"},
            {CanPauseRole, "canPause"},
            {CanResumeRole, "canResume"},
            {CanRetryRole, "canRetry"},
            {CanRetryNowRole, "canRetryNow"},
            {CanCancelRole, "canCancel"},
            {PartialRole, "partial"}};
}

DownloadEpisodesModel* DownloadSeriesModel::episodesOf(const QString& slug) {
    auto*& model = episodes_[slug];
    if (!model) {
        model = new DownloadEpisodesModel(this);
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    }
    return model;
}

void DownloadSeriesModel::setRows(QList<DownloadSeriesRow> rows) {
    const auto count = rows_.size();
    reconcile(
        rows_, rows, [](const DownloadSeriesRow& row) { return row.slug; },
        [this](int i) {
            beginRemoveRows({}, i, i);
            const auto slug = rows_.takeAt(i).slug;
            endRemoveRows();
            if (auto* model = episodes_.take(slug)) {
                model->deleteLater();
            }
        },
        [this](int from, int to) {
            beginMoveRows({}, from, from, {}, to);
            rows_.move(from, to);
            endMoveRows();
        },
        [this](int i, const DownloadSeriesRow& row) {
            episodesOf(row.slug)->setRows(row.episodes);
            beginInsertRows({}, i, i);
            rows_.insert(i, row);
            endInsertRows();
        },
        [this](int i, const DownloadSeriesRow& row) {
            if (rows_[i] == row) {
                return;
            }
            episodesOf(row.slug)->setRows(row.episodes);
            rows_[i] = row;
            emit dataChanged(index(i), index(i));
        });
    if (rows_.size() != count) {
        emit countChanged();
    }
}

const DownloadSeriesRow* DownloadSeriesModel::row(const QString& slug) const {
    const auto it = std::ranges::find(rows_, slug, &DownloadSeriesRow::slug);
    return it == rows_.end() ? nullptr : &*it;
}

int DownloadSeriesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant DownloadSeriesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_[index.row()];
    switch (role) {
    case SlugRole:
        return row.slug;
    case TitleRole:
    case Qt::DisplayRole:
        return row.title;
    case PosterRole:
        return row.poster;
    case BackdropRole:
        return row.backdrop;
    case SummaryRole:
        return row.summary;
    case ProgressRole:
        return row.progress;
    case FailedRole:
        return row.failed;
    case FailedTextRole:
        return row.failedText;
    case CanPauseRole:
        return row.canPause;
    case CanResumeRole:
        return row.canResume;
    case CanRetryRole:
        return row.canRetry;
    case EngineLineRole:
        return row.engineLine;
    case FirstRole:
        return row.first;
    case CancelTextRole:
        return row.cancelText;
    case FilesOnDiskRole:
        return row.filesOnDisk;
    case DiskTextRole:
        return row.diskText;
    case WatchTextRole:
        return row.watchText;
    case HasFolderRole:
        return row.hasFolder;
    case NoticeRole:
        return row.notice;
    case RunningRole:
        return row.running;
    case ActiveCountRole:
        return row.activeCount;
    case RunningSpeedTextRole:
        return row.runningSpeedText;
    case CancelCountRole:
        return row.cancelCount;
    case FilesGoneRole:
        return row.filesGone;
    case CanRetryNowRole:
        return row.canRetryNow;
    case EpisodesRole:
    case TasksRole:
        return QVariant::fromValue(episodes_.value(row.slug));
    case TaskCountRole:
        return static_cast<int>(row.episodes.size());
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadSeriesModel::roleNames() const {
    return {{SlugRole, "slug"},
            {TitleRole, "title"},
            {PosterRole, "poster"},
            {BackdropRole, "backdrop"},
            {SummaryRole, "summary"},
            {ProgressRole, "progress"},
            {FailedRole, "failed"},
            {FailedTextRole, "failedText"},
            {CanPauseRole, "canPause"},
            {CanResumeRole, "canResume"},
            {CanRetryRole, "canRetry"},
            {EngineLineRole, "engineLine"},
            {FirstRole, "first"},
            {CancelTextRole, "cancelText"},
            {FilesOnDiskRole, "filesOnDisk"},
            {DiskTextRole, "diskText"},
            {WatchTextRole, "watchText"},
            {HasFolderRole, "hasFolder"},
            {NoticeRole, "notice"},
            {RunningRole, "running"},
            {ActiveCountRole, "activeCount"},
            {RunningSpeedTextRole, "runningSpeedText"},
            {CancelCountRole, "cancelCount"},
            {FilesGoneRole, "filesGone"},
            {CanRetryNowRole, "canRetryNow"},
            {EpisodesRole, "episodes"},
            {TasksRole, "tasks"},
            {TaskCountRole, "taskCount"}};
}

} // namespace animehub::app
