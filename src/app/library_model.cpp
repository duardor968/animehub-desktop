#include "library_model.h"

#include "display.h"
#include "schedule.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace animehub::app {
namespace {

using library::LibraryEntry;

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
    // A queued episode is no longer missing; a removed one is again.
    rebuild_timer_.setSingleShot(true);
    rebuild_timer_.setInterval(40);
    connect(&rebuild_timer_, &QTimer::timeout, this, [this] {
        rebuildRows();
        rebuildShelves();
    });
    for (const auto signal : {&downloads::DownloadManager::taskAdded,
                              &downloads::DownloadManager::taskRemoved}) {
        connect(&downloads_, signal, &rebuild_timer_, qOverload<>(&QTimer::start));
    }
    connect(&downloads_, &downloads::DownloadManager::loaded, &rebuild_timer_,
            qOverload<>(&QTimer::start));

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

// A watched episode or one in progress.
bool LibraryModel::started(const LibraryEntry& entry) {
    return entry.watchedEpisodes > 0 || entry.resumePositionMs > 0;
}

// What the user keeps up with: a series begun and not finished, or one still airing.
bool LibraryModel::following(const LibraryEntry& entry) {
    return (started(entry) && !entry.finished()) || entry.status == api::AnimeStatus::Airing;
}

// Followed, and the episode it goes on with is on disk.
bool LibraryModel::ready(const LibraryEntry& entry) {
    return following(entry) && entry.resumeNumber && entry.resumeOnDisk;
}

// The episodes a followed anime still needs on disk: from the next one to watch on, without
// those already queued.
QList<double> LibraryModel::pendingOf(const LibraryEntry& entry) const {
    QList<double> pending;
    if (!following(entry)) {
        return pending;
    }
    for (const auto number : entry.missingEpisodes) {
        if (!downloads_.activeTaskFor(entry.slug, number)) {
            pending.append(number);
        }
    }
    return pending;
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

// "Ep. 6", "Continuar · Ep. 6", "Ep. 6 · 3 en disco"
QString LibraryModel::readyCaption(const LibraryEntry& entry) {
    const auto episode = display::shortEpisode(*entry.resumeNumber);
    auto caption = entry.resumePositionMs > 0 ? QStringLiteral("Continuar · ") + episode : episode;
    if (entry.unwatchedOnDisk > 1) {
        caption += QStringLiteral(" · %1 en disco").arg(entry.unwatchedOnDisk);
    }
    return caption;
}

// "Ep. 4", "Ep. 4 a 7", "5 episodios"
QString LibraryModel::pendingCaption(const QList<double>& pending) {
    if (pending.size() == 1) {
        return display::shortEpisode(pending.first());
    }
    const bool run =
        pending.last() - pending.first() == static_cast<double>(pending.size() - 1) &&
        std::ranges::all_of(pending, [](double number) { return number == std::trunc(number); });
    return run ? display::shortEpisode(pending.first()) + QStringLiteral(" a ") +
                     display::episodeNumber(pending.last())
               : QStringLiteral("%1 episodios").arg(pending.size());
}

// How far a series is, while it is under way: a seen one needs no full bar.
double LibraryModel::progressOf(const LibraryEntry& entry) {
    const int total = std::max(entry.episodeCount.value_or(0), entry.knownEpisodes);
    if (!started(entry) || entry.finished()) {
        return -1;
    }
    return total > 0 ? std::clamp(static_cast<double>(entry.watchedEpisodes) / total, 0.0, 1.0)
                     : -1;
}

bool LibraryModel::passes(const LibraryEntry& entry) const {
    if (on_disk_only_ && entry.downloadedEpisodes == 0) {
        return false;
    }
    switch (filter_) {
    case Filter::All:
        return true;
    case Filter::Airing:
        return entry.status == api::AnimeStatus::Airing;
    case Filter::Watching:
        return started(entry) && !entry.finished();
    case Filter::NotStarted:
        return !started(entry);
    case Filter::Watched:
        return entry.finished();
    case Filter::Favorites:
        return entry.favorite;
    }
    return true;
}

const LibraryEntry* LibraryModel::entryFor(const QString& slug) const {
    const auto it = std::ranges::find(entries_, slug, &LibraryEntry::slug);
    return it == entries_.end() ? nullptr : &*it;
}

void LibraryModel::setFilter(Filter filter) {
    // A saved value from another version falls back to the whole library.
    if (filter < Filter::All || filter > Filter::Favorites) {
        filter = Filter::All;
    }
    if (filter_ == filter) {
        return;
    }
    filter_ = filter;
    emit filterChanged();
    rebuildRows();
    rebuildShelves();
}

void LibraryModel::setOnDiskOnly(bool on_disk_only) {
    if (on_disk_only_ == on_disk_only) {
        return;
    }
    on_disk_only_ = on_disk_only;
    emit filterChanged();
    rebuildRows();
    rebuildShelves();
}

void LibraryModel::setGrouped(bool grouped) {
    if (grouped_ == grouped) {
        return;
    }
    grouped_ = grouped;
    emit filterChanged();
    rebuildRows();
    rebuildShelves();
}

QList<int> LibraryModel::filterCounts() const {
    QList<int> counts(static_cast<int>(Filter::Favorites) + 1, 0);
    for (const auto& entry : entries_) {
        if (entry.archived || (on_disk_only_ && entry.downloadedEpisodes == 0)) {
            continue;
        }
        ++counts[static_cast<int>(Filter::All)];
        counts[static_cast<int>(Filter::Watching)] += started(entry) && !entry.finished();
        counts[static_cast<int>(Filter::Airing)] += entry.status == api::AnimeStatus::Airing;
        counts[static_cast<int>(Filter::NotStarted)] += !started(entry);
        counts[static_cast<int>(Filter::Watched)] += entry.finished();
        counts[static_cast<int>(Filter::Favorites)] += entry.favorite;
    }
    return counts;
}

// A franchise shows its anime when the user unfolded it, or when a search or a filter narrows
// the list: then only the ones that pass are under it.
bool LibraryModel::unfolded(qint64 id) const {
    return expanded_.contains(id) || !search_.isEmpty() || filter_ != Filter::All ||
           on_disk_only_;
}

// Titles sort by their first letter or digit: "[Oshi no Ko]" goes with the O.
QString LibraryModel::sortKey(const QString& title) {
    const auto first = std::ranges::find_if(title, [](QChar c) { return c.isLetterOrNumber(); });
    return first == title.end() ? title : title.sliced(first - title.begin());
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
    auto entries = library_.repository().entries(true);
    if (!entries) {
        qWarning("library list: %s", qPrintable(entries.error().message));
        return;
    }
    entries_ = std::move(*entries);
    if (auto groups = library_.repository().franchises()) {
        franchises_ = std::move(*groups);
    }
    archived_count_ = 0;
    total_ = 0;
    for (const auto& entry : entries_) {
        archived_count_ += entry.archived ? 1 : 0;
        total_ += entry.archived == archived_only_ ? 1 : 0;
    }
    QSet<QString> slugs;
    for (const auto& entry : entries_) {
        slugs.insert(entry.slug);
    }
    const bool membership = slugs != slugs_;
    slugs_ = std::move(slugs);
    rebuildRows();
    rebuildShelves();
    rebuildFranchise();
    if (membership) {
        emit membershipChanged();
    }
}

void LibraryModel::rebuildShelves() {
    QList<const LibraryEntry*> shown;
    for (const auto& entry : entries_) {
        if (entry.archived == archived_only_ && passes(entry)) {
            shown.append(&entry);
        }
    }

    // What goes on first: new episodes, then the last watched, then what airs.
    const auto sooner = [](const LibraryEntry* a, const LibraryEntry* b) {
        if ((a->newEpisodes > 0) != (b->newEpisodes > 0)) {
            return a->newEpisodes > 0;
        }
        if (a->lastActivityAt != b->lastActivityAt) {
            return a->lastActivityAt > b->lastActivityAt;
        }
        const bool a_airs = a->status == api::AnimeStatus::Airing;
        if (a_airs != (b->status == api::AnimeStatus::Airing)) {
            return a_airs;
        }
        return QString::localeAwareCompare(a->title, b->title) < 0;
    };
    QList<const LibraryEntry*> watchable;
    QList<std::pair<const LibraryEntry*, QList<double>>> missing;
    if (!archived_only_) {
        for (const auto& entry : entries_) {
            if (entry.archived) {
                continue;
            }
            if (ready(entry)) {
                watchable.append(&entry);
            }
            if (auto pending = pendingOf(entry); !pending.isEmpty()) {
                missing.append({&entry, std::move(pending)});
            }
        }
    }
    std::ranges::sort(watchable, sooner);
    std::ranges::sort(missing, sooner, &decltype(missing)::value_type::first);
    ready_count_ = static_cast<int>(watchable.size());
    download_count_ = static_cast<int>(missing.size());

    // The first one to watch is featured; the shelf goes on with the rest.
    if (const auto* featured = watchable.value(0)) {
        featured_slug_ = featured->slug;
        featured_on_disk_ = featured->resumeOnDisk;
    } else {
        featured_slug_.clear();
        featured_on_disk_ = false;
    }
    updateFeatured();

    QList<Card> ready_cards;
    for (const auto* entry : watchable | std::views::drop(1)) {
        ready_cards.append({.slug = entry->slug,
                            .title = entry->title,
                            .posterUrl = entry->posterUrl,
                            .caption = readyCaption(*entry),
                            .progress = progressOf(*entry),
                            .episode = *entry->resumeNumber,
                            .fresh = entry->newEpisodes > 0,
                            .action = QStringLiteral("play")});
    }
    ready_shelf_.setCards(std::move(ready_cards));

    QList<Card> download_cards;
    pending_count_ = 0;
    for (const auto& [entry, pending] : missing) {
        pending_count_ += static_cast<int>(pending.size());
        download_cards.append({.slug = entry->slug,
                               .title = entry->title,
                               .posterUrl = entry->posterUrl,
                               .caption = pendingCaption(pending),
                               .progress = progressOf(*entry),
                               .episode = pending.first(),
                               .fresh = entry->newEpisodes > 0,
                               .action = QStringLiteral("download")});
    }
    download_shelf_.setCards(std::move(download_cards));

    QList<Card> all_cards;
    for (const auto* entry : shown) {
        all_cards.append({.slug = entry->slug,
                          .title = entry->title,
                          .posterUrl = entry->posterUrl,
                          .caption = captionOf(*entry),
                          .progress = progressOf(*entry),
                          .fresh = entry->newEpisodes > 0});
    }
    all_shelf_.setCards(std::move(all_cards));
    emit countChanged();

    // The home's agenda is not the list's: every anime of the library that airs.
    QList<const LibraryEntry*> library;
    for (const auto& entry : entries_) {
        if (!entry.archived) {
            library.append(&entry);
        }
    }
    rebuildUpcoming(library);
}

QString LibraryModel::downloadPendingOf(const QString& slug) {
    const auto* entry = entryFor(slug);
    const auto pending = entry ? pendingOf(*entry) : QList<double>{};
    if (pending.isEmpty()) {
        return {};
    }
    const auto queued =
        downloads_.enqueueMany(slug, pending, entry->title, downloads::TaskOrigin::Manual);
    return queued ? QString() : queued.error();
}

QString LibraryModel::downloadPending() {
    QStringList failed;
    for (const auto& entry : entries_) {
        if (entry.archived) {
            continue;
        }
        const auto pending = pendingOf(entry);
        if (pending.isEmpty()) {
            continue;
        }
        const auto queued =
            downloads_.enqueueMany(entry.slug, pending, entry.title, downloads::TaskOrigin::Manual);
        if (!queued) {
            failed.append(queued.error());
        }
    }
    failed.removeDuplicates();
    return failed.join(QLatin1Char('\n'));
}

// The next episode of each library anime that airs weekly, soonest first; late ones last.
void LibraryModel::rebuildUpcoming(const QList<const LibraryEntry*>& shown) {
    const auto now = QDateTime::currentDateTimeUtc();
    struct Upcoming {
        const LibraryEntry* entry;
        schedule::Next next;
    };
    QList<Upcoming> upcoming;
    for (const auto& slot : schedule_) {
        const auto next = schedule::next(slot, now);
        if (!next) {
            continue;
        }
        const auto it = std::ranges::find(shown, slot.anime.slug, &LibraryEntry::slug);
        if (it == shown.end()) {
            continue;
        }
        // A copy from the cache cannot tell a late episode from one already out.
        if (next->late && schedule_stale_) {
            continue;
        }
        upcoming.append({.entry = *it, .next = *next});
    }
    std::ranges::sort(upcoming, [](const Upcoming& a, const Upcoming& b) {
        return a.next.late != b.next.late ? b.next.late : a.next.at < b.next.at;
    });
    // One column per day, in the order they come: [{day, items: [{slug, title, posterUrl,
    // caption}]}].
    QVariantList days;
    for (const auto& item : upcoming) {
        const auto day = schedule::day(item.next, now);
        if (days.isEmpty() || days.last().toMap().value(QStringLiteral("day")) != day) {
            days.append(QVariantMap{{QStringLiteral("day"), day},
                                    {QStringLiteral("items"), QVariantList{}}});
        }
        auto column = days.last().toMap();
        auto items = column.value(QStringLiteral("items")).toList();
        items.append(QVariantMap{{QStringLiteral("slug"), item.entry->slug},
                                 {QStringLiteral("title"), item.entry->title},
                                 {QStringLiteral("posterUrl"), item.entry->posterUrl},
                                 {QStringLiteral("caption"), schedule::slot(item.next, now)}});
        column.insert(QStringLiteral("items"), items);
        days.last() = column;
    }
    upcoming_count_ = static_cast<int>(upcoming.size());
    if (days != upcoming_days_) {
        upcoming_days_ = std::move(days);
        emit upcomingChanged();
    }
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
            action_text = started(*entry) ? QStringLiteral("Continuar") : QStringLiteral("Empezar");
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
    downloading_.clear();
    for (const auto& task : downloads_.tasks()) {
        if (task.state != downloads::TaskState::Completed &&
            task.state != downloads::TaskState::Cancelled &&
            task.state != downloads::TaskState::Failed) {
            downloading_.insert(task.slug);
        }
    }
    QList<Row> rows;
    const auto matches = [this](const LibraryEntry& entry) {
        return entry.archived == archived_only_ && passes(entry) &&
               (search_.isEmpty() || entry.title.contains(search_, Qt::CaseInsensitive) ||
                entry.alternativeTitle.contains(search_, Qt::CaseInsensitive));
    };
    QSet<QString> in_group;
    if (grouped_) {
        for (const auto& group : franchises_) {
            QList<int> members;
            const bool group_matches = group.title.contains(search_, Qt::CaseInsensitive);
            for (int i = 0; i < entries_.size(); ++i) {
                const auto& entry = entries_[i];
                if (!group.members.contains(entry.slug))
                    continue;
                in_group.insert(entry.slug);
                if (entry.archived == archived_only_ && passes(entry) &&
                    (group_matches || matches(entry)))
                    members.append(i);
            }
            if (members.isEmpty())
                continue;
            rows.append({-1, group.id, 0});
        }
    }
    for (int i = 0; i < entries_.size(); ++i) {
        if (matches(entries_[i]) && !in_group.contains(entries_[i].slug))
            rows.append({i, 0, 0});
    }
    const auto title = [this](const Row& row) {
        if (row.entry >= 0)
            return sortKey(entries_[row.entry].title);
        const auto it = std::ranges::find(franchises_, row.group, &library::FranchiseRecord::id);
        return it == franchises_.end() ? QString() : sortKey(it->title);
    };
    std::ranges::stable_sort(rows, [&](const Row& a, const Row& b) {
        return QString::localeAwareCompare(title(a), title(b)) < 0;
    });
    if (grouped_) {
        for (int row = 0; row < rows.size(); ++row) {
            const auto id = rows[row].group;
            if (!id || !unfolded(id))
                continue;
            const auto it = std::ranges::find(franchises_, id, &library::FranchiseRecord::id);
            if (it == franchises_.end())
                continue;
            QList<int> members;
            for (int i = 0; i < entries_.size(); ++i) {
                const auto& entry = entries_[i];
                if (it->members.contains(entry.slug) && entry.archived == archived_only_ &&
                    passes(entry) &&
                    (it->title.contains(search_, Qt::CaseInsensitive) || matches(entry)))
                    members.append(i);
            }
            std::ranges::stable_sort(members, [this](int a, int b) {
                const auto ay = entries_[a].startYear.value_or(9999);
                const auto by = entries_[b].startYear.value_or(9999);
                return ay == by
                           ? QString::localeAwareCompare(entries_[a].title, entries_[b].title) < 0
                           : ay < by;
            });
            for (int member : members)
                rows.insert(++row, {member, id, 1});
        }
    }
    overview_.clear();
    for (const auto& row : rows) {
        if (row.depth)
            continue;
        if (row.entry >= 0) {
            auto card = workMap(entries_[row.entry]);
            card.insert(QStringLiteral("groupId"), 0);
            overview_.append(card);
            continue;
        }
        const auto group = std::ranges::find(franchises_, row.group, &library::FranchiseRecord::id);
        if (group == franchises_.end())
            continue;
        QUrl poster;
        int count = 0;
        for (const auto& entry : entries_) {
            if (group->members.contains(entry.slug) && entry.archived == archived_only_) {
                if (poster.isEmpty())
                    poster = entry.posterUrl;
                ++count;
            }
        }
        overview_.append(QVariantMap{
            {QStringLiteral("groupId"), group->id},
            {QStringLiteral("slug"), QString()},
            {QStringLiteral("title"), group->title},
            {QStringLiteral("posterUrl"), poster},
            {QStringLiteral("caption"),
             count == 1 ? QStringLiteral("1 entrega") : QStringLiteral("%1 entregas").arg(count)},
            {QStringLiteral("progress"), -1.0}});
    }
    // The same rows keep the list's scroll and focus; only what they show may have changed.
    if (rows == rows_) {
        if (!rows_.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        }
    } else {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
    }
    emit countChanged();
}

int LibraryModel::indexOf(const QString& slug) const {
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_[row].entry >= 0 && entries_[rows_[row].entry].slug == slug) {
            return row;
        }
    }
    return -1;
}

