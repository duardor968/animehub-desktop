#ifndef ANIMEHUB_CORE_LIBRARY_SERVICE_H
#define ANIMEHUB_CORE_LIBRARY_SERVICE_H

#include "api/api_client.h"
#include "library_repository.h"

#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <optional>

namespace animehub::library {

// An episode's file in the Recycle Bin, and where it goes back to.
struct TrashedFile {
    double number = 0;
    QString path;
    QString trashPath; // empty when the bin did not say
    qint64 size = 0;
};

// Files moved to the Recycle Bin. `failed` ones are still on disk (in use, no permission).
struct Trashed {
    int count = 0;
    qint64 bytes = 0;
    int failed = 0;
    QList<TrashedFile> files;
};

// The library's rules on top of its storage: keeping snapshots and episode lists in sync with
// the API, manual watch-state edits, and noticing episodes that appear while AnimeHub runs.
class LibraryService final : public QObject {
    Q_OBJECT

public:
    // Moving a file to the Recycle Bin (where it went, nothing when it stays) and back; tests
    // put something harmless in their place.
    using MoveToTrash = std::function<std::optional<QString>(const QString& path)>;
    using RestoreFromTrash = std::function<bool(const QString& trash_path, const QString& path)>;

    LibraryService(LibraryRepository& repository, api::ApiClient& api, QObject* parent = nullptr,
                   MoveToTrash move_to_trash = {}, RestoreFromTrash restore_from_trash = {});

    [[nodiscard]] LibraryRepository& repository() const {
        return repository_;
    }

    Stored<void> add(const AnimeRecord& snapshot);
    Stored<void> remove(const QString& slug);
    Stored<void> archive(const QString& slug, bool archived);
    Stored<void> setFlag(const QString& slug, Flag flag, bool value);
    // Remembers the metadata of an anime seen in the catalog without adding it.
    Stored<void> remember(const AnimeRecord& snapshot);

    Stored<void> markWatched(const QString& slug, double number);
    Stored<void> markUnwatched(const QString& slug, double number);
    Stored<void> markWatching(const QString& slug, double number, qint64 position_ms);
    Stored<void> markWatchedThrough(const QString& slug, double number);
    Stored<void> markAllUnwatched(const QString& slug);
    Stored<void> seen(const QString& slug); // the user looked at its new episodes

    // Moves the files of the watched episodes, or of `numbers`, to the Recycle Bin; their
    // episodes no longer have a file. Files already gone only lose their path.
    Stored<Trashed> trashWatched(const QString& slug);
    Stored<Trashed> trashFiles(const QString& slug, const QList<double>& numbers);
    // Brings trashed files back to their episodes; says how many came back.
    Stored<int> restore(const QString& slug, const QList<TrashedFile>& files);

    // Refreshes metadata and episodes from the API, and the franchises a followed anime grows
    // into. With `flag_new`, episodes unknown so far are new arrivals (the watcher); otherwise
    // they are simply stored (first sync).
    void refresh(const QString& slug, bool flag_new);
    // The same with the anime's detail already fetched by its page; looking at an anime never
    // adds it, or anything related to it, to the library.
    void refresh(const api::AnimeDetail& detail, bool flag_new);
    [[nodiscard]] bool isRefreshing(const QString& slug) const {
        return refreshing_.contains(slug);
    }

    // Starts periodic checks for new episodes of the library's airing anime.
    void startWatching();
    void checkNow();

    // Forgets files deleted outside AnimeHub.
    void verifyFiles();
    // Something outside the service (playback, a finished download) changed this anime.
    void announce(const QString& slug);

signals:
    void animeChanged(const QString& slug);
    void listChanged();
    void refreshFailed(const QString& slug, const QString& message);
    void newEpisodes(const QString& slug, const QList<double>& numbers);
    void newFranchiseMembers(const QStringList& slugs);

private:
    void store(const api::AnimeDetail& detail, bool flag_new, bool discover);
    void fetchEpisodes(const QString& slug, int page, bool flag_new, QList<double> added);
    void finishRefresh(const QString& slug, bool flag_new, const QList<double>& added);
    void sweep();
    Stored<void> changed(Stored<void> result, const QString& slug, bool list = false);
    Stored<Trashed> trash(const QString& slug,
                          const std::function<bool(const EpisodeRecord&)>& chosen,
                          bool skip_shared = false);

    LibraryRepository& repository_;
    api::ApiClient& api_;
    MoveToTrash move_to_trash_;
    RestoreFromTrash restore_from_trash_;
    QSet<QString> refreshing_;
    QTimer recent_timer_;
    QTimer sweep_timer_;
    qsizetype finished_sweep_offset_ = 0;
};

} // namespace animehub::library

#endif // ANIMEHUB_CORE_LIBRARY_SERVICE_H
