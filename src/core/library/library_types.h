#ifndef ANIMEHUB_CORE_LIBRARY_TYPES_H
#define ANIMEHUB_CORE_LIBRARY_TYPES_H

#include "api/api_types.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <optional>

namespace animehub::library {

enum class WatchState { Unwatched = 0, Watching = 1, Watched = 2 };

// What the library keeps of an anime: a metadata snapshot (so it works offline) plus the
// user's own flags.
struct AnimeRecord {
    QString slug;
    QString title;
    QString alternativeTitle;
    QString synopsis;
    QUrl posterUrl;
    QUrl backdropUrl;
    QString category;
    api::AnimeStatus status = api::AnimeStatus::Unknown;
    std::optional<int> episodeCount;
    QString genres; // display names joined by ", "
    std::optional<int> startYear;

    bool inLibrary = false;
    bool favorite = false;
    bool notify = true;
    bool autoDownload = false;
    QDateTime addedAt;
    QDateTime lastActivityAt;
    QDateTime episodesSyncedAt;
};

// A library row with what the list and the home shelves need.
struct LibraryEntry : AnimeRecord {
    int knownEpisodes = 0;
    int watchedEpisodes = 0;
    int downloadedEpisodes = 0;
    qint64 downloadedBytes = 0; // what the episodes on disk take up
    int newEpisodes = 0;
    // Episode "Continuar" plays: the one in progress, else the first unwatched after the last
    // watched one.
    std::optional<double> resumeNumber;
    qint64 resumePositionMs = 0;
    qint64 resumeDurationMs = 0;

    [[nodiscard]] bool finished() const;
};

struct EpisodeRecord {
    double number = 0;
    QString title;
    QUrl imageUrl;
    QDateTime publishedAt;
    bool isNew = false;
    WatchState state = WatchState::Unwatched;
    qint64 positionMs = 0;
    qint64 durationMs = 0;
    QDateTime stateAt;
    QString filePath;
    qint64 fileSize = 0;
};

// "12", "12.5": how episode numbers are shown and written in file names.
[[nodiscard]] QString formatEpisodeNumber(double number);

// Snapshot of the API metadata worth keeping for an anime.
[[nodiscard]] AnimeRecord snapshotOf(const api::AnimeSummary& anime);
[[nodiscard]] AnimeRecord snapshotOf(const api::AnimeDetail& anime);

} // namespace animehub::library

#endif // ANIMEHUB_CORE_LIBRARY_TYPES_H
