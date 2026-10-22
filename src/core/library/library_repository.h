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
    [[nodiscard]] Stored<QList<LibraryEntry>> entries(bool include_archived = false) const;
    [[nodiscard]] Stored<QList<LibraryEntry>> followedEntries() const;
    // Archive is a view of the personal library, independent from follow and watch state.
    Stored<void> setArchived(const QString& slug, bool archived);
    // Clears personal library data and file mappings; never modifies physical media.
    Stored<void> removePersonalRecord(const QString& slug);
    Stored<qint64> createFranchise(const QString& title);
    Stored<qint64> createFranchise(const QString& title, const QString& initial_slug);
    // Review only a still-pending candidate. Admission preserves existing personal state;
    // new library entries follow updates, inherit the group's archive view and never download.
    Stored<void> resolveFranchiseCandidate(const QString& slug, qint64 franchise_id);
    Stored<qint64> resolveFranchiseCandidate(const QString& slug, const QString& new_title);
    Stored<void> dismissFranchiseCandidate(const QString& slug);
    [[nodiscard]] Stored<QList<FranchiseRecord>> franchises() const;
    Stored<void> setFranchiseMember(qint64 id, const QString& slug, bool included);
    // Caller resolves a unique target group and saves metadata first. Never reassigns a member,
    // revives a manual exclusion or enables downloads; ambiguous relationships remain pending.
    Stored<void> applyFranchiseRelations(qint64 id, const QStringList& slugs);
    // Updates explicit provider edges, then adds only unambiguous personal-group candidates.
    // Returns newly added slugs; never changes existing manual membership or download settings.
    Stored<QStringList> discoverFranchiseRelations(const api::AnimeDetail& detail);
    [[nodiscard]] Stored<QList<FranchiseCandidate>> pendingFranchiseCandidates() const;
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
    [[nodiscard]] Stored<QList<EpisodeLocation>> episodeLocations(const QString& slug,
                                                                  double number) const;
    Stored<void> removeEpisodeFile(const QString& slug, double number, const QString& path);
    [[nodiscard]] Stored<QList<std::tuple<QString, double>>>
    episodeFileMappings(const QString& path) const;
    // Explicit episode mapping only. Importing an unwatched duplicate cannot erase watch state.
    Stored<void> importEpisodeFile(const QString& slug, double number, const QString& path,
                                   qint64 size, bool watched);
    Stored<void> clearNew(const QString& slug);

    // Every stored file path, to check which still exist on disk.
    [[nodiscard]] Stored<QList<std::tuple<QString, double, QString>>> episodeFiles() const;

private:
    Stored<void> writeFranchiseMember(qint64 id, const QString& slug, bool included);
    Stored<void> admitFranchiseCandidate(const QString& slug, qint64 id);
    Stored<QStringList> candidateStandaloneSlugs(const QString& slug) const;
    Stored<void> ensureEpisode(const QString& slug, double number);
    Stored<void> touch(const QString& slug);
    Stored<LibraryEntry> withProgress(LibraryEntry entry) const;
    Stored<QList<LibraryEntry>> queryEntries(const QString& predicate) const;
    Stored<void> storeEpisodeFile(const QString& slug, double number, const QString& path,
                                  qint64 size);
    Stored<void> updateLegacyFile(const QString& slug, double number);
    Stored<void> pruneUnreferencedFile(const QString& path);
    Stored<void> selectBestFile(const QString& slug, EpisodeRecord& episode) const;

    storage::Database& database_;
};

} // namespace animehub::library

#endif // ANIMEHUB_CORE_LIBRARY_REPOSITORY_H
