#ifndef ANIMEHUB_CORE_LIBRARY_SERVICE_H
#define ANIMEHUB_CORE_LIBRARY_SERVICE_H

#include "api/api_client.h"
#include "library_repository.h"

#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

namespace animehub::library {

// Files moved to the Recycle Bin. `failed` ones are still on disk (in use, no permission).
struct Trashed {
    int count = 0;
    qint64 bytes = 0;
    int failed = 0;
};

// The library's rules on top of its storage: keeping snapshots and episode lists in sync with
// the API, manual watch-state edits, and noticing episodes that appear while AnimeHub runs.
class LibraryService final : public QObject {
    Q_OBJECT

public:
    // Moves a file to the Recycle Bin; tests put something harmless in its place.
    using MoveToTrash = std::function<bool(const QString& path)>;

    LibraryService(LibraryRepository& repository, api::ApiClient& api, QObject* parent = nullptr,
                   MoveToTrash move_to_trash = {});

    [[nodiscard]] LibraryRepository& repository() const {
        return repository_;
    }

    Stored<void> add(const AnimeRecord& snapshot);
    Stored<void> remove(const QString& slug);
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

    // Refreshes metadata and episodes from the API. With `flag_new`, episodes unknown so far are
    // new arrivals (the watcher); otherwise they are simply stored (first sync, browsing).
    void refresh(const QString& slug, bool flag_new);
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

private:
    void fetchEpisodes(const QString& slug, int page, bool flag_new, QList<double> added);
    void finishRefresh(const QString& slug, bool flag_new, const QList<double>& added);
    void sweep();
    Stored<void> changed(Stored<void> result, const QString& slug, bool list = false);
    Stored<Trashed> trash(const QString& slug,
                          const std::function<bool(const EpisodeRecord&)>& chosen);

    LibraryRepository& repository_;
    api::ApiClient& api_;
    MoveToTrash move_to_trash_;
    QSet<QString> refreshing_;
    QTimer recent_timer_;
    QTimer sweep_timer_;
};

} // namespace animehub::library

#endif // ANIMEHUB_CORE_LIBRARY_SERVICE_H
