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
    case LabelRole:
    case Qt::DisplayRole:
        return row.label;
    case ImageRole:
        return row.image;
    case StateRole:
        return row.state;
    case ProgressRole:
        return row.progress;
    case DetailRole:
        return row.detail;
    case ProblemRole:
        return row.problem;
    case HasFileRole:
        return row.hasFile;
    case SourceRole:
        return row.source;
    case EngineRole:
        return row.engine;
    case ConnectionRole:
        return row.connection;
    case DestinationRole:
        return row.destination;
    case PhaseRole:
        return row.phase;
    case CanPauseRole:
        return row.canPause;
    case CanResumeRole:
        return row.canResume;
    case CanRetryRole:
        return row.canRetry;
    case PartialRole:
        return row.partial;
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadEpisodesModel::roleNames() const {
    return {{IdRole, "taskId"},
            {NumberRole, "number"},
            {LabelRole, "label"},
            {ImageRole, "image"},
            {StateRole, "taskState"},
            {ProgressRole, "progress"},
            {DetailRole, "detail"},
            {ProblemRole, "problem"},
            {HasFileRole, "hasFile"},
            {SourceRole, "source"},
            {EngineRole, "engine"},
            {ConnectionRole, "connection"},
            {DestinationRole, "destination"},
            {PhaseRole, "phase"},
            {CanPauseRole, "canPause"},
            {CanResumeRole, "canResume"},
            {CanRetryRole, "canRetry"},
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
    case ArtRole:
        return row.art;
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
    case EnginesRole:
        return row.engines;
    case DestinationsRole:
        return row.destinations;
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
    case EpisodesRole:
        return QVariant::fromValue(episodes_.value(row.slug));
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadSeriesModel::roleNames() const {
    return {{SlugRole, "slug"},
            {TitleRole, "title"},
            {ArtRole, "art"},
            {SummaryRole, "summary"},
            {ProgressRole, "progress"},
            {FailedRole, "failed"},
            {FailedTextRole, "failedText"},
            {CanPauseRole, "canPause"},
            {CanResumeRole, "canResume"},
            {CanRetryRole, "canRetry"},
            {EnginesRole, "engines"},
            {DestinationsRole, "destinations"},
            {FirstRole, "first"},
            {CancelTextRole, "cancelText"},
            {FilesOnDiskRole, "filesOnDisk"},
            {DiskTextRole, "diskText"},
            {WatchTextRole, "watchText"},
            {HasFolderRole, "hasFolder"},
            {EpisodesRole, "episodes"}};
}

} // namespace animehub::app
