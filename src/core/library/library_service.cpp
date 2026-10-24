#include "library_service.h"

#include "platform/trash.h"

#include <QFileInfo>

namespace animehub::library {
namespace {

using namespace std::chrono_literals;

constexpr int kEpisodesPerPage = 50;

} // namespace

LibraryService::LibraryService(LibraryRepository& repository, api::ApiClient& api, QObject* parent,
                               MoveToTrash move_to_trash, RestoreFromTrash restore_from_trash)
    : QObject(parent), repository_(repository), api_(api),
      move_to_trash_(move_to_trash ? std::move(move_to_trash) : platform::moveToTrash),
      restore_from_trash_(restore_from_trash ? std::move(restore_from_trash)
                                             : platform::restoreFromTrash) {
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
    return changed(repository_.removePersonalRecord(slug), slug, true);
}

Stored<void> LibraryService::archive(const QString& slug, bool archived) {
    return changed(repository_.setArchived(slug, archived), slug, true);
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
    return trash(
        slug, [](const EpisodeRecord& episode) { return episode.state == WatchState::Watched; },
        true);
}

Stored<Trashed> LibraryService::trashFiles(const QString& slug, const QList<double>& numbers) {
    return trash(slug, [&numbers](const EpisodeRecord& episode) {
        return numbers.contains(episode.number);
    });
}

Stored<Trashed> LibraryService::trash(const QString& slug,
                                      const std::function<bool(const EpisodeRecord&)>& chosen,
                                      bool skip_shared) {
    const auto episodes = repository_.episodes(slug);
    if (!episodes) {
        return std::unexpected(episodes.error());
    }
    // Preflight the entire request before touching disk: a physical edition may represent
    // several episodes or works, and the selection must cover every one of its mappings.
    QSet<QString> protected_paths;
    QSet<QString> checked_paths;
    for (const auto& episode : *episodes) {
        if (!chosen(episode))
            continue;
        const auto locations = repository_.episodeLocations(slug, episode.number);
        if (!locations)
            return std::unexpected(locations.error());
        for (const auto& location : *locations) {
            if (checked_paths.contains(location.path))
                continue;
            checked_paths.insert(location.path);
            const auto mappings = repository_.episodeFileMappings(location.path);
            if (!mappings)
                return std::unexpected(mappings.error());
            for (const auto& [mapped_slug, mapped_number] : *mappings) {
                const auto mapped = repository_.episode(mapped_slug, mapped_number);
                if (!mapped)
                    return std::unexpected(mapped.error());
                if (mapped_slug != slug || !*mapped || !chosen(**mapped)) {
                    if (!skip_shared) {
                        return std::unexpected(storage::StorageError{
                            QStringLiteral(
                                "El archivo %1 contiene otros episodios fuera de la selección. "
                                "Selecciona todos sus episodios para moverlo a la papelera.")
                                .arg(QFileInfo(location.path).fileName())});
                    }
                    protected_paths.insert(location.path);
                    break;
                }
            }
        }
    }
    Trashed trashed;
    std::optional<storage::StorageError> error;
    bool touched = false;
    for (const auto& episode : *episodes) {
        if (!chosen(episode))
            continue;
        const auto locations = repository_.episodeLocations(slug, episode.number);
        if (!locations)
            return std::unexpected(locations.error());
        for (const auto& location : *locations) {
            if (protected_paths.contains(location.path))
                continue;
            const QFileInfo file(location.path);
            const auto already_moved =
                std::ranges::find(trashed.files, location.path, &TrashedFile::path);
            if (already_moved != trashed.files.end()) {
                auto mapping = *already_moved;
                mapping.number = episode.number;
                trashed.files.append(std::move(mapping));
            }
            if (file.exists()) {
                const auto size = file.size();
                const auto trash_path = move_to_trash_(location.path);
                if (!trash_path) {
                    ++trashed.failed;
                    continue;
                }
                ++trashed.count;
                trashed.bytes += size;
                trashed.files.append({.number = episode.number,
                                      .path = location.path,
                                      .trashPath = *trash_path,
                                      .size = size});
            }
            touched = true;
            if (auto cleared = repository_.removeEpisodeFile(slug, episode.number, location.path);
                !cleared) {
                error = cleared.error();
                break;
            }
        }
        if (error)
            break;
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

Stored<int> LibraryService::restore(const QString& slug, const QList<TrashedFile>& files) {
    int restored = 0;
    QSet<QString> restored_paths;
    std::optional<storage::StorageError> error;
    for (const auto& file : files) {
        if (file.trashPath.isEmpty()) {
            continue;
        }
        if (!restored_paths.contains(file.path)) {
            if (!restore_from_trash_(file.trashPath, file.path))
                continue;
            restored_paths.insert(file.path);
            ++restored;
        }
        if (auto stored = repository_.setEpisodeFile(slug, file.number, file.path, file.size);
            !stored) {
            error = stored.error();
            break;
        }
    }
    if (restored > 0) {
        emit animeChanged(slug);
        emit listChanged();
    }
    if (error) {
        return std::unexpected(*error);
    }
    return restored;
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
            store(detail->value, flag_new, true);
        },
        api::CachePolicy::NetworkOnly);
}

void LibraryService::refresh(const api::AnimeDetail& detail, bool flag_new) {
    if (refreshing_.contains(detail.slug)) {
        return;
    }
    refreshing_.insert(detail.slug);
    store(detail, flag_new, false);
}

void LibraryService::store(const api::AnimeDetail& detail, bool flag_new, bool discover) {
    const auto& slug = detail.slug;
    if (!discover) {
        if (auto saved = repository_.saveSnapshot(snapshotOf(detail)); !saved) {
            qWarning("library: %s", qPrintable(saved.error().message));
        }
    } else if (const auto discovered = repository_.discoverFranchiseRelations(detail);
               !discovered) {
        qWarning("library: %s", qPrintable(discovered.error().message));
    } else {
        QStringList notify;
        for (const auto& added : *discovered) {
            emit animeChanged(added);
            const auto record = repository_.anime(added);
            if (record && *record && (*record)->notify)
                notify.append(added);
        }
        if (!notify.isEmpty())
            emit newFranchiseMembers(notify);
        emit listChanged(); // franchises and proposals may have changed
    }
    emit animeChanged(slug);
    // Episodes arrive in ascending pages of fifty; a known list only needs its tail.
    const auto record = repository_.anime(slug);
    const bool synced = record && *record && (*record)->episodesSyncedAt.isValid();
    const auto known = repository_.episodes(slug);
    const int first_page =
        synced && known ? std::max(1, static_cast<int>(known->size()) / kEpisodesPerPage) : 1;
    fetchEpisodes(slug, first_page, flag_new && synced, {});
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
            const auto entries = repository_.followedEntries();
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
    const auto entries = repository_.followedEntries();
    if (!entries) {
        return;
    }
    int delay = 0;
    QStringList grouped;
    const auto groups = repository_.franchises();
    if (groups) {
        for (const auto& group : *groups)
            grouped.append(group.members);
    }
    QStringList finished;
    for (const auto& entry : *entries) {
        if (entry.status != api::AnimeStatus::Airing &&
            entry.status != api::AnimeStatus::Upcoming) {
            if (grouped.contains(entry.slug))
                finished.append(entry.slug);
            continue;
        }
        // Spread the requests out; the API is shared with everyone.
        QTimer::singleShot(std::chrono::milliseconds(delay), this,
                           [this, slug = entry.slug] { refresh(slug, true); });
        delay += 3000;
    }
    // Bound catalog checks for finished franchises; rotate so a large library is not starved.
    const auto count = std::min<qsizetype>(finished.size(), 8);
    for (qsizetype index = 0; index < count; ++index) {
        const auto slug = finished.at((finished_sweep_offset_ + index) % finished.size());
        QTimer::singleShot(std::chrono::milliseconds(delay), this, [this, slug] {
            const auto current = repository_.anime(slug);
            if (current && *current && (*current)->inLibrary && (*current)->notify)
                refresh(slug, true);
        });
        delay += 3000;
    }
    if (!finished.isEmpty())
        finished_sweep_offset_ = (finished_sweep_offset_ + count) % finished.size();
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
        if (!QFileInfo::exists(path) && repository_.removeEpisodeFile(slug, number, path)) {
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
