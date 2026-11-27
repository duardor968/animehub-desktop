#include "library_model.h"

#include "display.h"
#include "schedule.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace animehub::app {
namespace {

using library::LibraryEntry;

QString formatGenres(const QString& raw) {
    if (raw.isEmpty()) {
        return {};
    }
    QStringList parts;
    for (const auto& item : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const auto trimmed = item.trimmed();
        if (!trimmed.isEmpty()) {
            parts.append(trimmed);
            if (parts.size() == 3) {
                break;
            }
        }
    }
    return parts.join(QStringLiteral(" · "));
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

    // A queued episode is no longer missing; a removed one is again.
    rebuild_timer_.setSingleShot(true);
    rebuild_timer_.setInterval(40);
    connect(&rebuild_timer_, &QTimer::timeout, this, [this] {
        rebuildRows();
        rebuildShelves();
        rebuildFranchise();
    });
    for (const auto signal :
         {&downloads::DownloadManager::taskAdded, &downloads::DownloadManager::taskRemoved}) {
        connect(&downloads_, signal, &rebuild_timer_, qOverload<>(&QTimer::start));
    }
    connect(&downloads_, &downloads::DownloadManager::loaded, &rebuild_timer_,
            qOverload<>(&QTimer::start));
    // The open franchise's button follows its download (paused, failed, done), wherever it is
    // changed from.
    franchise_timer_.setSingleShot(true);
    franchise_timer_.setInterval(300);
    connect(&franchise_timer_, &QTimer::timeout, this, &LibraryModel::rebuildFranchise);
    connect(&downloads_, &downloads::DownloadManager::taskChanged, this, [this](qint64 id) {
        const auto task = downloads_.task(id);
        const auto* group = franchiseFor(selected_franchise_);
        if (task && group && group->members.contains(task->slug)) {
            franchise_timer_.start();
        }
    });

    schedule_timer_.setInterval(std::chrono::minutes(30));
    connect(&schedule_timer_, &QTimer::timeout, this, &LibraryModel::fetchSchedule);
    // "Hoy", "Próximo", "Retrasado" and the local clock depend on wall-clock time.
    clock_timer_.setInterval(std::chrono::seconds(30));
    connect(&clock_timer_, &QTimer::timeout, this, &LibraryModel::tickClock);
    updateClock(QDateTime::currentDateTimeUtc(), false);
    reload();
}

void LibraryModel::start() {
    started_ = true;
    fetchSchedule();
    schedule_timer_.start();
    if (window_shown_) {
        clock_timer_.start();
    }
}

void LibraryModel::setWindowShown(bool shown) {
    if (window_shown_ == shown) {
        return;
    }
    window_shown_ = shown;
    if (!window_shown_) {
        clock_timer_.stop();
    } else {
        tickClock();
        if (started_) {
            clock_timer_.start();
        }
    }
    emit windowShownChanged();
}

// The saved copy paints the shelf at once; the network answer replaces it.
void LibraryModel::fetchSchedule() {
    schedule_fetch_elapsed_.restart();
    api_.schedule(
        this,
        [this](const api::Result<api::Schedule>& result) {
            if (!result) {
                return; // the shelf keeps what it had; offline it may have nothing to show
            }
            schedule_ = result->value.entries;
            const bool stale = (result->origin == api::Origin::Cache) || result->value.stale;
            const bool meta_changed = (schedule_stale_ != stale) || !schedule_loaded_;
            schedule_stale_ = stale;
            schedule_loaded_ = true;
            rebuildSchedule(meta_changed);
        },
        api::CachePolicy::CacheThenNetwork);
}

void LibraryModel::refreshSchedule() {
    tickClock();
    constexpr qint64 kRefreshThrottleMs = 30'000;
    if (schedule_fetch_elapsed_.isValid() &&
        schedule_fetch_elapsed_.elapsed() < kRefreshThrottleMs) {
        return;
    }
    fetchSchedule();
}

// A watched episode or one in progress.
bool LibraryModel::started(const LibraryEntry& entry) {
    return entry.watchedEpisodes > 0 || entry.resumePositionMs > 0;
}

// What the user keeps up with: a series begun and not finished, or one still running (airing,
// or paused between seasons).
bool LibraryModel::following(const LibraryEntry& entry) {
    return (started(entry) && !entry.finished()) || entry.status == api::AnimeStatus::Airing ||
           entry.status == api::AnimeStatus::Paused;
}

// "Para ver", on the home and as a filter: the episode it goes on with is on disk, so its card
// plays it.
bool LibraryModel::toWatch(const LibraryEntry& entry) {
    return entry.resumeNumber && entry.resumeOnDisk;
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

// Every card of the home reads the same way: the episode it is about, then how far the series
// is. " · 3/12 vistos", or nothing before the first one is watched.
QString LibraryModel::seenCaption(const LibraryEntry& entry) {
    const int total = std::max(entry.episodeCount.value_or(0), entry.knownEpisodes);
    return entry.watchedEpisodes > 0 && total > 0
               ? QStringLiteral(" · %1/%2 vistos").arg(entry.watchedEpisodes).arg(total)
               : QString();
}

// "Ep. 6 · 3/12 vistos", "Ep. 6 · 8:12 de 24:00" while it is under way.
QString LibraryModel::readyCaption(const LibraryEntry& entry) {
    auto caption = display::shortEpisode(*entry.resumeNumber);
    if (entry.resumePositionMs > 0) {
        caption += QStringLiteral(" · ") + display::clock(entry.resumePositionMs);
        if (entry.resumeDurationMs > 0) {
            caption += QStringLiteral(" de ") + display::clock(entry.resumeDurationMs);
        }
        return caption;
    }
    return caption + seenCaption(entry);
}

// "Ep. 4 · 3/12 vistos", "Ep. 4 a 7 · 3/12 vistos", "5 episodios"
QString LibraryModel::pendingCaption(const LibraryEntry& entry, const QList<double>& pending) {
    if (pending.size() == 1) {
        return display::shortEpisode(pending.first()) + seenCaption(entry);
    }
    const bool run =
        pending.last() - pending.first() == static_cast<double>(pending.size() - 1) &&
        std::ranges::all_of(pending, [](double number) { return number == std::trunc(number); });
    return run ? display::shortEpisode(pending.first()) + QStringLiteral(" a ") +
                     display::episodeNumber(pending.last()) + seenCaption(entry)
               : QStringLiteral("%1 episodios").arg(pending.size()) + seenCaption(entry);
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
    case Filter::ToWatch:
        return toWatch(entry);
    }
    return true;
}

bool LibraryModel::named(const QString& title) const {
    return !search_.isEmpty() && title.contains(search_, Qt::CaseInsensitive);
}

bool LibraryModel::shown(const LibraryEntry& entry, bool franchise_named) const {
    if (entry.archived != archived_only_) {
        return false;
    }
    if (search_.isEmpty()) {
        return passes(entry);
    }
    return franchise_named || named(entry.title) || named(entry.alternativeTitle);
}

const LibraryEntry* LibraryModel::entryFor(const QString& slug) const {
    const auto it = entry_index_.constFind(slug);
    return it == entry_index_.cend() ? nullptr : &entries_[*it];
}

const library::FranchiseRecord* LibraryModel::franchiseFor(qint64 id) const {
    const auto it = franchise_index_.constFind(id);
    return it == franchise_index_.cend() ? nullptr : &franchises_[*it];
}

void LibraryModel::setFilter(Filter filter) {
    // A saved value from another version falls back to the whole library.
    if (filter < Filter::All || filter > Filter::ToWatch) {
        filter = Filter::All;
    }
    if (filter_ == filter) {
        return;
    }
    filter_ = filter;
    collapsed_filtered_.clear();
    emit filterChanged();
    rebuildRows();
    rebuildShelves();
}

void LibraryModel::setOnDiskOnly(bool on_disk_only) {
    if (on_disk_only_ == on_disk_only) {
        return;
    }
    on_disk_only_ = on_disk_only;
    collapsed_filtered_.clear();
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
    QList<int> counts(static_cast<int>(Filter::ToWatch) + 1, 0);
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
        counts[static_cast<int>(Filter::ToWatch)] += toWatch(entry);
    }
    return counts;
}