QString LibraryModel::slugAt(int row) const {
    return row >= 0 && row < rows_.size() && rows_[row].entry >= 0 ? entries_[rows_[row].entry].slug
                                                                   : QString();
}

bool LibraryModel::contains(const QString& slug) const {
    return slugs_.contains(slug);
}

int LibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_[index.row()];
    if (role == GroupIdRole)
        return row.group;
    if (role == IsGroupRole)
        return row.entry < 0;
    if (role == DepthRole)
        return row.depth;
    if (role == ExpandedRole)
        return unfolded(row.group);
    if (row.entry < 0 && (role == ReadyRole || role == DownloadingRole)) {
        const auto it = std::ranges::find(franchises_, row.group, &library::FranchiseRecord::id);
        return it != franchises_.end() &&
               std::ranges::any_of(entries_, [&](const LibraryEntry& member) {
                   return it->members.contains(member.slug) &&
                          (role == ReadyRole ? member.unwatchedOnDisk > 0
                                             : downloading_.contains(member.slug));
               });
    }
    if (row.entry < 0) {
        const auto it = std::ranges::find(franchises_, row.group, &library::FranchiseRecord::id);
        if (it == franchises_.end())
            return {};
        const LibraryEntry* first = nullptr;
        int count = 0;
        for (const auto& item : entries_) {
            if (it->members.contains(item.slug) && item.archived == archived_only_) {
                if (!first)
                    first = &item;
                ++count;
            }
        }
        switch (role) {
        case TitleRole:
        case Qt::DisplayRole:
            return it->title;
        case PosterRole:
            return first ? first->posterUrl : QUrl();
        case CaptionRole:
            return count == 1 ? QStringLiteral("1 entrega")
                              : QStringLiteral("%1 entregas").arg(count);
        case ProgressRole:
            return -1.0;
        case FreshRole:
        case FavoriteRole:
            return false;
        case SlugRole:
            return QString();
        default:
            return {};
        }
    }
    const auto& entry = entries_[row.entry];
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
    case ReadyRole:
        return entry.unwatchedOnDisk > 0;
    case DownloadingRole:
        return downloading_.contains(entry.slug);
    default:
        return {};
    }
}

