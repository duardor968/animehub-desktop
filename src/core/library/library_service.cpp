#include "library_service.h"

#include <QFile>
#include <QFileInfo>

namespace animehub::library {
namespace {

using namespace std::chrono_literals;

constexpr int kEpisodesPerPage = 50;

} // namespace

LibraryService::LibraryService(LibraryRepository& repository, api::ApiClient& api, QObject* parent,
                               MoveToTrash move_to_trash)
    : QObject(parent), repository_(repository), api_(api),
      move_to_trash_(move_to_trash ? std::move(move_to_trash)
                                   : [](const QString& path) { return QFile::moveToTrash(path); }) {
    recent_timer_.setInterval(20min);
    connect(&recent_timer_, &QTimer::timeout, this, &LibraryService::checkNow);
    sweep_timer_.setInterval(6h);
    connect(&sweep_timer_, &QTimer::timeout, this, &LibraryService::sweep);
}

Stored<void> LibraryService::changed(Stored<void> result, const QString& slug, bool list) {
    if (result) {
        emit animeChanged(slug);
        if (list) {
            emit listChanged();
        }
    }
    return result;
}

Stored<void> LibraryService::remember(const AnimeRecord& snapshot) {
    return changed(repository_.saveSnapshot(snapshot), snapshot.slug);
}

Stored<void> LibraryService::add(const AnimeRecord& snapshot) {
    if (auto saved = repository_.saveSnapshot(snapshot); !saved) {
        return saved;
    }
    auto added = changed(repository_.setInLibrary(snapshot.slug, true), snapshot.slug, true);
    if (added) {
        refresh(snapshot.slug, false);
    }
    return added;
}

Stored<void> LibraryService::remove(const QString& slug) {
    return changed(repository_.setInLibrary(slug, false), slug, true);
}

Stored<void> LibraryService::setFlag(const QString& slug, Flag flag, bool value) {
    auto record = repository_.anime(slug);
    if (!record) {
        return std::unexpected(record.error());
    }
    const bool joins = value && *record && !(*record)->inLibrary;
    if (joins) { // marking something is a way of keeping it
        if (auto added = repository_.setInLibrary(slug, true); !added) {
            return added;
        }
    }
    return changed(repository_.setFlag(slug, flag, value), slug, joins);
}

Stored<void> LibraryService::markWatched(const QString& slug, double number) {
    return changed(repository_.setWatchState(slug, number, WatchState::Watched), slug, true);
}

Stored<void> LibraryService::markUnwatched(const QString& slug, double number) {
    return changed(repository_.setWatchState(slug, number, WatchState::Unwatched), slug, true);
}

Stored<void> LibraryService::markWatching(const QString& slug, double number, qint64 position_ms) {
    const auto episode = repository_.episode(slug, number);
    const qint64 duration = episode && *episode ? (*episode)->durationMs : 0;
    return changed(
        repository_.setWatchState(slug, number, WatchState::Watching, position_ms, duration), slug,
        true);
}

Stored<void> LibraryService::markWatchedThrough(const QString& slug, double number) {
    return changed(repository_.setWatchedThrough(slug, number), slug, true);
}

Stored<void> LibraryService::markAllUnwatched(const QString& slug) {
    return changed(repository_.setAllUnwatched(slug), slug, true);
}

Stored<void> LibraryService::seen(const QString& slug) {
    return changed(repository_.clearNew(slug), slug, true);
}

Stored<Trashed> LibraryService::trashWatched(const QString& slug) {
    return trash(slug,
                 [](const EpisodeRecord& episode) { return episode.state == WatchState::Watched; });
}

Stored<Trashed> LibraryService::trashFiles(const QString& slug, const QList<double>& numbers) {
    return trash(slug, [&numbers](const EpisodeRecord& episode) {
        return numbers.contains(episode.number);
    });
}

Stored<Trashed> LibraryService::trash(const QString& slug,
                                      const std::function<bool(const EpisodeRecord&)>& chosen) {
    const auto episodes = repository_.episodes(slug);
    if (!episodes) {
        return std::unexpected(episodes.error());
    }
    Trashed trashed;
    std::optional<storage::StorageError> error;
    bool touched = false;
    for (const auto& episode : *episodes) {
        if (episode.filePath.isEmpty() || !chosen(episode)) {
            continue;
        }
        const QFileInfo file(episode.filePath);
        if (file.exists()) {
            const auto size = file.size();
            if (!move_to_trash_(episode.filePath)) {
                ++trashed.failed;
                continue;
            }
            ++trashed.count;
            trashed.bytes += size;
        }
        touched = true;
        if (auto cleared = repository_.clearEpisodeFile(slug, episode.number); !cleared) {
            error = cleared.error();
            break;
        }
    }
    if (touched) {
        emit animeChanged(slug);
        emit listChanged();
    }
    if (error) {
        return std::unexpected(*error);
    }
    return trashed;
}

void LibraryService::refresh(const QString& slug, bool flag_new) {
    if (refreshing_.contains(slug)) {
        return;
    }
    refreshing_.insert(slug);
    api_.anime(
        slug, this,
        [this, slug, flag_new](const api::Result<api::AnimeDetail>& detail) {
            if (!detail) {
                refreshing_.remove(slug);
                emit refreshFailed(slug, detail.error().message);
                return;
            }
            if (auto saved = repository_.saveSnapshot(snapshotOf(detail->value)); !saved) {
                qWarning("library: %s", qPrintable(saved.error().message));
            }
            emit animeChanged(slug);
            // Episodes arrive in ascending pages of fifty; a known list only needs its tail.
            const auto record = repository_.anime(slug);
            const bool synced = record && *record && (*record)->episodesSyncedAt.isValid();
            const auto known = repository_.episodes(slug);
            const int first_page =
                synced && known ? std::max(1, static_cast<int>(known->size()) / kEpisodesPerPage)
                                : 1;
            fetchEpisodes(slug, first_page, flag_new && synced, {});
        },
        api::CachePolicy::NetworkOnly);
}

void LibraryService::fetchEpisodes(const QString& slug, int page, bool flag_new,
                                   QList<double> added) {
    api_.episodes(
        slug, page, this,
        [this, slug, page, flag_new, added](const api::Result<api::EpisodePage>& result) mutable {
            if (!result) {
                refreshing_.remove(slug);
                emit animeChanged(slug);
                emit refreshFailed(slug, result.error().message);
                return;
            }
            auto stored = repository_.syncEpisodes(slug, result->value.items, flag_new);
            if (!stored) {
                refreshing_.remove(slug);
                emit refreshFailed(slug, stored.error().message);
                return;
            }
            added += *stored;
            const int total = result->value.totalPages;
            if (page < total) {
                emit animeChanged(slug); // the first pages show while the rest arrive
                fetchEpisodes(slug, page + 1, flag_new, added);
                return;
            }
            finishRefresh(slug, flag_new, added);
        },
        api::CachePolicy::NetworkOnly);
}

void LibraryService::finishRefresh(const QString& slug, bool flag_new, const QList<double>& added) {
    refreshing_.remove(slug);
    emit animeChanged(slug);
    emit listChanged();
    if (flag_new && !added.isEmpty()) {
        emit newEpisodes(slug, added);
    }
}

void LibraryService::startWatching() {
    recent_timer_.start();
    sweep_timer_.start();
    QTimer::singleShot(15s, this, &LibraryService::checkNow);
    QTimer::singleShot(2min, this, &LibraryService::sweep);
}

void LibraryService::checkNow() {
    // The home's recent episodes cover the whole catalog in one request.
    api_.home(
        this,
        [this](const api::Result<api::Home>& home) {
            if (!home) {
                return;
            }
            const auto entries = repository_.entries();
            if (!entries) {
                return;
            }
            for (const auto& recent : home->value.recentEpisodes) {
                const auto entry =
                    std::ranges::find(*entries, recent.anime.slug, &LibraryEntry::slug);
                if (entry == entries->end() || (!entry->notify && !entry->autoDownload)) {
                    continue;
                }
                const auto known = repository_.episode(entry->slug, recent.episode.number);
                if (known && !*known) {
                    refresh(entry->slug, true);
                }
            }
        },
        api::CachePolicy::NetworkOnly);
}

void LibraryService::sweep() {
    const auto entries = repository_.entries();
    if (!entries) {
        return;
    }
    int delay = 0;
    for (const auto& entry : *entries) {
        if (entry.status != api::AnimeStatus::Airing &&
            entry.status != api::AnimeStatus::Upcoming) {
            continue;
        }
        // Spread the requests out; the API is shared with everyone.
        QTimer::singleShot(std::chrono::milliseconds(delay), this,
                           [this, slug = entry.slug] { refresh(slug, true); });
        delay += 3000;
    }
}

void LibraryService::announce(const QString& slug) {
    emit animeChanged(slug);
    emit listChanged();
}

void LibraryService::verifyFiles() {
    const auto files = repository_.episodeFiles();
    if (!files) {
        return;
    }
    QSet<QString> touched;
    for (const auto& [slug, number, path] : *files) {
        if (!QFileInfo::exists(path) && repository_.clearEpisodeFile(slug, number)) {
            touched.insert(slug);
        }
    }
    for (const auto& slug : touched) {
        emit animeChanged(slug);
    }
    if (!touched.isEmpty()) {
        emit listChanged();
    }
}

} // namespace animehub::library