// A franchise defaults to folded under "Todos" (expanded by explicit user toggle) and unfolded
// under any active filter or search, while still letting the user fold individual groups back.
bool LibraryModel::unfolded(qint64 id) const {
    return filterActive() ? !collapsed_filtered_.contains(id) : expanded_.contains(id);
}

// Under its franchise, a work says what sets it apart: "Part 2" under "Chitose-kun wa Ramune
// Bin no Naka". One that only repeats the franchise, or does not start with it, stays whole.
QString LibraryModel::distinctTitle(const QString& title, const QString& franchise) {
    // Letter by letter, punctuation aside: "Re:Zero kara…" starts with "ReZero kara…".
    qsizetype at = 0;
    for (const auto c : franchise) {
        if (!c.isLetterOrNumber()) {
            continue;
        }
        while (at < title.size() && !title[at].isLetterOrNumber()) {
            ++at;
        }
        if (at == title.size() || title[at].toCaseFolded() != c.toCaseFolded()) {
            return title;
        }
        ++at;
    }
    while (at < title.size() && !title[at].isLetterOrNumber()) {
        ++at;
    }
    const auto rest = title.sliced(at);
    // "…The Animation", "… TV" say nothing of their own, and "II" alone reads as a mistake: those
    // keep their whole name.
    static const QStringList kGeneric{QStringLiteral("the"), QStringLiteral("animation"),
                                      QStringLiteral("tv"), QStringLiteral("anime")};
    const auto words = rest.toLower().split(u' ', Qt::SkipEmptyParts);
    const bool generic =
        std::ranges::all_of(words, [](const QString& word) { return kGeneric.contains(word); });
    return rest.size() < 4 || generic ? title : rest;
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
    collapsed_filtered_.clear();
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
    QHash<QString, int> next_entry_index;
    next_entry_index.reserve(entries_.size());
    for (int i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        archived_count_ += entry.archived ? 1 : 0;
        total_ += entry.archived == archived_only_ ? 1 : 0;
        next_entry_index.insert(entry.slug, i);
    }
    bool membership = (next_entry_index.size() != entry_index_.size());
    if (!membership) {
        for (const auto& entry : entries_) {
            if (!entry_index_.contains(entry.slug)) {
                membership = true;
                break;
            }
        }
    }
    entry_index_ = std::move(next_entry_index);
    franchise_index_.clear();
    franchise_index_.reserve(franchises_.size());
    member_franchise_index_.clear();
    for (int i = 0; i < franchises_.size(); ++i) {
        franchise_index_.insert(franchises_[i].id, i);
        for (const auto& slug : franchises_[i].members) {
            if (!member_franchise_index_.contains(slug)) {
                member_franchise_index_.insert(slug, i);
            }
        }
    }
    rebuildRows();
    rebuildShelves();
    rebuildFranchise();
    if (membership) {
        emit membershipChanged();
    }
}