void LibraryModel::setArchivedOnly(bool archived) {
    if (archived_only_ == archived)
        return;
    archived_only_ = archived;
    emit archivedOnlyChanged();
    reload();
}

int LibraryModel::indexOfGroup(qint64 id) const {
    for (int row = 0; row < rows_.size(); ++row)
        if (rows_[row].entry < 0 && rows_[row].group == id)
            return row;
    return -1;
}

qint64 LibraryModel::groupAt(int row) const {
    return row >= 0 && row < rows_.size() && rows_[row].entry < 0 ? rows_[row].group : 0;
}

void LibraryModel::toggleGroup(qint64 id) {
    if (expanded_.contains(id))
        expanded_.remove(id);
    else
        expanded_.insert(id);
    rebuildRows();
}

QVariantMap LibraryModel::workMap(const LibraryEntry& entry) const {
    const bool single =
        entry.episodeCount.value_or(0) == 1 || (!entry.episodeCount && entry.knownEpisodes == 1 &&
                                                entry.status == api::AnimeStatus::Finished);
    return {{QStringLiteral("slug"), entry.slug},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("posterUrl"), entry.posterUrl},
            {QStringLiteral("caption"), captionOf(entry)},
            {QStringLiteral("progress"), progressOf(entry)},
            {QStringLiteral("archived"), entry.archived},
            {QStringLiteral("bytesText"),
             entry.downloadedBytes > 0 ? display::bytes(entry.downloadedBytes) : QString()},
            {QStringLiteral("metadata"),
             entry.category +
                 (entry.startYear ? QStringLiteral(" · %1").arg(*entry.startYear) : QString())},
            {QStringLiteral("diskText"),
             entry.downloadedEpisodes > 0
                 ? (single ? QStringLiteral("En disco")
                           : QStringLiteral("%1 episodios en disco").arg(entry.downloadedEpisodes))
                 : QStringLiteral("Sin archivos locales")}};
}

