#include "library_model.h"

#include "display.h"

namespace animehub::app {

using library::LibraryEntry;

LibraryModel::LibraryModel(library::LibraryService& library, QObject* parent)
    : QAbstractListModel(parent), library_(library) {
    reload_timer_.setSingleShot(true);
    reload_timer_.setInterval(40);
    connect(&reload_timer_, &QTimer::timeout, this, &LibraryModel::reload);
    connect(&library_, &library::LibraryService::listChanged, &reload_timer_,
            qOverload<>(&QTimer::start));
    connect(&library_, &library::LibraryService::animeChanged, &reload_timer_,
            qOverload<>(&QTimer::start));
    reload();
}

bool LibraryModel::continuing(const LibraryEntry& entry) {
    return entry.lastActivityAt.isValid() && entry.resumeNumber.has_value() && !entry.finished();
}

QString LibraryModel::captionOf(const LibraryEntry& entry) {
    if (entry.newEpisodes > 0) {
        return entry.newEpisodes == 1
                   ? QStringLiteral("1 episodio nuevo")
                   : QStringLiteral("%1 episodios nuevos").arg(entry.newEpisodes);
    }
    if (entry.resumeNumber && entry.resumePositionMs > 0) {
        return QStringLiteral("Continuar · ") + display::episode(*entry.resumeNumber);
    }
    if (entry.resumeNumber && entry.watchedEpisodes > 0) {
        return QStringLiteral("Siguiente · ") + display::episode(*entry.resumeNumber);
    }
    if (entry.finished()) {
        return QStringLiteral("Visto");
    }
    if (entry.status == api::AnimeStatus::Upcoming) {
        return display::status(entry.status);
    }
    if (entry.watchedEpisodes > 0) {
        return QStringLiteral("Al día");
    }
    if (entry.downloadedEpisodes > 0) {
        return entry.downloadedEpisodes == 1
                   ? QStringLiteral("1 episodio descargado")
                   : QStringLiteral("%1 episodios descargados").arg(entry.downloadedEpisodes);
    }
    return QStringLiteral("Sin empezar");
}

double LibraryModel::progressOf(const LibraryEntry& entry) {
    const int total = std::max(entry.episodeCount.value_or(0), entry.knownEpisodes);
    if (entry.watchedEpisodes == 0 && entry.resumePositionMs == 0) {
        return -1;
    }
    return total > 0 ? std::clamp(static_cast<double>(entry.watchedEpisodes) / total, 0.0, 1.0)
                     : -1;
}

void LibraryModel::setFilter(Filter filter) {
    if (filter_ == filter) {
        return;
    }
    filter_ = filter;
    emit filterChanged();
    rebuildRows();
}

void LibraryModel::setSearch(const QString& search) {
    const auto normalized = search.simplified();
    if (search_ == normalized) {
        return;
    }
    search_ = normalized;
    emit searchChanged();
    rebuildRows();
}

void LibraryModel::reload() {
    auto entries = library_.repository().entries();
    if (!entries) {
        qWarning("library list: %s", qPrintable(entries.error().message));
        return;
    }
    entries_ = std::move(*entries);
    rebuildRows();

    QList<const LibraryEntry*> active;
    for (const auto& entry : entries_) {
        if (continuing(entry)) {
            active.append(&entry);
        }
    }
    std::ranges::sort(active, [](const LibraryEntry* a, const LibraryEntry* b) {
        return a->lastActivityAt > b->lastActivityAt;
    });
    QList<Card> continue_cards;
    for (const auto* entry : active | std::views::take(12)) {
        continue_cards.append({.slug = entry->slug,
                               .title = entry->title,
                               .posterUrl = entry->posterUrl,
                               .caption = display::episode(*entry->resumeNumber),
                               .progress = entry->resumeDurationMs > 0
                                               ? static_cast<double>(entry->resumePositionMs) /
                                                     static_cast<double>(entry->resumeDurationMs)
                                               : 0.0,
                               .episode = *entry->resumeNumber});
    }
    continue_shelf_.setCards(std::move(continue_cards));

    QList<Card> fresh_cards;
    QList<Card> all_cards;
    for (const auto& entry : entries_) {
        const Card card{.slug = entry.slug,
                        .title = entry.title,
                        .posterUrl = entry.posterUrl,
                        .caption = captionOf(entry),
                        .progress = progressOf(entry),
                        .fresh = entry.newEpisodes > 0};
        if (entry.newEpisodes > 0) {
            fresh_cards.append(card);
        }
        all_cards.append(card);
    }
    fresh_shelf_.setCards(std::move(fresh_cards));
    all_shelf_.setCards(std::move(all_cards));
}

void LibraryModel::rebuildRows() {
    QList<int> rows;
    QList<bool> sections;
    const auto matches = [this](const LibraryEntry& entry) {
        if (!search_.isEmpty() && !entry.title.contains(search_, Qt::CaseInsensitive) &&
            !entry.alternativeTitle.contains(search_, Qt::CaseInsensitive)) {
            return false;
        }
        switch (filter_) {
        case Filter::All:
            return true;
        case Filter::Pending:
            return entry.pending;
        case Filter::Favorites:
            return entry.favorite;
        case Filter::Watched:
            return entry.finished();
        case Filter::Downloaded:
            return entry.downloadedEpisodes > 0;
        }
        return true;
    };
    // Without filter or search, what is being watched goes first, most recent on top.
    const bool grouped = filter_ == Filter::All && search_.isEmpty();
    if (grouped) {
        QList<int> active;
        for (int i = 0; i < entries_.size(); ++i) {
            if (continuing(entries_[i])) {
                active.append(i);
            }
        }
        std::ranges::sort(active, [this](int a, int b) {
            return entries_[a].lastActivityAt > entries_[b].lastActivityAt;
        });
        rows = active;
        sections.fill(true, active.size());
    }
    for (int i = 0; i < entries_.size(); ++i) {
        if (matches(entries_[i]) && !(grouped && continuing(entries_[i]))) {
            rows.append(i);
            sections.append(false);
        }
    }
    const bool same_rows = rows.size() == rows_.size() &&
                           std::ranges::equal(rows, rows_, [this](int a, int b) { return a == b; });
    if (same_rows && sections == continue_section_) {
        rows_ = std::move(rows);
        if (!rows_.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        }
        return;
    }
    beginResetModel();
    rows_ = std::move(rows);
    continue_section_ = std::move(sections);
    endResetModel();
    emit countChanged();
}

int LibraryModel::indexOf(const QString& slug) const {
    for (int row = 0; row < rows_.size(); ++row) {
        if (entries_[rows_[row]].slug == slug) {
            return row;
        }
    }
    return -1;
}

QString LibraryModel::slugAt(int row) const {
    return row >= 0 && row < rows_.size() ? entries_[rows_[row]].slug : QString();
}

int LibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const auto& entry = entries_[rows_[index.row()]];
    switch (role) {
    case SlugRole:
        return entry.slug;
    case TitleRole:
    case Qt::DisplayRole:
        return entry.title;
    case PosterRole:
        return entry.posterUrl;
    case CaptionRole:
        return captionOf(entry);
    case ProgressRole:
        return progressOf(entry);
    case SectionRole:
        return continue_section_[index.row()] ? QStringLiteral("Continuar viendo")
                                              : QStringLiteral("Biblioteca");
    case FreshRole:
        return entry.newEpisodes > 0;
    case FavoriteRole:
        return entry.favorite;
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{SlugRole, "slug"},       {TitleRole, "title"},       {PosterRole, "posterUrl"},
            {CaptionRole, "caption"}, {ProgressRole, "progress"}, {SectionRole, "section"},
            {FreshRole, "fresh"},     {FavoriteRole, "favorite"}};
}

} // namespace animehub::app