void LibraryModel::rebuildShelves() {
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
            if (toWatch(entry)) {
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
    featured_slug_ = watchable.isEmpty() ? QString() : watchable.first()->slug;
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
                               .caption = pendingCaption(*entry, pending),
                               // How far it is seen belongs to "Para ver", not here.
                               .progress = -1,
                               .episode = pending.first(),
                               .fresh = entry->newEpisodes > 0,
                               .action = QStringLiteral("download")});
    }
    download_shelf_.setCards(std::move(download_cards));
    emit countChanged();

    // The home's agenda and the weekly schedule follow library membership and archive state.
    rebuildSchedule(false);
}

void LibraryModel::updateClock(const QDateTime& now, bool notify) {
    const auto time_text = display::time(now);
    const bool time_changed = (local_time_text_ != time_text);
    local_time_text_ = time_text;
    if (!notify) {
        const auto local_date = now.toLocalTime().date();
        today_weekday_ = local_date.dayOfWeek() - 1;
        today_heading_ = schedule::todayHeading(local_date);
        return;
    }
    if (time_changed) {
        emit clockChanged();
    }
}

void LibraryModel::tickClock() {
    const auto now = QDateTime::currentDateTimeUtc();
    updateClock(now, true);
    rebuildSchedule(false);
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

// Rebuilds both the home's today-only rail (followed, non-archived library anime airing today)
// and the full catalog Monday–Sunday weekly timetable.
void LibraryModel::rebuildSchedule(bool force_emit) {
    const auto now = QDateTime::currentDateTimeUtc();
    const auto local_date = now.toLocalTime().date();
    const int next_today_weekday = local_date.dayOfWeek() - 1;
    const QString next_today_heading = schedule::todayHeading(local_date);

    // 1) Home right rail: strictly today's followed, non-archived library anime.
    struct Upcoming {
        const LibraryEntry* entry;
        schedule::Next next;
    };
    QList<Upcoming> today_entries;
    for (const auto& slot : schedule_) {
        const auto next = schedule::agenda(slot, now);
        if (!next || !schedule::isToday(*next, now)) {
            continue;
        }
        const auto* entry = entryFor(slot.anime.slug);
        if (!entry || entry->archived) {
            continue;
        }
        // A copy from the cache cannot tell a late episode from one already out.
        if (next->late && schedule_stale_) {
            continue;
        }
        today_entries.append({.entry = entry, .next = *next});
    }
    std::ranges::sort(today_entries, [](const Upcoming& a, const Upcoming& b) {
        if (a.next.at != b.next.at) {
            return a.next.at < b.next.at;
        }
        return QString::localeAwareCompare(a.entry->title, b.entry->title) < 0;
    });

    QVariantList upcoming_list;
    upcoming_list.reserve(today_entries.size());
    for (const auto& item : today_entries) {
        const auto slot_text = schedule::slot(item.next, now);
        const auto state = schedule::state(item.next, now);
        const auto badge = schedule::badge(item.next, now);
        const auto time_text = schedule::timeText(item.next, now);
        upcoming_list.append(
            QVariantMap{{QStringLiteral("slug"), item.entry->slug},
                        {QStringLiteral("title"), item.entry->title},
                        {QStringLiteral("posterUrl"), item.entry->posterUrl.toString()},
                        {QStringLiteral("episode"), display::episodeNumber(item.next.number)},
                        {QStringLiteral("episodeNumber"), item.next.number},
                        {QStringLiteral("episodeText"), display::shortEpisode(item.next.number)},
                        {QStringLiteral("time"), time_text},
                        {QStringLiteral("timeText"), time_text},
                        {QStringLiteral("state"), state},
                        {QStringLiteral("stateLabel"), badge},
                        {QStringLiteral("badgeText"), badge},
                        {QStringLiteral("day"), QStringLiteral("Hoy")},
                        {QStringLiteral("caption"), slot_text},
                        {QStringLiteral("slot"), slot_text}});
    }
    QVariantList days;
    if (!upcoming_list.isEmpty()) {
        days.append(QVariantMap{{QStringLiteral("day"), QStringLiteral("Hoy")},
                                {QStringLiteral("isToday"), true},
                                {QStringLiteral("items"), upcoming_list}});
    }

    // 2) Full catalog Monday–Sunday weekly timetable (strictly 0..6, never rotating Today first).
    struct WeeklyItem {
        QString slug;
        QString title;
        QString posterUrl;
        QString backdropUrl;
        schedule::WeeklySlot slot;
        bool inLibrary = false;
    };
    QList<WeeklyItem> weekly_items;
    weekly_items.reserve(schedule_.size());
    for (const auto& entry_slot : schedule_) {
        const auto derived = schedule::weekly(entry_slot, now, schedule_stale_);
        if (!derived) {
            continue;
        }
        const auto* lib = entryFor(entry_slot.anime.slug);
        const auto title = !entry_slot.anime.title.isEmpty()
                               ? entry_slot.anime.title
                               : (lib ? lib->title : entry_slot.anime.slug);
        const auto poster = !entry_slot.anime.posterUrl.isEmpty() ? entry_slot.anime.posterUrl
                                                                  : (lib ? lib->posterUrl : QUrl());
        const auto backdrop = !entry_slot.anime.backdropUrl.isEmpty()
                                  ? entry_slot.anime.backdropUrl
                                  : (lib ? lib->backdropUrl : QUrl());
        weekly_items.append({.slug = entry_slot.anime.slug,
                             .title = title,
                             .posterUrl = poster.toString(),
                             .backdropUrl = backdrop.toString(),
                             .slot = *derived,
                             .inLibrary = lib != nullptr && !lib->archived});
    }
    std::ranges::sort(weekly_items, [](const WeeklyItem& a, const WeeklyItem& b) {
        if (a.slot.weekday != b.slot.weekday) {
            return a.slot.weekday < b.slot.weekday;
        }
        if (a.slot.slotMinutes != b.slot.slotMinutes) {
            return a.slot.slotMinutes < b.slot.slotMinutes;
        }
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });

    QVariantList weekly_list;
    weekly_list.reserve(weekly_items.size());
    for (const auto& item : weekly_items) {
        weekly_list.append(
            QVariantMap{{QStringLiteral("slug"), item.slug},
                        {QStringLiteral("title"), item.title},
                        {QStringLiteral("posterUrl"), item.posterUrl},
                        {QStringLiteral("backdropUrl"), item.backdropUrl},
                        {QStringLiteral("episode"), display::episodeNumber(item.slot.number)},
                        {QStringLiteral("episodeNumber"), item.slot.number},
                        {QStringLiteral("time"), item.slot.time},
                        {QStringLiteral("slotMinutes"), item.slot.slotMinutes},
                        {QStringLiteral("weekday"), item.slot.weekday},
                        {QStringLiteral("isToday"), item.slot.isToday},
                        {QStringLiteral("state"), item.slot.state},
                        {QStringLiteral("stateLabel"), item.slot.stateLabel},
                        {QStringLiteral("inLibrary"), item.inLibrary}});
    }

    const int next_upcoming_count = static_cast<int>(today_entries.size());
    const bool upcoming_changed = (upcoming_list != upcoming_) || (days != upcoming_days_) ||
                                  (next_upcoming_count != upcoming_count_);
    const bool schedule_changed = upcoming_changed || (weekly_list != weekly_schedule_) ||
                                  (next_today_weekday != today_weekday_) ||
                                  (next_today_heading != today_heading_) || force_emit;

    upcoming_ = std::move(upcoming_list);
    upcoming_days_ = std::move(days);
    upcoming_count_ = next_upcoming_count;
    weekly_schedule_ = std::move(weekly_list);
    today_weekday_ = next_today_weekday;
    today_heading_ = next_today_heading;

    if (upcoming_changed || force_emit) {
        emit upcomingChanged();
    }
    if (schedule_changed) {
        emit scheduleChanged();
    }
}

void LibraryModel::updateFeatured() {
    const auto* entry = featured_slug_.isEmpty() ? nullptr : entryFor(featured_slug_);
    QVariantMap featured;
    if (entry && entry->resumeNumber) {
        // The line reads as the posters' beside it; the button only says what it does.
        double progress = -1;
        QString remaining_text;
        if (entry->resumePositionMs > 0 && entry->resumeDurationMs > 0) {
            progress = std::clamp(
                static_cast<double>(entry->resumePositionMs) / entry->resumeDurationMs, 0.0, 1.0);
            if (entry->resumeDurationMs > entry->resumePositionMs) {
                const qint64 secs = (entry->resumeDurationMs - entry->resumePositionMs) / 1000;
                remaining_text = secs < 60 ? QStringLiteral("Queda menos de un minuto")
                                           : QStringLiteral("Quedan ") + display::remaining(secs);
            }
        } else if (entry->resumePositionMs <= 0) {
            progress = progressOf(*entry);
        }
        // It is on disk: the button plays it.
        featured = {{QStringLiteral("slug"), entry->slug},
                    {QStringLiteral("title"), entry->title},
                    {QStringLiteral("backdropUrl"), entry->backdropUrl},
                    {QStringLiteral("posterUrl"), entry->posterUrl},
                    {QStringLiteral("detail"), readyCaption(*entry)},
                    {QStringLiteral("progress"), progress},
                    {QStringLiteral("actionText"),
                     started(*entry) ? QStringLiteral("Continuar") : QStringLiteral("Empezar")},
                    {QStringLiteral("episodeLabel"), display::episode(*entry->resumeNumber)},
                    {QStringLiteral("unwatchedOnDisk"), entry->unwatchedOnDisk},
                    {QStringLiteral("statusText"), display::status(entry->status)},
                    {QStringLiteral("genres"), formatGenres(entry->genres)},
                    {QStringLiteral("hasBackdrop"),
                     entry->backdropUrl.isValid() && !entry->backdropUrl.isEmpty()},
                    {QStringLiteral("remainingText"), remaining_text}};
    }
    if (featured != featured_) {
        featured_ = std::move(featured);
        emit featuredChanged();
    }
}

void LibraryModel::rebuildRows(bool force_groups_emit) {
    QList<Row> rows;
    QSet<QString> in_group;
    int shown_count = 0;
    if (grouped_) {
        for (const auto& group : franchises_) {
            int visible_members = 0;
            const bool group_named = named(group.title);
            for (const auto& slug : group.members) {
                const auto idx = entry_index_.constFind(slug);
                if (idx == entry_index_.cend())
                    continue;
                in_group.insert(slug);
                if (shown(entries_[*idx], group_named))
                    ++visible_members;
            }
            if (visible_members == 0)
                continue;
            shown_count += visible_members;
            rows.append({-1, group.id, 0});
        }
    }
    for (int i = 0; i < entries_.size(); ++i) {
        if (shown(entries_[i], false) && !in_group.contains(entries_[i].slug)) {
            rows.append({i, 0, 0});
            ++shown_count;
        }
    }
    shown_count_ = shown_count;
    const auto title = [this](const Row& row) {
        if (row.entry >= 0)
            return sortKey(entries_[row.entry].title);
        const auto* group = franchiseFor(row.group);
        return group ? sortKey(group->title) : QString();
    };
    std::ranges::stable_sort(rows, [&](const Row& a, const Row& b) {
        return QString::localeAwareCompare(title(a), title(b)) < 0;
    });
    if (grouped_) {
        for (int row = 0; row < rows.size(); ++row) {
            const auto id = rows[row].group;
            if (!id || !unfolded(id))
                continue;
            const auto* group = franchiseFor(id);
            if (!group)
                continue;
            QList<int> members;
            const bool group_named = named(group->title);
            for (const auto& slug : group->members) {
                const auto idx = entry_index_.constFind(slug);
                if (idx != entry_index_.cend() && shown(entries_[*idx], group_named))
                    members.append(*idx);
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
    // Under "Sin empezar" or "Vistos", where the user is says nothing: what the work is does.
    const bool state_said =
        search_.isEmpty() && (filter_ == Filter::NotStarted || filter_ == Filter::Watched);
    for (const auto& row : rows) {
        if (row.depth)
            continue;
        if (row.entry >= 0) {
            auto card = workMap(entries_[row.entry]);
            card.insert(QStringLiteral("groupId"), 0);
            if (state_said)
                card.insert(QStringLiteral("caption"), card.value(QStringLiteral("facts")));
            overview_.append(card);
            continue;
        }
        const auto* group = franchiseFor(row.group);
        if (!group)
            continue;
        QList<int> matched;
        const bool group_named = named(group->title);
        for (const auto& slug : group->members) {
            const auto idx = entry_index_.constFind(slug);
            if (idx != entry_index_.cend() && shown(entries_[*idx], group_named)) {
                matched.append(*idx);
            }
        }
        std::ranges::sort(matched);
        QUrl poster;
        for (const int idx : matched) {
            if (!entries_[idx].posterUrl.isEmpty()) {
                poster = entries_[idx].posterUrl;
                break;
            }
        }
        const auto count = matched.size(); // the works that pass, as the filter counts them
        overview_.append(QVariantMap{
            {QStringLiteral("groupId"), group->id},
            {QStringLiteral("slug"), QString()},
            {QStringLiteral("title"), group->title},
            {QStringLiteral("posterUrl"), poster},
            {QStringLiteral("caption"),
             count == 1 ? QStringLiteral("1 entrega") : QStringLiteral("%1 entregas").arg(count)},
            {QStringLiteral("progress"), -1.0}});
    }
    bool has_groups = false;
    bool all_expanded = true;
    for (const auto& row : rows) {
        if (row.entry < 0 && row.group != 0) {
            has_groups = true;
            if (!unfolded(row.group)) {
                all_expanded = false;
            }
        }
    }
    if (!has_groups) {
        all_expanded = false;
    }
    const bool groups_changed = (has_groups_ != has_groups) || (all_expanded_ != all_expanded);
    has_groups_ = has_groups;
    all_expanded_ = all_expanded;

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
    if (groups_changed || force_groups_emit) {
        emit groupsChanged();
    }
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
    return entry_index_.contains(slug);
}

QVariantMap LibraryModel::franchiseStep(const QString& slug) const {
    const auto it = member_franchise_index_.constFind(slug);
    if (it == member_franchise_index_.cend())
        return {};
    const auto& group = franchises_[*it];
    if (group.members.size() < 2)
        return {};
    // Its letters and digits alone: "Ansatsu Kyoushitsu" names the series "Ansatsu Kyoushitsu".
    const auto letters = [](const QString& text) {
        QString kept;
        for (const auto c : text) {
            if (c.isLetterOrNumber())
                kept.append(c.toCaseFolded());
        }
        return kept;
    };
    const auto* entry = entryFor(slug);
    if (entry && letters(entry->title) == letters(group.title))
        return {};
    return {{QStringLiteral("id"), group.id}, {QStringLiteral("title"), group.title}};
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
    if (row.entry < 0 && role == ReadyRole) {
        const auto* group = franchiseFor(row.group);
        if (!group)
            return false;
        for (const auto& slug : group->members) {
            if (const auto* member = entryFor(slug); member && member->unwatchedOnDisk > 0) {
                return true;
            }
        }
        return false;
    }
    if (row.entry < 0) {
        const auto* group = franchiseFor(row.group);
        if (!group)
            return {};
        switch (role) {
        case TitleRole:
        case Qt::DisplayRole:
            return group->title;
        case PosterRole: {
            const bool group_named = named(group->title);
            int best_idx = -1;
            for (const auto& slug : group->members) {
                const auto idx = entry_index_.constFind(slug);
                if (idx != entry_index_.cend() && shown(entries_[*idx], group_named) &&
                    (best_idx < 0 || *idx < best_idx)) {
                    best_idx = *idx;
                }
            }
            return best_idx >= 0 ? entries_[best_idx].posterUrl : QUrl();
        }
        case CaptionRole: {
            const bool group_named = named(group->title);
            int count = 0; // the works that pass, as the filter counts them
            for (const auto& slug : group->members) {
                const auto idx = entry_index_.constFind(slug);
                if (idx != entry_index_.cend() && shown(entries_[*idx], group_named)) {
                    ++count;
                }
            }
            return count == 1 ? QStringLiteral("1 entrega")
                              : QStringLiteral("%1 entregas").arg(count);
        }
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
        if (row.depth > 0) {
            if (const auto* group = franchiseFor(row.group))
                return distinctTitle(entry.title, group->title);
        }
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
    default:
        return {};
    }
}

void LibraryModel::setArchivedOnly(bool archived) {
    if (archived_only_ == archived)
        return;
    archived_only_ = archived;
    collapsed_filtered_.clear();
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
    auto& set = filterActive() ? collapsed_filtered_ : expanded_;
    if (set.contains(id)) {
        set.remove(id);
    } else {
        set.insert(id);
    }
    rebuildRows(true);
}

void LibraryModel::expandAll() {
    bool mutated = false;
    if (filterActive()) {
        if (!collapsed_filtered_.isEmpty()) {
            collapsed_filtered_.clear();
            mutated = true;
        }
    } else {
        for (const auto& group : franchises_) {
            if (!expanded_.contains(group.id)) {
                expanded_.insert(group.id);
                mutated = true;
            }
        }
    }
    if (!mutated) {
        return;
    }
    rebuildRows(true);
}

void LibraryModel::collapseAll() {
    bool mutated = false;
    if (filterActive()) {
        for (const auto& group : franchises_) {
            if (!collapsed_filtered_.contains(group.id)) {
                collapsed_filtered_.insert(group.id);
                mutated = true;
            }
        }
    } else {
        if (!expanded_.isEmpty()) {
            expanded_.clear();
            mutated = true;
        }
    }
    if (!mutated) {
        return;
    }
    rebuildRows(true);
}

QVariantMap LibraryModel::workMap(const LibraryEntry& entry) const {
    const bool single =
        entry.episodeCount.value_or(0) == 1 || (!entry.episodeCount && entry.knownEpisodes == 1 &&
                                                entry.status == api::AnimeStatus::Finished);
    // "TV · 2012 · 26 ep.": what the work is, short enough for a card.
    QStringList facts;
    if (!entry.category.isEmpty()) {
        facts.append(display::category(entry.category));
    }
    if (entry.startYear) {
        facts.append(QString::number(*entry.startYear));
    }
    if (const int count = std::max(entry.episodeCount.value_or(0), entry.knownEpisodes);
        count > 1) {
        facts.append(QStringLiteral("%1 ep.").arg(count));
    }
    // Where the user is with it: "Siguiente: Ep. 6".
    QString state = QStringLiteral("Sin empezar");
    if (entry.resumeNumber && entry.resumePositionMs > 0) {
        state = QStringLiteral("Continuar: ") + display::shortEpisode(*entry.resumeNumber);
    } else if (entry.resumeNumber && entry.watchedEpisodes > 0) {
        state = QStringLiteral("Siguiente: ") + display::shortEpisode(*entry.resumeNumber);
    } else if (entry.finished()) {
        state = QStringLiteral("Visto");
    } else if (entry.watchedEpisodes > 0) {
        state = QStringLiteral("Al día");
    }
    // The poster's own button, as on the home: play the episode it goes on with, or fetch it.
    QString action;
    if (entry.resumeNumber && !entry.finished()) {
        if (entry.resumeOnDisk) {
            action = QStringLiteral("play");
        } else if (!downloads_.activeTaskFor(entry.slug, *entry.resumeNumber)) {
            action = QStringLiteral("download");
        }
    }
    return {{QStringLiteral("slug"), entry.slug},
            {QStringLiteral("facts"), facts.join(QStringLiteral(" · "))},
            {QStringLiteral("state"), state},
            {QStringLiteral("action"), action},
            {QStringLiteral("fresh"), entry.newEpisodes > 0},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("posterUrl"), entry.posterUrl},
            {QStringLiteral("caption"), captionOf(entry)},
            {QStringLiteral("progress"), progressOf(entry)},
            {QStringLiteral("archived"), entry.archived},
            {QStringLiteral("bytesText"),
             entry.downloadedBytes > 0 ? display::bytes(entry.downloadedBytes) : QString()},
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
    if (const auto* it = franchiseFor(selected_franchise_)) {
        QVariantList works;
        QStringList slugs;
        QUrl backdrop;
        QUrl poster;
        int files = 0;
        int episodes = 0;
        qint64 bytes = 0;
        int started_works = 0;
        int watched = 0;
        std::optional<int> first_year;
        std::optional<int> last_year;
        QList<const LibraryEntry*> members;
        for (const auto& slug : it->members) {
            if (const auto* entry = entryFor(slug); entry && entry->archived == archived_only_) {
                members.append(entry);
            }
        }
        std::ranges::stable_sort(members, [](const LibraryEntry* a, const LibraryEntry* b) {
            const auto ay = a->startYear.value_or(9999);
            const auto by = b->startYear.value_or(9999);
            return ay == by ? QString::localeAwareCompare(a->title, b->title) < 0 : ay < by;
        });
        for (const auto* member : members) {
            const auto& entry = *member;
            auto work = workMap(entry);
            // Under its franchise, a work goes by what sets it apart.
            work.insert(QStringLiteral("shortTitle"), distinctTitle(entry.title, it->title));
            works.append(work);
            if (backdrop.isEmpty())
                backdrop = entry.backdropUrl;
            if (poster.isEmpty())
                poster = entry.posterUrl;
            files += entry.downloadedEpisodes;
            episodes += entry.knownEpisodes;
            bytes += entry.downloadedBytes;
            slugs.append(entry.slug);
            started_works += started(entry) ? 1 : 0;
            watched += entry.watchedEpisodes;
            if (entry.startYear) {
                first_year = std::min(first_year.value_or(*entry.startYear), *entry.startYear);
                last_year = std::max(last_year.value_or(*entry.startYear), *entry.startYear);
            }
        }
        // The work the main button goes on with: the first one under way; else the first one
        // not seen after the last one seen, else before it. All seen, none.
        const auto pending = [](const LibraryEntry* entry) {
            return entry->resumeNumber && !entry->finished();
        };
        const auto under_way = std::ranges::find_if(
            members, [&](const LibraryEntry* entry) { return pending(entry) && started(*entry); });
        const auto last_seen =
            std::ranges::find_if(members | std::views::reverse,
                                 [](const LibraryEntry* entry) { return entry->finished(); });
        const auto after = std::find_if(last_seen.base(), members.end(), pending);
        const auto first = std::ranges::find_if(members, pending);
        const LibraryEntry* next = under_way != members.end() ? *under_way
                                   : after != members.end()   ? *after
                                   : first != members.end()   ? *first
                                                              : nullptr;
        // The years head the page; the rest goes beside the main button.
        QString facts;
        if (first_year) {
            facts = *first_year == *last_year
                        ? QString::number(*first_year)
                        : QStringLiteral("%1–%2").arg(*first_year).arg(*last_year);
        }
        const auto figure = [](const QString& caption, const QString& value,
                               double progress = -1.0) {
            return QVariantMap{{QStringLiteral("caption"), caption},
                               {QStringLiteral("value"), value},
                               {QStringLiteral("progress"), progress},
                               {QStringLiteral("problem"), false}};
        };
        // Beside the main button: which work it goes on with, how many are seen, what is on
        // disk. The button says what a press does with that work's next episode, as an anime's
        // does: watch it, or bring it to disk.
        QVariantList figures;
        QString next_text;
        QString next_action;
        if (next) {
            figures.append(
                figure(QStringLiteral("Entrega"), distinctTitle(next->title, it->title)));
            // A film is not counted in episodes, as its card does not.
            const bool single = next->episodeCount.value_or(0) == 1 ||
                                (!next->episodeCount && next->knownEpisodes == 1 &&
                                 next->status == api::AnimeStatus::Finished);
            const auto number = *next->resumeNumber;
            const auto episode =
                single ? QString() : QStringLiteral(" ep. ") + display::episodeNumber(number);
            const auto download = single ? QStringLiteral(" la descarga") : episode;
            if (next->resumeOnDisk) {
                next_action = QStringLiteral("play");
                next_text = (next->resumePositionMs > 0 ? QStringLiteral("Continuar")
                                                        : QStringLiteral("Ver")) +
                            episode;
            } else if (const auto task = downloads_.activeTaskFor(next->slug, number)) {
                if (task->state == downloads::TaskState::Paused) {
                    next_action = QStringLiteral("resume");
                    next_text = QStringLiteral("Reanudar") + download;
                } else if (task->state == downloads::TaskState::Failed) {
                    next_action = QStringLiteral("retry");
                    next_text = QStringLiteral("Reintentar") + download;
                } else {
                    next_action = QStringLiteral("busy"); // on its way: nothing to press
                    next_text = QStringLiteral("Descargando") + episode;
                }
            } else {
                next_action = QStringLiteral("download");
                next_text = QStringLiteral("Descargar") + episode;
            }
        }
        // Episodes, as an anime's page counts them: "30/52", then "4/52 · 532 MB".
        if (started_works > 0 && episodes > 0) {
            figures.append(figure(QStringLiteral("Vistos"),
                                  QStringLiteral("%1/%2").arg(watched).arg(episodes),
                                  std::clamp(static_cast<double>(watched) / episodes, 0.0, 1.0)));
        }
        // Counted as an anime's are: "195/196 · 44,0 GB".
        if (files > 0) {
            figures.append(figure(QStringLiteral("En disco"),
                                  QStringLiteral("%1/%2 · ").arg(files).arg(episodes) +
                                      display::bytes(bytes)));
        }
        value = {{QStringLiteral("id"), it->id},
                 {QStringLiteral("title"), it->title},
                 {QStringLiteral("works"), works},
                 {QStringLiteral("slugs"), slugs},
                 {QStringLiteral("backdropUrl"), backdrop},
                 {QStringLiteral("posterUrl"), poster},
                 {QStringLiteral("files"), files},
                 {QStringLiteral("facts"), facts},
                 {QStringLiteral("figures"), figures},
                 {QStringLiteral("nextSlug"), next ? next->slug : QString()},
                 {QStringLiteral("nextText"), next_text},
                 // play, download, resume, retry; busy while its download runs or waits.
                 {QStringLiteral("nextAction"), next_action}};
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
    const auto* group = franchiseFor(id);
    QSet<QString> existing;
    if (group) {
        existing = QSet<QString>(group->members.cbegin(), group->members.cend());
    }
    QVariantList choices;
    for (const auto& entry : entries_) {
        if (existing.contains(entry.slug))
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

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{SlugRole, "slug"},         {TitleRole, "title"},       {PosterRole, "posterUrl"},
            {CaptionRole, "caption"},   {ProgressRole, "progress"}, {FreshRole, "fresh"},
            {FavoriteRole, "favorite"}, {GroupIdRole, "groupId"},   {IsGroupRole, "isGroup"},
            {DepthRole, "depth"},       {ExpandedRole, "expanded"}, {ReadyRole, "ready"}};
}

} // namespace animehub::app