void LibraryModel::openFranchise(qint64 id) {
    selected_franchise_ = id;
    rebuildFranchise();
}

void LibraryModel::rebuildFranchise() {
    QVariantMap value;
    const auto it =
        std::ranges::find(franchises_, selected_franchise_, &library::FranchiseRecord::id);
    if (it != franchises_.end()) {
        QVariantList works;
        QStringList slugs;
        QUrl backdrop;
        int files = 0;
        QList<const LibraryEntry*> members;
        for (const auto& entry : entries_) {
            if (it->members.contains(entry.slug) && entry.archived == archived_only_)
                members.append(&entry);
        }
        std::ranges::stable_sort(members, [](const LibraryEntry* a, const LibraryEntry* b) {
            const auto ay = a->startYear.value_or(9999);
            const auto by = b->startYear.value_or(9999);
            return ay == by ? QString::localeAwareCompare(a->title, b->title) < 0 : ay < by;
        });
        for (const auto* member : members) {
            const auto& entry = *member;
            {
                works.append(workMap(entry));
                if (backdrop.isEmpty())
                    backdrop = entry.backdropUrl;
                files += entry.downloadedEpisodes;
                slugs.append(entry.slug);
            }
        }
        value = {{QStringLiteral("id"), it->id},
                 {QStringLiteral("title"), it->title},
                 {QStringLiteral("works"), works},
                 {QStringLiteral("slugs"), slugs},
                 {QStringLiteral("backdropUrl"), backdrop},
                 {QStringLiteral("files"), files}};
    }
    if (value != franchise_) {
        franchise_ = std::move(value);
        emit franchiseChanged();
    }
}

