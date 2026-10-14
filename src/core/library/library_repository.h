#ifndef ANIMEHUB_CORE_LIBRARY_REPOSITORY_H
#define ANIMEHUB_CORE_LIBRARY_REPOSITORY_H

#include "library_types.h"
#include "storage/database.h"

#include <QList>

namespace animehub::library {

using storage::Stored;

enum class Flag { Favorite, Notify, AutoDownload };

// SQLite access for anime snapshots, the user's flags and per-episode state.
class LibraryRepository {
public:
    explicit LibraryRepository(storage::Database& database);

    // Upserts metadata; never touches the user's flags or progress. Empty fields of a lighter
    // snapshot (a catalog summary) keep what a fuller one (the detail) already stored.
    Stored<void> saveSnapshot(const AnimeRecord& snapshot);
    Stored<void> setInLibrary(const QString& slug, bool in_library);
    Stored<void> setFlag(const QString& slug, Flag flag, bool value);

    [[nodiscard]] Stored<std::optional<AnimeRecord>> anime(const QString& slug) const;
    [[nodiscard]] Stored<QList<LibraryEntry>> entries() const;
    [[nodiscard]] Stored<std::optional<LibraryEntry>> entry(const QString& slug) const;

    // Stores the episode list; returns the numbers that were not known before. With `flag_new`
    // those are marked new (the watcher found them, the user has not seen them yet).
    Stored<QList<double>> syncEpisodes(const QString& slug, const QList<api::Episode>& episodes,
                                       bool flag_new);
    [[nodiscard]] Stored<QList<EpisodeRecord>> episodes(const QString& slug) const;
    [[nodiscard]] Stored<std::optional<EpisodeRecord>> episode(const QString& slug,
                                                               double number) const;

    Stored<void> setWatchState(const QString& slug, double number, WatchState state,
                               qint64 position_ms = 0, qint64 duration_ms = 0);
    // Watched up to `number` included; later episodes stay as they are.
    Stored<void> setWatchedThrough(const QString& slug, double number);
    Stored<void> setAllUnwatched(const QString& slug);
    Stored<void> setEpisodeFile(const QString& slug, double number, const QString& path,
                                qint64 size);
    Stored<void> clearEpisodeFile(const QString& slug, double number);
    Stored<void> clearNew(const QString& slug);

    // Every stored file path, to check which still exist on disk.
    [[nodiscard]] Stored<QList<std::tuple<QString, double, QString>>> episodeFiles() const;

private:
    Stored<void> ensureEpisode(const QString& slug, double number);
    Stored<void> touch(const QString& slug);
    Stored<LibraryEntry> withProgress(LibraryEntry entry) const;

    storage::Database& database_;
};

} // namespace animehub::library

#endif // ANIMEHUB_CORE_LIBRARY_REPOSITORY_H
