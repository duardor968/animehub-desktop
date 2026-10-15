#include "library_model.h"

#include "display.h"

#include <QLocale>

#include <cmath>

namespace animehub::app {
namespace {

using library::LibraryEntry;

// The schedule's rules (`ScheduleEntryDto` in the API contract): the next episode is the latest
// one plus one, a week after it; older slots no longer describe a weekly release.
constexpr qint64 kWeekSeconds = 7LL * 24 * 3600;
constexpr qint64 kScheduleMaxAgeSeconds = 21LL * 24 * 3600;
// Past its hour, an episode is still on its way for a while before it counts as late.
constexpr qint64 kDueGraceSeconds = 3LL * 3600;

const QLocale& spanish() {
    static const QLocale kSpanish(QLocale::Spanish, QLocale::Spain);
    return kSpanish;
}

// "Jueves 22:00 · Ep. 14", "Mañana 09:30 · Ep. 3", "Ahora · Ep. 8", "Retrasado · Ep. 8".
QString upcomingCaption(const QDateTime& at, double number, const QDateTime& now) {
    const auto episode = display::shortEpisode(number);
    if (now > at.addSecs(kDueGraceSeconds)) {
        return QStringLiteral("Retrasado · ") + episode;
    }
    if (now >= at) {
        return QStringLiteral("Ahora · ") + episode;
    }
    const auto local = at.toLocalTime();
    const auto days = now.toLocalTime().date().daysTo(local.date());
    QString day;
    if (days == 0) {
        day = QStringLiteral("Hoy");
    } else if (days == 1) {
        day = QStringLiteral("Mañana");
    } else {
        day = spanish().dayName(local.date().dayOfWeek());
        day[0] = day[0].toUpper();
    }
    return day + QLatin1Char(' ') + local.toString(QStringLiteral("HH:mm")) +
           QStringLiteral(" · ") + episode;
}

} // namespace

LibraryModel::LibraryModel(library::LibraryService& library, downloads::DownloadManager& downloads,
                           api::ApiClient& api, QObject* parent)
    : QAbstractListModel(parent), library_(library), downloads_(downloads), api_(api) {
    reload_timer_.setSingleShot(true);
    reload_timer_.setInterval(40);
    connect(&reload_timer_, &QTimer::timeout, this, &LibraryModel::reload);
    connect(&library_, &library::LibraryService::listChanged, &reload_timer_,
            qOverload<>(&QTimer::start));
    connect(&library_, &library::LibraryService::animeChanged, &reload_timer_,
            qOverload<>(&QTimer::start));
    // A new episode moves its slot in the schedule.
    connect(&library_, &library::LibraryService::newEpisodes, this, &LibraryModel::fetchSchedule);

    // The featured episode's button follows its download.
    const auto on_task = [this](qint64 id) {
        const auto task = downloads_.task(id);
        if (task && task->slug == featured_slug_) {
            updateFeatured();
        }
    };
    connect(&downloads_, &downloads::DownloadManager::taskAdded, this, on_task);
    connect(&downloads_, &downloads::DownloadManager::taskChanged, this, on_task);
    connect(&downloads_, &downloads::DownloadManager::taskRemoved, this,
            &LibraryModel::updateFeatured);
    connect(&downloads_, &downloads::DownloadManager::loaded, this, &LibraryModel::updateFeatured);

    schedule_timer_.setInterval(std::chrono::minutes(30));
    connect(&schedule_timer_, &QTimer::timeout, this, &LibraryModel::fetchSchedule);
    // "Hoy", "Ahora" and "Retrasado" depend on the clock.
    clock_timer_.setInterval(std::chrono::minutes(1));
    connect(&clock_timer_, &QTimer::timeout, this, &LibraryModel::rebuildShelves);
    reload();
}

void LibraryModel::start() {
    fetchSchedule();
    schedule_timer_.start();
    clock_timer_.start();
}

// The saved copy paints the shelf at once; the network answer replaces it.
void LibraryModel::fetchSchedule() {
    api_.schedule(
        this,
        [this](const api::Result<QList<api::ScheduleEntry>>& result) {
            if (!result) {
                return; // the shelf keeps what it had; offline it may have nothing to show
            }
            schedule_ = result->value;
            schedule_stale_ = result->origin == api::Origin::Cache;
            rebuildShelves();
        },
        api::CachePolicy::CacheThenNetwork);
}

bool LibraryModel::continuing(const LibraryEntry& entry) {
    return entry.lastActivityAt.isValid() && entry.resumeNumber.has_value() && !entry.finished();
}

// A watched episode or one in progress.
bool LibraryModel::started(const LibraryEntry& entry) {
    return entry.watchedEpisodes > 0 || entry.resumePositionMs > 0;
}

// Where the user is with the anime; whether it airs is the page's business, not the list's.
QString LibraryModel::captionOf(const LibraryEntry& entry) {
    if (entry.newEpisodes > 0) {
        return entry.newEpisodes == 1
                   ? QStringLiteral("1 episodio nuevo")
                   : QStringLiteral("%1 episodios nuevos").arg(entry.newEpisodes);
    }
    if (entry.resumeNumber && entry.resumePositionMs > 0) {
        return QStringLiteral("Continuar · ") + display::shortEpisode(*entry.resumeNumber);
    }
    if (entry.resumeNumber && entry.watchedEpisodes > 0) {
        return QStringLiteral("Siguiente · ") + display::shortEpisode(*entry.resumeNumber);
    }
    if (entry.finished()) {
        return QStringLiteral("Visto");
    }
    if (entry.watchedEpisodes > 0) {
        return QStringLiteral("Al día");
    }
    return QStringLiteral("Sin empezar");
}

double LibraryModel::progressOf(const LibraryEntry& entry) {
    const int total = std::max(entry.episodeCount.value_or(0), entry.knownEpisodes);
    if (!started(entry)) {
        return -1;
    }
    return total > 0 ? std::clamp(static_cast<double>(entry.watchedEpisodes) / total, 0.0, 1.0)
                     : -1;
}

bool LibraryModel::passes(const LibraryEntry& entry) const {
    switch (filter_) {
    case Filter::All:
        return true;
    case Filter::Watching:
        return started(entry) && !entry.finished();
    case Filter::NotStarted:
        return !started(entry);
    case Filter::Watched:
        return entry.finished();
    case Filter::Favorites:
        return entry.favorite;
    case Filter::Downloaded:
        return entry.downloadedEpisodes > 0;
    }
    return true;
}

const LibraryEntry* LibraryModel::entryFor(const QString& slug) const {
    const auto it = std::ranges::find(entries_, slug, &LibraryEntry::slug);
    return it == entries_.end() ? nullptr : &*it;
}

void LibraryModel::setFilter(Filter filter) {
    if (filter_ == filter) {
        return;
    }
    filter_ = filter;
    emit filterChanged();
    rebuildRows();
    rebuildShelves();
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
    QSet<QString> slugs;
    for (const auto& entry : entries_) {
        slugs.insert(entry.slug);
    }
    const bool membership = slugs != slugs_;
    slugs_ = std::move(slugs);
    rebuildRows();
    rebuildShelves();
    if (membership) {
        emit membershipChanged();
    }
}

void LibraryModel::rebuildShelves() {
    QList<const LibraryEntry*> shown;
    for (const auto& entry : entries_) {
        if (passes(entry)) {
            shown.append(&entry);
        }
    }

    QList<const LibraryEntry*> active;
    for (const auto* entry : shown) {
        if (continuing(*entry)) {
            active.append(entry);
        }
    }
    std::ranges::sort(active, [](const LibraryEntry* a, const LibraryEntry* b) {
        return a->lastActivityAt > b->lastActivityAt;
    });
    // The one watched last is featured; the shelf goes on with the rest.
    if (const auto* featured = active.value(0)) {
        featured_slug_ = featured->slug;
        const auto episode = library_.repository().episode(featured->slug, *featured->resumeNumber);
        featured_on_disk_ = episode && *episode && !(*episode)->filePath.isEmpty();
    } else {
        featured_slug_.clear();
        featured_on_disk_ = false;
    }
    updateFeatured();

    // The same caption and the same bar (how much of the series) as in the list.
    QList<Card> continue_cards;
    for (const auto* entry : active | std::views::drop(1) | std::views::take(12)) {
        continue_cards.append({.slug = entry->slug,
                               .title = entry->title,
                               .posterUrl = entry->posterUrl,
                               .caption = captionOf(*entry),
                               .progress = progressOf(*entry),
                               .episode = *entry->resumeNumber});
    }
    continue_shelf_.setCards(std::move(continue_cards));

    QList<Card> fresh_cards;
    QList<Card> all_cards;
    for (const auto* entry : shown) {
        const Card card{.slug = entry->slug,
                        .title = entry->title,
                        .posterUrl = entry->posterUrl,
                        .caption = captionOf(*entry),
                        .progress = progressOf(*entry),
                        .fresh = entry->newEpisodes > 0};
        if (entry->newEpisodes > 0) {
            // The shelf is titled "Episodios nuevos" already: just how many, no dot.
            auto fresh = card;
            fresh.caption = entry->newEpisodes == 1
                                ? QStringLiteral("1 nuevo")
                                : QStringLiteral("%1 nuevos").arg(entry->newEpisodes);
            fresh.fresh = false;
            fresh_cards.append(fresh);
        }
        all_cards.append(card);
    }
    fresh_shelf_.setCards(std::move(fresh_cards));
    all_shelf_.setCards(std::move(all_cards));

    rebuildUpcoming(shown);
}

// The next episode of each library anime that airs weekly, soonest first; late ones last.
void LibraryModel::rebuildUpcoming(const QList<const LibraryEntry*>& shown) {
    const auto now = QDateTime::currentDateTimeUtc();
    struct Next {
        const LibraryEntry* entry;
        double number;
        QDateTime at;
        bool late;
    };
    QList<Next> upcoming;
    for (const auto& slot : schedule_) {
        const auto& basis = slot.basisPublishedAt;
        const auto latest = slot.latestEpisode.number;
        // A finale has no next episode; without a whole number there is none to predict.
        if (slot.isFinalEpisode || slot.anime.status == api::AnimeStatus::Finished ||
            !basis.isValid() || basis > now || basis.secsTo(now) > kScheduleMaxAgeSeconds ||
            latest != std::trunc(latest)) {
            continue;
        }
        const auto it = std::ranges::find(shown, slot.anime.slug, &LibraryEntry::slug);
        if (it == shown.end()) {
            continue;
        }
        const auto at = basis.addSecs(kWeekSeconds);
        const bool late = now > at.addSecs(kDueGraceSeconds);
        // A copy from the cache cannot tell a late episode from one already out.
        if (late && schedule_stale_) {
            continue;
        }
        upcoming.append({.entry = *it, .number = latest + 1, .at = at, .late = late});
    }
    std::ranges::sort(upcoming, [](const Next& a, const Next& b) {
        return a.late != b.late ? b.late : a.at < b.at;
    });
    QList<Card> cards;
    for (const auto& next : upcoming) {
        cards.append({.slug = next.entry->slug,
                      .title = next.entry->title,
                      .posterUrl = next.entry->posterUrl,
                      .caption = upcomingCaption(next.at, next.number, now),
                      .episode = next.number});
    }
    upcoming_shelf_.setCards(std::move(cards));
}

void LibraryModel::updateFeatured() {
    const auto* entry = featured_slug_.isEmpty() ? nullptr : entryFor(featured_slug_);
    QVariantMap featured;
    if (entry && entry->resumeNumber) {
        const auto number = *entry->resumeNumber;
        // The line names the episode; the button only says what it does.
        QString detail = display::shortEpisode(number);
        double progress = -1;
        if (entry->resumePositionMs > 0) {
            detail += QStringLiteral(" · ") + display::clock(entry->resumePositionMs);
            if (entry->resumeDurationMs > 0) {
                detail += QStringLiteral(" de ") + display::clock(entry->resumeDurationMs);
                progress = std::clamp(static_cast<double>(entry->resumePositionMs) /
                                          entry->resumeDurationMs,
                                      0.0, 1.0);
            }
        } else if (const int total =
                       std::max(entry->episodeCount.value_or(0), entry->knownEpisodes);
                   total > 0) {
            // The next episode is not started: how far the series is.
            detail += QStringLiteral(" · %1 de %2 vistos").arg(entry->watchedEpisodes).arg(total);
            progress = progressOf(*entry);
        }
        auto action = FeaturedAction::Download;
        QString action_text = QStringLiteral("Descargar");
        if (featured_on_disk_) {
            action = FeaturedAction::Play;
            action_text = QStringLiteral("Continuar");
        } else if (const auto task = downloads_.activeTaskFor(entry->slug, number);
                   task && task->state != downloads::TaskState::Paused) {
            action = FeaturedAction::Downloading;
            action_text = QStringLiteral("Descargando");
        }
        featured = {{QStringLiteral("slug"), entry->slug},
                    {QStringLiteral("title"), entry->title},
                    {QStringLiteral("backdropUrl"), entry->backdropUrl},
                    {QStringLiteral("posterUrl"), entry->posterUrl},
                    {QStringLiteral("detail"), detail},
                    {QStringLiteral("progress"), progress},
                    {QStringLiteral("action"), static_cast<int>(action)},
                    {QStringLiteral("actionText"), action_text}};
    }
    if (featured != featured_) {
        featured_ = std::move(featured);
        emit featuredChanged();
    }
}

void LibraryModel::rebuildRows() {
    QList<int> rows;
    const auto matches = [this](const LibraryEntry& entry) {
        if (!search_.isEmpty() && !entry.title.contains(search_, Qt::CaseInsensitive) &&
            !entry.alternativeTitle.contains(search_, Qt::CaseInsensitive)) {
            return false;
        }
        return passes(entry);
    };
    // One alphabetical list, like Steam's; what is being watched lives on the home shelves.
    for (int i = 0; i < entries_.size(); ++i) {
        if (matches(entries_[i])) {
            rows.append(i);
        }
    }
    if (rows == rows_) {
        if (!rows_.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        }
        return;
    }
    beginResetModel();
    rows_ = std::move(rows);
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

bool LibraryModel::contains(const QString& slug) const {
    return slugs_.contains(slug);
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
    case FreshRole:
        return entry.newEpisodes > 0;
    case FavoriteRole:
        return entry.favorite;
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{SlugRole, "slug"},        {TitleRole, "title"},       {PosterRole, "posterUrl"},
            {CaptionRole, "caption"},  {ProgressRole, "progress"}, {FreshRole, "fresh"},
            {FavoriteRole, "favorite"}};
}

} // namespace animehub::app