bool LibraryModel::report(const library::Stored<void>& result) {
    const auto error = result ? QString() : result.error().message;
    if (error_ != error) {
        error_ = error;
        emit errorChanged();
    }
    return result.has_value();
}

bool LibraryModel::archive(const QStringList& slugs, bool archived) {
    for (const auto& slug : slugs) {
        if (!report(library_.archive(slug, archived))) {
            reload();
            return false;
        }
    }
    reload();
    return true;
}

bool LibraryModel::removePersonal(const QStringList& slugs) {
    for (const auto& slug : slugs) {
        if (!report(library_.remove(slug))) {
            reload();
            return false;
        }
    }
    reload();
    return true;
}

bool LibraryModel::setFranchiseMember(qint64 id, const QString& slug, bool included) {
    if (!report(library_.repository().setFranchiseMember(id, slug, included)))
        return false;
    library_.announce(slug);
    reload();
    return true;
}

QVariantList LibraryModel::memberChoices(qint64 id) const {
    const auto it = std::ranges::find(franchises_, id, &library::FranchiseRecord::id);
    QVariantList choices;
    for (const auto& entry : entries_) {
        if (it != franchises_.end() && it->members.contains(entry.slug))
            continue;
        choices.append(workMap(entry));
    }
    return choices;
}

