#include "library_repository.h"

#include <QSqlRecord>
#include <QTimeZone>

namespace animehub::library {
namespace {

using storage::nowMs;
using storage::StorageError;

QVariant nullable(const QDateTime& value) {
    return value.isValid() ? QVariant(value.toMSecsSinceEpoch())
                           : QVariant(QMetaType::fromType<qint64>());
}

QVariant nullable(std::optional<int> value) {
    return value ? QVariant(*value) : QVariant(QMetaType::fromType<int>());
}

QDateTime timeAt(const QVariant& value) {
    return value.isNull() ? QDateTime()
                          : QDateTime::fromMSecsSinceEpoch(value.toLongLong(), QTimeZone::UTC);
}

std::optional<int> optionalInt(const QVariant& value) {
    return value.isNull() ? std::nullopt : std::optional<int>(value.toInt());
}

constexpr auto kAnimeColumns =
    "slug, title, alternative_title, synopsis, poster_url, backdrop_url, category, status, "
    "episode_count, genres, start_year, in_library, added_at, favorite, notify, auto_download, "
    "last_activity_at, episodes_synced_at";

AnimeRecord readAnime(const QSqlQuery& query) {
    AnimeRecord record;
    record.slug = query.value(0).toString();
    record.title = query.value(1).toString();
    record.alternativeTitle = query.value(2).toString();
    record.synopsis = query.value(3).toString();
    record.posterUrl = QUrl(query.value(4).toString());
    record.backdropUrl = QUrl(query.value(5).toString());
    record.category = query.value(6).toString();
    record.status = api::statusFromKey(query.value(7).toString());
    record.episodeCount = optionalInt(query.value(8));
    record.genres = query.value(9).toString();
    record.startYear = optionalInt(query.value(10));
    record.inLibrary = query.value(11).toBool();
    record.addedAt = timeAt(query.value(12));
    record.favorite = query.value(13).toBool();
    record.notify = query.value(14).toBool();
    record.autoDownload = query.value(15).toBool();
    record.lastActivityAt = timeAt(query.value(16));
    record.episodesSyncedAt = timeAt(query.value(17));
    return record;
}

EpisodeRecord readEpisode(const QSqlQuery& query) {
    EpisodeRecord record;
    record.number = query.value(0).toDouble();
    record.title = query.value(1).toString();
    record.imageUrl = QUrl(query.value(2).toString());
    record.publishedAt = timeAt(query.value(3));
    record.isNew = query.value(4).toBool();
    record.state = static_cast<WatchState>(std::clamp(query.value(5).toInt(), 0, 2));
    record.positionMs = query.value(6).toLongLong();
    record.durationMs = query.value(7).toLongLong();
    record.stateAt = timeAt(query.value(8));
    record.filePath = query.value(9).toString();
    record.fileSize = query.value(10).toLongLong();
    return record;
}

constexpr auto kEpisodeColumns = "number, title, image_url, published_at, is_new, state, "
                                 "position_ms, duration_ms, state_at, file_path, file_size";

QString columnFor(Flag flag) {
    switch (flag) {
    case Flag::Favorite:
        return QStringLiteral("favorite");
    case Flag::Notify:
        return QStringLiteral("notify");
    case Flag::AutoDownload:
        return QStringLiteral("auto_download");
    }
    return {};
}

} // namespace

LibraryRepository::LibraryRepository(storage::Database& database) : database_(database) {}

Stored<void> LibraryRepository::saveSnapshot(const AnimeRecord& s) {
    const auto result =
        database_.run(QStringLiteral(R"sql(
INSERT INTO anime (slug, title, alternative_title, synopsis, poster_url, backdrop_url, category,
                   status, episode_count, genres, start_year, refreshed_at)
VALUES (?, ?, COALESCE(?, ''), COALESCE(?, ''), COALESCE(?, ''), COALESCE(?, ''),
        COALESCE(?, ''), ?, ?, COALESCE(?, ''), ?, ?)
ON CONFLICT(slug) DO UPDATE SET
    title = excluded.title,
    alternative_title = COALESCE(NULLIF(excluded.alternative_title, ''), alternative_title),
    synopsis = COALESCE(NULLIF(excluded.synopsis, ''), synopsis),
    poster_url = COALESCE(NULLIF(excluded.poster_url, ''), poster_url),
    backdrop_url = COALESCE(NULLIF(excluded.backdrop_url, ''), backdrop_url),
    category = COALESCE(NULLIF(excluded.category, ''), category),
    status = excluded.status,
    episode_count = COALESCE(excluded.episode_count, episode_count),
    genres = COALESCE(NULLIF(excluded.genres, ''), genres),
    start_year = COALESCE(excluded.start_year, start_year),
    refreshed_at = excluded.refreshed_at
)sql"),
                      {s.slug, s.title, s.alternativeTitle, s.synopsis, s.posterUrl.toString(),
                       s.backdropUrl.toString(), s.category, api::statusKey(s.status),
                       nullable(s.episodeCount), s.genres, nullable(s.startYear), nowMs()});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::setInLibrary(const QString& slug, bool in_library) {
    const auto result =
        in_library
            ? database_.run(QStringLiteral("UPDATE anime SET in_library = 1, added_at = COALESCE("
                                           "CASE WHEN in_library = 1 THEN added_at END, ?) "
                                           "WHERE slug = ?"),
                            {nowMs(), slug})
            : database_.run(QStringLiteral("UPDATE anime SET in_library = 0, auto_download = 0, "
                                           "added_at = NULL WHERE slug = ?"),
                            {slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->numRowsAffected() == 0) {
        return std::unexpected(StorageError{QStringLiteral("unknown anime %1").arg(slug)});
    }
    return {};
}

Stored<void> LibraryRepository::setFlag(const QString& slug, Flag flag, bool value) {
    const auto result =
        database_.run(QStringLiteral("UPDATE anime SET %1 = ? WHERE slug = ?").arg(columnFor(flag)),
                      {value ? 1 : 0, slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->numRowsAffected() == 0) {
        return std::unexpected(StorageError{QStringLiteral("unknown anime %1").arg(slug)});
    }
    return {};
}

Stored<std::optional<AnimeRecord>> LibraryRepository::anime(const QString& slug) const {
    auto query = database_.run(
        QStringLiteral("SELECT %1 FROM anime WHERE slug = ?").arg(QLatin1StringView(kAnimeColumns)),
        {slug});
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        return std::optional<AnimeRecord>();
    }
    return std::optional<AnimeRecord>(readAnime(*query));
}

Stored<LibraryEntry> LibraryRepository::withProgress(LibraryEntry entry) const {
    auto counts = database_.run(QStringLiteral(R"sql(
SELECT COUNT(*),
       COALESCE(SUM(state = 2), 0),
       COALESCE(SUM(file_path IS NOT NULL), 0),
       COALESCE(SUM(is_new), 0),
       MAX(CASE WHEN state = 2 THEN number END),
       COALESCE(SUM(CASE WHEN file_path IS NOT NULL THEN file_size END), 0)
FROM episode WHERE slug = ?
)sql"),
                                {entry.slug});
    if (!counts) {
        return std::unexpected(counts.error());
    }
    counts->next();
    entry.knownEpisodes = counts->value(0).toInt();
    entry.watchedEpisodes = counts->value(1).toInt();
    entry.downloadedEpisodes = counts->value(2).toInt();
    entry.newEpisodes = counts->value(3).toInt();
    const auto last_watched = counts->value(4);
    entry.downloadedBytes = counts->value(5).toLongLong();

    auto watching = database_.run(QStringLiteral("SELECT number, position_ms, duration_ms FROM "
                                                 "episode WHERE slug = ? AND state = 1 "
                                                 "ORDER BY state_at DESC LIMIT 1"),
                                  {entry.slug});
    if (!watching) {
        return std::unexpected(watching.error());
    }
    if (watching->next()) {
        entry.resumeNumber = watching->value(0).toDouble();
        entry.resumePositionMs = watching->value(1).toLongLong();
        entry.resumeDurationMs = watching->value(2).toLongLong();
        return entry;
    }
    auto next = database_.run(QStringLiteral("SELECT MIN(number) FROM episode WHERE slug = ? "
                                             "AND state = 0 AND number > ?"),
                              {entry.slug, last_watched.isNull() ? -1.0 : last_watched.toDouble()});
    if (!next) {
        return std::unexpected(next.error());
    }
    if (next->next() && !next->value(0).isNull()) {
        entry.resumeNumber = next->value(0).toDouble();
    }
    return entry;
}

Stored<QList<LibraryEntry>> LibraryRepository::entries() const {
    auto query = database_.run(QStringLiteral("SELECT %1 FROM anime WHERE in_library = 1 "
                                              "ORDER BY title COLLATE NOCASE")
                                   .arg(QLatin1StringView(kAnimeColumns)));
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<LibraryEntry> entries;
    while (query->next()) {
        LibraryEntry entry;
        static_cast<AnimeRecord&>(entry) = readAnime(*query);
        auto complete = withProgress(std::move(entry));
        if (!complete) {
            return std::unexpected(complete.error());
        }
        entries.append(std::move(*complete));
    }
    return entries;
}

Stored<std::optional<LibraryEntry>> LibraryRepository::entry(const QString& slug) const {
    auto record = anime(slug);
    if (!record) {
        return std::unexpected(record.error());
    }
    if (!*record) {
        return std::optional<LibraryEntry>();
    }
    LibraryEntry entry;
    static_cast<AnimeRecord&>(entry) = **record;
    auto complete = withProgress(std::move(entry));
    if (!complete) {
        return std::unexpected(complete.error());
    }
    return std::optional<LibraryEntry>(std::move(*complete));
}

Stored<QList<double>> LibraryRepository::syncEpisodes(const QString& slug,
                                                      const QList<api::Episode>& episodes,
                                                      bool flag_new) {
    QList<double> added;
    auto stored = database_.transaction([&]() -> Stored<void> {
        const auto now = nowMs();
        for (const auto& episode : episodes) {
            auto exists =
                database_.run(QStringLiteral("SELECT 1 FROM episode WHERE slug = ? AND number = ?"),
                              {slug, episode.number});
            if (!exists) {
                return std::unexpected(exists.error());
            }
            const bool known = exists->next();
            exists->finish();
            auto written =
                database_.run(QStringLiteral(R"sql(
INSERT INTO episode (slug, number, title, image_url, published_at, known_at, is_new)
VALUES (?, ?, COALESCE(?, ''), COALESCE(?, ''), ?, ?, ?)
ON CONFLICT(slug, number) DO UPDATE SET
    title = excluded.title,
    image_url = excluded.image_url,
    published_at = COALESCE(excluded.published_at, published_at)
)sql"),
                              {slug, episode.number, episode.title, episode.imageUrl.toString(),
                               nullable(episode.publishedAt), now, flag_new && !known ? 1 : 0});
            if (!written) {
                return std::unexpected(written.error());
            }
            if (!known) {
                added.append(episode.number);
            }
        }
        auto synced = database_.run(
            QStringLiteral("UPDATE anime SET episodes_synced_at = ? WHERE slug = ?"), {now, slug});
        if (!synced) {
            return std::unexpected(synced.error());
        }
        return {};
    });
    if (!stored) {
        return std::unexpected(stored.error());
    }
    return added;
}

Stored<QList<EpisodeRecord>> LibraryRepository::episodes(const QString& slug) const {
    auto query = database_.run(QStringLiteral("SELECT %1 FROM episode WHERE slug = ? ORDER BY "
                                              "number")
                                   .arg(QLatin1StringView(kEpisodeColumns)),
                               {slug});
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<EpisodeRecord> episodes;
    while (query->next()) {
        episodes.append(readEpisode(*query));
    }
    return episodes;
}

Stored<std::optional<EpisodeRecord>> LibraryRepository::episode(const QString& slug,
                                                                double number) const {
    auto query = database_.run(QStringLiteral("SELECT %1 FROM episode WHERE slug = ? AND "
                                              "number = ?")
                                   .arg(QLatin1StringView(kEpisodeColumns)),
                               {slug, number});
    if (!query) {
        return std::unexpected(query.error());
    }
    if (!query->next()) {
        return std::optional<EpisodeRecord>();
    }
    return std::optional<EpisodeRecord>(readEpisode(*query));
}

Stored<void> LibraryRepository::ensureEpisode(const QString& slug, double number) {
    auto result = database_.run(QStringLiteral("INSERT OR IGNORE INTO episode (slug, number, "
                                               "known_at) VALUES (?, ?, ?)"),
                                {slug, number, nowMs()});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::touch(const QString& slug) {
    auto result = database_.run(
        QStringLiteral("UPDATE anime SET last_activity_at = ? WHERE slug = ?"), {nowMs(), slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::setWatchState(const QString& slug, double number, WatchState state,
                                              qint64 position_ms, qint64 duration_ms) {
    return database_.transaction([&]() -> Stored<void> {
        if (auto ensured = ensureEpisode(slug, number); !ensured) {
            return ensured;
        }
        const bool watching = state == WatchState::Watching;
        auto result = database_.run(
            QStringLiteral("UPDATE episode SET state = ?, position_ms = ?, duration_ms = "
                           "CASE WHEN ? > 0 THEN ? ELSE duration_ms END, state_at = ?, is_new = 0 "
                           "WHERE slug = ? AND number = ?"),
            {static_cast<int>(state), watching ? std::max<qint64>(position_ms, 0) : 0, duration_ms,
             duration_ms, nowMs(), slug, number});
        if (!result) {
            return std::unexpected(result.error());
        }
        if (state == WatchState::Watching) {
            // Only one episode of an anime is "in progress" at a time.
            auto others = database_.run(
                QStringLiteral("UPDATE episode SET state = 0, position_ms = 0 WHERE slug = ? "
                               "AND state = 1 AND number <> ?"),
                {slug, number});
            if (!others) {
                return std::unexpected(others.error());
            }
        }
        return touch(slug);
    });
}

Stored<void> LibraryRepository::setWatchedThrough(const QString& slug, double number) {
    return database_.transaction([&]() -> Stored<void> {
        if (auto ensured = ensureEpisode(slug, number); !ensured) {
            return ensured;
        }
        auto result = database_.run(
            QStringLiteral("UPDATE episode SET state = 2, position_ms = 0, state_at = ?, "
                           "is_new = 0 WHERE slug = ? AND number <= ? AND state <> 2"),
            {nowMs(), slug, number});
        if (!result) {
            return std::unexpected(result.error());
        }
        return touch(slug);
    });
}

Stored<void> LibraryRepository::setAllUnwatched(const QString& slug) {
    auto result = database_.run(QStringLiteral("UPDATE episode SET state = 0, position_ms = 0, "
                                               "state_at = ? WHERE slug = ?"),
                                {nowMs(), slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::setEpisodeFile(const QString& slug, double number,
                                               const QString& path, qint64 size) {
    return database_.transaction([&]() -> Stored<void> {
        if (auto ensured = ensureEpisode(slug, number); !ensured) {
            return ensured;
        }
        auto result = database_.run(QStringLiteral("UPDATE episode SET file_path = ?, file_size = "
                                                   "? WHERE slug = ? AND number = ?"),
                                    {path, size, slug, number});
        if (!result) {
            return std::unexpected(result.error());
        }
        return {};
    });
}

Stored<void> LibraryRepository::clearEpisodeFile(const QString& slug, double number) {
    auto result = database_.run(QStringLiteral("UPDATE episode SET file_path = NULL, file_size = "
                                               "NULL WHERE slug = ? AND number = ?"),
                                {slug, number});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<void> LibraryRepository::clearNew(const QString& slug) {
    auto result = database_.run(
        QStringLiteral("UPDATE episode SET is_new = 0 WHERE slug = ? AND is_new = 1"), {slug});
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Stored<QList<std::tuple<QString, double, QString>>> LibraryRepository::episodeFiles() const {
    auto query = database_.run(
        QStringLiteral("SELECT slug, number, file_path FROM episode WHERE file_path IS NOT NULL"));
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<std::tuple<QString, double, QString>> files;
    while (query->next()) {
        files.append(
            {query->value(0).toString(), query->value(1).toDouble(), query->value(2).toString()});
    }
    return files;
}

} // namespace animehub::library