bool LibraryModel::createFranchise(const QString& title, const QString& slug) {
    if (title.trimmed().isEmpty() || !contains(slug))
        return false;
    auto created = library_.repository().createFranchise(title.trimmed(), slug);
    if (!created)
        return report(std::unexpected(created.error()));
    report({});
    library_.announce(slug);
    reload();
    openFranchise(*created);
    return true;
}

QVariantList LibraryModel::pendingCandidates() const {
    QVariantList values;
    const auto pending = library_.repository().pendingFranchiseCandidates();
    if (!pending)
        return values;
    for (const auto& candidate : *pending) {
        QVariantList standalone;
        QStringList titles;
        for (const auto& slug : candidate.standaloneSlugs) {
            if (const auto* entry = entryFor(slug)) {
                standalone.append(workMap(*entry));
                titles.append(entry->title);
            }
        }
        const auto anime = library_.repository().anime(candidate.slug);
        const auto reason =
            candidate.reason == QStringLiteral("needs_group")
                ? QStringLiteral("Relacionada con %1, sin franquicia en tu biblioteca.")
                      .arg(titles.join(QStringLiteral(", ")))
            : candidate.franchiseTitles.isEmpty()
                ? QStringLiteral("Relacionada con varias franquicias de tu biblioteca.")
                : QStringLiteral("Relacionada con las franquicias %1.")
                      .arg(candidate.franchiseTitles.join(QStringLiteral(", ")));
        values.append(QVariantMap{
            {QStringLiteral("slug"), candidate.slug},
            {QStringLiteral("title"), candidate.title},
            {QStringLiteral("reason"), candidate.reason},
            {QStringLiteral("reasonText"), reason},
            {QStringLiteral("posterUrl"), anime && *anime ? (*anime)->posterUrl : QUrl()},
            {QStringLiteral("inLibrary"), anime && *anime && (*anime)->inLibrary},
            {QStringLiteral("standaloneWorks"), standalone}});
    }
    return values;
}

QVariantList LibraryModel::franchiseChoices() const {
    QVariantList values;
    for (const auto& group : franchises_) {
        bool archived = false;
        bool active = false;
        for (const auto& entry : entries_) {
            if (!group.members.contains(entry.slug))
                continue;
            archived |= entry.archived;
            active |= !entry.archived;
        }
        values.append(QVariantMap{{QStringLiteral("id"), group.id},
                                  {QStringLiteral("title"), group.title},
                                  {QStringLiteral("archivedOnly"), archived && !active}});
    }
    return values;
}

bool LibraryModel::resolveCandidate(const QString& slug, qint64 id) {
    if (!report(library_.repository().resolveFranchiseCandidate(slug, id)))
        return false;
    library_.announce(slug);
    reload();
    return true;
}

bool LibraryModel::createCandidateFranchise(const QString& slug, const QString& title) {
    const auto created = library_.repository().resolveFranchiseCandidate(slug, title.trimmed());
    if (!created)
        return report(std::unexpected(created.error()));
    report({});
    library_.announce(slug);
    reload();
    openFranchise(*created);
    return true;
}

bool LibraryModel::dismissCandidate(const QString& slug) {
    if (!report(library_.repository().dismissFranchiseCandidate(slug)))
        return false;
    library_.announce(slug);
    reload();
    return true;
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{SlugRole, "slug"},         {TitleRole, "title"},       {PosterRole, "posterUrl"},
            {CaptionRole, "caption"},   {ProgressRole, "progress"}, {FreshRole, "fresh"},
            {FavoriteRole, "favorite"}, {GroupIdRole, "groupId"},   {IsGroupRole, "isGroup"},
            {DepthRole, "depth"},       {ExpandedRole, "expanded"}, {ReadyRole, "ready"},
            {DownloadingRole, "downloading"}};
}

} // namespace animehub::app
